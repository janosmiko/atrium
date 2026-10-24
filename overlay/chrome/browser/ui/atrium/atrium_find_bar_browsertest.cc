// Copyright 2026 The Atrium Authors

#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/run_loop.h"
#include "base/test/run_until.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/find_in_page/find_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace {

class AtriumFindBarBrowserTest : public InProcessBrowserTest {};

// Cmd+F twice must not build a second widget. The window owns one instance
// for its lifetime, the same contract CommandBarWidget keeps.
IN_PROC_BROWSER_TEST_F(AtriumFindBarBrowserTest, ASecondOpenReusesTheBar) {
  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1, true);
  ASSERT_TRUE(content::WaitForLoadStop(tab));
  atrium::AtriumBrowserWindow* window =
      atrium::BrowserController::PresentedWindowForBrowser(browser());
  ASSERT_TRUE(window);
  window->OpenFindBar();
  content::WebContents* first = window->FindBarContentsForTesting();
  ASSERT_TRUE(first);
  window->OpenFindBar();
  EXPECT_EQ(first, window->FindBarContentsForTesting());
  EXPECT_TRUE(window->IsFindBarShowing());
}

// A key window is not enough: until the WebView is the widget's focused view
// every keystroke stops at the root and the bar is visible but deaf.
IN_PROC_BROWSER_TEST_F(AtriumFindBarBrowserTest, TheFieldHoldsTheKeyboard) {
  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1, true);
  ASSERT_TRUE(content::WaitForLoadStop(tab));
  atrium::AtriumBrowserWindow* window =
      atrium::BrowserController::PresentedWindowForBrowser(browser());
  ASSERT_TRUE(window);
  window->OpenFindBar();
  ASSERT_TRUE(window->FindBarContentsForTesting());
  EXPECT_TRUE(window->FindBarPageHoldsKeyboardForTesting())
      << "the field never took the keyboard, so nothing typed after Cmd+F "
         "reaches the query field";
}

// Escape gives the page back the keyboard, not just the pixels.
IN_PROC_BROWSER_TEST_F(AtriumFindBarBrowserTest,
                       ClosingReturnsFocusToThePage) {
  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1, true);
  ASSERT_TRUE(content::WaitForLoadStop(tab));
  atrium::AtriumBrowserWindow* window =
      atrium::BrowserController::PresentedWindowForBrowser(browser());
  ASSERT_TRUE(window);
  window->OpenFindBar();
  ASSERT_TRUE(window->FindBarPageHoldsKeyboardForTesting());

  window->CloseFindBar();

  EXPECT_FALSE(window->IsFindBarShowing());
  EXPECT_FALSE(window->FindBarPageHoldsKeyboardForTesting())
      << "the hidden bar still holds the keyboard, so the page below it stays "
         "deaf after Escape";
}

// The command bar closes on deactivation and a find bar must not: the
// highlights and the count outlive the focus.
IN_PROC_BROWSER_TEST_F(AtriumFindBarBrowserTest, LosingKeyDoesNotCloseTheBar) {
  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1, true);
  ASSERT_TRUE(content::WaitForLoadStop(tab));
  atrium::AtriumBrowserWindow* window =
      atrium::BrowserController::PresentedWindowForBrowser(browser());
  ASSERT_TRUE(window);
  window->OpenFindBar();
  ASSERT_TRUE(window->IsFindBarShowing());

  window->ClearFindBarPageFocusForTesting();
  base::RunLoop().RunUntilIdle();

  EXPECT_TRUE(window->IsFindBarShowing())
      << "a click on the page dismissed the bar and took the highlights with "
         "it";
}

// The bar goes and the highlights go with it, on the tab that had them rather
// than on whichever tab the window moved to.
IN_PROC_BROWSER_TEST_F(AtriumFindBarBrowserTest,
                       SwitchingTabsClosesTheBarAndClearsTheOldTab) {
  content::WebContents* first =
      chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1, true);
  ASSERT_TRUE(content::WaitForLoadStop(first));
  atrium::AtriumBrowserWindow* window =
      atrium::BrowserController::PresentedWindowForBrowser(browser());
  ASSERT_TRUE(window);
  window->OpenFindBar();
  window->FindQueryChangedForTesting(u"the");
  ASSERT_TRUE(find_in_page::FindTabHelper::FromWebContents(first)
                  ->is_find_session_active());

  content::WebContents* second =
      chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1, true);
  content::WaitForLoadStopWithoutSuccessCheck(second);

  EXPECT_FALSE(window->IsFindBarShowing());
  EXPECT_FALSE(find_in_page::FindTabHelper::FromWebContents(first)
                   ->is_find_session_active())
      << "the old tab kept its find session, so Chrome will build a "
         "FindBarController the next time that tab is inserted anywhere";
}

// A tab carrying a live session, inserted into a Browser, makes Browser build
// a FindBarController, and CreateFindBar CHECKs on a chromeless window.
IN_PROC_BROWSER_TEST_F(AtriumFindBarBrowserTest,
                       HandingTheTabToAnotherWindowDoesNotCrash) {
  content::WebContents* searched =
      chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1, true);
  content::WaitForLoadStopWithoutSuccessCheck(searched);
  atrium::AtriumBrowserWindow* window =
      atrium::BrowserController::PresentedWindowForBrowser(browser());
  ASSERT_TRUE(window);
  window->OpenFindBar();
  window->FindQueryChangedForTesting(u"the");
  ASSERT_TRUE(find_in_page::FindTabHelper::FromWebContents(searched)
                  ->is_find_session_active());

  Browser* other = CreateBrowser(browser()->profile());
  // A bare new window whose surfaces are still loading never signals its own
  // close, so settle its first tab before anything else (docs/BUILD.md).
  content::WaitForLoadStopWithoutSuccessCheck(
      other->tab_strip_model()->GetActiveWebContents());
  TabStripModel* source = browser()->tab_strip_model();
  const int index = source->GetIndexOfWebContents(searched);
  ASSERT_NE(TabStripModel::kNoTab, index);
  TabStripModel* dest = other->tab_strip_model();
  ASSERT_TRUE(atrium::MoveWebContentsBetweenStrips(source, index, dest,
                                                   dest->count()));

  EXPECT_FALSE(find_in_page::FindTabHelper::FromWebContents(searched)
                   ->is_find_session_active());
  EXPECT_FALSE(window->IsFindBarShowing());

  // Fixture teardown of a second window crashes the compositor, so close it
  // here (docs/BUILD.md).
  chrome::CloseWindow(other);
  ui_test_utils::WaitForBrowserToClose(other);
}

// The same handoff, measured on key rather than on the session: a bar closing
// in a background window must not haul that window in front of the one the
// user is working in.
IN_PROC_BROWSER_TEST_F(AtriumFindBarBrowserTest,
                       AHandoffFromABackgroundWindowKeepsKey) {
  content::WebContents* searched =
      chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1, true);
  content::WaitForLoadStopWithoutSuccessCheck(searched);
  atrium::AtriumBrowserWindow* background =
      atrium::BrowserController::PresentedWindowForBrowser(browser());
  ASSERT_TRUE(background);
  background->OpenFindBar();
  background->FindQueryChangedForTesting(u"the");
  ASSERT_TRUE(background->IsFindBarShowing());

  Browser* other = CreateBrowser(browser()->profile());
  // A bare new window whose surfaces are still loading never signals its own
  // close, so settle its first tab before anything else (docs/BUILD.md).
  content::WaitForLoadStopWithoutSuccessCheck(
      other->tab_strip_model()->GetActiveWebContents());
  atrium::AtriumBrowserWindow* key_window =
      atrium::BrowserController::PresentedWindowForBrowser(other);
  ASSERT_TRUE(key_window);
  key_window->Activate();
  ASSERT_TRUE(base::test::RunUntil([&]() { return key_window->IsActive(); }))
      << "the second window never took key, so nothing below measures a steal";
  ASSERT_FALSE(background->IsActive())
      << "the first window still held key, so the handoff below is not from a "
         "background one";

  TabStripModel* source = browser()->tab_strip_model();
  const int index = source->GetIndexOfWebContents(searched);
  ASSERT_NE(TabStripModel::kNoTab, index);
  TabStripModel* dest = other->tab_strip_model();
  ASSERT_TRUE(atrium::MoveWebContentsBetweenStrips(source, index, dest,
                                                   dest->count()));
  base::RunLoop().RunUntilIdle();

  EXPECT_FALSE(background->IsFindBarShowing())
      << "the background window kept a bar over a tab it no longer holds";
  EXPECT_TRUE(key_window->IsActive())
      << "the background window's closing bar took key from the front window";
  EXPECT_FALSE(background->IsActive())
      << "the background window came forward on a handoff the user never "
         "looked at";

  // Fixture teardown of a second window crashes the compositor, so close it
  // here (docs/BUILD.md).
  chrome::CloseWindow(other);
  ui_test_utils::WaitForBrowserToClose(other);
}

// Cmd+F is not in Chrome's reserved list, so the chord reaches the renderer
// first and an editable field is the one thing that might consume it.
IN_PROC_BROWSER_TEST_F(AtriumFindBarBrowserTest, OpensOverAFocusedTextField) {
  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1, true);
  ASSERT_TRUE(content::WaitForLoadStop(tab));
  ASSERT_TRUE(content::ExecJs(tab,
                              "const field = document.createElement('input');"
                              "document.body.appendChild(field);"
                              "field.focus();"));
  ASSERT_EQ("INPUT", content::EvalJs(tab, "document.activeElement.tagName"));

  atrium::AtriumBrowserWindow* window =
      atrium::BrowserController::PresentedWindowForBrowser(browser());
  ASSERT_TRUE(window);
  window->OpenFindBar();

  EXPECT_TRUE(window->IsFindBarShowing());
  EXPECT_TRUE(window->FindBarPageHoldsKeyboardForTesting())
      << "the page's own text field kept the keyboard, so the query the user "
         "types after Cmd+F goes into the page instead of the bar";
}

// BrowserController has no public accessor for its own Browser*, so the new
// window has to be caught as it is created.
class FindBarNewBrowserCapture : public BrowserCollectionObserver {
 public:
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    captured_ = browser;
  }

  raw_ptr<BrowserWindowInterface> captured_ = nullptr;
};

// IDC_FIND is disabled with no active tab, so the chord never fires here. If
// it ever does, resolving a null WebContents has to be quiet rather than
// fatal.
IN_PROC_BROWSER_TEST_F(AtriumFindBarBrowserTest, OpeningWithNoTabIsQuiet) {
  FindBarNewBrowserCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      observation(&capture);
  observation.Observe(GlobalBrowserCollection::GetInstance());

  atrium::BrowserController* slot = atrium::BrowserController::CreateWindowSlot(
      browser(), /*screen_point=*/std::nullopt, /*open_command_bar=*/false);
  observation.Reset();
  ASSERT_TRUE(slot) << "CreateWindowSlot returned nullptr";
  ASSERT_TRUE(capture.captured_)
      << "CreateWindowSlot's Browser::Create did not fire OnBrowserCreated";

  Browser* slot_browser = capture.captured_->GetBrowserForMigrationOnly();
  ASSERT_TRUE(slot_browser);
  ASSERT_TRUE(slot_browser->tab_strip_model()->empty())
      << "CreateWindowSlot no longer opens tabless - test setup assumption "
         "broke";
  auto* slot_window =
      static_cast<atrium::AtriumBrowserWindow*>(slot_browser->window());
  ASSERT_TRUE(slot_window);

  slot_window->OpenFindBar();
  slot_window->FindNextMatch(true);
  slot_window->CloseFindBar();

  EXPECT_FALSE(slot_window->IsFindBarShowing())
      << "a closed bar still claims to be showing over a window with nothing "
         "to search";

  // The empty-strip suppression keeps this window open forever otherwise, and
  // a second window left standing crashes fixture teardown (docs/BUILD.md).
  slot_window->CloseUserInitiated();
}

// The palette is a separate activatable widget with no
// BrowserWindowCommandHandler, so commandDispatch: reaches no handler while it
// holds key.
IN_PROC_BROWSER_TEST_F(AtriumFindBarBrowserTest,
                       ThePaletteBeingUpDoesNotProduceASecondBar) {
  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1, true);
  ASSERT_TRUE(content::WaitForLoadStop(tab));
  atrium::AtriumBrowserWindow* window =
      atrium::BrowserController::PresentedWindowForBrowser(browser());
  ASSERT_TRUE(window);
  window->OpenCommandBar();
  ASSERT_TRUE(window->IsCommandBarShowing())
      << "the palette never opened, so nothing below measures a find bar "
         "built under it";

  window->OpenFindBar();
  content::WebContents* first = window->FindBarContentsForTesting();
  ASSERT_TRUE(first);
  window->OpenFindBar();

  EXPECT_EQ(first, window->FindBarContentsForTesting())
      << "opening under the palette built a second bar, so the first one's "
         "session and highlights are now unreachable";
  EXPECT_TRUE(window->IsFindBarShowing());
}

// The palette is a sibling overlay of the same parent window, not a foreign
// window, so the find bar's reclaim must recognise it as its own group
// rather than decline to touch it.
IN_PROC_BROWSER_TEST_F(AtriumFindBarBrowserTest,
                       ReclaimTreatsTheSiblingPaletteAsItsOwnGroup) {
  content::WebContents* tab =
      chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1, true);
  ASSERT_TRUE(content::WaitForLoadStop(tab));
  atrium::AtriumBrowserWindow* window =
      atrium::BrowserController::PresentedWindowForBrowser(browser());
  ASSERT_TRUE(window);

  window->OpenFindBar();
  ASSERT_TRUE(window->IsFindBarShowing());
  window->OpenCommandBar();
  ASSERT_TRUE(window->IsCommandBarShowing());
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window->CommandBarIsActiveForTesting(); }))
      << "the palette never took key, so nothing below measures a reclaim "
         "against it";

  EXPECT_TRUE(window->FindBarKeyIsInOwnGroupForTesting())
      << "the find bar read the palette holding key as a foreign window "
         "instead of its own group, so its reclaim would decline";
}

}  // namespace
