// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_sidebar_move_execution_decision.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

TEST(AtriumSidebarMoveExecutionDecisionTest, SameProfileMovesLive) {
  EXPECT_EQ(SidebarMoveExecution::kLiveMove,
            ChooseSidebarMoveExecution("Default", "Default"));
}

TEST(AtriumSidebarMoveExecutionDecisionTest, DifferentProfileReopens) {
  EXPECT_EQ(SidebarMoveExecution::kReopenAndClose,
            ChooseSidebarMoveExecution("Default", "Profile 1"));
}

}  // namespace
}  // namespace atrium
