// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_native_hotkey_map.mm
#include "chrome/browser/ui/atrium/atrium_native_hotkey_map.h"

#import <Cocoa/Cocoa.h>

#include <algorithm>

#include "base/json/json_reader.h"
#include "base/logging.h"
#include "base/no_destructor.h"
#include "base/strings/string_split.h"
#include "base/strings/string_util.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_favorite_slot.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_defaults.h"

namespace atrium {

namespace {

// Virtual key codes for the named keys the combo grammar allows (Carbon
// kVK_* values, spelled out so this file needs no Carbon include). Must stay
// in sync with KeyCombo's namedKeys on the Swift side.
constexpr int kVKTab = 0x30;
constexpr int kVKSpace = 0x31;
constexpr int kVKEscape = 0x35;
constexpr int kVKLeftArrow = 0x7B;
constexpr int kVKRightArrow = 0x7C;
constexpr int kVKDownArrow = 0x7D;
constexpr int kVKUpArrow = 0x7E;

int KeyCodeForNamedKey(std::string_view name) {
  if (name == "tab") {
    return kVKTab;
  }
  if (name == "space") {
    return kVKSpace;
  }
  if (name == "escape") {
    return kVKEscape;
  }
  if (name == "left") {
    return kVKLeftArrow;
  }
  if (name == "right") {
    return kVKRightArrow;
  }
  if (name == "down") {
    return kVKDownArrow;
  }
  if (name == "up") {
    return kVKUpArrow;
  }
  return -1;
}

// The stock combo each action's IDC intercept keys off - see
// IsDefaultComboBound's doc in the header.
std::string_view DefaultComboFor(NativeHotkeyAction action) {
  return NativeHotkeyDefaultFor(action).combo;
}

}  // namespace

// static
NativeHotkeyMap* NativeHotkeyMap::Get() {
  static base::NoDestructor<NativeHotkeyMap> instance;
  return instance.get();
}

NativeHotkeyMap::~NativeHotkeyMap() = default;

NativeHotkeyMap::Combo::Combo() = default;
NativeHotkeyMap::Combo::Combo(const Combo&) = default;
NativeHotkeyMap::Combo& NativeHotkeyMap::Combo::operator=(const Combo&) =
    default;
NativeHotkeyMap::Combo::~Combo() = default;

NativeHotkeyMap::Binding::Binding() = default;
NativeHotkeyMap::Binding::Binding(const Binding&) = default;
NativeHotkeyMap::Binding& NativeHotkeyMap::Binding::operator=(
    const Binding&) = default;
NativeHotkeyMap::Binding::~Binding() = default;

NativeHotkeyMap::NativeHotkeyMap() {
  ResetToDefaults();
}

void NativeHotkeyMap::ResetToDefaults() {
  bindings_.clear();
  for (const NativeHotkeyDefault& entry : NativeHotkeyDefaults()) {
    Binding binding;
    binding.action = entry.action;
    // Empty is a real row (goToSpace1..9): it ships with nothing to
    // bind, not a combo this grammar rejects.
    if (entry.combo.empty()) {
      binding.default_combo_bound = false;
      bindings_.push_back(std::move(binding));
      continue;
    }
    // CHECKed rather than dereferenced blind. Nothing outside this file runs
    // ParseCombo - the map is Objective-C++ in //chrome/browser/ui, which no
    // unit-test target links - so a compiled-in combo this grammar rejects
    // (an unknown named key, or the reserved Ctrl+Tab) would otherwise be
    // undefined behaviour on the first NativeHotkeyMap::Get().
    std::optional<Combo> combo = ParseCombo(entry.combo);
    CHECK(combo) << "default combo '" << entry.combo << "' for '"
                 << entry.action_id << "' is not one this map can bind";
    binding.combos = {*std::move(combo)};
    binding.combo_strings = {std::string(entry.combo)};
    binding.default_combo_bound = true;
    bindings_.push_back(std::move(binding));
  }
}

void NativeHotkeyMap::ResetForTesting() {
  ResetToDefaults();
}

// static
std::optional<NativeHotkeyMap::ParsedCombo>
NativeHotkeyMap::ParseComboForTesting(std::string_view combo) {
  const std::optional<Combo> parsed = ParseCombo(combo);
  if (!parsed) {
    return std::nullopt;
  }
  return ParsedCombo{.modifiers = parsed->modifiers,
                     .character = parsed->character,
                     .key_code = parsed->key_code};
}

NativeHotkeyMap::Binding& NativeHotkeyMap::binding_for(
    NativeHotkeyAction action) {
  auto it = std::find_if(
      bindings_.begin(), bindings_.end(),
      [action](const Binding& binding) { return binding.action == action; });
  CHECK(it != bindings_.end());
  return *it;
}

const NativeHotkeyMap::Binding& NativeHotkeyMap::binding_for(
    NativeHotkeyAction action) const {
  return const_cast<NativeHotkeyMap*>(this)->binding_for(action);
}

// static
std::optional<NativeHotkeyMap::Combo> NativeHotkeyMap::ParseCombo(
    std::string_view raw) {
  const std::vector<std::string> segments = base::SplitString(
      base::ToLowerASCII(raw), "+", base::TRIM_WHITESPACE,
      base::SPLIT_WANT_NONEMPTY);
  if (segments.empty()) {
    return std::nullopt;
  }
  Combo combo;
  const std::string& key = segments.back();
  for (size_t i = 0; i + 1 < segments.size(); ++i) {
    const std::string& modifier = segments[i];
    if (modifier == "cmd") {
      combo.modifiers |= NSEventModifierFlagCommand;
    } else if (modifier == "shift") {
      combo.modifiers |= NSEventModifierFlagShift;
    } else if (modifier == "opt") {
      combo.modifiers |= NSEventModifierFlagOption;
    } else if (modifier == "ctrl") {
      combo.modifiers |= NSEventModifierFlagControl;
    } else {
      return std::nullopt;
    }
  }
  // Without cmd or ctrl, only a modified tab or escape - what Opt+Tab needs
  // and no more. ComboEditsText gates arrows only when cmd is held, so
  // admitting opt+arrow or shift+arrow would eat a word move or a selection.
  const bool has_cmd_or_ctrl =
      (combo.modifiers &
       (NSEventModifierFlagCommand | NSEventModifierFlagControl)) != 0;
  if (!has_cmd_or_ctrl &&
      (combo.modifiers == 0 || (key != "tab" && key != "escape"))) {
    return std::nullopt;
  }
  // Checked on the raw key, before the single-character shortcut below: a
  // letter chord like cmd+shift+w never reaches the named-key path where the
  // Ctrl+Tab rule used to live alone.
  if (atrium::IsReservedHotkeyCombo(
          key, (combo.modifiers & NSEventModifierFlagCommand) != 0,
          (combo.modifiers & NSEventModifierFlagShift) != 0,
          (combo.modifiers & NSEventModifierFlagOption) != 0,
          (combo.modifiers & NSEventModifierFlagControl) != 0)) {
    return std::nullopt;
  }
  if (key.size() == 1) {
    combo.character = key;
    return combo;
  }
  combo.key_code = KeyCodeForNamedKey(key);
  if (combo.key_code < 0) {
    return std::nullopt;
  }
  return combo;
}

void NativeHotkeyMap::SetFromJSON(std::string_view json) {
  CHECK([NSThread isMainThread]);
  std::optional<base::DictValue> root =
      base::JSONReader::ReadDict(json, base::JSON_PARSE_RFC);
  if (!root) {
    LOG(WARNING) << "Atrium: set_native_hotkeys payload is not a JSON object;"
                 << " keeping current bindings";
    return;
  }
  // The core's schema::can_load rule, not an equality: older is readable,
  // newer is not. Equality against a stale constant is what kept this map on
  // its compiled-in bindings after the store went to 2 - every payload was
  // refused wholesale, and refusing in one line is indistinguishable in the
  // log from a file that really is from a later build.
  const int version = root->FindInt("schemaVersion").value_or(-1);
  if (version < 1 || version > kNativeHotkeySchemaVersion) {
    LOG(WARNING) << "Atrium: native hotkeys payload is at schemaVersion "
                 << version << ", which this build does not read; keeping"
                 << " current bindings";
    return;
  }
  const base::DictValue* actions = root->FindDict("actions");
  if (!actions) {
    LOG(WARNING) << "Atrium: set_native_hotkeys payload has no actions dict;"
                 << " keeping current bindings";
    return;
  }
  for (const auto [id, value] : *actions) {
    const std::optional<NativeHotkeyAction> action = NativeHotkeyActionForId(id);
    if (!action) {
      // VLOG, not WARNING: the store carries every action that exists and this
      // map owns eight of them, so the whole hotkeys file names two dozen ids
      // there is deliberately no C++ handler for. Warning on each of them once
      // a launch would bury the ones that are really a typo.
      VLOG(1) << "Atrium: native hotkey action '" << id
              << "' is not one this map runs; skipped";
      continue;
    }
    const base::ListValue* raw_combos = value.GetIfList();
    if (!raw_combos) {
      LOG(WARNING) << "Atrium: native hotkey action '" << id
                   << "' is not a combo list; skipped";
      continue;
    }
    // An action key that IS present replaces that action's bindings
    // entirely - an empty list is an explicit unbind. Absent actions keep
    // whatever they had (the built-in default on the first push).
    std::vector<Combo> combos;
    std::vector<std::string> combo_strings;
    bool default_bound = false;
    // Empty for goToSpace1..9, which ship with no default to keep
    // bound - nothing here can ever match it.
    const std::string_view default_combo_str = DefaultComboFor(*action);
    const std::optional<Combo> default_combo =
        default_combo_str.empty() ? std::nullopt
                                  : ParseCombo(default_combo_str);
    for (const base::Value& raw : *raw_combos) {
      const std::string* serialized = raw.GetIfString();
      std::optional<Combo> combo =
          serialized ? ParseCombo(*serialized) : std::nullopt;
      if (!combo) {
        LOG(WARNING) << "Atrium: unparseable native hotkey combo for '" << id
                     << "' skipped";
        continue;
      }
      if (default_combo && *combo == *default_combo) {
        default_bound = true;
      }
      combos.push_back(std::move(*combo));
      combo_strings.push_back(*serialized);
    }
    Binding& binding = binding_for(*action);
    binding.combos = std::move(combos);
    binding.combo_strings = std::move(combo_strings);
    binding.default_combo_bound = default_bound;
    VLOG(1) << "Atrium: native hotkey action '" << id << "' bound to "
            << binding.combos.size() << " combo(s), stock combo "
            << (default_bound ? "kept" : "released");
  }
}

bool NativeHotkeyMap::IsDefaultComboBound(NativeHotkeyAction action) const {
  return binding_for(action).default_combo_bound;
}

std::vector<std::string> NativeHotkeyMap::CombosFor(
    NativeHotkeyAction action) const {
  return binding_for(action).combo_strings;
}

std::optional<NativeHotkeyAction> NativeHotkeyMap::MatchAction(
    NSEvent* event) const {
  if (!event ||
      (event.type != NSEventTypeKeyDown && event.type != NSEventTypeKeyUp)) {
    return std::nullopt;
  }
  const unsigned long long event_modifiers =
      event.modifierFlags &
      (NSEventModifierFlagCommand | NSEventModifierFlagShift |
       NSEventModifierFlagOption | NSEventModifierFlagControl);
  const std::string characters =
      base::ToLowerASCII([event.charactersIgnoringModifiers UTF8String] ?: "");
  for (const Binding& binding : bindings_) {
    for (const Combo& combo : binding.combos) {
      if (combo.modifiers != event_modifiers) {
        continue;
      }
      // The unshifted form is tried too: charactersIgnoringModifiers keeps
      // shift, so cmd+shift+] arrives as "}" while the store spells it "]"
      //.
      const bool key_matches =
          combo.key_code >= 0
              ? event.keyCode == combo.key_code
              : (!combo.character.empty() &&
                 (characters == combo.character ||
                  UnshiftedGlyph(characters) == combo.character));
      if (key_matches) {
        return binding.action;
      }
    }
  }
  return std::nullopt;
}

namespace {

// SelectSpace's result is dropped on purpose - see the kGoToSpace arms.
bool SelectSpaceOrSwallow(BrowserController* controller, int index) {
  controller->SelectSpace(index);
  return true;
}

}  // namespace

bool ExecuteNativeHotkeyAction(BrowserController* controller,
                               NativeHotkeyAction action) {
  if (!controller) {
    return false;
  }
  switch (action) {
    case NativeHotkeyAction::kOpenCommandBar:
      // no gate. The palette used to exist only when the dylib
      // exported the command-bar group; it is an HTML surface the browser
      // builds itself now, so it is always there to toggle. The about:blank
      // fallback deliberately does NOT live here: it is IDC_NEW_TAB-specific
      // semantics ("Cmd+T must never reach the chrome://newtab handler, which
      // FATALs"), so it stays in the intercept.
      controller->ToggleCommandBar();
      return true;
    case NativeHotkeyAction::kToggleSidebar:
      // the toggle lived in the Swift model and now lives here.
      // Declining rather than swallowing is what keeps Cmd+S on native Save
      // Page when there is no sidebar to act on, and
      // ClaimsSavePageForSidebar reads the same answer at the other dispatch
      // level so the two cannot disagree.
      return controller->ToggleSidebar();
    case NativeHotkeyAction::kNextTab:
      // Axis 1: the SIDEBAR's order, not the strip's. The Rust core
      // owns that order now, and BuildSidebarTabsJSON is where it surfaces.
      return controller->CycleTabInSidebarOrder(1);
    case NativeHotkeyAction::kPreviousTab:
      return controller->CycleTabInSidebarOrder(-1);
    case NativeHotkeyAction::kEditCurrentURL:
      // NOT a toggle - Cmd+L on an already-open palette re-opens
      // it with the edit-URL state rather than dismissing it, which is what
      // the stock "focus the location bar" key means everywhere else.
      // Ungated, like kOpenCommandBar above.
      controller->OpenCommandBarWithReason(
          CommandBarOpenReason::kEditCurrentURL);
      return true;
    case NativeHotkeyAction::kNextSpace:
      // Built against
      // the Rust core, which owns the spaces document.
      //
      // Cmd+Shift+Right is FORWARD - the space after the active one in rail
      // order - which is what space-cycle-edit-focus-gate has always
      // asserted for this chord. The HTML swipe reads a rightward scrub as
      // backward and always has; that
      // disagreement is not settled here.
      //
      // Unguarded on purpose: the edit-focus gate has already run for this
      // event in MayFireHotkeyForEvent, and cmd+shift+arrow is a text-editing
      // chord, so a focused field keeps the key and this is never reached.
      return controller->CycleSpace(1);
    case NativeHotkeyAction::kLittleWindow: {
      // The binding that finally gives CreateLittleWindow a
      // caller. Stage 1 shipped the whole little-window path with no call site
      // anywhere, so none of it was reachable by a user.
      //
      // Opens on the INVOKING controller's profile, which is the profile whose
      // window the user pressed the key in. Deliberately not the visible one:
      // a little window is a throwaway scratch surface rather than an action
      // on the page in front of you, and CreateWindowSlot's visible-profile
      // resolution answers a different question.
      //
      // No availability gate. Unlike the space and command-bar actions this
      // needs nothing from the dylib to open the window - a missing Swift
      // chrome group leaves the strip unattached, which AttachLittleChrome
      // already null-guards, rather than leaving a half-built window.
      controller->OpenLittleWindow();
      return true;
    }
    case NativeHotkeyAction::kPreviousSpace:
      // Same as kNextSpace, backward.
      return controller->CycleSpace(-1);
    case NativeHotkeyAction::kToggleOverview:
      controller->ToggleOverview();
      return true;
    case NativeHotkeyAction::kToggleDevBar:
      return controller->ToggleDevBar();
    case NativeHotkeyAction::kPinToggleTab:
      // No IDC intercept beside this one, unlike Cmd+S and Cmd+T: this route
      // runs ahead of the dispatcher's IDC lookup, so it takes Cmd+D before
      // IDC_BOOKMARK_THIS_TAB can, and declining hands the key back.
      return controller->TogglePinShownTab();
    // Cmd+Opt+1..9. Same posture as Cmd+D above: nothing stock owns these, and
    // a space with no tile in that slot declines and hands the key back.
    case NativeHotkeyAction::kGoToSidebarItem1:
      return controller->ActivateFavoriteInSlot(1);
    case NativeHotkeyAction::kGoToSidebarItem2:
      return controller->ActivateFavoriteInSlot(2);
    case NativeHotkeyAction::kGoToSidebarItem3:
      return controller->ActivateFavoriteInSlot(3);
    case NativeHotkeyAction::kGoToSidebarItem4:
      return controller->ActivateFavoriteInSlot(4);
    case NativeHotkeyAction::kGoToSidebarItem5:
      return controller->ActivateFavoriteInSlot(5);
    case NativeHotkeyAction::kGoToSidebarItem6:
      return controller->ActivateFavoriteInSlot(6);
    case NativeHotkeyAction::kGoToSidebarItem7:
      return controller->ActivateFavoriteInSlot(7);
    case NativeHotkeyAction::kGoToSidebarItem8:
      return controller->ActivateFavoriteInSlot(8);
    case NativeHotkeyAction::kGoToSidebarItemLast:
      return controller->ActivateFavoriteInSlot(kLastFavorite);
    // Consumed even when the space is missing, unlike the arms above: handing
    // Cmd+3 back on a two-space profile returns it to IDC_SELECT_TAB_3, which
    // picks a tab of the shared strip and moves the window to that tab's space.
    case NativeHotkeyAction::kGoToSpace1:
      return SelectSpaceOrSwallow(controller, 0);
    case NativeHotkeyAction::kGoToSpace2:
      return SelectSpaceOrSwallow(controller, 1);
    case NativeHotkeyAction::kGoToSpace3:
      return SelectSpaceOrSwallow(controller, 2);
    case NativeHotkeyAction::kGoToSpace4:
      return SelectSpaceOrSwallow(controller, 3);
    case NativeHotkeyAction::kGoToSpace5:
      return SelectSpaceOrSwallow(controller, 4);
    case NativeHotkeyAction::kGoToSpace6:
      return SelectSpaceOrSwallow(controller, 5);
    case NativeHotkeyAction::kGoToSpace7:
      return SelectSpaceOrSwallow(controller, 6);
    case NativeHotkeyAction::kGoToSpace8:
      return SelectSpaceOrSwallow(controller, 7);
    // The NINTH space, not the last one. The core's id says goToSpace9 and
    // has no goToSpaceLast to mean otherwise, so retargeting it would make
    // the Inputs pane's own label wrong.
    case NativeHotkeyAction::kGoToSpace9:
      return SelectSpaceOrSwallow(controller, 8);
    case NativeHotkeyAction::kCreateSpace:
      return controller->CreateSpace();
    case NativeHotkeyAction::kOpenArchive:
      controller->OpenArchive();
      return true;
    case NativeHotkeyAction::kCloseSplitPane:
      return controller->CloseSplitPane();
    case NativeHotkeyAction::kToggleSplitOrientation:
      return controller->ToggleSplitOrientation();
    case NativeHotkeyAction::kToggleDeveloperMode:
      return controller->ToggleDeveloperMode();
    // Cmd+R and Cmd+Shift+R were already live as main-menu accelerators, which
    // act on the STRIP's active tab - not the one an Atrium window displays.
    case NativeHotkeyAction::kReload:
      return controller->ReloadShownTab();
    case NativeHotkeyAction::kHardReload:
      return controller->HardReloadShownTab();
    case NativeHotkeyAction::kReloadClearingCache:
      return controller->ReloadShownTabClearingCache();
    case NativeHotkeyAction::kReloadClearingSiteData:
      return controller->ReloadShownTabClearingSiteData();
    case NativeHotkeyAction::kClearTabs:
      return controller->ClearTabs();
    case NativeHotkeyAction::kToggleAiPanel:
      controller->ToggleAiPanel();
      return true;
  }
}

}  // namespace atrium
