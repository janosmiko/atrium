// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_sidebar_grip_bounds.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/rect.h"

namespace atrium {
namespace {

// The column BoxLayout hands the grip on a 1200pt window with the stock 220pt
// sidebar docked left: the sidebar ends at 220, the pane starts at 226.
constexpr gfx::Rect kLeftDockedColumn(220, 0, kSidebarGripWidth, 800);

// The mirror: pane first, sidebar's leading edge at 986.
constexpr gfx::Rect kRightDockedColumn(980, 0, kSidebarGripWidth, 800);

TEST(AtriumSidebarGripBoundsTest, BandRunsFromInsideTheSidebarToThePane) {
  const gfx::Rect band = SidebarGripBounds(kLeftDockedColumn, false);

  EXPECT_EQ(band.x(), 216);
  EXPECT_EQ(band.right(), 226);
  EXPECT_EQ(band.y(), 0);
  EXPECT_EQ(band.height(), 800);
}

// The cursor remains continuous across the deliberately smaller
// grab area: literal bounds keep this independent of the constants above.
TEST(AtriumSidebarGripBoundsTest, NoUnownedPixelBetweenSidebarAndPane) {
  const gfx::Rect band = SidebarGripBounds(kLeftDockedColumn, false);

  for (int x = 216; x < 226; ++x) {
    EXPECT_TRUE(band.Contains(x, 400)) << "no handle owns x=" << x;
  }
}

// The native handle still reaches inside the sidebar, so the seam has no dead
// band between the WebView and the grip's layout column.
TEST(AtriumSidebarGripBoundsTest, CoversTheSidebarsOwnTrailingPixels) {
  const gfx::Rect band = SidebarGripBounds(kLeftDockedColumn, false);

  for (int x = 216; x < 220; ++x) {
    EXPECT_TRUE(band.Contains(x, 400)) << "no handle owns x=" << x;
  }
}

// The old 16px reach made x=210 part of the grip. Pin both the
// requested 10px-inside point and the exact start of the narrower band.
TEST(AtriumSidebarGripBoundsTest, TenPixelsInsideTheSidebarIsNotTheHandle) {
  const gfx::Rect band = SidebarGripBounds(kLeftDockedColumn, false);

  EXPECT_FALSE(band.Contains(210, 400));
  EXPECT_FALSE(band.Contains(215, 400));
  EXPECT_TRUE(band.Contains(216, 400));
}

TEST(AtriumSidebarGripBoundsTest, MirrorsWhenTheSidebarDocksRight) {
  const gfx::Rect band = SidebarGripBounds(kRightDockedColumn, true);

  EXPECT_EQ(band.x(), 980);
  EXPECT_EQ(band.right(), 990);
  for (int x = 980; x < 990; ++x) {
    EXPECT_TRUE(band.Contains(x, 400)) << "no handle owns x=" << x;
  }
  EXPECT_FALSE(band.Contains(996, 400));
}

// Reaching over the sidebar rather than widening the column is the whole point:
// a wider column pushes the pane and doubles the gutter the user sees.
TEST(AtriumSidebarGripBoundsTest, LeavesTheSeamColumnWhereTheLayoutPutIt) {
  EXPECT_EQ(SidebarGripBounds(kLeftDockedColumn, false).right(),
            kLeftDockedColumn.right());
  EXPECT_EQ(SidebarGripBounds(kRightDockedColumn, true).x(),
            kRightDockedColumn.x());
}

// Measured in the VM before this rule existed: a drag LEFT on a right-docked
// sidebar ran it down to the minimum width instead of widening it.
TEST(AtriumSidebarGripBoundsTest, DraggingLeftWidensARightDockedSidebar) {
  EXPECT_EQ(SidebarWidthForDrag(220, -80, /*sidebar_on_right=*/true), 300);
  EXPECT_EQ(SidebarWidthForDrag(220, 80, /*sidebar_on_right=*/true), 140);
}

TEST(AtriumSidebarGripBoundsTest, DraggingRightWidensALeftDockedSidebar) {
  EXPECT_EQ(SidebarWidthForDrag(220, 80, /*sidebar_on_right=*/false), 300);
  EXPECT_EQ(SidebarWidthForDrag(220, -80, /*sidebar_on_right=*/false), 140);
}

}  // namespace
}  // namespace atrium
