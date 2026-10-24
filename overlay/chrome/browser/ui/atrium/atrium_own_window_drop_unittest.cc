// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_own_window_drop.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

namespace atrium {
namespace {

constexpr gfx::Rect kSidebar(0, 30, 260, 600);
// Two side-by-side panes right of the sidebar.
std::vector<gfx::Rect> Panes() {
  return {gfx::Rect(260, 30, 400, 600), gfx::Rect(660, 30, 400, 600)};
}

TEST(AtriumOwnWindowDropTest, SidebarPointIsTheSidebar) {
  const OwnWindowDrop drop =
      ChooseOwnWindowDrop(gfx::Point(100, 300), kSidebar, Panes());
  EXPECT_EQ(drop.kind, OwnWindowDropKind::kSidebar);
  EXPECT_EQ(drop.pane_index, -1);
}

TEST(AtriumOwnWindowDropTest, PanePointNamesThePaneAndItsEdge) {
  const OwnWindowDrop right =
      ChooseOwnWindowDrop(gfx::Point(1040, 300), kSidebar, Panes());
  EXPECT_EQ(right.kind, OwnWindowDropKind::kPane);
  EXPECT_EQ(right.pane_index, 1);
  EXPECT_EQ(right.edge, SplitEdge::kRight);

  const OwnWindowDrop left =
      ChooseOwnWindowDrop(gfx::Point(270, 300), kSidebar, Panes());
  EXPECT_EQ(left.pane_index, 0);
  EXPECT_EQ(left.edge, SplitEdge::kLeft);
}

TEST(AtriumOwnWindowDropTest, PaneCentreIsTheCentre) {
  const OwnWindowDrop drop =
      ChooseOwnWindowDrop(gfx::Point(860, 330), kSidebar, Panes());
  EXPECT_EQ(drop.kind, OwnWindowDropKind::kPane);
  EXPECT_EQ(drop.pane_index, 1);
  EXPECT_EQ(drop.edge, SplitEdge::kCenter);
}

TEST(AtriumOwnWindowDropTest, PointOffTheWindowIsElsewhere) {
  EXPECT_EQ(ChooseOwnWindowDrop(gfx::Point(1500, 300), kSidebar, Panes()).kind,
            OwnWindowDropKind::kElsewhere);
  EXPECT_EQ(ChooseOwnWindowDrop(gfx::Point(300, 5), kSidebar, Panes()).kind,
            OwnWindowDropKind::kElsewhere);
}

TEST(AtriumOwnWindowDropTest, CollapsedSidebarNeverClaimsAPoint) {
  EXPECT_EQ(ChooseOwnWindowDrop(gfx::Point(100, 300), gfx::Rect(), Panes()).kind,
            OwnWindowDropKind::kElsewhere);
}

}  // namespace
}  // namespace atrium
