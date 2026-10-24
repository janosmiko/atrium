// Copyright 2026 The Atrium Authors

#include <utility>
#include <vector>

#include "base/functional/bind.h"
#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_split_orientation_decision.h"
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

// Same short-lived capture atrium_sidebar_drop_browsertest.cc uses -
// OnBrowserCreated fires synchronously on CreateLittleWindow's own call
// stack.
class NewBrowserCapture : public BrowserCollectionObserver {
 public:
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    captured_ = browser;
  }

  raw_ptr<BrowserWindowInterface> captured_ = nullptr;
};

using AtriumContentDropBrowserTest = InProcessBrowserTest;

// The split-insertion primitive has not landed,
// so this can only prove the orientation RouteContentDrop would hand it,
// not the resulting pane layout.
IN_PROC_BROWSER_TEST_F(AtriumContentDropBrowserTest,
                       AllFiveZonesClassifyTheOrientationTheLabWould) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL dragged_url = embedded_test_server()->GetURL("/title1.html");
  chrome::AddTabAt(browser(), dragged_url, /*index=*/-1, /*foreground=*/true);
  TabStripModel* source_strip = browser()->tab_strip_model();
  content::WebContents* dragged_contents =
      source_strip->GetWebContentsAt(source_strip->count() - 1);
  ASSERT_TRUE(dragged_contents);
  const int source_count_before = source_strip->count();

  NewBrowserCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      capture_observation(&capture);
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());
  atrium::BrowserController* target =
      atrium::BrowserController::CreateLittleWindow(
          browser()->profile(),
          embedded_test_server()->GetURL("/title2.html"), std::nullopt);
  capture_observation.Reset();
  ASSERT_TRUE(target);
  ASSERT_TRUE(capture.captured_);
  Browser* target_browser = capture.captured_->GetBrowserForMigrationOnly();
  ASSERT_TRUE(target_browser);

  atrium::BrowserController* source =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(source);

  const gfx::Rect content_bounds(0, 0, 800, 600);
  std::vector<
      std::pair<atrium::BrowserController*, atrium::ContentDropOrientation>>
      observed;
  atrium::BrowserController::SetContentDropInsertionCallbackForTesting(
      base::BindRepeating(
          [](std::vector<std::pair<atrium::BrowserController*,
                                    atrium::ContentDropOrientation>>* out,
             atrium::BrowserController* target,
             atrium::ContentDropOrientation orientation) {
            out->emplace_back(target, orientation);
          },
          &observed));

  const struct {
    gfx::Point point;
    atrium::ContentDropOrientation expected;
  } kZones[] = {
      // L
      {gfx::Point(50, 300), atrium::ContentDropOrientation::kHorizontalSplit},
      // R
      {gfx::Point(750, 300), atrium::ContentDropOrientation::kHorizontalSplit},
      // T
      {gfx::Point(400, 50), atrium::ContentDropOrientation::kVerticalSplit},
      // B
      {gfx::Point(400, 550), atrium::ContentDropOrientation::kVerticalSplit},
      // C
      {gfx::Point(400, 300), atrium::ContentDropOrientation::kSwap},
  };
  for (const auto& zone : kZones) {
    source->RouteContentDrop(dragged_contents, target, content_bounds,
                              zone.point);
  }

  atrium::BrowserController::SetContentDropInsertionCallbackForTesting(
      base::RepeatingCallback<void(atrium::BrowserController*,
                                    atrium::ContentDropOrientation)>());

  ASSERT_EQ(std::size(kZones), observed.size());
  auto observed_it = observed.begin();
  for (const auto& zone : kZones) {
    EXPECT_EQ(target, observed_it->first);
    EXPECT_EQ(zone.expected, observed_it->second);
    ++observed_it;
  }

  // No split-insertion primitive exists yet, so the dragged
  // tab must still be exactly where it started - nothing here should have
  // moved it.
  EXPECT_EQ(source_count_before, source_strip->count());

  chrome::CloseWindow(target_browser);
  ui_test_utils::WaitForBrowserToClose();
}

// no testing callback set, so RouteContentDrop takes
// the real InsertAtLeaf-backed path instead of the classification-only seam.
IN_PROC_BROWSER_TEST_F(AtriumContentDropBrowserTest,
                       ContentDropOpensNewPaneAndEmptiesSourceStrip) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL dragged_url = embedded_test_server()->GetURL("/title1.html");
  chrome::AddTabAt(browser(), dragged_url, /*index=*/-1, /*foreground=*/true);
  TabStripModel* source_strip = browser()->tab_strip_model();
  content::WebContents* dragged_contents =
      source_strip->GetWebContentsAt(source_strip->count() - 1);
  ASSERT_TRUE(dragged_contents);
  ASSERT_TRUE(content::WaitForLoadStop(dragged_contents));
  const uint64_t dragged_tab_id = atrium::TabId(dragged_contents);
  const int source_count_before = source_strip->count();

  NewBrowserCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      capture_observation(&capture);
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());
  atrium::BrowserController* target =
      atrium::BrowserController::CreateLittleWindow(
          browser()->profile(),
          embedded_test_server()->GetURL("/title2.html"), std::nullopt);
  capture_observation.Reset();
  ASSERT_TRUE(target);
  ASSERT_TRUE(capture.captured_);
  Browser* target_browser = capture.captured_->GetBrowserForMigrationOnly();
  ASSERT_TRUE(target_browser);
  content::WebContents* target_primary_contents =
      target_browser->tab_strip_model()->GetWebContentsAt(0);
  ASSERT_TRUE(target_primary_contents);
  ASSERT_TRUE(content::WaitForLoadStop(target_primary_contents));
  const uint64_t target_primary_tab_id = atrium::TabId(target_primary_contents);

  atrium::BrowserController* source =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(source);

  // Right band (fx > 0.75) - a horizontal split, per the lab's zoneOf().
  const gfx::Rect content_bounds(0, 0, 800, 600);
  const gfx::Point right_band(750, 300);
  source->RouteContentDrop(dragged_contents, target, content_bounds,
                            right_band);

  // Gone from the source's own strip.
  EXPECT_EQ(source_count_before - 1, source_strip->count());
  EXPECT_EQ(TabStripModel::kNoTab,
            source_strip->GetIndexOfWebContents(dragged_contents));

  // Opened as a real second pane in the target window, paired with
  // its pre-existing tab. BrowserController::window() is private - same
  // static_cast every other atrium/*.cc call site uses to reach it.
  auto* target_window =
      static_cast<atrium::AtriumBrowserWindow*>(target_browser->window());
  EXPECT_TRUE(target_window->IsSplit());
  EXPECT_EQ(dragged_tab_id,
            target_window->SplitPartnerTabId(target_primary_tab_id));
  EXPECT_NE(TabStripModel::kNoTab,
            target_browser->tab_strip_model()->GetIndexOfWebContents(
                dragged_contents));

  chrome::CloseWindow(target_browser);
  ui_test_utils::WaitForBrowserToClose();
}

// EnterSplit's default-ratio path (InsertPaneAtLeaf's
// was_unsplit branch) must not clobber a ratio the user already chose, once
// they've left and re-entered a split within the same session.
IN_PROC_BROWSER_TEST_F(AtriumContentDropBrowserTest,
                       SplitRatioSurvivesReEntryWithinSession) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL first_url = embedded_test_server()->GetURL("/title1.html");
  const GURL second_url = embedded_test_server()->GetURL("/title2.html");
  chrome::AddTabAt(browser(), first_url, /*index=*/-1, /*foreground=*/true);
  chrome::AddTabAt(browser(), second_url, /*index=*/-1, /*foreground=*/true);
  TabStripModel* source_strip = browser()->tab_strip_model();
  content::WebContents* first_dragged_contents =
      source_strip->GetWebContentsAt(source_strip->count() - 2);
  content::WebContents* second_dragged_contents =
      source_strip->GetWebContentsAt(source_strip->count() - 1);
  ASSERT_TRUE(first_dragged_contents);
  ASSERT_TRUE(second_dragged_contents);

  NewBrowserCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      capture_observation(&capture);
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());
  atrium::BrowserController* target =
      atrium::BrowserController::CreateLittleWindow(
          browser()->profile(),
          embedded_test_server()->GetURL("/title3.html"), std::nullopt);
  capture_observation.Reset();
  ASSERT_TRUE(target);
  ASSERT_TRUE(capture.captured_);
  Browser* target_browser = capture.captured_->GetBrowserForMigrationOnly();
  ASSERT_TRUE(target_browser);
  auto* target_window =
      static_cast<atrium::AtriumBrowserWindow*>(target_browser->window());

  atrium::BrowserController* source =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(source);

  const gfx::Rect content_bounds(0, 0, 800, 600);
  const gfx::Point right_band(750, 300);

  // Enter the split, then set a ratio far from the 0.5 default.
  source->RouteContentDrop(first_dragged_contents, target, content_bounds,
                            right_band);
  ASSERT_TRUE(target_window->IsSplit());
  target_window->UpdateSplit(atrium::SplitOrientation::kSideBySide, 0.7);
  ASSERT_NEAR(0.7, target_window->SplitRatioForTesting(), 0.01);

  // Leave the split within this same session.
  target_window->CloseSplit(nullptr);
  ASSERT_FALSE(target_window->IsSplit());

  // Re-enter with a different tab. The ratio the user chose above must
  // survive - not reset to the fresh-split 0.5 default.
  source->RouteContentDrop(second_dragged_contents, target, content_bounds,
                            right_band);
  ASSERT_TRUE(target_window->IsSplit());
  EXPECT_NEAR(0.7, target_window->SplitRatioForTesting(), 0.01);

  chrome::CloseWindow(target_browser);
  ui_test_utils::WaitForBrowserToClose();
}

}  // namespace

// the live edge highlight. Driven through the same entry point the
// drag feed uses, because the gesture itself cannot be synthesised here - a
// cross-window HTML5 drag needs a real pointer held down across two OS windows.
// What this pins is the half a scenario could not: a point resolves to a
// highlight on the OTHER window, moving across zones moves it, and the end of a
// drag clears it.
IN_PROC_BROWSER_TEST_F(AtriumContentDropBrowserTest, PreviewTracksTheCursor) {
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  ASSERT_TRUE(atrium::BrowserController::CreateWindowSlot(browser()))
      << "a second window is what the preview is drawn in";

  Browser* other_browser = nullptr;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        Browser* as_browser = static_cast<Browser*>(candidate);
        if (as_browser != browser()) {
          other_browser = as_browser;
          return false;  // Stop at the first one that is not ours.
        }
        return true;
      });
  ASSERT_TRUE(other_browser);
  atrium::AtriumBrowserWindow* other_window =
      atrium::BrowserController::PresentedWindowForBrowser(other_browser);
  ASSERT_TRUE(other_window);

  const gfx::Rect bounds = other_window->GetBounds();
  const int content_left = bounds.x() + other_window->LastDockedSidebarWidth();
  const int content_width = bounds.right() - content_left;
  const int mid_y = bounds.y() + bounds.height() / 2;

  controller->PreviewDropForPoint(
      gfx::Point(content_left + content_width / 20, mid_y));
  ASSERT_TRUE(other_window->IsContentDropPreviewVisibleForTesting())
      << "no highlight appeared in the window the point is over";
  const gfx::Rect left_band =
      other_window->ContentDropPreviewBoundsForTesting();

  // The band must MOVE. A preview that only ever appears would satisfy a
  // visibility check while pointing at the wrong edge the whole time.
  controller->PreviewDropForPoint(
      gfx::Point(bounds.right() - content_width / 20, mid_y));
  EXPECT_TRUE(other_window->IsContentDropPreviewVisibleForTesting());
  EXPECT_NE(left_band, other_window->ContentDropPreviewBoundsForTesting());

  // The centre is a swap, not a split, so there is no edge to draw.
  controller->PreviewDropForPoint(
      gfx::Point(content_left + content_width / 2, mid_y));
  EXPECT_FALSE(other_window->IsContentDropPreviewVisibleForTesting());

  // The gesture ending clears it, however it ended.
  controller->PreviewDropForPoint(
      gfx::Point(content_left + content_width / 20, mid_y));
  ASSERT_TRUE(other_window->IsContentDropPreviewVisibleForTesting());
  atrium::BrowserController::ClearDropPreviews();
  EXPECT_FALSE(other_window->IsContentDropPreviewVisibleForTesting());
}

// The other half, and it needs a THIRD window to be visible at all: with one
// candidate the highlight is always on the only window that could have it, so a
// build that never cleared the others would pass the test above unchanged.
//
// A pointer crossing straight from one window into another produces no leave
// event for the first, so clearing only the window being left strands a
// highlight there for the rest of the drag.
IN_PROC_BROWSER_TEST_F(AtriumContentDropBrowserTest,
                       MovingBetweenWindowsLeavesOneHighlight) {
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  ASSERT_TRUE(atrium::BrowserController::CreateWindowSlot(browser()));
  ASSERT_TRUE(atrium::BrowserController::CreateWindowSlot(browser()));

  std::vector<atrium::AtriumBrowserWindow*> others;
  GlobalBrowserCollection::GetInstance()->ForEach(
      [&](BrowserWindowInterface* candidate) {
        Browser* as_browser = static_cast<Browser*>(candidate);
        if (as_browser != browser()) {
          if (atrium::AtriumBrowserWindow* window =
                  atrium::BrowserController::PresentedWindowForBrowser(
                      as_browser)) {
            others.push_back(window);
          }
        }
        return true;
      });
  ASSERT_EQ(2u, others.size()) << "this needs two windows besides the source";

  // Moved apart on purpose. A fresh slot opens at the default position, so
  // without this the two sit on top of each other, every point is inside both,
  // and the classifier's first-match rule makes the test look like a clearing
  // bug when the geometry was simply ambiguous.
  others[0]->SetBounds(gfx::Rect(0, 0, 800, 600));
  others[1]->SetBounds(gfx::Rect(900, 0, 800, 600));
  ASSERT_FALSE(others[0]->GetBounds().Intersects(others[1]->GetBounds()));

  const auto left_edge_of = [](atrium::AtriumBrowserWindow* window) {
    const gfx::Rect bounds = window->GetBounds();
    const int content_left = bounds.x() + window->LastDockedSidebarWidth();
    return gfx::Point(content_left + (bounds.right() - content_left) / 20,
                      bounds.y() + bounds.height() / 2);
  };

  controller->PreviewDropForPoint(left_edge_of(others[0]));
  ASSERT_TRUE(others[0]->IsContentDropPreviewVisibleForTesting());

  controller->PreviewDropForPoint(left_edge_of(others[1]));
  EXPECT_TRUE(others[1]->IsContentDropPreviewVisibleForTesting());
  EXPECT_FALSE(others[0]->IsContentDropPreviewVisibleForTesting())
      << "the window the pointer left still carries a highlight";
}
