// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_reload_hotkey_browsertest.cc
//
// The four reload rungs, each pressed as a chord through NativeHotkeyMap: a
// surface message would exercise the handler and never the binding.

#include <atomic>
#include <memory>
#include <string>
#include <string_view>

#include "base/functional/bind.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_map.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
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
#include "url/gurl.h"

namespace {

constexpr char kPageOne[] = "/atrium_reload_one.html";
constexpr char kPageTwo[] = "/atrium_reload_two.html";
constexpr char kAssetOneName[] = "atrium_reload_one.css";
constexpr char kAssetTwoName[] = "atrium_reload_two.css";
constexpr char kAssetOne[] = "/atrium_reload_one.css";
constexpr char kAssetTwo[] = "/atrium_reload_two.css";

// Written on the embedded server's IO thread, read on the UI thread.
std::atomic<int> g_asset_one_requests{0};
std::atomic<int> g_asset_two_requests{0};

std::unique_ptr<net::test_server::HttpResponse> BuildPage(
    std::string_view asset) {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/html");
  // The document is never cached, so the only thing the counters below can be
  // measuring is the SUBRESOURCE's own trip to the cache.
  response->AddCustomHeader("Cache-Control", "no-store");
  response->set_content(std::string("<link rel=\"stylesheet\" href=\"") +
                        std::string(asset) + "\"><body>reload probe");
  return response;
}

std::unique_ptr<net::test_server::HttpResponse> BuildAsset() {
  auto response = std::make_unique<net::test_server::BasicHttpResponse>();
  response->set_content_type("text/css");
  // Fresh for an hour, so an ordinary reload has no reason to ask again.
  // Only a cache-bypassing reload, or an emptied cache, can move the counter.
  response->AddCustomHeader("Cache-Control", "max-age=3600");
  response->set_content("body{color:#123456}");
  return response;
}

std::unique_ptr<net::test_server::HttpResponse> HandleProbeRequest(
    const net::test_server::HttpRequest& request) {
  if (request.relative_url == kPageOne) {
    return BuildPage(kAssetOneName);
  }
  if (request.relative_url == kPageTwo) {
    return BuildPage(kAssetTwoName);
  }
  if (request.relative_url == kAssetOne) {
    g_asset_one_requests.fetch_add(1);
    return BuildAsset();
  }
  if (request.relative_url == kAssetTwo) {
    g_asset_two_requests.fetch_add(1);
    return BuildAsset();
  }
  return nullptr;
}

class AtriumReloadHotkeyBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    // Named hosts, so the two pages are different registrable domains rather
    // than two paths on 127.0.0.1.
    host_resolver()->AddRule("*", "127.0.0.1");
    g_asset_one_requests.store(0);
    g_asset_two_requests.store(0);
    embedded_test_server()->RegisterRequestHandler(
        base::BindRepeating(&HandleProbeRequest));
    InProcessBrowserTest::SetUpOnMainThread();
    atrium::NativeHotkeyMap::Get()->ResetForTesting();
  }

  atrium::AtriumBrowserWindow* AtriumWindow() {
    return static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  }

  // NavigateToURL never returns in an Atrium window, which starts on its own
  // about:blank tab.
  content::WebContents* OpenTab(const GURL& url) {
    content::WebContents* tab =
        chrome::AddAndReturnTabAt(browser(), url, -1, /*foreground=*/true);
    EXPECT_TRUE(tab);
    EXPECT_TRUE(content::WaitForLoadStop(tab));
    return tab;
  }

  // The chord, then BOTH waits: the navigation observer returns once the
  // document commits, which is before the stylesheet has been asked for.
  void PressReloadChord(content::WebContents* tab, std::string_view combo) {
    content::TestNavigationObserver reloaded(tab);
    ASSERT_TRUE(AtriumWindow()->DeliverComboForTesting(combo))
        << combo << " was declined, so it fell through to its stock meaning";
    reloaded.Wait();
    ASSERT_TRUE(content::WaitForLoadStop(tab));
  }
};

// The plain reload is the control: without it, a server that cached
// nothing would satisfy the bypass on its own.
IN_PROC_BROWSER_TEST_F(AtriumReloadHotkeyBrowserTest,
                       CmdShiftRReloadsPastTheCache) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* tab =
      OpenTab(embedded_test_server()->GetURL("app.test", kPageOne));
  ASSERT_EQ(1, g_asset_one_requests.load()) << "the page never asked for it";

  PressReloadChord(tab, "cmd+r");
  ASSERT_EQ(1, g_asset_one_requests.load())
      << "the plain reload refetched a max-age=3600 asset, so this test cannot "
         "tell a cache bypass from an uncached server";

  PressReloadChord(tab, "cmd+shift+r");
  EXPECT_EQ(2, g_asset_one_requests.load())
      << "Cmd+Shift+R served the stylesheet from cache instead of refetching";
}

// On the reloaded page a bypass and an emptied cache look identical, so
// the read that separates them is a SECOND site's asset going stale too.
IN_PROC_BROWSER_TEST_F(AtriumReloadHotkeyBrowserTest,
                       CmdOptionREmptiesTheCacheAndReloads) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* other =
      OpenTab(embedded_test_server()->GetURL("other.test", kPageTwo));
  content::WebContents* tab =
      OpenTab(embedded_test_server()->GetURL("app.test", kPageOne));
  ASSERT_EQ(1, g_asset_one_requests.load());
  ASSERT_EQ(1, g_asset_two_requests.load());

  PressReloadChord(tab, "cmd+opt+r");
  EXPECT_EQ(2, g_asset_one_requests.load())
      << "Cmd+Option+R did not reload the page it was pressed at";

  browser()->tab_strip_model()->ActivateTabAt(
      browser()->tab_strip_model()->GetIndexOfWebContents(other));
  PressReloadChord(other, "cmd+r");
  EXPECT_EQ(2, g_asset_two_requests.load())
      << "the other site's asset was still cached, so the cache was bypassed "
         "for one page rather than emptied";
}

// The narrowing case for the one above: the same second-site read, after the
// weaker chord. Without it "emptied" would pass on a build that merely
// bypasses, because every counter it looks at would move either way.
IN_PROC_BROWSER_TEST_F(AtriumReloadHotkeyBrowserTest,
                       CmdShiftRLeavesAnotherSitesCacheAlone) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* other =
      OpenTab(embedded_test_server()->GetURL("other.test", kPageTwo));
  content::WebContents* tab =
      OpenTab(embedded_test_server()->GetURL("app.test", kPageOne));
  ASSERT_EQ(1, g_asset_two_requests.load());

  PressReloadChord(tab, "cmd+shift+r");
  ASSERT_EQ(2, g_asset_one_requests.load());

  browser()->tab_strip_model()->ActivateTabAt(
      browser()->tab_strip_model()->GetIndexOfWebContents(other));
  PressReloadChord(other, "cmd+r");
  EXPECT_EQ(1, g_asset_two_requests.load())
      << "a plain cache bypass evicted another site's asset, which is the "
         "stronger rung's job";
}

// The other half. The site-data removal itself is covered by
// atrium_reload_clearing_site_browsertest.cc; what this adds is that the chord
// reaches that same handler at all.
IN_PROC_BROWSER_TEST_F(AtriumReloadHotkeyBrowserTest,
                       CmdOptionShiftRClearsTheSitesDataAndReloads) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL page = embedded_test_server()->GetURL("app.test", kPageOne);
  content::WebContents* tab = OpenTab(page);
  content::BrowserContext* context = browser()->profile();

  ASSERT_TRUE(content::SetCookie(context, page, "atrium=kept; path=/"));
  ASSERT_EQ("atrium=kept", content::GetCookies(context, page));

  PressReloadChord(tab, "cmd+shift+opt+r");

  EXPECT_EQ("", content::GetCookies(context, page))
      << "the chord reloaded without emptying the site first";
  EXPECT_EQ(page.spec(), tab->GetLastCommittedURL().spec())
      << "the tab left the page it was clearing";
}

// The rung below it must NOT clear cookies, or the ladder has three rungs.
IN_PROC_BROWSER_TEST_F(AtriumReloadHotkeyBrowserTest,
                       TheWeakerChordsLeaveTheSitesCookiesAlone) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL page = embedded_test_server()->GetURL("app.test", kPageOne);
  content::WebContents* tab = OpenTab(page);
  content::BrowserContext* context = browser()->profile();

  ASSERT_TRUE(content::SetCookie(context, page, "atrium=kept; path=/"));
  for (const char* combo : {"cmd+r", "cmd+shift+r", "cmd+opt+r"}) {
    PressReloadChord(tab, combo);
    EXPECT_EQ("atrium=kept", content::GetCookies(context, page))
        << combo << " cleared the site's cookies";
  }
}

}  // namespace
