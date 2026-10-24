// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_ambilight_cadence.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

constexpr int kFastest = kAmbilightRungsMs.front();
constexpr int kSlowest = kAmbilightRungsMs.back();

TEST(AtriumAmbilightCadenceTest, AFreshCadenceRunsAtTheTopRung) {
  AmbilightCadence cadence;
  EXPECT_EQ(cadence.interval_ms(), kFastest);
}

TEST(AtriumAmbilightCadenceTest, APageThatKeepsChangingStaysAtTheTopRung) {
  AmbilightCadence cadence;
  for (int i = 0; i < kAmbilightStepAfter * 4; ++i) {
    EXPECT_EQ(cadence.OnSample(/*changed=*/true), kFastest);
  }
}

TEST(AtriumAmbilightCadenceTest, EnoughStillReadsStepTheRateDown) {
  AmbilightCadence cadence;
  for (int i = 0; i < kAmbilightStepAfter - 1; ++i) {
    EXPECT_EQ(cadence.OnSample(/*changed=*/false), kFastest)
        << "stepped down early at read " << i;
  }
  EXPECT_EQ(cadence.OnSample(/*changed=*/false), kAmbilightRungsMs[1]);
}

TEST(AtriumAmbilightCadenceTest, TheLadderStopsAtItsSlowestRung) {
  AmbilightCadence cadence;
  for (int i = 0; i < kAmbilightStepAfter * 20; ++i) {
    cadence.OnSample(/*changed=*/false);
  }
  EXPECT_EQ(cadence.interval_ms(), kSlowest);
}

// A page waking up has to be answered at full rate immediately. Climbing back
// one rung at a time would leave the glow lagging a scroll by seconds, which
// is the whole failure the backoff exists to avoid causing.
TEST(AtriumAmbilightCadenceTest, OneChangeSnapsAllTheWayBackToTheTop) {
  AmbilightCadence cadence;
  for (int i = 0; i < kAmbilightStepAfter * 20; ++i) {
    cadence.OnSample(/*changed=*/false);
  }
  ASSERT_EQ(cadence.interval_ms(), kSlowest);
  EXPECT_EQ(cadence.OnSample(/*changed=*/true), kFastest);
}

// The counter is per rung, not cumulative. Without the reset a page changing
// once every few reads still walks down the ladder and ends up sampled at the
// slowest rate while it is visibly moving.
TEST(AtriumAmbilightCadenceTest, AChangeClearsTheRunOfStillReads) {
  AmbilightCadence cadence;
  for (int round = 0; round < 5; ++round) {
    for (int i = 0; i < kAmbilightStepAfter - 1; ++i) {
      cadence.OnSample(/*changed=*/false);
    }
    EXPECT_EQ(cadence.OnSample(/*changed=*/true), kFastest)
        << "round " << round;
  }
  EXPECT_EQ(cadence.interval_ms(), kFastest);
}

TEST(AtriumAmbilightCadenceTest, ResetPutsAWalkedDownCadenceBackOnTop) {
  AmbilightCadence cadence;
  for (int i = 0; i < kAmbilightStepAfter * 20; ++i) {
    cadence.OnSample(/*changed=*/false);
  }
  cadence.Reset();
  EXPECT_EQ(cadence.interval_ms(), kFastest);
}

}  // namespace
}  // namespace atrium
