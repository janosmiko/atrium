// Copyright 2026 The Atrium Authors

#include <memory>
#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/stringprintf.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/search_engines/template_url_service_factory.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/search_test_utils.h"
#include "components/search_engines/template_url.h"
#include "components/search_engines/template_url_data.h"
#include "components/search_engines/template_url_service.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/http/http_status_code.h"
#include "net/test/embedded_test_server/controllable_http_response.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "url/gurl.h"

// A favourite is paired with its live tab by exact URL, which a
// page that redirects on load has already moved. The chain is the case that
// bites: app.clockify.me/calendar went to /login, then to another origin.
namespace {

class AtriumFavoriteRedirectBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    // Named hosts, so a hop can be genuinely cross-origin rather than another
    // path on 127.0.0.1.
    host_resolver()->AddRule("*", "127.0.0.1");
    InProcessBrowserTest::SetUpOnMainThread();
  }

  // One space and one favourite, with no tab row of its own - the row the
  // assertions are about is the one the click creates.
  void OpenCoreWithFavorite(const std::string& favorite_url) {
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
        path, base::StringPrintf(kTemplate, favorite_url.c_str())));
    ASSERT_TRUE(atrium_core_open(path.value().c_str(), 1.0));
  }

  // Two spaces, both empty, so a favourite card in the second one's Courtyard
  // room routes through BrowserController::SelectSpace - see
  // AtriumBrowserWindow::ActivateCourtyardCard.
  void OpenCoreWithFavoriteAndTwoSpaces(const std::string& favorite_url) {
    static constexpr char kTemplate[] =
        R"({"spaces":[)"
        R"({"id":"s1","name":"A","emoji":"A","tint":"#111111",)"
        R"("profileID":"Default","pinnedTabs":[],"tabs":[],"order":0,"groups":[]},)"
        R"({"id":"s2","name":"B","emoji":"B","tint":"#222222",)"
        R"("profileID":"Default","pinnedTabs":[],"tabs":[],"order":1,"groups":[]}],)"
        R"("favorites":[{"profileID":"Default","title":"Fav","url":"%s"}],)"
        R"("appState":{"activeSpaceID":"s1","sidebarWidth":220,)"
        R"("schemaVersion":3,"archiveEpoch":1,"sidebarWidthMigrated":true}})";
    atrium_core_close();
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    const base::FilePath path = dir_.GetPath().AppendASCII("spaces.json");
    ASSERT_TRUE(base::WriteFile(
        path, base::StringPrintf(kTemplate, favorite_url.c_str())));
    ASSERT_TRUE(atrium_core_open(path.value().c_str(), 1.0));
  }

  void OpenCoreWithTwoFavorites(const std::string& first_url,
                               const std::string& second_url) {
    static constexpr char kTemplate[] =
        R"({"spaces":[{"id":"s1","name":"Test","emoji":"T",)"
        R"("tint":"#111111","profileID":"Default","pinnedTabs":[],)"
        R"("tabs":[],"order":0,"groups":[]}],)"
        R"("favorites":[{"profileID":"Default","title":"F1","url":"%s"},)"
        R"({"profileID":"Default","title":"F2","url":"%s"}],)"
        R"("appState":{"activeSpaceID":"s1","sidebarWidth":220,)"
        R"("schemaVersion":3,"archiveEpoch":1,"sidebarWidthMigrated":true}})";
    atrium_core_close();
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    const base::FilePath path = dir_.GetPath().AppendASCII("spaces.json");
    ASSERT_TRUE(base::WriteFile(
        path, base::StringPrintf(kTemplate, first_url.c_str(),
                                 second_url.c_str())));
    ASSERT_TRUE(atrium_core_open(path.value().c_str(), 1.0));
  }

  // Pinned: a two-member folder, then two plain pins. Every row is cold.
  void OpenCoreWithPinnedFolder(const GURL& first_member,
                                const GURL& second_member,
                                const GURL& first_pin,
                                const GURL& second_pin) {
    static constexpr char kTemplate[] =
        R"({"spaces":[{"id":"s1","name":"Test","emoji":"T",)"
        R"("tint":"#111111","profileID":"Default","pinnedTabs":[)"
        R"({"url":"%s","title":"M1","groupID":"G"},)"
        R"({"url":"%s","title":"M2","groupID":"G"},)"
        R"({"url":"%s","title":"P1"},{"url":"%s","title":"P2"}],)"
        R"("tabs":[],"order":0,)"
        R"("groups":[{"id":"G","name":"Folder","collapsed":false}]}],)"
        R"("favorites":[],)"
        R"("appState":{"activeSpaceID":"s1","sidebarWidth":220,)"
        R"("schemaVersion":3,"archiveEpoch":1,"sidebarWidthMigrated":true}})";
    atrium_core_close();
    base::ScopedAllowBlockingForTesting allow_blocking;
    ASSERT_TRUE(dir_.CreateUniqueTempDir());
    const base::FilePath path = dir_.GetPath().AppendASCII("spaces.json");
    ASSERT_TRUE(base::WriteFile(
        path, base::StringPrintf(kTemplate, first_member.spec().c_str(),
                                 second_member.spec().c_str(),
                                 first_pin.spec().c_str(),
                                 second_pin.spec().c_str())));
    ASSERT_TRUE(atrium_core_open(path.value().c_str(), 1.0));
  }

  void TearDownOnMainThread() override {
    atrium_core_close();
    InProcessBrowserTest::TearDownOnMainThread();
  }

 private:
  base::ScopedTempDir dir_;
};

// AcceptCommandBarEngine navigates to the engine's search url minus its query,
// so the engine's own path needs a real 200 rather than a 404.
std::unique_ptr<net::test_server::HttpResponse> ServeOk(
    const net::test_server::HttpRequest& request) {
  const GURL url = request.GetURL();
  if (url.path() != "/" && url.path() != "/search") {
    return nullptr;
  }
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_code(net::HTTP_OK);
  response->set_content_type("text/html");
  response->set_content("<p>ok</p>");
  return response;
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

// Whether the row carrying this tab id also carries the pill. `false` when
// the id is not in the model at all, matching what a caller pairing this with
// AnyRowHasId wants.
bool RowSelected(const base::Value& model, const std::string& tab_id) {
  bool selected = false;
  auto scan = [&selected, &tab_id](const base::ListValue* rows) {
    if (!rows) {
      return;
    }
    for (const base::Value& row : *rows) {
      if (const std::string* id = row.GetDict().FindString("id");
          id && *id == tab_id) {
        selected = row.GetDict().FindBool("selected").value_or(false);
      }
    }
  };
  scan(model.GetDict().FindList("tabs"));
  return selected;
}

std::string TileTabId(const base::Value& model, const std::string& favorite_url) {
  const base::ListValue* tiles = model.GetDict().FindList("favorites");
  if (!tiles) {
    return std::string();
  }
  for (const base::Value& tile : *tiles) {
    const std::string* id = tile.GetDict().FindString("id");
    if (id && *id == favorite_url) {
      const std::string* tab_id = tile.GetDict().FindString("tabId");
      return tab_id ? *tab_id : std::string();
    }
  }
  return std::string();
}

bool TileSelected(const base::Value& model, const std::string& favorite_url) {
  const base::ListValue* tiles = model.GetDict().FindList("favorites");
  if (!tiles) {
    return false;
  }
  for (const base::Value& tile : *tiles) {
    const std::string* id = tile.GetDict().FindString("id");
    if (id && *id == favorite_url) {
      return tile.GetDict().FindBool("selected").value_or(false);
    }
  }
  return false;
}

IN_PROC_BROWSER_TEST_F(AtriumFavoriteRedirectBrowserTest,
                       AFavouriteThatRedirectsTwiceOpensOneTabAndReturnsToIt) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  const GURL landing =
      embedded_test_server()->GetURL("landing.test", "/title1.html");
  const GURL hop = embedded_test_server()->GetURL(
      "sso.test", "/server-redirect?" + landing.spec());
  const GURL favorite = embedded_test_server()->GetURL(
      "app.test", "/server-redirect?" + hop.spec());
  const GURL other = embedded_test_server()->GetURL("other.test", "/title2.html");
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithFavorite(favorite.spec()));

  TabStripModel* tabs = browser()->tab_strip_model();
  const int count_before = tabs->count();

  base::ListValue args;
  args.Append(favorite.spec());
  args.Append("");
  window->OnSurfaceMessage("sidebarFavoriteClicked", args);
  ASSERT_EQ(count_before + 1, tabs->count())
      << "the first click must open the favourite";

  content::WebContents* opened = tabs->GetActiveWebContents();
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  ASSERT_EQ(landing.spec(), opened->GetLastCommittedURL().spec())
      << "the chain did not move, so this test would prove nothing";
  const std::string opened_id = base::NumberToString(atrium::TabId(opened));

  const base::Value after_first = SidebarModel(browser());
  EXPECT_EQ(opened_id, TileTabId(after_first, favorite.spec()))
      << "the tile carries no tab id, so its next click has nothing to "
         "activate and falls through to opening another copy";
  EXPECT_FALSE(AnyRowHasId(after_first, opened_id))
      << "the favourite's page is drawn as an ordinary tab row";

  // Somewhere else in front, so the second click has to travel rather than
  // pass because the tab was already active.
  ASSERT_TRUE(content::WaitForLoadStop(
      chrome::AddAndReturnTabAt(browser(), other, -1, /*foreground=*/true)));
  const int count_with_other = tabs->count();
  ASSERT_NE(opened, tabs->GetActiveWebContents());

  window->OnSurfaceMessage("sidebarFavoriteClicked", args);
  EXPECT_EQ(count_with_other, tabs->count())
      << "the second click stacked another copy of the favourite";
  EXPECT_EQ(opened, tabs->GetActiveWebContents())
      << "the second click did not return to the favourite's own tab";
}

// The claim used to be marked by reading the active tab back and checking its
// URL still matched. A store holding a URL GURL canonicalises never equalled
// the tab's own spec, so the claim was dropped in silence.
IN_PROC_BROWSER_TEST_F(AtriumFavoriteRedirectBrowserTest,
                       AFavouriteStoredUncanonicalisedStillClaimsItsTab) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  const std::string canonical =
      embedded_test_server()->GetURL("app.test", "/title1.html").spec();
  ASSERT_EQ(0u, canonical.find("http://"));
  const std::string stored = "HTTP://" + canonical.substr(7);
  ASSERT_NE(canonical, stored);
  ASSERT_EQ(canonical, GURL(stored).spec())
      << "GURL does not canonicalise this, so the test proves nothing";
  const GURL other = embedded_test_server()->GetURL("other.test", "/title2.html");
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithFavorite(stored));

  TabStripModel* tabs = browser()->tab_strip_model();
  base::ListValue args;
  args.Append(stored);
  args.Append("");
  window->OnSurfaceMessage("sidebarFavoriteClicked", args);
  const int count_after_first = tabs->count();

  content::WebContents* opened = tabs->GetActiveWebContents();
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  EXPECT_EQ(base::NumberToString(atrium::TabId(opened)),
            TileTabId(SidebarModel(browser()), stored))
      << "the tile carries no tab id, so its click cannot activate anything";

  ASSERT_TRUE(content::WaitForLoadStop(
      chrome::AddAndReturnTabAt(browser(), other, -1, /*foreground=*/true)));
  const int count_with_other = tabs->count();
  ASSERT_EQ(count_after_first + 1, count_with_other);

  window->OnSurfaceMessage("sidebarFavoriteClicked", args);
  EXPECT_EQ(count_with_other, tabs->count())
      << "the second click stacked another copy of the favourite";
  EXPECT_EQ(opened, tabs->GetActiveWebContents())
      << "the second click did not return to the favourite's own tab";
}

// A row is joined to its tab by URL, and the persisted row follows the
// COMMITTED url. Keying the strip on the visible one made a loading tab
// disagree with its own row, and sidebar.js reopens a row it reads as cold.
IN_PROC_BROWSER_TEST_F(AtriumFavoriteRedirectBrowserTest,
                       ARowKeepsItsLiveTabWhileThePageIsStillLoading) {
  auto hung = std::make_unique<net::test_server::ControllableHttpResponse>(
      embedded_test_server(), "/hang");
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL settled = embedded_test_server()->GetURL("app.test", "/title1.html");
  const GURL hang = embedded_test_server()->GetURL("app.test", "/hang");
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithFavorite("https://unused.test/"));

  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), settled, -1, /*foreground=*/true);
  ASSERT_TRUE(tab);
  ASSERT_TRUE(content::WaitForLoadStop(tab));
  const std::string tab_id = base::NumberToString(atrium::TabId(tab));
  ASSERT_TRUE(AnyRowHasId(SidebarModel(browser()), tab_id))
      << "the settled tab has no row, so the loading case proves nothing";

  tab->GetController().LoadURL(hang, content::Referrer(),
                               ui::PAGE_TRANSITION_TYPED, std::string());
  hung->WaitForRequest();
  ASSERT_EQ(hang.spec(), tab->GetVisibleURL().spec())
      << "the navigation is not pending, so nothing is mid-flight";
  ASSERT_EQ(settled.spec(), tab->GetLastCommittedURL().spec());

  EXPECT_TRUE(AnyRowHasId(SidebarModel(browser()), tab_id))
      << "the row lost its live tab while the page was loading, so a click on "
         "it takes the cold path and opens the page a second time";
}

// Through the tile, the only path that stamps the claim the address bar reads.
// The favourite must not be the space's startup tab either: a live tab already
// on the URL is activated by URL and never claimed.
content::WebContents* OpenTheFavouritesTab(Browser* browser,
                                           const GURL& favorite) {
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser->window());
  base::ListValue args;
  args.Append(favorite.spec());
  args.Append("");
  window->OnSurfaceMessage("sidebarFavoriteClicked", args);
  content::WebContents* opened =
      browser->tab_strip_model()->GetActiveWebContents();
  if (!opened || !content::WaitForLoadStop(opened)) {
    return nullptr;
  }
  return atrium::FavoriteKeyForTab(opened).empty() ? nullptr : opened;
}

// Both address bars open the palette on themselves now, and Return there lands
// here. Not the accept message itself: it names a match index rather than a
// URL, and only the resolved URL says where the submission was pointed.
void SubmitInTheAddressBar(Browser* browser, const GURL& typed) {
  static_cast<atrium::AtriumBrowserWindow*>(browser->window())
      ->OpenCommandBarMatch(typed);
}

IN_PROC_BROWSER_TEST_F(AtriumFavoriteRedirectBrowserTest,
                       ATypedUrlOnAnotherSiteOpensBesideTheFavouritesTab) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL favorite = embedded_test_server()->GetURL("app.test", "/title1.html");
  const GURL elsewhere =
      embedded_test_server()->GetURL("other.test", "/title2.html");
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithFavorite(favorite.spec()));

  TabStripModel* tabs = browser()->tab_strip_model();
  content::WebContents* claimed = OpenTheFavouritesTab(browser(), favorite);
  ASSERT_TRUE(claimed) << "the tile opened no tab it claims, so nothing below "
                          "would be about a favourite's tab at all";
  const std::string claimed_id = base::NumberToString(atrium::TabId(claimed));
  const int count_before = tabs->count();

  SubmitInTheAddressBar(browser(), elsewhere);

  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return tabs->count() == count_before + 1; }))
      << "the submission never opened a tab of its own";
  EXPECT_EQ(favorite.spec(), claimed->GetLastCommittedURL().spec())
      << "the favourite's tab was navigated away from its own page";
  EXPECT_EQ(claimed_id, TileTabId(SidebarModel(browser()), favorite.spec()))
      << "the tile lost its binding, so its next click opens another copy";
  content::WebContents* opened = tabs->GetActiveWebContents();
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  EXPECT_EQ(elsewhere.spec(), opened->GetLastCommittedURL().spec())
      << "the new tab did not go where the address bar was pointed";
}

IN_PROC_BROWSER_TEST_F(AtriumFavoriteRedirectBrowserTest,
                       ATypedUrlOnTheFavouritesOwnSiteNavigatesItInPlace) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL favorite = embedded_test_server()->GetURL("app.test", "/title1.html");
  const GURL same_site =
      embedded_test_server()->GetURL("app.test", "/title2.html");
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithFavorite(favorite.spec()));

  TabStripModel* tabs = browser()->tab_strip_model();
  content::WebContents* claimed = OpenTheFavouritesTab(browser(), favorite);
  ASSERT_TRUE(claimed);
  const int count_before = tabs->count();

  SubmitInTheAddressBar(browser(), same_site);

  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return claimed->GetLastCommittedURL() == same_site; }))
      << "the favourite's own site did not navigate its tab";
  EXPECT_EQ(count_before, tabs->count())
      << "a same-site submission opened a tab, so the rule reads every "
         "submission as another site";
}

IN_PROC_BROWSER_TEST_F(AtriumFavoriteRedirectBrowserTest,
                       ABrowsedTabKeepsItsRowAndPillAfterAFavouriteRoundTrip) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  const GURL favorite = embedded_test_server()->GetURL("app.test", "/title1.html");
  const GURL browsing = embedded_test_server()->GetURL("other.test", "/title2.html");
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithFavorite(favorite.spec()));

  TabStripModel* tabs = browser()->tab_strip_model();
  content::WebContents* browsed =
      chrome::AddAndReturnTabAt(browser(), browsing, -1, /*foreground=*/true);
  ASSERT_TRUE(browsed);
  ASSERT_TRUE(content::WaitForLoadStop(browsed));
  const std::string browsed_id = base::NumberToString(atrium::TabId(browsed));
  ASSERT_TRUE(RowSelected(SidebarModel(browser()), browsed_id))
      << "the control: browsing alone must select its own row";

  ASSERT_TRUE(OpenTheFavouritesTab(browser(), favorite));
  ASSERT_NE(browsed, tabs->GetActiveWebContents())
      << "the favourite click did not background the browsed tab";

  base::ListValue click;
  click.Append(browsed_id);
  window->OnSurfaceMessage("sidebarTabClicked", click);
  ASSERT_EQ(browsed, tabs->GetActiveWebContents())
      << "switching back did not reactivate the browsed tab";

  const base::Value after_switch_back = SidebarModel(browser());
  EXPECT_TRUE(AnyRowHasId(after_switch_back, browsed_id))
      << "the browsed tab's row vanished after the round trip";
  EXPECT_TRUE(RowSelected(after_switch_back, browsed_id))
      << "the window shows the browsed tab, but its row carries no pill";

  window->OnSurfaceMessage("sidebarTabClicked", click);
  EXPECT_EQ(browsed, tabs->GetActiveWebContents())
      << "the second click on the same row changed which tab is showing";
  EXPECT_TRUE(AnyRowHasId(SidebarModel(browser()), browsed_id))
      << "clicking the row a second time made it disappear";
}

IN_PROC_BROWSER_TEST_F(
    AtriumFavoriteRedirectBrowserTest,
    ABrowsedTabSurvivesAFavouriteCardClickedFromAnotherSpacesRoom) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  const GURL favorite = embedded_test_server()->GetURL("app.test", "/title1.html");
  const GURL browsing = embedded_test_server()->GetURL("other.test", "/title2.html");
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithFavoriteAndTwoSpaces(favorite.spec()));

  TabStripModel* tabs = browser()->tab_strip_model();
  content::WebContents* browsed =
      chrome::AddAndReturnTabAt(browser(), browsing, -1, /*foreground=*/true);
  ASSERT_TRUE(browsed);
  ASSERT_TRUE(content::WaitForLoadStop(browsed));
  const std::string browsed_id = base::NumberToString(atrium::TabId(browsed));
  ASSERT_TRUE(RowSelected(SidebarModel(browser()), browsed_id));
  ASSERT_EQ(0, atrium_core_active_space_index());

  // The favourite's card in space 1's room - space 0 (with the browsed tab)
  // is the active one, so this crosses through SelectSpace first.
  window->ActivateCourtyardCard(1, "favorites", 0);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return browsed != tabs->GetActiveWebContents(); }))
      << "the favourite card never took the tab strip off the browsed tab";
  content::WebContents* opened = tabs->GetActiveWebContents();
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));

  // Space 0 is the browsed tab's own room, so this switches straight back.
  window->ActivateCourtyardCard(0, "tabs", 0);
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return browsed == tabs->GetActiveWebContents(); }))
      << "switching back through the room never reactivated the browsed tab";

  const base::Value after_switch_back = SidebarModel(browser());
  EXPECT_TRUE(AnyRowHasId(after_switch_back, browsed_id))
      << "the browsed tab's row vanished after the round trip";
  EXPECT_TRUE(RowSelected(after_switch_back, browsed_id))
      << "the window shows the browsed tab, but its row carries no pill";

  base::ListValue click;
  click.Append(browsed_id);
  window->OnSurfaceMessage("sidebarTabClicked", click);
  EXPECT_EQ(browsed, tabs->GetActiveWebContents())
      << "the click changed which tab is showing";
  EXPECT_TRUE(AnyRowHasId(SidebarModel(browser()), browsed_id))
      << "clicking the row made it disappear";
}

// The URL-collision fixture, run through the round trip above.
IN_PROC_BROWSER_TEST_F(
    AtriumFavoriteRedirectBrowserTest,
    ABrowsedTabOnAFavouritesOwnUrlSurvivesAnotherFavouritesRoundTrip) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  const GURL browsed_favorite =
      embedded_test_server()->GetURL("app.test", "/title1.html");
  const GURL other_favorite =
      embedded_test_server()->GetURL("other.test", "/title2.html");
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithTwoFavorites(browsed_favorite.spec(),
                                                   other_favorite.spec()));

  TabStripModel* tabs = browser()->tab_strip_model();
  // Browsed to directly - AddAndReturnTabAt, not the tile - so nothing
  // claims it even though the address matches F1.
  content::WebContents* browsed = chrome::AddAndReturnTabAt(
      browser(), browsed_favorite, -1, /*foreground=*/true);
  ASSERT_TRUE(browsed);
  ASSERT_TRUE(content::WaitForLoadStop(browsed));
  const std::string browsed_id = base::NumberToString(atrium::TabId(browsed));
  ASSERT_TRUE(AnyRowHasId(SidebarModel(browser()), browsed_id))
      << "the control: an unclaimed tab on a favourite's own url must still "
         "draw its own row";
  ASSERT_TRUE(RowSelected(SidebarModel(browser()), browsed_id));

  ASSERT_TRUE(OpenTheFavouritesTab(browser(), other_favorite));
  ASSERT_NE(browsed, tabs->GetActiveWebContents());

  base::ListValue click;
  click.Append(browsed_id);
  window->OnSurfaceMessage("sidebarTabClicked", click);
  ASSERT_EQ(browsed, tabs->GetActiveWebContents());

  const base::Value after_switch_back = SidebarModel(browser());
  EXPECT_TRUE(AnyRowHasId(after_switch_back, browsed_id))
      << "the browsed tab's row vanished after the round trip";
  EXPECT_TRUE(RowSelected(after_switch_back, browsed_id))
      << "the window shows the browsed tab, but its row carries no pill";
  EXPECT_TRUE(TileTabId(after_switch_back, browsed_favorite.spec()).empty())
      << "F1's tile adopted a tab it never claimed";
}

// C++ used to recompute selection with a URL fallback the Rust core does not
// have, so a tab that merely landed on a favourite's url lit the tile even
// though the tile never opened it.
IN_PROC_BROWSER_TEST_F(
    AtriumFavoriteRedirectBrowserTest,
    ATabThatOnlyHappensToShareAFavouritesUrlDoesNotLightItsTile) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL favorite = embedded_test_server()->GetURL("app.test", "/title1.html");
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithFavorite(favorite.spec()));

  content::WebContents* browsed = chrome::AddAndReturnTabAt(
      browser(), favorite, -1, /*foreground=*/true);
  ASSERT_TRUE(browsed);
  ASSERT_TRUE(content::WaitForLoadStop(browsed));
  ASSERT_EQ(browsed, browser()->tab_strip_model()->GetActiveWebContents());

  EXPECT_FALSE(TileSelected(SidebarModel(browser()), favorite.spec()))
      << "the tile lit up for a tab it never opened";
}

// The counterpart: a tile the tab WAS opened through must still light up
// while that tab is active.
IN_PROC_BROWSER_TEST_F(AtriumFavoriteRedirectBrowserTest,
                       AFavouritesOwnTabLightsItsTileWhileActive) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL favorite = embedded_test_server()->GetURL("app.test", "/title1.html");
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithFavorite(favorite.spec()));

  ASSERT_TRUE(OpenTheFavouritesTab(browser(), favorite));

  EXPECT_TRUE(TileSelected(SidebarModel(browser()), favorite.spec()))
      << "the tile did not light up for the tab it opened";
}

// The favourite is saved on the bare host, but the engine's own
// template URL resolves to the www.-prefixed one - the mismatch that made
// Enter on the engine row open a second tab instead of the favourite's. A ROOT
// home, the only shape a favourite claims (see the path-bearing case below).
IN_PROC_BROWSER_TEST_F(AtriumFavoriteRedirectBrowserTest,
                       EnterOnAnEngineRowSwitchesToTheFavouritesTabInstead) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL favorite = embedded_test_server()->GetURL("site.test", "/title1.html");
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithFavorite(favorite.spec()));

  TemplateURLService* engines =
      TemplateURLServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(engines);
  search_test_utils::WaitForTemplateURLServiceToLoad(engines);
  TemplateURLData data;
  data.SetShortName(u"Site");
  data.SetKeyword(u"site");
  data.SetURL("https://www.site.test/?q={searchTerms}");
  ASSERT_TRUE(engines->Add(std::make_unique<TemplateURL>(data)));

  content::WebContents* claimed = OpenTheFavouritesTab(browser(), favorite);
  ASSERT_TRUE(claimed) << "the tile opened no tab it claims, so nothing below "
                          "would be about a favourite's tab at all";
  TabStripModel* tabs = browser()->tab_strip_model();
  const int count_before = tabs->count();

  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  controller->AcceptCommandBarEngine("site");

  EXPECT_EQ(count_before, tabs->count())
      << "Enter on the engine row opened a new tab instead of switching to "
         "the favourite's own";
  EXPECT_EQ(claimed, tabs->GetActiveWebContents())
      << "Enter on the engine row did not activate the favourite's tab";
}

// The favourite match is by registrable domain alone, which cannot tell
// site.test/search from a favourite on site.test - redirecting would drop the
// path that identifies the engine, so a path-bearing home opens itself.
IN_PROC_BROWSER_TEST_F(AtriumFavoriteRedirectBrowserTest,
                       EnterOnAnEngineWhoseHomeHasAPathOpensItsOwnTab) {
  embedded_test_server()->RegisterRequestHandler(
      base::BindRepeating(&ServeOk));
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL favorite =
      embedded_test_server()->GetURL("site.test", "/title1.html");
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithFavorite(favorite.spec()));

  TemplateURLService* engines =
      TemplateURLServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(engines);
  search_test_utils::WaitForTemplateURLServiceToLoad(engines);
  // Same registrable domain as the favourite, so ONLY the path keeps them apart.
  const GURL engine_search =
      embedded_test_server()->GetURL("www.site.test", "/search");
  TemplateURLData data;
  data.SetShortName(u"Site");
  data.SetKeyword(u"site");
  data.SetURL(engine_search.spec() + "?q={searchTerms}");
  ASSERT_TRUE(engines->Add(std::make_unique<TemplateURL>(data)));

  content::WebContents* claimed = OpenTheFavouritesTab(browser(), favorite);
  ASSERT_TRUE(claimed) << "the tile opened no tab it claims, so nothing below "
                          "would be about a favourite's tab at all";
  TabStripModel* tabs = browser()->tab_strip_model();
  const int count_before = tabs->count();

  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  controller->AcceptCommandBarEngine("site");

  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return tabs->count() == count_before + 1; }))
      << "the engine's path-bearing home was swallowed by the favourite on "
         "the same domain instead of opening its own tab";
  content::WebContents* opened = tabs->GetActiveWebContents();
  ASSERT_TRUE(opened);
  EXPECT_NE(claimed, opened) << "it activated the favourite's tab";
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  EXPECT_EQ(engine_search.spec(), opened->GetLastCommittedURL().spec());
}

// AcceptCommandBarFavorite used to call OpenOrActivateUrl, whose
// exact-string match misses once the favourite's tab has navigated - the
// same mismatch EnterOnAnEngineRow above proves for the engine path.
IN_PROC_BROWSER_TEST_F(
    AtriumFavoriteRedirectBrowserTest,
    EnterOnACommandBarFavoriteSwitchesToItsTabAfterItNavigatesAway) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL favorite = embedded_test_server()->GetURL("app.test", "/title1.html");
  const GURL moved_to = embedded_test_server()->GetURL("other.test", "/title2.html");
  const GURL foreground_url =
      embedded_test_server()->GetURL("front.test", "/title1.html");
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithFavorite(favorite.spec()));

  content::WebContents* claimed = OpenTheFavouritesTab(browser(), favorite);
  ASSERT_TRUE(claimed) << "the tile opened no tab it claims, so nothing below "
                          "would be about a favourite's tab at all";

  claimed->GetController().LoadURL(moved_to, content::Referrer(),
                                   ui::PAGE_TRANSITION_TYPED, std::string());
  ASSERT_TRUE(content::WaitForLoadStop(claimed));
  ASSERT_EQ(moved_to.spec(), claimed->GetLastCommittedURL().spec())
      << "the tab did not move off its stored url, so this test would prove "
         "nothing";

  TabStripModel* tabs = browser()->tab_strip_model();
  ASSERT_TRUE(content::WaitForLoadStop(chrome::AddAndReturnTabAt(
      browser(), foreground_url, -1, /*foreground=*/true)));
  ASSERT_NE(claimed, tabs->GetActiveWebContents());
  const int count_before = tabs->count();

  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  controller->AcceptCommandBarFavorite(favorite.spec());

  EXPECT_EQ(count_before, tabs->count())
      << "Enter on the command bar favourite opened a new tab instead of "
         "activating the favourite's own, moved tab";
  EXPECT_EQ(claimed, tabs->GetActiveWebContents())
      << "Enter on the command bar favourite did not activate the "
         "favourite's tab";
}

// A favourite with no live tab must still open through the
// command bar, adopting its own row the way OpenFavoriteTab claims a tile's
// cold open - not becoming a loose, unclaimed tab.
IN_PROC_BROWSER_TEST_F(AtriumFavoriteRedirectBrowserTest,
                       ACommandBarFavoriteWithNoLiveTabOpensAndAdoptsItsRow) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL favorite = embedded_test_server()->GetURL("app.test", "/title1.html");
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithFavorite(favorite.spec()));

  TabStripModel* tabs = browser()->tab_strip_model();
  const int count_before = tabs->count();

  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  controller->AcceptCommandBarFavorite(favorite.spec());

  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return tabs->count() == count_before + 1; }))
      << "no tab opened for a favourite with nothing live";
  content::WebContents* opened = tabs->GetActiveWebContents();
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  EXPECT_EQ(favorite.spec(), opened->GetLastCommittedURL().spec());
  EXPECT_EQ(favorite.spec(), atrium::FavoriteKeyForTab(opened))
      << "the tab opened is a loose tab, not adopted into its favourite row";
}

// A startup tab restored on a favourite's url is that tile's tab, claimed at
// restore. The click then finds it by claim: never by url, because an
// ordinary tab that navigated to the address is a different tab.
IN_PROC_BROWSER_TEST_F(AtriumFavoriteRedirectBrowserTest,
                       AStartupTabOnAFavouritesUrlIsClaimedAtRestore) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL favorite = embedded_test_server()->GetURL("app.test", "/title1.html");
  const GURL other = embedded_test_server()->GetURL("other.test", "/title2.html");
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithFavorite(favorite.spec()));

  // The production startup path: the window's first tab is the startup url.
  atrium::BrowserController* controller =
      atrium::BrowserController::CreateForProfile(browser()->profile(),
                                                  /*show=*/true, favorite.spec());
  ASSERT_TRUE(controller);
  // The window opens inside the constructor, so a wait for a later open
  // never returns: find it by its controller.
  Browser* restored = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        Browser* browser = candidate->GetBrowserForMigrationOnly();
        if (atrium::BrowserController::ControllerForBrowser(browser) ==
            controller) {
          restored = browser;
          return false;
        }
        return true;
      });
  ASSERT_TRUE(restored);
  ASSERT_NE(browser(), restored);
  TabStripModel* tabs = restored->tab_strip_model();
  ASSERT_EQ(1, tabs->count());
  content::WebContents* startup = tabs->GetActiveWebContents();
  ASSERT_TRUE(startup);
  ASSERT_TRUE(content::WaitForLoadStop(startup));
  EXPECT_EQ(favorite.spec(), atrium::FavoriteKeyForTab(startup))
      << "the startup tab on the favourite's url was not claimed";
  EXPECT_EQ(base::NumberToString(atrium::TabId(startup)),
            TileTabId(SidebarModel(restored), favorite.spec()))
      << "the tile is not bound to the startup tab";

  // Elsewhere in front, so the click has to travel rather than pass because
  // the tab happened to be active.
  ASSERT_TRUE(content::WaitForLoadStop(
      chrome::AddAndReturnTabAt(restored, other, -1, /*foreground=*/true)));
  const int count_before = tabs->count();
  ASSERT_NE(startup, tabs->GetActiveWebContents());

  auto* window = static_cast<atrium::AtriumBrowserWindow*>(restored->window());
  ASSERT_TRUE(window);
  base::ListValue args;
  args.Append(favorite.spec());
  args.Append("");
  window->OnSurfaceMessage("sidebarFavoriteClicked", args);

  EXPECT_EQ(count_before, tabs->count())
      << "the click opened a second copy of a page already on screen";
  EXPECT_EQ(startup, tabs->GetActiveWebContents())
      << "the click did not activate the tab already showing the favourite";
}

// The mirror gives every profile the same engine list, so a keyword present
// in both proves nothing - this one is added only to Second's own service.
IN_PROC_BROWSER_TEST_F(
    AtriumFavoriteRedirectBrowserTest,
    EnterOnATabScopedEngineResolvesFromTheActiveSpacesProfile) {
  embedded_test_server()->RegisterRequestHandler(
      base::BindRepeating(&ServeOk));
  ASSERT_TRUE(embedded_test_server()->Start());

  ProfileManager* profile_manager = g_browser_process->profile_manager();
  ASSERT_TRUE(profile_manager);
  Profile& second_profile = profiles::testing::CreateProfileSync(
      profile_manager, profile_manager->GenerateNextProfileDirectoryPath());
  const std::string second_id = second_profile.GetBaseName().AsUTF8Unsafe();

  TemplateURLService* second_engines =
      TemplateURLServiceFactory::GetForProfile(&second_profile);
  ASSERT_TRUE(second_engines);
  search_test_utils::WaitForTemplateURLServiceToLoad(second_engines);
  TemplateURLData data;
  data.SetShortName(u"Second Site");
  data.SetKeyword(u"site");
  // GetURL(host, "/search") rather than GetURL(host, "/") + "/search": the
  // latter's spec already ends in a slash, so it built a //search path that
  // only looked like a root once the origin-only home threw the path away.
  const GURL engine_search =
      embedded_test_server()->GetURL("second.site.test", "/search");
  data.SetURL(engine_search.spec() + "?q={searchTerms}");
  ASSERT_TRUE(second_engines->Add(std::make_unique<TemplateURL>(data)));

  TemplateURLService* default_engines =
      TemplateURLServiceFactory::GetForProfile(browser()->profile());
  ASSERT_TRUE(default_engines);
  search_test_utils::WaitForTemplateURLServiceToLoad(default_engines);
  ASSERT_FALSE(default_engines->GetTemplateURLForKeyword(u"site"))
      << "the fixture is wrong if Default already carries this keyword";

  // Active space s2 belongs to Second, while browser()'s own tab strip
  // stays on Default - the palette must answer for the space, not the tab.
  static constexpr char kTemplate[] =
      R"({"spaces":[)"
      R"({"id":"s1","name":"A","emoji":"A","tint":"#111111",)"
      R"("profileID":"Default","pinnedTabs":[],"tabs":[],"order":0,"groups":[]},)"
      R"({"id":"s2","name":"B","emoji":"B","tint":"#222222",)"
      R"("profileID":"%s","pinnedTabs":[],"tabs":[],"order":1,"groups":[]}],)"
      R"("favorites":[],)"
      R"("appState":{"activeSpaceID":"s2","sidebarWidth":220,)"
      R"("schemaVersion":3,"archiveEpoch":1,"sidebarWidthMigrated":true}})";
  atrium_core_close();
  {
    base::ScopedAllowBlockingForTesting allow_blocking;
    base::ScopedTempDir dir;
    ASSERT_TRUE(dir.CreateUniqueTempDir());
    const base::FilePath path = dir.GetPath().AppendASCII("spaces.json");
    ASSERT_TRUE(base::WriteFile(
        path, base::StringPrintf(kTemplate, second_id.c_str())));
    ASSERT_TRUE(atrium_core_open(path.value().c_str(), 1.0));
  }

  // Hidden (show=false), so ActiveSpaceProfile's ControllerFor(kDefaultSlot,
  // ...) lookup finds a real Profile* with no second window on screen.
  ASSERT_TRUE(atrium::BrowserController::CreateForProfile(
      &second_profile, /*show=*/false, ""));

  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);

  TabStripModel* tabs = browser()->tab_strip_model();
  const int count_before = tabs->count();

  controller->AcceptCommandBarEngine("site");

  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return tabs->count() == count_before + 1; }))
      << "the keyword exists only in the active space's profile - a "
         "window-profile lookup finds nothing and opens no tab";
  content::WebContents* opened = tabs->GetActiveWebContents();
  ASSERT_TRUE(opened);
  ASSERT_TRUE(content::WaitForLoadStop(opened));
  EXPECT_EQ(engine_search.spec(), opened->GetLastCommittedURL().spec())
      << "the accepted engine did not resolve through the active space's "
         "own TemplateURLService, or its home lost the engine's own path";
}

// The Courtyard draws each folder member as a card of its own, so the pinned
// cards are M1, M2, P1, P2. The core counts the folder as ONE pinned item.
IN_PROC_BROWSER_TEST_F(AtriumFavoriteRedirectBrowserTest,
                       APinnedCardAfterAFolderReachesItsOwnRow) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  const GURL m1 = embedded_test_server()->GetURL("m1.test", "/title1.html");
  const GURL m2 = embedded_test_server()->GetURL("m2.test", "/title1.html");
  const GURL p1 = embedded_test_server()->GetURL("p1.test", "/title1.html");
  const GURL p2 = embedded_test_server()->GetURL("p2.test", "/title1.html");
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithPinnedFolder(m1, m2, p1, p2));
  TabStripModel* tabs = browser()->tab_strip_model();

  window->ActivateCourtyardCard(0, "pinned", 1);
  EXPECT_EQ(m2, tabs->GetActiveWebContents()->GetVisibleURL())
      << "the second member's card opened another row";

  window->ActivateCourtyardCard(0, "pinned", 3);
  EXPECT_EQ(p2, tabs->GetActiveWebContents()->GetVisibleURL())
      << "the last pin's card opened another row";

  window->ApplyCourtyardCardX(0, "pinned", 2, 40);
  const base::Value model = SidebarModel(browser());
  const base::ListValue* pinned = model.GetDict().FindList("pinned");
  ASSERT_TRUE(pinned);
  ASSERT_EQ(3u, pinned->size());
  EXPECT_EQ(40, (*pinned)[1].GetDict().FindDouble("canvasX").value_or(-1))
      << "the first pin's card did not move its own row";
  EXPECT_FALSE((*pinned)[2].GetDict().FindDouble("canvasX"))
      << "the first pin's card moved the pin after it";
}

// Cmd+D on the member the window shows. The core refuses a folder as an item,
// and passing a member's flat position names a pin after the folder.
IN_PROC_BROWSER_TEST_F(AtriumFavoriteRedirectBrowserTest,
                       CmdDOnAFolderMemberUnpinsThatMemberOnly) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  const GURL m1 = embedded_test_server()->GetURL("m1.test", "/title1.html");
  const GURL m2 = embedded_test_server()->GetURL("m2.test", "/title1.html");
  const GURL p1 = embedded_test_server()->GetURL("p1.test", "/title1.html");
  const GURL p2 = embedded_test_server()->GetURL("p2.test", "/title1.html");
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithPinnedFolder(m1, m2, p1, p2));

  window->ActivateCourtyardCard(0, "pinned", 1);
  content::WebContents* shown =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_EQ(m2, shown->GetVisibleURL());
  ASSERT_TRUE(content::WaitForLoadStop(shown));
  {
    const base::Value before = SidebarModel(browser());
    const base::ListValue* folder =
        (*before.GetDict().FindList("pinned"))[0].GetDict().FindList("tabs");
    ASSERT_TRUE(folder);
    ASSERT_EQ(2u, folder->size());
    ASSERT_TRUE((*folder)[1].GetDict().FindBool("selected").value_or(false))
        << "the control: the opened member must be the selected row";
  }

  ASSERT_TRUE(window->TogglePinShownTab());

  const base::Value model = SidebarModel(browser());
  const base::ListValue* pinned = model.GetDict().FindList("pinned");
  ASSERT_TRUE(pinned);
  ASSERT_EQ(3u, pinned->size()) << "the folder or a pin left the section";
  const base::ListValue* members = (*pinned)[0].GetDict().FindList("tabs");
  ASSERT_TRUE(members);
  ASSERT_EQ(1u, members->size());
  EXPECT_EQ(m1.spec(), *(*members)[0].GetDict().FindString("url"));
  EXPECT_EQ(p1.spec(), *(*pinned)[1].GetDict().FindString("url"));
  EXPECT_EQ(p2.spec(), *(*pinned)[2].GetDict().FindString("url"));
  const base::ListValue* regular = model.GetDict().FindList("tabs");
  ASSERT_TRUE(regular);
  ASSERT_FALSE(regular->empty());
  EXPECT_EQ(m2.spec(), *(*regular)[0].GetDict().FindString("url"))
      << "the member did not land at the top of the regular list";
}

// The pinned section as hosts, a folder in brackets: "p1.test,[m1.test]".
std::string PinnedHosts(Browser* browser) {
  const base::Value model = SidebarModel(browser);
  const base::ListValue* pinned = model.GetDict().FindList("pinned");
  if (!pinned) {
    return std::string();
  }
  std::vector<std::string> out;
  for (const base::Value& entry : *pinned) {
    const base::DictValue& dict = entry.GetDict();
    if (const base::ListValue* members = dict.FindList("tabs")) {
      std::vector<std::string> hosts;
      for (const base::Value& member : *members) {
        hosts.emplace_back(GURL(*member.GetDict().FindString("url")).host());
      }
      out.push_back("[" + base::JoinString(hosts, ",") + "]");
      continue;
    }
    out.emplace_back(GURL(*dict.FindString("url")).host());
  }
  return base::JoinString(out, ",");
}

// A Courtyard card drag counts cards, one per folder member, and the core
// counts pinned items, one per folder. Driven through the page's own pointer
// handlers, so the drop decision, the forward and the handler all run.
IN_PROC_BROWSER_TEST_F(AtriumFavoriteRedirectBrowserTest,
                       CourtyardDragsAfterAFolderMoveTheirOwnRows) {
  ASSERT_TRUE(embedded_test_server()->Start());
  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  ASSERT_NO_FATAL_FAILURE(OpenCoreWithPinnedFolder(
      embedded_test_server()->GetURL("m1.test", "/title1.html"),
      embedded_test_server()->GetURL("m2.test", "/title1.html"),
      embedded_test_server()->GetURL("p1.test", "/title1.html"),
      embedded_test_server()->GetURL("p2.test", "/title1.html")));
  ASSERT_EQ("[m1.test,m2.test],p1.test,p2.test", PinnedHosts(browser()));

  window->OpenCourtyard();
  content::WebContents* courtyard = window->CourtyardContentsForTesting();
  ASSERT_TRUE(courtyard);
  ASSERT_TRUE(content::WaitForLoadStop(courtyard));

  static constexpr char kCards[] = R"(
    [...document.querySelectorAll(
        '.room[data-room="0"] .rlane[data-section="pinned"] .rtab')]
        .map((c) => c.textContent).join()
  )";
  // A drop at card slot `to`, counted the way the lane draws them. The page
  // drops the card's top, not the pointer, so both carry the same grab offset.
  static constexpr char kDrag[] = R"(
    const cards = [...document.querySelectorAll(
        '.room[data-room="0"] .rlane[data-section="pinned"] .rtab')];
    const box = (i) => cards[i].getBoundingClientRect();
    const step = box(1).top - box(0).top;
    const last = cards.length - 1;
    const grab = 8;
    const y = grab + ($2 <= last ? box($2).top : box(last).top + step * 0.6);
    const x = box($1).left + grab;
    const send = (type, py) => cards[$1].dispatchEvent(new PointerEvent(
        type, {bubbles: true, pointerId: 1, clientX: x, clientY: py}));
    send('pointerdown', box($1).top + grab);
    send('pointermove', y);
    send('pointerup', y);
  )";
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(courtyard, kCards).ExtractString() ==
           "M1,M2,P1,P2";
  })) << "the pinned lane never drew the folder members and both pins";

  // P1, card 2 and item 1, to the top.
  ASSERT_TRUE(content::ExecJs(courtyard, content::JsReplace(kDrag, 2, 0)));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(courtyard, kCards).ExtractString() !=
           "M1,M2,P1,P2";
  })) << "dragging P1 changed nothing";
  EXPECT_EQ("p1.test,[m1.test,m2.test],p2.test", PinnedHosts(browser()))
      << "dragging P1 moved another row";

  // M2, now card 2, below the last pin: out of its folder, the rest in place.
  const std::string before = content::EvalJs(courtyard, kCards).ExtractString();
  ASSERT_TRUE(content::ExecJs(courtyard, content::JsReplace(kDrag, 2, 4)));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(courtyard, kCards).ExtractString() != before;
  })) << "dragging M2 changed nothing";
  EXPECT_EQ("p1.test,[m1.test],p2.test,m2.test", PinnedHosts(browser()))
      << "dragging M2 moved another row";

  // M1, the folder's last member, before M2: its folder item goes with it.
  const std::string alone = content::EvalJs(courtyard, kCards).ExtractString();
  ASSERT_TRUE(content::ExecJs(courtyard, content::JsReplace(kDrag, 1, 3)));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(courtyard, kCards).ExtractString() != alone;
  })) << "dragging M1 changed nothing";
  EXPECT_EQ("p1.test,p2.test,m1.test,m2.test", PinnedHosts(browser()))
      << "the last member landed one slot late";
}

}  // namespace
