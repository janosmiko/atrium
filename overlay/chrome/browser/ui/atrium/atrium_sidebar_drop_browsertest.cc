// Copyright 2026 The Atrium Authors

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
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
#include "url/gurl.h"

namespace {

// Same short-lived capture as atrium_little_move_cross_profile_browsertest.cc
// - OnBrowserCreated fires synchronously on CreateLittleWindow's own call
// stack, so this grabs the new window's BrowserWindowInterface without
// BrowserController exposing one itself.
class NewBrowserCapture : public BrowserCollectionObserver {
 public:
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    captured_ = browser;
  }

  raw_ptr<BrowserWindowInterface> captured_ = nullptr;
};

using AtriumSidebarDropBrowserTest = InProcessBrowserTest;

// BrowserController::RouteSidebarDrop is the
// handler ClassifyAndRouteDroppedTab's kSidebar branch calls once a release
// point is ALREADY classified as landing on another window's sidebar - this
// test drives that handler directly with a synthetic (source, target) pair
// rather than re-deriving the classification through real screen
// coordinates, which the classifier tests already cover in
// isolation. Same workspace (profile) - the tab moves live, no prompt.
//
// The target is built via CreateLittleWindow rather than CreateForProfile:
// RouteSidebarDrop only reads profile_id_ and the tab strip, so little_'s
// value is irrelevant to it, and CreateLittleWindow skips the sidebar/
// command-bar/extension-bridge block CreateForProfile builds - the same
// reason atrium_little_move_cross_profile_browsertest.cc's cross-profile
// target uses it. A CreateForProfile target hit a FATAL Check failed:
// new_parent_ui_layer->GetCompositor() reproducibly in this headless VM,
// even after ./build/vm.sh restore - not the documented stale-state flake,
// but that block's own machinery, sidestepped by using the primitive this
// task's own RouteSidebarDrop already depends on.
IN_PROC_BROWSER_TEST_F(AtriumSidebarDropBrowserTest,
                       SameWorkspaceMovesTabLiveWithNoPrompt) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL dragged_url = embedded_test_server()->GetURL("/title1.html");
  chrome::AddTabAt(browser(), dragged_url, /*index=*/-1, /*foreground=*/true);

  TabStripModel* source_strip = browser()->tab_strip_model();
  const int dragged_index = source_strip->count() - 1;
  content::WebContents* dragged_contents =
      source_strip->GetWebContentsAt(dragged_index);
  ASSERT_TRUE(dragged_contents);
  const int source_count_before = source_strip->count();

  const GURL target_seed_url = embedded_test_server()->GetURL("/title2.html");

  NewBrowserCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      capture_observation(&capture);
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());

  // Same profile as browser() - the "same workspace" case.
  atrium::BrowserController* target =
      atrium::BrowserController::CreateLittleWindow(
          browser()->profile(), target_seed_url, std::nullopt);
  ASSERT_TRUE(target);
  capture_observation.Reset();
  ASSERT_TRUE(capture.captured_);

  content::WebContents* target_seed_tab =
      capture.captured_->GetTabStripModel()->GetWebContentsAt(0);
  ASSERT_TRUE(target_seed_tab);
  ASSERT_TRUE(content::WaitForLoadStop(target_seed_tab));

  atrium::BrowserController* source =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(source)
      << "browser() has no attached BrowserController - "
         "InstallCollectionObserver wiring changed";

  TabStripModel* target_strip = capture.captured_->GetTabStripModel();
  const int target_count_before = target_strip->count();

  source->RouteSidebarDrop(dragged_contents, target);

  EXPECT_EQ(source_count_before - 1, source_strip->count())
      << "same-workspace drop must remove the tab from its source strip";
  EXPECT_EQ(target_count_before + 1, target_strip->count())
      << "same-workspace drop must land the tab in the target's strip, "
         "silently";
  EXPECT_EQ(dragged_contents,
            target_strip->GetWebContentsAt(target_strip->count() - 1))
      << "the moved WebContents did not land in the target strip";
}

// The other half: a different workspace shows the confirmation
// prompt and does not move until it answers - unanswered here.
// DifferentWorkspaceConfirmMovesTab/CancelDoesNotMove below cover the answers.
IN_PROC_BROWSER_TEST_F(AtriumSidebarDropBrowserTest,
                       DifferentWorkspaceAsksFirstAndDoesNotMove) {
  // An earlier version of this test assumed browser() starts with exactly
  // 1 tab (stock Chromium's InProcessBrowserTest default) and asserted
  // ASSERT_EQ(1, source_strip->count()) before adding anything - false in
  // this build: Atrium's M2c Arc-parity bootstrap can start a profile's
  // window with ZERO tabs, and this VM's fixture does. Fixed by adding a
  // tab instead of assuming an index, below.
  //
  // Root cause of the FATAL Check failed:
  // new_parent_ui_layer->GetCompositor() this test used to hit: a second
  // real Profile's CreateLittleWindow window left open for
  // InProcessBrowserTest's own fixture teardown races a new
  // RenderWidgetHostView's SetParentUiLayer against a compositor already
  // torn down. atrium_multi_profile_teardown_browsertest.cc's
  // AskFirstBranchClosedBeforeTeardown_Passes proved the fix: close that
  // window explicitly - chrome::CloseWindow + WaitForBrowserToClose, NOT
  // CloseBrowserSynchronously (30s timeout in the headless VM) - before the
  // test ends. Applied at the bottom of this test.
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

  // browser()'s tab strip, whatever it already holds (Atrium's M2c
  // Arc-parity bootstrap can start it with zero tabs, unlike stock
  // Chromium's single about:blank default - CHECK_GT below caught that
  // wrong assumption once already). Add one about:blank tab as the
  // "dragged" one rather than assume an index - identity/count is all this
  // test asserts on, never its content.
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
      << "a cross-workspace drop must not move the tab before the user "
         "confirms - no confirmation UI exists yet";
  EXPECT_EQ(target_count_before, target_strip->count())
      << "a cross-workspace drop must not land in the target strip without "
         "confirmation";

  // See close the second-profile window here, during the test,
  // instead of leaving it for fixture teardown to close alongside browser().
  chrome::CloseWindow(capture.captured_);
  ui_test_utils::WaitForBrowserToClose();
}

// SetSidebarMoveConfirmResultForTesting(true) answers
// the prompt synchronously - nothing in a gtest can click the real box.
IN_PROC_BROWSER_TEST_F(AtriumSidebarDropBrowserTest,
                       DifferentWorkspaceConfirmReopensAndCloses) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL target_seed_url = embedded_test_server()->GetURL("/title2.html");
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
          &other_profile, target_seed_url, std::nullopt);
  ASSERT_TRUE(target);
  capture_observation.Reset();
  ASSERT_TRUE(capture.captured_);

  content::WebContents* target_seed_tab =
      capture.captured_->GetTabStripModel()->GetWebContentsAt(0);
  ASSERT_TRUE(target_seed_tab);
  ASSERT_TRUE(content::WaitForLoadStop(target_seed_tab));

  // A real, committed URL - the reopened tab must actually navigate there,
  // so GetVisibleURL() below has something real to compare against.
  chrome::AddTabAt(browser(), dragged_url, /*index=*/-1,
                    /*foreground=*/true);
  TabStripModel* source_strip = browser()->tab_strip_model();
  ASSERT_GT(source_strip->count(), 0);
  const int dragged_index = source_strip->count() - 1;
  content::WebContents* dragged_contents =
      source_strip->GetWebContentsAt(dragged_index);
  ASSERT_TRUE(dragged_contents);
  ASSERT_TRUE(content::WaitForLoadStop(dragged_contents));
  const int source_count_before = source_strip->count();

  atrium::BrowserController* source =
      atrium::BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(source);

  TabStripModel* target_strip = capture.captured_->GetTabStripModel();
  const int target_count_before = target_strip->count();

  atrium::BrowserController::SetSidebarMoveConfirmResultForTesting(true);
  source->RouteSidebarDrop(dragged_contents, target);
  atrium::BrowserController::SetSidebarMoveConfirmResultForTesting(
      std::nullopt);

  EXPECT_EQ(source_count_before - 1, source_strip->count())
      << "confirming a cross-profile drop must close the source tab";
  EXPECT_EQ(target_count_before + 1, target_strip->count())
      << "confirming a cross-profile drop must land a new tab in the "
         "target's strip";
  content::WebContents* reopened_tab =
      target_strip->GetWebContentsAt(target_strip->count() - 1);
  EXPECT_NE(dragged_contents, reopened_tab)
      << "a cross-profile move must REOPEN the URL (a fresh WebContents), "
         "not hand the source WebContents to a different BrowserContext";
  EXPECT_EQ(dragged_url, reopened_tab->GetVisibleURL())
      << "the reopened tab must navigate to the source tab's URL";

  chrome::CloseWindow(capture.captured_);
  ui_test_utils::WaitForBrowserToClose();
}

// the opposite answer must leave the tab untouched.
IN_PROC_BROWSER_TEST_F(AtriumSidebarDropBrowserTest,
                       DifferentWorkspaceCancelDoesNotMove) {
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

  atrium::BrowserController::SetSidebarMoveConfirmResultForTesting(false);
  source->RouteSidebarDrop(dragged_contents, target);
  atrium::BrowserController::SetSidebarMoveConfirmResultForTesting(
      std::nullopt);

  EXPECT_EQ(source_count_before, source_strip->count())
      << "cancelling a cross-workspace drop must leave the tab in its "
         "source strip";
  EXPECT_EQ(target_count_before, target_strip->count())
      << "cancelling a cross-workspace drop must not land the tab in the "
         "target strip";
  EXPECT_EQ(dragged_contents, source_strip->GetWebContentsAt(dragged_index))
      << "the tab must stay exactly where it was";

  chrome::CloseWindow(capture.captured_);
  ui_test_utils::WaitForBrowserToClose();
}

}  // namespace
