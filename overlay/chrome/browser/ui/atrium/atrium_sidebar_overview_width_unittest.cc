// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
#include "chrome/browser/ui/atrium/atrium_sidebar_overview_width.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

TEST(AtriumSidebarOverviewWidthTest, ANarrowSidebarWidensToTheOverviewMinimum) {
  EXPECT_EQ(EffectiveSidebarWidth(true, 280, kOverviewSidebarMinWidth),
            kOverviewSidebarMinWidth);
}

TEST(AtriumSidebarOverviewWidthTest, AWiderSidebarKeepsTheUsersWidth) {
  EXPECT_EQ(EffectiveSidebarWidth(true, 396, kOverviewSidebarMinWidth), 396);
}

TEST(AtriumSidebarOverviewWidthTest, TheMinimumItselfIsNotWidened) {
  EXPECT_EQ(EffectiveSidebarWidth(true, kOverviewSidebarMinWidth,
                                  kOverviewSidebarMinWidth),
            kOverviewSidebarMinWidth);
}

TEST(AtriumSidebarOverviewWidthTest, ClosingReturnsTheUsersWidth) {
  EXPECT_EQ(EffectiveSidebarWidth(false, 280, kOverviewSidebarMinWidth), 280);
}

TEST(AtriumSidebarOverviewWidthTest, ClosingLeavesAWideSidebarAlone) {
  EXPECT_EQ(EffectiveSidebarWidth(false, 396, kOverviewSidebarMinWidth), 396);
}

// The clamp in SetSidebarWidth tops out at 500. A minimum above that would
// widen to 500 instead, and the decision's own answer would never be reached.
TEST(AtriumSidebarOverviewWidthTest, TheMinimumFitsUnderTheSidebarWidthClamp) {
  EXPECT_LE(kOverviewSidebarMinWidth, 500);
}

}  // namespace
}  // namespace atrium
