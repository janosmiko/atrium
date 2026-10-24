// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_corner_radius.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

// The shipping geometry: Safari's 16pt macOS 27 window corner
// with the content card's 10pt gutter.
TEST(AtriumCornerRadiusTest, ContentCardIsConcentricWithTheWindow) {
  EXPECT_FLOAT_EQ(6, ConcentricCornerRadius(16, 10));
}

// The floating sidebar overlay sits a 5pt step in from the window edge, so it
// is rounder than the content card - different nesting depth, different
// radius. Both being 12 was the bug.
TEST(AtriumCornerRadiusTest, FloatingOverlayIsRounderThanTheContentCard) {
  const float overlay = ConcentricCornerRadius(16, 5);
  const float card = ConcentricCornerRadius(16, 10);
  EXPECT_FLOAT_EQ(11, overlay);
  EXPECT_GT(overlay, card);
}

// The whole point: the gap stays the same width along the edge and through
// the corner, for any inset the floor does not swallow.
TEST(AtriumCornerRadiusTest, GapBetweenTheTwoCurvesEqualsTheInset) {
  for (int inset = 0; inset <= 10; ++inset) {
    EXPECT_FLOAT_EQ(16 - inset, ConcentricCornerRadius(16, inset))
        << "inset " << inset;
  }
}

// A zero inset means the shape is flush with the parent, so it takes the
// parent's own corner.
TEST(AtriumCornerRadiusTest, ZeroInsetMatchesTheParent) {
  EXPECT_FLOAT_EQ(16, ConcentricCornerRadius(16, 0));
}

// Deep insets would drive the concentric value to zero and read as a square
// card; the floor holds them at the documented minimum instead.
TEST(AtriumCornerRadiusTest, DeepInsetsClampToTheFloor) {
  EXPECT_FLOAT_EQ(kMinNestedCornerRadius, ConcentricCornerRadius(16, 10));
  EXPECT_FLOAT_EQ(kMinNestedCornerRadius, ConcentricCornerRadius(16, 16));
  EXPECT_FLOAT_EQ(kMinNestedCornerRadius, ConcentricCornerRadius(16, 999));
}

// Never negative, whatever the caller passes.
TEST(AtriumCornerRadiusTest, NegativeInsetIsTreatedAsFlush) {
  EXPECT_FLOAT_EQ(16, ConcentricCornerRadius(16, -4));
}

// A window radius the probe could not read (0) must not produce a negative
// radius for anything nested in it.
TEST(AtriumCornerRadiusTest, UnreadableWindowRadiusStillYieldsTheFloor) {
  EXPECT_FLOAT_EQ(kMinNestedCornerRadius, ConcentricCornerRadius(0, 10));
}

// The derived radii track the window radius rather than a hardcoded number -
// the regression guard for the original bug. If Apple retunes the
// silhouette, the nested corners follow instead of silently desyncing.
TEST(AtriumCornerRadiusTest, NestedRadiiFollowTheWindowRadius) {
  EXPECT_FLOAT_EQ(6, ConcentricCornerRadius(16, 10));
  EXPECT_FLOAT_EQ(16, ConcentricCornerRadius(26, 10));
  EXPECT_FLOAT_EQ(20, ConcentricCornerRadius(30, 10));
}

}  // namespace
}  // namespace atrium
