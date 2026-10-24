// Copyright 2026 The Atrium Authors
#include "chrome/browser/ui/atrium/atrium_downloads_overview.h"

#include "base/files/file_path.h"
#include "base/functional/bind.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/time/time.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "components/download/public/common/mock_download_item.h"
#include "content/public/browser/download_item_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/mock_download_manager.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using ::testing::NiceMock;
using ::testing::Return;
using ::testing::ReturnRefOfCopy;

namespace atrium {
namespace {

void ConfigureItem(NiceMock<download::MockDownloadItem>& item,
                   const std::string& guid,
                   const std::string& filename,
                   download::DownloadItem::DownloadState state,
                   bool transient) {
  ON_CALL(item, GetGuid()).WillByDefault(ReturnRefOfCopy(guid));
  ON_CALL(item, GetTargetFilePath())
      .WillByDefault(ReturnRefOfCopy(base::FilePath::FromASCII(filename)));
  ON_CALL(item, GetState()).WillByDefault(Return(state));
  ON_CALL(item, IsTransient()).WillByDefault(Return(transient));
  ON_CALL(item, PercentComplete()).WillByDefault(Return(42));
  ON_CALL(item, GetReceivedBytes()).WillByDefault(Return(1024));
  ON_CALL(item, GetTotalBytes()).WillByDefault(Return(2048));
  ON_CALL(item, GetStartTime()).WillByDefault(Return(base::Time::Now()));
  ON_CALL(item, GetEndTime()).WillByDefault(Return(base::Time::Now()));
}

TEST(AtriumDownloadsOverviewTest, ExcludesCancelledAndTransientDownloads) {
  NiceMock<download::MockDownloadItem> complete;
  ConfigureItem(complete, "g1", "a.pdf", download::DownloadItem::COMPLETE,
               /*transient=*/false);
  NiceMock<download::MockDownloadItem> cancelled;
  ConfigureItem(cancelled, "g2", "b.pdf", download::DownloadItem::CANCELLED,
               /*transient=*/false);
  NiceMock<download::MockDownloadItem> transient;
  ConfigureItem(transient, "g3", "c.pdf", download::DownloadItem::COMPLETE,
               /*transient=*/true);

  EXPECT_TRUE(ShouldIncludeInDownloadsOverview(&complete));
  EXPECT_FALSE(ShouldIncludeInDownloadsOverview(&cancelled));
  EXPECT_FALSE(ShouldIncludeInDownloadsOverview(&transient));
  EXPECT_FALSE(ShouldIncludeInDownloadsOverview(nullptr));
}

TEST(AtriumDownloadsOverviewTest, DictCarriesNameAndCompletedSizeLabel) {
  NiceMock<download::MockDownloadItem> item;
  ConfigureItem(item, "g1", "report.pdf", download::DownloadItem::COMPLETE,
               /*transient=*/false);
  base::DictValue dict = DownloadItemToOverviewDict(&item);

  EXPECT_EQ(*dict.FindString("id"), "g1");
  EXPECT_EQ(*dict.FindString("name"), "report.pdf");
  EXPECT_EQ(*dict.FindString("status"), "complete");
  ASSERT_TRUE(dict.FindString("sizeLabel"));
  EXPECT_FALSE(dict.Find("progress"));
  EXPECT_FALSE(dict.Find("spaceId"));
}

TEST(AtriumDownloadsOverviewTest, DictCarriesProgressForActiveDownload) {
  NiceMock<download::MockDownloadItem> item;
  ConfigureItem(item, "g1", "video.mp4", download::DownloadItem::IN_PROGRESS,
               /*transient=*/false);
  base::DictValue dict = DownloadItemToOverviewDict(&item);

  EXPECT_EQ(*dict.FindString("status"), "active");
  EXPECT_EQ(dict.FindInt("progress"), 42);
  ASSERT_TRUE(dict.FindString("captionActive"));
}

// The server sent no Content-Length: GetTotalBytes and PercentComplete
// answer 0 and -1, and the dict must not carry a bogus total or percent.
TEST(AtriumDownloadsOverviewTest, DictMarksIndeterminateWhenSizeIsUnknown) {
  NiceMock<download::MockDownloadItem> item;
  ConfigureItem(item, "g1", "stream.mp4", download::DownloadItem::IN_PROGRESS,
               /*transient=*/false);
  ON_CALL(item, GetTotalBytes()).WillByDefault(Return(0));
  ON_CALL(item, PercentComplete()).WillByDefault(Return(-1));

  base::DictValue dict = DownloadItemToOverviewDict(&item);

  EXPECT_EQ(*dict.FindString("status"), "active");
  EXPECT_TRUE(dict.FindBool("indeterminate").value_or(false));
  EXPECT_FALSE(dict.Find("progress"));
  ASSERT_TRUE(dict.FindString("captionActive"));
  EXPECT_THAT(*dict.FindString("captionActive"),
             ::testing::Not(::testing::HasSubstr(" of ")));
}

using AtriumDownloadsOverviewSpaceTest = ChromeRenderViewHostTestHarness;

TEST_F(AtriumDownloadsOverviewSpaceTest, DictCarriesTheResolvedSpace) {
  std::unique_ptr<content::WebContents> contents = CreateTestWebContents();
  NiceMock<download::MockDownloadItem> item;
  ConfigureItem(item, "g1", "report.pdf", download::DownloadItem::COMPLETE,
               /*transient=*/false);
  content::DownloadItemUtils::AttachInfoForTesting(&item, profile(),
                                                   contents.get());

  const SpaceForWebContents resolve_space = base::BindRepeating(
      [](content::WebContents* expected,
         content::WebContents* actual) -> std::optional<DownloadItemSpace> {
        EXPECT_EQ(actual, expected);
        return DownloadItemSpace{"work", "Work", "#3366CC"};
      },
      contents.get());

  base::DictValue dict = DownloadItemToOverviewDict(&item, resolve_space);
  EXPECT_EQ(*dict.FindString("spaceId"), "work");
  EXPECT_EQ(*dict.FindString("spaceName"), "Work");
  EXPECT_EQ(*dict.FindString("spaceAccent"), "#3366CC");
}

TEST_F(AtriumDownloadsOverviewSpaceTest, UnresolvedSpaceLeavesTheKeysAbsent) {
  std::unique_ptr<content::WebContents> contents = CreateTestWebContents();
  NiceMock<download::MockDownloadItem> item;
  ConfigureItem(item, "g1", "report.pdf", download::DownloadItem::COMPLETE,
               /*transient=*/false);
  content::DownloadItemUtils::AttachInfoForTesting(&item, profile(),
                                                   contents.get());

  const SpaceForWebContents resolve_space = base::BindRepeating(
      [](content::WebContents*) -> std::optional<DownloadItemSpace> {
        return std::nullopt;
      });

  base::DictValue dict = DownloadItemToOverviewDict(&item, resolve_space);
  EXPECT_FALSE(dict.Find("spaceId"));
  EXPECT_FALSE(dict.Find("spaceName"));
  EXPECT_FALSE(dict.Find("spaceAccent"));
}

// A popup with no tracked tab, or a tab that has since closed - either way
// GetOriginalWebContents(item) is null and the resolver never runs.
TEST_F(AtriumDownloadsOverviewSpaceTest, ANoTabDownloadNeverCallsTheResolver) {
  NiceMock<download::MockDownloadItem> item;
  ConfigureItem(item, "g1", "report.pdf", download::DownloadItem::COMPLETE,
               /*transient=*/false);

  bool resolver_called = false;
  const SpaceForWebContents resolve_space = base::BindRepeating(
      [](bool* called,
         content::WebContents*) -> std::optional<DownloadItemSpace> {
        *called = true;
        return std::nullopt;
      },
      &resolver_called);

  base::DictValue dict = DownloadItemToOverviewDict(&item, resolve_space);
  EXPECT_FALSE(resolver_called);
  EXPECT_FALSE(dict.Find("spaceId"));
}

TEST(AtriumDownloadsOverviewTest, DispatchShowInFinderActsOnTheMatchingItem) {
  NiceMock<content::MockDownloadManager> manager;
  NiceMock<download::MockDownloadItem> item;
  ConfigureItem(item, "target-guid", "a.pdf", download::DownloadItem::COMPLETE,
               false);
  EXPECT_CALL(manager, GetDownloadByGuid("target-guid"))
      .WillOnce(Return(&item));
  EXPECT_CALL(item, ShowDownloadInShell());
  EXPECT_CALL(item, Remove()).Times(0);

  DispatchDownloadsOverviewMessage(&manager, "downloadsShowInFinder",
                                  "target-guid");
}

TEST(AtriumDownloadsOverviewTest, DispatchRemoveActsOnTheMatchingItemOnly) {
  NiceMock<content::MockDownloadManager> manager;
  NiceMock<download::MockDownloadItem> item;
  ConfigureItem(item, "target-guid", "a.pdf", download::DownloadItem::COMPLETE,
               false);
  EXPECT_CALL(manager, GetDownloadByGuid("target-guid"))
      .WillOnce(Return(&item));
  EXPECT_CALL(item, Remove());
  EXPECT_CALL(item, DeleteFile_(::testing::_)).Times(0);

  DispatchDownloadsOverviewMessage(&manager, "downloadsRemove", "target-guid");
}

TEST(AtriumDownloadsOverviewTest, DispatchDeleteFromDiskDeletesThenRemoves) {
  NiceMock<content::MockDownloadManager> manager;
  NiceMock<download::MockDownloadItem> item;
  ConfigureItem(item, "target-guid", "a.pdf", download::DownloadItem::COMPLETE,
               false);
  EXPECT_CALL(manager, GetDownloadByGuid("target-guid"))
      .WillOnce(Return(&item));
  ::testing::InSequence seq;
  EXPECT_CALL(item, DeleteFile_(::testing::_));
  EXPECT_CALL(item, Remove());

  DispatchDownloadsOverviewMessage(&manager, "downloadsDeleteFromDisk",
                                  "target-guid");
}

// A guid the manager does not hold must reach no item at all - lookup is
// the only path in.
TEST(AtriumDownloadsOverviewTest, UnknownGuidTouchesNoItem) {
  NiceMock<content::MockDownloadManager> manager;
  EXPECT_CALL(manager, GetDownloadByGuid("stale-guid"))
      .WillOnce(Return(nullptr));

  DispatchDownloadsOverviewMessage(&manager, "downloadsDeleteFromDisk",
                                  "stale-guid");
}

TEST(AtriumDownloadsOverviewTest, NullManagerProducesAnEmptyList) {
  EXPECT_EQ(BuildDownloadsOverviewJson(nullptr), "[]");
}

class AtriumDownloadsPushThrottleTest : public testing::Test {
 protected:
  base::test::SingleThreadTaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  int pushes_ = 0;
  bool showing_ = true;
  DownloadsPushThrottle throttle_{
      base::BindLambdaForTesting([this] { ++pushes_; }),
      base::BindLambdaForTesting([this] { return showing_; })};
};

TEST_F(AtriumDownloadsPushThrottleTest, FirstTickPushesAtOnce) {
  throttle_.OnUpdated(/*state_changed=*/false);
  EXPECT_EQ(pushes_, 1);
}

TEST_F(AtriumDownloadsPushThrottleTest,
       TicksInsideTheWindowCollapseToOneTrailingPush) {
  for (int i = 0; i < 5; ++i) {
    throttle_.OnUpdated(false);
  }
  EXPECT_EQ(pushes_, 1);

  task_environment_.FastForwardBy(base::Milliseconds(250));
  EXPECT_EQ(pushes_, 2);

  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_EQ(pushes_, 2);
}

TEST_F(AtriumDownloadsPushThrottleTest,
       TenSecondsOfTicksPushAtMostFourPerSecond) {
  for (int i = 0; i < 200; ++i) {
    throttle_.OnUpdated(false);
    task_environment_.FastForwardBy(base::Milliseconds(50));
  }
  EXPECT_GE(pushes_, 30);
  EXPECT_LE(pushes_, 41);
}

TEST_F(AtriumDownloadsPushThrottleTest, NoPushWhileOverviewIsHidden) {
  showing_ = false;
  throttle_.OnUpdated(false);
  throttle_.OnUpdated(true);
  throttle_.OnCreatedOrRemoved();
  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_EQ(pushes_, 0);
}

TEST_F(AtriumDownloadsPushThrottleTest,
       CloseDuringTheWindowDropsTheTrailingPush) {
  throttle_.OnUpdated(false);
  throttle_.OnUpdated(false);
  showing_ = false;
  task_environment_.FastForwardBy(base::Seconds(1));
  EXPECT_EQ(pushes_, 1);
}

TEST_F(AtriumDownloadsPushThrottleTest, CreatedOrRemovedPushesAtOnce) {
  throttle_.OnCreatedOrRemoved();
  EXPECT_EQ(pushes_, 1);

  throttle_.OnUpdated(false);
  throttle_.OnCreatedOrRemoved();
  EXPECT_EQ(pushes_, 3);
}

TEST_F(AtriumDownloadsPushThrottleTest, StateChangeBypassesTheWindow) {
  throttle_.OnUpdated(false);
  throttle_.OnUpdated(/*state_changed=*/true);
  EXPECT_EQ(pushes_, 2);
}

}  // namespace
}  // namespace atrium
