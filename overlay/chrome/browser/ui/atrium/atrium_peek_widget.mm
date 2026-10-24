// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_peek_widget.mm
#include "chrome/browser/ui/atrium/atrium_peek_widget.h"

#import <QuartzCore/QuartzCore.h>
#import <objc/runtime.h>

#include <algorithm>
#include <cmath>
#include <optional>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/strings/stringprintf.h"  // pane-claim instrumentation
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_confirm_quit_panel_style.h"
#include "chrome/browser/ui/atrium/atrium_confirm_to_quit.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_map.h"
#import "chrome/browser/ui/atrium/atrium_surface_key_route_mac.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/navigator/browser_navigator.h"
#include "chrome/browser/ui/navigator/browser_navigator_params.h"
#include "chrome/common/pref_names.h"
#include "components/input/native_web_keyboard_event.h"
#include "components/prefs/pref_service.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "base/types/expected.h"
#include "components/viz/common/frame_sinks/copy_output_result.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/base_window.h"
#import "ui/base/cocoa/command_dispatcher.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/metadata/metadata_impl_macros.h"
#include "ui/compositor/layer.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/insets.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/views/background.h"
#include "ui/views/border.h"
#include "ui/views/controls/label.h"
#include "ui/views/controls/throbber.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/layout/box_layout.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

// The peek card is a borderless, ACTIVATABLE child window (see PeekWidget's
// class doc): it becomes the key window while open, and unlike the palette
// its own content is a real WebView showing the previewed page - a genuine
// RenderWidgetHostViewCocoa, not just Swift-drawn chrome. Left with no
// CommandDispatcher delegate, an unhandled key equivalent (chiefly Cmd+Q,
// which is a main-menu accelerator, not a browser IDC one) is claimed by
// that WebView and handed to the renderer asynchronously; if the renderer
// declines it, the redispatch either gets silently dropped or bubbles into
// the PARENT window's own web content, whose identical async round-trip eats
// it there instead - the peek reproduces the exact class of
// bug CommandBarDispatcherDelegate already fixes for the palette (see
// atrium_command_bar_widget.mm's doc), and is in fact more exposed to it
// (the palette has no WebView of its own, so it only ever needed the
// bubble-to-parent case handled). This delegate severs the bubble the same
// way: Atrium's own combos still execute here against the parent controller,
// preserving today's Cmd+T/Cmd+S-while-peeking behavior; everything else -
// Cmd+Q above all - is passed to the main menu directly instead of ever
// reaching a WebView's async redispatch.
@interface AtriumPeekCommandDispatcherDelegate
    : NSObject <CommandDispatcherDelegate> {
  // The browser window dies before this delegate's NSWindow deallocs on quit.
  base::WeakPtr<atrium::AtriumBrowserWindow> _parent;
}
- (instancetype)initWithParent:(atrium::AtriumBrowserWindow*)parent;
@end

@implementation AtriumPeekCommandDispatcherDelegate

- (instancetype)initWithParent:(atrium::AtriumBrowserWindow*)parent {
  if ((self = [super init])) {
    _parent = parent->GetWeakPtr();
  }
  return self;
}

- (ui::PerformKeyEquivalentResult)prePerformKeyEquivalent:(NSEvent*)event
                                                   window:(NSWindow*)window {
  const std::optional<atrium::NativeHotkeyAction> action =
      atrium::NativeHotkeyMap::Get()->MatchAction(event);
  switch (atrium::RouteForSurfaceKey(
      atrium::SurfaceKeyInputsForEvent(event, action.has_value()))) {
    case atrium::SurfaceKeyRoute::kMainMenu:
      // the quit leaves on the way IN. postPerform below only runs
      // when this window's view hierarchy declines the event, and the card's
      // WebView never does - it claims the chord and answers its renderer
      // asynchronously. The palette hid this behind an empty startup scrim;
      // the card has a live page from its first frame, so there was no state
      // in which its "Cmd+Q above all" promise held.
      VLOG(1) << "Atrium: peek passing key-equivalent to main menu (keyCode "
              << event.keyCode << ")";
      return ui::PerformKeyEquivalentResult::kPassToMainMenu;
    case atrium::SurfaceKeyRoute::kWebContent:
      // Not an Atrium combo: let the card's WebView take it first (e.g. page
      // shortcuts, Cmd+C/V/A); postPerform below routes whatever it declines
      // to the main menu instead of bubbling into the parent's web content.
      return ui::PerformKeyEquivalentResult::kUnhandled;
    case atrium::SurfaceKeyRoute::kDropRepeat:
      // Match the browser/palette delegates' held-key guard so a held Cmd+T/
      // Cmd+S does not strobe the palette/sidebar while peeking.
      return ui::PerformKeyEquivalentResult::kDrop;
    case atrium::SurfaceKeyRoute::kAtriumAction:
      break;
  }
  if (!_parent) {
    return ui::PerformKeyEquivalentResult::kUnhandled;
  }
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForNativeWindow(
          _parent->widget()->GetNativeWindow());
  if (controller && atrium::ExecuteNativeHotkeyAction(controller, *action)) {
    return ui::PerformKeyEquivalentResult::kHandled;
  }
  return ui::PerformKeyEquivalentResult::kUnhandled;
}

- (ui::PerformKeyEquivalentResult)postPerformKeyEquivalent:(NSEvent*)event
                                                    window:(NSWindow*)window
                                              isRedispatch:(BOOL)isRedispatch {
  // The card's own WebView has already declined this equivalent (or the
  // redispatch round-trip just completed). Route it to the app main menu
  // instead of bubbling into the parent window's web content, whose async
  // renderer round-trip eats it - see this class's doc above.
  VLOG(1) << "Atrium: peek passing key-equivalent to main menu (keyCode "
          << event.keyCode << ")";
  return ui::PerformKeyEquivalentResult::kPassToMainMenu;
}

@end

namespace atrium {

namespace {

// Associated-object key: keeps the dispatcher delegate alive for the peek
// window's lifetime (CommandDispatcher holds its delegate weakly) - same
// pattern as CommandBarWidget's kCommandBarDispatcherDelegateKey.
const char kPeekDispatcherDelegateKey = '\0';

// See CommandBarWidget's kFocusBounceGuard - identical risk, identical fix.
constexpr base::TimeDelta kFocusBounceGuard = base::Milliseconds(250);

// Opening a tab from a link on the card takes key off the card, which the
// focus-loss dismiss would otherwise read as the user clicking away. Wider
// than the bounce guard: the steal can trail the insert by a frame or two.
// The width is empirical, measured against the VM under four parallel jobs.
constexpr base::TimeDelta kSelfOpenedTabGuard = base::Seconds(2);

// Card geometry: the card fills nearly the
// entire web-content pane, inset by a small uniform margin so the real page
// and the sidebar both stay visibly peeking out around it - see
// PeekWidget::ApplyParentBounds and AtriumBrowserWindow::
// GetWebContentBoundsInScreen.
constexpr int kPeekMargin = 8;

// Pop-out polish: the card's width as a fraction of the
// web-content pane's width - narrower than a near-full-width card so
// the underlying page (now dimmed by the scrim below) visibly peeks out on
// both sides instead of only a thin kPeekMargin strip, matching Arc's own
// pop-out preview card. Height keeps the near-full-pane treatment
// (kPeekMargin top/bottom) - only the width axis narrows here.
constexpr float kPeekWidthFraction = 0.8f;

// Rounded corners on the card - clips the chrome strip (top
// corners) and WebView/loading placeholder (bottom corners) identically via
// a single masksToBounds on the window's content layer, their common
// ancestor - see Show()'s comment.
constexpr CGFloat kPeekCornerRadius = 14;

// The gap between the card's right edge and the control cluster that floats
// OUTSIDE it. The dimmed margin either side of the card is far wider than
// the cluster, so there is no risk of it landing off-pane at any sane size.
constexpr int kPeekControlGap = 4;

// Open/dismiss animation timing: open is longer and ease-out
// (settling into place, reading as the page opening from the clicked link);
// dismiss is shorter and ease-in (getting out of the way, retreating back
// toward it) - same asymmetry the pre-Task-8 timings already had, widened so
// the now much larger scale change (see kPeekOpenStartScale) reads as a
// deliberate zoom instead of a snap.
// fade duration only - the scale now runs on its own spring below
// and settles on its own schedule. Shorter than the spring's first peak so
// the fade reads as incidental to a scale-driven motion, not the motion.
constexpr NSTimeInterval kPeekOpenDuration = 0.22;
constexpr NSTimeInterval kPeekDismissDuration = 0.17;
constexpr CGFloat kPeekRestScale = 1.0;
// A real "zoom in from the link" rather than a subtle
// pop (0.96->1.0) - small enough to read as growing FROM a point rather than
// just enlarging in place.
constexpr CGFloat kPeekOpenStartScale = 0.25;

// Damping ratio ~0.71: one overshoot to ~1.04, more reads as a wobble. First
// peak at ~280ms, where the page behind lands too (kPeekBackdropDuration,
// atrium_browser_window.cc). Change them together.
constexpr CGFloat kPeekOpenSpringMass = 1.0;
constexpr CGFloat kPeekOpenSpringStiffness = 260.0;
constexpr CGFloat kPeekOpenSpringDamping = 23.0;
constexpr CGFloat kPeekOpenSpringInitialVelocity = 0.0;

// Clamp margin for the zoom anchor's normalized position within the card
// (see ApplyZoomAnchor) - keeps a click near or outside an edge anchored
// just inside the card instead of exactly on/past its corner, which would
// read as growing from a degenerate sliver rather than a point.
constexpr float kZoomAnchorClampMargin = 0.05f;

NSString* const kPeekOpenAnimationKey = @"atriumPeekOpen";
NSString* const kPeekDismissAnimationKey = @"atriumPeekDismiss";
NSString* const kPeekPromoteAnimationKey = @"atriumPeekPromote";

// Damping ratio ~0.85 for the pop-out: the window is already pane-sized
// while the layer springs up to it, so any real overshoot is clipped flat
// at the window edge and the corners go square for a few frames.
constexpr CGFloat kPeekPromoteSpringDamping = 27.0;

constexpr int kPeekLoadingThrobberDiameter = 28;

// Origin chip: overlay.css's --ov-radius-chip, --ov-border (ink at 12%) and
// --ov-danger for the "Not secure" mark. Near-opaque so a busy page
// underneath cannot wash the host out - it is the only thing telling the
// user whose page the card is showing.
constexpr int kPeekOriginChipRadius = 12;
constexpr int kPeekOriginChipInset = 12;
constexpr SkAlpha kPeekOriginChipAlpha = 0xF2;
constexpr SkAlpha kPeekOriginChipBorderAlpha = 31;
constexpr SkColor kPeekDangerInkLight = SkColorSetRGB(0x96, 0x20, 0x1F);
constexpr SkColor kPeekDangerInkDark = SkColorSetRGB(0xFF, 0xB3, 0xB0);

// The overlay tokens, resolved the way the confirm-to-quit HUD resolves them:
// the card has no WebContents of its own to read overlay.css from. Read per
// show rather than kept, so an appearance switch reaches the next card.
ConfirmQuitPanelStyle PeekCardStyle() {
  NSAppearanceName best = [NSApp.effectiveAppearance
      bestMatchFromAppearancesWithNames:@[
        NSAppearanceNameAqua, NSAppearanceNameDarkAqua
      ]];
  const bool dark = [best isEqualToString:NSAppearanceNameDarkAqua];
  const PrefService* local_state =
      g_browser_process ? g_browser_process->local_state() : nullptr;
  if (!local_state) {
    return ComputeConfirmQuitPanelStyle(dark, /*native=*/false, std::nullopt);
  }
  const bool native =
      local_state->GetBoolean(prefs::kAtriumConfirmQuitPanelNative);
  const int surround_argb =
      local_state->GetInteger(prefs::kAtriumConfirmQuitPanelSurroundColor);
  return ComputeConfirmQuitPanelStyle(
      dark, native,
      surround_argb >= 0 ? std::make_optional(static_cast<SkColor>(surround_argb))
                         : std::nullopt,
      ConfirmQuitPanelThemeFromPrefs(*local_state));
}

// Zoom-in-from-link open animation: points `layer`'s scale
// transform at `origin_screen` instead of the layer's default center, so a
// CABasicAnimation on transform.scale added right after this call visibly
// grows FROM (or, run in reverse, shrinks TOWARD) that point rather than the
// card's middle. `window_bounds` is the card's current on-screen rect (the
// SAME rect `layer` fills at scale 1.0 - ApplyParentBounds already set it
// there before this is called), used to turn `origin_screen` into a
// normalized position within the card.
//
// Always fully re-derives both anchorPoint and position from `layer`'s
// CURRENT bounds (rather than incrementally adjusting from whatever
// anchorPoint the layer happened to have left over from a prior open/close),
// so this is correct regardless of the layer's history - including the "no
// origin" fallback branch below, which resets cleanly to the plain center
// anchor every time rather than only on a first call.
//
// The two lines actually moving the layer (anchorPoint/position) are only
// safe to call OUTSIDE any implicit-animation-enabled transaction (the
// caller wraps this in a transaction with actions disabled) - see Show()/
// Close()'s comments.
void ApplyZoomAnchor(CALayer* layer,
                     const gfx::Rect& window_bounds,
                     std::optional<gfx::Point> origin_screen) {
  CGFloat anchor_x = 0.5;
  CGFloat anchor_y = 0.5;
  if (origin_screen && !window_bounds.IsEmpty()) {
    float fx = static_cast<float>(origin_screen->x() - window_bounds.x()) /
              window_bounds.width();
    // CALayer's anchorPoint Y runs bottom(0)-to-top(1); gfx::Rect's Y runs
    // top(0)-to-bottom(1) (screen convention, see GetCursorScreenPoint's
    // doc at the atrium_peek_intercept.cc call site) - flip once, here, at
    // this single boundary between the two conventions.
    float fy = 1.0f - static_cast<float>(origin_screen->y() - window_bounds.y()) /
                          window_bounds.height();
    anchor_x = std::clamp(fx, kZoomAnchorClampMargin, 1.0f - kZoomAnchorClampMargin);
    anchor_y = std::clamp(fy, kZoomAnchorClampMargin, 1.0f - kZoomAnchorClampMargin);
  }
  const CGSize size = layer.bounds.size;
  layer.anchorPoint = CGPointMake(anchor_x, anchor_y);
  // position is where the anchorPoint sits, in the layer's own coordinate
  // space - correct here specifically because `layer` is the window's ROOT
  // content layer (no superlayer offset to account for) and this always
  // runs before any scale transform is applied (identity transform, so
  // frame == bounds at (0,0)).
  layer.position = CGPointMake(anchor_x * size.width, anchor_y * size.height);
}

// The corner radius is in layer units, so a scaled card shows a scaled
// radius: at kPeekOpenStartScale the corners read square. This counter-scales
// it along the same spring the scale runs on (CASpringAnimation's model with
// zero initial velocity), so the corners stay kPeekCornerRadius on screen.
CAKeyframeAnimation* CornerRadiusFollowingSpring(CGFloat from_scale,
                                                 CGFloat damping,
                                                 NSTimeInterval duration) {
  const double omega = std::sqrt(kPeekOpenSpringStiffness / kPeekOpenSpringMass);
  const double zeta =
      damping / (2.0 * std::sqrt(kPeekOpenSpringStiffness * kPeekOpenSpringMass));
  const double omega_d = omega * std::sqrt(std::max(0.0, 1.0 - zeta * zeta));
  constexpr int kSamples = 48;
  NSMutableArray* values = [NSMutableArray arrayWithCapacity:kSamples];
  for (int i = 0; i < kSamples; ++i) {
    const double t = duration * i / (kSamples - 1);
    const double decay = std::exp(-zeta * omega * t);
    const double scale =
        1.0 - (1.0 - from_scale) * decay *
                  (std::cos(omega_d * t) +
                   (zeta * omega / omega_d) * std::sin(omega_d * t));
    [values addObject:@(kPeekCornerRadius / std::max(scale, 0.05))];
  }
  CAKeyframeAnimation* radius =
      [CAKeyframeAnimation animationWithKeyPath:@"cornerRadius"];
  radius.values = values;
  radius.duration = duration;
  radius.calculationMode = kCAAnimationLinear;
  return radius;
}

}  // namespace

// PeekSnapshotView ------------------------------------------------------------

// Paints a bitmap stretched to its bounds, so the still follows the window
// when the pop-out resizes it to the pane.
class PeekSnapshotView : public views::View {
  METADATA_HEADER(PeekSnapshotView, views::View)

 public:
  void SetImage(gfx::ImageSkia image) {
    image_ = std::move(image);
    SchedulePaint();
  }

  void OnPaint(gfx::Canvas* canvas) override {
    views::View::OnPaint(canvas);
    if (image_.isNull()) {
      return;
    }
    const gfx::Rect bounds = GetLocalBounds();
    canvas->DrawImageInt(image_, 0, 0, image_.width(), image_.height(),
                         bounds.x(), bounds.y(), bounds.width(),
                         bounds.height(), /*filter=*/true);
  }

 private:
  gfx::ImageSkia image_;
};

BEGIN_METADATA(PeekSnapshotView)
END_METADATA

// PeekRootView ----------------------------------------------------------------

PeekRootView::PeekRootView(Profile* profile) {
  // Rounded in the compositor as well as on the window's CALayer: the
  // window's clip is not applied to Chromium's remote layer while that
  // CALayer animates, so the open spring showed square corners.
  SetPaintToLayer();
  layer()->SetFillsBoundsOpaquely(false);
  layer()->SetRoundedCornerRadius(gfx::RoundedCornersF(kPeekCornerRadius));
  layer()->SetIsFastRoundedCorner(true);
  // The origin chip floats over the content instead of reflowing it, so
  // Layout() below places both by hand - an exact FillLayout equivalent for
  // the content, the same swap AtriumPaneView made for its mirror badge.

  // web_view_ and loading_placeholder_ share the same card slot via
  // FillLayout - AtriumRootView's web_view_/snapshot_view_ precedent
  // (atrium_browser_window.cc) for two views that occupy identical bounds
  // and toggle visibility rather than reflow. See loading_placeholder_'s
  // header doc for why.
  auto content_container = std::make_unique<views::View>();
  content_container->SetLayoutManager(std::make_unique<views::FillLayout>());

  auto web_view = std::make_unique<views::WebView>(profile);
  web_view_ = content_container->AddChildView(std::move(web_view));

  auto loading_placeholder = std::make_unique<views::View>();
  auto* placeholder_layout =
      loading_placeholder->SetLayoutManager(std::make_unique<views::BoxLayout>(
          views::BoxLayout::Orientation::kVertical));
  placeholder_layout->set_main_axis_alignment(
      views::BoxLayout::MainAxisAlignment::kCenter);
  placeholder_layout->set_cross_axis_alignment(
      views::BoxLayout::CrossAxisAlignment::kCenter);
  loading_throbber_ = loading_placeholder->AddChildView(
      std::make_unique<views::Throbber>(kPeekLoadingThrobberDiameter));
  loading_placeholder_ =
      content_container->AddChildView(std::move(loading_placeholder));
  promote_snapshot_ =
      content_container->AddChildView(std::make_unique<PeekSnapshotView>());
  promote_snapshot_->SetVisible(false);

  content_container_ = AddChildView(std::move(content_container));

  // Widget-level Escape dismiss (Global Constraints: "Escape (widget-level
  // accelerator...)"). Registration is deferred by View::AddAccelerator
  // until this view is attached to a real Widget, so calling it here, before
  // PeekWidget has even Init()'d its widget_, is safe.
  AddAccelerator(ui::Accelerator(ui::VKEY_ESCAPE, ui::EF_NONE));
}

PeekRootView::~PeekRootView() = default;

void PeekRootView::SetEscapeCallback(base::RepeatingClosure callback) {
  escape_callback_ = std::move(callback);
}

void PeekRootView::SetLoadingPlaceholderVisible(bool visible) {
  if (!loading_placeholder_) {
    return;
  }
  if (visible) {
    loading_placeholder_->SetBackground(
        views::CreateSolidBackground(PeekCardStyle().surface));
    loading_throbber_->Start();
  } else {
    loading_throbber_->Stop();
  }
  loading_placeholder_->SetVisible(visible);
}

bool PeekRootView::LoadingPlaceholderVisibleForTesting() const {
  return loading_placeholder_ && loading_placeholder_->GetVisible();
}

void PeekRootView::ShowPromoteSnapshot(const SkBitmap& bitmap,
                                       float device_scale_factor) {
  promote_snapshot_->SetBackground(
      views::CreateSolidBackground(PeekCardStyle().surface));
  promote_snapshot_->SetImage(
      bitmap.drawsNothing()
          ? gfx::ImageSkia()
          : gfx::ImageSkia::CreateFromBitmap(bitmap, device_scale_factor));
  promote_snapshot_->SetVisible(true);
}

void PeekRootView::HidePromoteSnapshot() {
  promote_snapshot_->SetImage(gfx::ImageSkia());
  promote_snapshot_->SetVisible(false);
}

void PeekRootView::SetOriginChip(const std::u16string& host, bool secure) {
  if (!origin_chip_) {
    auto chip = std::make_unique<views::View>();
    chip->SetPaintToLayer();
    chip->layer()->SetFillsBoundsOpaquely(false);
    chip->layer()->SetRoundedCornerRadius(
        gfx::RoundedCornersF(kPeekOriginChipRadius));
    chip->layer()->SetIsFastRoundedCorner(true);
    chip->SetLayoutManager(std::make_unique<views::BoxLayout>(
        views::BoxLayout::Orientation::kHorizontal,
        gfx::Insets::VH(4, 12), 6));

    // Two labels, not one: the test and the reader both want the host on its
    // own, undiluted by the mark in front of it. Subpixel text off on both -
    // Label DCHECKs that when it paints into a layer that is not opaque.
    auto mark = std::make_unique<views::Label>();
    mark->SetSubpixelRenderingEnabled(false);
    origin_mark_label_ = chip->AddChildView(std::move(mark));

    auto host_label = std::make_unique<views::Label>();
    host_label->SetSubpixelRenderingEnabled(false);
    origin_host_label_ = chip->AddChildView(std::move(host_label));

    origin_chip_ = AddChildView(std::move(chip));
  }
  // A word, not a lock glyph: every lock character in a system font is an
  // emoji, and emoji are banned in this source tree.
  origin_mark_label_->SetText(secure ? std::u16string()
                                     : u"Not secure");
  origin_mark_label_->SetVisible(!secure);
  origin_host_label_->SetText(host);
  const ConfirmQuitPanelStyle style = PeekCardStyle();
  origin_mark_label_->SetEnabledColor(color_utils::IsDark(style.surface)
                                          ? kPeekDangerInkDark
                                          : kPeekDangerInkLight);
  origin_host_label_->SetEnabledColor(style.ink);
  origin_chip_->SetBackground(views::CreateRoundedRectBackground(
      SkColorSetA(style.surface, kPeekOriginChipAlpha),
      kPeekOriginChipRadius));
  origin_chip_->SetBorder(views::CreateRoundedRectBorder(
      1, kPeekOriginChipRadius,
      SkColorSetA(style.ink, kPeekOriginChipBorderAlpha)));
  origin_chip_->SetVisible(true);
  InvalidateLayout();
}

void PeekRootView::ClearOriginChip() {
  if (origin_chip_) {
    origin_chip_->SetVisible(false);
  }
}

std::u16string PeekRootView::OriginChipHostForTesting() const {
  return origin_chip_ && origin_chip_->GetVisible()
             ? std::u16string(origin_host_label_->GetText())
             : std::u16string();
}

void PeekRootView::Layout(PassKey) {
  const gfx::Rect bounds = GetLocalBounds();
  if (content_container_) {
    content_container_->SetBoundsRect(bounds);
  }
  if (origin_chip_ && origin_chip_->GetVisible()) {
    const gfx::Size pref = origin_chip_->GetPreferredSize();
    origin_chip_->SetBoundsRect(
        gfx::Rect(bounds.x() + kPeekOriginChipInset,
                  bounds.y() + kPeekOriginChipInset,
                  std::min(pref.width(), bounds.width()), pref.height()));
  }
}

bool PeekRootView::AcceleratorPressed(const ui::Accelerator& accelerator) {
  VLOG(1) << "Atrium: peek accelerator pressed";
  if (escape_callback_) {
    escape_callback_.Run();
  }
  return true;
}

// PeekControlsWidget ---------------------------------------------------------

PeekControlsWidget::PeekControlsWidget(AtriumBrowserWindow* parent,
                                       PeekWidget* owner)
    : AtriumWebSurfaceWidget(parent->widget(),
                             "peekctl",
                             "AtriumPeekControls",
                             /*activatable=*/false),
      parent_(parent),
      owner_(owner) {}

PeekControlsWidget::~PeekControlsWidget() = default;

bool PeekControlsWidget::RetriesOnMissingChromeContext() const {
  return false;
}

void PeekControlsWidget::ShowForCard(const gfx::Rect& card_bounds) {
  if (!widget_) {
    return;
  }
  if (card_bounds.IsEmpty()) {
    // No card to pin to - show nothing rather than a stranded cluster.
    Hide();
    return;
  }
  if (!web_contents_) {
    BuildContents();
  }
  // Just outside the card's top-right corner, top edges aligned - the
  // screenshot's layout. Screen coordinates, the same space
  // PeekWidget::ApplyParentBounds computes card_bounds in.
  const gfx::Rect bounds(card_bounds.right() + kPeekControlGap, card_bounds.y(),
                         kPeekControlsSize.width(), kPeekControlsSize.height());
  widget_->SetBounds(bounds);
  if (!widget_->IsVisible()) {
    // Inactive: never take key from the card (see the class doc).
    widget_->ShowInactive();
  }
}

void PeekControlsWidget::Hide() {
  if (widget_ && widget_->IsVisible()) {
    widget_->Hide();
  }
}

void PeekControlsWidget::OnSurfaceMessage(std::string_view name,
                                          const base::ListValue& args) {
  if (name == "peekControlsReady") {
    renderer_ready_ = true;
    parent_->PushOverlayTheme(web_contents_.get());
    return;
  }
  if (name != "peekControl") {
    return;
  }
  if (args.empty() || !args.front().is_string()) {
    return;
  }
  const std::string& action = args.front().GetString();
  if (action == "close") {
    // The same one place Escape and the focus-bounce guard funnel through.
    if (owner_) {
      owner_->RunDismissCallback();
    }
    return;
  }
  if (action == "popout") {
    if (promote_callback_) {
      promote_callback_.Run();
    }
    return;
  }
  LOG(WARNING) << "Atrium: unknown peekControl action " << action;
}

void PeekControlsWidget::SetPromoteCallback(base::RepeatingClosure callback) {
  promote_callback_ = std::move(callback);
}

// PeekWidget --------------------------------------------------------------

PeekWidget::PeekWidget(AtriumBrowserWindow* parent) : parent_(parent) {
  // the control cluster is a sibling window, built here so it
  // shares this widget's lifetime exactly.
  controls_ = std::make_unique<PeekControlsWidget>(parent, this);
  widget_ = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params.name = "AtriumPeek";
  // Translucent + activatable (CommandBarWidget precedent): on Mac this maps
  // to a borderless child NSWindow that can become key, so the preview can
  // be scrolled/clicked/typed into immediately.
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.activatable = views::Widget::InitParams::Activatable::kYes;
  params.SetParent(parent_->widget());

  widget_delegate_ = std::make_unique<views::WidgetDelegate>();
  widget_delegate_->SetCanActivate(true);
  root_view_ = widget_delegate_->SetContentsView(
      std::make_unique<PeekRootView>(parent_->browser()->profile()));
  params.delegate = widget_delegate_.get();

  widget_->Init(std::move(params));

  // Route this activatable child window's unconsumed key equivalents to the
  // main menu (Cmd+Q above all) instead of the parent's web-content
  // redispatch, which eats them - see AtriumPeekCommandDispatcherDelegate's
  // doc above.
  if (NSWindow* peek_ns_window_for_dispatch =
          widget_->GetNativeWindow().GetNativeNSWindow()) {
    if ([peek_ns_window_for_dispatch
            conformsToProtocol:@protocol(CommandDispatchingWindow)]) {
      CommandDispatcher* dispatcher = [static_cast<
          id<CommandDispatchingWindow>>(peek_ns_window_for_dispatch)
          commandDispatcher];
      AtriumPeekCommandDispatcherDelegate* delegate =
          [[AtriumPeekCommandDispatcherDelegate alloc] initWithParent:parent_];
      dispatcher.delegate = delegate;
      // CommandDispatcher holds its delegate weakly; retain it for the
      // window's lifetime via an associated object.
      objc_setAssociatedObject(peek_ns_window_for_dispatch,
                               &kPeekDispatcherDelegateKey, delegate,
                               OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
  }

  widget_->AddObserver(this);
  // Parent-bounds tracking (CommandBarWidget precedent): removed in the
  // destructor before widget_ itself is torn down, so this observer can
  // never dangle across the parent widget's destruction.
  parent_->widget()->AddObserver(this);

  root_view_->SetEscapeCallback(base::BindRepeating(
      &PeekWidget::OnEscapePressed, base::Unretained(this)));

  // Rounded corners + a real window drop
  // shadow, set up once here (this widget is never rebuilt - see the class
  // doc) rather than per-Show(). Precedent for touching the native NSWindow
  // immediately after Init(): AtriumBrowserWindow::InstallLiquidGlassBackdrop
  // does the same for the main window.
  if (NSWindow* peek_ns_window = widget_->GetNativeWindow().GetNativeNSWindow()) {
    if (NSView* content = peek_ns_window.contentView) {
      content.wantsLayer = YES;
      // masksToBounds on this ROOT content layer clips every descendant
      // sublayer to the rounded rect - the chrome strip's NativeViewHost
      // (top corners) and the WebView/loading-placeholder (bottom corners)
      // alike, with no per-view clipping needed on either of them.
      content.layer.cornerRadius = kPeekCornerRadius;
      content.layer.masksToBounds = YES;
    }
    // A real NSWindow shadow, not a hand-drawn layer shadow: masksToBounds
    // above would clip a layer-based shadow attached to the same layer (it
    // has to live outside whatever it masks), and this widget is already a
    // genuine standalone NSWindow, so the native window shadow is the
    // simpler, more native-looking option - Arc's own preview card is a
    // real window too. Borderless/frameless windows don't always default
    // this on, so set it explicitly. AppKit derives the shadow's shape from
    // the window's actual (transparent-outside-the-rounded-rect) pixels, so
    // this automatically follows the rounded corners with no extra work;
    // HideLoadingPlaceholder and ApplyParentBounds call invalidateShadow so
    // it stays in sync as the card's first real content paints and resizes.
    peek_ns_window.hasShadow = YES;
  }
}

PeekWidget::~PeekWidget() {
  parent_->widget()->RemoveObserver(this);
  widget_->RemoveObserver(this);
  // Defensive unbind: today ClosePeek always calls SetWebContents(nullptr)
  // before this destructor runs, so `contents` is normally null here. Guard
  // it anyway - a future teardown-ordering change that skips that call must
  // not leave a still-bound peek WebContents holding a dangling delegate
  // pointer back into this destroyed widget (mirrors SetWebContents' own
  // outgoing-delegate clear above).
  if (content::WebContents* contents = root_view_->web_view()->web_contents()) {
    if (contents->GetDelegate() == this) {
      contents->SetDelegate(nullptr);
    }
  }
}

void PeekWidget::ApplyParentBounds() {
  // Sized against the web-content pane
  // (excludes the sidebar strip - see GetWebContentBoundsInScreen's doc),
  // inset by a small margin, rather than a small fixed-size dialog centered
  // over the whole window. Screen coordinates - see this method's header
  // doc for why (top-level widget, InitParams::child never set).
  gfx::Rect content_bounds = parent_->GetWebContentBoundsInScreen();
  if (content_bounds.IsEmpty()) {
    // Web-content pane not laid out yet - shouldn't happen in practice (the
    // parent window is already on screen by the time a peek can open), but
    // fall back to the parent's own window bounds so the widget still gets
    // a sane, non-degenerate rect instead of collapsing to nothing.
    content_bounds = parent_->widget()->GetWindowBoundsInScreen();
  }
  gfx::Rect card_bounds = content_bounds;
  // Tall as the window, not the pane: the pane starts under the titlebar band
  // and inside the card inset, and Arc's card reaches past both.
  const gfx::Rect window_bounds = parent_->widget()->GetWindowBoundsInScreen();
  card_bounds.set_y(window_bounds.y() + kPeekMargin);
  card_bounds.set_height(window_bounds.height() - 2 * kPeekMargin);
  const int card_width = static_cast<int>(
      std::round(content_bounds.width() * kPeekWidthFraction));
  card_bounds.set_x(content_bounds.x() +
                    (content_bounds.width() - card_width) / 2);
  card_bounds.set_width(card_width);
  widget_->SetBounds(card_bounds);
  if (NSWindow* peek_ns_window = widget_->GetNativeWindow().GetNativeNSWindow()) {
    [peek_ns_window invalidateShadow];
  }
  // Gated on intended_visible_, not widget_->IsVisible(): Show() calls this
  // before widget_->Show(), so IsVisible() read stale-false here on a first
  // open.
  if (controls_ && intended_visible_) {
    controls_->ShowForCard(card_bounds);
  }
}

void PeekWidget::ReapplyBoundsForContentChange() {
  if (!widget_->IsVisible()) {
    return;
  }
  ApplyParentBounds();
}

void PeekWidget::Show(std::optional<gfx::Point> click_origin_screen) {
  // Set before ApplyParentBounds() - it gates the control cluster's
  // ShowForCard on this, not on widget_->IsVisible() (still false here,
  // since widget_->Show() runs later in this method).
  intended_visible_ = true;
  ApplyParentBounds();
  shown_at_ = base::TimeTicks::Now();
  // This widget outlives each peek, so a tab the OUTGOING card opened would
  // otherwise keep swallowing this one's genuine click-away.
  self_opened_tab_at_ = base::TimeTicks();
  // Stashed for Close()'s reverse zoom - see this method's header doc.
  open_origin_screen_ = click_origin_screen;
  // Every fresh peek starts with the loading placeholder covering the
  // WebView (see PeekRootView::SetLoadingPlaceholderVisible's doc) - reset
  // to visible on every Show(), including the no-stacking replace path
  // (OpenPeek() closes the old peek then immediately Show()s the new one),
  // since HideLoadingPlaceholder only fires once per peek's first paint.
  root_view_->SetLoadingPlaceholderVisible(true);
  // A stale host over a different page is worse than no chip at all, so the
  // chip starts off and only the adopt path puts one back.
  root_view_->ClearOriginChip();
  // Pop-out polish: dim the parent pane's real content behind the
  // card - see AtriumPaneView::scrim_view()'s doc. Shown here (Show(), not
  // FinishHide()) so it appears in lockstep with the card opening; removed
  // by FinishHide() below, the single point every close path (deliberate
  // Close(), the dismiss callback, promote) funnels through - see
  // FinishHide's own header doc for why that is the right funnel.
  parent_->SetPeekScrimVisible(true);
  VLOG(1) << "Atrium: peek widget showing for parent " << parent_.get();
  // Show() before the layer animation: the animation is purely visual (the
  // window's actual bounds/alpha are already final via ApplyParentBounds),
  // same ordering as AtriumSidebarOverlayWidget::Show. Show() (not
  // ShowInactive) - peek needs key focus immediately, see this method's
  // header doc.
  widget_->Show();

  NSWindow* peek_ns_window = widget_->GetNativeWindow().GetNativeNSWindow();
  NSView* content = peek_ns_window.contentView;
  if (!content) {
    return;
  }
  content.wantsLayer = YES;
  // Drop any outstanding dismiss animation first (open+dismiss+open churn
  // can land here before a prior dismiss's fillMode-held end state was
  // cleared) so the two animations never fight over the same layer keys.
  [content.layer removeAnimationForKey:kPeekDismissAnimationKey];
  // The interrupted dismiss above will never reach FinishHide (its
  // completion block sees intended_visible_ back to true and bails) - the
  // outgoing WebContents a churned Close() is holding onto has no more
  // reason to wait, since nothing is animating it away any more.
  if (on_hide_complete_) {
    std::move(on_hide_complete_).Run();
  }

  const gfx::Rect window_bounds = widget_->GetWindowBoundsInScreen();
  // Explicit transaction, actions disabled: ApplyZoomAnchor's anchorPoint/
  // position writes must land immediately (no implicit animation glide) so
  // the layer's identity-transform frame is already correct - (0,0,w,h) -
  // the instant BEFORE the explicit scale/fade animation below starts from
  // it. setDisableActions only suppresses implicit KVO-triggered actions;
  // the addAnimation: call below is explicit and unaffected by it.
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  ApplyZoomAnchor(content.layer, window_bounds, click_origin_screen);
  [CATransaction commit];

  // A spring rather than a plain curve: a timing function alone
  // is monotonic and can never overshoot, so a "jump in" needs the physics
  // model instead - see kPeekOpenSpringDamping's doc for the target ratio.
  CASpringAnimation* scale =
      [CASpringAnimation animationWithKeyPath:@"transform.scale"];
  scale.fromValue = @(kPeekOpenStartScale);
  scale.toValue = @(kPeekRestScale);
  scale.mass = kPeekOpenSpringMass;
  scale.stiffness = kPeekOpenSpringStiffness;
  scale.damping = kPeekOpenSpringDamping;
  scale.initialVelocity = kPeekOpenSpringInitialVelocity;
  scale.duration = scale.settlingDuration;
  CABasicAnimation* fade = [CABasicAnimation animationWithKeyPath:@"opacity"];
  fade.fromValue = @0.0;
  fade.toValue = @1.0;
  fade.duration = kPeekOpenDuration;
  fade.timingFunction =
      [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseOut];
  CAAnimationGroup* group = [CAAnimationGroup animation];
  // The group's own duration must cover the longer of the two children (the
  // spring usually settles after kPeekOpenDuration) - a shorter group
  // duration would cut the spring's overshoot off before it settles.
  group.duration = std::max(scale.duration, kPeekOpenDuration);
  group.animations = @[
    scale, fade,
    CornerRadiusFollowingSpring(kPeekOpenStartScale, kPeekOpenSpringDamping,
                                group.duration)
  ];

  // Recenter the anchor once the open animation settles, so a later PLAIN
  // resize (ApplyParentBounds while showing, via OnWidgetBoundsChanged/
  // ReapplyBoundsForContentChange - neither of which touches anchorPoint)
  // isn't skewed by an off-center anchor left over from the zoom. Guarded by
  // a weak ptr (may fire after teardown, same posture as Close()'s
  // completion block) AND a staleness check: removing an animation early
  // still fires its transaction's completion block, so a Close() inside the
  // open window (its removeAnimationForKey on this key) or a churn
  // re-Show() delivers this block AFTER the newer call has already
  // repositioned the anchor for its own animation - recentering then would
  // visibly stomp that anchor mid-flight (transform.scale animates,
  // anchorPoint does not). Rejecting a changed show_generation_ (superseding
  // Show()) or a false intended_visible_ (dismiss in flight) covers both -
  // the same removal-fires-completion hazard FinishHide's intended_visible_
  // guard already covers for the dismiss animation.
  base::WeakPtr<PeekWidget> weak = weak_factory_.GetWeakPtr();
  const int generation = ++show_generation_;
  [CATransaction begin];
  [CATransaction setCompletionBlock:^{
    if (!weak || weak->show_generation_ != generation ||
        !weak->intended_visible_) {
      return;
    }
    NSWindow* window = weak->widget_->GetNativeWindow().GetNativeNSWindow();
    if (CALayer* layer = window.contentView.layer) {
      [CATransaction begin];
      [CATransaction setDisableActions:YES];
      ApplyZoomAnchor(layer, gfx::Rect(), std::nullopt);
      [CATransaction commit];
    }
  }];
  [content.layer addAnimation:group forKey:kPeekOpenAnimationKey];
  [CATransaction commit];

  [peek_ns_window invalidateShadow];
}

void PeekWidget::Close(bool animate, base::OnceClosure on_hidden) {
  // Mark the close as deliberate BEFORE Hide() so the deactivation Hide()
  // triggers is not misread as a spurious focus bounce - see
  // intended_visible_ and OnWidgetActivationChanged (CommandBarWidget::Close
  // precedent). Also doubles as FinishHide's staleness guard - see that
  // method's header doc.
  intended_visible_ = false;
  // the cluster goes at once rather than riding the card's dismiss
  // animation out. It is a separate window that does not participate in the
  // card's layer animation, so leaving it up would strand it hanging in
  // space beside a shrinking card - and it must be gone on the non-animated
  // promote path too, which this covers by being before the branch below.
  if (controls_) {
    controls_->Hide();
  }
  if (!widget_->IsVisible()) {
    if (on_hidden) {
      std::move(on_hidden).Run();
    }
    return;
  }
  VLOG(1) << "Atrium: peek widget closing for parent " << parent_.get();
  on_hide_complete_ = std::move(on_hidden);
  // A dismiss mid pop-out wins: the grow's completion block sees
  // promoting_ false and never runs the promote.
  promoting_ = false;
  on_promote_insert_.Reset();

  if (!animate) {
    // Promote: instant, no dismiss animation - see this method's header doc.
    FinishHide();
    return;
  }

  NSWindow* peek_ns_window = widget_->GetNativeWindow().GetNativeNSWindow();
  NSView* content = peek_ns_window.contentView;
  if (!content) {
    FinishHide();
    return;
  }
  content.wantsLayer = YES;
  [content.layer removeAnimationForKey:kPeekOpenAnimationKey];
  [content.layer removeAnimationForKey:kPeekPromoteAnimationKey];

  // Reverse toward the SAME point Show() grew from (open_origin_screen_,
  // stashed there) - reads as the card retreating back into the link it
  // came from rather than just shrinking in place. The open animation's own
  // completion recentered the anchor (see Show()'s comment), so this must
  // re-apply it fresh, same disabled-actions posture as Show().
  const gfx::Rect window_bounds = widget_->GetWindowBoundsInScreen();
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  ApplyZoomAnchor(content.layer, window_bounds, open_origin_screen_);
  [CATransaction commit];

  // Guard the completion against teardown - it may fire after this object is
  // destroyed (window close, PromotePeek reentrancy, etc) - same
  // weak-ptr-guarded CATransaction completion block as
  // AtriumSidebarOverlayWidget::Close.
  base::WeakPtr<PeekWidget> weak = weak_factory_.GetWeakPtr();
  [CATransaction begin];
  [CATransaction setCompletionBlock:^{
    if (weak) {
      weak->FinishHide();
    }
  }];
  CABasicAnimation* scale =
      [CABasicAnimation animationWithKeyPath:@"transform.scale"];
  scale.fromValue = @(kPeekRestScale);
  scale.toValue = @(kPeekOpenStartScale);
  CABasicAnimation* fade = [CABasicAnimation animationWithKeyPath:@"opacity"];
  fade.fromValue = @1.0;
  fade.toValue = @0.0;
  CAAnimationGroup* group = [CAAnimationGroup animation];
  group.animations = @[ scale, fade ];
  group.duration = kPeekDismissDuration;
  group.timingFunction =
      [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseIn];
  // Hold the end state (scale kPeekOpenStartScale, opacity 0) until
  // FinishHide actually hides the widget - otherwise the layer snaps back to
  // scale 1/opacity 1 for one frame once the animation is removed at its
  // natural end. Same fillMode/removedOnCompletion pairing as
  // AtriumSidebarOverlayWidget::Close.
  group.fillMode = kCAFillModeForwards;
  group.removedOnCompletion = NO;
  [content.layer addAnimation:group forKey:kPeekDismissAnimationKey];
  [CATransaction commit];
}

void PeekWidget::FinishHide() {
  if (intended_visible_) {
    // Stale completion from a superseded Close() - see this method's header
    // doc. The widget was re-Show()'n since this animation started; leave it
    // alone.
    return;
  }
  widget_->Hide();
  // A card dismissed mid-load would leave the throbber's timer running.
  root_view_->SetLoadingPlaceholderVisible(false);
  root_view_->HidePromoteSnapshot();
  if (NSWindow* peek_ns_window =
          widget_->GetNativeWindow().GetNativeNSWindow()) {
    [peek_ns_window.contentView.layer
        removeAnimationForKey:kPeekDismissAnimationKey];
    [peek_ns_window.contentView.layer
        removeAnimationForKey:kPeekPromoteAnimationKey];
  }
  // undim the parent pane now that the card has actually hidden -
  // see Show()'s matching SetPeekScrimVisible(true) call for why this lives
  // here rather than at the top of Close().
  parent_->SetPeekScrimVisible(false);
  if (on_hide_complete_) {
    std::move(on_hide_complete_).Run();
  }
}

void PeekWidget::Promote(base::OnceClosure insert_tab) {
  if (!intended_visible_ || !widget_->IsVisible()) {
    std::move(insert_tab).Run();
    return;
  }
  if (promoting_) {
    return;
  }
  promoting_ = true;
  on_promote_insert_ = std::move(insert_tab);
  content::WebContents* contents = peeked_web_contents();
  content::RenderWidgetHostView* rwhv =
      contents ? contents->GetRenderWidgetHostView() : nullptr;
  if (!rwhv) {
    OnPromoteSnapshot(show_generation_, 1.0f,
                      content::CopyFromSurfaceResult(base::unexpected(
                          content::CopyFromSurfaceError::kUnknown)));
    return;
  }
  // Same idiom as AtriumBrowserWindow::ArmMoveOutFreeze: the callback lands
  // on this sequence, and an empty output size means device pixels.
  rwhv->CopyFromSurface(
      gfx::Rect(), gfx::Size(), base::TimeDelta(),
      base::BindOnce(&PeekWidget::OnPromoteSnapshot,
                     weak_factory_.GetWeakPtr(), show_generation_,
                     rwhv->GetDeviceScaleFactor()));
}

void PeekWidget::OnPromoteSnapshot(
    int generation,
    float device_scale_factor,
    const content::CopyFromSurfaceResult& result) {
  // A dismiss or a re-Show() while the still was in flight already dropped
  // the insert (Close() clears both).
  if (generation != show_generation_ || !intended_visible_ || !promoting_) {
    return;
  }
  root_view_->ShowPromoteSnapshot(
      result.has_value() ? result->bitmap : SkBitmap(), device_scale_factor);
  // The tab exists from here on: the pane behind shows the live page while
  // the card's still grows over it.
  if (on_promote_insert_) {
    std::move(on_promote_insert_).Run();
  }
  const gfx::Rect card_bounds = widget_->GetWindowBoundsInScreen();
  const gfx::Rect pane_bounds = parent_->GetWebContentBoundsInScreen();
  NSWindow* peek_ns_window = widget_->GetNativeWindow().GetNativeNSWindow();
  NSView* content = peek_ns_window.contentView;
  if (card_bounds.IsEmpty() || pane_bounds.IsEmpty() || !content) {
    Close(/*animate=*/false);
    return;
  }
  // The cluster is its own window and would hang beside a card whose edge
  // moved out from under it.
  if (controls_) {
    controls_->Hide();
  }
  // The page behind steps back to full size while the card grows over it.
  parent_->SetPeekScrimVisible(false);
  widget_->SetBounds(pane_bounds);
  [peek_ns_window invalidateShadow];

  content.wantsLayer = YES;
  [content.layer removeAnimationForKey:kPeekOpenAnimationKey];
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  ApplyZoomAnchor(content.layer, gfx::Rect(), std::nullopt);
  [CATransaction commit];

  // The card is centered in the pane on both axes (ApplyParentBounds), so a
  // center-anchored scale from the old size ratio lands it exactly where it
  // was at t=0. Separate x and y: the ratios differ.
  auto spring = [](NSString* key_path, CGFloat from) {
    CASpringAnimation* animation =
        [CASpringAnimation animationWithKeyPath:key_path];
    animation.fromValue = @(from);
    animation.toValue = @(kPeekRestScale);
    animation.mass = kPeekOpenSpringMass;
    animation.stiffness = kPeekOpenSpringStiffness;
    animation.damping = kPeekPromoteSpringDamping;
    animation.initialVelocity = kPeekOpenSpringInitialVelocity;
    animation.duration = animation.settlingDuration;
    return animation;
  };
  const CGFloat from_scale_x =
      static_cast<CGFloat>(card_bounds.width()) / pane_bounds.width();
  CASpringAnimation* scale_x = spring(@"transform.scale.x", from_scale_x);
  CASpringAnimation* scale_y = spring(
      @"transform.scale.y",
      static_cast<CGFloat>(card_bounds.height()) / pane_bounds.height());
  const NSTimeInterval grow = std::max(scale_x.duration, scale_y.duration);
  // The still stretches to the pane's aspect as it grows, and the live page
  // under it is already laid out for the pane, so the card dissolves into
  // it over the second half rather than snapping at the end.
  CABasicAnimation* fade = [CABasicAnimation animationWithKeyPath:@"opacity"];
  fade.fromValue = @1.0;
  fade.toValue = @0.0;
  fade.beginTime = grow * 0.45;
  fade.duration = grow * 0.55;
  fade.fillMode = kCAFillModeBackwards;
  fade.timingFunction =
      [CAMediaTimingFunction functionWithName:kCAMediaTimingFunctionEaseIn];
  CAAnimationGroup* group = [CAAnimationGroup animation];
  group.animations = @[
    scale_x, scale_y, fade,
    CornerRadiusFollowingSpring(from_scale_x, kPeekPromoteSpringDamping, grow)
  ];
  group.duration = grow;
  group.fillMode = kCAFillModeForwards;
  group.removedOnCompletion = NO;

  // Same staleness posture as Show()'s completion: a Close() or a re-Show()
  // during the grow removes this animation, which still fires the block.
  base::WeakPtr<PeekWidget> weak = weak_factory_.GetWeakPtr();
  [CATransaction begin];
  [CATransaction setCompletionBlock:^{
    if (!weak || weak->show_generation_ != generation ||
        !weak->intended_visible_ || !weak->promoting_) {
      return;
    }
    weak->Close(/*animate=*/false);
  }];
  [content.layer addAnimation:group forKey:kPeekPromoteAnimationKey];
  [CATransaction commit];
}

bool PeekWidget::IsShowing() const {
  return widget_ && widget_->IsVisible();
}

void PeekWidget::SetCardVisibleForActiveTab(bool visible) {
  if (!widget_ || visible == widget_->IsVisible()) {
    return;
  }
  if (visible) {
    // Ordering matches Show(): controls_->ShowForCard() gates on this.
    intended_visible_ = true;
    shown_at_ = base::TimeTicks::Now();
    ApplyParentBounds();
    parent_->SetPeekScrimVisible(true);
    // Show() on an activatable widget reclaims key the same way it did on
    // first open - focus returns to the card, not the tab it sat over.
    widget_->Show();
    return;
  }
  // Cleared before Hide(), like Close() - else its deactivation reads as a
  // genuine key loss and queues a dismiss instead of just hiding the card.
  intended_visible_ = false;
  // A hidden card cannot grow, and a pop-out left pending blocks the next one.
  promoting_ = false;
  on_promote_insert_.Reset();
  if (controls_) {
    controls_->Hide();
  }
  // Same as FindBarWidget::Close: the card's WebView re-announces focus to
  // this widget's own FocusManager on any AppKit change below, so resign its
  // NSWindow's first responder before anything else moves.
  if (NSWindow* peek_ns_window = widget_->GetNativeWindow().GetNativeNSWindow()) {
    [peek_ns_window makeFirstResponder:nil];
  }
  if (views::FocusManager* focus = widget_->GetFocusManager()) {
    focus->ClearFocus();
    focus->SetStoredFocusView(nullptr);
  }
  widget_->Hide();
  parent_->SetPeekScrimVisible(false);
}

content::WebContents* PeekWidget::peeked_web_contents() const {
  return root_view_ ? root_view_->web_view()->web_contents() : nullptr;
}

void PeekWidget::SetWebContents(content::WebContents* contents) {
  // See HandleKeyboardEvent's header doc for why this widget must be the
  // WebContents' delegate. Clear the outgoing contents' delegate first (a
  // promoted peek's WebContents outlives this call, moving into the real
  // tab strip right after DetachPeekWebContents/SetWebContents(nullptr) -
  // it must not carry a stale pointer back to this widget).
  if (content::WebContents* current = root_view_->web_view()->web_contents()) {
    current->SetDelegate(nullptr);
  }
  if (contents) {
    contents->SetDelegate(this);
  }
  // Pane-claim instrumentation. The peek card is the other holder
  // class that can claim a tab WebContents, so it has to appear in the same
  // ledger as the panes or the overlap is invisible.
  AtriumBrowserWindow::TraceClaim(root_view_->web_view(), contents,
                                  base::StringPrintf("peek%p", this));
  root_view_->web_view()->SetWebContents(contents);
}

bool PeekWidget::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  return unhandled_keyboard_event_handler_.HandleKeyboardEvent(
      event, widget_->GetFocusManager());
}

void PeekWidget::CloseContents(content::WebContents* source) {
  // A contents this widget no longer shows (a promoted tab still carrying the
  // delegate for a beat) must not take the card down with it.
  if (source != peeked_web_contents()) {
    return;
  }
  // The dismiss destroys `source`, and this runs from inside that same
  // WebContents' own renderer callback, so it has to wait a turn. The copy is
  // bound to `source`, so a switch in that turn cannot close another peek.
  if (dismiss_callback_) {
    base::SequencedTaskRunner::GetCurrentDefault()->PostTask(FROM_HERE,
                                                             dismiss_callback_);
  }
}

content::WebContents* PeekWidget::AddNewContents(
    content::WebContents* source,
    std::unique_ptr<content::WebContents> new_contents,
    const GURL& target_url,
    WindowOpenDisposition disposition,
    const blink::mojom::WindowFeatures& window_features,
    bool user_gesture,
    bool* was_blocked) {
  Browser* browser = parent_ ? parent_->browser() : nullptr;
  if (!browser) {
    return nullptr;
  }
  // A link the card's own page opened is not the user leaving the card, but
  // inserting its tab takes key off the card - see kSelfOpenedTabGuard.
  self_opened_tab_at_ = base::TimeTicks::Now();
  return chrome::AddWebContents(browser, source, std::move(new_contents),
                                target_url, disposition, window_features,
                                NavigateParams::WindowAction::kShowWindow,
                                user_gesture);
}

content::WebContents* PeekWidget::OpenURLFromTab(
    content::WebContents* source,
    const content::OpenURLParams& params,
    base::OnceCallback<void(content::NavigationHandle&)>
        navigation_handle_callback) {
  Browser* browser = parent_ ? parent_->browser() : nullptr;
  if (!browser) {
    return nullptr;
  }
  VLOG(1) << "Atrium: peek OpenURLFromTab opening " << params.url
          << " as a new tab";
  NavigateParams nav_params(browser, params.url, params.transition);
  nav_params.FillNavigateParamsFromOpenURLParams(params);
  // Only CURRENT_TAB is remapped - the peek has no strip slot to be one for.
  // Other dispositions pass through, matching AddNewContents.
  if (nav_params.disposition == WindowOpenDisposition::CURRENT_TAB) {
    nav_params.disposition = WindowOpenDisposition::NEW_FOREGROUND_TAB;
  }
  nav_params.source_contents = nullptr;
  // Same reason as AddNewContents.
  self_opened_tab_at_ = base::TimeTicks::Now();
  nav_params.window_action = NavigateParams::WindowAction::kShowWindow;
  base::WeakPtr<content::NavigationHandle> handle = Navigate(&nav_params);
  if (navigation_handle_callback && handle) {
    std::move(navigation_handle_callback).Run(*handle);
  }
  return nav_params.navigated_or_inserted_contents;
}

void PeekWidget::SetOriginChip(const std::u16string& host, bool secure) {
  root_view_->SetOriginChip(host, secure);
}

void PeekWidget::ClearOriginChip() {
  root_view_->ClearOriginChip();
}

std::u16string PeekWidget::OriginChipHostForTesting() const {
  return root_view_ ? root_view_->OriginChipHostForTesting() : std::u16string();
}

bool PeekWidget::LoadingPlaceholderVisibleForTesting() const {
  return root_view_ && root_view_->LoadingPlaceholderVisibleForTesting();
}

void PeekWidget::RunDismissCallback() {
  if (dismiss_callback_) {
    dismiss_callback_.Run();
  }
}

content::WebContents* PeekWidget::controls_web_contents() {
  return controls_ ? controls_->web_contents() : nullptr;
}

bool PeekWidget::SelfOpenedTabGuardArmedForTesting() const {
  return base::TimeTicks::Now() - self_opened_tab_at_ < kSelfOpenedTabGuard;
}

void PeekWidget::SetDismissCallback(base::RepeatingClosure callback) {
  dismiss_callback_ = std::move(callback);
}

void PeekWidget::HideLoadingPlaceholder() {
  root_view_->SetLoadingPlaceholderVisible(false);
  // The card's real content just replaced the flat placeholder
  // fill - re-derive the window shadow now in case AppKit cached a shape
  // from the placeholder's own (compositor-driven, not drawRect-driven)
  // paint and hasn't picked up the change on its own.
  if (NSWindow* peek_ns_window = widget_->GetNativeWindow().GetNativeNSWindow()) {
    [peek_ns_window invalidateShadow];
  }
}

void PeekWidget::OnEscapePressed() {
  VLOG(1) << "Atrium: peek escape pressed for parent " << parent_.get();
  if (dismiss_callback_) {
    dismiss_callback_.Run();
  }
}

void PeekWidget::OnWidgetBoundsChanged(views::Widget* widget,
                                       const gfx::Rect& new_bounds) {
  // Only the PARENT's moves/resizes matter here; widget_'s own bounds
  // changes (including the ones this method itself causes) are ignored, so
  // this can never self-recurse.
  if (widget != parent_->widget()) {
    return;
  }
  if (!widget_->IsVisible()) {
    return;
  }
  ApplyParentBounds();
}

void PeekWidget::OnWidgetActivationChanged(views::Widget* widget,
                                           bool active) {
  // This observer also watches the parent widget (for bounds); the parent's
  // activation changes must not drive the card's auto-close logic.
  if (widget != widget_.get()) {
    return;
  }
  if (active) {
    // widget_ just reclaimed key - invalidate any
    // deferred dismiss check already in flight from an earlier deactivation
    // (see activation_loss_epoch_'s doc). Covers the fast Cmd+Tab-away-and-
    // back case: macOS can restore key to the peek before that deactivation's
    // posted MaybeDismissAfterAppActivationLoss task runs, and without this
    // bump that stale task would still see NSApp.active==YES and
    // intended_visible_==YES and wrongly dismiss the peek the user just
    // brought back.
    ++activation_loss_epoch_;
    return;  // Only losing key is a candidate for auto-close.
  }
  // A deactivation caused by a deliberate Close()/Hide() must not be fought -
  // see CommandBarWidget::OnWidgetActivationChanged for the reentrancy this
  // avoids.
  if (!intended_visible_) {
    return;
  }
  const base::TimeDelta since_shown = base::TimeTicks::Now() - shown_at_;
  if (since_shown < kFocusBounceGuard) {
    // Ignore-AND-RECLAIM: this is the widget's OWN Show()-driven activation
    // bounce, not a real user-driven focus loss - see CommandBarWidget's
    // identical branch for why re-activating (not just declining to close)
    // is required. Activate() below re-enters this method with active=true,
    // which bumps activation_loss_epoch_ - harmless (nothing is queued for
    // it to invalidate yet).
    widget_->Activate();
    return;
  }
  if (base::TimeTicks::Now() - self_opened_tab_at_ < kSelfOpenedTabGuard) {
    // Key went to a tab a link on this card opened, not to a window the user
    // picked. No Activate(): the card stays up without fighting the strip.
    return;
  }
  // A genuine loss of key past the guard window - but this fires on BOTH an
  // app switch (Finder, another app) and a within-Atrium focus move (parent
  // window, another Atrium window), and only the latter should dismiss (see
  // this class's header doc). Deferred to MaybeDismissAfterAppActivationLoss
  // - see that method's doc for why the decision cannot be made
  // synchronously here. `epoch` captures the current value so a reactivation
  // in the meantime (fast Cmd+Tab-away-and-back) can be detected as stale.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&PeekWidget::MaybeDismissAfterAppActivationLoss,
                      weak_factory_.GetWeakPtr(), activation_loss_epoch_));
}

void PeekWidget::MaybeDismissAfterAppActivationLoss(int epoch) {
  if (!intended_visible_) {
    // Already dismissed/closed by something else (Escape, promote) in the
    // runloop turn this was deferred across - nothing to do.
    return;
  }
  if (epoch != activation_loss_epoch_) {
    // widget_ reclaimed key (bumping the epoch) since
    // this was posted - a fast Cmd+Tab-away-and-back. The user brought the
    // peek back; do not dismiss it out from under them.
    VLOG(1) << "Atrium: peek activation-loss check stale (fast reactivation),"
               " not dismissing";
    return;
  }
  if (widget_->IsActive()) {
    // Belt-and-suspenders alongside the epoch check above: widget_ is key
    // RIGHT NOW, so whatever caused the deactivation this check was deferred
    // for is moot - do not dismiss a peek that currently has focus.
    return;
  }
  if (!NSApp.active) {
    // Atrium itself is no longer the active application - the key loss that
    // triggered this was an app switch (e.g. Cmd+Tab to Finder), not a
    // within-Atrium focus move. Keep the peek open; it is still there,
    // untouched, when the user switches back to Atrium.
    VLOG(1) << "Atrium: peek key loss was an app switch, not dismissing";
    return;
  }
  // Key went to another browser window: the card still sits over its own,
  // untouched, the way it survives an app switch.
  if (NSWindow* key = NSApp.keyWindow) {
    NSWindow* own = parent_->widget()->GetNativeWindow().GetNativeNSWindow();
    bool other_window_took_key = false;
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* browser) {
          NSWindow* candidate =
              browser->GetWindow()->GetNativeWindow().GetNativeNSWindow();
          other_window_took_key = candidate == key && candidate != own;
          return !other_window_took_key;
        });
    if (other_window_took_key) {
      VLOG(1) << "Atrium: peek key loss went to another window, not "
                 "dismissing";
      return;
    }
  }
  // Atrium is still the active app, and widget_ is not currently key: the
  // key loss was a genuine within-Atrium focus move (the user clicked the
  // parent window or another Atrium window) - run the dismiss callback
  // (BrowserController::ClosePeek), which calls Close() and destroys the
  // peek WebContents. This is NOT called directly from
  // OnWidgetActivationChanged so there is exactly one place that decides "a
  // dismiss happened" (see SetDismissCallback's doc).
  if (dismiss_callback_) {
    dismiss_callback_.Run();
  }
}

}  // namespace atrium
