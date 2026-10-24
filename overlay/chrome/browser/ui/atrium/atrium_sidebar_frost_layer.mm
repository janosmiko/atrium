// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_sidebar_frost_layer.mm
#include "chrome/browser/ui/atrium/atrium_sidebar_frost_layer.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include "components/remote_cocoa/app_shim/native_widget_mac_nswindow.h"
#include "ui/gfx/mac/coordinate_conversion.h"
#include "ui/views/cocoa/native_widget_mac_ns_window_host.h"
#include "ui/views/widget/widget.h"

namespace {

// Converts straight into `view`, not its window's contentView, so the result
// is correct regardless of either view's isFlipped.
NSRect ScreenRectToViewRect(const gfx::Rect& rect, NSView* view) {
  NSRect screen_rect = gfx::ScreenRectToNSRect(rect);
  NSRect window_rect = [view.window convertRectFromScreen:screen_rect];
  return [view convertRect:window_rect fromView:nil];
}

NSWindow* GetNSWindow(views::Widget* widget) {
  auto* host = views::NativeWidgetMacNSWindowHost::GetFromNativeWindow(
      widget->GetNativeWindow());
  return host ? host->GetInProcessNSWindow() : nil;
}

// The panel content slides in from behind its own window edge, which clips it.
// The frost takes the same path inside the same shaped clip, so the two show
// exactly the same band on every frame.
CABasicAnimation* FrostSlide(double from_dx,
                             double to_dx,
                             base::TimeDelta duration,
                             bool hold) {
  CABasicAnimation* slide =
      [CABasicAnimation animationWithKeyPath:@"transform.translation.x"];
  slide.fromValue = @(from_dx);
  slide.toValue = @(to_dx);
  slide.duration = duration.InSecondsF();
  slide.timingFunction = [CAMediaTimingFunction functionWithControlPoints:
                                                     0.32:0.72:0.35:1.0];
  if (hold) {
    slide.fillMode = kCAFillModeForwards;
    slide.removedOnCompletion = NO;
  }
  return slide;
}

// The presentation value, so an interrupting slide starts from what is on
// screen now rather than snapping to the old animation's nominal endpoint.
CGFloat CurrentSlideDx(CALayer* layer) {
  CALayer* live = layer.presentationLayer ?: layer;
  return live.transform.m41;
}

}  // namespace

// hitTest: nil so the frost never intercepts a click meant for the page or
// the floating panel above it.
@interface AtriumSidebarFrostContainer : NSView
@end

@implementation AtriumSidebarFrostContainer
- (NSView*)hitTest:(NSPoint)point {
  return nil;
}
@end

namespace atrium {

struct AtriumSidebarFrostLayer::ObjCState {
  NSView* __strong container;
  NSView* __strong slider;
  NSVisualEffectView* __strong effect;
};

AtriumSidebarFrostLayer::AtriumSidebarFrostLayer(views::Widget* widget,
                                                 gfx::NativeView insert_below)
    : objc_(std::make_unique<ObjCState>()) {
  NSWindow* window = GetNSWindow(widget);
  NSView* content = window.contentView;
  if (!content) {
    return;
  }
  objc_->container =
      [[AtriumSidebarFrostContainer alloc] initWithFrame:NSZeroRect];
  objc_->container.hidden = YES;
  objc_->container.wantsLayer = YES;
  // Hidden until the first AnimateShow sets it. The CALayer default of 1 would
  // show the frost over the page before any reveal asked for it.
  objc_->container.layer.opacity = 0;
  objc_->container.layer.masksToBounds = YES;
  // All four corners, like the panel's own content.layer mask (Show/Close in
  // atrium_sidebar_overlay_widget.mm) - the dock-side edge is inset too, not
  // flush, so its corners are on screen as well.
  objc_->container.layer.maskedCorners =
      kCALayerMinXMinYCorner | kCALayerMaxXMinYCorner |
      kCALayerMinXMaxYCorner | kCALayerMaxXMaxYCorner;

  // The slide moves this, never the container. A clip that travels shows frost
  // past the panel's own edge, into the window gutter.
  objc_->slider = [[NSView alloc] initWithFrame:objc_->container.bounds];
  objc_->slider.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  objc_->slider.wantsLayer = YES;
  [objc_->container addSubview:objc_->slider];

  NSVisualEffectView* effect =
      [[NSVisualEffectView alloc] initWithFrame:objc_->slider.bounds];
  effect.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  effect.blendingMode = NSVisualEffectBlendingModeWithinWindow;
  effect.material = NSVisualEffectMaterialHUDWindow;
  effect.state = NSVisualEffectStateActive;
  effect.wantsLayer = YES;
  effect.layer.masksToBounds = YES;
  objc_->effect = effect;
  [objc_->slider addSubview:effect];

  NSView* shadow_view = insert_below.GetNativeNSView();
  if (shadow_view && shadow_view.superview == content) {
    [content addSubview:objc_->container
             positioned:NSWindowBelow
             relativeTo:shadow_view];
  } else {
    [content addSubview:objc_->container
             positioned:NSWindowAbove
             relativeTo:nil];
  }
}

AtriumSidebarFrostLayer::~AtriumSidebarFrostLayer() {
  [objc_->container removeFromSuperview];
}

void AtriumSidebarFrostLayer::SetFrame(const gfx::Rect& rect_in_screen,
                                       float corner_radius) {
  if (!objc_->container.window) {
    return;
  }
  // The slide is left running: ApplyBounds calls this on every parent move and
  // width drag, mid-dismiss included. A reveal beating a still-sliding Close()
  // re-bases from the live offset in AnimateShow instead.
  objc_->container.frame =
      ScreenRectToViewRect(rect_in_screen, objc_->container.superview);
  objc_->container.layer.cornerRadius = corner_radius;
  // The stationary clip keeps the frost inside the panel window, but its
  // moving edge needs the panel content's rounded shape too. Mask the effect
  // itself: its backdrop layer can draw through an ancestor's corner mask.
  objc_->effect.layer.cornerRadius = corner_radius;
  objc_->container.hidden = NO;
}

void AtriumSidebarFrostLayer::AnimateHide(base::TimeDelta duration,
                                          double slide_dx) {
  if (objc_->container.hidden || !objc_->container.window) {
    return;
  }
  CALayer* layer = objc_->slider.layer;
  // Read before removing: a reveal still sliding in when a dismiss arrives
  // must hand off from where it is, not snap to the settled position first.
  const CGFloat current = CurrentSlideDx(layer);
  [layer removeAnimationForKey:@"atriumFrostSlide"];
  [layer addAnimation:FrostSlide(current, slide_dx, duration, /*hold=*/true)
               forKey:@"atriumFrostSlide"];
}

void AtriumSidebarFrostLayer::AnimateShow(base::TimeDelta duration,
                                          double slide_dx) {
  if (!objc_->container.window) {
    return;
  }
  CALayer* layer = objc_->slider.layer;
  const CGFloat current = CurrentSlideDx(layer);
  const bool interrupting =
      [layer animationForKey:@"atriumFrostSlide"] != nil;
  [layer removeAnimationForKey:@"atriumFrostSlide"];
  // Full strength from the first frame: the panel does not fade either, and a
  // frost that ramps up reads as a thinner panel mid-slide.
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  objc_->container.layer.opacity = 1;
  [CATransaction commit];
  [layer addAnimation:FrostSlide(interrupting ? current : slide_dx, 0, duration,
                                 /*hold=*/false)
               forKey:@"atriumFrostSlide"];
}

bool AtriumSidebarFrostLayer::sliding_for_testing() const {
  return [objc_->slider.layer animationForKey:@"atriumFrostSlide"] != nil;
}

double AtriumSidebarFrostLayer::slide_dx_for_testing() const {
  return CurrentSlideDx(objc_->slider.layer);
}

void AtriumSidebarFrostLayer::Hide() {
  objc_->container.hidden = YES;
  [objc_->slider.layer removeAnimationForKey:@"atriumFrostSlide"];
  objc_->container.layer.opacity = 0;
}

void AtriumSidebarFrostLayer::SetDarkBackdrop(bool dark) {
  dark_backdrop_ = dark;
  // The vibrant names, not DarkAqua/Aqua: the blur materials are defined
  // against vibrancy, and HUDWindow is one of them.
  objc_->effect.appearance = [NSAppearance
      appearanceNamed:dark ? NSAppearanceNameVibrantDark
                           : NSAppearanceNameVibrantLight];
}

}  // namespace atrium
