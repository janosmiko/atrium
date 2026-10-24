// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_native_hotkey_defaults.cc
#include "chrome/browser/ui/atrium/atrium_native_hotkey_defaults.h"

#include <array>
#include <string_view>
#include <utility>

#include "base/notreached.h"

namespace atrium {

namespace {

// The ids are the ones hotkeys.json uses, so one file feeds this map and the
// Inputs pane's rows.
constexpr auto kDefaults = std::to_array<NativeHotkeyDefault>({
    // Cmd+T. The core ships openCommandBar with NO default binding of its own,
    // so this row is the only thing that binds it - which is why the Inputs
    // pane reads "Unassigned" for an action whose key works. Binding it in
    // hotkeys.json replaces this; leaving it out keeps it.
    {NativeHotkeyAction::kOpenCommandBar, "openCommandBar", "cmd+t"},
    // Cmd+S, released back to the native Save Page when rebound away.
    {NativeHotkeyAction::kToggleSidebar, "toggleSidebar", "cmd+s"},
    {NativeHotkeyAction::kNextTab, "nextTab", "cmd+opt+down"},
    {NativeHotkeyAction::kPreviousTab, "previousTab", "cmd+opt+up"},
    {NativeHotkeyAction::kEditCurrentURL, "editCurrentURL", "cmd+l"},
    {NativeHotkeyAction::kNextSpace, "nextSpace", "cmd+shift+right"},
    {NativeHotkeyAction::kPreviousSpace, "previousSpace", "cmd+shift+left"},
    {NativeHotkeyAction::kLittleWindow, "littleWindow", "cmd+opt+n"},
    {NativeHotkeyAction::kToggleOverview, "toggleOverview", "cmd+shift+o"},
    {NativeHotkeyAction::kToggleDevBar, "toggleDevBar", "cmd+shift+d"},
    {NativeHotkeyAction::kPinToggleTab, "pinToggleTab", "cmd+d"},
    // Cmd+Opt+1..9, the favourite tiles. Each digit is its own action so one
    // can be rebound without the rest, and the ninth is the core's
    // goToSidebarItemLast: the last tile, not a ninth slot.
    {NativeHotkeyAction::kGoToSidebarItem1, "goToSidebarItem1", "cmd+opt+1"},
    {NativeHotkeyAction::kGoToSidebarItem2, "goToSidebarItem2", "cmd+opt+2"},
    {NativeHotkeyAction::kGoToSidebarItem3, "goToSidebarItem3", "cmd+opt+3"},
    {NativeHotkeyAction::kGoToSidebarItem4, "goToSidebarItem4", "cmd+opt+4"},
    {NativeHotkeyAction::kGoToSidebarItem5, "goToSidebarItem5", "cmd+opt+5"},
    {NativeHotkeyAction::kGoToSidebarItem6, "goToSidebarItem6", "cmd+opt+6"},
    {NativeHotkeyAction::kGoToSidebarItem7, "goToSidebarItem7", "cmd+opt+7"},
    {NativeHotkeyAction::kGoToSidebarItem8, "goToSidebarItem8", "cmd+opt+8"},
    {NativeHotkeyAction::kGoToSidebarItemLast, "goToSidebarItemLast",
     "cmd+opt+9"},
    // Cmd+1..9, the spaces - see the enum's own comment.
    {NativeHotkeyAction::kGoToSpace1, "goToSpace1", "cmd+1"},
    {NativeHotkeyAction::kGoToSpace2, "goToSpace2", "cmd+2"},
    {NativeHotkeyAction::kGoToSpace3, "goToSpace3", "cmd+3"},
    {NativeHotkeyAction::kGoToSpace4, "goToSpace4", "cmd+4"},
    {NativeHotkeyAction::kGoToSpace5, "goToSpace5", "cmd+5"},
    {NativeHotkeyAction::kGoToSpace6, "goToSpace6", "cmd+6"},
    {NativeHotkeyAction::kGoToSpace7, "goToSpace7", "cmd+7"},
    {NativeHotkeyAction::kGoToSpace8, "goToSpace8", "cmd+8"},
    {NativeHotkeyAction::kGoToSpace9, "goToSpace9", "cmd+9"},
    // No default combo - see the enum's own comment.
    {NativeHotkeyAction::kCreateSpace, "createSpace", ""},
    {NativeHotkeyAction::kOpenArchive, "openArchive", "cmd+shift+a"},
    {NativeHotkeyAction::kCloseSplitPane, "closeSplitPane", "shift+ctrl+-"},
    {NativeHotkeyAction::kToggleSplitOrientation, "toggleSplitOrientation",
     "shift+ctrl+\\"},
    {NativeHotkeyAction::kToggleDeveloperMode, "toggleDeveloperMode",
     "opt+ctrl+d"},
    // Spelled in the core's canonical modifier order (cmd, shift, opt, ctrl),
    // because the cross-table guard byte-matches these against default_hotkeys.
    {NativeHotkeyAction::kReload, "reload", "cmd+r"},
    {NativeHotkeyAction::kHardReload, "hardReload", "cmd+shift+r"},
    {NativeHotkeyAction::kReloadClearingCache, "reloadClearingCache",
     "cmd+opt+r"},
    {NativeHotkeyAction::kReloadClearingSiteData, "reloadClearingSiteData",
     "cmd+shift+opt+r"},
    {NativeHotkeyAction::kClearTabs, "clearTabs", "cmd+shift+k"},
    {NativeHotkeyAction::kToggleAiPanel, "toggleAiPanel", "cmd+shift+i"},
});

static_assert(kDefaults.size() == kNativeHotkeyActionCount);
static_assert(kNativeHotkeyActionCount ==
                  static_cast<size_t>(NativeHotkeyAction::kMaxValue) + 1,
              "one row per NativeHotkeyAction");

}  // namespace

base::span<const NativeHotkeyDefault> NativeHotkeyDefaults() {
  return kDefaults;
}

const NativeHotkeyDefault& NativeHotkeyDefaultFor(NativeHotkeyAction action) {
  for (const NativeHotkeyDefault& entry : kDefaults) {
    if (entry.action == action) {
      return entry;
    }
  }
  // Unreachable for a valid enumerator: the static_asserts pin the count and
  // the unit test walks the whole enum. A CHECK rather than a fallback row,
  // because the fallback would be some other action's combo.
  NOTREACHED();
}

std::optional<NativeHotkeyAction> NativeHotkeyActionForId(
    std::string_view action_id) {
  for (const NativeHotkeyDefault& entry : kDefaults) {
    if (entry.action_id == action_id) {
      return entry.action;
    }
  }
  return std::nullopt;
}

std::optional<NativeHotkeyAction> NativeHotkeyActionForSpaceIndex(int index) {
  static constexpr auto kSpaceActions = std::to_array<NativeHotkeyAction>({
      NativeHotkeyAction::kGoToSpace1, NativeHotkeyAction::kGoToSpace2,
      NativeHotkeyAction::kGoToSpace3, NativeHotkeyAction::kGoToSpace4,
      NativeHotkeyAction::kGoToSpace5, NativeHotkeyAction::kGoToSpace6,
      NativeHotkeyAction::kGoToSpace7, NativeHotkeyAction::kGoToSpace8,
      NativeHotkeyAction::kGoToSpace9,
  });
  if (index < 0 || static_cast<size_t>(index) >= kSpaceActions.size()) {
    return std::nullopt;
  }
  return kSpaceActions[static_cast<size_t>(index)];
}

std::string_view UnshiftedGlyph(std::string_view shifted) {
  // US layout, the same assumption the kVK_* constants and the harness
  // keycodes already make. Letters are absent on purpose: the matcher
  // lowercases, so "A" already compares equal to "a".
  static constexpr std::pair<std::string_view, std::string_view> kPairs[] = {
      {"~", "`"}, {"!", "1"}, {"@", "2"}, {"#", "3"},  {"$", "4"},
      {"%", "5"}, {"^", "6"}, {"&", "7"}, {"*", "8"},  {"(", "9"},
      {")", "0"}, {"_", "-"}, {"+", "="}, {"{", "["},  {"}", "]"},
      {"|", "\\"}, {":", ";"}, {"\"", "'"}, {"<", ","}, {">", "."},
      {"?", "/"},
  };
  for (const auto& [with_shift, without] : kPairs) {
    if (shifted == with_shift) {
      return without;
    }
  }
  return std::string_view();
}

bool IsReservedHotkeyCombo(std::string_view key,
                           bool cmd,
                           bool shift,
                           bool opt,
                           bool ctrl) {
  // Option is in neither chord, and adding it makes a combo nothing claims.
  if (opt) {
    return false;
  }
  if (key == "tab") {
    return ctrl && !cmd;
  }
  if (key == "w") {
    return cmd && shift && !ctrl;
  }
  return false;
}

}  // namespace atrium
