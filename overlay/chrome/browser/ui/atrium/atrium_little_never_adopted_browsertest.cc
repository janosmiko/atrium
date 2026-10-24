// Copyright 2026 The Atrium Authors

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

// Same technique atrium_little_move_cross_profile_browsertest.cc uses:
// BrowserController has no public accessor for its own Browser*, but
// OnBrowserCreated fires synchronously on CreateLittleWindow's own call
// stack, so a short-lived observer captures the new little window's
// BrowserWindowInterface without needing BrowserController to expose one.
class NewBrowserCapture : public BrowserCollectionObserver {
 public:
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    captured_ = browser;
  }

  raw_ptr<BrowserWindowInterface> captured_ = nullptr;
};

using AtriumLittleNeverAdoptedBrowserTest = InProcessBrowserTest;

// "A Little Browser is never adopted into a sidebar/
// host-registered controller." CreateLittleWindow's self-creating ctor runs
// inside the CreatingOwnBrowser()/CreatingLittleBrowser() bracket
// (atrium_browser_controller.cc, around the Browser::Create() call in the
// self-creating ctor) specifically so BrowserController::CollectionObserver::
// OnBrowserCreated skips it - without that bracket the observer's fallback
// path (`new BrowserController(browser, new_slot)`) would wire the SAME
// Browser up a second time through the generic, sidebar-bearing ctor arm.
//
// Black-box, from outside the ctor: a little window's AtriumBrowserWindow
// must report HasSidebar() == false (little_ guards the entire
// sidebar/RegisterHostInSlot block), IsLittle() must be true, and the
// controller ControllerForBrowser() resolves for that Browser must be the
// exact controller CreateLittleWindow returned - not a second, differently
// wired one that replaced or shadowed it.
IN_PROC_BROWSER_TEST_F(AtriumLittleNeverAdoptedBrowserTest,
                       NeverGetsASidebarOrADoubleController) {
  NewBrowserCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      capture_observation(&capture);
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());

  atrium::BrowserController* little =
      atrium::BrowserController::CreateLittleWindow(
          browser()->profile(), GURL("https://example.com/little-ac6"),
          std::nullopt);
  capture_observation.Reset();

  ASSERT_TRUE(little) << "CreateLittleWindow returned nullptr";
  ASSERT_TRUE(capture.captured_)
      << "CreateLittleWindow's Browser::Create did not fire "
         "OnBrowserCreated - GlobalBrowserCollection wiring changed";

  Browser* little_browser = capture.captured_->GetBrowserForMigrationOnly();
  ASSERT_TRUE(little_browser);

  // Same controller, not a second one adopted alongside it: if the
  // CreatingOwnBrowser()/CreatingLittleBrowser() bracket did not cover this
  // Browser::Create(), CollectionObserver::OnBrowserCreated would have
  // already run `new BrowserController(little_browser, new_slot)` on the
  // same synchronous call stack, and ControllerForBrowser would resolve
  // THAT controller instead (Registry() scan order finds whichever pushed
  // last).
  EXPECT_EQ(little,
            atrium::BrowserController::ControllerForBrowser(little_browser))
      << "ControllerForBrowser resolved a different controller than the one "
         "CreateLittleWindow returned - the Browser was adopted a second "
         "time";

  EXPECT_TRUE(little->IsLittle());

  // The structural proof: no sidebar, ever, on this window.
  auto* atrium_window =
      static_cast<atrium::AtriumBrowserWindow*>(little_browser->window());
  ASSERT_TRUE(atrium_window);
  EXPECT_FALSE(atrium_window->HasSidebar())
      << "a little window reported a sidebar - it was wired through the "
         "generic (adopted) controller path instead of staying little_";

  chrome::CloseWindow(little_browser);
  ui_test_utils::WaitForBrowserToClose();
}

}  // namespace
