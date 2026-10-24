// Copyright 2026 The Atrium Authors

#include <string>

#include "base/test/run_until.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_peek_widget.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/common/referrer.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

// the close and pop-out buttons beside a peek card are a WebUI
// document on the shared overlay layer, not a dead NativeViewHost.
namespace atrium {
namespace {

class AtriumPeekControlsBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    InProcessBrowserTest::SetUpOnMainThread();
  }

  AtriumBrowserWindow* window() {
    return static_cast<AtriumBrowserWindow*>(browser()->window());
  }

  // An Atrium window starts on its own about:blank tab, and
  // ui_test_utils::NavigateToURL waits forever on it - the sibling peek
  // browsertests all open their source tab this way.
  bool OpenAPeek() {
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
    controller->OpenPeek(embedded_test_server()->GetURL("/title2.html"),
                         content::Referrer(), gfx::Point(200, 200));
    return base::test::RunUntil([&]() { return window()->IsPeekShowing(); });
  }
};

IN_PROC_BROWSER_TEST_F(AtriumPeekControlsBrowserTest,
                       TheClusterShowsBesideTheCardOnItsOwnPage) {
  ASSERT_TRUE(OpenAPeek()) << "the peek card never showed";

  PeekControlsWidget* controls = window()->PeekControlsForTesting();
  ASSERT_TRUE(controls);
  views::Widget* widget = controls->widget();
  ASSERT_TRUE(widget);

  EXPECT_TRUE(widget->IsVisible()) << "the cluster stayed hidden";
  // Key would go to the cluster and the card dismisses itself on losing it.
  EXPECT_FALSE(widget->IsActive()) << "the cluster took key from the card";
  EXPECT_FALSE(widget->CanActivate()) << "the cluster is activatable at all";
  EXPECT_EQ(kPeekControlsSize, widget->GetWindowBoundsInScreen().size());

  ASSERT_TRUE(controls->web_contents()) << "the cluster hosts no document";
  EXPECT_EQ(GURL(std::string(chrome::kChromeUIAtriumURL).append("peekctl")),
            controls->web_contents()->GetVisibleURL());
}

IN_PROC_BROWSER_TEST_F(AtriumPeekControlsBrowserTest,
                       CloseFromTheClusterDismissesTheCard) {
  ASSERT_TRUE(OpenAPeek()) << "the peek card never showed";

  PeekControlsWidget* controls = window()->PeekControlsForTesting();
  ASSERT_TRUE(controls);
  base::ListValue args;
  args.Append("close");
  controls->OnSurfaceMessage("peekControl", args);

  ASSERT_TRUE(
      base::test::RunUntil([&]() { return !window()->IsPeekShowing(); }))
      << "the card outlived the cluster's close button";
}

}  // namespace
}  // namespace atrium
