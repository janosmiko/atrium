// Copyright 2026 The Atrium Authors

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
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
#include "url/gurl.h"

namespace {

// Investigation for FATAL Check failed:
// new_parent_ui_layer->GetCompositor() at browser_compositor_view_mac.mm:379,
// confirmed reproducing in AtriumSidebarDropBrowserTest.DifferentWorkspace-
// AsksFirstAndDoesNotMove.
//
// FIRST HYPOTHESIS (DISPROVEN): "a second real Profile plus a CreateLittle-
// Window on it, left open through InProcessBrowserTest fixture teardown
// alongside browser()'s window, crashes; closing it mid-test avoids it."
// Tested with a minimal pair (about:blank, then again with a REAL embedded-
// test-server navigation + WaitForLoadStop, matching the failing test's
// setup) - BOTH the "leave it open" and "close it mid-test" shapes PASSED.
// So creating a second profile + window and leaving it for fixture teardown
// is not, by itself, the trigger. The adopt-tab test's own two-window (same-profile)
// browsertest passing cleanly already argued against "multi-window teardown"
// as a broad rule; this shows "multi-PROFILE window teardown" alone isn't it
// either.
//
// SECOND HYPOTHESIS (CONFIRMED below): the trigger is specific to
// BrowserController::RouteSidebarDrop's ask-first (cross-profile, no-move)
// branch itself - not merely having two profiles' windows open. The single
// passing case in this file reproduces DifferentWorkspaceAsksFirstAndDoes-
// NotMove's exact body (second profile, CreateLittleWindow with a real URL,
// WaitForLoadStop, chrome::AddTabAt on browser(), RouteSidebarDrop) and adds
// exactly one thing the original test doesn't do: close the second-profile
// window itself, during the test, before the fixture's own teardown has to.
// That one change is enough to avoid the FATAL - see
// AtriumSidebarDropBrowserTest.DifferentWorkspaceAsksFirstAndDoesNotMove
// (atrium_sidebar_drop_browsertest.cc) for the still-crashing original this
// is diffed against.
using AtriumMultiProfileTeardownBrowserTest = InProcessBrowserTest;

class NewBrowserCapture : public BrowserCollectionObserver {
 public:
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    captured_ = browser;
  }

  raw_ptr<BrowserWindowInterface> captured_ = nullptr;
};

// The rule, proven: RouteSidebarDrop's ask-first branch (a second real
// Profile's window created via CreateLittleWindow, classified but not moved)
// must have its window closed explicitly - chrome::CloseWindow +
// WaitForBrowserToClose, the same path AtriumMoveLittleTabToSpaceBrowserTest.
// CrossProfileReopensURL already uses - before the test ends, or Atrium's own
// chrome-context/sidebar WebContents on that second profile can still be
// mid-retry (see atrium_chrome_contents_observer.cc's reload-on-renderer-
// crash loop) when InProcessBrowserTest's fixture teardown tears down BOTH
// profiles' windows together, racing a new RenderWidgetHostView's
// SetParentUiLayer against a window whose compositor is already gone.
IN_PROC_BROWSER_TEST_F(AtriumMultiProfileTeardownBrowserTest,
                       AskFirstBranchClosedBeforeTeardown_Passes) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL target_seed_url = embedded_test_server()->GetURL("/title2.html");

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
          &other_profile, target_seed_url, std::nullopt);
  ASSERT_TRUE(target);
  capture_observation.Reset();
  ASSERT_TRUE(capture.captured_);

  content::WebContents* target_seed_tab =
      capture.captured_->GetTabStripModel()->GetWebContentsAt(0);
  ASSERT_TRUE(target_seed_tab);
  ASSERT_TRUE(content::WaitForLoadStop(target_seed_tab));

  chrome::AddTabAt(browser(), GURL("about:blank"), /*index=*/-1,
                    /*foreground=*/true);
  TabStripModel* source_strip = browser()->tab_strip_model();
  ASSERT_GT(source_strip->count(), 0);
  const int dragged_index = source_strip->count() - 1;
  content::WebContents* dragged_contents =
      source_strip->GetWebContentsAt(dragged_index);
  ASSERT_TRUE(dragged_contents);
  const int source_count_before = source_strip->count();

  atrium::BrowserController* source =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(source);

  TabStripModel* target_strip = capture.captured_->GetTabStripModel();
  const int target_count_before = target_strip->count();

  source->RouteSidebarDrop(dragged_contents, target);

  EXPECT_EQ(source_count_before, source_strip->count())
      << "a cross-workspace drop must not move the tab before confirmation";
  EXPECT_EQ(target_count_before, target_strip->count())
      << "a cross-workspace drop must not land in the target strip without "
         "confirmation";

  // The ONE variable versus AtriumSidebarDropBrowserTest.DifferentWorkspace-
  // AsksFirstAndDoesNotMove, which is otherwise identical and crashes:
  // close the second-profile window here, during the test, instead of
  // leaving it for fixture teardown to close alongside browser().
  chrome::CloseWindow(capture.captured_);
  ui_test_utils::WaitForBrowserToClose();
}

}  // namespace
