// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#import "chrome/browser/ui/atrium/atrium_rounded_hit_test_view.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>
#include <memory>

#include "base/memory/raw_ptr.h"
#include "ui/base/cocoa/views_hostable.h"
#include "ui/gfx/geometry/rect.h"

namespace {

CGFloat ClampRadius(NSRect bounds, CGFloat radius) {
  const CGFloat half = std::min(NSWidth(bounds), NSHeight(bounds)) / 2;
  return std::clamp<CGFloat>(radius, 0, half);
}

bool PointInRoundedRect(NSPoint point, NSRect bounds, CGFloat radius) {
  if (!NSPointInRect(point, bounds)) {
    return false;
  }
  const CGFloat r = ClampRadius(bounds, radius);
  if (r <= 0) {
    return true;
  }
  CGFloat dx = 0;
  CGFloat dy = 0;
  if (point.x < NSMinX(bounds) + r) {
    dx = NSMinX(bounds) + r - point.x;
  } else if (point.x > NSMaxX(bounds) - r) {
    dx = point.x - (NSMaxX(bounds) - r);
  }
  if (point.y < NSMinY(bounds) + r) {
    dy = NSMinY(bounds) + r - point.y;
  } else if (point.y > NSMaxY(bounds) - r) {
    dy = point.y - (NSMaxY(bounds) - r);
  }
  if (dx == 0 || dy == 0) {
    return true;
  }
  return dx * dx + dy * dy <= r * r;
}

// Passes the WebContentsView's own ViewsHostable calls through, so the layer,
// accessibility and first-responder stitching NativeViewHostMac performs still
// reaches it from one view further down.
class ForwardingHostable final : public ui::ViewsHostableView {
 public:
  ForwardingHostable(ui::ViewsHostableView* child,
                     NSView* child_view,
                     NSView* view)
      : child_(child), child_view_(child_view), view_(view) {}

  ForwardingHostable(const ForwardingHostable&) = delete;
  ForwardingHostable& operator=(const ForwardingHostable&) = delete;

  // The page's WebContents dies right after the C++ wrapper, but this object
  // lives on with the NSView. A dangling raw_ptr traps on quit.
  void ReleaseChild() { child_ = nullptr; }

  void ViewsHostableAttach(Host* host) override {
    if (!child_) {
      return;
    }
    child_->ViewsHostableAttach(host);
    // WebContentsNSViewBridge::ResetParentNSView removed the child on detach,
    // and its in-process re-parent is commented out upstream.
    [child_view_ setFrame:[view_ bounds]];
    [child_view_ setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [view_ addSubview:child_view_];
  }

  void ViewsHostableDetach() override {
    if (child_) {
      child_->ViewsHostableDetach();
    }
    // NativeViewDetaching leaves a hostable view where it is and DCHECKs that
    // detaching emptied its superview.
    [view_ removeFromSuperview];
  }

  void ViewsHostableSetBounds(const gfx::Rect& bounds_in_superview,
                              int superview_height) override {
    NSView* superview = [view_ superview];
    const CGFloat y = [superview isFlipped]
                          ? bounds_in_superview.y()
                          : superview_height - bounds_in_superview.y() -
                                bounds_in_superview.height();
    [view_ setFrame:NSMakeRect(bounds_in_superview.x(), y,
                               bounds_in_superview.width(),
                               bounds_in_superview.height())];
    if (!child_) {
      return;
    }
    // The child fills this view, which is flipped, so no second y flip.
    child_->ViewsHostableSetBounds(gfx::Rect(bounds_in_superview.size()),
                                   bounds_in_superview.height());
  }

  void ViewsHostableSetVisible(bool visible) override {
    [view_ setHidden:!visible];
    if (child_) {
      child_->ViewsHostableSetVisible(visible);
    }
  }

  void ViewsHostableMakeFirstResponder() override {
    if (child_) {
      child_->ViewsHostableMakeFirstResponder();
    }
  }

  void ViewsHostableSetParentAccessible(
      gfx::NativeViewAccessible parent_accessibility_element) override {
    if (child_) {
      child_->ViewsHostableSetParentAccessible(parent_accessibility_element);
    }
  }

  gfx::NativeViewAccessible ViewsHostableGetParentAccessible() override {
    return child_ ? child_->ViewsHostableGetParentAccessible()
                  : gfx::NativeViewAccessible();
  }

  gfx::NativeViewAccessible ViewsHostableGetAccessibilityElement() override {
    return child_ ? child_->ViewsHostableGetAccessibilityElement()
                  : gfx::NativeViewAccessible();
  }

 private:
  raw_ptr<ui::ViewsHostableView> child_;
  NSView* __weak child_view_;
  NSView* __weak view_;
};

}  // namespace

@interface AtriumRoundedHitTestNSView : NSView <ViewsHostable>
@property(nonatomic, assign) CGFloat cornerRadius;
- (void)adoptChildHostable:(ui::ViewsHostableView*)child
                 childView:(NSView*)childView;
- (void)releaseChildHostable;
@end

@implementation AtriumRoundedHitTestNSView {
  std::unique_ptr<ForwardingHostable> _hostable;
}

@synthesize cornerRadius = _cornerRadius;

// Owned by the view rather than by the C++ wrapper: NativeViewHostMac caches
// this pointer for as long as it holds the NSView, which can outlive the
// wrapper during window teardown.
- (void)adoptChildHostable:(ui::ViewsHostableView*)child
                 childView:(NSView*)childView {
  _hostable = child ? std::make_unique<ForwardingHostable>(child, childView,
                                                           self)
                    : nullptr;
}

- (void)releaseChildHostable {
  if (_hostable) {
    _hostable->ReleaseChild();
  }
}

- (ui::ViewsHostableView*)viewsHostableView {
  return _hostable.get();
}

- (BOOL)isFlipped {
  return YES;
}

- (NSView*)hitTest:(NSPoint)point {
  NSPoint local = [self convertPoint:point fromView:[self superview]];
  if (!PointInRoundedRect(local, [self bounds], _cornerRadius)) {
    return nil;
  }
  return [super hitTest:point];
}

@end

namespace atrium {

struct AtriumRoundedHitTestView::ObjCState {
  AtriumRoundedHitTestNSView* __strong view;
};

AtriumRoundedHitTestView::AtriumRoundedHitTestView(gfx::NativeView child)
    : objc_(std::make_unique<ObjCState>()) {
  NSView* child_view = child.GetNativeNSView();
  objc_->view = [[AtriumRoundedHitTestNSView alloc]
      initWithFrame:child_view ? [child_view frame] : NSZeroRect];
  if (!child_view) {
    return;
  }
  if ([child_view conformsToProtocol:@protocol(ViewsHostable)]) {
    id<ViewsHostable> hostable = static_cast<id<ViewsHostable>>(child_view);
    [objc_->view adoptChildHostable:[hostable viewsHostableView]
                          childView:child_view];
  }
  [child_view setFrame:[objc_->view bounds]];
  [child_view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
  [objc_->view addSubview:child_view];
}

// The owner detaches first and must destroy this before the WebContents.
AtriumRoundedHitTestView::~AtriumRoundedHitTestView() {
  [objc_->view releaseChildHostable];
}

gfx::NativeView AtriumRoundedHitTestView::native_view() const {
  return gfx::NativeView(objc_->view);
}

void AtriumRoundedHitTestView::SetCornerRadius(float radius) {
  objc_->view.cornerRadius = radius;
}

float AtriumRoundedHitTestView::corner_radius() const {
  return objc_->view.cornerRadius;
}

bool AtriumRoundedHitTestView::HitTestForTesting(
    const gfx::Point& point) const {
  NSView* superview = [objc_->view superview];
  if (!superview) {
    return false;
  }
  const NSPoint in_superview =
      [objc_->view convertPoint:NSMakePoint(point.x(), point.y())
                         toView:superview];
  return [objc_->view hitTest:in_superview] != nil;
}

}  // namespace atrium
