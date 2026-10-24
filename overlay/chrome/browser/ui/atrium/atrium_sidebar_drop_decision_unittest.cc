// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_sidebar_drop_decision.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

TEST(AtriumSidebarDropDecisionTest, SameWorkspaceMovesSilently) {
  EXPECT_EQ(SidebarDropAction::kMoveSilently,
            ChooseSidebarDropAction("space-a", "space-a"));
}

TEST(AtriumSidebarDropDecisionTest, DifferentWorkspaceAsksFirst) {
  EXPECT_EQ(SidebarDropAction::kAskBeforeMoving,
            ChooseSidebarDropAction("space-a", "space-b"));
}

}  // namespace
}  // namespace atrium
