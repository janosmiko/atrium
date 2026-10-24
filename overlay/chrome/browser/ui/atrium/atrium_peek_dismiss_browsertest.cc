// Copyright 2026 The Atrium Authors

#include <memory>
#include <vector>

#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/run_until.h"
#include "base/threading/thread_restrictions.h"
#include "base/values.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_manager.h"
#include "chrome/browser/profiles/profile_test_util.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_peek_intercept.h"
#include "chrome/browser/ui/atrium/atrium_peek_widget.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_delegate.h"
#include "content/public/common/referrer.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "content/public/test/test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/image/image.h"
#include "url/url_constants.h"
#include "ui/views/widget/widget.h"

// a click on the dimmed page outside the peek card closes it.
namespace atrium {
namespace {

class AtriumPeekDismissBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    InProcessBrowserTest::SetUpOnMainThread();
  }

  AtriumBrowserWindow* window() {
    return static_cast<AtriumBrowserWindow*>(browser()->window());
  }

  // Same startup-tab workaround as AtriumPeekControlsBrowserTest::OpenAPeek.
  bool OpenAPeek() {
    if (!embedded_test_server()->Start()) {
      return false;
    }
    content::WebContents* source = chrome::AddAndReturnTabAt(
        browser(), embedded_test_server()->GetURL("/title1.html"), -1,
        /*foreground=*/true);
    if (!source || !content::WaitForLoadStop(source)) {
      return false;
    }
    BrowserController* controller =
        BrowserController::ControllerForBrowser(browser());
    if (!controller) {
      return false;
    }
    controller->OpenPeek(embedded_test_server()->GetURL("/title2.html"),
                         content::Referrer(), gfx::Point(200, 200));
    return base::test::RunUntil([&]() { return window()->IsPeekShowing(); });
  }

  // A popup adopted as a peek never loads here, so the card keeps its loading
  // placeholder, and its origin chip names `host`.
  content::WebContents* AdoptAPeek(BrowserController* controller,
                                   Profile* profile,
                                   const std::string& host) {
    std::unique_ptr<content::WebContents> popup =
        content::WebContents::Create(content::WebContents::CreateParams(profile));
    content::WebContents* raw = popup.get();
    if (controller->AdoptPopupAsPeek(
            std::move(popup), gfx::Point(200, 200),
            embedded_test_server()->GetURL(host, "/title2.html"))) {
      return nullptr;
    }
    return base::test::RunUntil([&]() {
             return window()->PeekedWebContentsForTesting() == raw;
           })
               ? raw
               : nullptr;
  }

  // Swaps a second profile's controller into this window, so both controllers
  // open their peeks on it. Returns that controller's own, hidden browser.
  Browser* SwapInASecondController(BrowserController** second,
                                   bool with_source_tab = true) {
    BrowserController::ActivateWindow(
        BrowserController::ControllerForBrowser(browser()));
    ProfileManager* manager = g_browser_process->profile_manager();
    Profile& other = profiles::testing::CreateProfileSync(
        manager, manager->user_data_dir().AppendASCII("atrium-peek-badge"));
    *second = BrowserController::CreateForProfile(
        &other, /*show=*/false, /*startup_url=*/std::string(),
        BrowserController::kDefaultSlot);
    if (!*second) {
      return nullptr;
    }
    BrowserController::ActivateWindow(*second);
    Browser* other_browser = nullptr;
    GlobalBrowserCollection::GetInstance()->ForEach(
        [&](BrowserWindowInterface* candidate) {
          if (candidate->GetProfile() == &other) {
            other_browser = candidate->GetBrowserForMigrationOnly();
            return false;
          }
          return true;
        });
    if (!other_browser || BrowserController::PresentedWindowForBrowser(
                              other_browser) != window()) {
      return nullptr;
    }
    // The peek is keyed on its opener's active tab, and show=false has none.
    if (!with_source_tab) {
      return other_browser;
    }
    content::WebContents* source = chrome::AddAndReturnTabAt(
        other_browser, GURL(url::kAboutBlankURL), -1, /*foreground=*/true);
    if (!source || !content::WaitForLoadStop(source)) {
      return nullptr;
    }
    return other_browser;
  }

  // Opens a link peek from a fresh tab of `controller`, whose strip is `own`,
  // badged.
  content::WebContents* OpenAPeekFrom(BrowserController* controller,
                                      Browser* own) {
    BrowserController::ActivateWindow(controller);
    content::WebContents* source = chrome::AddAndReturnTabAt(
        own, embedded_test_server()->GetURL("source.test", "/title1.html"), -1,
        /*foreground=*/true);
    if (!source || !content::WaitForLoadStop(source)) {
      return nullptr;
    }
    return OpenABadgedPeek(controller);
  }

  // Opens a link peek from the tab on screen. The page has an icon of its
  // own, so the badge comes back from the page after a restore.
  content::WebContents* OpenABadgedPeek(BrowserController* controller) {
    controller->OpenPeek(
        embedded_test_server()->GetURL("peek.test",
                                       "/favicon/page_with_favicon.html"),
        content::Referrer(), gfx::Point(200, 200));
    content::WebContents* peek = controller->PeekContentsForTesting();
    if (!peek || !base::test::RunUntil([&]() {
          return window()->PeekedWebContentsForTesting() == peek &&
                 window()->IsPeekShowing();
        }) ||
        !content::WaitForLoadStop(peek)) {
      return nullptr;
    }
    return base::test::RunUntil(
               [&]() { return window()->PeekBadgeTabIdForTesting() != 0; })
               ? peek
               : nullptr;
  }

  // What a tab discard or a prerender activation does: same tab, new contents.
  void SwapTheContentsAt(int index) {
    browser()->tab_strip_model()->DiscardWebContentsAt(
        index, content::WebContents::Create(
                   content::WebContents::CreateParams(browser()->profile())));
  }

  static gfx::Image AnIcon() {
    SkBitmap bitmap;
    bitmap.allocN32Pixels(16, 16);
    bitmap.eraseColor(SK_ColorRED);
    return gfx::Image::CreateFrom1xBitmap(bitmap);
  }
};

IN_PROC_BROWSER_TEST_F(AtriumPeekDismissBrowserTest,
                       PressOnTheScrimDismissesTheCard) {
  ASSERT_TRUE(OpenAPeek()) << "the peek card never showed";

  window()->SimulatePeekScrimPressForTesting();

  EXPECT_TRUE(
      base::test::RunUntil([&]() { return !window()->IsPeekShowing(); }))
      << "the card outlived a press on the dimmed page";
}

IN_PROC_BROWSER_TEST_F(AtriumPeekDismissBrowserTest,
                       TheControlsClusterIsAWindowOfItsOwn) {
  // A separate NSWindow, never part of the pane's view tree the scrim
  // press handler lives on, so a press there cannot reach OnPeekScrimPressed.
  ASSERT_TRUE(OpenAPeek()) << "the peek card never showed";

  PeekControlsWidget* controls = window()->PeekControlsForTesting();
  ASSERT_TRUE(controls);
  EXPECT_NE(controls->widget()->GetNativeWindow(), window()->GetNativeWindow())
      << "the controls cluster shares the pane's own window";
}

// Every controller in a slot opens its peek on the one presenter window, so the
// controller whose peek the card no longer shows must not move the card or the
// badge.
IN_PROC_BROWSER_TEST_F(AtriumPeekDismissBrowserTest,
                       AStaleControllerLeavesTheCardAlone) {
  BrowserController* first = BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(first);
  BrowserController* second = nullptr;
  Browser* second_browser = SwapInASecondController(&second);
  ASSERT_TRUE(second_browser);

  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* first_source = chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL("/title1.html"), -1,
      /*foreground=*/true);
  ASSERT_TRUE(content::WaitForLoadStop(first_source));
  content::WebContents* first_other_tab = chrome::AddAndReturnTabAt(
      browser(), GURL(url::kAboutBlankURL), -1, /*foreground=*/false);
  ASSERT_TRUE(first_other_tab);
  BrowserController::ActivateWindow(first);
  ASSERT_TRUE(AdoptAPeek(first, browser()->profile(), "stale.test"));
  BrowserController::ActivateWindow(second);
  content::WebContents* current_peek =
      AdoptAPeek(second, second_browser->profile(), "current.test");
  ASSERT_TRUE(current_peek) << "the second controller's peek never took the card";
  second->ShowPeekFavicon(AnIcon());
  const uint64_t badge = window()->PeekBadgeTabIdForTesting();
  ASSERT_NE(badge, 0u) << "the peek on the card set no badge";
  ASSERT_EQ(window()->PeekOriginChipHostForTesting(), u"current.test");
  ASSERT_TRUE(window()->PeekLoadingPlaceholderVisibleForTesting());
  TabStripModel* first_strip = browser()->tab_strip_model();
  const int first_tabs = first_strip->count();

  auto expect_card_unchanged = [&](const std::string& stale_op) {
    EXPECT_EQ(window()->PeekedWebContentsForTesting(), current_peek)
        << stale_op << " took the card";
    EXPECT_TRUE(window()->IsPeekShowing()) << stale_op << " hid the card";
    EXPECT_EQ(window()->PeekBadgeTabIdForTesting(), badge)
        << stale_op << " moved the badge";
    EXPECT_EQ(window()->PeekOriginChipHostForTesting(), u"current.test")
        << stale_op << " renamed the origin chip";
    EXPECT_TRUE(window()->PeekLoadingPlaceholderVisibleForTesting())
        << stale_op << " hid the loading placeholder";
    EXPECT_EQ(first_strip->count(), first_tabs)
        << stale_op << " put its peek in its strip";
  };

  first->ShowPeekFavicon(AnIcon());
  first->ShowPeekFavicon(gfx::Image());
  expect_card_unchanged("a stale favicon");

  first_strip->ActivateTabAt(first_strip->GetIndexOfWebContents(first_other_tab));
  expect_card_unchanged("a stale tab switch");

  first->UpdatePeekOriginChip();
  expect_card_unchanged("a stale origin chip");

  first->HidePeekLoadingPlaceholder();
  expect_card_unchanged("a stale first paint");

  first->SplitPeek();
  expect_card_unchanged("a stale split");

  first->PromotePeek();
  expect_card_unchanged("a stale promote");

  first->ClosePeek();
  expect_card_unchanged("a stale close");

  second->ClosePeek();
  EXPECT_EQ(window()->PeekBadgeTabIdForTesting(), 0u)
      << "closing the peek on the card left its badge";
}

// The presenter window outlives the second controller's strip, so the card and
// badge must not stay on that strip's dead peek. The source tab's removal closes
// it before the strip goes.
IN_PROC_BROWSER_TEST_F(AtriumPeekDismissBrowserTest,
                       ADeadStripTakesItsPeekOffThePresenter) {
  BrowserController* second = nullptr;
  Browser* second_browser = SwapInASecondController(&second);
  ASSERT_TRUE(second_browser);
  ASSERT_TRUE(embedded_test_server()->Start());
  second->OpenPeek(embedded_test_server()->GetURL("/title3.html"),
                   content::Referrer(), gfx::Point(200, 200));
  ASSERT_TRUE(base::test::RunUntil([&]() { return window()->IsPeekShowing(); }))
      << "the second controller's peek never showed";
  ASSERT_TRUE(content::WaitForLoadStop(window()->PeekedWebContentsForTesting()));
  second->ShowPeekFavicon(AnIcon());
  ASSERT_NE(window()->PeekBadgeTabIdForTesting(), 0u);

  CloseBrowserSynchronously(second_browser);

  EXPECT_EQ(window()->PeekBadgeTabIdForTesting(), 0u)
      << "the badge outlived its peek's strip";
  EXPECT_TRUE(
      base::test::RunUntil([&]() { return !window()->IsPeekShowing(); }))
      << "the card outlived its peek's strip";
}

IN_PROC_BROWSER_TEST_F(AtriumPeekDismissBrowserTest,
                       AStaleStripTeardownLeavesTheCardAlone) {
  BrowserController* first = BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(first);
  BrowserController* second = nullptr;
  Browser* second_browser = SwapInASecondController(&second);
  ASSERT_TRUE(second_browser);
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(AdoptAPeek(second, second_browser->profile(), "stale.test"));
  content::WebContents* first_source = chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL("/title1.html"), -1,
      /*foreground=*/true);
  ASSERT_TRUE(content::WaitForLoadStop(first_source));
  BrowserController::ActivateWindow(first);
  content::WebContents* current_peek =
      AdoptAPeek(first, browser()->profile(), "current.test");
  ASSERT_TRUE(current_peek);
  first->ShowPeekFavicon(AnIcon());
  const uint64_t badge = window()->PeekBadgeTabIdForTesting();
  ASSERT_NE(badge, 0u);

  CloseBrowserSynchronously(second_browser);

  EXPECT_EQ(window()->PeekedWebContentsForTesting(), current_peek)
      << "a stale strip's teardown took the card";
  EXPECT_TRUE(window()->IsPeekShowing());
  EXPECT_EQ(window()->PeekBadgeTabIdForTesting(), badge)
      << "a stale strip's teardown moved the badge";
}

// The throttle posts an auto-peek, so a space or profile switch can land first.
IN_PROC_BROWSER_TEST_F(AtriumPeekDismissBrowserTest,
                       AHiddenControllerCannotOpenOverTheCard) {
  BrowserController* first = BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(first);
  BrowserController* second = nullptr;
  Browser* second_browser = SwapInASecondController(&second);
  ASSERT_TRUE(second_browser);
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* current_peek =
      AdoptAPeek(second, second_browser->profile(), "current.test");
  ASSERT_TRUE(current_peek);

  BrowserController::ActivateWindow(first);
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&OpenAutoPeek, browser(),
                     browser()->tab_strip_model()->GetActiveWebContents(),
                     embedded_test_server()->GetURL("late.test", "/title2.html"),
                     content::Referrer()));
  BrowserController::ActivateWindow(second);
  ASSERT_EQ(window()->PeekedWebContentsForTesting(), current_peek);
  // After the switch back: a restore badges from the page's icon, and this
  // blank popup has none.
  second->ShowPeekFavicon(AnIcon());
  const uint64_t badge = window()->PeekBadgeTabIdForTesting();
  ASSERT_NE(badge, 0u);
  const size_t contents_before = content::GetAllWebContents().size();
  base::RunLoop().RunUntilIdle();

  std::unique_ptr<content::WebContents> popup =
      content::WebContents::Create(
          content::WebContents::CreateParams(browser()->profile()));
  EXPECT_TRUE(first->AdoptPopupAsPeek(
      std::move(popup), gfx::Point(200, 200),
      embedded_test_server()->GetURL("popup.test", "/title2.html")))
      << "a hidden controller kept a popup it cannot show";

  EXPECT_EQ(content::GetAllWebContents().size(), contents_before)
      << "a refused auto-peek kept its contents";
  EXPECT_EQ(window()->PeekedWebContentsForTesting(), current_peek)
      << "a hidden controller took the card";
  EXPECT_TRUE(window()->IsPeekShowing());
  EXPECT_EQ(window()->PeekBadgeTabIdForTesting(), badge)
      << "a hidden controller moved the badge";
}

// a peek belongs to the tab that opened it, so another profile's
// page hides it, and coming back shows the same page where it was.
IN_PROC_BROWSER_TEST_F(AtriumPeekDismissBrowserTest,
                       ASwitchParksThePeekAndTheSwitchBackRestoresIt) {
  BrowserController* first = BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(first);
  BrowserController* second = nullptr;
  ASSERT_TRUE(SwapInASecondController(&second));
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* peek = OpenAPeekFrom(first, browser());
  ASSERT_TRUE(peek);
  ASSERT_TRUE(content::ExecJs(
      peek, "document.body.style.height = '5000px'; scrollTo(0, 1234);"));
  const uint64_t badge = window()->PeekBadgeTabIdForTesting();

  BrowserController::ActivateWindow(second);
  EXPECT_FALSE(window()->IsPeekShowing())
      << "the peek stayed over another profile's page";
  EXPECT_FALSE(window()->PeekedWebContentsForTesting());
  EXPECT_EQ(window()->PeekBadgeTabIdForTesting(), 0u)
      << "the badge stayed over another profile's sidebar";
  EXPECT_TRUE(first->HoldsPeekForTesting(peek)) << "the switch closed the peek";

  const int springs = window()->PeekShowGenerationForTesting();
  BrowserController::ActivateWindow(first);
  EXPECT_EQ(window()->PeekedWebContentsForTesting(), peek)
      << "the switch back did not bring the peek back";
  EXPECT_TRUE(window()->IsPeekShowing());
  EXPECT_EQ(window()->PeekShowGenerationForTesting(), springs)
      << "the restore replayed the open spring";
  EXPECT_EQ(window()->PeekBadgeTabIdForTesting(), badge)
      << "the switch back did not bring the badge back";
  EXPECT_EQ(content::EvalJs(peek, "scrollY"), 1234) << "the page lost its place";
}

// The card's owner is hidden after the switch, and its late open must neither
// close its own parked peek nor take the card.
IN_PROC_BROWSER_TEST_F(AtriumPeekDismissBrowserTest,
                       AParkedOwnerCannotOpenOverTheCard) {
  BrowserController* first = BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(first);
  BrowserController* second = nullptr;
  Browser* second_browser = SwapInASecondController(&second);
  ASSERT_TRUE(second_browser);
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* parked = OpenAPeekFrom(first, browser());
  ASSERT_TRUE(parked);
  BrowserController::ActivateWindow(second);
  content::WebContents* current_peek =
      AdoptAPeek(second, second_browser->profile(), "current.test");
  ASSERT_TRUE(current_peek);
  second->ShowPeekFavicon(AnIcon());
  const uint64_t badge = window()->PeekBadgeTabIdForTesting();
  ASSERT_NE(badge, 0u);
  const size_t contents_before = content::GetAllWebContents().size();

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&OpenAutoPeek, browser(),
                     browser()->tab_strip_model()->GetActiveWebContents(),
                     embedded_test_server()->GetURL("late.test", "/title2.html"),
                     content::Referrer()));
  base::RunLoop().RunUntilIdle();
  std::unique_ptr<content::WebContents> popup =
      content::WebContents::Create(
          content::WebContents::CreateParams(browser()->profile()));
  EXPECT_TRUE(first->AdoptPopupAsPeek(
      std::move(popup), gfx::Point(200, 200),
      embedded_test_server()->GetURL("popup.test", "/title2.html")))
      << "a parked owner kept a popup it cannot show";

  EXPECT_TRUE(first->HoldsPeekForTesting(parked))
      << "a late open closed the parked peek";
  EXPECT_EQ(content::GetAllWebContents().size(), contents_before)
      << "a refused open kept or lost contents";
  EXPECT_EQ(window()->PeekedWebContentsForTesting(), current_peek)
      << "a parked owner took the card";
  EXPECT_TRUE(window()->IsPeekShowing());
  EXPECT_EQ(window()->PeekBadgeTabIdForTesting(), badge)
      << "a parked owner moved the badge";
}

// A peek opened from an empty strip has no source tab, so no tab removal
// closes it, and the strip's teardown must. The tab added after it only lets
// the browser close.
IN_PROC_BROWSER_TEST_F(AtriumPeekDismissBrowserTest,
                       ASourcelessPeekLeavesWithItsStrip) {
  BrowserController* second = nullptr;
  Browser* second_browser =
      SwapInASecondController(&second, /*with_source_tab=*/false);
  ASSERT_TRUE(second_browser);
  ASSERT_TRUE(embedded_test_server()->Start());
  ASSERT_TRUE(AdoptAPeek(second, second_browser->profile(), "current.test"));
  content::WebContents* tab = chrome::AddAndReturnTabAt(
      second_browser, GURL(url::kAboutBlankURL), -1, /*foreground=*/true);
  ASSERT_TRUE(tab && content::WaitForLoadStop(tab));
  ASSERT_TRUE(window()->IsPeekShowing());

  CloseBrowserSynchronously(second_browser);

  EXPECT_TRUE(
      base::test::RunUntil([&]() { return !window()->IsPeekShowing(); }))
      << "the card outlived its peek's strip";
  EXPECT_FALSE(window()->PeekedWebContentsForTesting())
      << "the card still holds its dead peek";
}

// Each tab keeps its own peek, and past four parked ones the peek shown
// longest ago goes.
IN_PROC_BROWSER_TEST_F(AtriumPeekDismissBrowserTest,
                       AFifthParkedPeekEvictsTheOldest) {
  BrowserController* controller =
      BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  ASSERT_TRUE(embedded_test_server()->Start());
  std::vector<content::WebContents*> peeks;
  std::unique_ptr<content::WebContentsDestroyedWatcher> oldest;
  for (int i = 0; i < 6; ++i) {
    peeks.push_back(OpenAPeekFrom(controller, browser()));
    ASSERT_TRUE(peeks.back());
    if (i == 0) {
      oldest = std::make_unique<content::WebContentsDestroyedWatcher>(peeks[0]);
    }
  }

  for (int i = 1; i < 6; ++i) {
    EXPECT_TRUE(controller->HoldsPeekForTesting(peeks[i]))
        << "peek " << i << " was closed";
  }
  EXPECT_EQ(window()->PeekedWebContentsForTesting(), peeks[5]);
  oldest->Wait();
}

// The four-peek cap is per window: the second profile's parks close the peek
// the first profile showed longest ago.
IN_PROC_BROWSER_TEST_F(AtriumPeekDismissBrowserTest,
                       TheParkedCapSpansEveryProfileInTheWindow) {
  BrowserController* first = BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(first);
  BrowserController* second = nullptr;
  Browser* second_browser = SwapInASecondController(&second);
  ASSERT_TRUE(second_browser);
  ASSERT_TRUE(embedded_test_server()->Start());
  std::vector<content::WebContents*> firsts;
  for (int i = 0; i < 3; ++i) {
    firsts.push_back(OpenAPeekFrom(first, browser()));
    ASSERT_TRUE(firsts.back());
  }
  content::WebContentsDestroyedWatcher oldest(firsts[0]);

  // Parks the first profile's card, so it holds three.
  BrowserController::ActivateWindow(second);
  std::vector<content::WebContents*> seconds;
  for (int i = 0; i < 3; ++i) {
    if (i > 0) {
      ASSERT_TRUE(chrome::AddAndReturnTabAt(
          second_browser, GURL(url::kAboutBlankURL), -1, /*foreground=*/true));
    }
    seconds.push_back(
        AdoptAPeek(second, second_browser->profile(), "second.test"));
    ASSERT_TRUE(seconds.back());
  }

  EXPECT_TRUE(base::test::RunUntil([&]() { return oldest.IsDestroyed(); }))
      << "five parked peeks in one window stayed alive";
  EXPECT_TRUE(first->HoldsPeekForTesting(firsts[1]));
  EXPECT_TRUE(first->HoldsPeekForTesting(firsts[2]));
  for (content::WebContents* peek : seconds) {
    EXPECT_TRUE(second->HoldsPeekForTesting(peek))
        << "the cap closed a peek shown more recently";
  }
  EXPECT_EQ(window()->PeekedWebContentsForTesting(), seconds[2]);
}

// A switch in the middle of a pop-out cancels it, so the popout button works on
// the peek that comes back.
IN_PROC_BROWSER_TEST_F(AtriumPeekDismissBrowserTest,
                       AParkMidPromoteLeavesThePopoutWorking) {
  BrowserController* controller =
      BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* peek = OpenAPeekFrom(controller, browser());
  ASSERT_TRUE(peek);
  TabStripModel* strip = browser()->tab_strip_model();
  content::WebContents* source = strip->GetActiveWebContents();
  content::WebContents* other = chrome::AddAndReturnTabAt(
      browser(), GURL(url::kAboutBlankURL), -1, /*foreground=*/false);
  ASSERT_TRUE(other);
  const int tabs = strip->count();

  controller->PromotePeek();
  ASSERT_TRUE(window()->PeekPromotingForTesting());
  strip->ActivateTabAt(strip->GetIndexOfWebContents(other));
  EXPECT_FALSE(window()->PeekPromotingForTesting())
      << "a park left the pop-out pending";
  strip->ActivateTabAt(strip->GetIndexOfWebContents(source));
  ASSERT_EQ(window()->PeekedWebContentsForTesting(), peek);
  ASSERT_TRUE(window()->IsPeekShowing());
  PeekControlsWidget* controls = window()->PeekControlsForTesting();
  ASSERT_TRUE(controls);
  base::ListValue args;
  args.Append("popout");
  controls->OnSurfaceMessage("peekControl", args);

  EXPECT_TRUE(base::test::RunUntil([&]() { return strip->count() == tabs + 1; }))
      << "the popout button stayed dead after the switch back";
  EXPECT_EQ(strip->GetActiveWebContents(), peek);
}

// A discard or prerender swap gives the tab a new id, and the peek on the card
// follows it.
IN_PROC_BROWSER_TEST_F(AtriumPeekDismissBrowserTest,
                       APeekOnTheCardFollowsItsTabThroughASwap) {
  BrowserController* controller =
      BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* peek = OpenAPeekFrom(controller, browser());
  ASSERT_TRUE(peek);
  TabStripModel* strip = browser()->tab_strip_model();
  const int source = strip->active_index();
  content::WebContents* other = chrome::AddAndReturnTabAt(
      browser(), GURL(url::kAboutBlankURL), -1, /*foreground=*/false);
  ASSERT_TRUE(other);

  SwapTheContentsAt(source);
  EXPECT_EQ(window()->PeekedWebContentsForTesting(), peek);
  EXPECT_TRUE(window()->IsPeekShowing()) << "the swap hid the tab's own peek";

  strip->ActivateTabAt(strip->GetIndexOfWebContents(other));
  strip->ActivateTabAt(source);
  EXPECT_EQ(window()->PeekedWebContentsForTesting(), peek)
      << "the swapped tab did not get its peek back";
  EXPECT_TRUE(window()->IsPeekShowing());
}

// The same swap on a tab whose peek is parked.
IN_PROC_BROWSER_TEST_F(AtriumPeekDismissBrowserTest,
                       AParkedPeekFollowsItsTabThroughASwap) {
  BrowserController* controller =
      BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  ASSERT_TRUE(embedded_test_server()->Start());
  content::WebContents* peek = OpenAPeekFrom(controller, browser());
  ASSERT_TRUE(peek);
  TabStripModel* strip = browser()->tab_strip_model();
  const int source = strip->active_index();
  content::WebContents* other = chrome::AddAndReturnTabAt(
      browser(), GURL(url::kAboutBlankURL), -1, /*foreground=*/true);
  ASSERT_TRUE(other);
  ASSERT_FALSE(window()->IsPeekShowing());

  SwapTheContentsAt(source);
  strip->ActivateTabAt(source);
  EXPECT_EQ(window()->PeekedWebContentsForTesting(), peek)
      << "the swapped tab did not get its parked peek back";
  EXPECT_TRUE(window()->IsPeekShowing());
}

// In a split the card sits over the selected pane, so the second pane's link
// opens a peek, and that peek follows the pane selection.
IN_PROC_BROWSER_TEST_F(AtriumPeekDismissBrowserTest,
                       TheSelectedSecondPaneOwnsItsPeek) {
  BrowserController* controller =
      BrowserController::ControllerForBrowser(browser());
  ASSERT_TRUE(controller);
  ASSERT_TRUE(embedded_test_server()->Start());
  TabStripModel* strip = browser()->tab_strip_model();
  content::WebContents* first = chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL("source.test", "/title1.html"),
      -1, /*foreground=*/true);
  content::WebContents* second = chrome::AddAndReturnTabAt(
      browser(), embedded_test_server()->GetURL("other.test", "/title1.html"),
      -1, /*foreground=*/false);
  ASSERT_TRUE(first && second);
  ASSERT_TRUE(content::WaitForLoadStop(first));
  ASSERT_TRUE(content::WaitForLoadStop(second));
  window()->EnterSplit(second, SplitOrientation::kSideBySide, 0.5);
  ASSERT_TRUE(window()->IsSplit());
  second->Focus();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window()->ActivePaneIndexForTesting() == 1; }));
  ASSERT_EQ(strip->GetActiveWebContents(), second);

  content::WebContents* peek = OpenABadgedPeek(controller);
  ASSERT_TRUE(peek) << "a link in the selected second pane opened no peek";

  first->Focus();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window()->ActivePaneIndexForTesting() == 0; }));
  EXPECT_FALSE(window()->IsPeekShowing())
      << "the peek stayed up over the other pane's selection";
  EXPECT_TRUE(controller->HoldsPeekForTesting(peek));

  second->Focus();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window()->ActivePaneIndexForTesting() == 1; }));
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return window()->PeekedWebContentsForTesting() == peek &&
           window()->IsPeekShowing();
  })) << "the second pane did not get its peek back";
}

// Two spaces of the Default profile, and a favourite both of them draw.
class AtriumPeekSpaceBrowserTest : public AtriumPeekDismissBrowserTest {
 protected:
  void TearDownOnMainThread() override {
    atrium_core_close();
    AtriumPeekDismissBrowserTest::TearDownOnMainThread();
  }

  BrowserController* controller() {
    return BrowserController::ControllerForBrowser(browser());
  }

  // The rail's glide announces its space a frame later, and a switch back made
  // before that is undone by it.
  bool SelectSpace(int index) {
    content::WebContents* sidebar = window()->SidebarContentsForTesting();
    if (!controller()->SelectSpace(index) || !sidebar ||
        !content::ExecJs(sidebar,
                         "new Promise(r => requestAnimationFrame(() => "
                         "requestAnimationFrame(r)))",
                         content::EXECUTE_SCRIPT_NO_USER_GESTURE)) {
      return false;
    }
    base::RunLoop().RunUntilIdle();
    return atrium_core_active_space_index() == index;
  }

  // Returns the favourite's tab, open in the first space.
  content::WebContents* OpenTheFavourite() {
    static constexpr char kTemplate[] =
        R"({"spaces":[{"id":"s1","name":"One","emoji":"1",)"
        R"("tint":"#111111","profileID":"Default","pinnedTabs":[],)"
        R"("tabs":[],"order":0,"groups":[]},)"
        R"({"id":"s2","name":"Two","emoji":"2",)"
        R"("tint":"#111111","profileID":"Default","pinnedTabs":[],)"
        R"("tabs":[],"order":1,"groups":[]}],)"
        R"("favorites":[{"profileID":"Default","title":"Fav","url":"%s"}],)"
        R"("appState":{"activeSpaceID":"s1","sidebarWidth":220,)"
        R"("schemaVersion":3,"archiveEpoch":1,"sidebarWidthMigrated":true}})";
    if (!embedded_test_server()->Start()) {
      return nullptr;
    }
    favourite_ = embedded_test_server()->GetURL("app.test", "/title1.html");
    atrium_core_close();
    {
      base::ScopedAllowBlockingForTesting allow_blocking;
      if (!dir_.CreateUniqueTempDir()) {
        return nullptr;
      }
      const base::FilePath path = dir_.GetPath().AppendASCII("spaces.json");
      if (!base::WriteFile(path, base::StringPrintf(
                                     kTemplate, favourite_.spec().c_str())) ||
          !atrium_core_open(path.value().c_str(), 1.0)) {
        return nullptr;
      }
    }
    return ClickTheFavourite();
  }

  content::WebContents* ClickTheFavourite() {
    base::ListValue args;
    args.Append(favourite_.spec());
    args.Append("");
    window()->OnSurfaceMessage("sidebarFavoriteClicked", args);
    content::WebContents* tab =
        browser()->tab_strip_model()->GetActiveWebContents();
    return tab && content::WaitForLoadStop(tab) ? tab : nullptr;
  }

  // A tab of its own in the space on screen.
  content::WebContents* AddATab() {
    content::WebContents* tab = chrome::AddAndReturnTabAt(
        browser(), embedded_test_server()->GetURL("other.test", "/title1.html"),
        -1, /*foreground=*/true);
    return tab && content::WaitForLoadStop(tab) ? tab : nullptr;
  }

 private:
  base::ScopedTempDir dir_;
  GURL favourite_;
};

// An empty space does not show the favourite's tab, so it parks the peek, and
// the space that does brings it back.
IN_PROC_BROWSER_TEST_F(AtriumPeekSpaceBrowserTest,
                       ASpaceWithoutTheSourceTabParksThePeek) {
  ASSERT_TRUE(OpenTheFavourite());
  content::WebContents* peek = OpenABadgedPeek(controller());
  ASSERT_TRUE(peek);
  const uint64_t badge = window()->PeekBadgeTabIdForTesting();

  ASSERT_TRUE(SelectSpace(1));
  EXPECT_FALSE(window()->IsPeekShowing()) << "the peek stayed over an empty space";
  EXPECT_EQ(window()->PeekBadgeTabIdForTesting(), 0u);
  EXPECT_TRUE(controller()->HoldsPeekForTesting(peek))
      << "the switch closed the peek";

  ASSERT_TRUE(SelectSpace(0));
  EXPECT_EQ(window()->PeekedWebContentsForTesting(), peek)
      << "the switch back did not bring the peek back";
  EXPECT_TRUE(base::test::RunUntil([&]() { return window()->IsPeekShowing(); }));
  EXPECT_EQ(window()->PeekBadgeTabIdForTesting(), badge);
}

// Both spaces last showed the favourite's tab, so the switch keeps its peek up.
IN_PROC_BROWSER_TEST_F(AtriumPeekSpaceBrowserTest,
                       ASpaceShowingTheSourceTabKeepsThePeek) {
  content::WebContents* favourite = OpenTheFavourite();
  ASSERT_TRUE(favourite);
  ASSERT_TRUE(AddATab());
  ASSERT_TRUE(SelectSpace(1));
  ASSERT_EQ(ClickTheFavourite(), favourite);
  ASSERT_TRUE(SelectSpace(0));
  ASSERT_EQ(ClickTheFavourite(), favourite);
  content::WebContents* peek = OpenABadgedPeek(controller());
  ASSERT_TRUE(peek);
  const uint64_t badge = window()->PeekBadgeTabIdForTesting();

  ASSERT_TRUE(SelectSpace(1));
  ASSERT_EQ(browser()->tab_strip_model()->GetActiveWebContents(), favourite);
  EXPECT_EQ(window()->PeekedWebContentsForTesting(), peek)
      << "a space showing the same tab dropped its peek";
  EXPECT_TRUE(window()->IsPeekShowing());
  EXPECT_EQ(window()->PeekBadgeTabIdForTesting(), badge);
}

// A delayed open from the space just left may not replace the peek of the space
// on screen, nor its own parked one.
IN_PROC_BROWSER_TEST_F(AtriumPeekSpaceBrowserTest,
                       AHiddenSpaceCannotOpenAPeek) {
  content::WebContents* favourite = OpenTheFavourite();
  ASSERT_TRUE(favourite);
  content::WebContents* parked = OpenABadgedPeek(controller());
  ASSERT_TRUE(parked);
  ASSERT_TRUE(SelectSpace(1));
  ASSERT_TRUE(AddATab());
  content::WebContents* shown = OpenABadgedPeek(controller());
  ASSERT_TRUE(shown);
  const uint64_t badge = window()->PeekBadgeTabIdForTesting();
  ASSERT_TRUE(SelectSpace(0));
  const size_t contents_before = content::GetAllWebContents().size();

  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&OpenAutoPeek, browser(), favourite,
                     embedded_test_server()->GetURL("late.test", "/title2.html"),
                     content::Referrer()));
  ASSERT_TRUE(SelectSpace(1));
  base::RunLoop().RunUntilIdle();
  EXPECT_FALSE(MaybeOpenPeek(browser(), favourite,
                             embedded_test_server()->GetURL("shift.test",
                                                            "/title2.html"),
                             content::Referrer()))
      << "a refused shift-click reported a peek, so its link went nowhere";
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return favourite->GetVisibleURL().host() == "late.test";
  })) << "the refused auto-peek's link never opened in its own tab";

  EXPECT_EQ(window()->PeekedWebContentsForTesting(), shown)
      << "a hidden space's late open replaced the peek on screen";
  EXPECT_TRUE(window()->IsPeekShowing());
  EXPECT_EQ(window()->PeekBadgeTabIdForTesting(), badge);
  EXPECT_TRUE(controller()->HoldsPeekForTesting(parked))
      << "a hidden space's late open closed its own parked peek";
  EXPECT_EQ(content::GetAllWebContents().size(), contents_before)
      << "a refused open kept or lost contents";
}

// A page that closes itself closes its own peek, even when a switch parked it
// before the close ran, and never the peek that took the card.
IN_PROC_BROWSER_TEST_F(AtriumPeekSpaceBrowserTest,
                       AClosingPageClosesOnlyItsOwnPeek) {
  ASSERT_TRUE(OpenTheFavourite());
  content::WebContents* closing = OpenABadgedPeek(controller());
  ASSERT_TRUE(closing);
  ASSERT_TRUE(SelectSpace(1));
  ASSERT_TRUE(AddATab());
  content::WebContents* other = OpenABadgedPeek(controller());
  ASSERT_TRUE(other);
  ASSERT_TRUE(SelectSpace(0));
  ASSERT_EQ(window()->PeekedWebContentsForTesting(), closing);
  content::WebContentsDestroyedWatcher closed(closing);

  // What window.close() sends. The close itself waits a turn, and the switch
  // below parks the page before that turn.
  closing->GetDelegate()->CloseContents(closing);
  ASSERT_TRUE(SelectSpace(1));

  EXPECT_TRUE(base::test::RunUntil([&]() { return closed.IsDestroyed(); }))
      << "the parked page that asked to close stayed parked";
  EXPECT_EQ(window()->PeekedWebContentsForTesting(), other)
      << "a parked page's close took down the peek on the card";
  EXPECT_TRUE(window()->IsPeekShowing());
}

// Each space keeps the peek its own tab opened, with its page and its place,
// and a restore puts it back without the open spring.
IN_PROC_BROWSER_TEST_F(AtriumPeekSpaceBrowserTest, EachSpaceKeepsItsOwnPeek) {
  ASSERT_TRUE(OpenTheFavourite());
  content::WebContents* first = OpenABadgedPeek(controller());
  ASSERT_TRUE(first);
  ASSERT_TRUE(content::ExecJs(
      first, "document.body.style.height = '5000px'; scrollTo(0, 1234);"));
  ASSERT_TRUE(SelectSpace(1));
  ASSERT_TRUE(AddATab());
  content::WebContents* second = OpenABadgedPeek(controller());
  ASSERT_TRUE(second);
  ASSERT_TRUE(controller()->HoldsPeekForTesting(first))
      << "the second space's peek closed the first space's";
  EXPECT_TRUE(first->IsAudioMuted()) << "a parked peek can still play sound";

  const int springs = window()->PeekShowGenerationForTesting();
  ASSERT_TRUE(SelectSpace(0));
  EXPECT_EQ(window()->PeekedWebContentsForTesting(), first)
      << "the first space did not get its peek back";
  EXPECT_TRUE(window()->IsPeekShowing());
  EXPECT_NE(window()->PeekBadgeTabIdForTesting(), 0u);
  EXPECT_EQ(window()->PeekShowGenerationForTesting(), springs)
      << "the restore replayed the open spring";
  EXPECT_FALSE(first->IsAudioMuted()) << "the restored peek stayed muted";
  EXPECT_EQ(content::EvalJs(first, "scrollY"), 1234) << "the page lost its place";
  EXPECT_TRUE(controller()->HoldsPeekForTesting(second));

  ASSERT_TRUE(SelectSpace(1));
  EXPECT_EQ(window()->PeekedWebContentsForTesting(), second)
      << "the second space did not get its peek back";
  EXPECT_TRUE(window()->IsPeekShowing());
}

}  // namespace
}  // namespace atrium
