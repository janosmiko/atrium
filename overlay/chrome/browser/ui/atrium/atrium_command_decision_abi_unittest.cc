// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_command_decision_abi_unittest.cc
//
// the command-routing decisions moved into the Rust core, and these
// are the cases that came with them.
//
// What each decision ANSWERS is asserted in the crate, in commands.rs, where it
// runs without a Chromium checkout. These tests exist for the one thing the
// crate cannot check itself: the core carries a COPY of Chromium's IDC_*
// numbers, because Rust cannot include chrome_command_ids.h. Every case below
// calls across the ABI with the real macro, so a renumbering upstream fails
// here and names the command, rather than silently rerouting a key.
//
// That makes this file a pin, not a duplicate suite. If a case here ever
// disagrees with commands.rs, the numbers have drifted.

#include "third_party/atrium/core/include/atrium_core.h"

#include "chrome/app/chrome_command_ids.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

// Every gate on, so a test can turn ONE off and see what that alone changes.
AtriumCommandInterceptInputs AllOn(int command) {
  AtriumCommandInterceptInputs in = {};
  in.command = command;
  in.is_before_first_responder = true;
  in.global_accelerators_suspended = false;
  in.is_reserved_command = false;
  in.sidebar_toggle_combo_bound = true;
  in.command_bar_combo_bound = true;
  in.sidebar_toggle_available = true;
  return in;
}

AtriumCommandIntercept InterceptFor(const AtriumCommandInterceptInputs& in) {
  return static_cast<AtriumCommandIntercept>(
      atrium_core_command_intercept(&in));
}

// MARK: - What is claimed before the first responder

TEST(AtriumCommandDecisionAbiTest, SuspendedAcceleratorsClaimNothing) {
  AtriumCommandInterceptInputs in = AllOn(IDC_SAVE_PAGE);
  in.global_accelerators_suspended = true;
  EXPECT_FALSE(atrium_core_will_claim_command(&in));
}

// IDC_SAVE_PAGE is not reserved, so without this claim the key press reaches
// the renderer and then Save Page, and the toggle never runs.
TEST(AtriumCommandDecisionAbiTest, SavePageIsClaimedForTheSidebarToggle) {
  const AtriumCommandInterceptInputs in = AllOn(IDC_SAVE_PAGE);
  EXPECT_TRUE(atrium_core_will_claim_command(&in));
}

// Each gate is independently necessary. A decision table that keeps passing
// after one input stops mattering has stopped meaning anything.
TEST(AtriumCommandDecisionAbiTest, EverySavePageGateIsNecessary) {
  {
    AtriumCommandInterceptInputs in = AllOn(IDC_SAVE_PAGE);
    in.sidebar_toggle_combo_bound = false;
    EXPECT_FALSE(atrium_core_will_claim_command(&in))
        << "rebound away, the key is Save Page";
  }
  {
    AtriumCommandInterceptInputs in = AllOn(IDC_SAVE_PAGE);
    in.sidebar_toggle_available = false;
    EXPECT_FALSE(atrium_core_will_claim_command(&in))
        << "nothing can run the toggle, so the key is Save Page";
  }
}

// Cmd+, and the menu item belong to stock chrome://settings.
TEST(AtriumCommandDecisionAbiTest, OptionsIsNoLongerClaimed) {
  const AtriumCommandInterceptInputs in = AllOn(IDC_OPTIONS);
  EXPECT_FALSE(atrium_core_will_claim_command(&in));
}

// Everything else follows Chrome: only a reserved command bypasses the first
// responder.
TEST(AtriumCommandDecisionAbiTest, EverythingElseFollowsTheReservedList) {
  AtriumCommandInterceptInputs in = AllOn(IDC_CLOSE_TAB);
  in.is_reserved_command = false;
  EXPECT_FALSE(atrium_core_will_claim_command(&in));
  in.is_reserved_command = true;
  EXPECT_TRUE(atrium_core_will_claim_command(&in));
}

// The menu-item path is not early, so there is nothing to claim early.
TEST(AtriumCommandDecisionAbiTest, TheMenuPathIsAlwaysLetThrough) {
  AtriumCommandInterceptInputs in = AllOn(IDC_SAVE_PAGE);
  in.is_before_first_responder = false;
  in.global_accelerators_suspended = true;
  EXPECT_TRUE(atrium_core_will_claim_command(&in));
}

// MARK: - What runs

TEST(AtriumCommandDecisionAbiTest, SavePageTogglesTheSidebarOnTheKeyPathOnly) {
  EXPECT_EQ(InterceptFor(AllOn(IDC_SAVE_PAGE)),
            ATRIUM_COMMAND_INTERCEPT_TOGGLE_SIDEBAR);

  // The "Save Page As..." menu item must still save the page.
  AtriumCommandInterceptInputs in = AllOn(IDC_SAVE_PAGE);
  in.is_before_first_responder = false;
  EXPECT_EQ(InterceptFor(in), ATRIUM_COMMAND_INTERCEPT_NONE);
}

TEST(AtriumCommandDecisionAbiTest, OptionsRunsNoIntercept) {
  EXPECT_EQ(InterceptFor(AllOn(IDC_OPTIONS)), ATRIUM_COMMAND_INTERCEPT_NONE);

  AtriumCommandInterceptInputs in = AllOn(IDC_OPTIONS);
  in.is_before_first_responder = false;
  EXPECT_EQ(InterceptFor(in), ATRIUM_COMMAND_INTERCEPT_NONE);
}

// The stock path returns early on a tabless window AND reports success, so the
// quit is swallowed and even the Cmd+Q fallback never gets a turn.
TEST(AtriumCommandDecisionAbiTest, ExitIsAlwaysIntercepted) {
  EXPECT_EQ(InterceptFor(AllOn(IDC_EXIT)), ATRIUM_COMMAND_INTERCEPT_EXIT);

  AtriumCommandInterceptInputs in = AllOn(IDC_EXIT);
  in.sidebar_toggle_available = false;
  in.sidebar_toggle_combo_bound = false;
  in.command_bar_combo_bound = false;
  EXPECT_EQ(InterceptFor(in), ATRIUM_COMMAND_INTERCEPT_EXIT)
      << "quitting must not depend on any other gate";
}

// An Atrium window's strip holds one live tab, so closing it empties the strip
// - which is exactly when Cmd+Shift+T goes dead on the stock path.
TEST(AtriumCommandDecisionAbiTest, RestoreTabIsAlwaysIntercepted) {
  EXPECT_EQ(InterceptFor(AllOn(IDC_RESTORE_TAB)),
            ATRIUM_COMMAND_INTERCEPT_RESTORE_TAB);
}

// closing must not depend on there being a tab either, same wall as
// IDC_EXIT/IDC_RESTORE_TAB.
TEST(AtriumCommandDecisionAbiTest, CloseWindowIsAlwaysIntercepted) {
  EXPECT_EQ(InterceptFor(AllOn(IDC_CLOSE_WINDOW)),
            ATRIUM_COMMAND_INTERCEPT_CLOSE_WINDOW);
}

TEST(AtriumCommandDecisionAbiTest, NewTabOpensThePaletteWhileItIsBound) {
  EXPECT_EQ(InterceptFor(AllOn(IDC_NEW_TAB)),
            ATRIUM_COMMAND_INTERCEPT_COMMAND_PALETTE);
}

// Rebound away, Cmd+T is a blank tab - never the stock new-tab handler, which
// is fatal for a chromeless window.
TEST(AtriumCommandDecisionAbiTest, NewTabFallsBackToABlankTabNotTheStockPath) {
  AtriumCommandInterceptInputs in = AllOn(IDC_NEW_TAB);
  in.command_bar_combo_bound = false;
  EXPECT_EQ(InterceptFor(in), ATRIUM_COMMAND_INTERCEPT_BLANK_TAB);
}

// this used to need the dylib's spaces group, so since the cut
// Cmd+N opened a stock window with no controller. CreateWindowSlot needs
// nothing from a dylib, so the slot is unconditional now.
TEST(AtriumCommandDecisionAbiTest, NewWindowAlwaysOpensAWiredSlot) {
  EXPECT_EQ(InterceptFor(AllOn(IDC_NEW_WINDOW)),
            ATRIUM_COMMAND_INTERCEPT_NEW_WINDOW_SLOT);

  AtriumCommandInterceptInputs in = AllOn(IDC_NEW_WINDOW);
  in.sidebar_toggle_available = false;
  in.sidebar_toggle_combo_bound = false;
  EXPECT_EQ(InterceptFor(in), ATRIUM_COMMAND_INTERCEPT_NEW_WINDOW_SLOT);
}

// Split view still has no Atrium answer, so it stays swallowed. The find
// commands do not: they open Atrium's own bar.
TEST(AtriumCommandDecisionAbiTest, SplitViewIsStillSwallowed) {
  EXPECT_EQ(InterceptFor(AllOn(IDC_NEW_SPLIT_TAB)),
            ATRIUM_COMMAND_INTERCEPT_SWALLOW);
}

TEST(AtriumCommandDecisionAbiTest, TheFindCommandsOpenAtriumsOwnBar) {
  EXPECT_EQ(InterceptFor(AllOn(IDC_FIND)), ATRIUM_COMMAND_INTERCEPT_FIND_OPEN);
  EXPECT_EQ(InterceptFor(AllOn(IDC_FIND_NEXT)),
            ATRIUM_COMMAND_INTERCEPT_FIND_NEXT);
  EXPECT_EQ(InterceptFor(AllOn(IDC_FIND_PREVIOUS)),
            ATRIUM_COMMAND_INTERCEPT_FIND_PREVIOUS);
}

// Cmd+F is not reserved, so the menu path must intercept too.
TEST(AtriumCommandDecisionAbiTest, FindIsInterceptedOnTheMenuPathToo) {
  AtriumCommandInterceptInputs in = AllOn(IDC_FIND);
  in.is_before_first_responder = false;
  EXPECT_EQ(InterceptFor(in), ATRIUM_COMMAND_INTERCEPT_FIND_OPEN);
}

// Enablement only: a presenter window's browser can have no active tab while
// the tab on screen belongs to another one.
TEST(AtriumCommandDecisionAbiTest, FindEnablesOffTheVisibleTab) {
  for (int command : {IDC_FIND, IDC_FIND_NEXT, IDC_FIND_PREVIOUS}) {
    EXPECT_TRUE(atrium_core_command_acts_on_visible_tab(command))
        << "command " << command;
  }
}

// An ordinary command has no intercept and dispatches normally. Without this a
// decision that returned something for everything would still pass.
TEST(AtriumCommandDecisionAbiTest, AnOrdinaryCommandIsNotIntercepted) {
  for (int command : {IDC_RELOAD, IDC_BACK, IDC_PRINT, IDC_ZOOM_PLUS}) {
    EXPECT_EQ(InterceptFor(AllOn(command)), ATRIUM_COMMAND_INTERCEPT_NONE)
        << "command " << command;
  }
}

// MARK: - Which browser a command acts on

// The sets that shipped before stage 2. Kept as their
// own cases so a regression names the stage it broke.
TEST(AtriumCommandDecisionAbiTest,
     TabAndWindowScopedCommandsFollowTheVisibleTab) {
  for (int command : {IDC_CLOSE_TAB, IDC_CLOSE_WINDOW, IDC_SELECT_NEXT_TAB,
                      IDC_SELECT_PREVIOUS_TAB, IDC_RESTORE_TAB, IDC_NEW_TAB}) {
    EXPECT_TRUE(atrium_core_command_acts_on_visible_tab(command))
        << "command " << command;
  }
}

TEST(AtriumCommandDecisionAbiTest, NavigationFollowsTheVisibleTab) {
  for (int command : {IDC_RELOAD, IDC_RELOAD_BYPASSING_CACHE, IDC_BACK,
                      IDC_FORWARD, IDC_STOP, IDC_HOME}) {
    EXPECT_TRUE(atrium_core_command_acts_on_visible_tab(command))
        << "command " << command;
  }
}

// Enumerated in the task by user impact. Every one of these
// reads or mutates the page, so it belongs to the tab on screen - the whole
// point of the stage.
TEST(AtriumCommandDecisionAbiTest, StageTwoPerTabCommandsFollowTheVisibleTab) {
  for (int command : {IDC_BOOKMARK_THIS_TAB, IDC_ZOOM_PLUS, IDC_ZOOM_MINUS,
                      IDC_ZOOM_NORMAL, IDC_SHOW_HISTORY, IDC_SHOW_DOWNLOADS,
                      IDC_VIEW_SOURCE, IDC_DEV_TOOLS, IDC_DEV_TOOLS_CONSOLE,
                      IDC_DEV_TOOLS_DEVICES, IDC_DEV_TOOLS_INSPECT,
                      IDC_DEV_TOOLS_TOGGLE, IDC_PRINT, IDC_BASIC_PRINT,
                      IDC_DUPLICATE_TAB, IDC_MANAGE_PASSWORDS_FOR_PAGE,
                      IDC_SHOW_TRANSLATE, IDC_SAVE_PAGE}) {
    EXPECT_TRUE(atrium_core_command_acts_on_visible_tab(command))
        << "command " << command;
  }
}

// App-scoped commands stay on the invoking window ON PURPOSE. IDC_NEW_WINDOW
// is the one that reads like a mistake and is not: it resolves the visible
// PROFILE inside CreateWindowSlot instead, which is a different question from
// which Browser the command runs on. Asserted here so nobody "fixes" it by
// adding it to the set and quietly double-resolves it.
TEST(AtriumCommandDecisionAbiTest, AppScopedCommandsStayOnTheInvokingWindow) {
  for (int command : {IDC_EXIT, IDC_NEW_WINDOW, IDC_OPTIONS}) {
    EXPECT_FALSE(atrium_core_command_acts_on_visible_tab(command))
        << "command " << command;
  }
}

// An unknown command must fall through rather than being swept into the set by
// a range check or a default that leans the wrong way.
TEST(AtriumCommandDecisionAbiTest, AnUnknownCommandDoesNotFollowTheVisibleTab) {
  EXPECT_FALSE(atrium_core_command_acts_on_visible_tab(-1));
  EXPECT_FALSE(atrium_core_command_acts_on_visible_tab(0));
  EXPECT_FALSE(atrium_core_command_acts_on_visible_tab(999999));
}

// MARK: - The boundary itself

// Null is the one shape C++ can hand over that the crate's own tests cannot
// reach. Letting a key through is recoverable; claiming one Atrium then cannot
// run is not, so the safe answers are "not claimed" and "no intercept".
TEST(AtriumCommandDecisionAbiTest, ANullArgumentClaimsNothingAndRunsNothing) {
  EXPECT_FALSE(atrium_core_will_claim_command(nullptr));
  EXPECT_EQ(static_cast<AtriumCommandIntercept>(
                atrium_core_command_intercept(nullptr)),
            ATRIUM_COMMAND_INTERCEPT_NONE);
}

}  // namespace
