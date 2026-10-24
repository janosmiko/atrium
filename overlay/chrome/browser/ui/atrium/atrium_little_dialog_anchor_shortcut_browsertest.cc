// Copyright 2026 The Atrium Authors

#include "base/memory/raw_ptr.h"
#include "base/scoped_observation.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_commands.h"
#include "chrome/browser/ui/browser_window/public/browser_collection_observer.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/web_modal/web_contents_modal_dialog_host.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace {

// Same technique atrium_little_never_adopted_browsertest.cc uses: capture
// the new little window's BrowserWindowInterface off
// BrowserCollectionObserver::OnBrowserCreated, since CreateLittleWindow's
// synchronous call stack fires it before returning.
class NewBrowserCapture : public BrowserCollectionObserver {
 public:
  void OnBrowserCreated(BrowserWindowInterface* browser) override {
    captured_ = browser;
  }

  raw_ptr<BrowserWindowInterface> captured_ = nullptr;
};

using AtriumLittleDialogAnchorShortcutBrowserTest = InProcessBrowserTest;

// "Dialogs, anchors, and reserved-shortcut redispatch work in
// Little windows (fix-lineage regression)." All three go through the same
// resolution seam, BrowserController::PresentedWindowForBrowser - see its
// doc and AtriumBrowserWindow::ModalDialogHost::EffectiveWindow's comment
// (dialog host/position), GetAutofillBubbleAnchor's comment (anchor), and
// HandleKeyboardEvent's comment (reserved-shortcut redispatch target). None
// of those call sites special-case little_, so a little window resolves
// through them exactly like any other window PROVIDED it is registered as
// the presenter of its own slot - this test pins that registration.
IN_PROC_BROWSER_TEST_F(AtriumLittleDialogAnchorShortcutBrowserTest,
                       PresentedWindowResolvesToTheLittleWindowItself) {
  NewBrowserCapture capture;
  base::ScopedObservation<GlobalBrowserCollection, BrowserCollectionObserver>
      capture_observation(&capture);
  capture_observation.Observe(GlobalBrowserCollection::GetInstance());

  atrium::BrowserController* little =
      atrium::BrowserController::CreateLittleWindow(
          browser()->profile(), GURL("https://example.com/little-ac7"),
          std::nullopt);
  capture_observation.Reset();

  ASSERT_TRUE(little) << "CreateLittleWindow returned nullptr";
  ASSERT_TRUE(capture.captured_)
      << "CreateLittleWindow's Browser::Create did not fire "
         "OnBrowserCreated - GlobalBrowserCollection wiring changed";

  Browser* little_browser = capture.captured_->GetBrowserForMigrationOnly();
  ASSERT_TRUE(little_browser);
  auto* atrium_window =
      static_cast<atrium::AtriumBrowserWindow*>(little_browser->window());
  ASSERT_TRUE(atrium_window);

  // Reserved-shortcut redispatch target: HandleKeyboardEvent redispatches to
  // PresentedWindowForBrowser(browser_)->GetNativeWindow(), falling back to
  // `this` only when no controller/presenter resolves. A little window with
  // no swap in effect must resolve to ITSELF, never nullptr - nullptr there
  // would mean a reserved shortcut like Cmd+Q silently never reaches the
  // menu from a little window.
  EXPECT_EQ(atrium::BrowserController::PresentedWindowForBrowser(
                little_browser),
            atrium_window)
      << "a little window did not resolve as the presenter of its own "
         "slot - reserved-shortcut redispatch would target the wrong "
         "window or silently drop the event";

  // Anchor: GetAutofillBubbleAnchor() resolves the same presenter, then
  // anchors to its ActivePane(). A little window's AtriumRootView always
  // builds a pane_container_ (unconditional on `little`),
  // so this must never fall back to the unanchored BubbleAnchor().
  EXPECT_TRUE(static_cast<bool>(atrium_window->GetAutofillBubbleAnchor()))
      << "a little window's autofill bubble anchor came back unanchored - "
         "a bubble anchored here would render with no visible anchor point";

  // Dialog: GetWebContentsModalDialogHost() resolves through the identical
  // presenter fallback (ModalDialogHost::EffectiveWindow), then sizes/
  // positions itself off EffectiveWindow()->ActivePane()->web_view(). A
  // little window's pane_container_/ActivePane() is built unconditionally,
  // so this must produce a real, non-empty size rather
  // than crash on a null pane or come back empty.
  web_modal::WebContentsModalDialogHost* dialog_host =
      atrium_window->GetWebContentsModalDialogHost();
  ASSERT_TRUE(dialog_host);
  EXPECT_FALSE(dialog_host->GetMaximumDialogSize().IsEmpty())
      << "a little window's modal dialog host reported an empty maximum "
         "size - a JS/tab-modal dialog opened from a little window's tab "
         "would have nowhere sane to render";

  chrome::CloseWindow(little_browser);
  ui_test_utils::WaitForBrowserToClose();
}

}  // namespace
