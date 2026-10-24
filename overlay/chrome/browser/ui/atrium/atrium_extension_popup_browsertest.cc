// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_extension_popup_browsertest.cc
// where an extension popup lands and what it lands in front of.

#include "base/logging.h"
#include "base/test/run_until.h"
#include "base/time/time.h"
#include "chrome/browser/extensions/extension_browsertest.h"
#include "chrome/browser/extensions/extension_view_host.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/views/extensions/security_dialog_tracker.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "extensions/browser/extension_host.h"
#include "extensions/browser/extension_host_registry.h"
#include "extensions/common/extension.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/hit_test.h"
#include "ui/base/page_transition_types.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/test/widget_test.h"
#include "ui/views/widget/unique_widget_ptr.h"
#include "ui/views/widget/widget.h"
#include "ui/views/window/client_view.h"
#include "ui/views/window/dialog_delegate.h"
#include "url/gurl.h"

namespace atrium {
namespace {

using AtriumExtensionPopupTest = extensions::ExtensionBrowserTest;

constexpr char kPopupExtension[] = "api_test/browser_action/popup";
// kPopupExtension grows to 800x600. On the 1024x768 VM screen a popup that
// size is held on screen and cannot stay inside a moved window.
constexpr char kSmallPopupExtension[] = "api_test/browser_action/open_popup";

// The popup is now a real window, found by its host rather than by a
// bubble's static last-instance pointer - which also means no active tab
// is required to look one up.
extensions::ExtensionHost* FindPopupHost(Browser* browser,
                                         const std::string& extension_id) {
  for (extensions::ExtensionHost* host :
       extensions::ExtensionHostRegistry::Get(browser->profile())
           ->GetHostsForExtension(extension_id)) {
    if (host->extension_host_type() ==
        extensions::mojom::ViewType::kExtensionPopup) {
      return host;
    }
  }
  return nullptr;
}

// Returns the popup's widget once the page inside it has loaded.
views::Widget* OpenThePopup(Browser* browser,
                            const std::string& extension_id,
                            const gfx::Rect& icon_screen_rect) {
  // Through the surface message the chips actually send, so the rect's whole
  // route is under test rather than only the anchor code at the end of it.
  base::ListValue args;
  args.Append(extension_id);
  if (!icon_screen_rect.IsEmpty()) {
    base::ListValue rect;
    rect.Append(icon_screen_rect.x());
    rect.Append(icon_screen_rect.y());
    rect.Append(icon_screen_rect.width());
    rect.Append(icon_screen_rect.height());
    args.Append(std::move(rect));
  }
  static_cast<AtriumBrowserWindow*>(browser->window())
      ->OnSurfaceMessage("pillActionClicked", args);
  if (!base::test::RunUntil(
          [&]() { return FindPopupHost(browser, extension_id) != nullptr; })) {
    return nullptr;
  }
  extensions::ExtensionHost* host = FindPopupHost(browser, extension_id);
  if (!content::WaitForLoadStop(host->host_contents())) {
    return nullptr;
  }
  views::Widget* widget = views::Widget::GetTopLevelWidgetForNativeView(
      host->host_contents()->GetNativeView());
  if (!base::test::RunUntil(
          [&]() { return widget && widget->IsVisible(); })) {
    return nullptr;
  }
  return widget;
}

// Both together: either one alone still leaves the reported defect.
// Parked at the screen corner a popup is in front of nothing, and behind the
// window it is anchored to an icon nobody can see.
IN_PROC_BROWSER_TEST_F(AtriumExtensionPopupTest,
                       APopupLandsOverTheWindowNotAtTheScreenCorner) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kPopupExtension));
  ASSERT_TRUE(extension);
  ASSERT_TRUE(AddTabAtIndex(0, GURL("about:blank"),
                            ui::PAGE_TRANSITION_TYPED));

  auto* window = static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  window->widget()->SetBounds(gfx::Rect(200, 150, 1200, 800));

  // Inside the sidebar column, which runs from the window's own left edge to
  // the content card. A real chip sends its own rect from there.
  const gfx::Rect icon(240, 120, 24, 24);
  views::Widget* popup = OpenThePopup(browser(), extension->id(), icon);
  ASSERT_TRUE(popup) << "the extension popup never opened";

  const gfx::Rect popup_bounds = popup->GetWindowBoundsInScreen();
  const gfx::Rect window_bounds =
      window->widget()->GetWindowBoundsInScreen();
  // Contains, not Intersects: anchored to the whole sidebar column's top right
  // the popup hangs off the top of the window, which still intersects it.
  EXPECT_TRUE(window_bounds.Contains(popup_bounds))
      << "the popup landed at " << popup_bounds.ToString()
      << ", outside the window at " << window_bounds.ToString();

  // Within 8px of the icon's bottom-left, the slack the window-bounds
  // clamp above can use.
  EXPECT_LE((popup_bounds.origin() - icon.bottom_left()).Length(), 8)
      << "the popup at " << popup_bounds.origin().ToString()
      << " is not near the icon's bottom-left at "
      << icon.bottom_left().ToString();

  // In front, read out of AppKit's own order rather than out of
  // anything this process stamped on the widget.
  EXPECT_GT(window->OverlayChildOrderForTesting(popup),
            window->OverlayChildOrderForTesting(window->widget()))
      << "the popup opened behind the window that owns it";
}

// A popup that is correct only at the instant it opens still strands the
// user the moment they move the window, which is the same complaint one beat
// later.
IN_PROC_BROWSER_TEST_F(AtriumExtensionPopupTest,
                       APopupFollowsTheWindowAndGoesWithIt) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kSmallPopupExtension));
  ASSERT_TRUE(extension);
  ASSERT_TRUE(AddTabAtIndex(0, GURL("about:blank"),
                            ui::PAGE_TRANSITION_TYPED));

  auto* window = static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);
  // Both window places stay inside the work area, so the screen edge never
  // moves the window or the popup.
  const gfx::Rect area =
      display::Screen::Get()->GetPrimaryDisplay().work_area();
  const gfx::Size size(area.width() * 2 / 3, area.height() * 2 / 3);
  window->widget()->SetBounds(gfx::Rect(area.origin(), size));

  const gfx::Rect icon(area.x() + 40, area.y() + 30, 24, 24);
  views::Widget* popup = OpenThePopup(browser(), extension->id(), icon);
  ASSERT_TRUE(popup) << "the extension popup never opened";
  const gfx::Rect before = popup->GetWindowBoundsInScreen();

  window->widget()->SetBounds(gfx::Rect(
      area.x() + area.width() / 3, area.y() + area.height() / 3,
      size.width(), size.height()));
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return popup->GetWindowBoundsInScreen() != before;
  })) << "the popup stayed where the window used to be";

  const gfx::Rect after = popup->GetWindowBoundsInScreen();
  EXPECT_TRUE(window->widget()->GetWindowBoundsInScreen().Contains(after))
      << "the popup at " << after.ToString()
      << " was left behind by the window at "
      << window->widget()->GetWindowBoundsInScreen().ToString()
      << ", work area " << area.ToString();
  EXPECT_GT(window->OverlayChildOrderForTesting(popup),
            window->OverlayChildOrderForTesting(window->widget()))
      << "the move dropped the popup behind the window";

  // The second half, and the one a leak would hide: no popup outlives
  // the window that opened it.
  views::test::WidgetDestroyedWaiter waiter(popup);
  window->widget()->CloseNow();
  waiter.Wait();
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionPopupTest,
                       APopupIsAWindowWithTrafficLightsAboveItsPage) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kPopupExtension));
  ASSERT_TRUE(extension);
  ASSERT_TRUE(AddTabAtIndex(0, GURL("about:blank"),
                            ui::PAGE_TRANSITION_TYPED));
  auto* window = static_cast<AtriumBrowserWindow*>(browser()->window());
  window->widget()->SetBounds(gfx::Rect(200, 150, 1200, 800));

  views::Widget* popup =
      OpenThePopup(browser(), extension->id(), gfx::Rect(240, 120, 24, 24));
  ASSERT_TRUE(popup) << "the extension popup never opened";
  EXPECT_TRUE(AtriumBrowserWindow::HasTrafficLightsForTesting(popup));

  const gfx::Rect page = FindPopupHost(browser(), extension->id())
                             ->host_contents()
                             ->GetContainerBounds();
  const gfx::Rect frame = popup->GetWindowBoundsInScreen();
  EXPECT_GE(page.y() - frame.y(), 20)
      << "the page at " << page.ToString() << " sits under the lights of "
      << frame.ToString();
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionPopupTest, TheRedLightClosesThePopup) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kPopupExtension));
  ASSERT_TRUE(extension);
  ASSERT_TRUE(AddTabAtIndex(0, GURL("about:blank"),
                            ui::PAGE_TRANSITION_TYPED));
  views::Widget* popup =
      OpenThePopup(browser(), extension->id(), gfx::Rect(240, 120, 24, 24));
  ASSERT_TRUE(popup) << "the extension popup never opened";

  views::test::WidgetDestroyedWaiter waiter(popup);
  AtriumBrowserWindow::ClickCloseLightForTesting(popup);
  waiter.Wait();
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionPopupTest, APopupOpensWithNoTab) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kPopupExtension));
  ASSERT_TRUE(extension);
  ASSERT_EQ(browser()->tab_strip_model()->count(), 0);

  views::Widget* popup =
      OpenThePopup(browser(), extension->id(), gfx::Rect(240, 120, 24, 24));
  EXPECT_TRUE(popup) << "the extension popup must not require an active tab";
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionPopupTest, TheTopStripDragsTheWindow) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kPopupExtension));
  ASSERT_TRUE(extension);
  views::Widget* popup =
      OpenThePopup(browser(), extension->id(), gfx::Rect(240, 120, 24, 24));
  ASSERT_TRUE(popup) << "the extension popup never opened";

  views::ClientView* client_view = popup->non_client_view()->client_view();
  const int width = popup->GetWindowBoundsInScreen().width();
  EXPECT_EQ(client_view->NonClientHitTest(gfx::Point(width / 2, 2)),
            HTCAPTION)
      << "the strip above the page must drag the window";
  // 60: below the title bar, which is 28px or taller by macOS version.
  EXPECT_NE(client_view->NonClientHitTest(gfx::Point(width / 2, 60)),
            HTCAPTION)
      << "the page below the strip must not drag the window";
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionPopupTest,
                       APopupStaysOpenWhenTheBrowserActivates) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kPopupExtension));
  ASSERT_TRUE(extension);
  views::Widget* popup =
      OpenThePopup(browser(), extension->id(), gfx::Rect(240, 120, 24, 24));
  ASSERT_TRUE(popup) << "the extension popup never opened";

  static_cast<AtriumBrowserWindow*>(browser()->window())->widget()->Activate();
  EXPECT_TRUE(popup->IsVisible())
      << "activating the browser window is not a click-outside close";
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionPopupTest, WindowCloseClosesThePopup) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kPopupExtension));
  ASSERT_TRUE(extension);
  views::Widget* popup =
      OpenThePopup(browser(), extension->id(), gfx::Rect(240, 120, 24, 24));
  ASSERT_TRUE(popup) << "the extension popup never opened";
  extensions::ExtensionHost* host = FindPopupHost(browser(), extension->id());
  ASSERT_TRUE(host);

  views::test::WidgetDestroyedWaiter waiter(popup);
  ASSERT_TRUE(content::ExecJs(host->host_contents(), "window.close()"));
  waiter.Wait();
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionPopupTest, ASecondClickLeavesOnePopup) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kPopupExtension));
  ASSERT_TRUE(extension);
  views::Widget* first =
      OpenThePopup(browser(), extension->id(), gfx::Rect(240, 120, 24, 24));
  ASSERT_TRUE(first) << "the extension popup never opened";

  views::test::WidgetDestroyedWaiter waiter(first);
  views::Widget* second =
      OpenThePopup(browser(), extension->id(), gfx::Rect(240, 120, 24, 24));
  ASSERT_TRUE(second) << "the second click never opened a popup";
  waiter.Wait();

  EXPECT_EQ(extensions::ExtensionHostRegistry::Get(browser()->profile())
                ->GetHostsForExtension(extension->id())
                .size(),
            1u)
      << "a second click must leave exactly one popup host";
}

IN_PROC_BROWSER_TEST_F(AtriumExtensionPopupTest,
                       APopupStaysClosedOverASecurityDialog) {
  const extensions::Extension* extension =
      LoadExtension(test_data_dir_.AppendASCII(kPopupExtension));
  ASSERT_TRUE(extension);

  auto* window = static_cast<AtriumBrowserWindow*>(browser()->window());
  ASSERT_TRUE(window);

  // A plain views::WidgetDelegate in place of $SRC's DialogDelegateView:
  // AtriumExtensionPopupTest isn't in dialog_delegate.h's CreatePassKey()
  // friend list, but CreateDialogWidget only needs a WidgetDelegate to build
  // the same DialogWidget subclass SecurityDialogTracker is meant to see.
  views::UniqueWidgetPtr dialog(std::unique_ptr<views::Widget>(
      views::DialogDelegate::CreateDialogWidget(
          std::make_unique<views::WidgetDelegate>().release(),
          gfx::NativeWindow(), window->widget()->GetNativeView())));
  extensions::SecurityDialogTracker::GetInstance()->AddSecurityDialog(
      dialog.get());
  dialog->Show();
  views::test::WidgetVisibleWaiter(dialog.get()).Wait();
  ASSERT_TRUE(extensions::SecurityDialogTracker::GetInstance()
                  ->BrowserHasVisibleSecurityDialogs(browser()));

  base::ListValue args;
  args.Append(extension->id());
  window->OnSurfaceMessage("pillActionClicked", args);

  extensions::ExtensionHost* host = nullptr;
  ASSERT_TRUE(base::test::RunUntil([&]() {
    host = FindPopupHost(browser(), extension->id());
    return host != nullptr;
  })) << "the popup host never even started loading";
  views::Widget* popup_widget = views::Widget::GetTopLevelWidgetForNativeView(
      host->host_contents()->GetNativeView());
  ASSERT_TRUE(popup_widget);
  EXPECT_FALSE(popup_widget->IsVisible());

  // Not a load-stop wait: the guard destroys the host's WebContents
  // mid-navigation, so no WaitFor* on it ever returns.
  EXPECT_TRUE(base::test::RunUntil([&]() {
    return FindPopupHost(browser(), extension->id()) == nullptr;
  })) << "a popup stayed open over a visible security dialog";

  views::test::WidgetDestroyedWaiter dialog_waiter(dialog.get());
  dialog->CloseNow();
  dialog_waiter.Wait();
}

}  // namespace
}  // namespace atrium
