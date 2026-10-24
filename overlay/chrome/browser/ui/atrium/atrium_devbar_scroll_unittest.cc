// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_devbar_scroll.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

TEST(DevBarScrollAwayTest, ADownwardScrollPastTheDeadZoneHidesTheBar) {
  DevBarScrollAway rule;
  EXPECT_EQ(rule.OnScroll(-5), std::nullopt);
  EXPECT_EQ(rule.OnScroll(-5), std::nullopt);
  EXPECT_EQ(rule.OnScroll(-5), std::optional<bool>(true));
  EXPECT_TRUE(rule.away());
  EXPECT_EQ(rule.OnScroll(-40), std::nullopt);
}

TEST(DevBarScrollAwayTest, AnUpwardScrollPastTheDeadZoneBringsItBack) {
  DevBarScrollAway rule;
  rule.OnScroll(-30);
  EXPECT_EQ(rule.OnScroll(8), std::nullopt);
  EXPECT_EQ(rule.OnScroll(8), std::optional<bool>(false));
  EXPECT_FALSE(rule.away());
}

TEST(DevBarScrollAwayTest, ATurnStartsTheCountOver) {
  DevBarScrollAway rule;
  rule.OnScroll(-200);
  EXPECT_EQ(rule.OnScroll(11), std::nullopt);
  EXPECT_EQ(rule.OnScroll(-11), std::nullopt);
  EXPECT_EQ(rule.OnScroll(12), std::optional<bool>(false));
}

TEST(DevBarScrollAwayTest, JitterUnderTheDeadZoneMovesNothing) {
  DevBarScrollAway rule;
  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(rule.OnScroll(i % 2 ? 3 : -3), std::nullopt);
  }
  EXPECT_FALSE(rule.away());
}

TEST(DevBarScrollAwayTest, ResetShowsTheBarAndReportsOnlyAChange) {
  DevBarScrollAway rule;
  EXPECT_EQ(rule.Reset(), std::nullopt);
  rule.OnScroll(-30);
  EXPECT_EQ(rule.Reset(), std::optional<bool>(false));
  EXPECT_FALSE(rule.away());
}

// --- The pin ---------------------------------------------------------------

TEST(DevBarScrollAwayTest, PinnedTheBarIgnoresEveryScroll) {
  DevBarScrollAway rule;
  EXPECT_EQ(rule.SetPinned(true), std::nullopt);
  EXPECT_TRUE(rule.pinned());
  for (int i = 0; i < 20; ++i) {
    EXPECT_EQ(rule.OnScroll(-40), std::nullopt);
  }
  EXPECT_FALSE(rule.away());
}

TEST(DevBarScrollAwayTest, PinningWhileAwayBringsTheBarBack) {
  DevBarScrollAway rule;
  rule.OnScroll(-30);
  EXPECT_TRUE(rule.away());
  EXPECT_EQ(rule.SetPinned(true), std::optional<bool>(false));
  EXPECT_FALSE(rule.away());
}

TEST(DevBarScrollAwayTest, UnpinningRestoresTheOldBehaviour) {
  DevBarScrollAway rule;
  rule.SetPinned(true);
  rule.OnScroll(-200);
  EXPECT_EQ(rule.SetPinned(false), std::nullopt);
  // The travel banked while pinned is not spent on the first move after: a
  // fresh dead zone has to be crossed.
  EXPECT_EQ(rule.OnScroll(-11), std::nullopt);
  EXPECT_EQ(rule.OnScroll(-11), std::optional<bool>(true));
}

TEST(DevBarScrollAwayTest, PinningTwiceReportsOnlyTheChange) {
  DevBarScrollAway rule;
  rule.OnScroll(-30);
  EXPECT_EQ(rule.SetPinned(true), std::optional<bool>(false));
  EXPECT_EQ(rule.SetPinned(true), std::nullopt);
}

TEST(DevBarScrollAwayTest, ResetLeavesThePinAlone) {
  DevBarScrollAway rule;
  rule.SetPinned(true);
  EXPECT_EQ(rule.Reset(), std::nullopt);
  EXPECT_TRUE(rule.pinned());
  EXPECT_EQ(rule.OnScroll(-40), std::nullopt);
}

}  // namespace
}  // namespace atrium
