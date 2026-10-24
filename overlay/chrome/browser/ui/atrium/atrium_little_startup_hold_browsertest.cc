// Copyright 2026 The Atrium Authors

#include <optional>

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace {

class NewBrowserCapture : public BrowserCollectionObserver {
 public:
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    captured_ = browser;
  }

  raw_ptr<BrowserWindowInterface> captured_ = nullptr;
};

using AtriumLittleStartupHoldBrowserTest = InProcessBrowserTest;

IN_PROC_BROWSER_TEST_F(AtriumLittleStartupHoldBrowserTest,
                       ALittleWindowShowsWithoutTheHold) {
  NewBrowserCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      capture_observation(&capture);
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());

  atrium::BrowserController* little =
      atrium::BrowserController::CreateLittleWindow(
          browser()->profile(), GURL("https://example.com/little-hold"),
          std::nullopt);
  capture_observation.Reset();

  ASSERT_TRUE(little);
  ASSERT_TRUE(capture.captured_);
  Browser* little_browser = capture.captured_->GetBrowserForMigrationOnly();
  ASSERT_TRUE(little_browser);

  auto* window =
      static_cast<atrium::AtriumBrowserWindow*>(little_browser->window());
  ASSERT_TRUE(window);
  EXPECT_FALSE(window->StartupHoldPendingForTesting());
  EXPECT_TRUE(window->IsVisible());

  chrome::CloseWindow(little_browser);
  ui_test_utils::WaitForBrowserToClose();
}

}  // namespace
