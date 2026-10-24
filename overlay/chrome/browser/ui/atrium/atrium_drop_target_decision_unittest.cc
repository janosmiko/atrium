// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_drop_target_decision_unittest.cc
#include "chrome/browser/ui/atrium/atrium_drop_target_decision.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

namespace atrium {
namespace {

TEST(AtriumDropTargetDecisionTest, NoCandidatesYieldsNoMatch) {
  DropTargetDecision decision = ChooseDropTarget(gfx::Point(10, 10), {});
  EXPECT_EQ(decision.kind, DropTargetKind::kNoMatch);
  EXPECT_EQ(decision.index, -1);
}

TEST(AtriumDropTargetDecisionTest, PointOutsideEveryWindowYieldsNoMatch) {
  std::vector<DropTargetCandidate> candidates = {
      {.window_bounds = gfx::Rect(0, 0, 800, 600),
       .sidebar_bounds = gfx::Rect(0, 0, 220, 600),
       .has_sidebar = true},
  };
  DropTargetDecision decision = ChooseDropTarget(gfx::Point(900, 900),
                                                  candidates);
  EXPECT_EQ(decision.kind, DropTargetKind::kNoMatch);
  EXPECT_EQ(decision.index, -1);
}

TEST(AtriumDropTargetDecisionTest, PointInsideSidebarRectYieldsSidebar) {
  std::vector<DropTargetCandidate> candidates = {
      {.window_bounds = gfx::Rect(0, 0, 800, 600),
       .sidebar_bounds = gfx::Rect(0, 0, 220, 600),
       .has_sidebar = true},
  };
  DropTargetDecision decision = ChooseDropTarget(gfx::Point(100, 100),
                                                  candidates);
  EXPECT_EQ(decision.kind, DropTargetKind::kSidebar);
  EXPECT_EQ(decision.index, 0);
}

TEST(AtriumDropTargetDecisionTest, PointInsideContentRectYieldsContent) {
  std::vector<DropTargetCandidate> candidates = {
      {.window_bounds = gfx::Rect(0, 0, 800, 600),
       .sidebar_bounds = gfx::Rect(0, 0, 220, 600),
       .has_sidebar = true},
  };
  DropTargetDecision decision = ChooseDropTarget(gfx::Point(500, 300),
                                                  candidates);
  EXPECT_EQ(decision.kind, DropTargetKind::kContent);
  EXPECT_EQ(decision.index, 0);
}

// A little window has no
// sidebar, so a point over its bounds - even where a sidebar rect would
// have been - lands in kContent, never kSidebar.
TEST(AtriumDropTargetDecisionTest, LittleWindowHasNoSidebarBranch) {
  std::vector<DropTargetCandidate> candidates = {
      {.window_bounds = gfx::Rect(0, 0, 800, 600),
       .sidebar_bounds = gfx::Rect(0, 0, 220, 600),
       .has_sidebar = false},
  };
  DropTargetDecision decision = ChooseDropTarget(gfx::Point(100, 100),
                                                  candidates);
  EXPECT_EQ(decision.kind, DropTargetKind::kContent);
  EXPECT_EQ(decision.index, 0);
}

TEST(AtriumDropTargetDecisionTest, FirstMatchingCandidateWinsOverlap) {
  std::vector<DropTargetCandidate> candidates = {
      {.window_bounds = gfx::Rect(0, 0, 800, 600),
       .sidebar_bounds = gfx::Rect(0, 0, 220, 600),
       .has_sidebar = true},
      {.window_bounds = gfx::Rect(400, 0, 800, 600),
       .sidebar_bounds = gfx::Rect(400, 0, 220, 600),
       .has_sidebar = true},
  };
  DropTargetDecision decision = ChooseDropTarget(gfx::Point(500, 300),
                                                  candidates);
  EXPECT_EQ(decision.kind, DropTargetKind::kContent);
  EXPECT_EQ(decision.index, 0);
}

TEST(AtriumDropTargetDecisionTest, SecondCandidateMatchesWhenFirstDoesNot) {
  std::vector<DropTargetCandidate> candidates = {
      {.window_bounds = gfx::Rect(0, 0, 800, 600),
       .sidebar_bounds = gfx::Rect(0, 0, 220, 600),
       .has_sidebar = true},
      {.window_bounds = gfx::Rect(1000, 0, 800, 600),
       .sidebar_bounds = gfx::Rect(1000, 0, 220, 600),
       .has_sidebar = true},
  };
  DropTargetDecision decision = ChooseDropTarget(gfx::Point(1050, 300),
                                                  candidates);
  EXPECT_EQ(decision.kind, DropTargetKind::kSidebar);
  EXPECT_EQ(decision.index, 1);
}

}  // namespace
}  // namespace atrium
