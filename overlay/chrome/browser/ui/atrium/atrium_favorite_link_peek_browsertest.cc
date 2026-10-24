// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_favorite_link_peek_browsertest.cc
//
// An off-site link from a favourite's tab opens the Keyhole, whatever target
// the link names. The tab has moved within its site first: a favourite is
// known by the claim its tab carries, not by the URL it was stored with.
#include <memory>
#include <string>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/strings/stringprintf.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_new_tab_modifiers.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_navigation_observer.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "url/gurl.h"

namespace {

constexpr char kLinksPath[] = "/atrium-favorite-links.html";

// Every off-site link points at other.test on this server's own port, which
// only the request knows.
std::unique_ptr<net::test_server::HttpResponse> HandleLinksPage(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url != kLinksPath) {
    return nullptr;
  }
  GURL::Replacements other_host;
  other_host.SetHostStr("other.test");
  const std::string off_site = request.GetURL()
                                   .Resolve("/title2.html")
                                   .ReplaceComponents(other_host)
                                   .spec();
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  response->set_content(base::StringPrintf(
      "<html><body>"
      "<a id='plain' href='%s'>plain</a>"
      "<a id='blank' href='%s' target='_blank'>blank</a>"
      "<a id='noopener' href='%s' target='_blank' rel='noopener'>noopener</a>"
      "<a id='samesite' href='/title2.html' target='_blank'>same site</a>"
      "</body></html>",
      off_site.c_str(), off_site.c_str(), off_site.c_str()));
  return response;
}

class AtriumFavoriteLinkPeekBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&HandleLinksPage));
    InProcessBrowserTest::SetUpOnMainThread();
  }

  atrium::AtriumBrowserWindow* window() {
    return static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  }

  // One favourite and no tab rows.
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

  // The tile is the only path that stamps the claim. The tab then moves to
  // the links page on the same site, so its URL no longer equals the
  // favourite's and only the claim can say whose tab it is.
  content::WebContents* OpenTheFavouritesTabOnItsLinksPage() {
    const GURL favorite =
        embedded_test_server()->GetURL("app.test", "/title1.html");
    if (!OpenCoreWithFavorite(favorite)) {
      return nullptr;
    }
    base::ListValue args;
    args.Append(favorite.spec());
    args.Append("");
    window()->OnSurfaceMessage("sidebarFavoriteClicked", args);
    content::WebContents* opened =
        browser()->tab_strip_model()->GetActiveWebContents();
    if (!opened || !content::WaitForLoadStop(opened) ||
        atrium::FavoriteKeyForTab(opened).empty()) {
      return nullptr;
    }
    const GURL links = embedded_test_server()->GetURL("app.test", kLinksPath);
    content::TestNavigationObserver observer(opened);
    if (!content::ExecJs(opened, "location = '" + links.spec() + "';")) {
      return nullptr;
    }
    observer.Wait();
    return opened->GetLastCommittedURL() == links ? opened : nullptr;
  }

  // ExecJs carries a user gesture, which the throttle and the popup blocker
  // both need. An injected mouse click reaches this page without one often
  // enough that the popup blocker ate every fourth run.
  void ClickLink(content::WebContents* page, const char* link_id) {
    ASSERT_TRUE(content::ExecJs(
        page, base::StringPrintf("document.getElementById('%s').click();",
                                 link_id)));
  }

  // The claim: a peek, no new tab, and the favourite's tab still on its page.
  void ClickExpectingAPeek(const char* link_id) {
    ASSERT_TRUE(embedded_test_server()->Start());
    content::WebContents* claimed = OpenTheFavouritesTabOnItsLinksPage();
    ASSERT_TRUE(claimed) << "no claimed favourite tab reached the links page";
    TabStripModel* tabs = browser()->tab_strip_model();
    const int tabs_before = tabs->count();
    const GURL page = claimed->GetLastCommittedURL();

    ClickLink(claimed, link_id);

    EXPECT_TRUE(base::test::RunUntil([&]() { return window()->IsPeekShowing(); }))
        << "the link never opened a peek";
    EXPECT_EQ(tabs_before, tabs->count()) << "the link opened a tab instead";
    EXPECT_EQ(page, claimed->GetLastCommittedURL())
        << "the favourite's tab left its site";
  }

  base::ScopedTempDir dir_;
};

// The throttle path. Before, it matched the tab's URL against the stored
// favourite exactly, which the move to the links page defeats.
IN_PROC_BROWSER_TEST_F(AtriumFavoriteLinkPeekBrowserTest,
                       APlainOffSiteLinkOpensAPeek) {
  ClickExpectingAPeek("plain");
}

// The renderer creates the tab itself and hands it to AddNewContents.
IN_PROC_BROWSER_TEST_F(AtriumFavoriteLinkPeekBrowserTest,
                       ATargetBlankOffSiteLinkOpensAPeek) {
  ClickExpectingAPeek("blank");
}

// A noopener link arrives as a NEW_FOREGROUND_TAB OpenURLFromTab.
IN_PROC_BROWSER_TEST_F(AtriumFavoriteLinkPeekBrowserTest,
                       ANoopenerOffSiteLinkOpensAPeek) {
  ClickExpectingAPeek("noopener");
}

// Cmd+Shift+click arrives as the same NEW_FOREGROUND_TAB open a noopener link
// does, and only the held keys tell the browser it asked for a focused tab.
IN_PROC_BROWSER_TEST_F(AtriumFavoriteLinkPeekBrowserTest,
                       ACmdShiftClickOnAnOffSiteLinkOpensAFocusedTab) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* claimed = OpenTheFavouritesTabOnItsLinksPage();
  ASSERT_TRUE(claimed);
  TabStripModel* tabs = browser()->tab_strip_model();
  const int tabs_before = tabs->count();

  atrium::SetNewTabModifiersHeldForTesting(true);
  ASSERT_TRUE(content::ExecJs(
      claimed,
      "document.getElementById('plain').dispatchEvent(new MouseEvent('click',"
      " {bubbles: true, cancelable: true, metaKey: true, shiftKey: true}));"));

  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return tabs->count() == tabs_before + 1; }))
      << "the cmd+shift+click opened no tab";
  atrium::SetNewTabModifiersHeldForTesting(std::nullopt);
  EXPECT_FALSE(window()->IsPeekShowing()) << "the cmd+shift+click peeked";
  EXPECT_EQ(tabs->GetActiveWebContents(),
            tabs->GetWebContentsAt(tabs->count() - 1))
      << "the new tab is not the active one";
}

// The control: on its own site a favourite keeps working as an app, so a new
// tab it asks for is a new tab.
IN_PROC_BROWSER_TEST_F(AtriumFavoriteLinkPeekBrowserTest,
                       ASameSiteTargetBlankLinkStaysATab) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* claimed = OpenTheFavouritesTabOnItsLinksPage();
  ASSERT_TRUE(claimed);
  TabStripModel* tabs = browser()->tab_strip_model();
  const int tabs_before = tabs->count();

  ClickLink(claimed, "samesite");

  EXPECT_TRUE(base::test::RunUntil(
      [&]() { return tabs->count() == tabs_before + 1; }))
      << "the same-site link opened no tab";
  EXPECT_FALSE(window()->IsPeekShowing()) << "a same-site link peeked";
}

}  // namespace
