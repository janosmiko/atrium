// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_theme_animator.h"

#include <vector>

#include "base/functional/bind.h"
#include "base/test/simple_test_clock.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"

namespace atrium {
namespace {

struct TickCall {
  double ambient_time;
  double warmth;
};

double LocalHourOf(base::Time time) {
  base::Time::Exploded exploded;
  time.LocalExplode(&exploded);
  return exploded.hour + exploded.minute / 60.0 + exploded.second / 3600.0;
}

class ThemeAnimatorTest : public testing::Test {
 protected:
  void OnTick(double ambient_time, double warmth) {
    ticks_.push_back({ambient_time, warmth});
  }

  base::test::TaskEnvironment env_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  std::vector<TickCall> ticks_;
  ThemeAnimator animator_{base::BindRepeating(&ThemeAnimatorTest::OnTick,
                                              base::Unretained(this))};
};

TEST_F(ThemeAnimatorTest, NeitherFlagIsNotRunning) {
  animator_.Configure(/*night_shift=*/false, /*ambient=*/false,
                      /*reduced_motion=*/false);
  EXPECT_FALSE(animator_.running());
  env_.FastForwardBy(base::Minutes(5));
  EXPECT_TRUE(ticks_.empty());
}

TEST_F(ThemeAnimatorTest, AmbientTicksThirtyTimesASecond) {
  animator_.Configure(/*night_shift=*/false, /*ambient=*/true,
                      /*reduced_motion=*/false);
  ASSERT_TRUE(animator_.running());
  env_.FastForwardBy(base::Seconds(1));
  EXPECT_EQ(ticks_.size(), 30u);
  for (const TickCall& tick : ticks_) {
    EXPECT_GE(tick.ambient_time, 0.0);
  }
}

TEST_F(ThemeAnimatorTest, NightShiftAloneTicksOncePerMinuteWithItsWarmth) {
  base::SimpleTestClock clock;
  const base::Time now = base::Time::Now();
  clock.SetNow(now);
  animator_.set_clock_for_testing(&clock);

  animator_.Configure(/*night_shift=*/true, /*ambient=*/false,
                      /*reduced_motion=*/false);
  ASSERT_TRUE(animator_.running());
  env_.FastForwardBy(base::Minutes(3));
  ASSERT_EQ(ticks_.size(), 3u);

  const double expected_warmth = atrium_core_night_shift_offset(LocalHourOf(now));
  for (const TickCall& tick : ticks_) {
    EXPECT_EQ(tick.ambient_time, -1.0);
    EXPECT_EQ(tick.warmth, expected_warmth);
  }
}

TEST_F(ThemeAnimatorTest, NightShiftWarmthChangesAcrossAClockBoundary) {
  base::SimpleTestClock clock;
  const base::Time noon = base::Time::Now();
  clock.SetNow(noon);
  animator_.set_clock_for_testing(&clock);

  animator_.Configure(/*night_shift=*/true, /*ambient=*/false,
                      /*reduced_motion=*/false);
  ASSERT_TRUE(animator_.running());
  env_.FastForwardBy(base::Minutes(1));
  ASSERT_EQ(ticks_.size(), 1u);
  const double warmth_at_noon = ticks_.back().warmth;
  EXPECT_EQ(warmth_at_noon, atrium_core_night_shift_offset(LocalHourOf(noon)));

  // Noon to 02:00's span, wherever the host's own clock happens to start.
  const base::Time two_am = noon + base::Hours(14);
  clock.SetNow(two_am);
  env_.FastForwardBy(base::Minutes(1));
  ASSERT_EQ(ticks_.size(), 2u);
  const double warmth_at_two_am = ticks_.back().warmth;
  EXPECT_EQ(warmth_at_two_am,
           atrium_core_night_shift_offset(LocalHourOf(two_am)));
  EXPECT_NE(warmth_at_noon, warmth_at_two_am)
      << "atrium_core_night_shift_offset gave the same warmth 14 hours apart";
}

TEST_F(ThemeAnimatorTest, ReducedMotionWithAmbientOnBehavesLikeNightShiftOnly) {
  base::SimpleTestClock clock;
  const base::Time now = base::Time::Now();
  clock.SetNow(now);
  animator_.set_clock_for_testing(&clock);

  animator_.Configure(/*night_shift=*/true, /*ambient=*/true,
                      /*reduced_motion=*/true);
  ASSERT_TRUE(animator_.running());
  env_.FastForwardBy(base::Minutes(1));
  ASSERT_EQ(ticks_.size(), 1u);
  EXPECT_EQ(ticks_[0].ambient_time, -1.0);
  EXPECT_EQ(ticks_[0].warmth, atrium_core_night_shift_offset(LocalHourOf(now)));
}

TEST_F(ThemeAnimatorTest, HidingAndShowingDoesNotRewindTheAmbientPhase) {
  animator_.Configure(/*night_shift=*/false, /*ambient=*/true,
                      /*reduced_motion=*/false);
  ASSERT_TRUE(animator_.running());
  env_.FastForwardBy(base::Seconds(1));
  ASSERT_FALSE(ticks_.empty());
  const double before_hide = ticks_.back().ambient_time;

  animator_.SetVisible(false);
  animator_.SetVisible(true);
  ticks_.clear();
  env_.FastForwardBy(base::Hertz(30));
  ASSERT_FALSE(ticks_.empty());
  EXPECT_GT(ticks_.back().ambient_time, before_hide)
      << "a hide/show cycle rewound the ambient phase";
}

TEST_F(ThemeAnimatorTest, ResetPhaseRestartsTheAmbientClockAtZero) {
  animator_.Configure(/*night_shift=*/false, /*ambient=*/true,
                      /*reduced_motion=*/false);
  env_.FastForwardBy(base::Seconds(1));
  ASSERT_FALSE(ticks_.empty());
  ASSERT_GT(ticks_.back().ambient_time, 0.5);

  animator_.ResetPhase();
  ticks_.clear();
  env_.FastForwardBy(base::Hertz(30));
  ASSERT_FALSE(ticks_.empty());
  EXPECT_LT(ticks_.back().ambient_time, 0.1);
}

TEST_F(ThemeAnimatorTest, ARepeatConfigureLeavesTheTimerOnItsOwnSchedule) {
  animator_.Configure(/*night_shift=*/false, /*ambient=*/true,
                      /*reduced_motion=*/false);
  ASSERT_TRUE(animator_.running());
  // Two thirds into the 33ms frame, so a restart here would push the next
  // tick past the moment the original schedule fires at.
  env_.FastForwardBy(base::Milliseconds(22));
  ASSERT_TRUE(ticks_.empty());

  animator_.Configure(/*night_shift=*/false, /*ambient=*/true,
                      /*reduced_motion=*/false);
  env_.FastForwardBy(base::Milliseconds(12));
  EXPECT_EQ(ticks_.size(), 1u)
      << "a repeat Configure re-armed the 30fps timer, so the tick it was "
         "already due never fired";
}

TEST_F(ThemeAnimatorTest, ARepeatSetVisibleLeavesTheTimerOnItsOwnSchedule) {
  animator_.Configure(/*night_shift=*/false, /*ambient=*/true,
                      /*reduced_motion=*/false);
  ASSERT_TRUE(animator_.running());
  env_.FastForwardBy(base::Milliseconds(22));
  ASSERT_TRUE(ticks_.empty());

  animator_.SetVisible(true);
  env_.FastForwardBy(base::Milliseconds(12));
  EXPECT_EQ(ticks_.size(), 1u)
      << "a repeat SetVisible re-armed the 30fps timer - ApplyThemeSpec calls "
         "it on every preview frame";
}

TEST_F(ThemeAnimatorTest, SetVisibleFalseStopsTicks) {
  animator_.Configure(/*night_shift=*/false, /*ambient=*/true,
                      /*reduced_motion=*/false);
  ASSERT_TRUE(animator_.running());
  animator_.SetVisible(false);
  EXPECT_FALSE(animator_.running());
  ticks_.clear();
  env_.FastForwardBy(base::Seconds(1));
  EXPECT_TRUE(ticks_.empty());
}

}  // namespace
}  // namespace atrium
