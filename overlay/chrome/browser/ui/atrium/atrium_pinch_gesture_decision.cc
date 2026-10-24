// Copyright 2026 The Atrium Authors
#include "chrome/browser/ui/atrium/atrium_pinch_gesture_decision.h"

namespace atrium {
namespace {

constexpr double kWheelThreshold = 70.0;
constexpr double kMagnifyThreshold = 0.4;

PinchGestureStep StepAccumulator(double accumulator,
                                 double delta,
                                 bool courtyard_showing,
                                 bool over_sidebar,
                                 bool zoom_at_or_above_one,
                                 double threshold) {
  if (!courtyard_showing && !over_sidebar) {
    return {0.0, PinchGestureAction::kNone};
  }
  const double next = accumulator + delta;
  if (!courtyard_showing) {
    if (next > threshold) {
      return {0.0, PinchGestureAction::kOpenCourtyard};
    }
    return {next, PinchGestureAction::kNone};
  }
  if (zoom_at_or_above_one && next < -threshold) {
    return {0.0, PinchGestureAction::kCloseCourtyard};
  }
  // Every other sample while open is the plane's. The accumulator keeps
  // running so a sustained spread still reaches the close once the zoom
  // report crosses back to 1.0 or above.
  return {next, PinchGestureAction::kZoomCanvas};
}

}  // namespace

PinchGestureStep StepWheelAccumulator(double accumulator,
                                      double delta_y,
                                      bool courtyard_showing,
                                      bool over_sidebar,
                                      bool zoom_at_or_above_one) {
  return StepAccumulator(accumulator, delta_y, courtyard_showing,
                         over_sidebar, zoom_at_or_above_one, kWheelThreshold);
}

PinchGestureStep StepMagnifyAccumulator(double accumulator,
                                        double delta,
                                        bool courtyard_showing,
                                        bool over_sidebar,
                                        bool zoom_at_or_above_one) {
  return StepAccumulator(accumulator, delta, courtyard_showing,
                         over_sidebar, zoom_at_or_above_one,
                         kMagnifyThreshold);
}

}  // namespace atrium
