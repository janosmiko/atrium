// Copyright 2026 The Atrium Authors

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/browser.h"
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

using AtriumMoveLittleTabToSpaceBrowserTest = InProcessBrowserTest;

// BrowserController has no public accessor for its own Browser* - the
// production code (AdoptTabIntoLittleWindow's own test) reaches a window's
// TabStripModel through tabs::TabInterface::GetBrowserWindowInterface()
// instead, but that needs an existing WebContents to start from, which is
// exactly what this test needs to FIND. OnBrowserCreated fires synchronously
// on CreateLittleWindow's own call stack (see BrowserController::
// CollectionObserver's comment on the same guarantee), so a short-lived
// observer captures the new little window's BrowserWindowInterface without
// needing BrowserController to expose one.
class NewBrowserCapture : public BrowserCollectionObserver {
 public:
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    captured_ = browser;
  }

  raw_ptr<BrowserWindowInterface> captured_ = nullptr;
};

// the cross-profile half of BrowserController::
// MoveLittleTabToSpace (atrium_browser_controller.cc) - a little window's
// tab cannot move live into a window on a DIFFERENT BrowserContext (a
// WebContents is bound to the profile that created it), so that branch
// reopens the URL fresh in the target's profile instead of detaching and
// re-inserting the WebContents. The same-profile branch already has a VM
// scenario (build/scenarios/foyer-move-to-space.json) proving the live
// move; this is the branch that has none.
//
// MoveLittleTabToSpace takes the cross-profile branch whenever its own
// profile scan (Registry(), matching profile_id_) finds no OTHER resident
// controller for the little window's profile - see its own scan order. An
// earlier version of this test used browser()->profile() for the little
// window (already resident via browser() itself) and then called
// CloseBrowserSynchronously(browser()) mid-test to remove that match. That
// was the real trigger of the FATAL this test chased for two prior lanes:
// in this headless VM, CloseBrowserSynchronously's 30s wait on browser()
// TIMED OUT rather than completing (GlobalBrowserCollection still reported
// 3 browsers afterward, not 2), and the renderer instability that followed
// ("chrome renderer died" repeated 5x while retrying a reload) eventually
// hit FATAL Check failed: new_parent_ui_layer->GetCompositor() in
// BrowserCompositorMac::SetParentUiLayer. It is the in-test synchronous
// close of a live window that this VM's headless compositor cannot
// service, not simply having more than one window open: the
// AtriumAdoptTabIntoLittleWindowBrowserTest also runs with two live windows
// (browser() plus an adopted little window) and tears both down cleanly
// through ordinary end-of-test teardown, with no mid-test window close at
// all. An earlier note gave "multi-window teardown crashes" as the
// conclusion; the real fault line is narrower than that.
//
// This version avoids the whole class of problem instead of working around
// it: the little window opens on a brand-new profile that has no resident
// controller of its own, so MoveLittleTabToSpace's same-profile scan finds
// nothing and falls straight through to the cross-profile branch - no
// mid-test close is needed to get there, and the fixture's own browser()
// window (already open, a different profile) is the natural cross-profile
// target.
IN_PROC_BROWSER_TEST_F(AtriumMoveLittleTabToSpaceBrowserTest,
                       CrossProfileReopensURL) {
  ProfileManager* profile_manager = g_browser_process->profile_manager();
  ASSERT_TRUE(profile_manager);

  // A real URL is needed - the reopened tab must actually commit so
  // GetLastCommittedURL() (what MoveLittleTabToSpace reopens) and
  // WaitForLoadStop below have something real to observe. This sandboxed
  // VM has no outbound network, so a live https://example.com never
  // resolves; the embedded test server serves the same test data over a
  // real loopback connection instead.
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL cross_profile_url =
      embedded_test_server()->GetURL("/title1.html");

  // A profile with no Browser at all: MoveLittleTabToSpace's same-profile
  // scan finds no match for it, so the cross-profile branch fires without
  // needing to close any window mid-test.
  Profile& little_profile = profiles::testing::CreateProfileSync(
      profile_manager, profile_manager->GenerateNextProfileDirectoryPath());

  NewBrowserCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      capture_observation(&capture);
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());

  atrium::BrowserController* little =
      atrium::BrowserController::CreateLittleWindow(
          &little_profile, cross_profile_url, std::nullopt);
  ASSERT_TRUE(little);
  capture_observation.Reset();
  ASSERT_TRUE(capture.captured_)
      << "CreateLittleWindow's Browser::Create did not fire "
         "OnBrowserCreated - GlobalBrowserCollection wiring changed";

  ASSERT_EQ(2u, GlobalBrowserCollection::GetInstance()->GetSize())
      << "expected browser() + the new little window";

  // MoveLittleTabToSpace's cross-profile branch reopens
  // GetLastCommittedURL(), not the URL the little window was seeded with -
  // that is empty until the little window's own tab finishes navigating,
  // so the move must wait for that commit first, exactly like a real user
  // would (there is always human delay between opening a little window and
  // pressing Move to Space).
  content::WebContents* little_tab =
      capture.captured_->GetTabStripModel()->GetWebContentsAt(0);
  ASSERT_TRUE(little_tab);
  ASSERT_TRUE(content::WaitForLoadStop(little_tab));

  const int target_tab_count_before = browser()->tab_strip_model()->count();

  little->MoveLittleTabToSpace();

  // chrome::CloseWindow(browser_) inside MoveLittleTabToSpace closes the
  // little window via a posted NSWindow close on Mac, not synchronously -
  // wait for that specific event rather than assume it already happened.
  ui_test_utils::WaitForBrowserToClose();

  EXPECT_EQ(1u, GlobalBrowserCollection::GetInstance()->GetSize())
      << "the little window did not close - the cross-profile branch "
         "must close it exactly like the same-profile branch does";

  TabStripModel* target_strip = browser()->tab_strip_model();
  ASSERT_EQ(target_tab_count_before + 1, target_strip->count())
      << "the handed-over tab never reached the target profile's window";
  content::WebContents* reopened_tab =
      target_strip->GetWebContentsAt(target_strip->count() - 1);
  EXPECT_EQ(cross_profile_url, reopened_tab->GetVisibleURL())
      << "the cross-profile branch must REOPEN the URL (a fresh "
         "WebContents), not attempt a live move across BrowserContexts";
}

}  // namespace
