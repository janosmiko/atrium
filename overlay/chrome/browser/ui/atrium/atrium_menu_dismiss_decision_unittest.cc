// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_menu_dismiss_decision.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

namespace atrium {
namespace {

// A left-docked sidebar 260 wide under a 30pt title bar.
constexpr gfx::Rect kSidebar(0, 30, 260, 800);

TEST(AtriumMenuDismissDecisionTest, PageClickDismissesWhileAMenuIsOpen) {
  EXPECT_TRUE(
      MouseDownDismissesSidebarMenu(true, kSidebar, gfx::Point(600, 400)));
}

TEST(AtriumMenuDismissDecisionTest, PageClickIsIgnoredWithNoMenuOpen) {
  EXPECT_FALSE(
      MouseDownDismissesSidebarMenu(false, kSidebar, gfx::Point(600, 400)));
}

TEST(AtriumMenuDismissDecisionTest, PressInsideTheSidebarIsThePagesOwn) {
  EXPECT_FALSE(
      MouseDownDismissesSidebarMenu(true, kSidebar, gfx::Point(120, 400)));
}

// Rect::Contains is half-open, so the far edge belongs to whatever is beyond
// it. The near edges are the sidebar's own.
TEST(AtriumMenuDismissDecisionTest, TheSurfaceOwnsItsNearEdgesOnly) {
  EXPECT_FALSE(MouseDownDismissesSidebarMenu(true, kSidebar, gfx::Point(0, 30)));
  EXPECT_TRUE(
      MouseDownDismissesSidebarMenu(true, kSidebar, gfx::Point(260, 400)));
  EXPECT_TRUE(MouseDownDismissesSidebarMenu(true, kSidebar, gfx::Point(120, 830)));
}

// A click above the sidebar's top is native chrome, not the page.
TEST(AtriumMenuDismissDecisionTest, TitleBarClickDismisses) {
  EXPECT_TRUE(MouseDownDismissesSidebarMenu(true, kSidebar, gfx::Point(120, 10)));
}

// No view means no bounds, and every press is then outside the surface.
TEST(AtriumMenuDismissDecisionTest, AnEmptyRectContainsNothing) {
  EXPECT_TRUE(
      MouseDownDismissesSidebarMenu(true, gfx::Rect(), gfx::Point(0, 0)));
}

}  // namespace
}  // namespace atrium
