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
#include "chrome/browser/ui/scoped_tabbed_browser_displayer.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace {

// Same technique atrium_multi_profile_teardown_browsertest.cc uses:
// BrowserController has no public accessor for its own Browser*.
class NewBrowserCapture : public BrowserCollectionObserver {
 public:
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    captured_ = browser;
  }

  raw_ptr<BrowserWindowInterface> captured_ = nullptr;
};

using AtriumLittleWindowFindTabbedBrowserBrowserTest = InProcessBrowserTest;

// With only a little window open for a profile,
// ScopedTabbedBrowserDisplayer - one of FindTabbedBrowser's ~26 callers -
// must open a fresh normal browser instead of reusing the little one, and
// the little window's single-tab invariant must survive untouched.
IN_PROC_BROWSER_TEST_F(AtriumLittleWindowFindTabbedBrowserBrowserTest,
                       DisplayerOpensANewBrowserInsteadOfTheLittleWindow) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL seed_url = embedded_test_server()->GetURL("/title1.html");

  ProfileManager* profile_manager = g_browser_process->profile_manager();
  ASSERT_TRUE(profile_manager);
  Profile& little_profile = profiles::testing::CreateProfileSync(
      profile_manager, profile_manager->GenerateNextProfileDirectoryPath());

  NewBrowserCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      capture_observation(&capture);
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());

  atrium::BrowserController* little =
      atrium::BrowserController::CreateLittleWindow(&little_profile, seed_url,
                                                      std::nullopt);
  ASSERT_TRUE(little);
  capture_observation.Reset();
  ASSERT_TRUE(capture.captured_);

  content::WebContents* seed_tab =
      capture.captured_->GetTabStripModel()->GetWebContentsAt(0);
  ASSERT_TRUE(seed_tab);
  ASSERT_TRUE(content::WaitForLoadStop(seed_tab));

  Browser* displayed = nullptr;
  {
    // Scoped so the displayer's own dtor (Show()/Restore() on `displayed`)
    // runs before that window is closed below - it must not touch it after.
    chrome::ScopedTabbedBrowserDisplayer displayer(&little_profile);
    displayed = displayer.browser();
    ASSERT_TRUE(displayed);

    EXPECT_NE(displayed, capture.captured_->GetBrowserForMigrationOnly())
        << "ScopedTabbedBrowserDisplayer reused the little window instead of "
           "opening a new tabbed browser";
    EXPECT_EQ(1, capture.captured_->GetTabStripModel()->count())
        << "the little window's single-tab invariant broke";
  }

  // Browser::Create() leaves the strip empty. Give it a tab before closing.
  chrome::AddTabAt(displayed, GURL("about:blank"), /*index=*/-1,
                    /*foreground=*/true);
  content::WebContents* displayed_tab =
      displayed->tab_strip_model()->GetActiveWebContents();
  ASSERT_TRUE(displayed_tab);
  ASSERT_TRUE(content::WaitForLoadStop(displayed_tab));

  chrome::CloseWindow(displayed);
  ui_test_utils::WaitForBrowserToClose();
  chrome::CloseWindow(capture.captured_);
  ui_test_utils::WaitForBrowserToClose();
}

}  // namespace
