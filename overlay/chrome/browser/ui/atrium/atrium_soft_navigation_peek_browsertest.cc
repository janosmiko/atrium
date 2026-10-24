// Copyright 2026 The Atrium Authors

#include <memory>
#include <string>

#include "base/test/run_until.h"
#include "base/test/scoped_feature_list.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/page_load_metrics/browser/page_load_metrics_test_waiter.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/hit_test_region_observer.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/accessibility/accessibility_features.h"
#include "url/gurl.h"

// A soft navigation crashed the browser: page_load_metrics runs
// ReadAnythingSoftNavigationObserver for every page load, and the peek card is
// a WebContents with a tab's helpers that no TabStripModel holds.
namespace {

constexpr char kSoftNavigationPath[] =
    "/page_load_metrics/soft_navigation.html";

class AtriumSoftNavigationPeekBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUp() override {
    // page_load_metrics_initialize.cc only registers the observer under this
    // feature, so without it the crashing path never runs and the test passes
    // for the wrong reason.
    scoped_feature_list_.InitAndEnableFeature(
        features::kImmersiveReadAnything);
    InProcessBrowserTest::SetUp();
  }

  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  atrium::AtriumBrowserWindow* AtriumWindow() {
    return static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  }

  // A new tab, not ui_test_utils::NavigateToURL: that helper never returns in
  // an Atrium window, which starts on its own about:blank tab.
  content::WebContents* OpenTab(const GURL& url) {
    content::WebContents* tab =
        chrome::AddAndReturnTabAt(browser(), url, -1, /*foreground=*/true);
    EXPECT_TRUE(tab);
    EXPECT_TRUE(content::WaitForLoadStop(tab));
    return tab;
  }

 private:
  base::test::ScopedFeatureList scoped_feature_list_;
};

IN_PROC_BROWSER_TEST_F(AtriumSoftNavigationPeekBrowserTest,
                       ASoftNavigationInAPeekDoesNotTakeTheBrowserDown) {
  content::WebContents* opener =
      OpenTab(embedded_test_server()->GetURL("app.test", "/title1.html"));
  ASSERT_TRUE(opener);

  // Same origin as the opener, so the opener's handle can steer the card to
  // the soft navigation page below.
  ASSERT_TRUE(content::ExecJs(
      opener, "window.__h = window.open('" +
                  embedded_test_server()->GetURL("app.test", "/title1.html")
                      .spec() +
                  "', 'peeked', 'width=420,height=520');"));
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return AtriumWindow()->IsPeekShowing(); }))
      << "the popup never landed in the peek card, so nothing below runs "
         "against a non-tab WebContents";

  content::WebContents* peeked = AtriumWindow()->PeekedWebContentsForTesting();
  ASSERT_TRUE(peeked);

  // VACUITY GUARD. If the card's page were a Chromium tab, the lookup that
  // crashed would succeed and this test would pass with the bug still in.
  ASSERT_FALSE(tabs::TabInterface::MaybeGetFromContents(peeked))
      << "the peeked page is a TabInterface tab, so it is not the shape that "
         "crashed";

  // Built before the navigation: the waiter attaches its observer on commit.
  auto waiter = std::make_unique<page_load_metrics::PageLoadMetricsTestWaiter>(
      peeked);
  waiter->AddPageExpectation(
      page_load_metrics::PageLoadMetricsTestWaiter::TimingField::kLoadEvent);
  ASSERT_TRUE(content::ExecJs(
      opener, "window.__h.location = '" +
                  embedded_test_server()
                      ->GetURL("app.test", kSoftNavigationPath)
                      .spec() +
                  "';"));
  waiter->Wait();

  waiter->AddSoftNavigationCountExpectation(1);
  content::WaitForHitTestData(peeked->GetPrimaryMainFrame());
  content::MainThreadFrameObserver frame_observer(
      peeked->GetPrimaryMainFrame()->GetRenderWidgetHost());
  frame_observer.Wait();

  // The page's own click handler runs history.pushState and mutates the DOM,
  // which is what Blink reports as a soft navigation.
  content::SimulateMouseClickOrTapElementWithId(peeked, "button");

  // Reaching the other side of this is the assertion: the browser process
  // dispatched OnSoftNavigation to its page load observers and survived.
  waiter->Wait();

  EXPECT_TRUE(peeked->GetPrimaryMainFrame()->IsRenderFrameLive());
  EXPECT_EQ(true, content::EvalJs(opener, "!!window.__h && !window.__h.closed"))
      << "the card died along with the soft navigation";
}

}  // namespace
