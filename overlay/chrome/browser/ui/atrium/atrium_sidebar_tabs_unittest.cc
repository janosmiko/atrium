// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_sidebar_tabs_unittest.cc
#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"

#include <memory>
#include <map>
#include <string>

#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/string_number_conversions.h"
#include "base/values.h"
#include "chrome/browser/metrics/desktop_session_duration/desktop_session_duration_tracker.h"
#include "chrome/browser/performance_manager/policies/discard_eligibility_policy.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/resource_coordinator/tab_helper.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_external.h"
#include "chrome/browser/resource_coordinator/tab_lifecycle_unit_source.h"
#include "chrome/browser/resource_coordinator/utils.h"
#include "chrome/browser/tab_contents/form_interaction_tab_helper.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/tabs/tab_model.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/tabs/test_tab_strip_model_delegate.h"
#include "chrome/test/base/chrome_render_view_host_test_harness.h"
#include "chrome/test/base/testing_browser_process.h"
#include "components/performance_manager/public/decorators/page_live_state_decorator.h"
#include "components/performance_manager/public/graph/graph.h"
#include "components/performance_manager/public/performance_manager.h"
#include "components/performance_manager/test_support/test_harness_helper.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/navigation_simulator.h"
#include "content/public/test/web_contents_tester.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace atrium {
namespace {

using performance_manager::PageLiveStateDecorator;
using performance_manager::PerformanceManager;
using resource_coordinator::ResourceCoordinatorTabHelper;
using resource_coordinator::TabLifecycleUnitExternal;

// The fixture, minus the Rust core: this test drives
// MergeLiveDiscarded directly against a TabStripModel, so no spaces.json is
// needed.
class AtriumSidebarTabsTest : public ChromeRenderViewHostTestHarness {
 protected:
  void SetUp() override {
    TestingBrowserProcess::GetGlobal()->SetUpGlobalFeaturesForTesting(
        /*profile_manager=*/false);
    ChromeRenderViewHostTestHarness::SetUp();
    pm_helper_.SetUp();
    PerformanceManager::GetGraph()->PassToGraph(
        std::make_unique<PageLiveStateDecorator>());
    PerformanceManager::GetGraph()->PassToGraph(
        FormInteractionTabHelper::CreateGraphObserver());
    metrics::DesktopSessionDurationTracker::Initialize();
    g_browser_process->GetTabManager();  // Forces TabLifecycleUnitSource up.
    PerformanceManager::GetGraph()->PassToGraph(
        std::make_unique<
            performance_manager::policies::DiscardEligibilityPolicy>());

    tab_strip_model_ =
        std::make_unique<TabStripModel>(&tab_strip_model_delegate_, profile());
    tab_strip_model_->AddObserver(
        resource_coordinator::GetTabLifecycleUnitSource());
  }

  void TearDown() override {
    while (!tab_strip_model_->empty()) {
      tab_strip_model_->DetachAndDeleteWebContentsAt(0);
    }
    tab_strip_model_->RemoveObserver(
        resource_coordinator::GetTabLifecycleUnitSource());
    tab_strip_model_.reset();
    metrics::DesktopSessionDurationTracker::CleanupForTesting();
    pm_helper_.TearDown();
    ChromeRenderViewHostTestHarness::TearDown();
    TestingBrowserProcess::GetGlobal()->TearDownGlobalFeaturesForTesting();
  }

  content::WebContents* AddDiscardableTab() {
    std::unique_ptr<content::WebContents> contents = CreateTestWebContents();
    content::WebContents* raw = contents.get();
    auto* tester = content::WebContentsTester::For(raw);
    tester->SetLastActiveTimeTicks(base::TimeTicks::Now());
    tester->SetLastActiveTime(base::Time::Now());
    ResourceCoordinatorTabHelper::CreateForWebContents(raw);
    auto navigation = content::NavigationSimulator::CreateBrowserInitiated(
        GURL("https://example.com"), raw);
    navigation->SetKeepLoading(true);
    navigation->Commit();
    tab_strip_model_->AppendWebContents(std::move(contents), false);
    raw->WasHidden();
    return raw;
  }

  TabStripModel* strip() { return tab_strip_model_.get(); }

 private:
  TestTabStripModelDelegate tab_strip_model_delegate_;
  const tabs::TabModel::PreventFeatureInitializationForTesting prevent_;
  std::unique_ptr<TabStripModel> tab_strip_model_;
  performance_manager::PerformanceManagerTestHarnessHelper pm_helper_;
};

// DiscardTab() destroys `asleep_tab` and swaps in a fresh WebContents at the
// same strip slot (kWebContentsDiscard is off by default outside Android) -
// the id read back afterwards is the one that now belongs there. Carrying
// the OLD id onto the replacement is a separate, real gap, not this
// function's job.
TEST_F(AtriumSidebarTabsTest, MarksTheRowOfAnActuallyDiscardedTabOnly) {
  content::WebContents* awake_tab = AddDiscardableTab();
  content::WebContents* asleep_tab = AddDiscardableTab();
  const std::string awake_id = base::NumberToString(TabId(awake_tab));

  auto* asleep_unit = TabLifecycleUnitExternal::FromWebContents(asleep_tab);
  ASSERT_TRUE(asleep_unit);
  ASSERT_TRUE(asleep_unit->DiscardTab(
      mojom::LifecycleUnitDiscardReason::EXTERNAL, 0));
  const std::string asleep_id =
      base::NumberToString(TabId(strip()->GetWebContentsAt(1)));

  const std::string model = R"({"tabs":[{"id":")" + awake_id +
                             R"("},{"id":")" + asleep_id + R"("}]})";
  const std::string merged = MergeLiveDiscarded(model, strip());

  std::optional<base::Value> parsed =
      base::JSONReader::Read(merged, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  const base::ListValue* tabs = parsed->GetDict().FindList("tabs");
  ASSERT_TRUE(tabs);
  ASSERT_EQ(2u, tabs->size());
  EXPECT_EQ(false, (*tabs)[0].GetDict().FindBool("discarded"));
  EXPECT_EQ(true, (*tabs)[1].GetDict().FindBool("discarded"));
}

TEST_F(AtriumSidebarTabsTest, ANullStripReturnsTheModelUnchanged) {
  const std::string model = R"({"tabs":[{"id":"1"}]})";
  EXPECT_EQ(model, MergeLiveDiscarded(model, nullptr));
}

// The page-side guards are cosmetic: a renderer sends whatever message it
// likes, and every one of these lands on a handler that writes the document.
TEST(AtriumIncognitoRouteGateTest, EveryDocumentWriterIsNamed) {
  for (const char* name :
       {"sidebarTabReordered", "sidebarTabShelved", "sidebarTabPinned",
        "sidebarTabMovedToSpace", "sidebarSpaceSelected", "sidebarTabArchived",
        "sidebarTabRenamed", "sidebarRestoreSlice", "sidebarTabMirrored",
        "sidebarRowFavorited", "sidebarTabSnoozed", "sidebarSpaceCreated",
        "foyerMoveToSpace", "notesBodyChanged", "shelfListAdd",
        "sidebarFolderReordered", "sidebarColdTabRemoved",
        "settingsThemeChanged", "sidebarRunMoved", "sidebarSpaceRenamed",
        "sidebarSpaceIconSet"}) {
    EXPECT_TRUE(SurfaceMessageWritesDocument(name)) << name;
  }
}

// The preview is the live scrub and stores nothing, so it must stay outside
// the list its commit is on - an incognito window may paint, not persist.
TEST(AtriumIncognitoRouteGateTest, TheThemePreviewIsNotAWrite) {
  EXPECT_FALSE(SurfaceMessageWritesDocument("settingsThemePreview"));
}

// An incognito window still has to browse. Refusing these would leave it with
// a sidebar it cannot click.
TEST(AtriumIncognitoRouteGateTest, ReadsAndWindowLocalGesturesArePassedOn) {
  for (const char* name :
       {"sidebarReady", "sidebarRowsDrawn", "sidebarTabClicked",
        "sidebarTabClosed", "sidebarNewTab", "sidebarAddressRowClicked",
        "sidebarBackPressed", "sidebarReloadPressed", "sidebarToggleRequested",
        "sidebarWindowClose", "sidebarTextFocusChanged", "pillActionClicked",
        "sidebarSpaceIconPickerOpened"}) {
    EXPECT_FALSE(SurfaceMessageWritesDocument(name)) << name;
  }
}

// The second end is the one no browsertest can reach: a slot presents another
// profile's browser, so the window and the rows it draws can disagree about
// being off the record. Both ends refuse.
TEST_F(AtriumSidebarTabsTest, EitherOffTheRecordEndRefusesTheWrite) {
  Profile* plain = profile();
  Profile* hidden = profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true);
  ASSERT_TRUE(hidden);
  ASSERT_NE(plain, hidden);

  EXPECT_TRUE(ProfilesMayWriteDocument(plain, plain));
  EXPECT_FALSE(ProfilesMayWriteDocument(hidden, plain))
      << "an incognito window showing another profile's rows";
  EXPECT_FALSE(ProfilesMayWriteDocument(plain, hidden))
      << "an ordinary window whose slot presents an incognito browser";
  EXPECT_FALSE(ProfilesMayWriteDocument(hidden, hidden));
}

// Unresolved, not private. Refusing here would stop an ordinary window
// persisting whenever its target browser has already gone.
TEST_F(AtriumSidebarTabsTest, AnUnresolvedEndDoesNotRefuseTheWrite) {
  EXPECT_TRUE(ProfilesMayWriteDocument(nullptr, nullptr));
  EXPECT_TRUE(ProfilesMayWriteDocument(profile(), nullptr));
  EXPECT_FALSE(ProfilesMayWriteDocument(
      nullptr, profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true)));
}

TEST(AtriumIncognitoRouteGateTest, AnUnknownNameIsNotTreatedAsAWrite) {
  EXPECT_FALSE(SurfaceMessageWritesDocument(""));
  EXPECT_FALSE(SurfaceMessageWritesDocument("sidebarTabReordere"));
  EXPECT_FALSE(SurfaceMessageWritesDocument("sidebarTabReorderedX"));
}

// The same strip read twice, once per profile: the rows must not move, and
// the space furniture must be gone from the off-the-record answer. Reading one
// strip through both profiles is what isolates the flag from the tab list.
TEST_F(AtriumSidebarTabsTest, AnIncognitoModelKeepsItsRowsAndLosesItsSpaces) {
  content::WebContents* tab = AddDiscardableTab();
  const std::string tab_id = base::NumberToString(TabId(tab));

  std::optional<base::Value> regular = base::JSONReader::Read(
      BuildFlatSidebarJSON(strip(), profile()), base::JSON_PARSE_RFC);
  std::optional<base::Value> incognito = base::JSONReader::Read(
      BuildFlatSidebarJSON(
          strip(), profile()->GetPrimaryOTRProfile(/*create_if_needed=*/true)),
      base::JSON_PARSE_RFC);
  ASSERT_TRUE(regular && regular->is_dict());
  ASSERT_TRUE(incognito && incognito->is_dict());

  const base::ListValue* regular_rows = regular->GetDict().FindList("tabs");
  const base::ListValue* incognito_rows = incognito->GetDict().FindList("tabs");
  ASSERT_TRUE(regular_rows && incognito_rows);
  ASSERT_EQ(1u, incognito_rows->size());
  EXPECT_EQ(tab_id, *(*incognito_rows)[0].GetDict().FindString("id"));
  EXPECT_EQ(*regular_rows, *incognito_rows)
      << "the window's own tabs are the one thing incognito must not change";

  const base::ListValue* spaces = incognito->GetDict().FindList("spaces");
  ASSERT_TRUE(spaces) << "an absent key leaves the surface guessing; empty is "
                         "the shape that says there are none";
  EXPECT_TRUE(spaces->empty());
  EXPECT_EQ(true, incognito->GetDict().FindBool("incognito"));
  EXPECT_FALSE(regular->GetDict().FindBool("incognito").has_value())
      << "an ordinary window carrying the flag would render without its rail";
}

// the tile's selection and the click both used to identify a favourite
// by URL, which a redirect moves. The key has to outlive the navigation.
TEST_F(AtriumSidebarTabsTest, AFavouriteKeySurvivesTheTabNavigatingAway) {
  content::WebContents* contents = web_contents();
  ASSERT_TRUE(contents);
  EXPECT_EQ("", FavoriteKeyForTab(contents)) << "unclaimed by default";

  SetFavoriteKeyForTab(contents, "https://fav.test/");
  EXPECT_EQ("https://fav.test/", FavoriteKeyForTab(contents));

  content::WebContentsTester::For(contents)->NavigateAndCommit(
      GURL("https://fav.test/somewhere-else"));
  EXPECT_EQ("https://fav.test/", FavoriteKeyForTab(contents))
      << "the claim must not move when the URL does";

  EXPECT_EQ("", FavoriteKeyForTab(nullptr));
  SetFavoriteKeyForTab(contents, "");
  EXPECT_EQ("https://fav.test/", FavoriteKeyForTab(contents))
      << "an empty url must not erase an existing claim";
}

// The production path applies both overlays to
// one parsed tree; the string wrappers apply one each through their own parse
// and write. This pins that the two produce THE SAME BYTES, so the refactor
// changed cost and not content - and counts the round trips both ways, which
// is the measurement.
//
// Multi-space and multi-window on purpose: a single flat tab list would pass
// even if the shared walk had lost the per-space or folder lists.
TEST(AtriumSidebarMergePipelineTest, OneParseGivesTheSameBytesAsThreeAndCostsNoRoundTrips) {
  const std::string model = R"({
    "tabs":[{"id":"11","url":"https://here.test/"},
            {"id":"","url":"https://moved.test/","elsewhereTabId":"77"},
            {"id":"","url":"https://cold.test/"}],
    "favorites":[{"id":"","url":"https://fav.test/"}],
    "pinned":[{"id":"12","url":"https://pinned.test/"},
              {"id":"","url":"https://moved.test/","elsewhereTabId":"77"}],
    "folders":[{"id":"f1","tabs":[{"id":"13","url":"https://in-folder.test/"},
                                  {"id":"","url":"https://moved.test/","elsewhereTabId":"77"}]}],
    "spaces":[
      {"id":"s1","tabs":[{"id":"11","url":"https://here.test/"},
                         {"id":"","url":"https://moved.test/","elsewhereTabId":"77"}],
       "pinned":[{"id":"12","url":"https://pinned.test/"}],
       "folders":[{"id":"f1","tabs":[{"id":"","url":"https://moved.test/","elsewhereTabId":"77"}]}]},
      {"id":"s2","tabs":[{"id":"","url":"https://moved.test/","elsewhereTabId":"77"},
                         {"id":"","url":"https://cold.test/"}],
       "pinned":[],
       "folders":[]}
    ]
  })";
  const std::string active_id = "11";
  const std::string active_favorite = "https://fav.test/";
  const std::map<std::string, bool> discarded = {{"11", false}, {"13", true}};

  // The wrappers, chained by string, as the hot path used to do it.
  const int before = SidebarMergeRoundTripsForTesting();
  std::string chained = model;
  chained = MergeThroughStringForTesting(
      std::move(chained), [&](base::DictValue& root) {
        MergeLiveSelectionInto(root, active_id, active_favorite);
      });
  chained = MergeThroughStringForTesting(
      std::move(chained), [&](base::DictValue& root) {
        MergeLiveDiscardedInto(root, discarded);
      });
  const int chained_round_trips = SidebarMergeRoundTripsForTesting() - before;
  EXPECT_EQ(2, chained_round_trips)
      << "the string-chained form is what costs a round trip per stage";

  // The production shape: parse once, every overlay, write once.
  const int before_single = SidebarMergeRoundTripsForTesting();
  std::optional<base::Value> parsed =
      base::JSONReader::Read(model, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  base::DictValue& root = parsed->GetDict();
  MergeLiveSelectionInto(root, active_id, active_favorite);
  MergeLiveDiscardedInto(root, discarded);
  std::string once;
  ASSERT_TRUE(base::JSONWriter::Write(root, &once));
  EXPECT_EQ(0, SidebarMergeRoundTripsForTesting() - before_single)
      << "no stage may parse or reserialise on the production path";

  EXPECT_EQ(chained, once)
      << "the refactor must change cost, not content";
}

TEST(AtriumSidebarUrlPolicyTest, AllowsWhatAPageCouldReachAnyway) {
  EXPECT_TRUE(IsOrdinarySidebarUrl(GURL("https://example.test/")));
  EXPECT_TRUE(IsOrdinarySidebarUrl(GURL("http://example.test/")));
  EXPECT_TRUE(IsOrdinarySidebarUrl(GURL("about:blank")));
}

// the schemes a compromised surface renderer would ask a mirror to
// load into a pane on another profile.
TEST(AtriumSidebarUrlPolicyTest, RefusesFileChromeAndExtensionSchemes) {
  EXPECT_FALSE(IsOrdinarySidebarUrl(GURL("file:///etc/passwd")));
  EXPECT_FALSE(IsOrdinarySidebarUrl(GURL("chrome://settings/")));
  EXPECT_FALSE(IsOrdinarySidebarUrl(
      GURL("chrome-extension://abcdefghijklmnopabcdefghijklmnop/x.html")));
  EXPECT_FALSE(IsOrdinarySidebarUrl(GURL("devtools://devtools/bundled/x.html")));
}

TEST(AtriumSidebarUrlPolicyTest, RefusesJavascriptAndUnparseableUrls) {
  EXPECT_FALSE(IsOrdinarySidebarUrl(GURL("javascript:alert(1)")));
  EXPECT_FALSE(IsOrdinarySidebarUrl(GURL("not a url")));
  EXPECT_FALSE(IsOrdinarySidebarUrl(GURL()));
}

// the core joins a favourite to its tab by comparing URLs exactly,
// so a favourite that redirects loses it - no id on the tile. The tab's claim
// is what still knows.
TEST(AtriumSidebarFavoriteClaimTest, AClaimedTabReachesItsTile) {
  const std::map<std::string, std::string> claims = {
      {"https://fav.test/start", "42"}};
  const std::string model = R"({
    "favorites":[{"id":"https://fav.test/start","tabId":""}],
    "tabs":[{"id":"42","url":"https://fav.test/landed"},
            {"id":"43","url":"https://other.test/"}]
  })";

  std::optional<base::Value> parsed = base::JSONReader::Read(
      MergeFavoriteClaims(model, claims), base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  const base::DictValue& root = parsed->GetDict();

  const base::ListValue* favorites = root.FindList("favorites");
  ASSERT_TRUE(favorites && favorites->size() == 1u);
  const std::string* carried = (*favorites)[0].GetDict().FindString("tabId");
  ASSERT_TRUE(carried) << "without the id the click has nothing to activate";
  EXPECT_EQ("42", *carried);
}

// The other half of this stage changed. It used to erase the claimed
// tab's row, and that shifted every gesture past it: the surface counts the
// rows it was sent, the core resolves an index against its own list. Dragging
// a tab onto the pinned strip pinned a different one, and a favourite dragged
// out left its page with no row at all, because the tab kept the claim.
TEST(AtriumSidebarFavoriteClaimTest, AClaimedTabKeepsItsRow) {
  const std::map<std::string, std::string> claims = {
      {"https://fav.test/start", "42"}};
  const std::string model = R"({
    "favorites":[{"id":"https://fav.test/start","tabId":""}],
    "tabs":[{"id":"42","url":"https://fav.test/landed"},
            {"id":"43","url":"https://other.test/"}]
  })";

  std::optional<base::Value> parsed = base::JSONReader::Read(
      MergeFavoriteClaims(model, claims), base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  const base::ListValue* tabs = parsed->GetDict().FindList("tabs");
  ASSERT_TRUE(tabs);
  ASSERT_EQ(2u, tabs->size()) << "no row may leave, whatever is claimed";
  EXPECT_EQ("42", *(*tabs)[0].GetDict().FindString("id"));
  EXPECT_EQ("43", *(*tabs)[1].GetDict().FindString("id"));
}

// A tab nobody claimed is an ordinary tab, and a tile nobody claimed keeps the
// id the core gave it.
TEST(AtriumSidebarFavoriteClaimTest, AnUnclaimedTabKeepsItsRowAndItsTileStaysEmpty) {
  const std::map<std::string, std::string> claims = {
      {"https://fav.test/start", "42"}};
  const std::string model = R"({
    "favorites":[{"id":"https://other-fav.test/","tabId":""}],
    "tabs":[{"id":"43","url":"https://other.test/"}]
  })";

  std::optional<base::Value> parsed = base::JSONReader::Read(
      MergeFavoriteClaims(model, claims), base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  const base::DictValue& root = parsed->GetDict();
  EXPECT_EQ("", *(*root.FindList("favorites"))[0].GetDict().FindString("tabId"));
  EXPECT_EQ(1u, root.FindList("tabs")->size());
}

// Correcting only the top-level list left the same tile lit in the space's own
// copy, so a pane reading that copy drew a tile whose page the window had lost.
TEST(AtriumSidebarFavoriteSelectionTest, EverySpacesTilesAreMarkedToo) {
  const std::string model = R"({
    "favorites":[{"id":"https://fav.test/","selected":true}],
    "spaces":[
      {"favorites":[{"id":"https://fav.test/","selected":true}]},
      {"favorites":[{"id":"https://fav.test/","selected":false}]}
    ]
  })";

  std::optional<base::Value> parsed =
      base::JSONReader::Read(model, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  base::DictValue& root = parsed->GetDict();
  MergeLiveSelectionInto(root, "43", "https://other.test/");

  auto tile_selected = [](const base::ListValue* tiles) {
    return tiles && tiles->size() == 1u &&
           (*tiles)[0].GetDict().FindBool("selected").value_or(false);
  };
  EXPECT_FALSE(tile_selected(root.FindList("favorites"))) << "top level";
  const base::ListValue* spaces = root.FindList("spaces");
  ASSERT_TRUE(spaces && spaces->size() == 2u);
  EXPECT_FALSE(tile_selected((*spaces)[0].GetDict().FindList("favorites")))
      << "the space whose copy said selected";
  EXPECT_FALSE(tile_selected((*spaces)[1].GetDict().FindList("favorites")))
      << "the space whose copy already agreed";
}

// A pinned folder's members sit in the folder entry's own "tabs" list.
TEST(AtriumSidebarFavoriteSelectionTest, APinnedFoldersMemberIsMarkedSelected) {
  const std::string model = R"({
    "pinned":[{"kind":"folder","id":"G","tabs":[{"id":"9","selected":false}]}]
  })";

  std::optional<base::Value> parsed =
      base::JSONReader::Read(model, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  base::DictValue& root = parsed->GetDict();
  MergeLiveSelectionInto(root, "9", std::string());

  const base::ListValue* pinned = root.FindList("pinned");
  ASSERT_TRUE(pinned && pinned->size() == 1u);
  const base::ListValue* member_tabs = (*pinned)[0].GetDict().FindList("tabs");
  ASSERT_TRUE(member_tabs && member_tabs->size() == 1u);
  EXPECT_TRUE((*member_tabs)[0].GetDict().FindBool("selected").value_or(false));
}

// Closing a favourite's tab can leave the window with no active tab at all.
// Empty is a real answer here - nothing is selected - and the tile stayed lit
// over a page that was gone while it read as "no answer, keep what you have".
TEST(AtriumSidebarFavoriteSelectionTest, NoActiveTabClearsEveryTileAndRow) {
  const std::string model = R"({
    "tabs":[{"id":"42","url":"https://fav.test/","selected":true}],
    "favorites":[{"id":"https://fav.test/","selected":true}],
    "spaces":[{"favorites":[{"id":"https://fav.test/","selected":true}]}]
  })";

  std::optional<base::Value> parsed =
      base::JSONReader::Read(model, base::JSON_PARSE_RFC);
  ASSERT_TRUE(parsed && parsed->is_dict());
  base::DictValue& root = parsed->GetDict();
  MergeLiveSelectionInto(root, std::string(), std::string());

  EXPECT_FALSE(
      (*root.FindList("tabs"))[0].GetDict().FindBool("selected").value_or(false))
      << "the row";
  EXPECT_FALSE((*root.FindList("favorites"))[0]
                   .GetDict()
                   .FindBool("selected")
                   .value_or(false))
      << "the tile";
  EXPECT_FALSE((*(*root.FindList("spaces"))[0].GetDict().FindList("favorites"))[0]
                   .GetDict()
                   .FindBool("selected")
                   .value_or(false))
      << "the space's copy of the tile";
}

// A favourite that redirects on load lands on a URL the tile does not carry.
// The first commit is the one that counts.
TEST_F(AtriumSidebarTabsTest, AFavouriteKeepsTheFirstPageItCommittedAsItsLanding) {
  content::WebContents* contents = web_contents();
  SetFavoriteKeyForTab(contents, "https://youtube.test/");
  EXPECT_EQ(GURL(), FavoriteLandingForTab(contents)) << "nothing committed yet";

  NoteFavoriteLandingForTab(contents, GURL("https://www.youtube.test/"));
  NoteFavoriteLandingForTab(contents, GURL("https://www.youtube.test/watch"));
  EXPECT_EQ(GURL("https://www.youtube.test/"), FavoriteLandingForTab(contents))
      << "a later commit moved the landing";
}

TEST_F(AtriumSidebarTabsTest, AnUnclaimedTabRecordsNoLanding) {
  content::WebContents* contents = web_contents();
  NoteFavoriteLandingForTab(contents, GURL("https://fav.test/"));
  EXPECT_EQ(GURL(), FavoriteLandingForTab(contents));
  EXPECT_EQ(GURL(), FavoriteLandingForTab(nullptr));
}

// An initial empty document's PrimaryPageChanged must not latch the landing.
TEST_F(AtriumSidebarTabsTest, AnEmptyFirstDocumentDoesNotBecomeTheLanding) {
  content::WebContents* contents = web_contents();
  SetFavoriteKeyForTab(contents, "https://fav.test/");

  NoteFavoriteLandingForTab(contents, GURL("about:blank"));
  EXPECT_EQ(GURL(), FavoriteLandingForTab(contents));

  NoteFavoriteLandingForTab(contents, GURL("https://www.example.com/"));
  EXPECT_EQ(GURL("https://www.example.com/"), FavoriteLandingForTab(contents));
}

// A favourited row claims a tab that already shows its page.
TEST_F(AtriumSidebarTabsTest, ATabClaimedAfterItsFirstCommitHasLandedThere) {
  content::WebContents* contents = web_contents();
  content::WebContentsTester::For(contents)->NavigateAndCommit(
      GURL("https://fav.test/app"));
  SetFavoriteKeyForTab(contents, "https://fav.test/app");
  EXPECT_EQ(GURL("https://fav.test/app"), FavoriteLandingForTab(contents));
}

TEST(AtriumFavoriteAtHomeTest, TheSavedUrlIsHome) {
  EXPECT_TRUE(IsFavoriteAtHome(GURL("https://fav.test/app"),
                               GURL("https://fav.test/app"), GURL()));
}

TEST(AtriumFavoriteAtHomeTest, ATrailingSlashTheSavedUrlLacksIsStillHome) {
  EXPECT_TRUE(IsFavoriteAtHome(GURL("https://fav.test/"),
                               GURL("https://fav.test"), GURL()));
}

TEST(AtriumFavoriteAtHomeTest, TheRedirectLandingIsHome) {
  EXPECT_TRUE(IsFavoriteAtHome(GURL("https://www.youtube.test/"),
                               GURL("https://youtube.test/"),
                               GURL("https://www.youtube.test/")));
}

TEST(AtriumFavoriteAtHomeTest, AFragmentDoesNotLeaveHome) {
  EXPECT_TRUE(IsFavoriteAtHome(GURL("https://fav.test/app#inbox"),
                               GURL("https://fav.test/app"), GURL()));
  EXPECT_TRUE(IsFavoriteAtHome(GURL("https://www.youtube.test/#top"),
                               GURL("https://youtube.test/"),
                               GURL("https://www.youtube.test/")));
}

TEST(AtriumFavoriteAtHomeTest, ADifferentPathIsNotHome) {
  EXPECT_FALSE(IsFavoriteAtHome(GURL("https://fav.test/app/settings"),
                                GURL("https://fav.test/app"),
                                GURL("https://fav.test/app")));
}

TEST(AtriumFavoriteAtHomeTest, ADifferentQueryIsNotHome) {
  EXPECT_FALSE(IsFavoriteAtHome(GURL("https://fav.test/app?tab=2"),
                                GURL("https://fav.test/app"),
                                GURL("https://fav.test/app")));
}

}  // namespace
}  // namespace atrium
