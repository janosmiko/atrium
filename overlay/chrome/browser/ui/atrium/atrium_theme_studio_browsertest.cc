// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_theme_studio_browsertest.cc
#include <algorithm>

#include "base/test/run_until.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_theme_studio_placement.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_tabstrip.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget.h"
#include "url/gurl.h"

namespace {

constexpr char kPayload[] =
    R"({"spaceId":"77777777-7777-7777-7777-777777777777",)"
    R"("spaceName":"Personal","accent":"#3366CC","spec":null})";

constexpr int kDragStep = 20;

// One axis of a drag the clamp will not touch: at most kDragStep toward the
// roomier side, and 0 when the studio already fills the axis. Negative travels
// toward the parent's origin.
int StepTowardTheRoomierSide(int room_before, int room_after) {
  return room_after >= room_before ? std::min(kDragStep, room_after)
                                   : -std::min(kDragStep, room_before);
}

class AtriumThemeStudioBrowserTest : public InProcessBrowserTest {
 protected:
  atrium::AtriumBrowserWindow* OpenStudio(int rail_index = 0) {
    content::WebContents* tab =
        chrome::AddAndReturnTabAt(browser(), GURL("about:blank"), -1, true);
    EXPECT_TRUE(content::WaitForLoadStop(tab));
    atrium::AtriumBrowserWindow* window =
        atrium::BrowserController::PresentedWindowForBrowser(browser());
    EXPECT_TRUE(window);
    const gfx::Rect parent = window->widget()->GetWindowBoundsInScreen();
    // A dot low in the sidebar column, the place the rail draws them.
    const gfx::Rect anchor(parent.x() + 60, parent.bottom() - 40, 14, 14);
    window->ShowThemeStudio(kPayload, rail_index, anchor);
    return window;
  }
};

IN_PROC_BROWSER_TEST_F(AtriumThemeStudioBrowserTest, OpensAboveTheParent) {
  atrium::AtriumBrowserWindow* window = OpenStudio();
  ASSERT_TRUE(window->ThemeStudioContentsForTesting());
  EXPECT_TRUE(window->IsThemeStudioShowing());
  const gfx::Rect parent = window->widget()->GetWindowBoundsInScreen();
  const gfx::Rect bounds = window->ThemeStudioBoundsForTesting();
  EXPECT_TRUE(parent.Contains(bounds)) << bounds.ToString() << " outside "
                                       << parent.ToString();
  EXPECT_EQ(atrium::ThemeStudioSizeFor(parent), bounds.size());
  EXPECT_EQ(0, window->ThemeStudioSpaceIndex());
  // A themeStudioReady that never arrives leaves every assertion above green.
  // The ?? '' matters: before the module builds the header the optional chain
  // yields undefined, and ExtractString CHECK-fails on the null it crosses as.
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(
               window->ThemeStudioContentsForTesting(),
               "document.querySelector('.tw-name')?.textContent ?? ''")
               .ExtractString() == "Personal";
  }));
}

IN_PROC_BROWSER_TEST_F(AtriumThemeStudioBrowserTest, ASecondOpenReusesThePage) {
  atrium::AtriumBrowserWindow* window = OpenStudio();
  content::WebContents* first = window->ThemeStudioContentsForTesting();
  ASSERT_TRUE(first);
  window->ShowThemeStudio(kPayload, 1,
                          window->ThemeStudioBoundsForTesting());
  EXPECT_EQ(first, window->ThemeStudioContentsForTesting());
  EXPECT_EQ(1, window->ThemeStudioSpaceIndex());
}

IN_PROC_BROWSER_TEST_F(AtriumThemeStudioBrowserTest, DragMovesAndClamps) {
  atrium::AtriumBrowserWindow* window = OpenStudio();
  const gfx::Rect parent = window->widget()->GetWindowBoundsInScreen();
  const gfx::Rect before = window->ThemeStudioBoundsForTesting();
  // Each axis travels toward its own roomier side, so the exact move below
  // holds whatever size of window this screen gave the test.
  const int dx = StepTowardTheRoomierSide(before.x() - parent.x(),
                                          parent.right() - before.right());
  const int dy = StepTowardTheRoomierSide(before.y() - parent.y(),
                                          parent.bottom() - before.bottom());
  ASSERT_NE(0, dx) << "the window is no wider than the studio card, so no "
                      "horizontal move can be asserted";
  window->DragThemeStudioByForTesting(dx, dy);
  gfx::Rect after = window->ThemeStudioBoundsForTesting();
  EXPECT_EQ(before.x() + dx, after.x());
  EXPECT_EQ(before.y() + dy, after.y());
  // Far past the right edge: the clamp holds the widget inside.
  window->DragThemeStudioByForTesting(parent.width() * 2, 0);
  after = window->ThemeStudioBoundsForTesting();
  EXPECT_EQ(parent.right() - after.width(), after.x());
  EXPECT_TRUE(parent.Contains(after));
}

IN_PROC_BROWSER_TEST_F(AtriumThemeStudioBrowserTest, FollowsTheParentWindow) {
  atrium::AtriumBrowserWindow* window = OpenStudio();
  const gfx::Rect before = window->ThemeStudioBoundsForTesting();
  gfx::Rect parent = window->widget()->GetWindowBoundsInScreen();
  parent.Offset(40, 25);
  window->widget()->SetBounds(parent);
  ASSERT_TRUE(base::test::RunUntil([&]() {
    return window->ThemeStudioBoundsForTesting().x() == before.x() + 40;
  }));
  EXPECT_EQ(before.y() + 25, window->ThemeStudioBoundsForTesting().y());
}

IN_PROC_BROWSER_TEST_F(AtriumThemeStudioBrowserTest,
                       CloseHidesAndDropsTheKeyboard) {
  atrium::AtriumBrowserWindow* window = OpenStudio();
  window->CloseThemeStudio();
  EXPECT_FALSE(window->IsThemeStudioShowing());
  EXPECT_EQ(-1, window->ThemeStudioSpaceIndex());
  EXPECT_FALSE(window->ThemeStudioPageHoldsKeyboardForTesting());
  // Idempotent.
  window->CloseThemeStudio();
  EXPECT_FALSE(window->IsThemeStudioShowing());
}

IN_PROC_BROWSER_TEST_F(AtriumThemeStudioBrowserTest,
                       ASwitchToAnotherSpaceClosesIt) {
  atrium::AtriumBrowserWindow* window = OpenStudio(0);
  base::ListValue same;
  same.Append(0);
  window->OnSurface_SidebarSpaceSelectedForTesting(same);
  EXPECT_TRUE(window->IsThemeStudioShowing())
      << "a switch to the edited space is the preview's own doing";
  base::ListValue other;
  other.Append(1);
  window->OnSurface_SidebarSpaceSelectedForTesting(other);
  EXPECT_FALSE(window->IsThemeStudioShowing());
}

}  // namespace
