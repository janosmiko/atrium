// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_favorite_keep_alive_browsertest.cc
#include <optional>
#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/time/time.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_favorite_parking.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_entry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace {

// The four keys the core requires, so the object loads rather than reading as
// the defaults.
constexpr char kKeepAliveOn[] =
    R"({"schemaVersion":1,"appIconVariant":"auto","animationsEnabled":true,)"
    R"("startupBehavior":"restoreLastActive","favoriteKeepAliveEnabled":true})";
constexpr char kKeepAliveOff[] =
    R"({"schemaVersion":1,"appIconVariant":"auto","animationsEnabled":true,)"
    R"("startupBehavior":"restoreLastActive","favoriteKeepAliveEnabled":false})";

class AtriumFavoriteKeepAliveBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
    favorite_ = embedded_test_server()->GetURL("fav.test", "/title1.html");
    ASSERT_NO_FATAL_FAILURE(OpenCoreWithFavorite());
    // The window's own settings read lands asynchronously and would replace a
    // value set before it.
    ASSERT_TRUE(base::test::RunUntil([] {
      return !atrium::AtriumBrowserWindow::LastSettingsJson().empty();
    }));
    atrium::AtriumBrowserWindow::SetLastSettingsJson(kKeepAliveOn);
  }

  void TearDownOnMainThread() override {
    atrium::AtriumFavoriteParking::SetKeepAliveForTesting(std::nullopt);
    atrium_core_close();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  void OpenCoreWithFavorite() {
    static constexpr char kTemplate[] =
        R"({"spaces":[{"id":"s1","name":"Test","emoji":"T",)"
        R"("tint":"#111111","profileID":"Default","pinnedTabs":[],)"
        R"("tabs":[],"order":0,"groups":[]}],)"
        R"("favorites":[{"profileID":"Default","title":"Fav","url":"%s"}],)"
        R"("appState":{"activeSpaceID":"s1","sidebarWidth":220,)"
        R"("schemaVersion":3,"archiveEpoch":1,"sidebarWidthMigrated":true}})";
    atrium_core_close();
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    const base::FilePath path = dir_.GetPath().AppendASCII("spaces.json");
    ASSERT_TRUE(base::WriteFile(
        path, base::StringPrintf(kTemplate, favorite_.spec().c_str())));
    ASSERT_TRUE(atrium_core_open(path.value().c_str(), 1.0));
  }

  atrium::AtriumBrowserWindow* window() {
    return static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  }

  atrium::AtriumFavoriteParking& parking() {
    atrium::BrowserController* controller =
        atrium::BrowserController::ControllerForBrowser(browser());
    CHECK(controller);
    return controller->favorite_parking_for_testing();
  }

  content::WebContents* ClickTile() { return ClickTileIn(browser()); }

  content::WebContents* ClickTileIn(Browser* target) {
    base::ListValue args;
    args.Append(favorite_.spec());
    args.Append("");
    static_cast<atrium::AtriumBrowserWindow*>(target->window())
        ->OnSurfaceMessage("sidebarFavoriteClicked", args);
    return target->tab_strip_model()->GetActiveWebContents();
  }

  // The sidebar's close route, which is a user close.
  void CloseFromSidebar(content::WebContents* contents) {
    base::ListValue args;
    args.Append(base::NumberToString(atrium::TabId(contents)));
    window()->OnSurfaceMessage("sidebarTabClosed", args);
  }

  GURL favorite_;

 private:
  base::ScopedTempDir dir_;
};

IN_PROC_BROWSER_TEST_F(AtriumFavoriteKeepAliveBrowserTest,
                       AClosedFavouriteComesBackAsTheSamePage) {
  content::WebContents* opened = ClickTile();
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  ASSERT_EQ(favorite_.spec(), atrium::FavoriteKeyForTab(opened));
  const int entry_id =
      opened->GetController().GetLastCommittedEntry()->GetUniqueID();
  const std::string tab_id = base::NumberToString(atrium::TabId(opened));
  TabStripModel* tabs = browser()->tab_strip_model();
  const int count_open = tabs->count();

  CloseFromSidebar(opened);
  EXPECT_EQ(count_open - 1, tabs->count()) << "the page is still in the strip";
  ASSERT_EQ(opened, parking().ParkedForTesting(favorite_.spec()))
      << "the close deleted the page instead of parking it";
  EXPECT_EQ(std::string::npos,
            atrium::BuildSidebarTabsJSON(browser()).find(
                "\"tabId\":\"" + tab_id + "\""))
      << "the tile still reads as loaded";

  EXPECT_EQ(opened, ClickTile())
      << "the click did not bring back the parked page";
  EXPECT_EQ(count_open, tabs->count());
  EXPECT_EQ(nullptr, parking().ParkedForTesting(favorite_.spec()));
  EXPECT_FALSE(opened->IsLoading()) << "the restore loaded the page again";
  EXPECT_EQ(entry_id,
            opened->GetController().GetLastCommittedEntry()->GetUniqueID())
      << "the restore committed a new navigation";
}

IN_PROC_BROWSER_TEST_F(AtriumFavoriteKeepAliveBrowserTest,
                       AParkedPageIsMutedUntilItComesBack) {
  content::WebContents* opened = ClickTile();
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  ASSERT_FALSE(opened->IsAudioMuted());

  CloseFromSidebar(opened);
  ASSERT_EQ(opened, parking().ParkedForTesting(favorite_.spec()));
  EXPECT_TRUE(opened->IsAudioMuted());

  ASSERT_EQ(opened, ClickTile());
  EXPECT_FALSE(opened->IsAudioMuted());
}

IN_PROC_BROWSER_TEST_F(AtriumFavoriteKeepAliveBrowserTest,
                       AFavouriteOnASubPageClosesAndLoadsAgain) {
  const GURL sub = embedded_test_server()->GetURL("fav.test", "/title2.html");
  content::WebContents* opened = ClickTile();
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  opened->GetController().LoadURL(sub, content::Referrer(),
                                  ui::PAGE_TRANSITION_LINK, std::string());
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  ASSERT_EQ(sub, opened->GetLastCommittedURL());
  const uint64_t closed_id = atrium::TabId(opened);

  content::WebContentsDestroyedWatcher destroyed(opened);
  CloseFromSidebar(opened);
  destroyed.Wait();
  EXPECT_EQ(nullptr, parking().ParkedForTesting(favorite_.spec()));

  content::WebContents* reopened = ClickTile();
  ASSERT_TRUE(reopened);
  EXPECT_NE(closed_id, atrium::TabId(reopened));
  ASSERT_TRUE(content::WaitForLoadStop(reopened));
  EXPECT_EQ(favorite_, reopened->GetLastCommittedURL());
}

IN_PROC_BROWSER_TEST_F(AtriumFavoriteKeepAliveBrowserTest,
                       AParkedPageIsDeletedWhenItsTimeRunsOut) {
  atrium::AtriumFavoriteParking::SetKeepAliveForTesting(
      base::Milliseconds(100));
  content::WebContents* opened = ClickTile();
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));

  CloseFromSidebar(opened);
  ASSERT_EQ(opened, parking().ParkedForTesting(favorite_.spec()));
  content::WebContentsDestroyedWatcher destroyed(opened);
  destroyed.Wait();
  EXPECT_EQ(nullptr, parking().ParkedForTesting(favorite_.spec()));
}

// The tile means the same page in every window of the profile. A restore that
// only looked in the clicking window loaded the page again while the other
// window held a live renderer nothing could reach.
IN_PROC_BROWSER_TEST_F(AtriumFavoriteKeepAliveBrowserTest,
                       ASecondWindowTakesThePageTheFirstOneParked) {
  content::WebContents* opened = ClickTile();
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  const int entry_id =
      opened->GetController().GetLastCommittedEntry()->GetUniqueID();

  CloseFromSidebar(opened);
  ASSERT_EQ(opened, parking().ParkedForTesting(favorite_.spec()));

  Browser* second = CreateBrowser(browser()->profile());
  const int second_count = second->tab_strip_model()->count();

  EXPECT_EQ(opened, ClickTileIn(second))
      << "the second window loaded the page again instead of taking it";
  EXPECT_EQ(second_count + 1, second->tab_strip_model()->count());
  EXPECT_EQ(nullptr, parking().ParkedForTesting(favorite_.spec()))
      << "the first window is still holding a page nothing can reach";
  EXPECT_EQ(entry_id,
            opened->GetController().GetLastCommittedEntry()->GetUniqueID());
}

// The rule is the page, not the URL: a fragment is a place on the same page.
IN_PROC_BROWSER_TEST_F(AtriumFavoriteKeepAliveBrowserTest,
                       AFragmentIsStillTheFavouritesOwnPage) {
  content::WebContents* opened = ClickTile();
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));

  const GURL fragment(favorite_.spec() + "#section");
  opened->GetController().LoadURL(fragment, content::Referrer(),
                                  ui::PAGE_TRANSITION_LINK, std::string());
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  ASSERT_EQ(fragment, opened->GetLastCommittedURL());

  CloseFromSidebar(opened);
  EXPECT_EQ(opened, parking().ParkedForTesting(favorite_.spec()))
      << "a fragment change read as navigating away";
}

// A parked page is owned by nothing on screen, so a window that goes away
// without dropping it would leak a whole renderer for the rest of the session.
IN_PROC_BROWSER_TEST_F(AtriumFavoriteKeepAliveBrowserTest,
                       ClosingTheWindowDeletesThePageItParked) {
  content::WebContents* opened = ClickTile();
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));

  CloseFromSidebar(opened);
  ASSERT_EQ(opened, parking().ParkedForTesting(favorite_.spec()));

  // A second window, so closing the first does not end the browser process
  // before the watcher below can see the page go.
  CreateBrowser(browser()->profile());
  content::WebContentsDestroyedWatcher destroyed(opened);
  CloseBrowserSynchronously(browser());
  destroyed.Wait();
}

IN_PROC_BROWSER_TEST_F(AtriumFavoriteKeepAliveBrowserTest,
                       WithTheSettingOffACloseDeletesThePage) {
  atrium::AtriumBrowserWindow::SetLastSettingsJson(kKeepAliveOff);
  content::WebContents* opened = ClickTile();
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));

  content::WebContentsDestroyedWatcher destroyed(opened);
  CloseFromSidebar(opened);
  destroyed.Wait();
  EXPECT_EQ(nullptr, parking().ParkedForTesting(favorite_.spec()));
}

}  // namespace
