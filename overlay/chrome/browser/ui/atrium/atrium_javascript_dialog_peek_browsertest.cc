// Copyright 2026 The Atrium Authors

#include <string>

#include "base/test/run_until.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_devtools_protocol_client.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

// A JS dialog from a peek crashed the browser: the tab-modal dialog view
// CHECKed a TabInterface that a peek's WebContents does not have.
namespace {

class AtriumJavaScriptDialogPeekBrowserTest
    : public InProcessBrowserTest,
      public content::TestDevToolsProtocolClient {
 protected:
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
};

IN_PROC_BROWSER_TEST_F(AtriumJavaScriptDialogPeekBrowserTest,
                       AnAlertInAPeekDoesNotTakeTheBrowserDown) {
  content::WebContents* opener =
      OpenTab(embedded_test_server()->GetURL("app.test", "/title1.html"));
  ASSERT_TRUE(opener);

  // Same origin as the opener, so the opener's handle can steer the card to
  // the dialog page below.
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

  // A peek is never tab-strip foreground, so an alert() there auto-resolves
  // without building a dialog view. DevTools attached routes it through the
  // same construction path a foreground tab uses instead.
  AttachToWebContents(peeked);

  // Blocks on the renderer's alert() call. Hangs, rather than crashing, if
  // the dialog is never resolved.
  EXPECT_TRUE(content::ExecJs(peeked, "alert('from the peek')"));

  EXPECT_TRUE(peeked->GetPrimaryMainFrame()->IsRenderFrameLive());

  // Teardown destroys the peeked contents with this client still attached, and
  // TestDevToolsProtocolClient answers that with NOTREACHED.
  DetachProtocolClient();
}

}  // namespace
