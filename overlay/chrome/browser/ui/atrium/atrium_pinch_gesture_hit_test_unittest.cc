// Copyright 2026 The Atrium Authors
#include "chrome/browser/ui/atrium/atrium_pinch_gesture_hit_test.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

TEST(PinchGestureHitTestTest, BottomOfWindowFlipsToLargeY) {
  const gfx::Point point = WindowPointForEventLocation(
      /*location_x=*/10, /*location_y=*/0, /*content_view_height=*/800);
  EXPECT_EQ(point, gfx::Point(10, 800));
}

TEST(PinchGestureHitTestTest, TopOfWindowFlipsToZeroY) {
  const gfx::Point point = WindowPointForEventLocation(
      /*location_x=*/10, /*location_y=*/800, /*content_view_height=*/800);
  EXPECT_EQ(point, gfx::Point(10, 0));
}

TEST(PinchGestureHitTestTest, MidWindowIsItsOwnMirror) {
  const gfx::Point point = WindowPointForEventLocation(
      /*location_x=*/10, /*location_y=*/400, /*content_view_height=*/800);
  EXPECT_EQ(point, gfx::Point(10, 400));
}

TEST(PinchGestureHitTestTest, XIsUnaffectedByTheFlip) {
  const gfx::Point point = WindowPointForEventLocation(
      /*location_x=*/633, /*location_y=*/50, /*content_view_height=*/800);
  EXPECT_EQ(point.x(), 633);
}

}  // namespace
}  // namespace atrium
