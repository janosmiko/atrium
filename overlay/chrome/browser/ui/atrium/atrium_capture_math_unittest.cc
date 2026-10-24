// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_capture_math_unittest.cc
#include "chrome/browser/ui/atrium/atrium_capture_math.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

constexpr gfx::Size kViewBoundsDip(800, 600);

TEST(AtriumCaptureMathTest, RegionInsideTheViewIsUnchanged) {
  // No device scaling here, and that is the whole point: CopyFromSurface takes
  // its src_rect in VIEW coordinates and scales by the device factor itself
  // (render_widget_host_view.h). Handing it device pixels applied the factor
  // twice, so a 200x200 selection came back as an 800x276 image clamped at the
  // surface edge.
  EXPECT_EQ(ClampRegionToView(gfx::Rect(10, 20, 100, 50), kViewBoundsDip),
            gfx::Rect(10, 20, 100, 50));
}

TEST(AtriumCaptureMathTest, RegionPastTheEdgeIsClamped) {
  EXPECT_EQ(ClampRegionToView(gfx::Rect(700, 500, 400, 400), kViewBoundsDip),
            gfx::Rect(700, 500, 100, 100));
}

TEST(AtriumCaptureMathTest, RegionFullyOutsideTheViewIsEmpty) {
  EXPECT_TRUE(
      ClampRegionToView(gfx::Rect(900, 700, 50, 50), kViewBoundsDip).IsEmpty());
}

TEST(AtriumCaptureMathTest, EmptyRegionIsEmpty) {
  EXPECT_TRUE(ClampRegionToView(gfx::Rect(), kViewBoundsDip).IsEmpty());
  EXPECT_TRUE(
      ClampRegionToView(gfx::Rect(10, 10, 0, 40), kViewBoundsDip).IsEmpty());
}

TEST(AtriumCaptureMathTest, EmptyViewIsEmpty) {
  EXPECT_TRUE(
      ClampRegionToView(gfx::Rect(0, 0, 100, 100), gfx::Size()).IsEmpty());
}

TEST(AtriumCaptureMathTest, CaptureFileNameIsSortableAndFinderSafe) {
  base::Time::Exploded local = {};
  local.year = 2026;
  local.month = 8;
  local.day_of_month = 19;
  local.hour = 14;
  local.minute = 5;
  local.second = 7;
  EXPECT_EQ(CaptureFileName(local),
            "Atrium Screenshot 2026-08-19 at 14.05.07.png");
}

TEST(AtriumCaptureMathTest, CaptureFileNameCarriesNoPathSeparator) {
  base::Time::Exploded local = {};
  local.year = 2026;
  local.month = 12;
  local.day_of_month = 31;
  local.hour = 23;
  local.minute = 59;
  local.second = 59;
  const std::string name = CaptureFileName(local);
  // A colon is the separator in the classic Mac path form, and Finder renders
  // one in a file name as a slash - so the timestamp uses dots.
  EXPECT_EQ(name.find(':'), std::string::npos);
  EXPECT_EQ(name.find('/'), std::string::npos);
  EXPECT_EQ(name, "Atrium Screenshot 2026-12-31 at 23.59.59.png");
}

}  // namespace
}  // namespace atrium
