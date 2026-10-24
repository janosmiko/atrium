// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_sidebar_overlay_widget.mm
#include "chrome/browser/ui/atrium/atrium_sidebar_overlay_widget.h"

#include <algorithm>
#include <utility>

#import <QuartzCore/QuartzCore.h>

#include "base/functional/bind.h"
#include "base/logging.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/atrium/atrium_corner_radius.h"
#include "chrome/browser/ui/atrium/atrium_window_corner_radius.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/background.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/controls/native/native_view_host.h"
#include "ui/views/controls/webview/webview.h"
#include "content/public/browser/web_contents.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"

namespace {
// how far in from the docked edge (left, or right if on_right_ -
// see AtriumSidebarOverlayWidget::SetOnRight) the reveal arms while the
// sidebar is collapsed. This was a 4pt strip, which in practice meant the
// cursor had to be pinned against the window edge before anything happened;
// at 32pt the sidebar reveals as the cursor approaches. Mirrored by
// SidebarOverlayProximity.revealZoneWidth (Swift), which pins it under test.
constexpr int kProximityRevealWidth = 32;
// Hide debounce - see SidebarOverlayHideDebounce (Swift) which pins the same
// value under test. It is not "the hide rule" but "when the
// hide rule is first consulted" (see HideAfterDebounce).
// 280, the lab's own grace (swipe-lab.html:5372). The reporter found
// 200 too quick once the panel stopped closing outright on the zone exit.
constexpr int kHideDebounceMs = 280;
// how far outside the overlay the pointer may stray and still count
// as inside. A bare mouseExited cannot tell a 3pt slip from leaving for the
// page; this band can. SidebarOverlayDismissPolicy.toleranceBand (Swift).
constexpr int kStickyToleranceBand = 24;
// re-check cadence while the pointer is out but not yet gone. Short
// enough that a deliberate move away dismisses promptly once the initial
// debounce has elapsed.
constexpr int kExitRecheckMs = 60;
// a live width push older than this is a finished drag. Paired with
// the primary button still being held, so the hide suppression has two
// independent expiries and cannot latch on.
constexpr int kResizeDragIdleMs = 250;
// Must match swipe-lab.html's own transition duration, and is shared by
// Show()'s reveal, Close()'s dismiss and the frost's fade so the three never
// drift out of sync with each other.
constexpr NSTimeInterval kOverlaySlideDurationSeconds = 0.24;
// Margins around the floating overlay panel (todo 7 + user follow-up): the
// overlay reads as a floating card, not a full-height slab - it sits clear
// of the window's top/bottom edges AND floats a step in from its docked
// edge (flush-to-edge read as attached rather than floating).
// User-tuned to 5, not derived: the content card's full gutter read too far
// in. It stays 5 even when the borderWidth setting moves that gutter, and it
// no longer matches the lab's own 8 (swipe-lab.html).
constexpr int kOverlayVerticalInset = 5;
constexpr int kOverlayHorizontalInset = 5;
// concentric with the window corner across the overlay's own step-in
// gap, so the panel's curve stays parallel to the window's instead of pinching
// at the 45-degree point. Sitting only kOverlayHorizontalInset from the edge,
// the overlay is ROUNDER than the content card (a 10pt gutter) - different
// nesting depth, different radius. Both being a flat 12 was the bug.
//
// SidebarView.swift's sidebarClipShape derives the overlay case the same way
// from the same window radius, so the two cannot drift apart; changing one
// without the other clips the SwiftUI content against a different curve than
// this layer masks to.
CGFloat OverlayCornerRadius() {
  return atrium::ConcentricCornerRadius(atrium::AtriumWindowCornerRadius(),
                                        kOverlayHorizontalInset);
}

class PanelBackdropBackground : public views::Background {
 public:
  PanelBackdropBackground(atrium::AtriumBrowserWindow* window,
                          views::Widget* widget)
      : window_(window), widget_(widget) {}

  void Paint(gfx::Canvas* canvas, views::View* view) const override {
    window_->PaintFloatingPanelBackdrop(canvas,
                                        widget_->GetWindowBoundsInScreen());
  }

 private:
  const raw_ptr<atrium::AtriumBrowserWindow> window_;
  const raw_ptr<views::Widget> widget_;
};
}  // namespace

// Objective-C mouse-tracking helper: installs an NSTrackingArea on a view and
// forwards enter/exit to C++ closures. Two instances per overlay widget - one
// for the parent's docked-edge hover strip, one for the overlay's own bounds.
@interface AtriumOverlayTracker : NSObject
- (instancetype)initWithEnter:(base::RepeatingClosure)enter
                         exit:(base::RepeatingClosure)exit;
- (void)attachToView:(NSView*)view
                rect:(NSRect)rect
      useVisibleRect:(BOOL)useVisibleRect;
- (void)detach;
@end

@implementation AtriumOverlayTracker {
  base::RepeatingClosure _enter;
  base::RepeatingClosure _exit;
  NSTrackingArea* __strong _area;
  NSView* __weak _view;
}

- (instancetype)initWithEnter:(base::RepeatingClosure)enter
                         exit:(base::RepeatingClosure)exit {
  if ((self = [super init])) {
    _enter = std::move(enter);
    _exit = std::move(exit);
  }
  return self;
}

- (void)attachToView:(NSView*)view
                rect:(NSRect)rect
      useVisibleRect:(BOOL)useVisibleRect {
  [self detach];
  if (!view) {
    return;
  }
  _view = view;
  NSTrackingAreaOptions opts =
      NSTrackingMouseEnteredAndExited | NSTrackingActiveAlways;
  if (useVisibleRect) {
    // The overlay tracker covers the whole overlay; the mouse is already
    // inside when it appears, so assume-inside makes the first mouse-out
    // deliver a mouseExited: correctly.
    opts |= NSTrackingInVisibleRect | NSTrackingAssumeInside;
  }
  _area = [[NSTrackingArea alloc] initWithRect:rect options:opts owner:self
                                     userInfo:nil];
  [view addTrackingArea:_area];
}

- (void)detach {
  if (_area && _view) {
    [_view removeTrackingArea:_area];
  }
  _area = nil;
  _view = nil;
}

- (void)mouseEntered:(NSEvent*)event {
  if (_enter) {
    _enter.Run();
  }
}

- (void)mouseExited:(NSEvent*)event {
  if (_exit) {
    _exit.Run();
  }
}
@end

namespace atrium {

namespace {

// The parent's content view, where the docked-edge proximity zone lives.
NSView* ParentContentView(AtriumBrowserWindow* parent) {
  NSWindow* window = parent->GetNSWindowForAnimation();
  return window ? [window contentView] : nil;
}

// whether the cursor is inside `window`'s frame grown by `slack`.
// Both -[NSWindow frame] and +[NSEvent mouseLocation] are unflipped screen
// coordinates, so they compare directly - unlike widget bounds, which views
// reports top-left-origin.
bool PointerNearWindow(NSWindow* window, CGFloat slack) {
  if (!window) {
    return false;
  }
  const NSRect frame = [window frame];
  if (NSIsEmptyRect(frame)) {
    return false;
  }
  return NSMouseInRect([NSEvent mouseLocation], NSInsetRect(frame, -slack,
                                                            -slack),
                       /*isFlipped=*/NO);
}

}  // namespace

AtriumSidebarOverlayWidget::AtriumSidebarOverlayWidget(
    AtriumBrowserWindow* parent)
    : parent_(parent) {
  widget_ = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params.name = "AtriumSidebarOverlay";
  // Translucent so the sidebar's own glass material reads correctly; NOT
  // activatable (unlike the command bar) so revealing it never steals key
  // focus from the web content - typing/clicks in the page keep working.
  params.opacity = views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.activatable = views::Widget::InitParams::Activatable::kNo;
  params.SetParent(parent_->widget());

  widget_delegate_ = std::make_unique<views::WidgetDelegate>();
  widget_delegate_->SetCanActivate(false);
  // Empty until HoldContents hands the sidebar over. A WebView with no
  // contents draws nothing, which is what a hidden panel should do.
  //
  // The context is NOT null. A WebView built without one still registers as a
  // views::AXUpdateObserver, and the adapter that wraps an observer takes a
  // weak pointer from it - which DCHECKed !needs_adopt_ref_ inside this very
  // constructor. The stack named AddObserver and nothing of ours.
  auto backdrop = std::make_unique<views::View>();
  backdrop->SetLayoutManager(std::make_unique<views::FillLayout>());
  backdrop->SetBackground(
      std::make_unique<PanelBackdropBackground>(parent_, widget_.get()));
  content_web_ = backdrop->AddChildView(
      std::make_unique<views::WebView>(parent_->browser()->profile()));
  backdrop_ = widget_delegate_->SetContentsView(std::move(backdrop));
  params.delegate = widget_delegate_.get();

  widget_->Init(std::move(params));
  widget_->AddObserver(this);
  // Follows the parent's moves/resizes while visible - the same continuous
  // parent-bounds tracking the command bar uses. Removed in the destructor,
  // which AtriumBrowserWindow's destructor runs before resetting its own
  // widget_, so this observer can never dangle across the parent's teardown.
  parent_->widget()->AddObserver(this);

  // Non-opaque with a clear background, so the page's own alpha reaches the
  // screen and the corner mask below has something to clip.
  //
  // no NSVisualEffectView. This window is a CHILD window, and a
  // child window's material samples nothing - measured against a checkerboard,
  // it dropped the region's saturation from 220 to 10 while the page under it
  // stayed at 206 with the material removed. It was painting a flat fill, not
  // the page. PanelBackdropBackground paints the window's own stack instead.
  if (NSWindow* ns_window = widget_->GetNativeWindow().GetNativeNSWindow()) {
    ns_window.opaque = NO;
    ns_window.backgroundColor = NSColor.clearColor;
  }

  // Strip tracker: reveals the overlay when the mouse enters the parent's
  // left edge AND the sidebar is collapsed. Installed now (the parent content
  // view already exists); re-sized on every parent bounds change.
  strip_tracker_ = [[AtriumOverlayTracker alloc]
      initWithEnter:base::BindRepeating(
                        &AtriumSidebarOverlayWidget::OnStripEntered,
                        base::Unretained(this))
               exit:base::RepeatingClosure()];
  // POSTED, never inline. AppKit can deliver mouseEntered while a tracking
  // area is installed under a pointer already inside it, and OnStripEntered
  // calls Show - so an inline install can run Show from inside this
  // constructor, before anything owns the object. A reveal re-entered from
  // there sees a null sidebar_overlay_widget_, builds a SECOND widget, and the
  // assignment destroys one whose callback is still on the stack.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&AtriumSidebarOverlayWidget::UpdateStripTracking,
                     weak_factory_.GetWeakPtr()));

  overlay_tracker_ = [[AtriumOverlayTracker alloc]
      initWithEnter:base::BindRepeating(
                        &AtriumSidebarOverlayWidget::OnOverlayEntered,
                        base::Unretained(this))
               exit:base::BindRepeating(
                        &AtriumSidebarOverlayWidget::OnOverlayExited,
                        base::Unretained(this))];
}

AtriumSidebarOverlayWidget::~AtriumSidebarOverlayWidget() {
  parent_->widget()->RemoveObserver(this);
  widget_->RemoveObserver(this);
  [strip_tracker_ detach];
  strip_tracker_ = nil;
  [overlay_tracker_ detach];
  overlay_tracker_ = nil;
}

void AtriumSidebarOverlayWidget::ApplyBounds(int width) {
  // Near the parent's docked edge (left, or right if on_right_ - see
  // SetOnRight) but inset a step (kOverlayHorizontalInset), `width` wide,
  // shy of full height by the top/bottom margins - screen coordinates
  // (top-level widget; see CommandBarWidget::ApplyParentBounds).
  const gfx::Rect parent_screen =
      parent_->widget()->GetWindowBoundsInScreen();
  const int x = on_right_
                    ? parent_screen.right() - kOverlayHorizontalInset - width
                    : parent_screen.x() + kOverlayHorizontalInset;
  widget_->SetBounds(
      gfx::Rect(x, parent_screen.y() + kOverlayVerticalInset, width,
                parent_screen.height() - 2 * kOverlayVerticalInset));
  // In the parent's own coordinates, so its width grip can sit on the edge the
  // pointer sees rather than on the collapsed pane's zero-width seam.
  const int local_x = x - parent_screen.x();
  parent_->PublishFloatingSidebarSpan(
      gfx::Range(local_x, local_x + width));
  // every reveal, live resize and parent move goes through here, so
  // this one call keeps the frost under the panel wherever it lands.
  parent_->SetSidebarFrostFrame(widget_->GetWindowBoundsInScreen(),
                                OverlayCornerRadius());
}

void AtriumSidebarOverlayWidget::UpdateStripTracking() {
  NSView* content = ParentContentView(parent_);
  if (!content) {
    return;
  }
  // The proximity zone lives on the docked edge - left by default, right if
  // on_right_ (see SetOnRight). Clamped to the window so a window narrower
  // than the zone never yields a negative origin.
  const CGFloat available = content.bounds.size.width;
  const CGFloat width =
      std::max<CGFloat>(0, std::min<CGFloat>(kProximityRevealWidth, available));
  const CGFloat x = on_right_ ? available - width : 0;
  const NSRect strip = NSMakeRect(x, 0, width, content.bounds.size.height);
  [strip_tracker_ attachToView:content rect:strip useVisibleRect:NO];
}

void AtriumSidebarOverlayWidget::SetOnRight(bool on_right) {
  on_right_ = on_right;
  UpdateStripTracking();
  if (IsShowing()) {
    ApplyBounds(parent_->LastDockedSidebarWidth());
  }
}

void AtriumSidebarOverlayWidget::Show(int width) {
  if (width <= 0) {
    return;
  }
  // Here rather than at the call sites. Two things reveal this panel - the
  // parent's RevealFloatingSidebar and OnStripEntered below, which arm on the
  // same window edge - and only the first used to hand the sidebar over. A
  // reveal that won the race through the other one showed an empty panel
  //. Taking the contents here means no caller can forget.
  parent_->HandSidebarToPanel();
  hide_timer_.Stop();
  // Fresh reveal - no drag and no exit carried over from the last one.
  last_resize_push_ = base::TimeTicks();
  exit_time_ = base::TimeTicks();
  ApplyBounds(width);
  // Bumped so a dismissal still animating from the previous cycle cannot
  // finish onto this one - see Close's completion block.
  ++cycle_;
  VLOG(1) << "Atrium: sidebar overlay revealing at width " << width;
  // Animated reveal: the CONTENT slides in from behind the window's docked
  // edge (left, or right if on_right_ - Arc-style). The window FRAME never
  // moves - animating it would fight ApplyBounds/OnWidgetBoundsChanged's
  // parent tracking, and a child window sliding outside the parent would
  // draw over the desktop; a layer translation is clipped by the window
  // surface, so the slide reveals naturally at the edge. ShowInactive:
  // reveal without taking key focus from the web content.
  widget_->ShowInactive();
  parent_->OrderOverlayChild(widget_.get(),
                             AtriumBrowserWindow::OverlayLayer::kSidebarPanel);
  NSWindow* overlay_ns_window = widget_->GetNativeWindow().GetNativeNSWindow();
  // Re-enable after Close() turned it off. Without this, a reveal that
  // interrupted a close (cycle guard in Close) inherits the old window's
  // ignoresMouseEvents=YES and the panel does not take clicks.
  overlay_ns_window.ignoresMouseEvents = NO;
  if (NSView* content = overlay_ns_window.contentView) {
    content.wantsLayer = YES;
    // Keep a rounded clip at the window edge while the content's own rounded
    // edge travels. Translating the only mask leaves a square slice through
    // the content at the dock side for every intermediate frame.
    // This frameless window's frame view has the same bounds as its content.
    NSView* frame = content.superview;
    frame.wantsLayer = YES;
    frame.layer.cornerRadius = OverlayCornerRadius();
    frame.layer.masksToBounds = YES;
    // The panel clips itself, and keeps clipping after the slide. It does not
    // restore masksToBounds=NO on completion. No other view rounds the
    // panel, so the corners would turn square the moment the slide finished. The window's own shadow is drawn outside the
    // content view by the window server, so clipping here does not touch it.
    // All four corners: ApplyBounds insets the dock-side edge too, so that
    // edge's corners are on screen as well.
    content.layer.cornerRadius = OverlayCornerRadius();
    content.layer.maskedCorners =
        kCALayerMinXMinYCorner | kCALayerMaxXMinYCorner |
        kCALayerMinXMaxYCorner | kCALayerMaxXMaxYCorner;
    content.layer.masksToBounds = YES;
    // Explicit, like Close(): the slide and the frost's fade must commit in
    // the same frame, not merely in the same run-loop turn.
    [CATransaction begin];
    CABasicAnimation* slide =
        [CABasicAnimation animationWithKeyPath:@"transform.translation.x"];
    slide.fromValue = on_right_ ? @(width) : @(-width);
    slide.toValue = @0;
    // the lab's own timing, not an approximation. swipe-lab.html sets
    // transform .24s cubic-bezier(.32,.72,.35,1) on the hidden sidebar, and a
    // named ease-out is visibly different over a slide this long.
    slide.duration = kOverlaySlideDurationSeconds;
    slide.timingFunction = [CAMediaTimingFunction functionWithControlPoints:
                                                      0.32:0.72:0.35:1.0];
    [content.layer addAnimation:slide forKey:@"atriumOverlaySlide"];
    parent_->AnimateSidebarFrostShow(
        base::Seconds(kOverlaySlideDurationSeconds),
        on_right_ ? width : -width);
    [CATransaction commit];
  }

  // Track mouse-out on the overlay's own bounds so it auto-hides.
  NSWindow* overlay_window = widget_->GetNativeWindow().GetNativeNSWindow();
  if (overlay_window) {
    [overlay_tracker_ attachToView:[overlay_window contentView]
                              rect:NSZeroRect
                    useVisibleRect:YES];
  }
}

void AtriumSidebarOverlayWidget::HoldContents(content::WebContents* contents) {
  if (content_web_) {
    content_web_->SetWebContents(contents);
  }
}

void AtriumSidebarOverlayWidget::RepaintBackdrop() {
  if (backdrop_ && widget_->IsVisible()) {
    backdrop_->SchedulePaint();
  }
}

SkColor AtriumSidebarOverlayWidget::BackdropPixelForTesting() const {
  const gfx::Rect bounds = backdrop_ ? backdrop_->GetLocalBounds() : gfx::Rect();
  if (bounds.IsEmpty()) {
    return SK_ColorTRANSPARENT;
  }
  gfx::Canvas canvas(bounds.size(), 1.0f, /*is_opaque=*/false);
  backdrop_->GetBackground()->Paint(&canvas, backdrop_);
  return canvas.GetBitmap().getColor(bounds.width() / 2, bounds.height() / 2);
}

void AtriumSidebarOverlayWidget::Close() {
  hide_timer_.Stop();
  last_resize_push_ = base::TimeTicks();
  exit_time_ = base::TimeTicks();
  [overlay_tracker_ detach];
  if (!widget_->IsVisible()) {
    return;
  }
  VLOG(1) << "Atrium: sidebar overlay closing";
  // Animated dismiss, mirroring Show()'s slide-in: the content slides back
  // behind the window's docked edge (left, or right if on_right_), then the
  // widget hides. The completion is guarded by a weak pointer: it can fire
  // after teardown.
  NSWindow* overlay_ns_window = widget_->GetNativeWindow().GetNativeNSWindow();
  // macOS 27: the CATransaction completion can miss, leaving the window up
  // after the content slides off-screen. Show() re-enables.
  overlay_ns_window.ignoresMouseEvents = YES;
  NSView* content = overlay_ns_window.contentView;
  if (!content) {
    FinishHide();
    return;
  }
  // todo 58: keep the same all-four-corner rounded clip through the
  // slide-OUT (mirrors Show).
  content.layer.cornerRadius = OverlayCornerRadius();
  content.layer.maskedCorners =
      kCALayerMinXMinYCorner | kCALayerMaxXMinYCorner |
      kCALayerMinXMaxYCorner | kCALayerMaxXMaxYCorner;
  content.layer.masksToBounds = YES;
  const CGFloat width = widget_->GetWindowBoundsInScreen().width();
  base::WeakPtr<AtriumSidebarOverlayWidget> weak = weak_factory_.GetWeakPtr();
  // The cycle this dismissal belongs to. A reveal that lands before the slide
  // finishes bumps it, and this completion then does nothing - otherwise it
  // hides a panel that has just been shown and hands its contents back from
  // under it. The weak pointer alone is not enough: the object is still very
  // much alive, it has simply moved on.
  const uint64_t cycle = ++cycle_;
  [CATransaction begin];
  [CATransaction setCompletionBlock:^{
    if (weak && weak->cycle_ == cycle) {
      weak->FinishHide();
    }
  }];
  CABasicAnimation* slide =
      [CABasicAnimation animationWithKeyPath:@"transform.translation.x"];
  slide.fromValue = @0;
  slide.toValue = on_right_ ? @(width) : @(-width);
  // The same curve out as in - the lab uses one transition for both directions.
  slide.duration = kOverlaySlideDurationSeconds;
  slide.timingFunction = [CAMediaTimingFunction functionWithControlPoints:
                                                    0.32:0.72:0.35:1.0];
  // Hold the off-screen position until FinishHide actually hides the widget
  // (otherwise the layer snaps back for one frame at animation end).
  slide.fillMode = kCAFillModeForwards;
  slide.removedOnCompletion = NO;
  [content.layer addAnimation:slide forKey:@"atriumOverlaySlide"];
  // Started in the same transaction as the slide so the frost fades out in
  // step with it instead of sitting at full strength until FinishHide.
  parent_->AnimateSidebarFrostHide(
      base::Seconds(kOverlaySlideDurationSeconds),
      on_right_ ? width : -width);
  [CATransaction commit];
}

void AtriumSidebarOverlayWidget::FinishHide() {
  widget_->Hide();
  parent_->HideSidebarFrost();
  parent_->PublishFloatingSidebarSpan(std::nullopt);
  // POSTED. Moving a WebContents between two WebViews reparents a compositing
  // surface, and doing that inside a frame trips a DCHECK deep in cc's tile
  // manager - a stack with no atrium frames in it at all. This runs from a
  // CAAnimation completion, which can land mid-frame, so the handover waits
  // for the next task.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&AtriumBrowserWindow::DockFloatingSidebar,
                                parent_->GetWeakPtr()));
  if (NSWindow* overlay_ns_window =
          widget_->GetNativeWindow().GetNativeNSWindow()) {
    [overlay_ns_window.contentView.layer
        removeAnimationForKey:@"atriumOverlaySlide"];
  }
}

bool AtriumSidebarOverlayWidget::IsShowing() const {
  return widget_->IsVisible();
}

void AtriumSidebarOverlayWidget::DismissForWindowCommand() {
  if (!IsShowing()) {
    return;
  }
  hide_timer_.Stop();
  [overlay_tracker_ detach];
  // FinishHide rather than Close: Close animates for 240ms and hides in the
  // completion, and the command that follows this needs the child window gone
  // now. The contents go back to the docked pane the same way either path
  // ends up doing it.
  FinishHide();
}

int AtriumSidebarOverlayWidget::WidthForTesting() const {
  return IsShowing() ? widget_->GetWindowBoundsInScreen().width() : 0;
}

gfx::Rect AtriumSidebarOverlayWidget::BoundsForTesting() const {
  return IsShowing() ? widget_->GetWindowBoundsInScreen() : gfx::Rect();
}

float AtriumSidebarOverlayWidget::CornerRadiusForTesting() const {
  if (!IsShowing()) {
    return 0;
  }
  NSView* content =
      widget_->GetNativeWindow().GetNativeNSWindow().contentView;
  return content ? content.layer.cornerRadius : 0;
}

double AtriumSidebarOverlayWidget::SlideDxForTesting() const {
  if (!IsShowing()) {
    return 0;
  }
  NSView* content =
      widget_->GetNativeWindow().GetNativeNSWindow().contentView;
  if (!content) {
    return 0;
  }
  CALayer* live = content.layer.presentationLayer ?: content.layer;
  return live.transform.m41;
}

void AtriumSidebarOverlayWidget::ResizeTo(int width) {
  if (!IsShowing()) {
    return;
  }
  // the op carries no drag begin/end, so the push itself is the
  // signal - see IsResizeDragActive. A resize drag is engagement: ApplyBounds
  // below moves the overlay's own bounds, and the throttled updates trail the
  // cursor, both of which fire mouseExited mid-drag.
  last_resize_push_ = base::TimeTicks::Now();
  // Do NOT cancel a pending check outright. If the drag ends with the cursor
  // outside, nothing fires a fresh mouseExited, and cancelling here would
  // leave the overlay stuck open; keeping a check armed makes the drag's end
  // the moment dismissal is reconsidered. HideAfterDebounce holds the overlay
  // open for as long as the drag is live.
  if (hide_timer_.IsRunning()) {
    // The grace cap measures idle time outside the overlay, and a long drag
    // is not idle - without this a 5-second resize would exhaust the cap and
    // dismiss the instant the button came up.
    exit_time_ = base::TimeTicks::Now();
    ArmHideCheck(base::Milliseconds(kExitRecheckMs));
  }
  ApplyBounds(width);
}

void AtriumSidebarOverlayWidget::HideForDock() {
  Close();
}

void AtriumSidebarOverlayWidget::OnStripEntered() {
  // Only reveal while the pane is genuinely collapsed - hovering the docked
  // edge of a docked sidebar does nothing.
  if (!parent_->IsSidebarCollapsed()) {
    return;
  }
  // kProximityRevealWidth is wide enough to cover the window's own
  // resize border, and on a narrow window the overlay's resize handle too.
  // Dropping the overlay's window under a grab already in progress would
  // steal that drag, so a zone entered with a button held reveals nothing.
  // A drag STARTED after a reveal still works: ApplyBounds leaves the
  // window's outermost kOverlayHorizontalInset points uncovered.
  if ([NSEvent pressedMouseButtons] != 0) {
    return;
  }
  // Already up: this is a re-entry, not a reveal. Show would re-run the whole
  // slide - bounds, ShowInactive, the animation from off-screen - on a panel
  // standing still, which reads as the panel vanishing and coming back. The
  // strip and the panel overlap along the docked edge, so a pointer resting
  // near that border crosses in and out of the strip repeatedly and produced
  // one of these each time. Cancel the pending hide instead, which is what
  // the lab's edgehot does (swipe-lab.html:5366) and what the parent's own
  // reveal path already does.
  if (IsShowing()) {
    OnOverlayEntered();
    return;
  }
  Show(parent_->LastDockedSidebarWidth());
}

void AtriumSidebarOverlayWidget::OnOverlayEntered() {
  // Mouse came back onto the overlay - cancel any pending hide and forget the
  // grace already spent, so the next exit gets the full tolerance again.
  hide_timer_.Stop();
  exit_time_ = base::TimeTicks();
}

void AtriumSidebarOverlayWidget::OnOverlayExited() {
  if (!IsShowing()) {
    return;
  }
  // Only the FIRST exit of a run starts the grace clock; a re-exit while a
  // check is already pending (the overlay's own bounds moving under the
  // cursor can produce a burst) must not keep extending it.
  if (exit_time_.is_null()) {
    exit_time_ = base::TimeTicks::Now();
  }
  ArmHideCheck(base::Milliseconds(kHideDebounceMs));
}

void AtriumSidebarOverlayWidget::ArmHideCheck(base::TimeDelta delay) {
  hide_timer_.Start(
      FROM_HERE, delay,
      base::BindOnce(&AtriumSidebarOverlayWidget::HideAfterDebounce,
                     base::Unretained(this)));
}

void AtriumSidebarOverlayWidget::HideAfterDebounce() {
  if (!IsShowing()) {
    return;
  }
  // hold through a live resize drag. The cursor is stationary on
  // the handle while the overlay's own bounds move away from it, so its
  // position says nothing about intent - and the grace cap must not run down
  // during a long drag, hence the reset.
  if (IsResizeDragActive()) {
    exit_time_ = base::TimeTicks::Now();
    ArmHideCheck(base::Milliseconds(kExitRecheckMs));
    return;
  }
  // decide on where the cursor actually IS, not on the exit event
  // that got us here. Back over the overlay counts as a re-entry even without
  // a mouseEntered - which is exactly the case a bounds change under a
  // stationary cursor produces. Keep the check armed at the slow cadence
  // instead of dropping the run: when our view of the pointer and Cocoa's
  // disagree, no further mouseEntered/mouseExited need ever arrive to resolve
  // it, and a stuck-open overlay is worse than a 200ms rect test.
  // OnOverlayEntered cancels this the moment Cocoa agrees.
  if (IsPointerOverOverlay()) {
    exit_time_ = base::TimeTicks::Now();
    ArmHideCheck(base::Milliseconds(kHideDebounceMs));
    return;
  }
  // Still within a few points of the overlay: a slip, not a departure. No cap
  // on how long that may last. The cap forced a close on a pointer that was
  // sitting still in the band, and the band's inner edge is where the panel's
  // own window buttons are - so the panel slid out from under the pointer,
  // the strip underneath it then revealed it again, and the pair repeated for
  // as long as the pointer stayed there. The lab has no cap either: it looks
  // once, 280ms after the leave, and a pointer inside the expanded rect keeps
  // the sidebar open for as long as it stays (swipe-lab.html:5368-5375). The
  // re-check stands so that leaving still closes it promptly.
  if (IsPointerWithinToleranceBand()) {
    ArmHideCheck(base::Milliseconds(kExitRecheckMs));
    return;
  }
  Close();
}

bool AtriumSidebarOverlayWidget::IsPointerOverOverlay() const {
  return PointerNearWindow(widget_->GetNativeWindow().GetNativeNSWindow(),
                           /*slack=*/0);
}

bool AtriumSidebarOverlayWidget::IsPointerWithinToleranceBand() const {
  return PointerNearWindow(widget_->GetNativeWindow().GetNativeNSWindow(),
                           kStickyToleranceBand);
}

bool AtriumSidebarOverlayWidget::IsResizeDragActive() const {
  if (last_resize_push_.is_null()) {
    return false;
  }
  // Two independent expiries. Either alone could latch: pushes stop arriving
  // the moment the drag pauses, and a button-up we never observe would pin
  // the suppression forever.
  if (base::TimeTicks::Now() - last_resize_push_ >
      base::Milliseconds(kResizeDragIdleMs)) {
    return false;
  }
  return ([NSEvent pressedMouseButtons] & 1) != 0;
}

void AtriumSidebarOverlayWidget::OnWidgetBoundsChanged(
    views::Widget* widget,
    const gfx::Rect& new_bounds) {
  // Only the PARENT's moves/resizes matter; the overlay's own bounds changes
  // (including the ones this method causes) are ignored, so no self-recursion.
  if (widget != parent_->widget()) {
    return;
  }
  // The strip's height tracks the parent even while hidden, so the next
  // reveal is correctly sized.
  UpdateStripTracking();
  if (!IsShowing()) {
    return;
  }
  ApplyBounds(parent_->LastDockedSidebarWidth());
}

}  // namespace atrium
