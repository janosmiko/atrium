// Copyright 2026 The Atrium Authors
#import "chrome/browser/ui/atrium/atrium_pinch_gesture_monitor.h"

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_pinch_gesture_decision.h"
#include "chrome/browser/ui/atrium/atrium_pinch_gesture_hit_test.h"

namespace {

gfx::Point PointInWindowForEvent(NSEvent* event) {
  return atrium::WindowPointForEventLocation(
      event.locationInWindow.x, event.locationInWindow.y,
      event.window.contentView.bounds.size.height);
}

}  // namespace

@implementation AtriumPinchGestureMonitor {
  __weak NSWindow* _window;
  raw_ptr<atrium::AtriumBrowserWindow> _browserWindow;  // Not owned.
  id _monitor;
  double _wheelAccumulator;
  double _magnifyAccumulator;
}

- (instancetype)initWithWindow:(NSWindow*)window
                 browserWindow:(atrium::AtriumBrowserWindow*)browser_window {
  if ((self = [super init])) {
    _window = window;
    _browserWindow = browser_window;
    __weak __typeof(self) weak_self = self;
    _monitor = [NSEvent
        addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel |
                                             NSEventMaskMagnify
                                      handler:^NSEvent*(NSEvent* event) {
                                        return [weak_self handleEvent:event];
                                      }];
  }
  return self;
}

- (void)dealloc {
  [self invalidate];
}

- (void)invalidate {
  if (_monitor) {
    [NSEvent removeMonitor:_monitor];
    _monitor = nil;
  }
  _browserWindow = nullptr;
}

- (NSEvent*)handleEvent:(NSEvent*)event {
  // Local monitors are process-wide, so filter to this instance's own window.
  if (event.window != _window || !_browserWindow) {
    return event;
  }
  if (event.type == NSEventTypeMagnify) {
    return [self stepMagnify:event];
  }
  if (!(event.modifierFlags & NSEventModifierFlagControl)) {
    // Leave the wheel to swipe.js, and drop any partial ctrl+wheel run so an
    // interrupted pinch can't combine with an unrelated scroll later.
    _wheelAccumulator = 0;
    return event;
  }
  const bool courtyardShowing = _browserWindow->IsCourtyardShowing();
  const bool overSidebar = _browserWindow->IsPointOverSidebar(
      PointInWindowForEvent(event));
  const atrium::PinchGestureStep step = atrium::StepWheelAccumulator(
      _wheelAccumulator, event.deltaY, courtyardShowing, overSidebar,
      _browserWindow->IsCourtyardZoomAtOrAboveOne());
  _wheelAccumulator = step.next_accumulator;
  if (!courtyardShowing && !overSidebar) {
    // Off the sidebar and closed: let the page's own pinch zoom handle it.
    return event;
  }
  [self applyAction:step.action delta:event.deltaY];
  // Consumed: once claimed here, ctrl+wheel never reaches content, which
  // otherwise treats it as a page-zoom gesture. That is what keeps the page
  // UNDER the full-window overlay from seeing a pinch meant for the plane.
  return nil;
}

- (NSEvent*)stepMagnify:(NSEvent*)event {
  // AppKit reports a SPREAD as positive magnification, the opposite of the
  // wheel's deltaY. Flipped once here so the accumulator and the plane read
  // one sign: negative is a spread, which zooms in and arms the close.
  const double delta = -event.magnification;
  const bool courtyardShowing = _browserWindow->IsCourtyardShowing();
  const bool overSidebar = _browserWindow->IsPointOverSidebar(
      PointInWindowForEvent(event));
  const atrium::PinchGestureStep step = atrium::StepMagnifyAccumulator(
      _magnifyAccumulator, delta, courtyardShowing, overSidebar,
      _browserWindow->IsCourtyardZoomAtOrAboveOne());
  _magnifyAccumulator = step.next_accumulator;
  if (!courtyardShowing && !overSidebar) {
    return event;
  }
  // A magnification is a fraction of a doubling, not a wheel delta. Scaled to
  // the wheel's units here rather than in the page, so the plane has one
  // entrypoint and one meaning for its argument.
  [self applyAction:step.action delta:delta * 200.0];
  return nil;
}

- (void)applyAction:(atrium::PinchGestureAction)action delta:(double)delta {
  switch (action) {
    case atrium::PinchGestureAction::kOpenCourtyard:
      _browserWindow->OpenCourtyard();
      break;
    case atrium::PinchGestureAction::kCloseCourtyard:
      _browserWindow->CloseCourtyard();
      break;
    case atrium::PinchGestureAction::kZoomCanvas:
      _browserWindow->ForwardPinchToCourtyard(delta);
      break;
    case atrium::PinchGestureAction::kNone:
      break;
  }
}

@end
