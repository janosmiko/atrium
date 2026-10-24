// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_page_focus_browsertest.cc

#include "base/test/run_until.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/devtools/devtools_window_testing.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/focus_manager.h"
#include "url/gurl.h"

// A page shown without the keyboard also gets no hover until the first click,
// which is the user report behind page-hover-works-before-the-first-click.
namespace {

bool PageHasFocus(content::WebContents* contents) {
  content::RenderWidgetHostView* view = contents->GetRenderWidgetHostView();
  return view && view->HasFocus();
}

class AtriumPageFocusBrowserTest : public InProcessBrowserTest {
 protected:
  atrium::AtriumBrowserWindow* KeyWindow() {
    atrium::AtriumBrowserWindow* window =
        atrium::BrowserController::PresentedWindowForBrowser(browser());
    if (!window) {
      return nullptr;
    }
    window->Activate();
    return base::test::RunUntil([&] { return window->IsActive(); }) ? window
                                                                    : nullptr;
  }
};

IN_PROC_BROWSER_TEST_F(AtriumPageFocusBrowserTest,
                       TheDamageHealLeavesThePageTheKeyboard) {
  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1, true);
  content::WaitForLoadStopWithoutSuccessCheck(tab);
  atrium::AtriumBrowserWindow* window = KeyWindow();
  ASSERT_TRUE(window) << "the window never took key, so nothing below counts";
  window->FocusWebContentsPane();
  ASSERT_TRUE(base::test::RunUntil([&] { return PageHasFocus(tab); }));

  window->RunDamageHealForTesting();

  EXPECT_TRUE(PageHasFocus(tab))
      << "the heal re-attached the page without the keyboard";
}

IN_PROC_BROWSER_TEST_F(AtriumPageFocusBrowserTest,
                       TheDamageHealLeavesTheSidebarItsKeyboard) {
  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1, true);
  content::WaitForLoadStopWithoutSuccessCheck(tab);
  atrium::AtriumBrowserWindow* window = KeyWindow();
  ASSERT_TRUE(window) << "the window never took key, so nothing below counts";
  content::WebContents* sidebar = window->SidebarContentsForTesting();
  ASSERT_TRUE(sidebar);
  sidebar->Focus();
  ASSERT_TRUE(base::test::RunUntil([&] { return !PageHasFocus(tab); }));

  window->RunDamageHealForTesting();

  EXPECT_FALSE(PageHasFocus(tab))
      << "the heal took the keyboard from the sidebar";
}

// A split pane that already hosts the tab only changes which pane is active,
// and that exit left the keyboard in the sidebar the row was clicked in.
IN_PROC_BROWSER_TEST_F(AtriumPageFocusBrowserTest,
                       ARowClickOnTheOtherSplitPaneGivesItTheKeyboard) {
  content::WebContents* first =
      chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1, true);
  content::WaitForLoadStopWithoutSuccessCheck(first);
  content::WebContents* second =
      chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1, false);
  content::WaitForLoadStopWithoutSuccessCheck(second);
  atrium::AtriumBrowserWindow* window = KeyWindow();
  ASSERT_TRUE(window) << "the window never took key, so nothing below counts";
  window->EnterSplit(second, atrium::SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window->IsSplit());
  content::WebContents* shown = window->CurrentlyDisplayedWebContents();
  content::WebContents* other = shown == first ? second : first;

  content::WebContents* sidebar = window->SidebarContentsForTesting();
  ASSERT_TRUE(sidebar);
  sidebar->Focus();
  ASSERT_TRUE(base::test::RunUntil(
      [&] { return !PageHasFocus(shown) && !PageHasFocus(other); }));

  atrium::BrowserController* host =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(host);
  atrium::BrowserController::ActivateTab(host, atrium::TabId(other));
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window->CurrentlyDisplayedWebContents() == other;
  })) << "the row click did not select the other pane";

  EXPECT_TRUE(base::test::RunUntil([&] { return PageHasFocus(other); }))
      << "the other pane became active without the keyboard";
}

// OnActiveTabChanged and BrowserController both show the new tab, so the
// second call re-shows the tab already on screen.
IN_PROC_BROWSER_TEST_F(AtriumPageFocusBrowserTest,
                       ReShowingTheTabLeavesDockedDevtoolsItsKeyboard) {
  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1, true);
  content::WaitForLoadStopWithoutSuccessCheck(tab);
  atrium::AtriumBrowserWindow* window = KeyWindow();
  ASSERT_TRUE(window) << "the window never took key, so nothing below counts";
  DevToolsWindow* devtools =
      DevToolsWindowTesting::OpenDevToolsWindowSync(tab, /*is_docked=*/true);
  ASSERT_TRUE(devtools);
  window->UpdateDockedDevtools(tab);
  ASSERT_TRUE(window->PaneHasDockedDevtoolsForTesting(tab));
  content::WebContents* frontend =
      DevToolsWindowTesting::Get(devtools)->main_web_contents();
  frontend->Focus();
  ASSERT_TRUE(base::test::RunUntil([&] { return PageHasFocus(frontend); }));

  window->ShowTab(tab);

  EXPECT_TRUE(PageHasFocus(frontend))
      << "re-showing the tab moved the keyboard out of devtools";
  EXPECT_FALSE(PageHasFocus(tab));
  DevToolsWindowTesting::CloseDevToolsWindowSync(devtools);
}

IN_PROC_BROWSER_TEST_F(AtriumPageFocusBrowserTest,
                       SwitchingAwayFromDockedDevtoolsGivesTheNewTabTheKeyboard) {
  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1, true);
  content::WaitForLoadStopWithoutSuccessCheck(tab);
  content::WebContents* other =
      chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1, false);
  content::WaitForLoadStopWithoutSuccessCheck(other);
  atrium::AtriumBrowserWindow* window = KeyWindow();
  ASSERT_TRUE(window) << "the window never took key, so nothing below counts";
  // Nothing for the devtools focus tracker to hand back on detach.
  window->ActivePaneForTesting()->GetFocusManager()->ClearFocus();
  DevToolsWindow* devtools =
      DevToolsWindowTesting::OpenDevToolsWindowSync(tab, /*is_docked=*/true);
  ASSERT_TRUE(devtools);
  window->UpdateDockedDevtools(tab);
  ASSERT_TRUE(window->PaneHasDockedDevtoolsForTesting(tab));
  content::WebContents* frontend =
      DevToolsWindowTesting::Get(devtools)->main_web_contents();
  frontend->Focus();
  ASSERT_TRUE(base::test::RunUntil([&] { return PageHasFocus(frontend); }));
  const views::View* focused =
      window->ActivePaneForTesting()->GetFocusManager()->GetFocusedView();
  ASSERT_TRUE(focused);
  ASSERT_TRUE(
      window->ActivePaneForTesting()->devtools_web_view()->Contains(focused))
      << "views focus never reached the devtools view, so the switch proves "
         "nothing";

  window->ShowTab(other);

  EXPECT_TRUE(base::test::RunUntil([&] { return PageHasFocus(other); }))
      << "the tab switched to never got the keyboard from the old tab's "
         "devtools";
  EXPECT_FALSE(PageHasFocus(frontend));
  DevToolsWindowTesting::CloseDevToolsWindowSync(devtools);
}

IN_PROC_BROWSER_TEST_F(AtriumPageFocusBrowserTest,
                       ClosingTheCommandBarGivesTheNewPageTheKeyboard) {
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1, true);
  content::WaitForLoadStopWithoutSuccessCheck(tab);
  atrium::AtriumBrowserWindow* window = KeyWindow();
  ASSERT_TRUE(window) << "the window never took key, so nothing below counts";
  window->OpenCommandBar();
  ASSERT_TRUE(base::test::RunUntil([&] {
    return window->CommandBarIsActiveForTesting();
  })) << "the palette never took key, so its close hands nothing back";

  // What Return on a typed URL does: open, then close.
  window->OpenCommandBarMatch(embedded_test_server()->GetURL("/title1.html"));
  window->CloseCommandBar();
  content::WebContents* opened =
      browser()->tab_strip_model()->GetActiveWebContents();
  ASSERT_NE(tab, opened) << "the match did not open a new tab";
  ASSERT_TRUE(content::WaitForLoadStop(opened));

  EXPECT_TRUE(base::test::RunUntil([&] { return PageHasFocus(opened); }))
      << "the page the palette opened never got the keyboard";
}

}  // namespace
