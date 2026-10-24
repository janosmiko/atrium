// Copyright 2026 The Atrium Authors

#include <vector>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
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
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "url/gurl.h"

namespace {

// Same short-lived capture atrium_content_drop_browsertest.cc uses -
// OnBrowserCreated fires synchronously on the creating call's own stack.
class NewBrowserCapture : public BrowserCollectionObserver {
 public:
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    captured_ = browser;
  }

  raw_ptr<BrowserWindowInterface> captured_ = nullptr;
};

std::vector<atrium::AtriumBrowserWindow*> WindowsOtherThan(Browser* source) {
  std::vector<atrium::AtriumBrowserWindow*> others;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        Browser* as_browser = candidate->GetBrowserForMigrationOnly();
        if (as_browser && as_browser != source) {
          if (atrium::AtriumBrowserWindow* window =
                  atrium::BrowserController::PresentedWindowForBrowser(
                      as_browser)) {
            others.push_back(window);
          }
        }
        return true;
      });
  return others;
}

// A point in the left split band of `window`'s content area - the zone that
// draws a highlight, unlike the centre.
gfx::Point LeftBandOf(atrium::AtriumBrowserWindow* window) {
  const gfx::Rect bounds = window->GetBounds();
  const int content_left = bounds.x() + window->LastDockedSidebarWidth();
  return gfx::Point(content_left + (bounds.right() - content_left) / 20,
                    bounds.y() + bounds.height() / 2);
}

using AtriumDropTargetGuardBrowserTest = InProcessBrowserTest;

// First half: an inactive profile bootstraps with show=false and keeps
// full-size centred bounds, so before the visibility guard a release over
// empty desktop landed in a window nobody could see.
IN_PROC_BROWSER_TEST_F(AtriumDropTargetGuardBrowserTest,
                       HiddenWindowIsNeverADropTarget) {
  ASSERT_TRUE(embedded_test_server()->Start());
  atrium::BrowserController* source =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(source);
  ASSERT_TRUE(atrium::BrowserController::CreateWindowSlot(browser()));
  ASSERT_TRUE(atrium::BrowserController::CreateWindowSlot(browser()));

  std::vector<atrium::AtriumBrowserWindow*> others = WindowsOtherThan(browser());
  ASSERT_EQ(2u, others.size());

  // Far apart on purpose: the drop point below is inside the hidden window
  // and inside nothing else, so a candidate list that still holds it has
  // nowhere else to put the tab.
  others[0]->SetBounds(gfx::Rect(0, 0, 800, 600));
  others[1]->SetBounds(gfx::Rect(900, 0, 800, 600));
  others[0]->Hide();
  ASSERT_TRUE(others[0]->IsExcludedFromWindowCycling());
  const gfx::Point over_hidden = LeftBandOf(others[0]);

  source->PreviewDropForPoint(over_hidden);
  EXPECT_FALSE(others[0]->IsContentDropPreviewVisibleForTesting())
      << "a hidden window offered itself as a drop target";
  EXPECT_FALSE(others[1]->IsContentDropPreviewVisibleForTesting());
  atrium::BrowserController::ClearDropPreviews();

  chrome::AddTabAt(browser(), embedded_test_server()->GetURL("/title1.html"),
                   /*index=*/-1, /*foreground=*/true);
  TabStripModel* source_strip = browser()->tab_strip_model();
  content::WebContents* dragged =
      source_strip->GetWebContentsAt(source_strip->count() - 1);
  ASSERT_TRUE(dragged);
  const int hidden_count_before =
      others[0]->browser()->tab_strip_model()->count();

  NewBrowserCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      capture_observation(&capture);
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());
  source->ClassifyAndRouteDroppedTab(dragged, over_hidden);
  capture_observation.Reset();

  EXPECT_EQ(hidden_count_before,
            others[0]->browser()->tab_strip_model()->count())
      << "the tab landed in the hidden window's strip";
  ASSERT_TRUE(capture.captured_) << "the release should have torn off instead";

  chrome::CloseWindow(capture.captured_);
  ui_test_utils::WaitForBrowserToClose();
}

// Second half: two stacked windows are decided by candidate order
// alone, so activating one and then the other is what separates a live
// z-order from any fixed one.
IN_PROC_BROWSER_TEST_F(AtriumDropTargetGuardBrowserTest,
                       FrontWindowOfTwoOverlappingOnesTakesTheDrop) {
  atrium::BrowserController* source =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(source);
  ASSERT_TRUE(atrium::BrowserController::CreateWindowSlot(browser()));
  ASSERT_TRUE(atrium::BrowserController::CreateWindowSlot(browser()));

  std::vector<atrium::AtriumBrowserWindow*> others = WindowsOtherThan(browser());
  ASSERT_EQ(2u, others.size());
  const gfx::Rect stacked(0, 0, 800, 600);
  others[0]->SetBounds(stacked);
  others[1]->SetBounds(stacked);
  const gfx::Point over_both = LeftBandOf(others[0]);

  others[0]->Activate();
  source->PreviewDropForPoint(over_both);
  EXPECT_TRUE(others[0]->IsContentDropPreviewVisibleForTesting())
      << "the drop skipped the front window";
  EXPECT_FALSE(others[1]->IsContentDropPreviewVisibleForTesting());

  others[1]->Activate();
  source->PreviewDropForPoint(over_both);
  EXPECT_TRUE(others[1]->IsContentDropPreviewVisibleForTesting())
      << "the drop stayed on the window that is now behind";
  EXPECT_FALSE(others[0]->IsContentDropPreviewVisibleForTesting());

  atrium::BrowserController::ClearDropPreviews();
}

// A WebContents is bound to its creating BrowserContext, so a live move
// leaves the tab with the target profile's cookies. The target is a little
// window because CreateForProfile trips a compositor CHECK in this VM.
IN_PROC_BROWSER_TEST_F(AtriumDropTargetGuardBrowserTest,
                       CrossProfileContentDropReopensInsteadOfMoving) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL dragged_url = embedded_test_server()->GetURL("/title1.html");

  ProfileManager* profile_manager = g_browser_process->profile_manager();
  ASSERT_TRUE(profile_manager);
  Profile& other_profile = profiles::testing::CreateProfileSync(
      profile_manager, profile_manager->GenerateNextProfileDirectoryPath());

  NewBrowserCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      capture_observation(&capture);
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());
  atrium::BrowserController* target =
      atrium::BrowserController::CreateLittleWindow(
          &other_profile, embedded_test_server()->GetURL("/title2.html"),
          std::nullopt);
  capture_observation.Reset();
  ASSERT_TRUE(target);
  ASSERT_TRUE(capture.captured_);

  TabStripModel* target_strip = capture.captured_->GetTabStripModel();
  content::WebContents* target_seed = target_strip->GetWebContentsAt(0);
  ASSERT_TRUE(target_seed);
  ASSERT_TRUE(content::WaitForLoadStop(target_seed));
  const int target_count_before = target_strip->count();

  chrome::AddTabAt(browser(), dragged_url, /*index=*/-1, /*foreground=*/true);
  TabStripModel* source_strip = browser()->tab_strip_model();
  content::WebContents* dragged =
      source_strip->GetWebContentsAt(source_strip->count() - 1);
  ASSERT_TRUE(dragged);
  ASSERT_TRUE(content::WaitForLoadStop(dragged));
  const int source_count_before = source_strip->count();

  atrium::BrowserController* source =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(source);
  source->RouteContentDrop(dragged, target, gfx::Rect(0, 0, 800, 600),
                           gfx::Point(750, 300));

  EXPECT_EQ(source_count_before - 1, source_strip->count())
      << "the source tab must close";
  EXPECT_EQ(target_count_before + 1, target_strip->count());
  content::WebContents* reopened =
      target_strip->GetWebContentsAt(target_strip->count() - 1);
  ASSERT_TRUE(reopened);
  EXPECT_NE(dragged, reopened)
      << "the live WebContents was handed to a different BrowserContext";
  EXPECT_EQ(dragged_url, reopened->GetVisibleURL());

  // See a second profile's window left for fixture teardown races a
  // torn-down compositor.
  chrome::CloseWindow(capture.captured_);
  ui_test_utils::WaitForBrowserToClose();
}

// A little window is single-tab by construction - a second tab gets no
// sidebar row, and MoveLittleTabToSpace then refuses to move it back out.
IN_PROC_BROWSER_TEST_F(AtriumDropTargetGuardBrowserTest,
                       LittleWindowNeverTakesASecondTab) {
  ASSERT_TRUE(embedded_test_server()->Start());

  NewBrowserCapture little_capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      little_observation(&little_capture);
  little_observation.Observe(GlobalBrowserCollection::GetInstance());
  ASSERT_TRUE(atrium::BrowserController::CreateLittleWindow(
      browser()->profile(), embedded_test_server()->GetURL("/title2.html"),
      std::nullopt));
  little_observation.Reset();
  ASSERT_TRUE(little_capture.captured_);
  Browser* little_browser =
      little_capture.captured_->GetBrowserForMigrationOnly();
  ASSERT_TRUE(little_browser);
  atrium::AtriumBrowserWindow* little_window =
      atrium::BrowserController::PresentedWindowForBrowser(little_browser);
  ASSERT_TRUE(little_window);
  little_window->SetBounds(gfx::Rect(0, 0, 800, 600));
  ASSERT_EQ(1, little_browser->tab_strip_model()->count());

  chrome::AddTabAt(browser(), embedded_test_server()->GetURL("/title1.html"),
                   /*index=*/-1, /*foreground=*/true);
  TabStripModel* source_strip = browser()->tab_strip_model();
  content::WebContents* dragged =
      source_strip->GetWebContentsAt(source_strip->count() - 1);
  ASSERT_TRUE(dragged);

  atrium::BrowserController* source =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(source);

  NewBrowserCapture torn_off_capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      torn_off_observation(&torn_off_capture);
  torn_off_observation.Observe(GlobalBrowserCollection::GetInstance());
  source->ClassifyAndRouteDroppedTab(dragged, LeftBandOf(little_window));
  torn_off_observation.Reset();

  EXPECT_EQ(1, little_browser->tab_strip_model()->count())
      << "the drop gave the little window a second tab";
  ASSERT_TRUE(torn_off_capture.captured_)
      << "the release should have torn off into its own little window";

  chrome::CloseWindow(torn_off_capture.captured_);
  ui_test_utils::WaitForBrowserToClose();
  chrome::CloseWindow(little_capture.captured_);
  ui_test_utils::WaitForBrowserToClose();
}

// An incognito profile's GetBaseName() is its parent's, so profile_id_ reads
// an OTR window as the same workspace - a live regular WebContents would land
// in an OTR strip, which is the one hand-over no reopen branch catches.
IN_PROC_BROWSER_TEST_F(AtriumDropTargetGuardBrowserTest,
                       IncognitoWindowIsNeverADropTarget) {
  ASSERT_TRUE(embedded_test_server()->Start());
  Browser* incognito = CreateIncognitoBrowser();
  ASSERT_TRUE(incognito);
  ASSERT_TRUE(incognito->profile()->IsOffTheRecord());
  atrium::AtriumBrowserWindow* incognito_window =
      atrium::BrowserController::PresentedWindowForBrowser(incognito);
  ASSERT_TRUE(incognito_window)
      << "no Atrium window for the incognito browser, so this proves nothing";
  incognito_window->SetBounds(gfx::Rect(0, 0, 800, 600));
  const int incognito_count_before = incognito->tab_strip_model()->count();
  const gfx::Point over_incognito = LeftBandOf(incognito_window);

  chrome::AddTabAt(browser(), embedded_test_server()->GetURL("/title1.html"),
                   /*index=*/-1, /*foreground=*/true);
  TabStripModel* source_strip = browser()->tab_strip_model();
  content::WebContents* dragged =
      source_strip->GetWebContentsAt(source_strip->count() - 1);
  ASSERT_TRUE(dragged);

  atrium::BrowserController* source =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(source);

  source->PreviewDropForPoint(over_incognito);
  EXPECT_FALSE(incognito_window->IsContentDropPreviewVisibleForTesting())
      << "an incognito window offered itself as a drop target";
  atrium::BrowserController::ClearDropPreviews();

  NewBrowserCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      capture_observation(&capture);
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());
  source->ClassifyAndRouteDroppedTab(dragged, over_incognito);
  capture_observation.Reset();

  EXPECT_EQ(incognito_count_before, incognito->tab_strip_model()->count())
      << "a regular tab was handed to an off-the-record strip";
  ASSERT_TRUE(capture.captured_) << "the release should have torn off instead";

  chrome::CloseWindow(capture.captured_);
  ui_test_utils::WaitForBrowserToClose();
  chrome::CloseWindow(incognito);
  ui_test_utils::WaitForBrowserToClose();
}

}  // namespace
