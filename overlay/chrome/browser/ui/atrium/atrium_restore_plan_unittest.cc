// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_restore_plan_unittest.cc
#include "chrome/browser/ui/atrium/atrium_restore_plan.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

TEST(AtriumRestorePlanTest, JsonThatIsNotAnObjectIsNotAPlan) {
  EXPECT_FALSE(ParseRestorePlan("not json").parsed);
  EXPECT_FALSE(ParseRestorePlan("[1,2,3]").parsed);
}

TEST(AtriumRestorePlanTest, AnEmptyPlanParsesAndAsksForNothing) {
  RestorePlan plan = ParseRestorePlan("{}");
  EXPECT_TRUE(plan.parsed);
  EXPECT_TRUE(plan.close_tab_ids.empty());
  EXPECT_TRUE(plan.reopen.empty());
  EXPECT_FALSE(plan.select_tab_id.has_value());
}

TEST(AtriumRestorePlanTest, CloseIdsSurviveAsNumbers) {
  RestorePlan plan = ParseRestorePlan(R"({"closeTabIds":[7,9]})");
  ASSERT_EQ(2u, plan.close_tab_ids.size());
  EXPECT_EQ(7u, plan.close_tab_ids[0]);
  EXPECT_EQ(9u, plan.close_tab_ids[1]);
}

TEST(AtriumRestorePlanTest, ANonNumericCloseIdIsDropped) {
  RestorePlan plan = ParseRestorePlan(R"({"closeTabIds":["7",null,9]})");
  ASSERT_EQ(1u, plan.close_tab_ids.size());
  EXPECT_EQ(9u, plan.close_tab_ids[0]);
}

// The defect this task fixes: every reopened row used to open in the
// foreground, so the LAST one silently took the selection.
TEST(AtriumRestorePlanTest, OnlyTheFlaggedRowIsReopenedSelected) {
  RestorePlan plan = ParseRestorePlan(R"({"reopen":[
      {"url":"https://a.example/","selected":true},
      {"url":"https://b.example/","selected":false},
      {"url":"https://c.example/","selected":false}]})");
  ASSERT_EQ(3u, plan.reopen.size());
  EXPECT_TRUE(plan.reopen[0].selected);
  EXPECT_FALSE(plan.reopen[1].selected);
  EXPECT_FALSE(plan.reopen[2].selected);
  EXPECT_EQ("https://a.example/", plan.reopen[0].url);
}

// A plan from a core that predates the flag must not hand the selection to
// whichever row happens to come last.
TEST(AtriumRestorePlanTest, AMissingSelectedFlagMeansBackground) {
  RestorePlan plan =
      ParseRestorePlan(R"({"reopen":[{"url":"https://a.example/"},
                                     {"url":"https://b.example/"}]})");
  ASSERT_EQ(2u, plan.reopen.size());
  EXPECT_FALSE(plan.reopen[0].selected);
  EXPECT_FALSE(plan.reopen[1].selected);
}

TEST(AtriumRestorePlanTest, RowsWithoutAUsableUrlAreDropped) {
  RestorePlan plan = ParseRestorePlan(R"({"reopen":[
      {"url":""},
      {"title":"no url at all"},
      "not a row",
      {"url":"https://kept.example/","selected":true}]})");
  ASSERT_EQ(1u, plan.reopen.size());
  EXPECT_EQ("https://kept.example/", plan.reopen[0].url);
  EXPECT_TRUE(plan.reopen[0].selected);
}

// Live selection and reopened selection are mutually exclusive by
// construction in the core; the reader must carry both shapes.
TEST(AtriumRestorePlanTest, ALiveSelectionComesBackAsATabId) {
  RestorePlan plan =
      ParseRestorePlan(R"({"selectTabId":42,"reopen":[
          {"url":"https://a.example/","selected":false}]})");
  ASSERT_TRUE(plan.select_tab_id.has_value());
  EXPECT_EQ(42u, *plan.select_tab_id);
  EXPECT_FALSE(plan.reopen[0].selected);
}

TEST(AtriumRestorePlanTest, ADeadSelectionLeavesNoTabIdToActivate) {
  RestorePlan plan = ParseRestorePlan(
      R"({"reopen":[{"url":"https://a.example/","selected":true}]})");
  EXPECT_FALSE(plan.select_tab_id.has_value());
  EXPECT_TRUE(plan.reopen[0].selected);
}

// RestoreSidebarSlice needs each entry's space to tell a
// background space's row apart from one in the space it is about to show.
TEST(AtriumRestorePlanTest, ReopenEntriesCarryTheirSpace) {
  RestorePlan plan = ParseRestorePlan(R"({"activeSpace":1,"reopen":[
      {"url":"https://a.example/","space":0},
      {"url":"https://b.example/","space":1}]})");
  ASSERT_EQ(2u, plan.reopen.size());
  EXPECT_EQ(0u, plan.reopen[0].space);
  EXPECT_EQ(1u, plan.reopen[1].space);
  EXPECT_EQ(1u, plan.active_space);
}

TEST(AtriumRestorePlanTest, AMissingSpaceOrActiveSpaceDefaultsToZero) {
  RestorePlan plan =
      ParseRestorePlan(R"({"reopen":[{"url":"https://a.example/"}]})");
  EXPECT_EQ(0u, plan.reopen[0].space);
  EXPECT_EQ(0u, plan.active_space);
}

// RestoreSidebarSlice's own filter, extracted. A background
// space's row must stay cold rather than open onto the wrong strip.
TEST(AtriumRestorePlanTest, ReopenRowsForActiveSpaceDropsOtherSpaces) {
  RestorePlan plan = ParseRestorePlan(R"({"activeSpace":1,"reopen":[
      {"url":"https://a.example/","space":0},
      {"url":"https://b.example/","space":1},
      {"url":"https://c.example/","space":1}]})");
  std::vector<RestoreReopenStep> rows = ReopenRowsForActiveSpace(plan);
  ASSERT_EQ(2u, rows.size());
  EXPECT_EQ("https://b.example/", rows[0].url);
  EXPECT_EQ("https://c.example/", rows[1].url);
}

TEST(AtriumRestorePlanTest, ReopenRowsForActiveSpaceCanBeEmpty) {
  RestorePlan plan = ParseRestorePlan(R"({"activeSpace":2,"reopen":[
      {"url":"https://a.example/","space":0}]})");
  EXPECT_TRUE(ReopenRowsForActiveSpace(plan).empty());
}

}  // namespace
}  // namespace atrium
