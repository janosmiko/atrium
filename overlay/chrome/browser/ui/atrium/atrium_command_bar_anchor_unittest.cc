// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
#include "chrome/browser/ui/atrium/atrium_command_bar_anchor.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace atrium {
namespace {

TEST(AtriumCommandBarAnchorTest, AFittingRowKeepsItsLeftAndTop) {
  const PalettePlacement placement = PlaceAnchoredPalette(
      gfx::Rect(12, 96, 400, 34), gfx::Size(1200, 800), 320);
  EXPECT_EQ(placement.left, 12);
  EXPECT_EQ(placement.top, 96);
  EXPECT_EQ(placement.width, 400);
}

TEST(AtriumCommandBarAnchorTest, ANarrowRowWidensToTheMinimum) {
  // The sidebar's field is about 240pt at the default width, which is too
  // narrow to type a URL into.
  const PalettePlacement placement = PlaceAnchoredPalette(
      gfx::Rect(12, 96, 240, 34), gfx::Size(1200, 800), 320);
  EXPECT_EQ(placement.width, 320);
  EXPECT_EQ(placement.left, 12) << "widening moved the box off its row";
}

TEST(AtriumCommandBarAnchorTest, ARowNearTheRightEdgeSlidesBackIn) {
  const PalettePlacement placement = PlaceAnchoredPalette(
      gfx::Rect(1000, 96, 240, 34), gfx::Size(1200, 800), 320);
  EXPECT_EQ(placement.width, 320);
  EXPECT_EQ(placement.left, 880) << "the box hangs off the window's right";
}

TEST(AtriumCommandBarAnchorTest, ARowNearTheBottomLiftsToFitTheList) {
  const PalettePlacement placement = PlaceAnchoredPalette(
      gfx::Rect(12, 760, 400, 34), gfx::Size(1200, 800), 320);
  EXPECT_EQ(placement.top, 800 - kAnchoredPaletteHeight)
      << "the suggestions grow off the bottom of the window";
}

TEST(AtriumCommandBarAnchorTest, AWindowNarrowerThanTheMinimumGetsAllOfIt) {
  const PalettePlacement placement = PlaceAnchoredPalette(
      gfx::Rect(4, 96, 240, 34), gfx::Size(280, 800), 320);
  EXPECT_EQ(placement.width, 280);
  EXPECT_EQ(placement.left, 0);
}

TEST(AtriumCommandBarAnchorTest, AWindowShorterThanTheCardStartsAtTheTop) {
  const PalettePlacement placement = PlaceAnchoredPalette(
      gfx::Rect(12, 96, 400, 34), gfx::Size(1200, 180), 320);
  EXPECT_EQ(placement.top, 0);
}

TEST(AtriumCommandBarAnchorTest, TheBoxStartsAtTheRowsOwnHeight) {
  // The expand runs from this to the card's full height, so a row height
  // dropped here would make the palette pop rather than grow.
  const PalettePlacement placement = PlaceAnchoredPalette(
      gfx::Rect(12, 96, 400, 34), gfx::Size(1200, 800), 320);
  EXPECT_EQ(placement.height, 34);
}

}  // namespace
}  // namespace atrium
