// Copyright 2026 The Atrium Authors

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/strings/stringprintf.h"
#include "base/threading/thread_restrictions.h"
#include "base/token.h"
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
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "url/gurl.h"

// Drives RestoreSidebarSlice against a real Browser.
// Nonce per test: a stale write from an earlier run once let a browsertest
// here pass with its handler disabled.
namespace {

using AtriumRestoreSidebarSliceBrowserTest = InProcessBrowserTest;

std::string WriteTwoSpaceFixture(const base::FilePath& path,
                                 const std::string& home_marker_url,
                                 const std::string& home_marker_title) {
  static constexpr char kTemplate[] =
      R"({"spaces":[)"
      R"({"id":"work","name":"Work","emoji":"W","tint":"#111111",)"
      R"("profileID":"Default","pinnedTabs":[],"tabs":[],)"
      R"("order":0,"groups":[]},)"
      R"({"id":"home","name":"Home","emoji":"H","tint":"#222222",)"
      R"("profileID":"Default","pinnedTabs":[],)"
      R"("tabs":[{"url":"%s","title":"%s"}],"order":1,"groups":[]})"
      R"(],"appState":{"activeSpaceID":"work","sidebarWidth":280,)"
      R"("schemaVersion":3,"archiveEpoch":1,"sidebarWidthMigrated":true}})";
  std::string json = base::StringPrintf(kTemplate, home_marker_url.c_str(),
                                        home_marker_title.c_str());
  base::ScopedAllowBlockingForTesting allow_blocking;
  CHECK(base::WriteFile(path, json));
  return json;
}

// Finds the sidebar model's chip for `space_id` ("work" or "home" in every
// fixture this file writes) in the JSON BuildSidebarTabsJSON returns.
// SpaceChip (the sidebar's rendered shape) carries no space id, only its
// display name - "Work" and "Home" in every fixture this file writes.
const base::DictValue* FindSpaceChip(const base::Value& model,
                                     std::string_view space_name) {
  const base::ListValue* spaces = model.GetDict().FindList("spaces");
  if (!spaces) {
    return nullptr;
  }
  for (const base::Value& chip : *spaces) {
    if (const std::string* name = chip.GetDict().FindString("name");
        name && *name == space_name) {
      return &chip.GetDict();
    }
  }
  return nullptr;
}

base::Value SidebarModel(Browser* browser) {
  std::optional<base::Value> parsed = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(browser), base::JSON_PARSE_RFC);
  CHECK(parsed) << "BuildSidebarTabsJSON did not return valid JSON";
  return std::move(*parsed);
}

void SendRestoreSlice(Browser* browser, const std::string& payload_json) {
  auto* window =
      static_cast<atrium::AtriumBrowserWindow*>(browser->window());
  base::ListValue args;
  args.Append(payload_json);
  window->OnSurfaceMessage("sidebarRestoreSlice", args);
}

// One row names the already-open tab's live id (must
// MOVE), the other names an unopened URL (must REOPEN); Home is the
// isolation witness.
IN_PROC_BROWSER_TEST_F(AtriumRestoreSidebarSliceBrowserTest,
                       PerSpaceRestoreMovesLiveRowsAndLeavesTheOtherSpaceAlone) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const std::string nonce = base::Token::CreateRandom().ToString();
  const std::string home_marker_url = base::StringPrintf(
      "http://home.marker.test/%s", nonce.c_str());
  const std::string home_marker_title =
      base::StringPrintf("Home Marker %s", nonce.c_str());

  atrium_core_close();
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath spaces_path = dir.GetPath().AppendASCII("spaces.json");
  WriteTwoSpaceFixture(spaces_path, home_marker_url, home_marker_title);
  ASSERT_TRUE(atrium_core_open(spaces_path.value().c_str(), 1.0));

  // Real tab-open path, so BrowserController::AddTab's production hook binds
  // the row - not a shortcut this test invents.
  const GURL work_url = embedded_test_server()->GetURL(
      base::StringPrintf("/title1.html?work-%s", nonce.c_str()));
  content::WebContents* work_tab =
      chrome::AddAndReturnTabAt(browser(), work_url, -1, /*foreground=*/true);
  ASSERT_TRUE(work_tab);
  ASSERT_TRUE(content::WaitForLoadStop(work_tab));
  // No pre-fixture tab to drop: an atrium window opens with none (see
  // BrowserController's ctor). The count is the witness that the add landed.
  ASSERT_EQ(1, browser()->tab_strip_model()->count());

  const base::Value before = SidebarModel(browser());
  const base::DictValue* work_chip_before = FindSpaceChip(before, "Work");
  ASSERT_TRUE(work_chip_before);
  const base::ListValue* work_rows_before =
      work_chip_before->FindList("tabs");
  ASSERT_TRUE(work_rows_before);
  ASSERT_EQ(1u, work_rows_before->size());
  const std::string live_row_id =
      *(*work_rows_before)[0].GetDict().FindString("id");
  ASSERT_FALSE(live_row_id.empty())
      << "the tab this test just opened has no live row id to move";

  const base::DictValue* home_chip_before = FindSpaceChip(before, "Home");
  ASSERT_TRUE(home_chip_before);
  const base::Value home_chip_before_copy(home_chip_before->Clone());

  const GURL new_work_url = embedded_test_server()->GetURL(
      base::StringPrintf("/title2.html?reopened-%s", nonce.c_str()));
  // "999999999" stands for a tab id the moment saw live but that tab is gone
  // now - an empty id means "already cold in the moment", which note_reopen
  // (restore.rs) never reopens at all.
  const std::string payload = base::StringPrintf(
      R"({"scope":"0","activeSpace":0,"spaces":[{"index":0,"name":"Work",)"
      R"("pinned":[],"tabs":[)"
      R"({"id":"%s","url":"%s","title":"still open","selected":false},)"
      R"({"id":"999999999","url":"%s","title":"reopened %s","selected":true})"
      R"(],"folders":[],"favorites":[],"pinnedCollapsed":false}]})",
      live_row_id.c_str(), work_url.spec().c_str(), new_work_url.spec().c_str(),
      nonce.c_str());

  SendRestoreSlice(browser(), payload);

  // MOVE, not reopen: the same WebContents/tab survives at whatever index the
  // strip settles on. Reopening it as a second tab is exactly the defect
  // this test exists to rule out.
  TabStripModel* strip = browser()->tab_strip_model();
  ASSERT_EQ(2, strip->count())
      << "expected the still-open row moved plus one new tab reopened for "
         "the row with no live id";
  bool original_tab_survives = false;
  for (int i = 0; i < strip->count(); ++i) {
    if (strip->GetWebContentsAt(i) == work_tab) {
      original_tab_survives = true;
    }
  }
  EXPECT_TRUE(original_tab_survives)
      << "the row named by its live id was reopened as a new tab instead of "
         "moved - the same defect this test exists to catch";

  // ISOLATION, the deliverable: Home was never named by this payload's scope,
  // so its chip - cold rows, never live - must be byte-identical to what it
  // was before the restore.
  const base::Value after = SidebarModel(browser());
  const base::DictValue* home_chip_after = FindSpaceChip(after, "Home");
  ASSERT_TRUE(home_chip_after);
  EXPECT_EQ(home_chip_before_copy, base::Value(home_chip_after->Clone()))
      << "a per-space restore of Work touched Home - isolation broken";
}

// an unknown space index is refused whole. Round-trips the
// ENTIRE sidebar model, not one field - a half-applied write would not show
// in a narrower check.
IN_PROC_BROWSER_TEST_F(AtriumRestoreSidebarSliceBrowserTest,
                       RestoreNamingAMissingSpaceRefusesAndChangesNothing) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const std::string nonce = base::Token::CreateRandom().ToString();
  const std::string home_marker_url = base::StringPrintf(
      "http://home.marker.test/%s", nonce.c_str());
  const std::string home_marker_title =
      base::StringPrintf("Home Marker %s", nonce.c_str());

  atrium_core_close();
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath spaces_path = dir.GetPath().AppendASCII("spaces.json");
  WriteTwoSpaceFixture(spaces_path, home_marker_url, home_marker_title);
  ASSERT_TRUE(atrium_core_open(spaces_path.value().c_str(), 1.0));

  const GURL work_url = embedded_test_server()->GetURL(
      base::StringPrintf("/title1.html?work-%s", nonce.c_str()));
  content::WebContents* work_tab =
      chrome::AddAndReturnTabAt(browser(), work_url, -1, /*foreground=*/true);
  ASSERT_TRUE(content::WaitForLoadStop(work_tab));
  ASSERT_EQ(1, browser()->tab_strip_model()->count());

  const base::Value before = SidebarModel(browser());

  // Space index 7 does not exist - only 0 ("work") and 1 ("home") do.
  static constexpr char kPayload[] =
      R"({"scope":"7","activeSpace":0,"spaces":[{"index":7,"name":"Ghost",)"
      R"("pinned":[],"tabs":[{"id":"","url":"https://example.com/ghost",)"
      R"("title":"ghost","selected":false}],"folders":[],"favorites":[],)"
      R"("pinnedCollapsed":false}]})";

  SendRestoreSlice(browser(), kPayload);

  EXPECT_EQ(1, browser()->tab_strip_model()->count())
      << "a refused restore opened or closed a tab - it must change nothing";
  EXPECT_EQ(browser()->tab_strip_model()->GetWebContentsAt(0), work_tab)
      << "a refused restore replaced the live tab instead of leaving it alone";

  const base::Value after = SidebarModel(browser());
  EXPECT_EQ(before, after)
      << "a refused restore (unknown space index) changed the sidebar model - "
         "the document must be byte-identical to before";
}

// RestoreSidebarSlice never switches spaces before applying
// its plan, so a background space's reopen entry may land on whatever strip
// is currently visible instead of staying with the space it belongs to.
IN_PROC_BROWSER_TEST_F(
    AtriumRestoreSidebarSliceBrowserTest,
    WholeWindowRestoreDoesNotLeakABackgroundSpacesPlanOntoTheVisibleStrip) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const std::string nonce = base::Token::CreateRandom().ToString();
  const std::string home_marker_url = base::StringPrintf(
      "http://home.marker.test/%s", nonce.c_str());
  const std::string home_marker_title =
      base::StringPrintf("Home Marker %s", nonce.c_str());

  atrium_core_close();
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath spaces_path = dir.GetPath().AppendASCII("spaces.json");
  WriteTwoSpaceFixture(spaces_path, home_marker_url, home_marker_title);
  ASSERT_TRUE(atrium_core_open(spaces_path.value().c_str(), 1.0));

  // Work (space 0) is active and gets one real live tab, same production hook
  // as the other tests here.
  const GURL work_url = embedded_test_server()->GetURL(
      base::StringPrintf("/title1.html?work-%s", nonce.c_str()));
  content::WebContents* work_tab =
      chrome::AddAndReturnTabAt(browser(), work_url, -1, /*foreground=*/true);
  ASSERT_TRUE(work_tab);
  ASSERT_TRUE(content::WaitForLoadStop(work_tab));
  ASSERT_EQ(1, browser()->tab_strip_model()->count());

  const base::Value before = SidebarModel(browser());
  const base::DictValue* work_chip_before = FindSpaceChip(before, "Work");
  ASSERT_TRUE(work_chip_before);
  const base::ListValue* work_rows_before = work_chip_before->FindList("tabs");
  ASSERT_TRUE(work_rows_before);
  ASSERT_EQ(1u, work_rows_before->size());
  const std::string live_row_id =
      *(*work_rows_before)[0].GetDict().FindString("id");
  ASSERT_FALSE(live_row_id.empty());

  // Home's row carries a synthetic, never-live id, so note_reopen (restore.rs)
  // cannot tell it from a genuinely gone tab and emits a Reopen entry for it.
  const GURL home_reopen_url = embedded_test_server()->GetURL(
      base::StringPrintf("/title2.html?home-reopen-%s", nonce.c_str()));
  const std::string payload = base::StringPrintf(
      R"({"scope":"all","activeSpace":0,"spaces":[)"
      R"({"index":0,"name":"Work","pinned":[],"tabs":[)"
      R"({"id":"%s","url":"%s","title":"still open","selected":true})"
      R"(],"folders":[],"favorites":[],"pinnedCollapsed":false},)"
      R"({"index":1,"name":"Home","pinned":[],"tabs":[)"
      R"({"id":"888888888","url":"%s","title":"home reopened %s",)"
      R"("selected":false})"
      R"(],"folders":[],"favorites":[],"pinnedCollapsed":false})"
      R"(]})",
      live_row_id.c_str(), work_url.spec().c_str(),
      home_reopen_url.spec().c_str(), nonce.c_str());

  SendRestoreSlice(browser(), payload);

  // browser()->tab_strip_model() is the strip the user actually sees - Home
  // is not showing right now.
  TabStripModel* strip = browser()->tab_strip_model();
  EXPECT_EQ(1, strip->count())
      << "a background space's (Home's) reopen entry was applied to the "
         "currently visible (Work) strip - the leak this task tests for";
  for (int i = 0; i < strip->count(); ++i) {
    content::WebContents* tab = strip->GetWebContentsAt(i);
    ASSERT_TRUE(tab);
    EXPECT_NE(home_reopen_url, tab->GetLastCommittedURL())
        << "Home's reopened tab was opened into the visible Work strip";
  }
}

// RestoreSidebarSlice only opens reopen entries for
// plan.active_space, so a background space's rows are
// written cold and unproven until the user switches there. Drives that
// switch via BrowserController::SelectSpace - the rail's own path - so a
// mismatch between what apply() wrote and what the live strip shows cannot
// hide behind reading the document directly.
IN_PROC_BROWSER_TEST_F(
    AtriumRestoreSidebarSliceBrowserTest,
    WholeWindowRestoreReinstatesTabsSelectionAndActiveSpaceAcrossASwitch) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const std::string nonce = base::Token::CreateRandom().ToString();
  const std::string home_marker_url = base::StringPrintf(
      "http://home.marker.test/%s", nonce.c_str());
  const std::string home_marker_title =
      base::StringPrintf("Home Marker %s", nonce.c_str());

  atrium_core_close();
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath spaces_path = dir.GetPath().AppendASCII("spaces.json");
  WriteTwoSpaceFixture(spaces_path, home_marker_url, home_marker_title);
  ASSERT_TRUE(atrium_core_open(spaces_path.value().c_str(), 1.0));

  // Work (space 0) is active with one real, live tab - the pre-restore state
  // the moment is about to overwrite.
  const GURL work_before_url = embedded_test_server()->GetURL(
      base::StringPrintf("/title1.html?work-before-%s", nonce.c_str()));
  content::WebContents* work_before_tab = chrome::AddAndReturnTabAt(
      browser(), work_before_url, -1, /*foreground=*/true);
  ASSERT_TRUE(work_before_tab);
  ASSERT_TRUE(content::WaitForLoadStop(work_before_tab));
  ASSERT_EQ(1, browser()->tab_strip_model()->count());

  // The moment: Work gets a DIFFERENT tab than the one live above, Home gets
  // a DIFFERENT tab than its fixture marker, and the window ends up showing
  // Home - none of the three (Work's tabs, Home's tabs, the active space)
  // match what is live right now.
  const GURL work_after_url = embedded_test_server()->GetURL(
      base::StringPrintf("/title1.html?work-after-%s", nonce.c_str()));
  const GURL home_after_url = embedded_test_server()->GetURL(
      base::StringPrintf("/title2.html?home-after-%s", nonce.c_str()));
  // Non-empty, never-live ids: an empty id tells note_reopen (restore.rs)
  // "already cold in the moment," which never produces a reopen entry at
  // all - exactly the two rows this test needs one for.
  const std::string payload = base::StringPrintf(
      R"({"scope":"all","activeSpace":1,"spaces":[)"
      R"({"index":0,"name":"Work","pinned":[],"tabs":[)"
      R"({"id":"111111111","url":"%s","title":"work after","selected":false})"
      R"(],"folders":[],"favorites":[],"pinnedCollapsed":false},)"
      R"({"index":1,"name":"Home","pinned":[],"tabs":[)"
      R"({"id":"222222222","url":"%s","title":"home after %s","selected":true})"
      R"(],"folders":[],"favorites":[],"pinnedCollapsed":false})"
      R"(]})",
      work_after_url.spec().c_str(), home_after_url.spec().c_str(),
      nonce.c_str());

  SendRestoreSlice(browser(), payload);

  // The document's active space is the moment's, not whatever was live when
  // the message arrived.
  EXPECT_EQ(1, atrium_core_active_space_index())
      << "the restore did not reinstate the moment's active space";

  // The window ends up showing Home (activeSpace:1): its restored tab is the
  // only thing live, and it is the NEW url, not the fixture marker. Work's
  // restored row stays cold - a space switch here (as opposed to a first
  // restore) never closes another space's tabs (see SelectSpace's own
  // browsertest coverage), so nothing else should have opened yet.
  TabStripModel* strip = browser()->tab_strip_model();
  ASSERT_EQ(1, strip->count())
      << "restore should have closed Work's stale live tab and opened only "
         "the active space's (Home's) restored one";
  content::WebContents* home_tab = strip->GetWebContentsAt(0);
  ASSERT_TRUE(content::WaitForLoadStop(home_tab));
  EXPECT_EQ(home_after_url, home_tab->GetLastCommittedURL())
      << "the tab left live after a whole-window restore is not the active "
         "space's restored tab";

  const base::Value after_restore = SidebarModel(browser());
  const base::DictValue* work_chip = FindSpaceChip(after_restore, "Work");
  ASSERT_TRUE(work_chip);
  const base::ListValue* work_rows = work_chip->FindList("tabs");
  ASSERT_TRUE(work_rows);
  ASSERT_EQ(1u, work_rows->size());
  EXPECT_EQ(work_after_url.spec(), *(*work_rows)[0].GetDict().FindString("url"))
      << "Work's own slice was not rewritten to the moment's tab while it "
         "was in the background";

  // Now switch TO Work - the space that was in the background for the whole
  // restore above - the same way the rail does it. SelectSpace brings the
  // space's first row forward. It does not close what was already open.
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  ASSERT_TRUE(controller->SelectSpace(0));
  EXPECT_EQ(0, atrium_core_active_space_index());

  content::WebContents* active = strip->GetActiveWebContents();
  ASSERT_TRUE(active);
  ASSERT_TRUE(content::WaitForLoadStop(active));
  EXPECT_EQ(work_after_url, active->GetLastCommittedURL())
      << "Work's row, cold-written by the whole-window restore while it was "
         "in the background, did not come back correctly once visited";
}

}  // namespace
