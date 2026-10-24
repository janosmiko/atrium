// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/extension_popup_watchdog_policy_unittest.cc
#include "chrome/browser/ui/atrium/extension_popup_watchdog_policy.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

class ExtensionPopupWatchdogPolicyTest : public testing::Test {
 protected:
  ExtensionPopupWatchdogPolicy policy_;
};

TEST_F(ExtensionPopupWatchdogPolicyTest, StartsIdleAndUnarmed) {
  EXPECT_FALSE(policy_.is_armed());
}

TEST_F(ExtensionPopupWatchdogPolicyTest, ArmSetsArmed) {
  policy_.Arm();
  EXPECT_TRUE(policy_.is_armed());
}

// The happy path: the popup shows before the watchdog fires. OnShown should
// report true (tell the caller to stop the timer) and disarm.
TEST_F(ExtensionPopupWatchdogPolicyTest, ShownBeforeTimeoutCancelsWatchdog) {
  policy_.Arm();

  EXPECT_TRUE(policy_.OnShown());
  EXPECT_FALSE(policy_.is_armed());

  // A timer fire arriving after the popup already shown (e.g. a race the
  // caller failed to stop in time) must not be treated as a hang.
  EXPECT_FALSE(policy_.OnTimerFired());
}

// The hang path (design spec section 9): the popup never shows and the
// watchdog fires. OnTimerFired should report true (reclaim) exactly once.
TEST_F(ExtensionPopupWatchdogPolicyTest, TimeoutBeforeShownReportsHang) {
  policy_.Arm();

  EXPECT_TRUE(policy_.OnTimerFired());
  EXPECT_FALSE(policy_.is_armed());

  // The timer is one-shot in production, but a defensive double-fire must
  // not double-report.
  EXPECT_FALSE(policy_.OnTimerFired());
}

// The popup closes itself (e.g. extension unload) before ever showing.
// OnShowFailed disarms without reporting a hang.
TEST_F(ExtensionPopupWatchdogPolicyTest, ShowFailedDisarmsWithoutHangReport) {
  policy_.Arm();

  policy_.OnShowFailed();

  EXPECT_FALSE(policy_.is_armed());
  EXPECT_FALSE(policy_.OnTimerFired());
}

// OnShown/OnTimerFired called without a prior Arm() (never opened, or
// already resolved) are no-ops, not false-positive reports.
TEST_F(ExtensionPopupWatchdogPolicyTest, SignalsWithoutArmAreNoOps) {
  EXPECT_FALSE(policy_.OnShown());
  EXPECT_FALSE(policy_.OnTimerFired());
  EXPECT_FALSE(policy_.is_armed());
}

// Reset unconditionally returns to idle from any phase - the host calls this
// on every Close()/CloseNow(), including a popup that never armed.
TEST_F(ExtensionPopupWatchdogPolicyTest, ResetReturnsToIdleFromAnyPhase) {
  policy_.Reset();
  EXPECT_FALSE(policy_.is_armed());

  policy_.Arm();
  policy_.Reset();
  EXPECT_FALSE(policy_.is_armed());
  EXPECT_FALSE(policy_.OnTimerFired());

  policy_.Arm();
  policy_.OnShown();
  policy_.Reset();
  EXPECT_FALSE(policy_.is_armed());
}

// A second Open() re-arms after a prior successful show - the policy must
// not remember the earlier kShown state as sticky.
TEST_F(ExtensionPopupWatchdogPolicyTest, RearmsAfterPriorShownCycle) {
  policy_.Arm();
  policy_.OnShown();

  policy_.Arm();

  EXPECT_TRUE(policy_.is_armed());
  EXPECT_TRUE(policy_.OnTimerFired());
}

}  // namespace
}  // namespace atrium
