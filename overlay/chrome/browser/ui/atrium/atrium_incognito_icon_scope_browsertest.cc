// Copyright 2026 The Atrium Authors

#include <optional>
#include <string>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/json/json_reader.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_favicon_source.h"
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

// The URL-keyed icon cache was process wide, and a request path said nothing
// about which profile asked - so an incognito window's favourite tiles were
// answered from the icons the regular profile had fetched.
namespace {

constexpr char kPage[] = "https://icon.test/";
constexpr char kEncodedPage[] = "https%3A%2F%2Ficon.test%2F";

std::vector<uint8_t> Icon() {
  return {1, 2, 3, 4};
}

// The path the sidebar builds for a row: /urlicon/<scope>/<encoded url>.png.
std::string IconPathFor(const std::string& scope) {
  return "urlicon/" + scope + "/" + kEncodedPage + ".png";
}

// Read off the pushed model rather than from the cache directly, so this
// covers the whole chain a real row travels.
std::string ScopeInModelFor(Browser* browser) {
  std::optional<base::Value> parsed = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(browser), base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::string();
  }
  const std::string* scope = parsed->GetDict().FindString("iconScope");
  return scope ? *scope : std::string();
}

using AtriumIncognitoIconScopeBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(AtriumIncognitoIconScopeBrowserTest,
                       AnIncognitoWindowIsNotServedARegularProfileIcon) {
  ASSERT_TRUE(embedded_test_server()->Start());
  atrium::IconCache::UrlFor(browser()->profile()).Store(kPage, Icon());

  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito);
  // A row of its own, so the window is a real sidebar rather than an empty
  // one. NavigateToURL on an Atrium window never returns.
  ASSERT_TRUE(content::WaitForLoadStop(chrome::AddAndReturnTabAt(
      incognito, embedded_test_server()->GetURL("/title1.html"), -1,
      /*foreground=*/true)));

  const std::string regular_scope = ScopeInModelFor(browser());
  const std::string incognito_scope = ScopeInModelFor(incognito);
  ASSERT_FALSE(regular_scope.empty());
  ASSERT_FALSE(incognito_scope.empty());
  EXPECT_NE(regular_scope, incognito_scope)
      << "the two windows push the same token, so either can name the other's "
         "cache";

  scoped_refptr<base::RefCountedMemory> served =
      atrium::ServeIcon(IconPathFor(regular_scope));
  ASSERT_TRUE(served) << "the regular profile's own icon stopped resolving, so "
                         "this test would pass for the wrong reason";
  EXPECT_EQ(Icon(), std::vector<uint8_t>(served->begin(), served->end()));

  EXPECT_FALSE(atrium::ServeIcon(IconPathFor(incognito_scope)))
      << "the incognito window's own row path is answered from the regular "
         "profile's icons";
}

// The other half of the same rule: an incognito window must not fill the cache
// either, because the lookup it would run reads the regular profile's database.
IN_PROC_BROWSER_TEST_F(AtriumIncognitoIconScopeBrowserTest,
                       AnIncognitoWindowFetchesNoRowIcon) {
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito);
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(incognito);
  ASSERT_TRUE(controller);

  controller->RequestFaviconsForRowURLs({kPage});

  EXPECT_FALSE(atrium::IconCache::UrlFor(incognito->profile()).Find(kPage));
  EXPECT_FALSE(atrium::IconCache::UrlFor(browser()->profile()).Find(kPage));
}

// One space holding one cold row, so the document this test guards has
// something in it to lose.
constexpr char kOneSpace[] =
    R"({"spaces":[{"id":"work","name":"Work","emoji":"W","tint":"#111111",)"
    R"("profileID":"Default","pinnedTabs":[],)"
    R"("tabs":[{"url":"https://kept.test/","title":"Kept"}],)"
    R"("order":0,"groups":[]}],)"
    R"("appState":{"activeSpaceID":"work","sidebarWidth":280,)"
    R"("schemaVersion":3,"archiveEpoch":1,"sidebarWidthMigrated":true}})";

base::Value SidebarModel(Browser* browser) {
  std::optional<base::Value> parsed = base::JSONReader::Read(
      atrium::BuildSidebarTabsJSON(browser), base::JSON_PARSE_RFC);
  CHECK(parsed) << "BuildSidebarTabsJSON did not return valid JSON";
  return std::move(*parsed);
}

using AtriumIncognitoSpacesBrowserTest = InProcessBrowserTest;

// Incognito is a PROFILE property, so such a window is is_type_normal and gets
// the same controller, window and sidebar as any other - which drew it the
// active space's rail and routed its tabs into that space.
IN_PROC_BROWSER_TEST_F(AtriumIncognitoSpacesBrowserTest,
                       AnIncognitoWindowHasNoSpacesAndMovesNone) {
  ASSERT_TRUE(embedded_test_server()->Start());
  atrium_core_close();
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath spaces_path = dir.GetPath().AppendASCII("spaces.json");
  ASSERT_TRUE(base::WriteFile(spaces_path, kOneSpace));
  ASSERT_TRUE(atrium_core_open(spaces_path.value().c_str(), 1.0));

  // A row of the regular window's own, through the production open path.
  ASSERT_TRUE(content::WaitForLoadStop(chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL("/title1.html"), -1,
      /*foreground=*/true)));
  // ApplySidebarGesture posts a save of its own after every write, so save()'s
  // RETURN is never asserted here - it races that autosave and answers 0
  // whenever production got there first. Only the file it leaves is evidence.
  base::RunLoop().RunUntilIdle();
  atrium_core_save();
  std::string disk_before;
  ASSERT_TRUE(base::ReadFileToString(spaces_path, &disk_before));

  const base::Value regular_before = SidebarModel(browser());
  const base::ListValue* spaces_before =
      regular_before.GetDict().FindList("spaces");
  ASSERT_TRUE(spaces_before && !spaces_before->empty())
      << "the core answered the regular window no spaces either, so the "
         "incognito check below would pass for the wrong reason";
  // The precondition proper: this document is the one being written, and an
  // ordinary window's tab reaches it. Without it every check below passes on a
  // core that was writing somewhere else, or not at all.
  ASSERT_NE(std::string::npos, disk_before.find("title1.html"))
      << "the regular window's own tab never landed in " << spaces_path;

  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito);
  content::WebContents* hidden_tab = chrome::AddAndReturnTabAt(
      incognito, embedded_test_server()->GetURL("/title2.html"), -1,
      /*foreground=*/true);
  ASSERT_TRUE(hidden_tab);
  ASSERT_TRUE(content::WaitForLoadStop(hidden_tab));
  base::RunLoop().RunUntilIdle();

  const base::Value hidden = SidebarModel(incognito);
  const base::ListValue* hidden_spaces = hidden.GetDict().FindList("spaces");
  ASSERT_TRUE(hidden_spaces);
  EXPECT_TRUE(hidden_spaces->empty()) << "the rail names a space this window "
                                         "must not be able to reach";
  EXPECT_EQ(true, hidden.GetDict().FindBool("incognito"))
      << "without the flag the surface cannot tell this from a window whose "
         "space model has not arrived yet";
  // The strip itself, in order: CreateIncognitoBrowser opens a tab of its own
  // so the count is not 1, and an equality rules out a foreign row without
  // naming the ones this fixture happens to have.
  std::vector<std::string> strip_titles;
  TabStripModel* hidden_strip = incognito->tab_strip_model();
  for (int i = 0; i < hidden_strip->count(); ++i) {
    strip_titles.push_back(
        base::UTF16ToUTF8(hidden_strip->GetWebContentsAt(i)->GetTitle()));
  }
  ASSERT_GE(strip_titles.size(), 1u);

  const base::ListValue* hidden_rows = hidden.GetDict().FindList("tabs");
  ASSERT_TRUE(hidden_rows);
  std::vector<std::string> row_titles;
  for (const base::Value& row : *hidden_rows) {
    const std::string* title = row.GetDict().FindString("title");
    ASSERT_TRUE(title);
    row_titles.push_back(*title);
  }
  EXPECT_EQ(strip_titles, row_titles)
      << "the rows are this window's own strip, in its order, and nothing else";

  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(incognito);
  ASSERT_TRUE(controller);
  EXPECT_FALSE(controller->PersistsTabs());

  CloseBrowserSynchronously(incognito);
  base::RunLoop().RunUntilIdle();

  // Forced to disk first: an in-memory mutation nobody flushed would leave the
  // file identical, and the comparison would pass for the wrong reason.
  atrium_core_save();
  std::string disk_after;
  ASSERT_TRUE(base::ReadFileToString(spaces_path, &disk_after));
  EXPECT_EQ(disk_before, disk_after);
  EXPECT_EQ(std::string::npos, disk_after.find("title2.html"))
      << "the incognito tab's own URL is in spaces.json";
  EXPECT_EQ(regular_before, SidebarModel(browser()))
      << "opening or closing the incognito window changed a space";
}

// An undraggable row and a trimmed menu are decisions the surface makes about
// itself, and chrome.send takes any name - so these are sent the way a stale
// or compromised sidebar would send them.
IN_PROC_BROWSER_TEST_F(AtriumIncognitoSpacesBrowserTest,
                       AWriteMessageSentFromAnIncognitoWindowIsRefused) {
  ASSERT_TRUE(embedded_test_server()->Start());
  atrium_core_close();
  base::ScopedAllowBlockingForTesting allow_blocking;
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());
  const base::FilePath spaces_path = dir.GetPath().AppendASCII("spaces.json");
  ASSERT_TRUE(base::WriteFile(spaces_path, kOneSpace));
  ASSERT_TRUE(atrium_core_open(spaces_path.value().c_str(), 1.0));

  ASSERT_TRUE(content::WaitForLoadStop(chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL("/title1.html"), -1,
      /*foreground=*/true)));
  // save()'s return is not asserted here either - see the test above.
  base::RunLoop().RunUntilIdle();
  atrium_core_save();
  std::string disk_before;
  ASSERT_TRUE(base::ReadFileToString(spaces_path, &disk_before));
  ASSERT_NE(std::string::npos, disk_before.find("title1.html"))
      << "the regular window's own tab never landed in " << spaces_path;
  const base::Value regular_before = SidebarModel(browser());

  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito);
  content::WebContents* hidden_tab = chrome::AddAndReturnTabAt(
      incognito, embedded_test_server()->GetURL("/title2.html"), -1,
      /*foreground=*/true);
  ASSERT_TRUE(hidden_tab);
  ASSERT_TRUE(content::WaitForLoadStop(hidden_tab));

  auto* hidden_window =
      static_cast<atrium::AtriumBrowserWindow*>(incognito->window());
  ASSERT_TRUE(hidden_window);

  base::ListValue reorder;
  reorder.Append(0);
  reorder.Append(1);
  hidden_window->OnSurfaceMessage("sidebarTabReordered", reorder);

  base::ListValue shelve;
  shelve.Append(base::NumberToString(atrium::TabId(hidden_tab)));
  hidden_window->OnSurfaceMessage("sidebarTabShelved", shelve);

  base::ListValue pin;
  pin.Append(0);
  pin.Append(0);
  hidden_window->OnSurfaceMessage("sidebarTabPinned", pin);

  base::ListValue select;
  select.Append(0);
  hidden_window->OnSurfaceMessage("sidebarSpaceSelected", select);

  // The width is not a surface message - the grip's release calls this
  // directly, and it lands in the same appState the rows do.
  const int stored_width = atrium_core_sidebar_width();
  hidden_window->SetSidebarWidthCommit(stored_width + 40);
  hidden_window->SetSidebarOverlayWidth(stored_width + 80);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(stored_width, atrium_core_sidebar_width())
      << "the incognito window's resize reached the shared stored width";

  // Flushed first, so an in-memory mutation cannot hide behind an unwritten
  // file: four write messages and two resizes arrived, and the document has
  // not moved for any of them.
  atrium_core_save();
  std::string disk_after;
  ASSERT_TRUE(base::ReadFileToString(spaces_path, &disk_after));
  EXPECT_EQ(disk_before, disk_after);
  EXPECT_EQ(regular_before, SidebarModel(browser()));

  // The control. Without it all of the above passes on messages the dispatcher
  // never understood in the first place.
  auto* plain_window =
      static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(plain_window);
  plain_window->OnSurfaceMessage("sidebarTabPinned", pin);
  plain_window->SetSidebarWidthCommit(stored_width + 40);
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(stored_width + 40, atrium_core_sidebar_width())
      << "the same resize wrote nothing from the ordinary window either, so "
         "the refusal above is not evidence of anything";
  atrium_core_save();
  std::string disk_control;
  ASSERT_TRUE(base::ReadFileToString(spaces_path, &disk_control));
  EXPECT_NE(disk_after, disk_control)
      << "the same message changed nothing from the ordinary window either, so "
         "the refusals above are not evidence of anything";
  EXPECT_NE(regular_before, SidebarModel(browser()));

  CloseBrowserSynchronously(incognito);
}

}  // namespace
