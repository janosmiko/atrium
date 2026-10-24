// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_window_platform_mac.mm
#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include "chrome/browser/ui/atrium/atrium_window_platform_mac.h"

#include <dlfcn.h>

#include <algorithm>

#include "components/input/native_web_keyboard_event.h"
#import "components/remote_cocoa/app_shim/native_widget_ns_window_bridge.h"
#include "ui/views/cocoa/native_widget_mac_ns_window_host.h"
#include "ui/views/widget/widget.h"

// NSGlassEffectView intercepts hit testing even when added below the content
// views. Returning nil lets clicks pass through to the sibling subviews.
API_AVAILABLE(macos(26.0))
@interface AtriumGlassBackdropView : NSGlassEffectView
@end

@implementation AtriumGlassBackdropView
- (NSView*)hitTest:(NSPoint)point {
  return nil;
}
@end

namespace atrium {
namespace {

// the backdrop is tagged so UpdateCornerMask can re-find exactly
// THIS view. Matching on class would also catch any other effect view that
// ends up a direct child of the content view, and mask that too.
NSString* const kGlassBackdropIdentifier = @"AtriumGlassBackdrop";

ServerCornerRadiusPush g_last_push;

// AppKit sets the server-side corner radius only for an opaque window, so a
// SkyLight reader (jankyborders) sees 0 for ours and draws a 9pt corner.
// Private API, dlsym'd: CoreGraphics does not re-export SLSTransaction*.
void SetServerCornerRadius(NSWindow* ns_window, double radius) {
  using ConnectionFn = int (*)();
  using CreateFn = CFTypeRef (*)(int);
  using SetFn = void (*)(CFTypeRef, uint32_t, double);
  using CommitFn = void (*)(CFTypeRef, int);
  static void* const skylight =
      dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/SkyLight",
             RTLD_LAZY | RTLD_LOCAL);
  static const auto connection = reinterpret_cast<ConnectionFn>(
      skylight ? dlsym(skylight, "SLSMainConnectionID") : nullptr);
  static const auto create = reinterpret_cast<CreateFn>(
      skylight ? dlsym(skylight, "SLSTransactionCreate") : nullptr);
  static const auto set = reinterpret_cast<SetFn>(
      skylight ? dlsym(skylight, "SLSTransactionSetWindowCornerRadius")
               : nullptr);
  // macOS 26 reads the SYSTEM radius, which AppKit fills in for an opaque
  // window and skips for ours - setting only the one above left readers on 0
  //. Absent before 26, so the legacy setter stays.
  static const auto set_system = reinterpret_cast<SetFn>(
      skylight ? dlsym(skylight, "SLSTransactionSetWindowSystemCornerRadius")
               : nullptr);
  static const auto commit = reinterpret_cast<CommitFn>(
      skylight ? dlsym(skylight, "SLSTransactionCommit") : nullptr);
  // Each arm returns silently while the caller still sets the LAYER radius, so
  // the window looks right and a SkyLight reader sees a square one.
  if (!connection || !create || !set || !commit) {
    VLOG(1) << "Atrium: server corner radius skipped, SkyLight symbols missing";
    return;
  }
  if (ns_window.windowNumber <= 0) {
    VLOG(1) << "Atrium: server corner radius skipped, window not on screen";
    return;
  }
  CFTypeRef transaction = create(connection());
  if (!transaction) {
    VLOG(1) << "Atrium: server corner radius skipped, no transaction";
    return;
  }
  const auto window_id = static_cast<uint32_t>(ns_window.windowNumber);
  set(transaction, window_id, radius);
  bool system_setter_ran = false;
  if (set_system) {
    set_system(transaction, window_id, radius);
    system_setter_ran = true;
  }
  commit(transaction, 0);
  CFRelease(transaction);
  g_last_push.count++;
  g_last_push.radius = radius;
  g_last_push.system_setter_ran = system_setter_ran;
  VLOG(1) << "Atrium: server corner radius pushed " << radius << " to window "
          << window_id << (set_system ? " (legacy+system)" : " (legacy only)");
}

// AppKit overwrites the pushed server radius with its own on every flush, and
// jankyborders reads once at order-in. Private, macOS 26 only.
void SetAppKitCornerRadius(NSWindow* ns_window, double radius) {
  SEL setter = NSSelectorFromString(@"_setCornerRadius:");
  if (![ns_window respondsToSelector:setter]) {
    return;
  }
  using SetterFn = void (*)(id, SEL, CGFloat);
  reinterpret_cast<SetterFn>([ns_window methodForSelector:setter])(
      ns_window, setter, radius);
}

NSWindow* NativeWindow(views::Widget* widget) {
  return widget->GetNativeWindow().GetNativeNSWindow();
}

NSString* const kLightSlideKey = @"atriumLightSlide";
NSString* const kLightFadeKey = @"atriumLightFade";

NSArray<NSButton*>* LightButtons(NSWindow* ns_window) {
  return @[
    [ns_window standardWindowButton:NSWindowCloseButton],
    [ns_window standardWindowButton:NSWindowMiniaturizeButton],
    [ns_window standardWindowButton:NSWindowZoomButton]
  ];
}

// The presentation values, so a reveal landing on a still-running collapse
// hands off from what is on screen instead of snapping back first.
CALayer* LiveLayer(CALayer* layer) {
  return layer.presentationLayer ?: layer;
}

// Test-only. The bounds of what `light` actually paints, in its own points.
// AppKit sizes the button to its hit target, several points wider than the
// circle, so the frame is not the light's size.
NSRect DrawnInkRect(NSButton* light) {
  NSRect bounds = light.bounds;
  NSBitmapImageRep* rep = [light bitmapImageRepForCachingDisplayInRect:bounds];
  if (!rep || NSIsEmptyRect(bounds)) {
    return NSZeroRect;
  }
  [light cacheDisplayInRect:bounds toBitmapImageRep:rep];
  const NSInteger wide = rep.pixelsWide;
  const NSInteger high = rep.pixelsHigh;
  NSInteger min_x = wide, max_x = -1, min_y = high, max_y = -1;
  for (NSInteger y = 0; y < high; ++y) {
    for (NSInteger x = 0; x < wide; ++x) {
      // Half cover, so the circle's antialiased rim counts as outside and the
      // measurement is the same one an eye makes.
      if ([rep colorAtX:x y:y].alphaComponent < 0.5) {
        continue;
      }
      min_x = std::min(min_x, x);
      max_x = std::max(max_x, x);
      min_y = std::min(min_y, y);
      max_y = std::max(max_y, y);
    }
  }
  if (max_x < min_x) {
    return NSZeroRect;
  }
  const CGFloat scale = wide / NSWidth(bounds);
  return NSMakeRect(NSMinX(bounds) + min_x / scale,
                    NSMinY(bounds) + min_y / scale,
                    (max_x - min_x + 1) / scale, (max_y - min_y + 1) / scale);
}

}  // namespace

ServerCornerRadiusPush LastServerCornerRadiusPush() {
  return g_last_push;
}

AtriumWindowPlatform::AtriumWindowPlatform(views::Widget* widget)
    : widget_(widget) {}

AtriumWindowPlatform::~AtriumWindowPlatform() = default;

void AtriumWindowPlatform::SetWindowControlsVisible(
    bool visible,
    base::TimeDelta duration,
    base::OnceCallback<bool()> confirm_hide) {
  // Resolved per call rather than cached: a window can be torn down and the
  // widget outlive it, and this runs from sidebar animations that a close
  // can interleave with.
  NSWindow* ns_window = NativeWindow(widget_);
  if (!ns_window) {
    return;
  }
  NSArray<NSButton*>* lights = LightButtons(ns_window);

  // Before the zero-duration path too: that one settles the state outright, so
  // a hide still pending from an earlier call must not act after it.
  pending_hide_.Reset();

  if (duration.is_zero()) {
    RestoreWindowControlsHitPath();
    for (NSButton* light in lights) {
      light.wantsLayer = YES;
      [light.layer removeAnimationForKey:kLightSlideKey];
      [light.layer removeAnimationForKey:kLightFadeKey];
      [CATransaction begin];
      [CATransaction setDisableActions:YES];
      light.layer.transform = CATransform3DIdentity;
      [CATransaction commit];
      light.hidden = !visible;
      light.alphaValue = visible ? 1 : 0;
    }
    return;
  }

  // No animation is added here at all: the caller steps the travel, and the
  // hide happens in SettleWindowControlsSlide once the pane lands.
  pending_hide_ = visible ? base::OnceCallback<bool()>()
                          : base::BindOnce(
                                [](base::OnceCallback<bool()> confirm) {
                                  return !confirm || std::move(confirm).Run();
                                },
                                std::move(confirm_hide));
  // A transform moves the drawn pixels only, and AppKit routes the mouse
  // through the frame, which stays put. Without this the lights answer clicks
  // at the corner they left, over the band the pane just vacated.
  for (NSButton* light in lights) {
    light.wantsLayer = YES;
    CALayer* layer = light.layer;
    [layer removeAnimationForKey:kLightSlideKey];
    [layer removeAnimationForKey:kLightFadeKey];
    if (visible) {
      // Unhidden first, so the caller's first step has something to move.
      light.hidden = NO;
    }
    // Nothing fades: AppKit redraws the button below full alpha in its
    // inactive grey, and travel alone already takes it off the window's left
    // edge. Never light.animator either - it redraws every frame.
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    light.alphaValue = 1;
    [CATransaction commit];
  }
  // After the alpha: AppKit reports a light at alpha 0 as disabled, and saving
  // that leaves it disabled once the slide ends.
  if (lights_were_enabled_.empty()) {
    for (NSButton* light in lights) {
      lights_were_enabled_.push_back(light.enabled);
      light.enabled = NO;
    }
  }
}

void AtriumWindowPlatform::StepWindowControlsSlide(double dx) {
  NSWindow* ns_window = NativeWindow(widget_);
  if (!ns_window) {
    return;
  }
  for (NSButton* light in LightButtons(ns_window)) {
    light.wantsLayer = YES;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    light.layer.transform = CATransform3DMakeTranslation(dx, 0, 0);
    [CATransaction commit];
  }
}

void AtriumWindowPlatform::RestoreWindowControlsHitPath() {
  NSWindow* ns_window = NativeWindow(widget_);
  if (!ns_window) {
    lights_were_enabled_.clear();
    return;
  }
  size_t index = 0;
  for (NSButton* light in LightButtons(ns_window)) {
    if (index >= lights_were_enabled_.size()) {
      break;
    }
    light.enabled = lights_were_enabled_[index++];
  }
  lights_were_enabled_.clear();
}

void AtriumWindowPlatform::SettleWindowControlsSlide() {
  NSWindow* ns_window = NativeWindow(widget_);
  if (!ns_window) {
    pending_hide_.Reset();
    lights_were_enabled_.clear();
    return;
  }
  // Ahead of the pending-hide return: a reveal owes no hide, and its lights
  // have to answer clicks again just the same.
  RestoreWindowControlsHitPath();
  if (!pending_hide_) {
    pending_hide_.Reset();
    return;
  }
  // By the time the pane lands the answer can have changed: the user reopened
  // the sidebar, or the dock flipped to the right.
  if (!std::move(pending_hide_).Run()) {
    return;
  }
  for (NSButton* light in LightButtons(ns_window)) {
    light.hidden = YES;
    light.alphaValue = 0;
  }
}

bool AtriumWindowPlatform::AreWindowControlsVisible() const {
  NSWindow* ns_window = NativeWindow(widget_);
  return ns_window &&
         ![ns_window standardWindowButton:NSWindowCloseButton].hidden;
}

AtriumWindowPlatform::ControlProbe
AtriumWindowPlatform::ProbeWindowControlsForTesting() const {
  ControlProbe probe;
  NSWindow* ns_window = NativeWindow(widget_);
  NSButton* close = ns_window
                        ? [ns_window standardWindowButton:NSWindowCloseButton]
                        : nil;
  NSButton* zoom =
      ns_window ? [ns_window standardWindowButton:NSWindowZoomButton] : nil;
  if (!close || !zoom) {
    return probe;
  }
  probe.exists = true;
  // Off its resting place and still on screen, or owing a hide. There is no
  // animation object to ask any more, and a settled hide leaves the transform
  // where the pane left it rather than resetting it.
  probe.sliding = !pending_hide_.is_null() ||
                  (!close.hidden && close.layer.transform.m41 != 0) ||
                  [close.layer animationForKey:kLightSlideKey] != nil;
  probe.layer_fading = [close.layer animationForKey:kLightFadeKey] != nil;
  CALayer* live = LiveLayer(close.layer);
  if (live) {
    probe.slide_dx = live.transform.m41;
    probe.layer_opacity = live.opacity;
  }
  CALayer* presented = close.layer.presentationLayer;
  probe.has_presentation = presented != nil;
  probe.presentation_dx = presented ? presented.transform.m41 : 0;
  probe.model_alpha = close.alphaValue;

  probe.frame_dx = slide_frame_dx_;
  // Where the button rested before the travel started, in window coordinates.
  NSRect settled = close.frame;
  settled.origin.x -= slide_frame_dx_;
  const NSPoint settled_centre =
      [close.superview convertPoint:NSMakePoint(NSMidX(settled), NSMidY(settled))
                             toView:nil];
  NSView* frame_view = [ns_window.contentView superview];
  NSView* hit = frame_view ? [frame_view hitTest:settled_centre] : nil;
  for (NSButton* light in LightButtons(ns_window)) {
    if (hit == light || [hit isDescendantOf:light]) {
      probe.settled_corner_hits_a_light = true;
      probe.settled_corner_takes_a_click = light.enabled;
      break;
    }
  }

  const NSRect close_ink = DrawnInkRect(close);
  const NSRect zoom_ink = DrawnInkRect(zoom);
  if (NSIsEmptyRect(close_ink) || NSIsEmptyRect(zoom_ink)) {
    return probe;
  }
  probe.diameter = NSWidth(close_ink);
  // Over two gaps, so a half-point rounding in either centre halves too.
  probe.pitch = ([zoom convertPoint:NSMakePoint(NSMidX(zoom_ink), 0) toView:nil]
                     .x -
                 [close convertPoint:NSMakePoint(NSMidX(close_ink), 0)
                             toView:nil]
                     .x) /
                2;
  return probe;
}

// The NSWindow goes non-opaque with a clear background, and an
// NSVisualEffectView (behind-window blending, state pinned ACTIVE - the
// palette's grey-when-not-key lesson, m4-appearance-report) is inserted
// BELOW the bridge's compositor view, so it frosts the desktop wherever
// that surface has alpha.
void AtriumWindowPlatform::MinimizeWindow() {
  NSWindow* ns_window = NativeWindow(widget_);
  if (!ns_window) {
    return;
  }
  // NOT performMiniaturize:. That asks the window's own miniaturise button
  // first, and this window hides it whenever the sidebar is collapsed -
  // which is every moment the page's dots are on screen, since they only
  // appear while it floats. On a hidden button it does nothing at all,
  // which is exactly what the dots looked like.
  [ns_window miniaturize:nil];
}

void AtriumWindowPlatform::InstallBackdrop() {
  NSWindow* ns_window = NativeWindow(widget_);
  if (!ns_window || !ns_window.contentView) {
    return;
  }
  ns_window.opaque = NO;
  ns_window.backgroundColor = NSColor.clearColor;
  NSView* backdrop;
  if (@available(macOS 26, *)) {
    // NSGlassEffectView gives the window the liquid glass appearance, which
    // makes AppKit draw the new 3D traffic lights instead of the old flat ones.
    AtriumGlassBackdropView* glass = [[AtriumGlassBackdropView alloc]
        initWithFrame:ns_window.contentView.bounds];
    glass.style = NSGlassEffectViewStyleRegular;
    backdrop = glass;
  } else {
    NSVisualEffectView* glass = [[NSVisualEffectView alloc]
        initWithFrame:ns_window.contentView.bounds];
    glass.material = NSVisualEffectMaterialUnderWindowBackground;
    glass.blendingMode = NSVisualEffectBlendingModeBehindWindow;
    glass.state = NSVisualEffectStateActive;
    backdrop = glass;
  }
  backdrop.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  backdrop.identifier = kGlassBackdropIdentifier;
  [ns_window.contentView addSubview:backdrop
                         positioned:NSWindowBelow
                         relativeTo:nil];
}

void AtriumWindowPlatform::UpdateCornerMask(float radius) {
  NSWindow* ns_window = NativeWindow(widget_);
  if (!ns_window || !ns_window.contentView) {
    return;
  }
  SetAppKitCornerRadius(ns_window, radius);
  SetServerCornerRadius(ns_window, radius);
  // The behind-window glass has to be masked directly - its CABackdropLayer
  // ignores an ancestor's mask.
  NSMutableArray<NSView*>* targets =
      [NSMutableArray arrayWithObject:ns_window.contentView];
  for (NSView* child in ns_window.contentView.subviews) {
    if ([child.identifier isEqual:kGlassBackdropIdentifier]) {
      [targets addObject:child];
    }
  }
  for (NSView* v in targets) {
    v.wantsLayer = YES;
    v.layer.cornerRadius = radius;
    v.layer.masksToBounds = YES;
    if (@available(macOS 26, *)) {
      v.layer.cornerCurve = kCACornerCurveContinuous;
    }
  }
}

bool AtriumWindowPlatform::IsWindowFullScreen() const {
  NSWindow* ns_window = NativeWindow(widget_);
  return ns_window && (ns_window.styleMask & NSWindowStyleMaskFullScreen);
}

// NSAnimationContext (Core Animation) rather than a views or compositor
// construct: a CA animation keeps running while the browser main thread
// stalls in the settle IPC.
void AtriumWindowPlatform::FadeNativeView(gfx::NativeView view,
                                          double alpha,
                                          base::TimeDelta duration) {
  NSView* ns_view = view.GetNativeNSView();
  if (!ns_view) {
    return;
  }
  const CGFloat target = std::clamp(alpha, 0.0, 1.0);
  if (duration.is_zero()) {
    // Instant, and cancels any alpha animation already on the view.
    [NSAnimationContext beginGrouping];
    [NSAnimationContext currentContext].duration = 0;
    ns_view.animator.alphaValue = target;
    [NSAnimationContext endGrouping];
    return;
  }
  [NSAnimationContext runAnimationGroup:^(NSAnimationContext* context) {
    context.duration = duration.InSecondsF();
    context.timingFunction = [CAMediaTimingFunction
        functionWithName:kCAMediaTimingFunctionEaseOut];
    ns_view.animator.alphaValue = target;
  }];
}

bool AtriumWindowPlatform::IsKeyDownEvent(
    const input::NativeWebKeyboardEvent& event) const {
  return event.os_event && event.os_event.Get().type == NSEventTypeKeyDown;
}

bool AtriumWindowPlatform::RedispatchKeyEvent(
    gfx::NativeWindow window,
    const input::NativeWebKeyboardEvent& event) {
  auto* host =
      views::NativeWidgetMacNSWindowHost::GetFromNativeWindow(window);
  return host && host->RedispatchKeyEvent(event.os_event.Get());
}

bool AtriumWindowPlatform::HasRealizedWindow() const {
  // Mirrors exactly what SetExcludedFromWindowCycling's CHECK(host) tests,
  // but reports instead of aborting. A torn-down window may already have
  // released its widget.
  gfx::NativeWindow native = widget_->GetNativeWindow();
  return native && views::NativeWidgetMacNSWindowHost::GetFromNativeWindow(
                       native) != nullptr;
}

void AtriumWindowPlatform::SetExcludedFromWindowCycling(bool excluded) {
  auto* host = views::NativeWidgetMacNSWindowHost::GetFromNativeWindow(
      widget_->GetNativeWindow());
  CHECK(host);
  auto* bridge = host->GetInProcessNSWindowBridge();
  CHECK(bridge);  // Atrium windows are always in-process (see
                  // AtriumNativeWidgetMac::OnWindowInitialized).
  bridge->SetExcludedFromWindowCycling(excluded);
}

}  // namespace atrium
