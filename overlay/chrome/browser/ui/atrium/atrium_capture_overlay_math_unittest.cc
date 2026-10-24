// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_capture_overlay_math_unittest.cc
#include "chrome/browser/ui/atrium/atrium_capture_overlay_math.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

constexpr gfx::Rect kPaneAtOrigin(0, 0, 800, 600);

TEST(AtriumCaptureOverlayMathTest, NormalDragTopLeftToBottomRight) {
  const gfx::Rect result = ComputeCaptureRegionRect(
      gfx::Point(10, 20), gfx::Point(110, 70), kPaneAtOrigin);

  EXPECT_EQ(result, gfx::Rect(10, 20, 100, 50));
}

TEST(AtriumCaptureOverlayMathTest, ReverseDragBottomRightToTopLeftNormalizes) {
  const gfx::Rect result = ComputeCaptureRegionRect(
      gfx::Point(110, 70), gfx::Point(10, 20), kPaneAtOrigin);

  EXPECT_EQ(result, gfx::Rect(10, 20, 100, 50));
}

TEST(AtriumCaptureOverlayMathTest, ZeroSizeDragCancels) {
  const gfx::Rect result = ComputeCaptureRegionRect(
      gfx::Point(50, 50), gfx::Point(50, 50), kPaneAtOrigin);

  EXPECT_TRUE(result.IsEmpty());
}

TEST(AtriumCaptureOverlayMathTest, ZeroWidthDragCancels) {
  // Straight vertical drag - zero width is still a degenerate selection.
  const gfx::Rect result = ComputeCaptureRegionRect(
      gfx::Point(50, 10), gfx::Point(50, 100), kPaneAtOrigin);

  EXPECT_TRUE(result.IsEmpty());
}

TEST(AtriumCaptureOverlayMathTest, ClampsToPaneBoundsWhenDragExtendsPastEdge) {
  const gfx::Rect result = ComputeCaptureRegionRect(
      gfx::Point(750, 550), gfx::Point(900, 700), kPaneAtOrigin);

  EXPECT_EQ(result, gfx::Rect(750, 550, 50, 50));
}

TEST(AtriumCaptureOverlayMathTest, DragFullyOutsidePaneClampsToEmpty) {
  const gfx::Rect result = ComputeCaptureRegionRect(
      gfx::Point(1000, 1000), gfx::Point(1100, 1100), kPaneAtOrigin);

  EXPECT_TRUE(result.IsEmpty());
}

TEST(AtriumCaptureOverlayMathTest, OffsetsByNonZeroPaneOrigin) {
  // The overlay is always sized exactly to the pane in practice (origin
  // 0,0), but this function accepts a general pane rect - verify the offset
  // subtraction is real, not assumed away.
  const gfx::Rect pane(200, 100, 800, 600);
  const gfx::Rect result = ComputeCaptureRegionRect(
      gfx::Point(210, 120), gfx::Point(310, 170), pane);

  EXPECT_EQ(result, gfx::Rect(10, 20, 100, 50));
}

}  // namespace
}  // namespace atrium
