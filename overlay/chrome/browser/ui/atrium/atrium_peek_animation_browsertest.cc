// Copyright 2026 The Atrium Authors

#include "base/test/run_until.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_peek_widget.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/point.h"

// a reopen during the close animation must not strand the old
// card or leave the new one holding stale contents.
namespace atrium {
namespace {

class AtriumPeekAnimationBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    InProcessBrowserTest::SetUpOnMainThread();
  }

  AtriumBrowserWindow* window() {
    return static_cast<AtriumBrowserWindow*>(browser()->window());
  }

  // Same startup-tab workaround as the other peek browsertests.
  bool OpenAPeek(const char* path) {
    if (!embedded_test_server()->Start()) {
      return false;
    }
    content::WebContents* source = chrome::AddAndReturnTabAt(
        browser(), embedded_test_server()->GetURL("/title1.html"), -1,
        /*foreground=*/true);
    if (!source || !content::WaitForLoadStop(source)) {
      return false;
    }
    BrowserController* controller =
        BrowserController::ControllerForBrowser(browser());
    if (!controller) {
      return false;
    }
    controller->OpenPeek(embedded_test_server()->GetURL(path),
                         content::Referrer(), gfx::Point(200, 200));
    return base::test::RunUntil([&]() { return window()->IsPeekShowing(); });
  }
};

IN_PROC_BROWSER_TEST_F(AtriumPeekAnimationBrowserTest,
                       ReopenDuringCloseShowsTheNewPageNotTheOld) {
  ASSERT_TRUE(OpenAPeek("/title2.html")) << "the first peek card never showed";
  content::WebContents* first = window()->PeekedWebContentsForTesting();
  ASSERT_TRUE(first);

  BrowserController* controller =
      BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  // Starts the animated close and returns immediately - the 170ms dismiss
  // animation has not had any chance to finish yet.
  controller->ClosePeek();

  controller->OpenPeek(embedded_test_server()->GetURL("/title3.html"),
                       content::Referrer(), gfx::Point(220, 220));

  ASSERT_TRUE(
      base::test::RunUntil([&]() { return window()->IsPeekShowing(); }))
      << "the reopened peek never showed";
  content::WebContents* second = window()->PeekedWebContentsForTesting();
  ASSERT_TRUE(second);
  EXPECT_NE(first, second)
      << "the reopened card is still holding the closed peek's contents";
  ASSERT_TRUE(content::WaitForLoadStop(second));
  EXPECT_EQ("/title3.html", second->GetLastCommittedURL().path());
}

}  // namespace
}  // namespace atrium
