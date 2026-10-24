// Copyright 2026 The Atrium Authors
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_PINCH_GESTURE_DECISION_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_PINCH_GESTURE_DECISION_H_

namespace atrium {

// Pure so the threshold and direction logic can be asserted without AppKit -
// same posture as atrium_peek_decision.h.
enum class PinchGestureAction {
  kNone,
  kOpenCourtyard,
  kCloseCourtyard,
  // the pinch belongs to the PLANE while the Courtyard is open.
  // The monitor forwards the sample instead of swallowing it.
  kZoomCanvas,
};

struct PinchGestureStep {
  double next_accumulator = 0;
  PinchGestureAction action = PinchGestureAction::kNone;
};

// Threshold and reset-on-fire mirror docs/lab/swipe-lab.html's pinchAcc
// (~line 1809). `courtyard_showing` arms only the direction that applies, and
// `zoom_at_or_above_one` is what the OPEN plane reports about itself: a
// spread closes only once the last reported zoom is at or above 1.0, so a
// user still zooming in below that is never thrown out mid-gesture.
PinchGestureStep StepWheelAccumulator(double accumulator,
                                      double delta_y,
                                      bool courtyard_showing,
                                      // Closed only: an off-sidebar sample is dropped.
                                      bool over_sidebar,
                                      bool zoom_at_or_above_one);

// Own threshold: AppKit's magnification units are not comparable to wheel
// deltaY. `delta` carries the WHEEL's sign, so the caller flips AppKit's
// magnification, which is positive on a spread.
PinchGestureStep StepMagnifyAccumulator(double accumulator,
                                        double delta,
                                        bool courtyard_showing,
                                        bool over_sidebar,
                                        bool zoom_at_or_above_one);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_PINCH_GESTURE_DECISION_H_
