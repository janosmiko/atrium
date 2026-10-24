// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include <string>

#include "base/command_line.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/stringprintf.h"
#include "base/test/mock_log.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_for_profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"
#include "chrome/browser/ui/atrium/atrium_traffic_routing.h"
#include "chrome/browser/ui/atrium/atrium_url_submit_decision.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace {

// Two spaces stored in the OPPOSITE of their rendered order, so an index read
// off the array addresses the wrong one and the test fails rather than passing
// by coincidence. `Work` is the routing target throughout.
constexpr char kSpaces[] =
    R"({"spaces":[
        {"id":"work","name":"Work","emoji":"W","tint":"#111111",
         "profileID":"%s","pinnedTabs":[],"tabs":[],"order":1,"groups":[]},
        {"id":"home","name":"Home","emoji":"H","tint":"#111111",
         "profileID":"Default","pinnedTabs":[],"tabs":[],"order":0,"groups":[]}],
        "profiles":[{"id":"Default","name":"Default"},{"id":"%s","name":"Other"}],
        "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
        "activeSpaceID":"home","sidebarWidthMigrated":true}})";

constexpr char kRules[] =
    R"({"schemaVersion":1,"rules":[{"id":"r1","enabled":true,)"
    R"("matchType":"contains","pattern":"routed.test",)"
    R"("targetSpaceID":"work"}]})";

// A path rule, because `contains` tests the host: a host rule for a
// favourite's own site claims the favourite's page too, and the tile's tab
// would be routed away before the submission under test ever ran.
constexpr char kPathRules[] =
    R"({"schemaVersion":1,"rules":[{"id":"r2","enabled":true,)"
    R"("matchType":"regex","pattern":"title2\\.html",)"
    R"("targetSpaceID":"work"}]})";

// Version 2: the Work rule skips navigations that start in Home.
constexpr char kExcludingRules[] =
    R"({"schemaVersion":2,"defaultTargetSpaceID":"","rules":[{"id":"r3",)"
    R"("enabled":true,"matchType":"contains","pattern":"routed.test",)"
    R"("targetSpaceID":"work","excludedSourceSpaceIDs":["home"]}]})";

// No rule at all, only the fallback: everything opens in Work.
constexpr char kFallbackRules[] =
    R"({"schemaVersion":2,"defaultTargetSpaceID":"work","rules":[]})";

// The space id of the row a live tab is showing, empty when it has none.
std::string SpaceIdForTab(content::WebContents* contents) {
  char* raw = atrium_core_space_id_for_tab(atrium::TabId(contents));
  if (!raw) {
    return std::string();
  }
  std::string id(raw);
  atrium_core_string_free(raw);
  return id;
}

base::Value SidebarModel(Browser* browser) {
  std::optional<base::Value> parsed = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(browser), base::JSON_PARSE_RFC);
  CHECK(parsed) << "BuildSidebarTabsJSON did not return valid JSON";
  return std::move(*parsed);
}

// Whether any row list in the model carries a row with this tab id. Walks the
// per-space panes too, because each one renders its own space.
bool AnyRowHasId(const base::Value& model, const std::string& tab_id) {
  bool found = false;
  auto scan = [&found, &tab_id](const base::ListValue* rows) {
    if (!rows) {
      return;
    }
    for (const base::Value& row : *rows) {
      if (const std::string* id = row.GetDict().FindString("id");
          id && *id == tab_id) {
        found = true;
      }
    }
  };
  scan(model.GetDict().FindList("tabs"));
  if (const base::ListValue* spaces = model.GetDict().FindList("spaces")) {
    for (const base::Value& space : *spaces) {
      scan(space.GetDict().FindList("tabs"));
    }
  }
  return found;
}

// Return in the command bar lands here. Not the accept message itself: it names
// a match index rather than a URL, and only the resolved URL says where the
// submission was pointed.
void SubmitInTheCommandBar(Browser* browser, const GURL& typed) {
  static_cast<atrium::AtriumBrowserWindow*>(browser->window())
      ->OpenCommandBarMatch(typed);
}

class AtriumRoutingMoveBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    InProcessBrowserTest::SetUpOnMainThread();
  }

  void TearDownOnMainThread() override {
    atrium::SetRoutingRulesJSON(std::string());
    atrium::BrowserController::SetRoutedOpenTargetUnrealisedForTesting(false);
    atrium_core_close();
    InProcessBrowserTest::TearDownOnMainThread();
  }

  // `work_profile` owns the Work space: "Default" keeps the route in one
  // window, another id sends it across profiles.
  void OpenCore(const std::string& work_profile) {
    OpenCoreWithRules(work_profile, kRules);
  }

  void OpenCoreWithRules(const std::string& work_profile, const char* rules) {
    atrium_core_close();
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    const base::FilePath path = dir_.GetPath().AppendASCII("spaces.json");
    ASSERT_TRUE(base::WriteFile(
        path, base::StringPrintf(kSpaces, work_profile.c_str(),
                                 work_profile.c_str())));
    ASSERT_TRUE(atrium_core_open(path.AsUTF8Unsafe().c_str(), 0.0));
    atrium::SetRoutingRulesJSON(rules);
  }

  // A browser-initiated load on a tab that already exists. NavigateToURL hangs
  // in this window, and the routed URL commits an error page in any case.
  void LoadInPlace(content::WebContents* contents, const GURL& url) {
    content::NavigationController::LoadURLParams params(url);
    params.transition_type = ui::PAGE_TRANSITION_TYPED;
    contents->GetController().LoadURLWithParams(params);
    content::RunAllTasksUntilIdle();
  }

 private:
  base::ScopedTempDir dir_;
};

// The reopen the route itself performs arrives here a task later, with the
// bypass already released. Without this gate the rule matches its own result
// and the chain never ends.
IN_PROC_BROWSER_TEST_F(AtriumRoutingMoveBrowserTest,
                       ATabAlreadyInTheTargetSpaceIsNotRoutedAgain) {
  ASSERT_TRUE(embedded_test_server()->Start());
  OpenCore("Default");
  const GURL routed = embedded_test_server()->GetURL("routed.test", "/title1.html");

  // Sidebar slot 1 is Work, which is stored FIRST.
  ASSERT_TRUE(atrium_core_set_active_space_index(1));
  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), routed, -1, /*foreground=*/true);
  ASSERT_TRUE(tab);
  content::WaitForLoadStopWithoutSuccessCheck(tab);
  ASSERT_EQ(SpaceIdForTab(tab), "work");

  const int before = browser()->tab_strip_model()->count();
  LoadInPlace(tab, routed);
  content::WaitForLoadStopWithoutSuccessCheck(tab);

  EXPECT_EQ(browser()->tab_strip_model()->count(), before)
      << "the route opened a second copy of a tab already in its target";
  EXPECT_EQ(tab->GetLastCommittedURL(), routed)
      << "the navigation was cancelled instead of proceeding";
  EXPECT_EQ(SpaceIdForTab(tab), "work");
}

// The tab the user typed into is the tab that moves. It keeps its renderer and
// its history: only its row changes space and the window changes what it shows.
IN_PROC_BROWSER_TEST_F(AtriumRoutingMoveBrowserTest,
                       ASameProfileRouteMovesTheTabInPlace) {
  ASSERT_TRUE(embedded_test_server()->Start());
  OpenCore("Default");
  const GURL start = embedded_test_server()->GetURL("start.test", "/title1.html");
  const GURL routed = embedded_test_server()->GetURL("routed.test", "/title1.html");

  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), start, -1, /*foreground=*/true);
  ASSERT_TRUE(tab);
  content::WaitForLoadStopWithoutSuccessCheck(tab);
  ASSERT_EQ(SpaceIdForTab(tab), "home");
  const int before = browser()->tab_strip_model()->count();

  LoadInPlace(tab, routed);
  content::WaitForLoadStopWithoutSuccessCheck(tab);

  EXPECT_EQ(SpaceIdForTab(tab), "work") << "the row stayed in the old space";
  EXPECT_EQ(browser()->tab_strip_model()->count(), before)
      << "the route added a tab instead of moving the one it was given";
  EXPECT_NE(browser()->tab_strip_model()->GetIndexOfWebContents(tab),
            TabStripModel::kNoTab)
      << "the same WebContents must still be in the strip";
  // Work is stored first but renders second, so an array index shows as 0 here.
  EXPECT_EQ(atrium_core_active_space_index(), 1)
      << "the window is not showing the space the rule named";
  EXPECT_EQ(tab->GetLastCommittedURL(), routed)
      << "the tab never actually loaded the routed URL";
}

// SelectSpace shows the target space's remembered or first row, which is not
// the tab that just moved when the target already holds another tab.
IN_PROC_BROWSER_TEST_F(AtriumRoutingMoveBrowserTest,
                       ASameProfileRouteShowsTheMovedTabWhenTheTargetIsNotEmpty) {
  ASSERT_TRUE(embedded_test_server()->Start());
  OpenCore("Default");
  const GURL seed = embedded_test_server()->GetURL("seed.test", "/title1.html");
  const GURL start = embedded_test_server()->GetURL("start.test", "/title1.html");
  const GURL routed = embedded_test_server()->GetURL("routed.test", "/title1.html");

  content::WebContents* existing =
      chrome::AddAndReturnTabAt(browser(), seed, -1, /*foreground=*/true);
  ASSERT_TRUE(existing);
  content::WaitForLoadStopWithoutSuccessCheck(existing);
  const std::string work = "work";
  ASSERT_TRUE(
      atrium_core_move_tab_id_to_space(atrium::TabId(existing), work.c_str()));
  ASSERT_EQ(SpaceIdForTab(existing), "work");

  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), start, -1, /*foreground=*/true);
  ASSERT_TRUE(tab);
  content::WaitForLoadStopWithoutSuccessCheck(tab);
  ASSERT_EQ(SpaceIdForTab(tab), "home");
  const int before = browser()->tab_strip_model()->count();

  LoadInPlace(tab, routed);
  content::WaitForLoadStopWithoutSuccessCheck(tab);

  EXPECT_EQ(SpaceIdForTab(tab), "work");
  EXPECT_EQ(browser()->tab_strip_model()->count(), before)
      << "the route added a tab instead of moving the one it was given";
  EXPECT_EQ(browser()->tab_strip_model()->GetActiveWebContents(), tab)
      << "the window is showing Work's existing tab, not the one that moved";
}

// A WebContents is bound to its creating BrowserContext, so a cross-profile
// route reopens - and then closes the source, which is the half that was
// missing: the tab used to be left behind in the space it was typed in.
IN_PROC_BROWSER_TEST_F(AtriumRoutingMoveBrowserTest,
                       ACrossProfileRouteClosesTheSourceTab) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  Profile& second = profiles::testing::CreateProfileSync(
      profile_manager, profile_manager->GenerateNextProfileDirectoryPath());
  const std::string other_id = second.GetBaseName().AsUTF8Unsafe();
  ASSERT_TRUE(atrium::BrowserController::CreateForProfile(
      &second, /*show=*/true, /*startup_url=*/"about:blank"));
  OpenCore(other_id);
  Browser* into = atrium::BrowserForProfileId(other_id);
  ASSERT_TRUE(into);

  const GURL start = embedded_test_server()->GetURL("start.test", "/title1.html");
  const GURL routed = embedded_test_server()->GetURL("routed.test", "/title1.html");
  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), start, -1, /*foreground=*/true);
  ASSERT_TRUE(tab);
  content::WaitForLoadStopWithoutSuccessCheck(tab);
  const int source_before = browser()->tab_strip_model()->count();
  const int target_before = into->tab_strip_model()->count();

  LoadInPlace(tab, routed);
  content::RunAllTasksUntilIdle();

  EXPECT_EQ(into->tab_strip_model()->count(), target_before + 1)
      << "nothing was reopened in the profile that owns the space";
  EXPECT_EQ(browser()->tab_strip_model()->count(), source_before - 1)
      << "the source tab was left behind in the space it was typed in";
  content::WebContents* reopened = into->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(reopened);
  content::WaitForLoadStopWithoutSuccessCheck(reopened);
  EXPECT_EQ(reopened->GetLastCommittedURL(), routed)
      << "the reopened tab never actually loaded the routed URL";
}

// The throttle already destroyed the original navigation before this runs, so
// a decline - the target profile has no window here - must not strand the
// tab on nothing.
IN_PROC_BROWSER_TEST_F(AtriumRoutingMoveBrowserTest,
                       ADeclinedRouteStillLoadsTheUrlInPlace) {
  ASSERT_TRUE(embedded_test_server()->Start());
  OpenCore("no-such-profile");
  const GURL start = embedded_test_server()->GetURL("start.test", "/title1.html");
  const GURL routed = embedded_test_server()->GetURL("routed.test", "/title1.html");

  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), start, -1, /*foreground=*/true);
  ASSERT_TRUE(tab);
  content::WaitForLoadStopWithoutSuccessCheck(tab);
  const int before = browser()->tab_strip_model()->count();

  LoadInPlace(tab, routed);
  content::WaitForLoadStopWithoutSuccessCheck(tab);

  EXPECT_EQ(tab->GetLastCommittedURL(), routed)
      << "a declined route left the navigation dropped instead of loading it";
  EXPECT_EQ(browser()->tab_strip_model()->count(), before);
}

// A typed URL used to open where it was typed and only then be moved, so the
// user watched a tab and a row appear in the space they were leaving. The
// command bar asks the rules first, so nothing ever lands here.
IN_PROC_BROWSER_TEST_F(AtriumRoutingMoveBrowserTest,
                       ACommandBarUrlOpensInTheRoutedSpaceWithoutASourceTab) {
  ASSERT_TRUE(embedded_test_server()->Start());
  OpenCore("Default");
  const GURL start = embedded_test_server()->GetURL("start.test", "/title1.html");
  const GURL routed = embedded_test_server()->GetURL("routed.test", "/title1.html");

  content::WebContents* origin =
      chrome::AddAndReturnTabAt(browser(), start, -1, /*foreground=*/true);
  ASSERT_TRUE(origin);
  content::WaitForLoadStopWithoutSuccessCheck(origin);
  ASSERT_EQ(SpaceIdForTab(origin), "home");
  ASSERT_EQ(atrium_core_active_space_index(), 0);
  const int before = browser()->tab_strip_model()->count();

  SubmitInTheCommandBar(browser(), routed);

  // Read before a single task runs: the throttle's route arrives later, so
  // anything it repairs would hide the tab and the row Home briefly held.
  EXPECT_EQ(atrium_core_active_space_index(), 1)
      << "the window was still on Home when the tab was added to it";
  content::WebContents* opened =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(opened);
  ASSERT_NE(opened, origin);
  EXPECT_EQ(SpaceIdForTab(opened), "work")
      << "the typed URL landed in Home and was moved out afterwards";

  content::RunAllTasksUntilIdle();

  EXPECT_EQ(browser()->tab_strip_model()->count(), before + 1)
      << "the route opened a second copy of the tab the command bar opened";
  EXPECT_EQ(SpaceIdForTab(opened), "work");
  EXPECT_EQ(SpaceIdForTab(origin), "home")
      << "the tab that was on screen when the user typed was moved too";
  content::WaitForLoadStopWithoutSuccessCheck(opened);
  EXPECT_EQ(opened->GetLastCommittedURL(), routed);
}

// The same across profiles, where the space's own window is the one that opens
// the tab. There is no source to close, so the space the user typed from keeps
// every tab it had.
IN_PROC_BROWSER_TEST_F(AtriumRoutingMoveBrowserTest,
                       ACommandBarUrlOpensInTheOtherProfileWithoutASourceTab) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  Profile& second = profiles::testing::CreateProfileSync(
      profile_manager, profile_manager->GenerateNextProfileDirectoryPath());
  const std::string other_id = second.GetBaseName().AsUTF8Unsafe();
  ASSERT_TRUE(atrium::BrowserController::CreateForProfile(
      &second, /*show=*/true, /*startup_url=*/"about:blank"));
  OpenCore(other_id);
  Browser* into = atrium::BrowserForProfileId(other_id);
  ASSERT_TRUE(into);
  // Showing the second profile's window made it the slot's visible one, so the
  // command bar would resolve straight to it and never route at all.
  atrium::BrowserController::ActivateWindow(
      atrium::BrowserController::ControllerForBrowser(browser()));

  const GURL start = embedded_test_server()->GetURL("start.test", "/title1.html");
  const GURL routed = embedded_test_server()->GetURL("routed.test", "/title1.html");
  content::WebContents* origin =
      chrome::AddAndReturnTabAt(browser(), start, -1, /*foreground=*/true);
  ASSERT_TRUE(origin);
  content::WaitForLoadStopWithoutSuccessCheck(origin);
  const int source_before = browser()->tab_strip_model()->count();
  const int target_before = into->tab_strip_model()->count();

  SubmitInTheCommandBar(browser(), routed);

  // Before any task runs, for the same reason the same-profile test reads
  // early: the throttle's reopen-and-close would erase the evidence.
  EXPECT_EQ(browser()->tab_strip_model()->count(), source_before)
      << "a tab was opened in the space the user typed from";
  EXPECT_EQ(into->tab_strip_model()->count(), target_before + 1)
      << "nothing opened in the profile that owns the space";

  // A tab added to a profile whose window is not the presenter shows nowhere
  // until the in-window swap puts it in the presenter's pane.
  content::WebContents* landed = into->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(landed);
  atrium::AtriumBrowserWindow* presenter =
      atrium::BrowserController::PresentedWindowForBrowser(into);
  ASSERT_TRUE(presenter);
  EXPECT_EQ(presenter->CurrentlyDisplayedWebContents(), landed)
      << "the rail moved to the routed space but the pane kept the old page";
  EXPECT_TRUE(AnyRowHasId(SidebarModel(into),
                          base::NumberToString(atrium::TabId(landed))))
      << "the routed space is showing no row for the tab it just opened";

  content::RunAllTasksUntilIdle();

  EXPECT_EQ(into->tab_strip_model()->count(), target_before + 1)
      << "the route opened a second copy in the target profile";
  EXPECT_EQ(browser()->tab_strip_model()->count(), source_before);
  content::WebContents* opened = into->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(opened);
  content::WaitForLoadStopWithoutSuccessCheck(opened);
  EXPECT_EQ(opened->GetLastCommittedURL(), routed)
      << "the opened tab never actually loaded the routed URL";
}

// A rule naming the space already on screen must not cost a second tab or a
// rail move, whichever of the two openers ends up serving it.
IN_PROC_BROWSER_TEST_F(AtriumRoutingMoveBrowserTest,
                       ACommandBarUrlForTheActiveSpaceOpensOneTabThere) {
  ASSERT_TRUE(embedded_test_server()->Start());
  OpenCore("Default");
  const GURL routed = embedded_test_server()->GetURL("routed.test", "/title1.html");

  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  ASSERT_TRUE(controller->SelectSpace(1));
  ASSERT_EQ(atrium_core_active_space_index(), 1);
  const int before = browser()->tab_strip_model()->count();

  SubmitInTheCommandBar(browser(), routed);
  content::RunAllTasksUntilIdle();

  EXPECT_EQ(browser()->tab_strip_model()->count(), before + 1)
      << "the URL opened twice, or not at all";
  content::WebContents* opened =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(opened);
  EXPECT_EQ(SpaceIdForTab(opened), "work");
  EXPECT_EQ(atrium_core_active_space_index(), 1)
      << "the rail left the space it was already showing";
}

// The reopen lands in a window the user is not looking at, and SelectSpace has
// just emptied the pane that presents it. Only the in-window swap fills it, so
// the tab must be on screen before anything else gets a turn.
IN_PROC_BROWSER_TEST_F(AtriumRoutingMoveBrowserTest,
                       ACrossProfileRouteShowsTheReopenedTabInTheTargetsPane) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  Profile& second = profiles::testing::CreateProfileSync(
      profile_manager, profile_manager->GenerateNextProfileDirectoryPath());
  const std::string other_id = second.GetBaseName().AsUTF8Unsafe();
  ASSERT_TRUE(atrium::BrowserController::CreateForProfile(
      &second, /*show=*/true, /*startup_url=*/"about:blank"));
  OpenCore(other_id);
  Browser* into = atrium::BrowserForProfileId(other_id);
  ASSERT_TRUE(into);
  // Showing the second profile's window made it the slot's presenter, and a
  // target that is already presenting proves nothing about the swap.
  atrium::BrowserController::ActivateWindow(
      atrium::BrowserController::ControllerForBrowser(browser()));

  const GURL start = embedded_test_server()->GetURL("start.test", "/title1.html");
  const GURL routed = embedded_test_server()->GetURL("routed.test", "/title1.html");
  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), start, -1, /*foreground=*/true);
  ASSERT_TRUE(tab);
  content::WaitForLoadStopWithoutSuccessCheck(tab);
  const int source_before = browser()->tab_strip_model()->count();
  const int target_before = into->tab_strip_model()->count();

  content::NavigationController::LoadURLParams params(routed);
  params.transition_type = ui::PAGE_TRANSITION_TYPED;
  tab->GetController().LoadURLWithParams(params);
  // Stops the moment the reopen lands, so nothing later can repair the pane
  // and pass this for the wrong reason.
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return into->tab_strip_model()->count() == target_before + 1;
  })) << "the route never reopened the URL in the profile that owns the space";

  content::WebContents* reopened = into->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(reopened);
  atrium::AtriumBrowserWindow* presenter =
      atrium::BrowserController::PresentedWindowForBrowser(into);
  ASSERT_TRUE(presenter);
  EXPECT_EQ(presenter->CurrentlyDisplayedWebContents(), reopened)
      << "the rail moved to the routed space but the pane kept the old page";

  content::RunAllTasksUntilIdle();

  EXPECT_EQ(into->tab_strip_model()->count(), target_before + 1)
      << "the route opened a second copy in the target profile";
  EXPECT_EQ(browser()->tab_strip_model()->count(), source_before - 1)
      << "the source tab was left behind in the space it was typed in";
}

// A rule claiming a URL on the favourite's own site has to beat the same-tab
// load: taking that load hands the favourite's tab to the throttle, which
// drags it out of the space it lives in.
IN_PROC_BROWSER_TEST_F(AtriumRoutingMoveBrowserTest,
                       ARoutedUrlOverAFavouritesTabLeavesThatTabInPlace) {
  ASSERT_TRUE(embedded_test_server()->Start());
  OpenCore("Default");
  atrium::SetRoutingRulesJSON(kPathRules);
  const GURL favourite = embedded_test_server()->GetURL("app.test", "/title1.html");
  const GURL typed = embedded_test_server()->GetURL("app.test", "/title2.html");

  content::WebContents* claimed =
      chrome::AddAndReturnTabAt(browser(), favourite, -1, /*foreground=*/true);
  ASSERT_TRUE(claimed);
  content::WaitForLoadStopWithoutSuccessCheck(claimed);
  atrium::SetFavoriteKeyForTab(claimed, favourite.spec());
  ASSERT_EQ(SpaceIdForTab(claimed), "home");
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_EQ(window->CurrentlyDisplayedWebContents(), claimed)
      << "the favourite's tab is not the one a submission would load into";
  ASSERT_EQ(atrium::DecideUrlSubmitTarget(/*active_is_favourite=*/true,
                                          favourite, typed),
            atrium::SubmitTarget::kSameTab)
      << "the typed URL is not one the favourite's tab would have taken";
  const int before = browser()->tab_strip_model()->count();

  SubmitInTheCommandBar(browser(), typed);

  // Read before a single task runs: the throttle's move would otherwise erase
  // the evidence that the favourite's tab took the load at all.
  EXPECT_EQ(atrium_core_active_space_index(), 1)
      << "the window stayed on the space the favourite lives in";
  content::WebContents* opened =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(opened);
  EXPECT_NE(opened, claimed) << "the routed URL loaded in the favourite's tab";
  EXPECT_EQ(SpaceIdForTab(opened), "work");
  EXPECT_EQ(browser()->tab_strip_model()->count(), before + 1)
      << "the routed URL opened no tab of its own";

  content::RunAllTasksUntilIdle();

  EXPECT_EQ(claimed->GetLastCommittedURL(), favourite)
      << "the favourite's tab was navigated off its own page";
  EXPECT_EQ(SpaceIdForTab(claimed), "home")
      << "the favourite's tab was dragged out of the space it lives in";
  EXPECT_EQ(atrium::FavoriteKeyForTab(claimed), favourite.spec())
      << "the tile lost its binding, so its next click opens another copy";
}

// VLOG is compiled in but silent by default, and the decline below speaks only
// through the log.
class AtriumRoutingMoveVlogBrowserTest : public AtriumRoutingMoveBrowserTest {
 protected:
  void SetUpCommandLine(base::CommandLine* command_line) override {
    command_line->AppendSwitchASCII("vmodule", "atrium_browser_controller=1");
    AtriumRoutingMoveBrowserTest::SetUpCommandLine(command_line);
  }
};

// A Browser with no native host aborts the process the moment a tab is added
// to it. Browser::Create realises its window before it returns, so the guard
// is forced rather than built.
IN_PROC_BROWSER_TEST_F(AtriumRoutingMoveVlogBrowserTest,
                       ACrossProfileRouteDeclinesAnUnrealisedTargetWindow) {
  ASSERT_TRUE(embedded_test_server()->Start());
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  Profile& second = profiles::testing::CreateProfileSync(
      profile_manager, profile_manager->GenerateNextProfileDirectoryPath());
  const std::string other_id = second.GetBaseName().AsUTF8Unsafe();
  ASSERT_TRUE(atrium::BrowserController::CreateForProfile(
      &second, /*show=*/true, /*startup_url=*/"about:blank"));
  OpenCore(other_id);
  Browser* into = atrium::BrowserForProfileId(other_id);
  ASSERT_TRUE(into);

  const GURL start = embedded_test_server()->GetURL("start.test", "/title1.html");
  const GURL routed = embedded_test_server()->GetURL("routed.test", "/title1.html");
  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), start, -1, /*foreground=*/true);
  ASSERT_TRUE(tab);
  content::WaitForLoadStopWithoutSuccessCheck(tab);
  const int source_before = browser()->tab_strip_model()->count();
  const int target_before = into->tab_strip_model()->count();

  base::test::MockLog log;
  EXPECT_CALL(log, Log(testing::_, testing::_, testing::_, testing::_,
                       testing::_))
      .Times(testing::AnyNumber());
  EXPECT_CALL(log, Log(testing::_, testing::_, testing::_, testing::_,
                       testing::HasSubstr(
                           "routed move declined - target window not realised")))
      .Times(testing::AtLeast(1));
  log.StartCapturingLogs();
  atrium::BrowserController::SetRoutedOpenTargetUnrealisedForTesting(true);
  LoadInPlace(tab, routed);
  content::RunAllTasksUntilIdle();
  atrium::BrowserController::SetRoutedOpenTargetUnrealisedForTesting(false);
  log.StopCapturingLogs();

  EXPECT_EQ(into->tab_strip_model()->count(), target_before)
      << "a tab was added to a window with no native host";
  EXPECT_EQ(browser()->tab_strip_model()->count(), source_before)
      << "the source tab was closed for a move that never happened";
  content::WaitForLoadStopWithoutSuccessCheck(tab);
  EXPECT_EQ(tab->GetLastCommittedURL(), routed)
      << "the declined route dropped the navigation instead of loading it";
}

// A rule that excludes the source space leaves a tab that starts there alone.
IN_PROC_BROWSER_TEST_F(AtriumRoutingMoveBrowserTest,
                       ANavigationFromAnExcludedSpaceStaysPut) {
  ASSERT_TRUE(embedded_test_server()->Start());
  OpenCoreWithRules("Default", kExcludingRules);
  const GURL start = embedded_test_server()->GetURL("start.test", "/title1.html");
  const GURL routed = embedded_test_server()->GetURL("routed.test", "/title1.html");

  // Sidebar slot 0 is Home, already active by default.
  ASSERT_EQ(atrium_core_active_space_index(), 0);
  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), start, -1, /*foreground=*/true);
  ASSERT_TRUE(tab);
  content::WaitForLoadStopWithoutSuccessCheck(tab);
  ASSERT_EQ(SpaceIdForTab(tab), "home");

  const int before = browser()->tab_strip_model()->count();
  LoadInPlace(tab, routed);
  content::WaitForLoadStopWithoutSuccessCheck(tab);

  EXPECT_EQ(browser()->tab_strip_model()->count(), before);
  EXPECT_NE(browser()->tab_strip_model()->GetIndexOfWebContents(tab),
            TabStripModel::kNoTab)
      << "the same WebContents must still be in the strip";
  EXPECT_EQ(tab->GetLastCommittedURL(), routed);
  EXPECT_EQ(SpaceIdForTab(tab), "home") << "the excluded source was routed";
}

// With no rule claiming the URL, the fallback space takes it. Set the
// fallback only after the setup tab is up, or it would claim that too.
IN_PROC_BROWSER_TEST_F(AtriumRoutingMoveBrowserTest,
                       AnUnclaimedUrlOpensInTheFallbackSpace) {
  ASSERT_TRUE(embedded_test_server()->Start());
  OpenCore("Default");
  const GURL start = embedded_test_server()->GetURL("start.test", "/title1.html");
  const GURL other = embedded_test_server()->GetURL("other.test", "/title1.html");

  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), start, -1, /*foreground=*/true);
  ASSERT_TRUE(tab);
  content::WaitForLoadStopWithoutSuccessCheck(tab);
  ASSERT_EQ(SpaceIdForTab(tab), "home");

  const int before = browser()->tab_strip_model()->count();
  atrium::SetRoutingRulesJSON(kFallbackRules);
  LoadInPlace(tab, other);
  content::WaitForLoadStopWithoutSuccessCheck(tab);

  EXPECT_EQ(SpaceIdForTab(tab), "work") << "the fallback did not claim the url";
  EXPECT_EQ(browser()->tab_strip_model()->count(), before)
      << "the route added a tab instead of moving the one it was given";
  EXPECT_NE(browser()->tab_strip_model()->GetIndexOfWebContents(tab),
            TabStripModel::kNoTab)
      << "the same WebContents must still be in the strip";
  EXPECT_EQ(tab->GetLastCommittedURL(), other);
}

}  // namespace
