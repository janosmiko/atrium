// Copyright 2026 The Atrium Authors
#include "chrome/browser/ui/atrium/atrium_pinch_gesture_decision.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

TEST(PinchGestureDecisionTest, WheelSubThresholdOnlyAccumulates) {
  PinchGestureStep step =
      StepWheelAccumulator(/*accumulator=*/0, /*delta_y=*/40,
                           /*courtyard_showing=*/false,
                           /*over_sidebar=*/true,
                           /*zoom_at_or_above_one=*/false);
  EXPECT_EQ(step.action, PinchGestureAction::kNone);
  EXPECT_DOUBLE_EQ(step.next_accumulator, 40);
}

TEST(PinchGestureDecisionTest, WheelCrossingThresholdOpensAndResets) {
  PinchGestureStep step =
      StepWheelAccumulator(/*accumulator=*/40, /*delta_y=*/31,
                           /*courtyard_showing=*/false,
                           /*over_sidebar=*/true,
                           /*zoom_at_or_above_one=*/false);
  EXPECT_EQ(step.action, PinchGestureAction::kOpenCourtyard);
  EXPECT_DOUBLE_EQ(step.next_accumulator, 0);
}

TEST(PinchGestureDecisionTest, WheelExactlyAtThresholdDoesNotFire) {
  PinchGestureStep step =
      StepWheelAccumulator(/*accumulator=*/0, /*delta_y=*/70,
                           /*courtyard_showing=*/false,
                           /*over_sidebar=*/true,
                           /*zoom_at_or_above_one=*/false);
  EXPECT_EQ(step.action, PinchGestureAction::kNone);
}

TEST(PinchGestureDecisionTest, WheelPinchOutWhileClosedNeverCloses) {
  PinchGestureStep step =
      StepWheelAccumulator(/*accumulator=*/-40, /*delta_y=*/-40,
                           /*courtyard_showing=*/false,
                           /*over_sidebar=*/true,
                           /*zoom_at_or_above_one=*/false);
  EXPECT_EQ(step.action, PinchGestureAction::kNone);
  EXPECT_DOUBLE_EQ(step.next_accumulator, -80);
}

// the third action. While the Courtyard is open every pinch
// sample is the PLANE's, so it is forwarded rather than swallowed.
TEST(PinchGestureDecisionTest, WheelWhileOpenZoomsTheCanvas) {
  PinchGestureStep step =
      StepWheelAccumulator(/*accumulator=*/0, /*delta_y=*/40,
                           /*courtyard_showing=*/true,
                           /*over_sidebar=*/false,
                           /*zoom_at_or_above_one=*/false);
  EXPECT_EQ(step.action, PinchGestureAction::kZoomCanvas);
  EXPECT_DOUBLE_EQ(step.next_accumulator, 40);
}

// The close is the reported zoom's, not the threshold's alone. A spread
// while the zoom is still below 1.0 keeps zooming instead of closing.
TEST(PinchGestureDecisionTest, WheelPinchOutBelowOneOnlyZooms) {
  PinchGestureStep step =
      StepWheelAccumulator(/*accumulator=*/-40, /*delta_y=*/-31,
                           /*courtyard_showing=*/true,
                           /*over_sidebar=*/false,
                           /*zoom_at_or_above_one=*/false);
  EXPECT_EQ(step.action, PinchGestureAction::kZoomCanvas);
  EXPECT_DOUBLE_EQ(step.next_accumulator, -71);
}

TEST(PinchGestureDecisionTest, WheelPinchOutAtOrAboveOneCloses) {
  PinchGestureStep step =
      StepWheelAccumulator(/*accumulator=*/-40, /*delta_y=*/-31,
                           /*courtyard_showing=*/true,
                           /*over_sidebar=*/false,
                           /*zoom_at_or_above_one=*/true);
  EXPECT_EQ(step.action, PinchGestureAction::kCloseCourtyard);
  EXPECT_DOUBLE_EQ(step.next_accumulator, 0);
}

TEST(PinchGestureDecisionTest, WheelPinchInWhileOpenNeverReopens) {
  PinchGestureStep step =
      StepWheelAccumulator(/*accumulator=*/40, /*delta_y=*/31,
                           /*courtyard_showing=*/true,
                           /*over_sidebar=*/false,
                           /*zoom_at_or_above_one=*/true);
  EXPECT_EQ(step.action, PinchGestureAction::kZoomCanvas);
  EXPECT_DOUBLE_EQ(step.next_accumulator, 71);
}

// The monitor flips AppKit's magnification before it gets here, so `delta`
// reads like the wheel's: POSITIVE is fingers together, negative is a spread.
TEST(PinchGestureDecisionTest, MagnifyFingersTogetherOpensAndResets) {
  PinchGestureStep step =
      StepMagnifyAccumulator(/*accumulator=*/0.3, /*delta=*/0.2,
                             /*courtyard_showing=*/false,
                             /*over_sidebar=*/true,
                             /*zoom_at_or_above_one=*/false);
  EXPECT_EQ(step.action, PinchGestureAction::kOpenCourtyard);
  EXPECT_DOUBLE_EQ(step.next_accumulator, 0);
}

TEST(PinchGestureDecisionTest, MagnifySpreadWhileClosedNeverOpens) {
  PinchGestureStep step =
      StepMagnifyAccumulator(/*accumulator=*/-0.3, /*delta=*/-0.2,
                             /*courtyard_showing=*/false,
                             /*over_sidebar=*/true,
                             /*zoom_at_or_above_one=*/false);
  EXPECT_EQ(step.action, PinchGestureAction::kNone);
  EXPECT_DOUBLE_EQ(step.next_accumulator, -0.5);
}

TEST(PinchGestureDecisionTest, MagnifySpreadAtOrAboveOneCloses) {
  PinchGestureStep step =
      StepMagnifyAccumulator(/*accumulator=*/-0.3, /*delta=*/-0.2,
                             /*courtyard_showing=*/true,
                             /*over_sidebar=*/false,
                             /*zoom_at_or_above_one=*/true);
  EXPECT_EQ(step.action, PinchGestureAction::kCloseCourtyard);
  EXPECT_DOUBLE_EQ(step.next_accumulator, 0);
}

TEST(PinchGestureDecisionTest, MagnifySpreadBelowOneOnlyZooms) {
  PinchGestureStep step =
      StepMagnifyAccumulator(/*accumulator=*/-0.3, /*delta=*/-0.2,
                             /*courtyard_showing=*/true,
                             /*over_sidebar=*/false,
                             /*zoom_at_or_above_one=*/false);
  EXPECT_EQ(step.action, PinchGestureAction::kZoomCanvas);
  EXPECT_DOUBLE_EQ(step.next_accumulator, -0.5);
}

TEST(PinchGestureDecisionTest, MagnifySubThresholdOnlyAccumulates) {
  PinchGestureStep step =
      StepMagnifyAccumulator(/*accumulator=*/0, /*delta=*/0.1,
                             /*courtyard_showing=*/false,
                             /*over_sidebar=*/true,
                             /*zoom_at_or_above_one=*/false);
  EXPECT_EQ(step.action, PinchGestureAction::kNone);
  EXPECT_DOUBLE_EQ(step.next_accumulator, 0.1);
}

// A pinch over the page must never claim the gesture, even mid-run: a prior
// partial accumulation (40, short of the 70 threshold) is dropped rather than
// carried forward once the pointer is off the sidebar.
TEST(PinchGestureDecisionTest, WheelClosedOffSidebarDropsPriorRunAndNeverOpens) {
  PinchGestureStep step =
      StepWheelAccumulator(/*accumulator=*/40, /*delta_y=*/31,
                           /*courtyard_showing=*/false,
                           /*over_sidebar=*/false,
                           /*zoom_at_or_above_one=*/false);
  EXPECT_EQ(step.action, PinchGestureAction::kNone);
  EXPECT_DOUBLE_EQ(step.next_accumulator, 0);
}

TEST(PinchGestureDecisionTest, MagnifyClosedOffSidebarDropsPriorRunAndNeverOpens) {
  PinchGestureStep step =
      StepMagnifyAccumulator(/*accumulator=*/0.3, /*delta=*/0.2,
                             /*courtyard_showing=*/false,
                             /*over_sidebar=*/false,
                             /*zoom_at_or_above_one=*/false);
  EXPECT_EQ(step.action, PinchGestureAction::kNone);
  EXPECT_DOUBLE_EQ(step.next_accumulator, 0);
}

// Over the sidebar, the existing threshold behavior is untouched.
TEST(PinchGestureDecisionTest, WheelClosedOverSidebarStillOpensAtThreshold) {
  PinchGestureStep step =
      StepWheelAccumulator(/*accumulator=*/40, /*delta_y=*/31,
                           /*courtyard_showing=*/false,
                           /*over_sidebar=*/true,
                           /*zoom_at_or_above_one=*/false);
  EXPECT_EQ(step.action, PinchGestureAction::kOpenCourtyard);
  EXPECT_DOUBLE_EQ(step.next_accumulator, 0);
}

TEST(PinchGestureDecisionTest, MagnifyClosedOverSidebarStillOpensAtThreshold) {
  PinchGestureStep step =
      StepMagnifyAccumulator(/*accumulator=*/0.3, /*delta=*/0.2,
                             /*courtyard_showing=*/false,
                             /*over_sidebar=*/true,
                             /*zoom_at_or_above_one=*/false);
  EXPECT_EQ(step.action, PinchGestureAction::kOpenCourtyard);
  EXPECT_DOUBLE_EQ(step.next_accumulator, 0);
}

// Open ignores the flag entirely: the plane still zooms and still closes
// regardless of where the pointer sits, because the Courtyard covers the
// whole window.
TEST(PinchGestureDecisionTest, WheelOpenOffSidebarStillZoomsTheCanvas) {
  PinchGestureStep step =
      StepWheelAccumulator(/*accumulator=*/0, /*delta_y=*/40,
                           /*courtyard_showing=*/true,
                           /*over_sidebar=*/false,
                           /*zoom_at_or_above_one=*/false);
  EXPECT_EQ(step.action, PinchGestureAction::kZoomCanvas);
  EXPECT_DOUBLE_EQ(step.next_accumulator, 40);
}

TEST(PinchGestureDecisionTest, WheelOpenOffSidebarStillCloses) {
  PinchGestureStep step =
      StepWheelAccumulator(/*accumulator=*/-40, /*delta_y=*/-31,
                           /*courtyard_showing=*/true,
                           /*over_sidebar=*/false,
                           /*zoom_at_or_above_one=*/true);
  EXPECT_EQ(step.action, PinchGestureAction::kCloseCourtyard);
  EXPECT_DOUBLE_EQ(step.next_accumulator, 0);
}

TEST(PinchGestureDecisionTest, MagnifyOpenOffSidebarStillZoomsTheCanvas) {
  PinchGestureStep step =
      StepMagnifyAccumulator(/*accumulator=*/-0.3, /*delta=*/-0.2,
                             /*courtyard_showing=*/true,
                             /*over_sidebar=*/false,
                             /*zoom_at_or_above_one=*/false);
  EXPECT_EQ(step.action, PinchGestureAction::kZoomCanvas);
  EXPECT_DOUBLE_EQ(step.next_accumulator, -0.5);
}

TEST(PinchGestureDecisionTest, MagnifyOpenOffSidebarStillCloses) {
  PinchGestureStep step =
      StepMagnifyAccumulator(/*accumulator=*/-0.3, /*delta=*/-0.2,
                             /*courtyard_showing=*/true,
                             /*over_sidebar=*/false,
                             /*zoom_at_or_above_one=*/true);
  EXPECT_EQ(step.action, PinchGestureAction::kCloseCourtyard);
  EXPECT_DOUBLE_EQ(step.next_accumulator, 0);
}

}  // namespace
}  // namespace atrium
