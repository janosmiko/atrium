// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_favorite_two_windows_browsertest.cc

#include <optional>
#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "url/gurl.h"

// A favourite click searched only its own window's strip, so a second window
// on the same profile opened a second copy of the site.
namespace {

class AtriumFavoriteTwoWindowsBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    InProcessBrowserTest::SetUpOnMainThread();
  }

  static atrium::AtriumBrowserWindow* WindowOf(Browser* browser) {
    return static_cast<atrium::AtriumBrowserWindow*>(browser->window());
  }

  bool OpenCoreWithFavorite(const GURL& favorite) {
    static constexpr char kTemplate[] =
        R"({"spaces":[{"id":"s1","name":"Test","emoji":"T",)"
        R"("tint":"#111111","profileID":"Default","pinnedTabs":[],)"
        R"("tabs":[],"order":0,"groups":[]}],)"
        R"("favorites":[{"profileID":"Default","title":"Fav","url":"%s"}],)"
        R"("appState":{"activeSpaceID":"s1","sidebarWidth":220,)"
        R"("schemaVersion":3,"archiveEpoch":1,"sidebarWidthMigrated":true}})";
    atrium_core_close();
    base::ScopedAllowBlockingForTesting allow_blocking;
    if (!dir_.CreateUniqueTempDir()) {
      return false;
    }
    const base::FilePath path = dir_.GetPath().AppendASCII("spaces.json");
    if (!base::WriteFile(
            path, base::StringPrintf(kTemplate, favorite.spec().c_str()))) {
      return false;
    }
    return atrium_core_open(path.value().c_str(), 1.0);
  }

  static void ClickFavorite(Browser* browser, const GURL& favorite) {
    base::ListValue args;
    args.Append(favorite.spec());
    args.Append("");
    WindowOf(browser)->OnSurfaceMessage("sidebarFavoriteClicked", args);
  }

  static int TabsClaiming(Browser* browser, const GURL& favorite) {
    int claiming = 0;
    TabStripModel* strip = browser->tab_strip_model();
    for (int i = 0; i < strip->count(); ++i) {
      if (atrium::FavoriteKeyForTab(strip->GetWebContentsAt(i)) ==
          favorite.spec()) {
        ++claiming;
      }
    }
    return claiming;
  }

  static std::optional<base::Value> ModelOf(Browser* browser) {
    return base::JSONReader::Read(atrium::BuildSidebarTabsJSON(browser),
                                  base::JSON_PARSE_RFC);
  }

  static std::optional<base::DictValue> FirstFavoriteOf(Browser* browser) {
    std::optional<base::Value> model = ModelOf(browser);
    if (!model || !model->is_dict()) {
      return std::nullopt;
    }
    const base::ListValue* tiles = model->GetDict().FindList("favorites");
    if (!tiles || tiles->empty()) {
      return std::nullopt;
    }
    return (*tiles)[0].GetDict().Clone();
  }

  static bool AnyRowSelected(Browser* browser) {
    std::optional<base::Value> model = ModelOf(browser);
    if (!model || !model->is_dict()) {
      return false;
    }
    const base::ListValue* rows = model->GetDict().FindList("tabs");
    if (!rows) {
      return false;
    }
    for (const base::Value& row : *rows) {
      if (row.GetDict().FindBool("selected").value_or(false)) {
        return true;
      }
    }
    return false;
  }

  base::ScopedTempDir dir_;
};

IN_PROC_BROWSER_TEST_F(AtriumFavoriteTwoWindowsBrowserTest,
                       ASecondWindowsClickMovesTheFavouritesTabInstead) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL favorite =
      embedded_test_server()->GetURL("app.test", "/title1.html");
  ASSERT_TRUE(OpenCoreWithFavorite(favorite));

  Browser* first = browser();
  ClickFavorite(first, favorite);
  content::WebContents* opened =
      first->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  ASSERT_EQ(1, TabsClaiming(first, favorite)) << "the first click claimed nothing";
  const uint64_t id = atrium::TabId(opened);

  Browser* second = CreateBrowser(first->profile());
  const int second_before = second->tab_strip_model()->count();
  ClickFavorite(second, favorite);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return TabsClaiming(second, favorite) == 1; }))
      << "the second window opened nothing, or opened a copy with no claim";

  EXPECT_EQ(second_before + 1, second->tab_strip_model()->count());
  EXPECT_EQ(0, TabsClaiming(first, favorite))
      << "the first window kept a copy of the site";
  content::WebContents* moved = second->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(moved);
  EXPECT_EQ(id, atrium::TabId(moved)) << "a new tab was opened instead";
}

// The losing window keeps the tile selected, drawn as elsewhere and pointing
// at the moved tab, and lights nothing else up: its strip auto-selected
// another tab on the detach, and selection used to follow that.
IN_PROC_BROWSER_TEST_F(AtriumFavoriteTwoWindowsBrowserTest,
                       TheLosingWindowKeepsTheTileSelectedAndElsewhere) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL favorite =
      embedded_test_server()->GetURL("app.test", "/title1.html");
  ASSERT_TRUE(OpenCoreWithFavorite(favorite));

  Browser* first = browser();
  // A second tab, so the detach has something to auto-select.
  content::WebContents* other = chrome::AddAndReturnTabAt(
      first, embedded_test_server()->GetURL("other.test", "/title2.html"), -1,
      /*foreground=*/true);
  ASSERT_TRUE(other);
  ASSERT_TRUE(content::WaitForLoadStop(other));
  ClickFavorite(first, favorite);
  content::WebContents* opened =
      first->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  const std::string id = base::NumberToString(atrium::TabId(opened));

  Browser* second = CreateBrowser(first->profile());
  ClickFavorite(second, favorite);
  // The detach waits for the snapshot: the pane must never show a blank
  // frame between the live page and the greyscale one.
  EXPECT_EQ(1, TabsClaiming(first, favorite))
      << "the tab left before the losing window froze";
  EXPECT_FALSE(WindowOf(first)->IsFrozen());
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return TabsClaiming(second, favorite) == 1; }));
  ASSERT_TRUE(WindowOf(first)->IsFrozen()) << "the losing window never froze";
  EXPECT_TRUE(WindowOf(first)->FrozenSnapshotHasImageForTesting())
      << "the pane froze on an empty image";

  std::optional<base::DictValue> losing = FirstFavoriteOf(first);
  ASSERT_TRUE(losing);
  EXPECT_TRUE(losing->FindBool("selected").value_or(false))
      << "the tile lost its ring";
  EXPECT_TRUE(losing->FindBool("elsewhere").value_or(false));
  EXPECT_FALSE(losing->FindString("tabId"));
  const std::string* carried = losing->FindString("elsewhereTabId");
  ASSERT_TRUE(carried);
  EXPECT_EQ(id, *carried);
  EXPECT_FALSE(AnyRowSelected(first))
      << "the auto-selected tab's row lit up in the losing window";

  std::optional<base::DictValue> taking = FirstFavoriteOf(second);
  ASSERT_TRUE(taking);
  EXPECT_TRUE(taking->FindBool("selected").value_or(false));
  EXPECT_FALSE(taking->FindBool("elsewhere").value_or(false));
  const std::string* own = taking->FindString("tabId");
  ASSERT_TRUE(own);
  EXPECT_EQ(id, *own);
}

// Window two moves on to another tab. The losing window's frozen pane must
// not vanish: the tab comes back to it live instead.
IN_PROC_BROWSER_TEST_F(AtriumFavoriteTwoWindowsBrowserTest,
                       TheTakingWindowMovingOnHandsTheTabBack) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL favorite =
      embedded_test_server()->GetURL("app.test", "/title1.html");
  ASSERT_TRUE(OpenCoreWithFavorite(favorite));

  Browser* first = browser();
  ClickFavorite(first, favorite);
  content::WebContents* opened =
      first->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  const uint64_t id = atrium::TabId(opened);

  Browser* second = CreateBrowser(first->profile());
  ClickFavorite(second, favorite);
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return TabsClaiming(second, favorite) == 1; }));
  ASSERT_TRUE(WindowOf(first)->IsFrozen());

  content::WebContents* fresh = chrome::AddAndReturnTabAt(
      second, embedded_test_server()->GetURL("other.test", "/title2.html"), -1,
      /*foreground=*/true);
  ASSERT_TRUE(fresh);
  ASSERT_TRUE(content::WaitForLoadStop(fresh));

  ASSERT_TRUE(base::test::RunUntil(
      [&] { return TabsClaiming(first, favorite) == 1; }))
      << "the tab never came back to the window frozen on it";
  EXPECT_FALSE(WindowOf(first)->IsFrozen()) << "the pane stayed frozen";
  content::WebContents* shown = first->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(shown);
  EXPECT_EQ(id, atrium::TabId(shown)) << "a different tab is shown";
  EXPECT_EQ(0, TabsClaiming(second, favorite));
  EXPECT_EQ(fresh, second->tab_strip_model()->GetActiveWebContents())
      << "the taking window lost its own selection";
}

}  // namespace
