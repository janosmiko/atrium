// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_window_close_decision_unittest.cc
#include "chrome/browser/ui/atrium/atrium_window_close_decision.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

// The state that keeps a window open: an emptied space window closing on its
// own. Every case below flips exactly one field of this.
WindowCloseInputs KeptOpen() {
  WindowCloseInputs inputs;
  inputs.tab_strip_empty = true;
  inputs.space_window = true;
  return inputs;
}

TEST(AtriumWindowCloseDecisionTest, AnEmptiedSpaceWindowStaysOpen) {
  EXPECT_TRUE(ShouldKeepWindowOpen(KeptOpen()));
}

TEST(AtriumWindowCloseDecisionTest, AUserInitiatedCloseWins) {
  WindowCloseInputs inputs = KeptOpen();
  inputs.user_initiated = true;
  EXPECT_FALSE(ShouldKeepWindowOpen(inputs));
}

TEST(AtriumWindowCloseDecisionTest, AWindowWithTabsIsNeverKeptOpen) {
  WindowCloseInputs inputs = KeptOpen();
  inputs.tab_strip_empty = false;
  EXPECT_FALSE(ShouldKeepWindowOpen(inputs));
}

TEST(AtriumWindowCloseDecisionTest, ANonSpaceWindowKeepsStockCloseBehaviour) {
  WindowCloseInputs inputs = KeptOpen();
  inputs.space_window = false;
  EXPECT_FALSE(ShouldKeepWindowOpen(inputs));
}

TEST(AtriumWindowCloseDecisionTest, AQuitSweepIsNotReIntercepted) {
  WindowCloseInputs inputs = KeptOpen();
  inputs.trying_to_quit = true;
  EXPECT_FALSE(ShouldKeepWindowOpen(inputs));
}

TEST(AtriumWindowCloseDecisionTest, AnIncognitoWindowIsLetGo) {
  WindowCloseInputs inputs = KeptOpen();
  inputs.off_the_record = true;
  EXPECT_FALSE(ShouldKeepWindowOpen(inputs));
}

// Who asked is the ONLY thing separating these two, so the clause cannot be
// deleted and left passing - which the earlier version of this case allowed
// by also setting two fields that force the same answer on their own.
TEST(AtriumWindowCloseDecisionTest, UserInitiatedIsTheOnlyDifference) {
  WindowCloseInputs automatic = KeptOpen();
  WindowCloseInputs requested = KeptOpen();
  requested.user_initiated = true;
  EXPECT_NE(ShouldKeepWindowOpen(automatic), ShouldKeepWindowOpen(requested));
}

// A cancelled beforeunload prompt leaves no callback behind, so the armed flag
// can only be retired the next time a close is requested.
TEST(AtriumWindowCloseDecisionTest, ACancelledPromptRetiresTheFlag) {
  EXPECT_TRUE(UserInitiatedCloseWasAbandoned(/*deferred_on_prompt=*/true,
                                             /*attempting_to_close=*/false));
}

TEST(AtriumWindowCloseDecisionTest, ACloseStillAtItsPromptKeepsTheFlag) {
  EXPECT_FALSE(UserInitiatedCloseWasAbandoned(/*deferred_on_prompt=*/true,
                                              /*attempting_to_close=*/true));
}

// The ordinary close: no prompt, so no attempt is latched either. Reading that
// as abandoned would retire the flag mid-close and veto the user's own close.
TEST(AtriumWindowCloseDecisionTest, ACloseThatNeverPromptedKeepsTheFlag) {
  EXPECT_FALSE(UserInitiatedCloseWasAbandoned(/*deferred_on_prompt=*/false,
                                              /*attempting_to_close=*/false));
}

TEST(AtriumWindowCloseDecisionTest, AFreshAppKitCloseStartsOne) {
  EXPECT_EQ(ChooseAppKitCloseAction(/*expect_commit_close=*/false,
                                    /*close_post_pending=*/false),
            AppKitCloseAction::kStartUserInitiated);
}

TEST(AtriumWindowCloseDecisionTest, TheCommitPassIsLetThrough) {
  EXPECT_EQ(ChooseAppKitCloseAction(/*expect_commit_close=*/true,
                                    /*close_post_pending=*/false),
            AppKitCloseAction::kLetCommitThrough);
}

// A second traffic-light click while the first close is still at its prompt.
TEST(AtriumWindowCloseDecisionTest, ASecondClickQueuesNothing) {
  EXPECT_EQ(ChooseAppKitCloseAction(/*expect_commit_close=*/false,
                                    /*close_post_pending=*/true),
            AppKitCloseAction::kAlreadyStarting);
}

// Precedence, not an accident of ordering: reading a commit as a fresh close
// would post into a window that is already going away.
TEST(AtriumWindowCloseDecisionTest, TheCommitOutranksAPendingPost) {
  EXPECT_EQ(ChooseAppKitCloseAction(/*expect_commit_close=*/true,
                                    /*close_post_pending=*/true),
            AppKitCloseAction::kLetCommitThrough);
}

}  // namespace
}  // namespace atrium
