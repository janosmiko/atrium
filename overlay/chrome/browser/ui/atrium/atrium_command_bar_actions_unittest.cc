// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_command_bar_actions_unittest.cc
//
//   autoninja -C out/atrium unit_tests &&
//   out/atrium/unit_tests --gtest_filter=CommandBarActions*

#include "chrome/browser/ui/atrium/atrium_command_bar_actions.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "chrome/browser/ui/atrium/atrium_command_bar_widget.h"
#include "chrome/browser/ui/atrium/atrium_surface_host.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

std::map<std::string, std::vector<std::string>> NoBindings() {
  return {};
}

const CommandBarAction* RowTitled(const std::vector<CommandBarAction>& catalog,
                                  const std::string& title) {
  for (const CommandBarAction& action : catalog) {
    if (action.title == title) {
      return &action;
    }
  }
  return nullptr;
}

bool Reaches(const CommandBarAction& action, const std::string& alias) {
  return std::ranges::find(action.aliases, alias) != action.aliases.end();
}

TEST(CommandBarActionsTest, EveryRowNamesARegisteredMessage) {
  // The rule the palette's whole vocabulary rests on: an unregistered
  // chrome.send name is FATAL in the browser process, and a row with no
  // message at all reads live and does nothing.
  const std::vector<CommandBarAction> catalog =
      CommandBarActionCatalog(NoBindings());
  ASSERT_FALSE(catalog.empty());
  for (const CommandBarAction& action : catalog) {
    EXPECT_FALSE(action.message.empty()) << action.title << " sends nothing";
    EXPECT_NE(std::ranges::find(kSurfaceMessages, action.message),
              std::end(kSurfaceMessages))
        << action.message << " is not in kSurfaceMessages";
  }
}

TEST(CommandBarActionsTest, EveryActionIsEitherARowOrOmittedForAReason) {
  // A new NativeHotkeyAction stops the build until it is classified, and fails
  // here until a classification of kNone is backed by a row with a message.
  const std::vector<CommandBarAction> catalog =
      CommandBarActionCatalog(NoBindings());
  for (const NativeHotkeyDefault& entry : NativeHotkeyDefaults()) {
    const bool has_row =
        std::ranges::any_of(catalog, [&entry](const CommandBarAction& action) {
          return action.hotkey_action == entry.action;
        });
    const bool offered = CommandBarActionOmissionFor(entry.action) ==
                         CommandBarActionOmission::kNone;
    EXPECT_EQ(offered, has_row)
        << entry.action_id << " is classified kNone=" << offered
        << " but the catalog says otherwise";
  }
}

TEST(CommandBarActionsTest, NavigationAndTheOpenVerbAreNotRows) {
  EXPECT_EQ(CommandBarActionOmission::kOpensTheCommandBar,
            CommandBarActionOmissionFor(NativeHotkeyAction::kOpenCommandBar));
  EXPECT_EQ(CommandBarActionOmission::kOpensTheCommandBar,
            CommandBarActionOmissionFor(NativeHotkeyAction::kEditCurrentURL));
  for (NativeHotkeyAction action :
       {NativeHotkeyAction::kNextTab, NativeHotkeyAction::kPreviousTab,
        NativeHotkeyAction::kNextSpace, NativeHotkeyAction::kPreviousSpace,
        NativeHotkeyAction::kGoToSidebarItem1,
        NativeHotkeyAction::kGoToSidebarItemLast,
        NativeHotkeyAction::kGoToSpace1, NativeHotkeyAction::kGoToSpace9}) {
    EXPECT_EQ(CommandBarActionOmission::kNavigatesRowsThePaletteLists,
              CommandBarActionOmissionFor(action));
  }
}

TEST(CommandBarActionsTest, AnActionWithNoMessageIsNotARow) {
  // Not a row and not silently included either: the reason is named, so the
  // next person to add littleWindow's message knows where to look.
  for (NativeHotkeyAction action :
       {NativeHotkeyAction::kLittleWindow, NativeHotkeyAction::kToggleDevBar,
        NativeHotkeyAction::kPinToggleTab}) {
    EXPECT_EQ(CommandBarActionOmission::kNoPaletteMessage,
              CommandBarActionOmissionFor(action));
  }
}

TEST(CommandBarActionsTest, TheSevenOriginalRowsKeepTheirAliases) {
  const std::vector<CommandBarAction> catalog =
      CommandBarActionCatalog(NoBindings());
  const std::vector<std::pair<std::string, std::string>> kPairs = {
      {"Settings", "preferences"},        {"Courtyard", "spaces"},
      {"Overview", "overview"},           {"New Space", "new space"},
      {"Capture Viewport", "shot"},       {"Capture Region", "crop"},
      {"Capture Full Page", "whole"},
  };
  for (const auto& [title, alias] : kPairs) {
    const CommandBarAction* row = RowTitled(catalog, title);
    ASSERT_TRUE(row) << title << " is gone from the palette";
    EXPECT_TRUE(Reaches(*row, alias)) << title << " lost the alias " << alias;
  }
}

TEST(CommandBarActionsTest, SettingsStillAsksForNoPane) {
  const std::vector<CommandBarAction> catalog =
      CommandBarActionCatalog(NoBindings());
  const CommandBarAction* row = RowTitled(catalog, "Settings");
  ASSERT_TRUE(row);
  EXPECT_EQ("openSettingsPane", row->message);
  ASSERT_EQ(1u, row->args.size());
  EXPECT_EQ("", row->args[0]);
}

TEST(CommandBarActionsTest, ImportFromBrowserOpensTheImportPane) {
  const std::vector<CommandBarAction> catalog =
      CommandBarActionCatalog(NoBindings());
  const CommandBarAction* row = RowTitled(catalog, "Import from Browser");
  ASSERT_TRUE(row);
  EXPECT_EQ("openSettingsPane", row->message);
  ASSERT_EQ(1u, row->args.size());
  EXPECT_EQ("import", row->args[0]);
  EXPECT_TRUE(Reaches(*row, "arc"));
  EXPECT_TRUE(Reaches(*row, "import bookmarks"));
}

TEST(CommandBarActionsTest, AVerbWithNoArgumentsCarriesNone) {
  const std::vector<CommandBarAction> catalog =
      CommandBarActionCatalog(NoBindings());
  const CommandBarAction* row = RowTitled(catalog, "Capture Region");
  ASSERT_TRUE(row);
  EXPECT_TRUE(row->args.empty());
}

TEST(CommandBarActionsTest, AnUnboundActionShowsNoShortcut) {
  const std::vector<CommandBarAction> catalog =
      CommandBarActionCatalog({{"toggleSidebar", {}}});
  const CommandBarAction* row = RowTitled(catalog, "Toggle Sidebar");
  ASSERT_TRUE(row);
  EXPECT_EQ("", row->binding);
}

TEST(CommandBarActionsTest, AVerbWithNoHotkeyActionShowsNoShortcut) {
  const std::vector<CommandBarAction> catalog =
      CommandBarActionCatalog(NoBindings());
  const CommandBarAction* row = RowTitled(catalog, "Capture Viewport");
  ASSERT_TRUE(row);
  EXPECT_EQ("", row->binding);
}

TEST(CommandBarActionsTest, TogglesTheAiPanelSurface) {
  const std::vector<CommandBarAction> catalog =
      CommandBarActionCatalog(NoBindings());
  const CommandBarAction* row = RowTitled(catalog, "Toggle AI panel");
  ASSERT_TRUE(row);
  EXPECT_EQ("aiPanelToggleRequested", row->message);
  EXPECT_TRUE(Reaches(*row, "ai panel"));
  EXPECT_EQ("⇧⌘I", row->binding);
}

// No hotkey: Arc, Chrome and Safari bind none for an extension side panel.
TEST(CommandBarActionsTest, TogglesTheExtensionSidePanel) {
  const std::vector<CommandBarAction> catalog =
      CommandBarActionCatalog(NoBindings());
  const CommandBarAction* row = RowTitled(catalog, "Toggle side panel");
  ASSERT_TRUE(row);
  EXPECT_EQ("extensionSidePanelToggleRequested", row->message);
  EXPECT_TRUE(Reaches(*row, "side panel"));
  EXPECT_TRUE(row->binding.empty());
}

// Settings shows accelerators_cocoa.mm's stock IDC_OPTIONS combo, which
// hotkeys.json cannot rebind - so `bindings` is empty here on purpose.
TEST(CommandBarActionsTest, SettingsShowsItsStockAcceleratorEvenUnbound) {
  const std::vector<CommandBarAction> catalog =
      CommandBarActionCatalog(NoBindings());
  const CommandBarAction* settings = RowTitled(catalog, "Settings");
  ASSERT_TRUE(settings);
  EXPECT_EQ("⌘,", settings->binding);
  const CommandBarAction* courtyard = RowTitled(catalog, "Courtyard");
  ASSERT_TRUE(courtyard);
  EXPECT_EQ("", courtyard->binding);
}

TEST(CommandBarActionsTest, AnAbsentActionFallsBackToItsCompiledInCombo) {
  // hotkeys.json names most actions and not all of them, and SetFromJSON
  // leaves an absent one on its built-in combo. A chip that read empty there
  // would say "unbound" about a key that works.
  const std::vector<CommandBarAction> catalog =
      CommandBarActionCatalog(NoBindings());
  const CommandBarAction* row = RowTitled(catalog, "Toggle Sidebar");
  ASSERT_TRUE(row);
  EXPECT_EQ("⌘S", row->binding);
}

TEST(CommandBarActionsTest, ARebindingChangesTheShortcutTheRowShows) {
  const std::vector<CommandBarAction> catalog =
      CommandBarActionCatalog({{"toggleSidebar", {"cmd+shift+j"}}});
  const CommandBarAction* row = RowTitled(catalog, "Toggle Sidebar");
  ASSERT_TRUE(row);
  EXPECT_EQ("⇧⌘J", row->binding);
}

TEST(CommandBarActionsTest, TheFirstOfSeveralCombosIsTheOneShown) {
  const std::vector<CommandBarAction> catalog = CommandBarActionCatalog(
      {{"toggleOverview", {"cmd+shift+o", "opt+ctrl+v"}}});
  const CommandBarAction* row = RowTitled(catalog, "Overview");
  ASSERT_TRUE(row);
  EXPECT_EQ("⇧⌘O", row->binding);
}

TEST(CommandBarActionsTest, GlyphsFollowTheOrderMacOSWritesThemIn) {
  EXPECT_EQ("⌘S", CommandBarShortcutGlyphs("cmd+s"));
  EXPECT_EQ("⌃⌥⇧⌘K", CommandBarShortcutGlyphs("cmd+shift+opt+ctrl+k"));
  EXPECT_EQ("⌥⌘↓", CommandBarShortcutGlyphs("cmd+opt+down"));
  EXPECT_EQ("⌘1", CommandBarShortcutGlyphs("cmd+1"));
  EXPECT_EQ("⌃⇧⇥", CommandBarShortcutGlyphs("shift+ctrl+tab"));
}

TEST(CommandBarActionsTest, AComboWithNoKeyRendersNothing) {
  EXPECT_EQ("", CommandBarShortcutGlyphs(""));
  EXPECT_EQ("", CommandBarShortcutGlyphs("cmd+"));
  EXPECT_EQ("", CommandBarShortcutGlyphs("cmd+shift"));
}

TEST(CommandBarActionsTest, TheSnapshotCarriesWhatThePageReads) {
  const std::string json = BuildCommandBarActionsJSON(
      CommandBarActionCatalog({{"toggleSidebar", {"cmd+shift+j"}}}));
  EXPECT_NE(std::string::npos, json.find("\"kind\":\"action\""));
  EXPECT_NE(std::string::npos,
            json.find("\"send\":\"sidebarToggleRequested\""));
  EXPECT_NE(std::string::npos, json.find("\"binding\":\"⇧⌘J\""));
  EXPECT_NE(std::string::npos, json.find("\"aliases\":[\"settings\""));
  EXPECT_NE(std::string::npos, json.find("\"args\":[\"\"]"));
}

TEST(CommandBarActionsTest, AnEmptyCatalogSerializesAsAnEmptyList) {
  EXPECT_EQ("{\"actions\":[]}", BuildCommandBarActionsJSON({}));
}

// The launch palette can be left visible but not key, so a deferred reclaim
// re-Activates it. Activate() is app-level, so every condition below has to
// hold or the reclaim yanks the whole app in front of the user.
TEST(CommandBarReclaimKeyTest, ReclaimsOnlyTheParentWindowsKeyWhileAppIsActive) {
  EXPECT_TRUE(ShouldReclaimKey(/*is_visible=*/true, /*epoch_current=*/true,
                               /*is_active=*/false, /*app_active=*/true,
                               /*key_is_parent=*/true));

  // The permission sheet or the settings window asked for key and needs it to
  // answer Enter and Escape - taking it back would break them.
  EXPECT_FALSE(ShouldReclaimKey(true, true, false, true,
                                /*key_is_parent=*/false));

  // Another app is frontmost: activating would haul Atrium over it.
  EXPECT_FALSE(ShouldReclaimKey(true, true, false, /*app_active=*/false, true));

  // Already key, so there is nothing to reclaim and no reason to re-Activate.
  EXPECT_FALSE(ShouldReclaimKey(true, true, /*is_active=*/true, true, true));

  // The palette regained key since this was posted, so the task is stale.
  EXPECT_FALSE(ShouldReclaimKey(true, /*epoch_current=*/false, false, true,
                                true));

  // Closed or hidden in the runloop turn this was deferred across.
  EXPECT_FALSE(ShouldReclaimKey(/*is_visible=*/false, true, false, true, true));
}

}  // namespace
}  // namespace atrium
