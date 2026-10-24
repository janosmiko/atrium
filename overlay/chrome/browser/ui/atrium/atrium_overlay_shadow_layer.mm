// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_overlay_shadow_layer.mm
#include "chrome/browser/ui/atrium/atrium_overlay_shadow_layer.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <utility>

#include "base/apple/scoped_cftyperef.h"
#include "base/check.h"
#include "base/strings/sys_string_conversions.h"
#import "components/remote_cocoa/app_shim/native_widget_mac_nswindow.h"
#include "ui/gfx/mac/coordinate_conversion.h"
#include "ui/views/cocoa/native_widget_mac_ns_window_host.h"
#include "ui/views/widget/widget.h"

// Objective-C declarations must sit at global scope, not inside namespace
// atrium - the ObjCState/AtriumOverlayShadowLayer definitions below it are
// where this file re-enters that namespace.
namespace {

// Resolved against the shadow view itself at Register() time, so a later
// window move never needs a re-push - view-local coordinates do not change
// with it.
struct ResolvedShadow {
  NSRect rect;
  CGFloat corner_radius;
  CGSize offset;
  CGFloat blur;
  NSColor* __strong color;
};

// Converts straight into `view`, not its window's contentView, so the result
// is correct regardless of either view's isFlipped.
NSRect ScreenRectToViewRect(const gfx::Rect& rect, NSView* view) {
  NSRect screen_rect = gfx::ScreenRectToNSRect(rect);
  NSRect window_rect = [view.window convertRectFromScreen:screen_rect];
  return [view convertRect:window_rect fromView:nil];
}

// Erases the surface's footprint after the draw, never clips before it: a clip
// starves the coverage mask Quartz builds the shadow from, and the shadow then
// comes out several times weaker than its token asks.
base::apple::ScopedCFTypeRef<CGImageRef> CreateShadowRing(
    const ResolvedShadow& shadow,
    CGFloat scale,
    NSRect* out_frame) {
  const CGFloat pad =
      shadow.blur * 2 +
      std::max(std::fabs(shadow.offset.width), std::fabs(shadow.offset.height)) +
      1;
  *out_frame = NSInsetRect(shadow.rect, -pad, -pad);
  const size_t pixel_w = std::lround(NSWidth(*out_frame) * scale);
  const size_t pixel_h = std::lround(NSHeight(*out_frame) * scale);
  if (pixel_w == 0 || pixel_h == 0) {
    return base::apple::ScopedCFTypeRef<CGImageRef>();
  }
  base::apple::ScopedCFTypeRef<CGColorSpaceRef> space(
      CGColorSpaceCreateWithName(kCGColorSpaceSRGB));
  base::apple::ScopedCFTypeRef<CGContextRef> ctx(CGBitmapContextCreate(
      nullptr, pixel_w, pixel_h, 8, 0, space.get(),
      kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host));
  if (!ctx) {
    return base::apple::ScopedCFTypeRef<CGImageRef>();
  }
  // Drawn in device pixels rather than under a scaled CTM: whether Quartz
  // transforms a shadow's blur by the CTM is not something to guess at on a
  // 1x VM for a 2x screen.
  const CGRect local = CGRectMake(pad * scale, pad * scale,
                                  NSWidth(shadow.rect) * scale,
                                  NSHeight(shadow.rect) * scale);
  // CSS clamps a radius past half the box, CGPathCreateWithRoundedRect does
  // not - and the devbar's 16px token sits near its 34px height.
  const CGFloat radius =
      std::min(shadow.corner_radius * scale,
               std::min(local.size.width, local.size.height) / 2);
  base::apple::ScopedCFTypeRef<CGPathRef> path(
      CGPathCreateWithRoundedRect(local, radius, radius, nullptr));
  CGContextSetShadowWithColor(
      ctx.get(),
      CGSizeMake(shadow.offset.width * scale, shadow.offset.height * scale),
      shadow.blur * scale, shadow.color.CGColor);
  CGContextSetGrayFillColor(ctx.get(), 0, 1);
  CGContextAddPath(ctx.get(), path.get());
  CGContextFillPath(ctx.get());
  // Erased with a path one device pixel wider than the one that was filled.
  // Clearing with the SAME path scales each boundary pixel by 1 minus its own
  // coverage, which leaves up to a quarter of opaque black along the curve.
  const CGRect erased = CGRectInset(local, -1, -1);
  base::apple::ScopedCFTypeRef<CGPathRef> erased_path(
      CGPathCreateWithRoundedRect(erased, radius + 1, radius + 1, nullptr));
  CGContextSetShadowWithColor(ctx.get(), CGSizeZero, 0, nullptr);
  CGContextSetBlendMode(ctx.get(), kCGBlendModeClear);
  CGContextAddPath(ctx.get(), erased_path.get());
  CGContextFillPath(ctx.get());
  return base::apple::ScopedCFTypeRef<CGImageRef>(
      CGBitmapContextCreateImage(ctx.get()));
}

NSWindow* GetNSWindow(views::Widget* widget) {
  auto* host = views::NativeWidgetMacNSWindowHost::GetFromNativeWindow(
      widget->GetNativeWindow());
  CHECK(host);
  NSWindow* window = host->GetInProcessNSWindow();
  CHECK(window);
  return window;
}

}  // namespace

@interface AtriumOverlayShadowLayerView : NSView
- (void)atrium_setShadow:(ResolvedShadow)shadow forId:(NSString*)surfaceId;
- (void)atrium_removeShadowForId:(NSString*)surfaceId;
- (NSUInteger)atrium_shadowCount;
- (BOOL)atrium_startedAwayForId:(NSString*)surfaceId;
- (NSUInteger)atrium_buildCountForId:(NSString*)surfaceId;
- (void)atrium_setAway:(BOOL)away
                 forId:(NSString*)surfaceId
                 shift:(CGSize)shift
              duration:(CFTimeInterval)duration;
@end

// hitTest: nil, same as AtriumEdgeRevealZone - every click, scroll and drag
// passes through untouched.
@implementation AtriumOverlayShadowLayerView {
  std::map<std::string, CALayer*> _shadowLayers;
  // Present means away. Held here so a re-register mid-slide does not put the
  // shadow back under a bar that has already left.
  std::map<std::string, CGSize> _awayShift;
  // Set once and never cleared, so a release and a fresh register cannot erase
  // what the first build did.
  std::map<std::string, bool> _startedAway;
  // How many times this surface's layer has been built from nothing. A rebuild
  // drops any animation in flight, so a re-register that only moves a shadow
  // must not show up here.
  std::map<std::string, NSUInteger> _buildCount;
}

- (instancetype)initWithFrame:(NSRect)frameRect {
  self = [super initWithFrame:frameRect];
  if (self) {
    self.wantsLayer = YES;
    self.layer.masksToBounds = NO;
  }
  return self;
}

- (NSView*)hitTest:(NSPoint)point {
  return nil;
}

- (BOOL)isOpaque {
  return NO;
}

- (void)atrium_setShadow:(ResolvedShadow)shadow forId:(NSString*)surfaceId {
  std::string key = base::SysNSStringToUTF8(surfaceId);
  CALayer* layer = _shadowLayers[key];
  if (!layer) {
    layer = [CALayer layer];
    [self.layer addSublayer:layer];
    _shadowLayers[key] = layer;
    _buildCount[key]++;
  }
  // Outside the build: a surface that hides and returns keeps its layer, so a
  // record kept only for a fresh one reads false for every entrance after the
  // first.
  if (_awayShift.find(key) != _awayShift.end()) {
    _startedAway[key] = true;
  }
  const CGFloat scale = self.window.backingScaleFactor ?: 1;
  NSRect frame = NSZeroRect;
  base::apple::ScopedCFTypeRef<CGImageRef> ring =
      CreateShadowRing(shadow, scale, &frame);
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  // bounds and position rather than frame: frame is undefined once a transform
  // is applied, and the away slide applies one.
  layer.bounds = CGRectMake(0, 0, NSWidth(frame), NSHeight(frame));
  layer.position = CGPointMake(NSMidX(frame), NSMidY(frame));
  layer.contentsScale = scale;
  layer.contents = (__bridge id)ring.get();
  // A slide in flight already holds the end state in the model value, so
  // re-applying it here would cut the animation short.
  if (layer.animationKeys.count == 0) {
    auto away = _awayShift.find(key);
    const BOOL is_away = away != _awayShift.end();
    layer.opacity = is_away ? 0 : 1;
    layer.affineTransform =
        is_away ? CGAffineTransformMakeTranslation(away->second.width,
                                                   away->second.height)
                : CGAffineTransformIdentity;
  }
  [CATransaction commit];
}

- (void)atrium_removeShadowForId:(NSString*)surfaceId {
  std::string key = base::SysNSStringToUTF8(surfaceId);
  _awayShift.erase(key);
  auto it = _shadowLayers.find(key);
  if (it != _shadowLayers.end()) {
    [it->second removeFromSuperlayer];
    _shadowLayers.erase(it);
  }
}

- (NSUInteger)atrium_shadowCount {
  return _shadowLayers.size();
}

- (BOOL)atrium_startedAwayForId:(NSString*)surfaceId {
  auto it = _startedAway.find(base::SysNSStringToUTF8(surfaceId));
  return it != _startedAway.end() && it->second;
}

- (NSUInteger)atrium_buildCountForId:(NSString*)surfaceId {
  auto it = _buildCount.find(base::SysNSStringToUTF8(surfaceId));
  return it == _buildCount.end() ? 0 : it->second;
}

- (void)atrium_setAway:(BOOL)away
                 forId:(NSString*)surfaceId
                 shift:(CGSize)shift
              duration:(CFTimeInterval)duration {
  std::string key = base::SysNSStringToUTF8(surfaceId);
  if (away) {
    _awayShift[key] = shift;
  } else {
    _awayShift.erase(key);
  }
  auto it = _shadowLayers.find(key);
  if (it == _shadowLayers.end()) {
    return;
  }
  [CATransaction begin];
  [CATransaction setDisableActions:duration <= 0];
  [CATransaction setAnimationDuration:duration];
  // CSS's `ease`, the curve devbar.css transitions the bar itself with.
  [CATransaction setAnimationTimingFunction:
                     [CAMediaTimingFunction functionWithControlPoints:0.25f
                                                                     :0.1f
                                                                     :0.25f
                                                                     :1.0f]];
  it->second.opacity = away ? 0 : 1;
  it->second.affineTransform =
      away ? CGAffineTransformMakeTranslation(shift.width, shift.height)
           : CGAffineTransformIdentity;
  [CATransaction commit];
}

@end

namespace atrium {

struct AtriumOverlayShadowLayer::ObjCState {
  AtriumOverlayShadowLayerView* __strong view;
};

AtriumOverlayShadowLayer::AtriumOverlayShadowLayer(views::Widget* widget)
    : objc_(std::make_unique<ObjCState>()) {
  NSWindow* window = GetNSWindow(widget);
  NSView* content = window.contentView;
  objc_->view =
      [[AtriumOverlayShadowLayerView alloc] initWithFrame:content.bounds];
  objc_->view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [content addSubview:objc_->view positioned:NSWindowAbove relativeTo:nil];
}

AtriumOverlayShadowLayer::~AtriumOverlayShadowLayer() {
  [objc_->view removeFromSuperview];
}

void AtriumOverlayShadowLayer::Register(
    const std::string& surface_id,
    const gfx::Rect& rect_in_screen,
    const AtriumOverlaySurfaceShadow& shadow) {
  if (!objc_->view.window) {
    return;
  }
  ResolvedShadow resolved;
  resolved.rect = ScreenRectToViewRect(rect_in_screen, objc_->view);
  resolved.corner_radius = shadow.corner_radius;
  resolved.blur = shadow.blur;
  // Quartz's shadow offset is y-up. The pushed offset comes from a y-down CSS
  // box-shadow, so the sign flips to fall the same visual direction.
  resolved.offset = NSMakeSize(shadow.offset.x(), -shadow.offset.y());
  resolved.color =
      [NSColor colorWithSRGBRed:SkColorGetR(shadow.color) / 255.0
                          green:SkColorGetG(shadow.color) / 255.0
                           blue:SkColorGetB(shadow.color) / 255.0
                          alpha:SkColorGetA(shadow.color) / 255.0];
  [objc_->view atrium_setShadow:resolved
                          forId:base::SysUTF8ToNSString(surface_id)];
}

void AtriumOverlayShadowLayer::Unregister(const std::string& surface_id) {
  [objc_->view
      atrium_removeShadowForId:base::SysUTF8ToNSString(surface_id)];
}

size_t AtriumOverlayShadowLayer::ShadowCountForTesting() const {
  return [objc_->view atrium_shadowCount];
}

bool AtriumOverlayShadowLayer::ShadowStartedAwayForTesting(
    const std::string& surface_id) const {
  return [objc_->view
      atrium_startedAwayForId:base::SysUTF8ToNSString(surface_id)];
}

size_t AtriumOverlayShadowLayer::ShadowBuildCountForTesting(
    const std::string& surface_id) const {
  return [objc_->view
      atrium_buildCountForId:base::SysUTF8ToNSString(surface_id)];
}

void AtriumOverlayShadowLayer::SetSurfaceAway(const std::string& surface_id,
                                              bool away,
                                              const gfx::Vector2d& shift,
                                              base::TimeDelta duration) {
  [objc_->view atrium_setAway:away
                        forId:base::SysUTF8ToNSString(surface_id)
                        shift:NSMakeSize(shift.x(), -shift.y())
                     duration:duration.InSecondsF()];
}

gfx::NativeView AtriumOverlayShadowLayer::view() const {
  return gfx::NativeView(objc_->view);
}

}  // namespace atrium
