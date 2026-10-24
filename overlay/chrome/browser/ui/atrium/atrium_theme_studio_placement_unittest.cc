// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_theme_studio_placement_unittest.cc
#include "chrome/browser/ui/atrium/atrium_theme_studio_placement.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/rect.h"

namespace atrium {
namespace {

constexpr gfx::Rect kParent(100, 100, 1600, 900);

TEST(ThemeStudioPlacement, SizeIsTheCardPlusItsOutset) {
  EXPECT_EQ(gfx::Size(332, 632), ThemeStudioSizeFor(kParent));
}

TEST(ThemeStudioPlacement, AShortParentShrinksTheHeightOnly) {
  const gfx::Size size = ThemeStudioSizeFor(gfx::Rect(0, 0, 1600, 500));
  EXPECT_EQ(332, size.width());
  EXPECT_EQ(500, size.height());
}

TEST(ThemeStudioPlacement, OpensAboveTheAnchorAtItsLeftEdge) {
  // A dot low in the sidebar: the card ends 8px above it.
  const gfx::Rect anchor(140, 940, 14, 14);
  const gfx::Rect bounds = PlaceThemeStudioAtAnchor(anchor, kParent);
  // Card left = anchor left, widget left is one outset further out.
  EXPECT_EQ(140 - kThemeStudioOutset, bounds.x());
  // Card bottom = anchor top - gap, widget bottom is one outset lower.
  EXPECT_EQ(940 - kThemeStudioAnchorGap + kThemeStudioOutset, bounds.bottom());
  EXPECT_EQ(ThemeStudioSizeFor(kParent), bounds.size());
}

TEST(ThemeStudioPlacement, AnAnchorNearTheTopClampsIntoTheParent) {
  const gfx::Rect anchor(140, 120, 14, 14);
  const gfx::Rect bounds = PlaceThemeStudioAtAnchor(anchor, kParent);
  EXPECT_EQ(kParent.y(), bounds.y());
  EXPECT_TRUE(kParent.Contains(bounds));
}

TEST(ThemeStudioPlacement, ClampKeepsTheWholeWidgetInside) {
  const gfx::Rect out(1500, 800, 332, 632);
  const gfx::Rect bounds = ClampThemeStudioBounds(out, kParent);
  EXPECT_EQ(kParent.right() - 332, bounds.x());
  EXPECT_EQ(kParent.bottom() - 632, bounds.y());
  EXPECT_TRUE(kParent.Contains(bounds));
}

TEST(ThemeStudioPlacement, ClampLeavesAnInsideWidgetAlone) {
  const gfx::Rect in(400, 200, 332, 632);
  EXPECT_EQ(in, ClampThemeStudioBounds(in, kParent));
}

TEST(ThemeStudioPlacement, AWidgetTallerThanTheParentPinsToTheOrigin) {
  const gfx::Rect small(0, 0, 300, 400);
  const gfx::Rect bounds = ClampThemeStudioBounds(gfx::Rect(50, 50, 332, 632), small);
  EXPECT_EQ(gfx::Point(0, 0), bounds.origin());
}

TEST(ThemeStudioPlacement, FollowingTheParentKeepsTheOffset) {
  const gfx::Rect bounds(400, 200, 332, 632);
  const gfx::Rect moved(300, 350, 1600, 900);
  EXPECT_EQ(gfx::Rect(600, 450, 332, 632),
            FollowParentBounds(bounds, kParent, moved));
}

TEST(ThemeStudioPlacement, FollowingAShrunkParentReclamps) {
  const gfx::Rect bounds(1300, 500, 332, 632);
  const gfx::Rect shrunk(100, 100, 1000, 700);
  const gfx::Rect after = FollowParentBounds(bounds, kParent, shrunk);
  EXPECT_TRUE(shrunk.Contains(after));
  EXPECT_EQ(shrunk.right() - 332, after.x());
}

}  // namespace
}  // namespace atrium
