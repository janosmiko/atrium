// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_find_action_unittest.cc
#include "chrome/browser/ui/atrium/atrium_find_action.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

TEST(AtriumFindAction, OpenWithAnEmptyQueryDoesNotStart) {
  const FindRequest r = FindRequestFor(FindTrigger::kOpen, /*query_empty=*/true);
  EXPECT_FALSE(r.start);
}

// find_match is false on purpose: pulling the bar up must not scroll the page
// (find_bar_controller.cc:80-84).
TEST(AtriumFindAction, OpenWithAPrefilledQueryStartsWithoutJumping) {
  const FindRequest r =
      FindRequestFor(FindTrigger::kOpen, /*query_empty=*/false);
  EXPECT_TRUE(r.start);
  EXPECT_TRUE(r.forward);
  EXPECT_FALSE(r.find_match);
}

TEST(AtriumFindAction, QueryEmptiedStopsAndClearsTheSelection) {
  const FindRequest r =
      FindRequestFor(FindTrigger::kQueryChanged, /*query_empty=*/true);
  EXPECT_FALSE(r.start);
  EXPECT_TRUE(r.stop);
  EXPECT_FALSE(r.keep_selection);
}

TEST(AtriumFindAction, QueryChangedToNonEmptyStartsAndJumps) {
  const FindRequest r =
      FindRequestFor(FindTrigger::kQueryChanged, /*query_empty=*/false);
  EXPECT_TRUE(r.start);
  EXPECT_TRUE(r.forward);
  EXPECT_TRUE(r.find_match);
}

TEST(AtriumFindAction, NextWithAnEmptyQueryDoesNothing) {
  const FindRequest r = FindRequestFor(FindTrigger::kNext, /*query_empty=*/true);
  EXPECT_FALSE(r.start);
}

TEST(AtriumFindAction, NextWithAQueryWalksForward) {
  const FindRequest r =
      FindRequestFor(FindTrigger::kNext, /*query_empty=*/false);
  EXPECT_TRUE(r.start);
  EXPECT_TRUE(r.forward);
  EXPECT_TRUE(r.find_match);
}

TEST(AtriumFindAction, PreviousWithAnEmptyQueryDoesNothing) {
  const FindRequest r =
      FindRequestFor(FindTrigger::kPrevious, /*query_empty=*/true);
  EXPECT_FALSE(r.start);
}

TEST(AtriumFindAction, PreviousWithAQueryWalksBackward) {
  const FindRequest r =
      FindRequestFor(FindTrigger::kPrevious, /*query_empty=*/false);
  EXPECT_TRUE(r.start);
  EXPECT_FALSE(r.forward);
  EXPECT_TRUE(r.find_match);
}

TEST(AtriumFindAction, ClosingWithAnyQueryStopsAndKeepsTheSelection) {
  const FindRequest r =
      FindRequestFor(FindTrigger::kClose, /*query_empty=*/true);
  EXPECT_FALSE(r.start);
  EXPECT_TRUE(r.stop);
  EXPECT_TRUE(r.keep_selection);
}

// Escape keeps the match as an ordinary selection. StopFinding tells the
// renderer to abort scoping either way, so the highlights go regardless.
TEST(AtriumFindAction, ClosingKeepsTheSelection) {
  const atrium::FindRequest r =
      atrium::FindRequestFor(atrium::FindTrigger::kClose, false);
  EXPECT_TRUE(r.stop);
  EXPECT_TRUE(r.keep_selection);
}

// An emptied field clears the selection too, so the page is left exactly as
// it was before the search.
TEST(AtriumFindAction, AnEmptiedQueryClearsTheSelection) {
  const atrium::FindRequest r =
      atrium::FindRequestFor(atrium::FindTrigger::kQueryChanged, true);
  EXPECT_TRUE(r.stop);
  EXPECT_FALSE(r.keep_selection);
}

}  // namespace
}  // namespace atrium
