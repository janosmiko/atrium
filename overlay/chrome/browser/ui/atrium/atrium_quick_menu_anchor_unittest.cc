// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_quick_menu_anchor_unittest.cc
#include "chrome/browser/ui/atrium/atrium_quick_menu_anchor.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

constexpr gfx::Size kWindow(1200, 800);
constexpr gfx::Size kMenu(232, 300);

TEST(AtriumQuickMenuAnchorTest, SitsSixBelowTheAnchorAndLeftAligned) {
  const gfx::Rect placed =
      PlaceQuickMenu(gfx::Rect(40, 46, 20, 34), kWindow, kMenu);
  EXPECT_EQ(placed.x(), 40);
  EXPECT_EQ(placed.y(), 46 + 34 + kQuickMenuAnchorGap);
  EXPECT_EQ(placed.width(), kMenu.width());
  EXPECT_EQ(placed.height(), kMenu.height());
}

TEST(AtriumQuickMenuAnchorTest, ClampsToTheWindowsRightEdge) {
  const gfx::Rect placed =
      PlaceQuickMenu(gfx::Rect(1150, 46, 20, 34), kWindow, kMenu);
  EXPECT_EQ(placed.right(), kWindow.width() - kQuickMenuEdgeInset);
}

TEST(AtriumQuickMenuAnchorTest, NeverSlidesPastTheLeftEdge) {
  // A menu wider than the window would otherwise clamp to a negative x.
  const gfx::Rect placed =
      PlaceQuickMenu(gfx::Rect(10, 46, 20, 34), gfx::Size(200, 800), kMenu);
  EXPECT_EQ(placed.x(), kQuickMenuEdgeInset);
}

TEST(AtriumQuickMenuAnchorTest, LiftsAMenuThatWouldRunOffTheBottom) {
  const gfx::Rect placed =
      PlaceQuickMenu(gfx::Rect(40, 700, 20, 34), kWindow, kMenu);
  EXPECT_EQ(placed.bottom(), kWindow.height() - kQuickMenuEdgeInset);
}

TEST(AtriumQuickMenuAnchorTest, AZeroSizedMenuIsPlacedRatherThanRefused) {
  const gfx::Rect placed =
      PlaceQuickMenu(gfx::Rect(40, 46, 20, 34), kWindow, gfx::Size());
  EXPECT_TRUE(placed.IsEmpty());
  EXPECT_EQ(placed.x(), 40);
}

// The widget shows before the renderer reports a size, so the first Show()
// places a card of this size and OnHtmlContentSizeChanged re-places it.
TEST(AtriumQuickMenuAnchorTest, TheFallbackCardSizeIsTheSpecWidth) {
  EXPECT_EQ(kQuickMenuFallbackSize.width(), 232);
  EXPECT_GT(kQuickMenuFallbackSize.height(), 0);
}

constexpr gfx::Size kPicker(264, 220);

TEST(PlaceSpaceIconPickerTest, SitsBelowTheDotByDefault) {
  const gfx::Rect placed =
      PlaceSpaceIconPicker(gfx::Rect(40, 46, 20, 20), kWindow, kPicker);
  EXPECT_EQ(placed.x(), 40);
  EXPECT_EQ(placed.y(), 46 + 20 + kSpaceIconPickerAnchorGap);
}

TEST(PlaceSpaceIconPickerTest, FlipsAboveAnAnchorNearTheBottom) {
  const gfx::Rect anchor(40, 700, 20, 20);
  const gfx::Rect placed = PlaceSpaceIconPicker(anchor, kWindow, kPicker);
  EXPECT_EQ(placed.bottom(), anchor.y() - kSpaceIconPickerAnchorGap);
}

TEST(PlaceSpaceIconPickerTest, ClampsToTheWindowsRightEdge) {
  const gfx::Rect placed =
      PlaceSpaceIconPicker(gfx::Rect(1150, 46, 20, 20), kWindow, kPicker);
  EXPECT_EQ(placed.right(), kWindow.width() - kSpaceIconPickerEdgeInset);
}

TEST(PlaceSpaceIconPickerTest, APickerWiderThanTheWindowStaysAtTheInset) {
  const gfx::Rect placed = PlaceSpaceIconPicker(
      gfx::Rect(10, 46, 20, 20), gfx::Size(200, 800), kPicker);
  EXPECT_EQ(placed.x(), kSpaceIconPickerEdgeInset);
}

}  // namespace
}  // namespace atrium
