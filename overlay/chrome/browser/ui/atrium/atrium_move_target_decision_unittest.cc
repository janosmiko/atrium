// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_move_target_decision_unittest.cc
#include "chrome/browser/ui/atrium/atrium_move_target_decision.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

TEST(AtriumMoveTargetDecisionTest, NoCandidatesYieldsNone) {
  MoveTargetDecision decision = ChooseMoveTarget("p1", {});
  EXPECT_EQ(decision.kind, MoveTargetKind::kNone);
  EXPECT_EQ(decision.index, -1);
}

TEST(AtriumMoveTargetDecisionTest, AllLittleCandidatesYieldNone) {
  std::vector<MoveTargetCandidate> candidates = {
      {.profile_id = "p1", .is_little = true},
      {.profile_id = "p2", .is_little = true},
  };
  MoveTargetDecision decision = ChooseMoveTarget("p1", candidates);
  EXPECT_EQ(decision.kind, MoveTargetKind::kNone);
  EXPECT_EQ(decision.index, -1);
}

TEST(AtriumMoveTargetDecisionTest, SameProfileMatchWins) {
  std::vector<MoveTargetCandidate> candidates = {
      {.profile_id = "p1", .is_little = false},
  };
  MoveTargetDecision decision = ChooseMoveTarget("p1", candidates);
  EXPECT_EQ(decision.kind, MoveTargetKind::kSameProfile);
  EXPECT_EQ(decision.index, 0);
}

TEST(AtriumMoveTargetDecisionTest, NoSameProfileFallsBackToFirstAnyCandidate) {
  std::vector<MoveTargetCandidate> candidates = {
      {.profile_id = "p2", .is_little = false},
      {.profile_id = "p3", .is_little = false},
  };
  MoveTargetDecision decision = ChooseMoveTarget("p1", candidates);
  EXPECT_EQ(decision.kind, MoveTargetKind::kCrossProfile);
  EXPECT_EQ(decision.index, 0);
}

TEST(AtriumMoveTargetDecisionTest,
     SameProfileMatchWinsEvenWhenSeenAfterAnAnyProfileCandidate) {
  std::vector<MoveTargetCandidate> candidates = {
      {.profile_id = "p2", .is_little = false},  // any_target candidate first
      {.profile_id = "p1", .is_little = false},  // same-profile match second
  };
  MoveTargetDecision decision = ChooseMoveTarget("p1", candidates);
  EXPECT_EQ(decision.kind, MoveTargetKind::kSameProfile);
  EXPECT_EQ(decision.index, 1);
}

TEST(AtriumMoveTargetDecisionTest, LittleCandidatesAreSkippedWhenPickingAny) {
  std::vector<MoveTargetCandidate> candidates = {
      {.profile_id = "p2", .is_little = true},   // skipped - little
      {.profile_id = "p3", .is_little = false},  // this one is the real any
  };
  MoveTargetDecision decision = ChooseMoveTarget("p1", candidates);
  EXPECT_EQ(decision.kind, MoveTargetKind::kCrossProfile);
  EXPECT_EQ(decision.index, 1);
}

TEST(AtriumMoveTargetDecisionTest,
     LittleCandidateSharingCurrentProfileIsNotASameProfileMatch) {
  std::vector<MoveTargetCandidate> candidates = {
      {.profile_id = "p1", .is_little = true},   // same profile but little -
                                                  // must not be chosen
      {.profile_id = "p4", .is_little = false},  // real cross-profile fallback
  };
  MoveTargetDecision decision = ChooseMoveTarget("p1", candidates);
  EXPECT_EQ(decision.kind, MoveTargetKind::kCrossProfile);
  EXPECT_EQ(decision.index, 1);
}

}  // namespace
}  // namespace atrium
