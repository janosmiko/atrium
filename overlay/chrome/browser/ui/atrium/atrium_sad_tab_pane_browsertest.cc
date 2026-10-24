// Copyright 2026 The Atrium Authors

#include "base/test/run_until.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/views/sad_tab_view.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/view.h"
#include "ui/views/view_utils.h"

// a page whose renderer died left the card blank. Chromium builds a
// SadTabView but parents it into a ContentsWebView it finds through
// BrowserView, and an Atrium window has neither.
namespace {

SadTabView* SadTabIn(views::WebView* pane_web_view) {
  for (views::View* child : pane_web_view->children()) {
    if (auto* sad_tab = views::AsViewClass<SadTabView>(child)) {
      return sad_tab;
    }
  }
  return nullptr;
}

using AtriumSadTabPaneBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(AtriumSadTabPaneBrowserTest,
                       ACrashedRendererRaisesASadTabOverThePane) {
  ASSERT_TRUE(embedded_test_server()->Start());
  // NavigateToURL on an Atrium window never returns.
  content::WebContents* tab = chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL("/title1.html"), -1,
      /*foreground=*/true);
  ASSERT_TRUE(content::WaitForLoadStop(tab));

  auto* window = static_cast<atrium::AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  views::WebView* pane_web_view = window->PaneWebViewForContents(tab);
  ASSERT_TRUE(pane_web_view) << "no pane hosts the tab, so the crash below "
                                "would prove nothing about the card";
  ASSERT_FALSE(SadTabIn(pane_web_view))
      << "a sad tab before the crash makes the read after it meaningless";

  content::CrashTab(tab);

  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return SadTabIn(pane_web_view) != nullptr; }))
      << "the renderer died and nothing was raised over the pane - the card "
         "stays blank, which is the reported bug";
  EXPECT_TRUE(SadTabIn(pane_web_view)->GetVisible())
      << "the overlay was parented but left hidden, so the card is still blank";
}

}  // namespace
