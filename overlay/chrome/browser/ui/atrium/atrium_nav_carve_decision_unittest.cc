// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
#include "chrome/browser/ui/atrium/atrium_nav_carve_decision.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/point.h"

namespace atrium {
namespace {

TEST(AtriumNavCarveDecisionTest, ToggleCarveTakesTheClickLeftDocked) {
  NavCarveGeometry geometry{.win_w = 800, .sidebar_width = 260,
                             .on_right = false};
  EXPECT_TRUE(PointInNavCarve(gfx::Point(100, 10), geometry));
}

TEST(AtriumNavCarveDecisionTest, BetweenTheTwoCarvesDragsTheWindow) {
  // sidebar_width 260: group spans [180, 250), toggle spans [82, 196).
  // x=300 clears both, at the fixture's own width.
  NavCarveGeometry geometry{.win_w = 800, .sidebar_width = 260,
                             .on_right = false};
  EXPECT_FALSE(PointInNavCarve(gfx::Point(300, 10), geometry));
}

TEST(AtriumNavCarveDecisionTest, GroupCarveTakesTheClickAtTheFixtureWidth) {
  // 260 - 10 - 70 = 180, 260 - 10 = 250.
  NavCarveGeometry geometry{.win_w = 800, .sidebar_width = 260,
                             .on_right = false};
  EXPECT_TRUE(PointInNavCarve(gfx::Point(200, 10), geometry));
}

TEST(AtriumNavCarveDecisionTest, GroupCarveTracksAWiderSidebar) {
  // The whole point: a point the 260pt carve misses is caught once
  // the sidebar has actually widened to 340, exactly where the row is now.
  NavCarveGeometry narrow{.win_w = 800, .sidebar_width = 260,
                           .on_right = false};
  NavCarveGeometry wide{.win_w = 800, .sidebar_width = 340,
                         .on_right = false};
  EXPECT_FALSE(PointInNavCarve(gfx::Point(300, 10), narrow));
  EXPECT_TRUE(PointInNavCarve(gfx::Point(300, 10), wide));
}

TEST(AtriumNavCarveDecisionTest, GroupCarveHiIsExclusive) {
  NavCarveGeometry geometry{.win_w = 800, .sidebar_width = 260,
                             .on_right = false};
  EXPECT_TRUE(PointInNavCarve(gfx::Point(249, 10), geometry));
  EXPECT_FALSE(PointInNavCarve(gfx::Point(250, 10), geometry));
}

TEST(AtriumNavCarveDecisionTest, RightDockedGroupHugsTheWindowsOwnEdge) {
  // on_right: the row's right edge is the window's right edge regardless of
  // sidebar_width, so the carve sits at [win_w - 80, win_w - 10).
  NavCarveGeometry geometry{.win_w = 800, .sidebar_width = 260,
                             .on_right = true};
  EXPECT_TRUE(PointInNavCarve(gfx::Point(750, 10), geometry));
  EXPECT_FALSE(PointInNavCarve(gfx::Point(700, 10), geometry));
}

TEST(AtriumNavCarveDecisionTest, RightDockedToggleCarveMirrorsTheLeftEdge) {
  NavCarveGeometry geometry{.win_w = 800, .sidebar_width = 260,
                             .on_right = true};
  // kBandControlsNearInset/FarInset from the sidebar's leading edge, which
  // for a right-docked sidebar is win_w - sidebar_width = 540.
  EXPECT_TRUE(PointInNavCarve(gfx::Point(600, 10), geometry));
}

// sidebar-nav-row.js runs this same ladder off the same constants,
// and sidebar-nav-row-test.mjs reads them out of the .cc to catch a drift.

TEST(AtriumNavCarveDecisionTest, EveryButtonSurvivesAWideEnoughRow) {
  EXPECT_EQ(3, VisibleNavButtonCount(94));
  EXPECT_EQ(3, VisibleNavButtonCount(200));
}

TEST(AtriumNavCarveDecisionTest, TheGroupShedsOneButtonAtATime) {
  EXPECT_EQ(2, VisibleNavButtonCount(93));
  EXPECT_EQ(2, VisibleNavButtonCount(88));
  // 64..87: the toggle is shed instead of forward, so the GROUP keeps two
  // buttons - the same rung sidebar-nav-row.js draws as back+forward+menu.
  EXPECT_EQ(2, VisibleNavButtonCount(87));
  EXPECT_EQ(2, VisibleNavButtonCount(64));
  EXPECT_EQ(1, VisibleNavButtonCount(63));
  EXPECT_EQ(1, VisibleNavButtonCount(40));
  EXPECT_EQ(0, VisibleNavButtonCount(39));
}

TEST(AtriumNavCarveDecisionTest, TheRowBudgetIsTheSidebarMinusItsInsets) {
  // 86 traffic-light clearance plus the row's own 10px right inset.
  EXPECT_EQ(164, NavControlsWidth(260));
  EXPECT_EQ(3, VisibleNavButtonCount(NavControlsWidth(190)));
  EXPECT_EQ(2, VisibleNavButtonCount(NavControlsWidth(189)));
  EXPECT_EQ(2, VisibleNavButtonCount(NavControlsWidth(160)));
}

TEST(AtriumNavCarveDecisionTest, TheCollapsedGroupStillTakesItsClicks) {
  // 189: forward, reload and the overflow button, spanning [115, 179).
  NavCarveGeometry geometry{.win_w = 800, .sidebar_width = 189,
                             .on_right = false};
  EXPECT_EQ(2, VisibleNavButtonCount(NavControlsWidth(189)));
  EXPECT_TRUE(PointInNavCarve(gfx::Point(178, 10), geometry));
  EXPECT_TRUE(PointInNavCarve(gfx::Point(116, 10), geometry));
}

TEST(AtriumNavCarveDecisionTest, TheOverflowButtonAloneStillTakesItsClick) {
  // 130 leaves room for the toggle and the overflow button only, [104, 120).
  NavCarveGeometry geometry{.win_w = 800, .sidebar_width = 130,
                             .on_right = false};
  EXPECT_EQ(0, VisibleNavButtonCount(NavControlsWidth(130)));
  EXPECT_TRUE(PointInNavCarve(gfx::Point(112, 10), geometry));
}

TEST(AtriumNavCarveDecisionTest, TheShrunkenGroupNeverOrphansAButton) {
  // The band carve [82, 196) already covers the group at every width the
  // collapse fires at, so shrinking the group can only give room back to the
  // window drag - never take a live button out of the carve.
  for (int sidebar = 120; sidebar < 190; ++sidebar) {
    NavCarveGeometry geometry{.win_w = 800, .sidebar_width = sidebar,
                               .on_right = false};
    for (int x = 86; x < sidebar - 10; ++x) {
      EXPECT_TRUE(PointInNavCarve(gfx::Point(x, 10), geometry))
          << "sidebar " << sidebar << " dropped x " << x;
    }
  }
}

// the floating address bar hosts back/forward/reload while it is
// up, and the row then ends at the toggle.

TEST(AtriumNavCarveDecisionTest, TheGroupCarveGoesWithTheGroup) {
  NavCarveGeometry hosted{.win_w = 800, .sidebar_width = 260,
                          .on_right = false, .nav_group_in_row = false};
  NavCarveGeometry in_row{.win_w = 800, .sidebar_width = 260,
                          .on_right = false};
  // x=200 is inside the group's strip at this width and outside the band.
  EXPECT_TRUE(PointInNavCarve(gfx::Point(200, 10), in_row));
  EXPECT_FALSE(PointInNavCarve(gfx::Point(200, 10), hosted));
}

TEST(AtriumNavCarveDecisionTest, TheToggleKeepsItsCarveWithTheGroupHosted) {
  NavCarveGeometry hosted{.win_w = 800, .sidebar_width = 260,
                          .on_right = false, .nav_group_in_row = false};
  EXPECT_TRUE(PointInNavCarve(gfx::Point(100, 10), hosted));
}

TEST(AtriumNavCarveDecisionTest, TheRightDockedGroupCarveGoesTooWhenHosted) {
  NavCarveGeometry hosted{.win_w = 800, .sidebar_width = 260,
                          .on_right = true, .nav_group_in_row = false};
  EXPECT_FALSE(PointInNavCarve(gfx::Point(750, 10), hosted));
  EXPECT_TRUE(PointInNavCarve(gfx::Point(600, 10), hosted));
}

}  // namespace
}  // namespace atrium
