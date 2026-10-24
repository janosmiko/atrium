// Copyright 2026 The Atrium Authors

#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/scoped_environment_variable_override.h"
#include "base/strings/stringprintf.h"
#include "base/test/scoped_feature_list.h"
#include "base/token.h"
#include "base/values.h"
#include "content/public/common/content_features.h"
#include "chrome/browser/resource_coordinator/lifecycle_unit_state.mojom.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
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

// proves the kReplaced row-carry end to end against a real discard,
// not just the Rust-only pieces. Nonce per test: a stale write from an
// earlier run once let a browsertest here pass against leftover state.
namespace {

class AtriumDiscardTabRowContinuityBrowserTest : public InProcessBrowserTest {
 public:
  AtriumDiscardTabRowContinuityBrowserTest() {
    // fieldtrial_testing_config.json enables WebContentsDiscard for mac in
    // browser_tests, the opposite of the shipping default this test needs.
    // Overrides must land in the constructor, before BrowserTestBase::SetUp().
    scoped_feature_list_.InitAndDisableFeature(features::kWebContentsDiscard);
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

std::string WriteTwoSpaceFixture(const base::FilePath& path,
                                 const std::string& work_url,
                                 const std::string& work_custom_title,
                                 const std::string& home_marker_url,
                                 const std::string& home_marker_title) {
  static constexpr char kTemplate[] =
      R"({"spaces":[)"
      R"({"id":"work","name":"Work","emoji":"W","tint":"#111111",)"
      R"("profileID":"Default","pinnedTabs":[],)"
      R"("tabs":[{"url":"%s","title":"cold row title","customTitle":"%s"}],)"
      R"("order":0,"groups":[]},)"
      R"({"id":"home","name":"Home","emoji":"H","tint":"#222222",)"
      R"("profileID":"Default","pinnedTabs":[],)"
      R"("tabs":[{"url":"%s","title":"%s"}],"order":1,"groups":[]})"
      R"(],"appState":{"activeSpaceID":"work","sidebarWidth":280,)"
      R"("schemaVersion":3,"archiveEpoch":1,"sidebarWidthMigrated":true}})";
  std::string json = base::StringPrintf(
      kTemplate, work_url.c_str(), work_custom_title.c_str(),
      home_marker_url.c_str(), home_marker_title.c_str());
  base::ScopedAllowBlockingForTesting allow_blocking;
  CHECK(base::WriteFile(path, json));
  return json;
}

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

// A tab the sidebar sees as Work's only live row, discarded for real, still
// resolves to Work's only row afterward - and Home (never touched) is
// untouched, proving the swap did not leak the row across spaces either.
IN_PROC_BROWSER_TEST_F(AtriumDiscardTabRowContinuityBrowserTest,
                       ARealDiscardCarriesTheRowToTheReplacementWebContents) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const std::string nonce = base::Token::CreateRandom().ToString();
  const std::string home_marker_url = base::StringPrintf(
      "http://home.marker.test/%s", nonce.c_str());
  const std::string home_marker_title =
      base::StringPrintf("Home Marker %s", nonce.c_str());
  const GURL work_url = embedded_test_server()->GetURL(
      base::StringPrintf("/title1.html?discard-%s", nonce.c_str()));
  // The row this custom title lives on is not renamed after discard, so the
  // mutation test (drop the rekey call) can tell a carried-over row from a
  // freshly-added one: only the fresh one lacks this title.
  const std::string work_custom_title =
      base::StringPrintf("Work Custom Title %s", nonce.c_str());

  atrium_core_close();
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath spaces_path = dir.GetPath().AppendASCII("spaces.json");
  WriteTwoSpaceFixture(spaces_path, work_url.spec(), work_custom_title,
                       home_marker_url, home_marker_title);
  ASSERT_TRUE(atrium_core_open(spaces_path.value().c_str(), 1.0));

  // Real tab-open path, so BrowserController::AddTab's production hook binds
  // the row - not a shortcut this test invents. The URL matches the cold row
  // the fixture just wrote, so this ADOPTS it rather than adding a new one.
  content::WebContents* work_tab =
      chrome::AddAndReturnTabAt(browser(), work_url, -1, /*foreground=*/true);
  ASSERT_TRUE(work_tab);
  ASSERT_TRUE(content::WaitForLoadStop(work_tab));
  // No pre-fixture tab to drop: an atrium window opens with none (see
  // BrowserController's ctor). The count is the witness that the add landed.
  TabStripModel* strip = browser()->tab_strip_model();
  ASSERT_EQ(1, strip->count());
  const int slot = strip->GetIndexOfWebContents(work_tab);
  ASSERT_NE(TabStripModel::kNoTab, slot);

  const base::Value before = SidebarModel(browser());
  const base::DictValue* work_chip_before = FindSpaceChip(before, "Work");
  ASSERT_TRUE(work_chip_before);
  const base::ListValue* work_rows_before = work_chip_before->FindList("tabs");
  ASSERT_TRUE(work_rows_before);
  ASSERT_EQ(1u, work_rows_before->size())
      << "the tab this test just opened has no row in Work yet";

  const base::DictValue* home_chip_before = FindSpaceChip(before, "Home");
  ASSERT_TRUE(home_chip_before);
  const base::Value home_chip_before_copy(home_chip_before->Clone());

  auto* unit =
      resource_coordinator::TabLifecycleUnitExternal::FromWebContents(
          work_tab);
  ASSERT_TRUE(unit) << "no TabLifecycleUnit for a real tab in a real Browser";
  unit->SetAutoDiscardable(true);
  ASSERT_TRUE(unit->DiscardTab(
      mojom::LifecycleUnitDiscardReason::EXTERNAL))
      << "DiscardTab refused - nothing to prove the id-continuity fix against";

  // kWebContentsDiscard is off by default outside Android, so this took the
  // destroy-and-swap path (TabLifecycleUnit::FinishDiscard).
  content::WebContents* replacement = strip->GetWebContentsAt(slot);
  ASSERT_TRUE(replacement);
  EXPECT_NE(replacement, work_tab)
      << "WebContents was not actually swapped - this run did not exercise "
         "the destroy-and-swap path this test is about";

  const base::Value after = SidebarModel(browser());
  const base::DictValue* work_chip_after = FindSpaceChip(after, "Work");
  ASSERT_TRUE(work_chip_after);
  const base::ListValue* work_rows_after = work_chip_after->FindList("tabs");
  ASSERT_TRUE(work_rows_after);
  EXPECT_EQ(1u, work_rows_after->size())
      << "the discard either dropped the row or duplicated it instead of "
         "carrying it onto the replacement WebContents";

  const std::string replacement_id =
      base::NumberToString(atrium::TabId(replacement));
  const std::string* row_id_after =
      (*work_rows_after)[0].GetDict().FindString("id");
  ASSERT_TRUE(row_id_after);
  EXPECT_EQ(replacement_id, *row_id_after)
      << "Work's surviving row does not resolve to the tab that is actually "
         "on screen now - the sidebar would show a stale or orphaned row";

  // The row carried over is the SAME row (with its customTitle), not a fresh
  // one dropping and re-adding would create with the page's own title.
  const std::string* title_after = (*work_rows_after)[0].GetDict().FindString(
      "title");
  ASSERT_TRUE(title_after);
  EXPECT_EQ(work_custom_title, *title_after)
      << "Work's row lost its customTitle across the discard - it was "
         "dropped and re-added as a fresh row instead of carried over";

  // ISOLATION: Home was never touched by this discard, so its chip must be
  // byte-identical to what it was before - the swap must not have leaked
  // the row into (or reset the binding against) the other space.
  const base::DictValue* home_chip_after = FindSpaceChip(after, "Home");
  ASSERT_TRUE(home_chip_after);
  EXPECT_EQ(home_chip_before_copy, base::Value(home_chip_after->Clone()))
      << "discarding Work's tab touched Home - isolation broken";
}

}  // namespace
