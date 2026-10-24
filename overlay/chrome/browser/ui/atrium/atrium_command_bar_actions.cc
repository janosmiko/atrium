// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_command_bar_actions.cc
#include "chrome/browser/ui/atrium/atrium_command_bar_actions.h"

#include <array>
#include <optional>

#include "base/json/json_writer.h"
#include "base/notreached.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "base/values.h"

namespace atrium {

namespace {

struct CatalogRow {
  // Never empty, and the reason this table exists: the field that names the
  // handler is the one that decides the row exists at all.
  std::string_view message;
  // The single argument the message takes, or nullopt for none.
  std::optional<std::string_view> arg;
  std::string_view title;
  std::string_view subtitle;
  // The action whose binding the row shows. Absent for a verb no key runs.
  std::optional<NativeHotkeyAction> action;
  // '|'-separated, so a row stays one line of table rather than a table plus
  // an array per row.
  std::string_view aliases;
  // A combo accelerators_cocoa.mm owns, for a row whose key hotkeys.json
  // cannot rebind. Read only when `action` is absent.
  std::optional<std::string_view> fixed_combo;
};

// Every row the palette offers. A verb needs a registered window message whose
// handler takes NO arguments - one needing a tab id or a row index has nothing
// to resolve it against from a palette.
constexpr auto kCatalog = std::to_array<CatalogRow>({
    // sidebarNewTab is deliberately absent even though it fits: Cmd+T is
    // always the palette now, so a New Tab verb here would reopen the palette.
    // Cmd+, is IDC_OPTIONS's stock combo in accelerators_cocoa.mm, not a
    // NativeHotkeyAction - the Settings menu item is retargeted onto
    // showPreferences: (atrium_settings_menu_hook.mm) but keeps that key.
    {"openSettingsPane", "", "Settings", "Atrium", std::nullopt,
     "settings|preferences", "cmd+,"},
    // No hotkey: Arc, Chrome and Safari bind none for import.
    {"openSettingsPane", "import", "Import from Browser", "Atrium",
     std::nullopt, "import|import from browser|import bookmarks|arc"},
    {"sidebarCourtyardOpened", std::nullopt, "Courtyard", "Atrium",
     std::nullopt, "courtyard|spaces"},
    {"sidebarOverviewToggled", std::nullopt, "Overview", "Atrium",
     NativeHotkeyAction::kToggleOverview, "overview"},
    {"sidebarSpaceCreated", std::nullopt, "New Space", "Atrium", std::nullopt,
     "new space|space"},
    {"sidebarToggleRequested", std::nullopt, "Toggle Sidebar", "Atrium",
     NativeHotkeyAction::kToggleSidebar,
     "toggle sidebar|sidebar|hide sidebar|show sidebar"},
    {"sidebarReloadPressed", std::nullopt, "Reload", "Atrium",
     NativeHotkeyAction::kReload, "reload|refresh"},
    {"sidebarReloadHard", std::nullopt, "Hard Reload", "Atrium",
     NativeHotkeyAction::kHardReload,
     "hard reload|force reload|bypass cache|ignore cache"},
    {"sidebarReloadClearingCache", std::nullopt, "Empty Cache and Reload",
     "Atrium", NativeHotkeyAction::kReloadClearingCache,
     "empty cache and reload|empty cache|clear cache"},
    {"sidebarReloadClearingSite", std::nullopt, "Clear Site Data and Reload",
     "Atrium", NativeHotkeyAction::kReloadClearingSiteData,
     "clear site data and reload|clear site data|clear cookies"},
    {"aiPanelToggleRequested", std::nullopt, "Toggle AI panel", "Atrium",
     NativeHotkeyAction::kToggleAiPanel, "ai panel|toggle ai panel|ai|assistant"},
    // No hotkey: Arc, Chrome and Safari bind none for an extension side panel.
    {"extensionSidePanelToggleRequested", std::nullopt, "Toggle side panel",
     "Atrium", std::nullopt, "side panel|toggle side panel|extension panel"},
    {"captureViewport", std::nullopt, "Capture Viewport", "Screenshot",
     std::nullopt, "capture viewport|screenshot|shot|grab|snap|visible"},
    {"captureRegion", std::nullopt, "Capture Region", "Screenshot",
     std::nullopt,
     "capture region|screenshot|shot|grab|snap|crop|area|select"},
    {"captureFullPage", std::nullopt, "Capture Full Page", "Screenshot",
     std::nullopt,
     "capture full page|screenshot|shot|grab|snap|whole|full"},
});

constexpr bool EveryRowNamesAMessage() {
  for (const CatalogRow& row : kCatalog) {
    if (row.message.empty()) {
      return false;
    }
  }
  return true;
}

static_assert(EveryRowNamesAMessage(),
              "a palette row with no message reads live and does nothing");

// The glyph macOS draws for a key the combo grammar names rather than spells.
std::string_view NamedKeyGlyph(std::string_view key) {
  static constexpr std::pair<std::string_view, std::string_view> kGlyphs[] = {
      {"tab", "⇥"},   {"escape", "⎋"}, {"space", "␣"},
      {"left", "←"},  {"right", "→"},  {"up", "↑"},
      {"down", "↓"},
  };
  for (const auto& [name, glyph] : kGlyphs) {
    if (key == name) {
      return glyph;
    }
  }
  return std::string_view();
}

std::string BindingFor(
    NativeHotkeyAction action,
    const std::map<std::string, std::vector<std::string>>& bindings) {
  const NativeHotkeyDefault& entry = NativeHotkeyDefaultFor(action);
  const auto it = bindings.find(std::string(entry.action_id));
  if (it == bindings.end()) {
    return CommandBarShortcutGlyphs(entry.combo);
  }
  // The first of several, the way the Inputs pane reads them left to right: a
  // chip has room for one, and the first is the one the store lists first.
  return it->second.empty() ? std::string()
                            : CommandBarShortcutGlyphs(it->second.front());
}

}  // namespace

CommandBarAction::CommandBarAction() = default;
CommandBarAction::CommandBarAction(const CommandBarAction&) = default;
CommandBarAction& CommandBarAction::operator=(const CommandBarAction&) =
    default;
CommandBarAction::CommandBarAction(CommandBarAction&&) = default;
CommandBarAction& CommandBarAction::operator=(CommandBarAction&&) = default;
CommandBarAction::~CommandBarAction() = default;

CommandBarActionOmission CommandBarActionOmissionFor(
    NativeHotkeyAction action) {
  switch (action) {
    case NativeHotkeyAction::kToggleSidebar:
    case NativeHotkeyAction::kToggleOverview:
    case NativeHotkeyAction::kReload:
    case NativeHotkeyAction::kHardReload:
    case NativeHotkeyAction::kReloadClearingCache:
    case NativeHotkeyAction::kReloadClearingSiteData:
    case NativeHotkeyAction::kToggleAiPanel:
      return CommandBarActionOmission::kNone;
    case NativeHotkeyAction::kOpenCommandBar:
    case NativeHotkeyAction::kEditCurrentURL:
      return CommandBarActionOmission::kOpensTheCommandBar;
    case NativeHotkeyAction::kNextTab:
    case NativeHotkeyAction::kPreviousTab:
    case NativeHotkeyAction::kNextSpace:
    case NativeHotkeyAction::kPreviousSpace:
    case NativeHotkeyAction::kGoToSidebarItem1:
    case NativeHotkeyAction::kGoToSidebarItem2:
    case NativeHotkeyAction::kGoToSidebarItem3:
    case NativeHotkeyAction::kGoToSidebarItem4:
    case NativeHotkeyAction::kGoToSidebarItem5:
    case NativeHotkeyAction::kGoToSidebarItem6:
    case NativeHotkeyAction::kGoToSidebarItem7:
    case NativeHotkeyAction::kGoToSidebarItem8:
    case NativeHotkeyAction::kGoToSidebarItemLast:
    case NativeHotkeyAction::kGoToSpace1:
    case NativeHotkeyAction::kGoToSpace2:
    case NativeHotkeyAction::kGoToSpace3:
    case NativeHotkeyAction::kGoToSpace4:
    case NativeHotkeyAction::kGoToSpace5:
    case NativeHotkeyAction::kGoToSpace6:
    case NativeHotkeyAction::kGoToSpace7:
    case NativeHotkeyAction::kGoToSpace8:
    case NativeHotkeyAction::kGoToSpace9:
      return CommandBarActionOmission::kNavigatesRowsThePaletteLists;
    // A little window, the dev bar and the pin have no no-argument message
    // yet. Giving one a row means adding that message, not editing this arm.
    case NativeHotkeyAction::kLittleWindow:
    case NativeHotkeyAction::kToggleDevBar:
    case NativeHotkeyAction::kPinToggleTab:
    case NativeHotkeyAction::kCreateSpace:
    case NativeHotkeyAction::kOpenArchive:
    case NativeHotkeyAction::kCloseSplitPane:
    case NativeHotkeyAction::kToggleSplitOrientation:
    case NativeHotkeyAction::kToggleDeveloperMode:
    case NativeHotkeyAction::kClearTabs:
      return CommandBarActionOmission::kNoPaletteMessage;
  }
  NOTREACHED();
}

std::string CommandBarShortcutGlyphs(std::string_view combo) {
  bool command = false;
  bool shift = false;
  bool option = false;
  bool control = false;
  std::string key;
  for (std::string_view token : base::SplitStringPiece(
           combo, "+", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY)) {
    const std::string lowered = base::ToLowerASCII(token);
    if (lowered == "cmd") {
      command = true;
    } else if (lowered == "shift") {
      shift = true;
    } else if (lowered == "opt") {
      option = true;
    } else if (lowered == "ctrl") {
      control = true;
    } else {
      key = lowered;
    }
  }
  if (key.empty()) {
    return std::string();
  }
  std::string out;
  if (control) {
    out += "⌃";
  }
  if (option) {
    out += "⌥";
  }
  if (shift) {
    out += "⇧";
  }
  if (command) {
    out += "⌘";
  }
  const std::string_view named = NamedKeyGlyph(key);
  out += named.empty() ? base::ToUpperASCII(key) : std::string(named);
  return out;
}

std::vector<CommandBarAction> CommandBarActionCatalog(
    const std::map<std::string, std::vector<std::string>>& bindings) {
  std::vector<CommandBarAction> catalog;
  for (const CatalogRow& row : kCatalog) {
    CommandBarAction action;
    action.title = std::string(row.title);
    action.subtitle = std::string(row.subtitle);
    action.message = std::string(row.message);
    if (row.arg.has_value()) {
      action.args.emplace_back(*row.arg);
    }
    for (std::string_view alias :
         base::SplitStringPiece(row.aliases, "|", base::TRIM_WHITESPACE,
                                base::SPLIT_WANT_NONEMPTY)) {
      action.aliases.emplace_back(alias);
    }
    action.hotkey_action = row.action;
    if (row.action.has_value()) {
      action.binding = BindingFor(*row.action, bindings);
    } else if (row.fixed_combo.has_value()) {
      action.binding = CommandBarShortcutGlyphs(*row.fixed_combo);
    }
    catalog.push_back(std::move(action));
  }
  return catalog;
}

std::string BuildCommandBarActionsJSON(
    const std::vector<CommandBarAction>& actions) {
  base::ListValue list;
  for (const CommandBarAction& action : actions) {
    base::ListValue aliases;
    for (const std::string& alias : action.aliases) {
      aliases.Append(alias);
    }
    base::ListValue args;
    for (const std::string& arg : action.args) {
      args.Append(arg);
    }
    base::DictValue entry;
    entry.Set("kind", "action");
    entry.Set("title", action.title);
    entry.Set("subtitle", action.subtitle);
    // "send" rather than "message": it is the key the page's accept() reads.
    entry.Set("send", action.message);
    entry.Set("args", std::move(args));
    entry.Set("aliases", std::move(aliases));
    entry.Set("binding", action.binding);
    list.Append(std::move(entry));
  }
  base::DictValue payload;
  payload.Set("actions", std::move(list));
  std::string json;
  base::JSONWriter::Write(payload, &json);
  return json;
}

}  // namespace atrium
