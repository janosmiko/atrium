// Copyright 2026 The Atrium Authors
#include "chrome/browser/ui/atrium/atrium_claim_ledger_decision.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

TEST(ClaimOwnerTest, SplitsAtTheFirstSlash) {
  EXPECT_EQ(ClaimOwner("win0x1/pane-active"), "win0x1");
  EXPECT_EQ(ClaimOwner("peek0x2"), "peek0x2");
}

// the bug this guards against - two tags naming the same window
// through different call sites must not read as a stale detach.
TEST(ApplyDetachTest, SameOwnerDifferentRoleIsNotStale) {
  std::vector<std::string> holders = {"win0x1/pane-active"};
  EXPECT_FALSE(ApplyDetach(holders, "win0x1/pane-heal-detach"));
  EXPECT_TRUE(holders.empty());
}

TEST(ApplyDetachTest, NoMatchingOwnerIsStale) {
  std::vector<std::string> holders = {"win0x1/pane-active"};
  EXPECT_TRUE(ApplyDetach(holders, "win0x2/pane-active"));
  EXPECT_EQ(holders.size(), 1u);
}

TEST(ApplyDetachTest, DropsOnlyTheMatchingOwner) {
  std::vector<std::string> holders = {"win0x1/pane-active",
                                       "win0x2/pane-active"};
  EXPECT_FALSE(ApplyDetach(holders, "win0x1/pane-active"));
  ASSERT_EQ(holders.size(), 1u);
  EXPECT_EQ(holders[0], "win0x2/pane-active");
}

// two different owners live-claiming the same contents is the
// real race DOUBLE-CLAIM must keep flagging.
TEST(ApplyClaimTest, DifferentOwnerAlreadyHoldingIsDoubleClaim) {
  std::vector<std::string> holders = {"win0x1/pane-active"};
  EXPECT_TRUE(ApplyClaim(holders, "win0x2/pane-active"));
}

TEST(ApplyClaimTest, SameOwnerReclaimingUnderAnotherRoleIsNotDoubleClaim) {
  std::vector<std::string> holders = {"win0x1/pane-active"};
  EXPECT_FALSE(ApplyClaim(holders, "win0x1/pane-heal-detach"));
  ASSERT_EQ(holders.size(), 1u);
  EXPECT_EQ(holders[0], "win0x1/pane-heal-detach");
}

TEST(ApplyClaimTest, FirstClaimIsNeverDoubleClaim) {
  std::vector<std::string> holders;
  EXPECT_FALSE(ApplyClaim(holders, "win0x1/pane-active"));
  ASSERT_EQ(holders.size(), 1u);
  EXPECT_EQ(holders[0], "win0x1/pane-active");
}

}  // namespace
}  // namespace atrium
