// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#import "chrome/browser/ui/atrium/atrium_sidebar_edge_reveal.h"

#import <Cocoa/Cocoa.h>

#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/logging.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"

namespace {

// How wide the arming strip is while the pane is hidden, in points.
//
// Deliberately narrow. The strip sits over the page, so anything wide enough
// to be crossed on the way somewhere else would reveal the pane during
// ordinary reading. Six points is about the width of a window's resize margin,
// which is the gesture a user already performs deliberately at an edge.
// 32, matching the panel's own kProximityRevealWidth. At 6 the
// pointer had to be pinned against the window edge before anything happened,
// which is what the reporter saw. Two trackers arm this reveal and they now
// arm at the same distance, so which one wins does not change the feel.
constexpr CGFloat kArmingStripWidth = 32;

}  // namespace

// Forwards a tracking area's enter and exit to C++ closures.
//
// Separate from AtriumSidebarOverlayWidget's own AtriumOverlayTracker rather
// than shared with it: that one is private to a translation unit whose widget
// cannot be constructed at all (see this class's header), and lifting it out
// would tie a live gesture to dead code.
@interface AtriumEdgeRevealTracker : NSObject
- (instancetype)initWithEnter:(base::RepeatingClosure)enter
                         exit:(base::RepeatingClosure)exit;
- (void)attachToView:(NSView*)view assumeInside:(BOOL)assumeInside;
- (void)detach;
@end

@implementation AtriumEdgeRevealTracker {
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

- (void)attachToView:(NSView*)view assumeInside:(BOOL)assumeInside {
  [self detach];
  if (!view) {
    return;
  }
  _view = view;
  // NSTrackingInVisibleRect so the area follows the zone view as the window
  // resizes; without it the strip would keep the height the window had when
  // the area was made, and the bottom of a grown window would stop arming.
  //
  // NSTrackingAssumeInside only for the widened zone, where the pointer really
  // is inside: it suppresses the first mouseEntered, which is what makes the
  // next exit arrive. On the arming strip it would suppress the very event the
  // reveal listens for, and the gesture would never fire.
  NSTrackingAreaOptions opts = NSTrackingMouseEnteredAndExited |
                               NSTrackingActiveAlways |
                               NSTrackingInVisibleRect;
  if (assumeInside) {
    opts |= NSTrackingAssumeInside;
  }
  _area = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                      options:opts
                                        owner:self
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

// The zone itself: a view that occupies space and never takes an event.
//
// hitTest: returns nil so every click, scroll and drag passes to the page
// underneath. A tracking area is geometric and does not consult hitTest:, so
// the zone still reports enter and exit while being invisible to input. This
// is what lets the strip sit over the web contents without swallowing a click
// on whatever it covers.
@interface AtriumEdgeRevealZone : NSView
@end

@implementation AtriumEdgeRevealZone
- (NSView*)hitTest:(NSPoint)point {
  return nil;
}
@end

namespace atrium {

struct AtriumSidebarEdgeReveal::ObjCState {
  AtriumEdgeRevealTracker* __strong tracker;
  NSView* __strong zone;
};

AtriumSidebarEdgeReveal::AtriumSidebarEdgeReveal(AtriumBrowserWindow* window)
    : window_(window), objc_(std::make_unique<ObjCState>()) {
  objc_->tracker = [[AtriumEdgeRevealTracker alloc]
      initWithEnter:base::BindRepeating(&AtriumSidebarEdgeReveal::OnEnter,
                                        weak_factory_.GetWeakPtr())
               exit:base::BindRepeating(&AtriumSidebarEdgeReveal::OnExit,
                                        weak_factory_.GetWeakPtr())];
  // No initial arm here. SetSidebarWidth constructs this and then calls
  // OnSidebarWidthChanged with the width it settled on, which is a better
  // answer than the pane's current one.
}

AtriumSidebarEdgeReveal::~AtriumSidebarEdgeReveal() {
  [objc_->tracker detach];
  [objc_->zone removeFromSuperview];
}

void AtriumSidebarEdgeReveal::OnSidebarWidthChanged(bool collapsed) {
  if (!window_->HasSidebar()) {
    ArmZone(0, /*pointer_is_inside=*/false);
    return;
  }
  if (collapsed) {
    // A collapse the user asked for lands here too, and it ends any reveal of
    // ours: there is nothing left to close when the pointer leaves.
    revealed_by_hover_ = false;
    ArmZone(kArmingStripWidth, /*pointer_is_inside=*/false);
    return;
  }
  if (revealed_by_hover_) {
    // Widen to cover the pane now standing where the strip was, so the
    // pointer moving onto it is still inside the zone.
    ArmZone(window_->LastDockedSidebarWidth(), /*pointer_is_inside=*/true);
    return;
  }
  // Open, and not by us. The pane is the user's, and there is no edge to arm
  // while it fills that edge itself.
  ArmZone(0, /*pointer_is_inside=*/false);
}

void AtriumSidebarEdgeReveal::ForgetReveal() {
  if (!revealed_by_hover_) {
    return;
  }
  revealed_by_hover_ = false;
  // The pane stays. Drop the zone rather than narrowing it: narrowing to the
  // strip under an open pane would arm a reveal for a pane already open.
  ArmZone(0, /*pointer_is_inside=*/false);
}

void AtriumSidebarEdgeReveal::OnEnter() {
  if (revealed_by_hover_ || !window_->IsSidebarCollapsed()) {
    return;
  }
  const int width = window_->LastDockedSidebarWidth();
  if (width <= 0) {
    return;
  }
  VLOG(1) << "Atrium: sidebar edge reveal opening at " << width;
  revealed_by_hover_ = true;
  // the floating panel, not the docked column. SetSidebarWidth here
  // re-expanded the pane, which is why the reveal read as a plain un-collapse.
  window_->RevealFloatingSidebar(true);
}

void AtriumSidebarEdgeReveal::OnExit() {
  if (!revealed_by_hover_) {
    return;
  }
  VLOG(1) << "Atrium: sidebar edge reveal leaving the zone";
  revealed_by_hover_ = false;
  // NOT a dismissal of its own. The panel is a child window that covers none
  // of this zone, so travelling from the zone onto the panel exits it - and
  // closing here shut the panel the moment the pointer reached it. The panel
  // owns the hide rule: a debounce, a tolerance band around its own frame and
  // a grace cap. This only tells it the pointer left.
  window_->NoteFloatingSidebarEdgeExit();
}

void AtriumSidebarEdgeReveal::ArmZone(int width, bool pointer_is_inside) {
  [objc_->tracker detach];
  [objc_->zone removeFromSuperview];
  objc_->zone = nil;
  if (width <= 0) {
    return;
  }
  NSWindow* window = window_->GetNSWindowForAnimation();
  NSView* content = window.contentView;
  if (!content) {
    return;
  }
  const NSRect bounds = content.bounds;
  // The trailing edge when the pane is docked right, so the gesture is always
  // at the edge the pane actually lives on.
  const CGFloat x = window_->IsSidebarOnRight() ? NSMaxX(bounds) - width
                                               : NSMinX(bounds);
  objc_->zone = [[AtriumEdgeRevealZone alloc]
      initWithFrame:NSMakeRect(x, NSMinY(bounds), width, NSHeight(bounds))];
  objc_->zone.autoresizingMask = window_->IsSidebarOnRight()
                               ? (NSViewMinXMargin | NSViewHeightSizable)
                               : (NSViewMaxXMargin | NSViewHeightSizable);
  [content addSubview:objc_->zone positioned:NSWindowAbove relativeTo:nil];
  [objc_->tracker attachToView:objc_->zone assumeInside:pointer_is_inside];
}

}  // namespace atrium
