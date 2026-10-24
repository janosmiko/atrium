// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_command_bar_actions.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_BAR_ACTIONS_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_BAR_ACTIONS_H_

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "chrome/browser/ui/atrium/atrium_native_hotkey_defaults.h"

namespace atrium {

// Why a native hotkey action is not a palette row. The switch answering this
// has NO default arm, so a new NativeHotkeyAction stops the build until
// someone classifies it.
enum class CommandBarActionOmission {
  // The catalog carries it.
  kNone,
  // Cmd+T and Cmd+L both open this palette.
  kOpensTheCommandBar,
  // The palette lists the spaces, tabs and favourites by name already, and
  // "go to space 3" cannot say which space that is.
  kNavigatesRowsThePaletteLists,
  // No registered window message runs it with no arguments, so a row would
  // read live and do nothing. See kCatalog in the .cc.
  kNoPaletteMessage,
};

CommandBarActionOmission CommandBarActionOmissionFor(NativeHotkeyAction action);

// One row of the palette's action section, already resolved.
struct CommandBarAction {
  CommandBarAction();
  CommandBarAction(const CommandBarAction&);
  CommandBarAction& operator=(const CommandBarAction&);
  CommandBarAction(CommandBarAction&&);
  CommandBarAction& operator=(CommandBarAction&&);
  ~CommandBarAction();

  std::string title;
  std::string subtitle;
  // Matched on word prefixes by the page.
  std::vector<std::string> aliases;
  std::string message;
  std::vector<std::string> args;
  // Whose binding `binding` shows. Absent for a verb no key runs.
  std::optional<NativeHotkeyAction> hotkey_action;
  // Empty when the row has no hotkey action, or has one nothing is bound to.
  std::string binding;
};

// "cmd+shift+s" -> "⌘⇧S", in the ⌃⌥⇧⌘ order macOS writes shortcuts in. Empty
// when the key cannot be named ("cmd++" splits to no key): a chip is a hint,
// so an unreadable one is left off rather than guessed at.
std::string CommandBarShortcutGlyphs(std::string_view combo);

// `bindings` is NativeHotkeyMap::BindingsById. An absent id falls back to the
// compiled-in combo and an id with no combos is unbound, as SetFromJSON reads
// the two.
std::vector<CommandBarAction> CommandBarActionCatalog(
    const std::map<std::string, std::vector<std::string>>& bindings);

// Serializes `actions` into the payload window.atriumSetCommandBarActions
// takes: {"actions":[{"kind":"action","title":..,"send":..,"binding":..}]}
std::string BuildCommandBarActionsJSON(
    const std::vector<CommandBarAction>& actions);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_BAR_ACTIONS_H_
