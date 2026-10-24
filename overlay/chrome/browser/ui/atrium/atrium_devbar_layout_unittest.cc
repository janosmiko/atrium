// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_devbar_layout.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

constexpr int kWidgetHeight =
    kDevBarHeight + kDevBarShadowPadTop + kDevBarShadowPadBottom;
constexpr int kWidestWidget = kDevBarMaxWidth + 2 * kDevBarShadowPadX;

TEST(DevBarLayoutTest, AWideCardCentresTheBarAtItsWidest) {
  EXPECT_EQ(DevBarBoundsInRect(gfx::Rect(12, 40, 1200, 800)),
            gfx::Rect(12 + (1200 - kWidestWidget) / 2,
                      40 + kDevBarMargin - kDevBarShadowPadTop, kWidestWidget,
                      kWidgetHeight));
}

TEST(DevBarLayoutTest, EachPaneOfARowSplitCentresItsOwnBar) {
  EXPECT_EQ(DevBarBoundsInRect(gfx::Rect(0, 0, 600, 800)),
            gfx::Rect(10, 0, 580, kWidgetHeight));
  EXPECT_EQ(DevBarBoundsInRect(gfx::Rect(606, 0, 600, 800)),
            gfx::Rect(616, 0, 580, kWidgetHeight));
}

TEST(DevBarLayoutTest, TheLowerPaneOfAColumnSplitWearsItsBarAtItsOwnTop) {
  EXPECT_EQ(DevBarBoundsInRect(gfx::Rect(0, 406, 1200, 400)).y(),
            406 + kDevBarMargin - kDevBarShadowPadTop);
}

TEST(DevBarLayoutTest, FourNarrowColumnsKeepEveryBarInsideItsPane) {
  for (int i = 0; i < 4; ++i) {
    const gfx::Rect pane(i * 156, 0, 150, 800);
    const gfx::Rect bar = DevBarBoundsInRect(pane);
    EXPECT_EQ(bar.width(), 150 - 2 * kDevBarMargin);
    EXPECT_EQ(bar.x(), pane.x() + kDevBarMargin);
  }
}

TEST(DevBarLayoutTest, APaneNarrowerThanItsMarginsGetsNoWidth) {
  EXPECT_EQ(DevBarBoundsInRect(gfx::Rect(0, 0, 12, 800)).width(), 0);
}

}  // namespace
}  // namespace atrium
