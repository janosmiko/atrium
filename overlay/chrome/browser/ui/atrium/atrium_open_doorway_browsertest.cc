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

// BrowserController has no public accessor for its own Browser*.
class NewBrowserCapture : public BrowserCollectionObserver {
 public:
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    captured_ = browser;
  }

  raw_ptr<BrowserWindowInterface> captured_ = nullptr;
};

using AtriumOpenDoorwayBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(AtriumOpenDoorwayBrowserTest,
                       OpensAChromelessSingleTabWindowUnderTheOwningProfile) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL doorway_url = embedded_test_server()->GetURL("/title1.html");

  ProfileManager* profile_manager = g_browser_process->profile_manager();
  ASSERT_TRUE(profile_manager);
  Profile& space_profile = profiles::testing::CreateProfileSync(
      profile_manager, profile_manager->GenerateNextProfileDirectoryPath());

  NewBrowserCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      capture_observation(&capture);
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());

  atrium::BrowserController* doorway =
      atrium::BrowserController::OpenOrFocusDoorwayWindow(&space_profile,
                                                           doorway_url);
  ASSERT_TRUE(doorway);
  capture_observation.Reset();
  ASSERT_TRUE(capture.captured_);
  EXPECT_TRUE(doorway->IsLittle());
  EXPECT_EQ(&space_profile, capture.captured_->GetProfile());

  TabStripModel* strip = capture.captured_->GetTabStripModel();
  ASSERT_EQ(1, strip->count());
  content::WebContents* tab = strip->GetWebContentsAt(0);
  ASSERT_TRUE(tab);
  ASSERT_TRUE(content::WaitForLoadStop(tab));
  EXPECT_EQ(doorway_url, tab->GetLastCommittedURL());

  chrome::CloseWindow(capture.captured_->GetBrowserForMigrationOnly());
  ui_test_utils::WaitForBrowserToClose();
}

IN_PROC_BROWSER_TEST_F(AtriumOpenDoorwayBrowserTest,
                       ReopeningTheSameDoorwayFocusesTheExistingWindow) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL doorway_url = embedded_test_server()->GetURL("/title1.html");

  ProfileManager* profile_manager = g_browser_process->profile_manager();
  ASSERT_TRUE(profile_manager);
  Profile& space_profile = profiles::testing::CreateProfileSync(
      profile_manager, profile_manager->GenerateNextProfileDirectoryPath());

  NewBrowserCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      capture_observation(&capture);
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());

  atrium::BrowserController* first =
      atrium::BrowserController::OpenOrFocusDoorwayWindow(&space_profile,
                                                           doorway_url);
  ASSERT_TRUE(first);
  capture_observation.Reset();
  ASSERT_TRUE(capture.captured_);
  BrowserWindowInterface* first_browser = capture.captured_;
  content::WebContents* first_tab =
      first_browser->GetTabStripModel()->GetWebContentsAt(0);
  ASSERT_TRUE(first_tab);
  ASSERT_TRUE(content::WaitForLoadStop(first_tab));

  const size_t browsers_before =
      GlobalBrowserCollection::GetInstance()->GetSize();

  // A second capture, armed only for this call: if reopening the same
  // doorway built a new window instead of reusing `first_browser`, this
  // fires and `capture.captured_` is non-null below.
  capture.captured_ = nullptr;
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());
  atrium::BrowserController* second =
      atrium::BrowserController::OpenOrFocusDoorwayWindow(&space_profile,
                                                           doorway_url);
  capture_observation.Reset();

  EXPECT_EQ(first, second);
  EXPECT_FALSE(capture.captured_)
      << "reopening the same doorway created a new browser window";
  EXPECT_EQ(browsers_before, GlobalBrowserCollection::GetInstance()->GetSize());

  chrome::CloseWindow(first_browser->GetBrowserForMigrationOnly());
  ui_test_utils::WaitForBrowserToClose();
}

IN_PROC_BROWSER_TEST_F(AtriumOpenDoorwayBrowserTest,
                       TheOriginalPinnedTabStaysInItsOriginalStrip) {
  ASSERT_TRUE(embedded_test_server()->Start());
  const GURL pinned_url = embedded_test_server()->GetURL("/title1.html");
  chrome::AddTabAt(browser(), pinned_url, /*index=*/-1, /*foreground=*/true);

  TabStripModel* source_strip = browser()->tab_strip_model();
  const int pinned_index = source_strip->count() - 1;
  source_strip->SetTabPinned(pinned_index, true);
  content::WebContents* pinned_tab =
      source_strip->GetWebContentsAt(pinned_index);
  ASSERT_TRUE(pinned_tab);
  ASSERT_TRUE(content::WaitForLoadStop(pinned_tab));
  const int count_before = source_strip->count();

  NewBrowserCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      capture_observation(&capture);
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());

  atrium::BrowserController* doorway =
      atrium::BrowserController::OpenOrFocusDoorwayWindow(browser()->profile(),
                                                           pinned_url);
  ASSERT_TRUE(doorway);
  capture_observation.Reset();
  ASSERT_TRUE(capture.captured_);

  EXPECT_EQ(count_before, source_strip->count());
  EXPECT_TRUE(source_strip->IsTabPinned(pinned_index));
  EXPECT_EQ(pinned_tab, source_strip->GetWebContentsAt(pinned_index));

  chrome::CloseWindow(capture.captured_->GetBrowserForMigrationOnly());
  ui_test_utils::WaitForBrowserToClose();
}

}  // namespace
