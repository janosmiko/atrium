// Copyright 2026 The Atrium Authors

#include <string>

#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
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
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// Cmd+Shift on the sidebar's reload button empties the current
// site's cookies and site data, then reloads bypassing the cache. The removal
// is asynchronous, so the reload waits on its reply rather than racing it.
namespace {

class AtriumReloadClearingSiteBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    // Named hosts, so "another site" is a different registrable domain rather
    // than another path on 127.0.0.1.
    host_resolver()->AddRule("*", "127.0.0.1");
    InProcessBrowserTest::SetUpOnMainThread();
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

  void SendSurface(const char* name) {
    AtriumWindow()->OnSurfaceMessage(name, base::ListValue());
  }
};

IN_PROC_BROWSER_TEST_F(AtriumReloadClearingSiteBrowserTest,
                       CmdShiftReloadEmptiesTheSitesCookiesAndReloads) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL page = embedded_test_server()->GetURL("app.test", "/title1.html");
  content::WebContents* tab = OpenTab(page);
  content::BrowserContext* context = browser()->profile();

  // VACUITY GUARD. A message that reaches no tab would clear nothing and
  // reload nothing, which is indistinguishable from a working removal.
  {
    content::TestNavigationObserver plain(tab);
    SendSurface("sidebarReloadPressed");
    plain.Wait();
  }

  ASSERT_TRUE(content::SetCookie(context, page, "atrium=kept; path=/"));
  ASSERT_EQ("atrium=kept", content::GetCookies(context, page));

  content::TestNavigationObserver reloaded(tab);
  SendSurface("sidebarReloadClearingSite");
  reloaded.Wait();

  EXPECT_EQ("", content::GetCookies(context, page))
      << "the reload ran without emptying the site first";
  EXPECT_EQ(page.spec(), tab->GetLastCommittedURL().spec())
      << "the tab left the page it was clearing";
}

// The filter is scoped to one registrable domain, so a second site's cookies
// must survive - a whole-profile removal would pass the test above too.
IN_PROC_BROWSER_TEST_F(AtriumReloadClearingSiteBrowserTest,
                       ClearingOneSiteLeavesAnotherSitesCookiesAlone) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL page = embedded_test_server()->GetURL("app.test", "/title1.html");
  const GURL other =
      embedded_test_server()->GetURL("other.test", "/title2.html");
  content::WebContents* tab = OpenTab(page);
  content::BrowserContext* context = browser()->profile();

  ASSERT_TRUE(content::SetCookie(context, page, "atrium=cleared; path=/"));
  ASSERT_TRUE(content::SetCookie(context, other, "atrium=kept; path=/"));
  ASSERT_EQ("atrium=cleared", content::GetCookies(context, page));
  ASSERT_EQ("atrium=kept", content::GetCookies(context, other));

  content::TestNavigationObserver reloaded(tab);
  SendSurface("sidebarReloadClearingSite");
  reloaded.Wait();

  EXPECT_EQ("", content::GetCookies(context, page));
  EXPECT_EQ("atrium=kept", content::GetCookies(context, other))
      << "the removal was not scoped to the tab's own site";
}

}  // namespace
