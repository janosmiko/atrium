// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_sidebar_seam_line.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/rect.h"

namespace atrium {
namespace {

// The band as Layout hands it over: the seam column plus the reach back over
// the sidebar, full window height.
constexpr int kBandWidth = kSidebarGripWidth + kSidebarGripReach;
constexpr int kWindowHeight = 800;

TEST(AtriumSidebarSeamLineTest, ThePillIsThickerThanAHairline) {
  const gfx::RectF pill = SidebarSeamLineBounds(
      gfx::Rect(0, 0, kBandWidth, kWindowHeight), /*sidebar_on_right=*/false);
  EXPECT_GT(pill.width(), 1.0f);
  EXPECT_EQ(4.0f, pill.width());
}

TEST(AtriumSidebarSeamLineTest, ThePillSitsCentredInTheSeamColumn) {
  // Left-docked the column is the band's trailing 6px, so [4, 10). A 4px pill
  // centred there spans [5, 9).
  const gfx::RectF pill = SidebarSeamLineBounds(
      gfx::Rect(0, 0, kBandWidth, kWindowHeight), /*sidebar_on_right=*/false);
  EXPECT_EQ(5.0f, pill.x());
  EXPECT_EQ(9.0f, pill.right());
}

TEST(AtriumSidebarSeamLineTest, ThePillHugsTheNearEdgeDockedRight) {
  // The reach flips to the far side, so the column is the band's first 6px.
  const gfx::RectF pill = SidebarSeamLineBounds(
      gfx::Rect(0, 0, kBandWidth, kWindowHeight), /*sidebar_on_right=*/true);
  EXPECT_EQ(1.0f, pill.x());
  EXPECT_EQ(5.0f, pill.right());
}

TEST(AtriumSidebarSeamLineTest, ThePillStopsShortOfBothEnds) {
  const gfx::RectF pill = SidebarSeamLineBounds(
      gfx::Rect(0, 0, kBandWidth, kWindowHeight), /*sidebar_on_right=*/false);
  EXPECT_EQ(16.0f, pill.y());
  EXPECT_EQ(784.0f, pill.bottom());
  EXPECT_LT(pill.height(), static_cast<float>(kWindowHeight));
}

TEST(AtriumSidebarSeamLineTest, ThePillTracksTheBandsOrigin) {
  const gfx::RectF pill = SidebarSeamLineBounds(
      gfx::Rect(40, 12, kBandWidth, kWindowHeight), /*sidebar_on_right=*/true);
  EXPECT_EQ(41.0f, pill.x());
  EXPECT_EQ(28.0f, pill.y());
}

TEST(AtriumSidebarSeamLineTest, AShortBandKeepsAProportionalGap) {
  // 30 tall: a fixed 16pt inset each end would leave nothing to draw.
  const gfx::RectF pill = SidebarSeamLineBounds(
      gfx::Rect(0, 0, kBandWidth, 30), /*sidebar_on_right=*/false);
  EXPECT_EQ(10.0f, pill.y());
  EXPECT_EQ(10.0f, pill.height());
  EXPECT_GT(pill.height(), 0.0f);
}

TEST(AtriumSidebarSeamLineTest, AnUnrevealedHoverStaysHidden) {
  // Covers both no hover at all and hover still inside the reveal delay - the
  // pure function only sees the fade's progress, so a quick pass across the
  // band that never starts the fade must not paint.
  EXPECT_EQ(0.f, SidebarSeamLineOpacity(/*reveal_t=*/0.f, /*dragging=*/false));
}

TEST(AtriumSidebarSeamLineTest, ThePillWearsTheAccentAtTheSeamsAlpha) {
  const SkColor nord = SkColorSetRGB(0x88, 0xC0, 0xD0);
  const SkColor lit = SidebarSeamLineColor(nord, /*opacity=*/1.f);
  EXPECT_EQ(0x88u, SkColorGetR(lit));
  EXPECT_EQ(0xC0u, SkColorGetG(lit));
  EXPECT_EQ(0xD0u, SkColorGetB(lit));
  EXPECT_EQ(kSidebarSeamLineAlpha, SkColorGetA(lit));
  // Mid-fade the alpha follows the opacity, the colour stays the accent.
  const SkColor fading = SidebarSeamLineColor(nord, /*opacity=*/0.4f);
  EXPECT_EQ(94u, SkColorGetA(fading));
  EXPECT_EQ(0xC0u, SkColorGetG(fading));
}

TEST(AtriumSidebarSeamLineTest, TheRevealEasesInRatherThanAppearing) {
  // The middle of the fade must be partly transparent, or the delay would end
  // in the one-frame appearance the fade exists to replace.
  EXPECT_FLOAT_EQ(0.4f,
                  SidebarSeamLineOpacity(/*reveal_t=*/0.4f, /*dragging=*/false));
  EXPECT_EQ(1.f, SidebarSeamLineOpacity(/*reveal_t=*/1.f, /*dragging=*/false));
}

TEST(AtriumSidebarSeamLineTest, ADragPaintsFullyInsideTheDelayWindow) {
  // A press that starts a drag before the fade begins must not wait on it, and
  // must not paint a half-faded line either.
  EXPECT_EQ(1.f, SidebarSeamLineOpacity(/*reveal_t=*/0.f, /*dragging=*/true));
  EXPECT_EQ(1.f, SidebarSeamLineOpacity(/*reveal_t=*/0.5f, /*dragging=*/true));
}

TEST(AtriumSidebarSeamLineTest, AProgressOutsideTheRangeIsHeldToIt) {
  // A tick that lands past the duration, or a clock that steps backwards, must
  // not produce an alpha the paint flags cannot carry.
  EXPECT_EQ(1.f, SidebarSeamLineOpacity(/*reveal_t=*/1.6f, /*dragging=*/false));
  EXPECT_EQ(0.f, SidebarSeamLineOpacity(/*reveal_t=*/-0.2f,
                                        /*dragging=*/false));
}

}  // namespace
}  // namespace atrium
