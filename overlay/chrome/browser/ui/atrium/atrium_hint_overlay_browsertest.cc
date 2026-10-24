// Copyright 2026 The Atrium Authors

#include <string>
#include <string_view>

#include "base/test/run_until.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_hint_overlay_widget.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

// The sidebar's own WebContents clips a hint past its own edge - see
// HintOverlayWidget's class doc. This is the overlay that lifts it above the
// content card, the same primitive CommandBarWidget's scrim uses.
namespace atrium {
namespace {

class AtriumHintOverlayBrowserTest : public InProcessBrowserTest {
 protected:
  AtriumBrowserWindow* window() {
    return static_cast<AtriumBrowserWindow*>(browser()->window());
  }

  void ShowHint() { ShowHintFrom("sidebarHintShow"); }

  void ShowHintFrom(std::string_view message) {
    base::ListValue args;
    args.Append("Open in a split");
    args.Append("shift click");
    args.Append(10);
    args.Append(20);
    args.Append(200);
    args.Append(24);
    args.Append(2500);
    window()->OnSurfaceMessage(message, args);
  }
};

IN_PROC_BROWSER_TEST_F(AtriumHintOverlayBrowserTest,
                       ShowBuildsAVisibleNonActivatableOverlay) {
  ShowHint();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window()->HintOverlayWidgetForTesting() != nullptr; }));
  HintOverlayWidget* hint = window()->HintOverlayWidgetForTesting();
  views::Widget* widget = hint->widget();
  ASSERT_TRUE(widget);
  ASSERT_TRUE(base::test::RunUntil([&]() { return widget->IsVisible(); }));
  EXPECT_FALSE(widget->IsActive())
      << "the hint bubble must never take key focus from the page";
  EXPECT_EQ(window()->widget()->GetWindowBoundsInScreen(),
            widget->GetWindowBoundsInScreen())
      << "the overlay spans the whole window, not just the anchor";

  ASSERT_TRUE(hint->web_contents());
  EXPECT_EQ(GURL(std::string(chrome::kChromeUIAtriumURL).append("hint")),
            hint->web_contents()->GetVisibleURL());
}

// The floating address bar drew its hints inside its own
// document, where the bar's capsule bounded them.
IN_PROC_BROWSER_TEST_F(AtriumHintOverlayBrowserTest,
                       TheFloatingBarsHintLandsOnTheWindowWideOverlay) {
  ASSERT_FALSE(window()->HintOverlayWidgetForTesting());
  ShowHintFrom("devBarHintShow");
  HintOverlayWidget* hint = window()->HintOverlayWidgetForTesting();
  ASSERT_TRUE(hint) << "the bar's hint never left the bar's own document";
  views::Widget* widget = hint->widget();
  ASSERT_TRUE(widget);
  ASSERT_TRUE(base::test::RunUntil([&]() { return widget->IsVisible(); }));
  EXPECT_EQ(window()->widget()->GetWindowBoundsInScreen(),
            widget->GetWindowBoundsInScreen())
      << "so a bar hint can reach past the bar";

  base::ListValue empty;
  window()->OnSurfaceMessage("devBarHintHide", empty);
  EXPECT_FALSE(widget->IsVisible());
}

IN_PROC_BROWSER_TEST_F(AtriumHintOverlayBrowserTest, HideHidesTheWidget) {
  ShowHint();
  ASSERT_TRUE(base::test::RunUntil(
      [&]() { return window()->HintOverlayWidgetForTesting() != nullptr; }));
  views::Widget* widget = window()->HintOverlayWidgetForTesting()->widget();
  ASSERT_TRUE(base::test::RunUntil([&]() { return widget->IsVisible(); }));

  base::ListValue empty;
  window()->OnSurfaceMessage("sidebarHintHide", empty);
  EXPECT_FALSE(widget->IsVisible());
}

}  // namespace
}  // namespace atrium
