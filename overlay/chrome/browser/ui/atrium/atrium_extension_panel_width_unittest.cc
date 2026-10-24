// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_extension_panel_width.h"

#include <optional>

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

TEST(AtriumExtensionPanelWidthTest, NoStoredWidthGivesTheDefault) {
  EXPECT_EQ(ResolveExtensionPanelWidth(std::nullopt, 1400), 400);
}

TEST(AtriumExtensionPanelWidthTest, AStoredWidthInRangeIsKept) {
  EXPECT_EQ(ResolveExtensionPanelWidth(360, 1400), 360);
}

TEST(AtriumExtensionPanelWidthTest, TheMaximumIsHalfTheWindow) {
  EXPECT_EQ(ClampExtensionPanelWidth(900, 1400), 700);
  EXPECT_EQ(ResolveExtensionPanelWidth(5000, 1200), 600);
}

TEST(AtriumExtensionPanelWidthTest, TheMinimumWinsInAWindowUnder640) {
  // Half of 600 is 300, under the floor. The floor wins, so the page keeps
  // 280px rather than the panel dropping below a usable width.
  EXPECT_EQ(ClampExtensionPanelWidth(400, 600), 320);
  EXPECT_EQ(ClampExtensionPanelWidth(100, 600), 320);
}

TEST(AtriumExtensionPanelWidthTest, AWindowNotLaidOutYetAppliesOnlyTheFloor) {
  EXPECT_EQ(ClampExtensionPanelWidth(450, 0), 450);
  EXPECT_EQ(ClampExtensionPanelWidth(100, 0), 320);
  EXPECT_EQ(ClampExtensionPanelWidth(450, -20), 450);
}

TEST(AtriumExtensionPanelWidthTest, ANegativeStoredWidthGoesToTheFloor) {
  EXPECT_EQ(ResolveExtensionPanelWidth(-5, 1400), 320);
  EXPECT_EQ(ResolveExtensionPanelWidth(0, 1400), 320);
}

}  // namespace
}  // namespace atrium
