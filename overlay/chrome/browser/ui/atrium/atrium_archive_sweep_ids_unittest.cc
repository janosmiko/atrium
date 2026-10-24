// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Proves SweepArchivedTabIds names exactly what a sweep moved and stays
// silent when nothing was due.

#include "chrome/browser/ui/atrium/atrium_main_extra_parts.h"

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/test/bind.h"
#include "base/threading/thread_restrictions.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"

namespace atrium {
namespace {

constexpr double kMigrationInstant = 788918400.0;

class ArchiveSweepIdsTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    path_ = dir_.GetPath().AppendASCII("spaces.json");
  }

  void TearDown() override { atrium_core_close(); }

  void OpenWith(const char* json) {
    {
      base::ScopedAllowBlockingForTesting allow_blocking;
      ASSERT_TRUE(base::WriteFile(path_, json));
    }
    atrium_core_close();
    ASSERT_TRUE(atrium_core_open(path_.value().c_str(), kMigrationInstant));
  }

  base::ScopedTempDir dir_;
  base::FilePath path_;
};

TEST_F(ArchiveSweepIdsTest, NamesExactlyTheRowsASweepMoved) {
  // A row already archived before this tick - the diff must exclude it, not
  // just report everything found afterward.
  OpenWith(R"({"spaces":[{"id":"a","name":"Work","emoji":"W",
      "tint":"#111111","profileID":"p","pinnedTabs":[],
      "tabs":[{"url":"https://example.com/due","title":"Due"},
              {"url":"https://example.com/live","title":"Live",
               "lastActiveAt":49000}],
      "order":0,"groups":[]}],
      "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":0,
      "sidebarWidthMigrated":true},
      "archived":[{"id":"already-archived","url":"https://example.com/old",
      "title":"Old","spaceID":"a","archivedAt":10}]})");

  std::vector<std::string> swept =
      SweepArchivedTabIds("twelveHours", 50000.0);

  ASSERT_EQ(swept.size(), 1u);
  EXPECT_NE(swept[0], "already-archived");
  char* raw = atrium_core_archived_overview();
  ASSERT_TRUE(raw);
  std::string archived(raw);
  atrium_core_string_free(raw);
  EXPECT_NE(archived.find(swept[0]), std::string::npos) << archived;
  EXPECT_NE(archived.find("https://example.com/due"), std::string::npos)
      << archived;
}

TEST_F(ArchiveSweepIdsTest, IsSilentWhenNothingWasDue) {
  OpenWith(R"({"spaces":[{"id":"a","name":"Work","emoji":"W",
      "tint":"#111111","profileID":"p","pinnedTabs":[],
      "tabs":[{"url":"https://example.com/live","title":"Live"}],
      "order":0,"groups":[]}],
      "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":0,
      "sidebarWidthMigrated":true}})");

  EXPECT_TRUE(SweepArchivedTabIds("twelveHours", 0.0).empty());
}

// the fly animation clones a still-present row, so the notify
// must land before the snapshot push rebuilds it away - order is the point.
TEST(ArchiveSweepNotifyOrderTest, NotifiesWithSweptIdsBeforePushingSnapshot) {
  std::vector<std::string> log;
  std::vector<std::string> notified_ids;
  NotifyThenPushArchiveSweep(
      {"a", "b"},
      base::BindLambdaForTesting([&](const std::vector<std::string>& ids) {
        notified_ids = ids;
        log.push_back("notify");
      }),
      base::BindLambdaForTesting([&] { log.push_back("push"); }));

  EXPECT_EQ(notified_ids, (std::vector<std::string>{"a", "b"}));
  ASSERT_EQ(log.size(), 2u);
  EXPECT_EQ(log[0], "notify");
  EXPECT_EQ(log[1], "push");
}

TEST(ArchiveSweepNotifyOrderTest, SkipsNotifyWhenNothingSwept) {
  std::vector<std::string> log;
  NotifyThenPushArchiveSweep(
      {},
      base::BindLambdaForTesting(
          [&](const std::vector<std::string>&) { log.push_back("notify"); }),
      base::BindLambdaForTesting([&] { log.push_back("push"); }));

  EXPECT_EQ(log, (std::vector<std::string>{"push"}));
}

}  // namespace
}  // namespace atrium
