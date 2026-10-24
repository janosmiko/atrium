// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_devtools_card_corners.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

constexpr float kRadius = 12.f;

// The content card, in pane-local coordinates.
constexpr gfx::Rect kCard(0, 0, 1000, 800);

TEST(AtriumDevToolsCardCorners, UndockedTileKeepsAllFourCorners) {
  const gfx::RoundedCornersF corners = CardCornerRadii(kCard, kCard, kRadius);
  EXPECT_EQ(kRadius, corners.upper_left());
  EXPECT_EQ(kRadius, corners.upper_right());
  EXPECT_EQ(kRadius, corners.lower_right());
  EXPECT_EQ(kRadius, corners.lower_left());
}

TEST(AtriumDevToolsCardCorners, EmptyTileGetsNoRounding) {
  const gfx::RoundedCornersF corners =
      CardCornerRadii(kCard, gfx::Rect(), kRadius);
  EXPECT_EQ(0.f, corners.upper_left());
  EXPECT_EQ(0.f, corners.upper_right());
  EXPECT_EQ(0.f, corners.lower_right());
  EXPECT_EQ(0.f, corners.lower_left());
}

// Docked at the bottom: the seam between contents and devtools must be
// straight - contents keeps only its top corners, devtools only its bottom
// ones. The reported bug was rounding showing up at that seam while the
// card's real bottom corners stayed square.
TEST(AtriumDevToolsCardCorners, DockedBottomRoundsOnlyTheOuterEdges) {
  const gfx::Rect contents(0, 0, 1000, 500);
  const gfx::Rect devtools(0, 500, 1000, 300);

  const gfx::RoundedCornersF top = CardCornerRadii(kCard, contents, kRadius);
  EXPECT_EQ(kRadius, top.upper_left());
  EXPECT_EQ(kRadius, top.upper_right());
  EXPECT_EQ(0.f, top.lower_right());
  EXPECT_EQ(0.f, top.lower_left());

  const gfx::RoundedCornersF bottom = CardCornerRadii(kCard, devtools, kRadius);
  EXPECT_EQ(0.f, bottom.upper_left());
  EXPECT_EQ(0.f, bottom.upper_right());
  EXPECT_EQ(kRadius, bottom.lower_right());
  EXPECT_EQ(kRadius, bottom.lower_left());
}

TEST(AtriumDevToolsCardCorners, DockedRightRoundsOnlyTheOuterEdges) {
  const gfx::Rect contents(0, 0, 700, 800);
  const gfx::Rect devtools(700, 0, 300, 800);

  const gfx::RoundedCornersF left = CardCornerRadii(kCard, contents, kRadius);
  EXPECT_EQ(kRadius, left.upper_left());
  EXPECT_EQ(0.f, left.upper_right());
  EXPECT_EQ(0.f, left.lower_right());
  EXPECT_EQ(kRadius, left.lower_left());

  const gfx::RoundedCornersF right = CardCornerRadii(kCard, devtools, kRadius);
  EXPECT_EQ(0.f, right.upper_left());
  EXPECT_EQ(kRadius, right.upper_right());
  EXPECT_EQ(kRadius, right.lower_right());
  EXPECT_EQ(0.f, right.lower_left());
}

TEST(AtriumDevToolsCardCorners, DockedLeftRoundsOnlyTheOuterEdges) {
  const gfx::Rect devtools(0, 0, 300, 800);
  const gfx::Rect contents(300, 0, 700, 800);

  const gfx::RoundedCornersF left = CardCornerRadii(kCard, devtools, kRadius);
  EXPECT_EQ(kRadius, left.upper_left());
  EXPECT_EQ(0.f, left.upper_right());
  EXPECT_EQ(0.f, left.lower_right());
  EXPECT_EQ(kRadius, left.lower_left());

  const gfx::RoundedCornersF right = CardCornerRadii(kCard, contents, kRadius);
  EXPECT_EQ(0.f, right.upper_left());
  EXPECT_EQ(kRadius, right.upper_right());
  EXPECT_EQ(kRadius, right.lower_right());
  EXPECT_EQ(0.f, right.lower_left());
}

// hide_inspected_contents: devtools covers the whole card and takes every
// corner back, so the silhouette stays rounded with the contents hidden.
TEST(AtriumDevToolsCardCorners, TileCoveringTheWholeCardTakesAllFour) {
  const gfx::RoundedCornersF corners =
      CardCornerRadii(kCard, gfx::Rect(0, 0, 1000, 800), kRadius);
  EXPECT_EQ(kRadius, corners.upper_left());
  EXPECT_EQ(kRadius, corners.upper_right());
  EXPECT_EQ(kRadius, corners.lower_right());
  EXPECT_EQ(kRadius, corners.lower_left());
}

// The card is not always at the origin (split view puts the secondary pane at
// an offset), so the corner test is relative to the card, not to (0,0).
TEST(AtriumDevToolsCardCorners, OffsetCardComparesAgainstItsOwnEdges) {
  constexpr gfx::Rect card(40, 60, 500, 400);
  const gfx::Rect contents(40, 60, 500, 250);
  const gfx::Rect devtools(40, 310, 500, 150);

  const gfx::RoundedCornersF top = CardCornerRadii(card, contents, kRadius);
  EXPECT_EQ(kRadius, top.upper_left());
  EXPECT_EQ(kRadius, top.upper_right());
  EXPECT_EQ(0.f, top.lower_right());
  EXPECT_EQ(0.f, top.lower_left());

  const gfx::RoundedCornersF bottom = CardCornerRadii(card, devtools, kRadius);
  EXPECT_EQ(0.f, bottom.upper_left());
  EXPECT_EQ(0.f, bottom.upper_right());
  EXPECT_EQ(kRadius, bottom.lower_right());
  EXPECT_EQ(kRadius, bottom.lower_left());
}

}  // namespace
}  // namespace atrium
