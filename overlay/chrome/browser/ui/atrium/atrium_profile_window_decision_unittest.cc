// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_profile_window_decision_unittest.cc
#include "chrome/browser/ui/atrium/atrium_profile_window_decision.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

ProfileWindowCandidate Usable(const std::string& id) {
  return {.profile_id = id, .realised = true, .little = false, .shown = true};
}

TEST(AtriumProfileWindowDecisionTest, NoWindowsAtAllAsksForANewOne) {
  EXPECT_EQ(ChooseProfileWindow("p1", {}), -1);
}

// The list arrives in activation order, so index 0 IS the last active window.
TEST(AtriumProfileWindowDecisionTest, TheLastActiveWindowOnThatProfileWins) {
  std::vector<ProfileWindowCandidate> candidates = {Usable("p1"), Usable("p1")};
  EXPECT_EQ(ChooseProfileWindow("p1", candidates), 0);
}

TEST(AtriumProfileWindowDecisionTest, AWindowOnAnotherProfileIsSkipped) {
  std::vector<ProfileWindowCandidate> candidates = {Usable("p2"), Usable("p1")};
  EXPECT_EQ(ChooseProfileWindow("p1", candidates), 1);
}

// A Browser with no native window aborts the process when a tab is added to it
//, so a hidden bootstrap controller is never a target.
TEST(AtriumProfileWindowDecisionTest, AnUnrealisedWindowIsNeverATarget) {
  std::vector<ProfileWindowCandidate> candidates = {
      {.profile_id = "p1", .realised = false, .little = false},
      Usable("p1"),
  };
  EXPECT_EQ(ChooseProfileWindow("p1", candidates), 1);
}

// A little window is chromeless and single-tab. A settings page does not go there.
TEST(AtriumProfileWindowDecisionTest, ALittleWindowIsNeverATarget) {
  std::vector<ProfileWindowCandidate> candidates = {
      {.profile_id = "p1", .realised = true, .little = true, .shown = true},
      Usable("p1"),
  };
  EXPECT_EQ(ChooseProfileWindow("p1", candidates), 1);
}

// A hidden bootstrap placeholder has a realised, non-little window - only
// `shown` keeps a data link from silently landing a tab where no one sees it.
TEST(AtriumProfileWindowDecisionTest, AnUnshownWindowIsNeverATarget) {
  std::vector<ProfileWindowCandidate> candidates = {
      {.profile_id = "p1", .realised = true, .little = false, .shown = false},
      Usable("p1"),
  };
  EXPECT_EQ(ChooseProfileWindow("p1", candidates), 1);
}

TEST(AtriumProfileWindowDecisionTest,
     OnlyUnusableWindowsOnThatProfileAskForANewOne) {
  std::vector<ProfileWindowCandidate> candidates = {
      {.profile_id = "p1", .realised = true, .little = true},
      {.profile_id = "p1", .realised = false, .little = false},
      Usable("p2"),
  };
  EXPECT_EQ(ChooseProfileWindow("p1", candidates), -1);
}

// The candidate carries an empty id too. Against a named one the comparison is
// false whether the guard is there or not, so the guard goes untested and a
// data link for no profile could open in somebody's window.
TEST(AtriumProfileWindowDecisionTest, AnEmptyProfileIdMatchesNothing) {
  std::vector<ProfileWindowCandidate> candidates = {
      Usable("p1"),
      {.profile_id = "", .realised = true, .little = false},
  };
  EXPECT_EQ(ChooseProfileWindow("", candidates), -1);
}

}  // namespace
}  // namespace atrium
