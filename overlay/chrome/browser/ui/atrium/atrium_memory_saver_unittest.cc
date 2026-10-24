// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_memory_saver_unittest.cc
#include "chrome/browser/ui/atrium/atrium_memory_saver.h"

#include "base/test/task_environment.h"
#include "chrome/test/base/testing_browser_process.h"
#include "components/performance_manager/public/user_tuning/prefs.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

TEST(AtriumMemorySaverTest, EnabledIsReadBothWaysRound) {
  EXPECT_EQ(MemorySaverEnabledFromSettings(R"({"memorySaverEnabled":true})"),
            std::make_optional(true));
  EXPECT_EQ(MemorySaverEnabledFromSettings(R"({"memorySaverEnabled":false})"),
            std::make_optional(false));
}

TEST(AtriumMemorySaverTest, EnabledAbsentFieldIsNotFalse) {
  EXPECT_EQ(MemorySaverEnabledFromSettings(R"({"glassOpacity":0.55})"),
            std::nullopt);
}

TEST(AtriumMemorySaverTest, EnabledUnreadableYieldsNoAnswer) {
  EXPECT_EQ(MemorySaverEnabledFromSettings(""), std::nullopt);
  EXPECT_EQ(MemorySaverEnabledFromSettings("not json"), std::nullopt);
  EXPECT_EQ(MemorySaverEnabledFromSettings("[1,2,3]"), std::nullopt);
}

TEST(AtriumMemorySaverTest, EnabledWrongTypeIsNotAnAnswer) {
  EXPECT_EQ(MemorySaverEnabledFromSettings(R"({"memorySaverEnabled":"true"})"),
            std::nullopt);
  EXPECT_EQ(MemorySaverEnabledFromSettings(R"({"memorySaverEnabled":1})"),
            std::nullopt);
}

// The ladder rust/atrium-core/src/settings.rs's MEMORY_SAVER_THRESHOLDS
// carries, each converted to the minutes the Chromium pref stores.
TEST(AtriumMemorySaverTest, ThresholdLadderConvertsToMinutes) {
  EXPECT_EQ(MemorySaverThresholdMinutesFromSettings(
                R"({"memorySaverThreshold":"fifteenMinutes"})"),
            std::make_optional(15));
  EXPECT_EQ(MemorySaverThresholdMinutesFromSettings(
                R"({"memorySaverThreshold":"thirtyMinutes"})"),
            std::make_optional(30));
  EXPECT_EQ(MemorySaverThresholdMinutesFromSettings(
                R"({"memorySaverThreshold":"oneHour"})"),
            std::make_optional(60));
  EXPECT_EQ(MemorySaverThresholdMinutesFromSettings(
                R"({"memorySaverThreshold":"twoHours"})"),
            std::make_optional(120));
  EXPECT_EQ(MemorySaverThresholdMinutesFromSettings(
                R"({"memorySaverThreshold":"fourHours"})"),
            std::make_optional(240));
}

// "never" is the retired spelling (rust/atrium-core/src/settings.rs's
// RETIRED_MEMORY_SAVER_NEVER); it must not resolve to a minute count.
TEST(AtriumMemorySaverTest, ThresholdRejectsRetiredAndUnknownSpellings) {
  EXPECT_EQ(MemorySaverThresholdMinutesFromSettings(
                R"({"memorySaverThreshold":"never"})"),
            std::nullopt);
  EXPECT_EQ(MemorySaverThresholdMinutesFromSettings(
                R"({"memorySaverThreshold":"whenever"})"),
            std::nullopt);
}

TEST(AtriumMemorySaverTest, ThresholdAbsentOrUnreadableYieldsNoAnswer) {
  EXPECT_EQ(MemorySaverThresholdMinutesFromSettings(R"({"glassOpacity":0.55})"),
            std::nullopt);
  EXPECT_EQ(MemorySaverThresholdMinutesFromSettings(""), std::nullopt);
  EXPECT_EQ(MemorySaverThresholdMinutesFromSettings("not json"), std::nullopt);
}

// Asserts the pref MemorySaverModePolicy actually reads, not a parsed field.
class AtriumMemorySaverPrefTest : public ::testing::Test {
 public:
  PrefService* local_state() {
    return TestingBrowserProcess::GetGlobal()->local_state();
  }

 private:
  content::BrowserTaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::UI};
};

TEST_F(AtriumMemorySaverPrefTest, EnabledSetsTheChromiumPref) {
  using performance_manager::user_tuning::prefs::kMemorySaverModeState;
  using performance_manager::user_tuning::prefs::MemorySaverModeState;

  SetMemorySaverEnabledPref(true);
  EXPECT_EQ(local_state()->GetInteger(kMemorySaverModeState),
            static_cast<int>(MemorySaverModeState::kEnabled));

  SetMemorySaverEnabledPref(false);
  EXPECT_EQ(local_state()->GetInteger(kMemorySaverModeState),
            static_cast<int>(MemorySaverModeState::kDisabled));
}

TEST_F(AtriumMemorySaverPrefTest, ThresholdSetsTheChromiumPref) {
  using performance_manager::user_tuning::prefs::
      kMemorySaverModeTimeBeforeDiscardInMinutes;

  SetMemorySaverThresholdMinutesPref(120);
  EXPECT_EQ(
      local_state()->GetInteger(kMemorySaverModeTimeBeforeDiscardInMinutes),
      120);

  SetMemorySaverThresholdMinutesPref(15);
  EXPECT_EQ(
      local_state()->GetInteger(kMemorySaverModeTimeBeforeDiscardInMinutes),
      15);
}

}  // namespace
}  // namespace atrium
