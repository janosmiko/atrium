// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_split_orientation_decision.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

namespace atrium {
namespace {

constexpr gfx::Rect kContent(0, 0, 800, 600);

TEST(AtriumSplitOrientationDecisionTest, LeftBandIsHorizontalSplit) {
  EXPECT_EQ(ContentDropOrientation::kHorizontalSplit,
            ChooseSplitOrientation(kContent, gfx::Point(50, 300)));
}

TEST(AtriumSplitOrientationDecisionTest, RightBandIsHorizontalSplit) {
  EXPECT_EQ(ContentDropOrientation::kHorizontalSplit,
            ChooseSplitOrientation(kContent, gfx::Point(750, 300)));
}

TEST(AtriumSplitOrientationDecisionTest, TopBandIsVerticalSplit) {
  EXPECT_EQ(ContentDropOrientation::kVerticalSplit,
            ChooseSplitOrientation(kContent, gfx::Point(400, 50)));
}

TEST(AtriumSplitOrientationDecisionTest, BottomBandIsVerticalSplit) {
  EXPECT_EQ(ContentDropOrientation::kVerticalSplit,
            ChooseSplitOrientation(kContent, gfx::Point(400, 550)));
}

TEST(AtriumSplitOrientationDecisionTest, CenterIsSwap) {
  EXPECT_EQ(ContentDropOrientation::kSwap,
            ChooseSplitOrientation(kContent, gfx::Point(400, 300)));
}

// zoneOf() checks fx (L/R) before fy (T/B), so a corner lands as horizontal.
TEST(AtriumSplitOrientationDecisionTest, CornerPrefersHorizontalSplit) {
  EXPECT_EQ(ContentDropOrientation::kHorizontalSplit,
            ChooseSplitOrientation(kContent, gfx::Point(10, 10)));
}

TEST(AtriumSplitOrientationDecisionTest, OffsetContentBoundsUsesRelativePosition) {
  const gfx::Rect offset_content(1000, 500, 800, 600);
  EXPECT_EQ(ContentDropOrientation::kHorizontalSplit,
            ChooseSplitOrientation(offset_content, gfx::Point(1050, 800)));
  EXPECT_EQ(ContentDropOrientation::kSwap,
            ChooseSplitOrientation(offset_content, gfx::Point(1400, 800)));
}

TEST(AtriumSplitOrientationDecisionTest, ResolveSplitEdgeLeftBand) {
  EXPECT_EQ(SplitEdge::kLeft, ResolveSplitEdge(kContent, gfx::Point(50, 300)));
}

TEST(AtriumSplitOrientationDecisionTest, ResolveSplitEdgeRightBand) {
  EXPECT_EQ(SplitEdge::kRight,
            ResolveSplitEdge(kContent, gfx::Point(750, 300)));
}

TEST(AtriumSplitOrientationDecisionTest, ResolveSplitEdgeTopBand) {
  EXPECT_EQ(SplitEdge::kTop, ResolveSplitEdge(kContent, gfx::Point(400, 50)));
}

TEST(AtriumSplitOrientationDecisionTest, ResolveSplitEdgeBottomBand) {
  EXPECT_EQ(SplitEdge::kBottom,
            ResolveSplitEdge(kContent, gfx::Point(400, 550)));
}

TEST(AtriumSplitOrientationDecisionTest, ResolveSplitEdgeCenter) {
  EXPECT_EQ(SplitEdge::kCenter,
            ResolveSplitEdge(kContent, gfx::Point(400, 300)));
}

// Same L/R-before-T/B check order as ChooseSplitOrientation's corner case.
TEST(AtriumSplitOrientationDecisionTest, ResolveSplitEdgeCornerPrefersLeft) {
  EXPECT_EQ(SplitEdge::kLeft, ResolveSplitEdge(kContent, gfx::Point(10, 10)));
}

TEST(AtriumSplitOrientationDecisionTest, ResolveSplitEdgeOffsetContentBounds) {
  const gfx::Rect offset_content(1000, 500, 800, 600);
  EXPECT_EQ(SplitEdge::kLeft,
            ResolveSplitEdge(offset_content, gfx::Point(1050, 800)));
  EXPECT_EQ(SplitEdge::kRight,
            ResolveSplitEdge(offset_content, gfx::Point(1650, 800)));
}

// the live highlight and the drop must never disagree. They call
// the same ResolveSplitEdge on the same content bounds, so this pins the
// property that makes that sharing worth anything - every point in the pane
// resolves to exactly one edge, and the corner rule (L/R before T/B) is the
// one the lab uses.
TEST(AtriumSplitOrientationDecisionTest, EveryPointResolvesToOneEdge) {
  const gfx::Rect content(100, 200, 800, 400);

  // A corner is inside BOTH a horizontal and a vertical band. The lab checks
  // L/R first, so it is a horizontal split - and a preview that drew the top
  // band there would point somewhere the drop would not go.
  EXPECT_EQ(SplitEdge::kLeft, ResolveSplitEdge(content, gfx::Point(120, 210)));
  EXPECT_EQ(SplitEdge::kRight, ResolveSplitEdge(content, gfx::Point(880, 210)));

  EXPECT_EQ(SplitEdge::kLeft, ResolveSplitEdge(content, gfx::Point(150, 400)));
  EXPECT_EQ(SplitEdge::kRight, ResolveSplitEdge(content, gfx::Point(850, 400)));
  EXPECT_EQ(SplitEdge::kTop, ResolveSplitEdge(content, gfx::Point(500, 250)));
  EXPECT_EQ(SplitEdge::kBottom, ResolveSplitEdge(content, gfx::Point(500, 550)));
  EXPECT_EQ(SplitEdge::kCenter, ResolveSplitEdge(content, gfx::Point(500, 400)));
}

// A zero-sized pane cannot be divided by, and a window mid-teardown reports
// exactly that. Center rather than a crash or an arbitrary edge.
TEST(AtriumSplitOrientationDecisionTest, AnEmptyPaneResolvesToCenter) {
  EXPECT_EQ(SplitEdge::kCenter,
            ResolveSplitEdge(gfx::Rect(0, 0, 0, 0), gfx::Point(5, 5)));
}

}  // namespace
}  // namespace atrium
