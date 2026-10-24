// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_native_hotkey_defaults.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_NATIVE_HOTKEY_DEFAULTS_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_NATIVE_HOTKEY_DEFAULTS_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

#include "base/containers/span.h"

namespace atrium {

// The hotkeys schema version this build reads, mirroring the core's
// schema::current::HOTKEYS.
//
// Mirrored rather than shared because the core exports no C constant for it.
// The drift is asserted instead: a payload the core emits carries its own
// current version, and a map that refused it would keep the compiled-in
// bindings on every launch while the Inputs pane showed the file's. That is
// exactly what happened while this said 1 and the core said 2.
inline constexpr int kNativeHotkeySchemaVersion = 2;

// The C++-owned actions a key can trigger (todo 17 follow-up) - see
// the retired ABI header's NATIVE HOTKEYS doc block for the whole design.
// Adding a future action (closeTab, ...) is a new case here, a row in the
// table below, and one ExecuteNativeHotkeyAction branch.
//
// Here rather than beside NativeHotkeyMap so this file stays Cocoa-free: the
// map is Objective-C++ and lives in //chrome/browser/ui, which no unit-test
// target links, and the whole point of this one is to be assertable.
//
// kNextTab/kPreviousTab (Arc-parity tab-switching):
// default combos cmd+opt+down/cmd+opt+up, driving AtriumOnTabCycle axis 1
// (sidebar order) - see the retired ABI header's TAB CYCLING doc block. Ordinary
// rebindable native actions, unlike Ctrl-Tab's axis-0 chord, which is
// hardcoded in AtriumCommandDispatcherDelegate and never reaches this map.
//
// kEditCurrentURL: default combo cmd+l, opening the palette
// with the active tab's URL prefilled and selected - the classic Cmd+L
// flow. Matched in the raw NSEvent hook, which runs BEFORE Chrome's IDC
// accelerator lookup, so it wins over stock IDC_FOCUS_LOCATION; rebinding it
// away lets Cmd+L fall through to that stock meaning with no other change.
//
// kNextSpace/kPreviousSpace: default combos cmd+shift+right/left,
// driving AtriumCycleSpace. These were SwiftUI `keyboardShortcut`s until the
// sidebar became HTML: a WebContents then owned the keyboard and swallowed
// the chord before SwiftUI saw it, so space cycling silently stopped working
// on the default surface. SidebarView's own comment predicted it - "full
// app-level shortcut routing, if needed, is a C++/NSResponder concern like
// M1's Cmd+W special-case, not a SwiftUI one". Matched in the raw NSEvent
// hook, which runs ahead of both, so no surface can swallow them. The SwiftUI
// buttons stay as the fallback for when this route declines.
// kLittleWindow: default combo cmd+opt+n, opening a
// throwaway single-tab window with no sidebar and no space rows. Stage 1 built
// CreateLittleWindow and left it with NO CALL SITE - the whole feature was
// reachable only from a future caller - so this is the binding that makes it
// exist for a user. Nothing stock is bound to cmd+opt+n, so there is no IDC
// intercept to key off and the value is purely the default binding.
//
// kToggleOverview: id/combo come from the core's
// default_hotkeys(), which already reserved cmd+shift+o - not cmd+e.
//
// kToggleDevBar: default combo cmd+shift+d. Was a hardcoded
// NSEvent intercept in AtriumCommandDispatcherDelegate before this row -
// distinct from the core's unrelated toggleDeveloperMode (opt+ctrl+d).
//
// kPinToggleTab: default combo cmd+d. The core has shipped this id
// and combo since the store was written, so the Inputs pane listed the
// shortcut while no row here ran it and the key reached Bookmark This Tab.
// kGoToSidebarItem1..8 and kGoToSidebarItemLast: the favourite tiles, the
// ninth being the LAST tile. On cmd+opt+1..9 since the digits went to spaces.
//
// kGoToSpace1..9: cmd+1..9, and the ONLY arms that swallow their chord when
// the action does nothing. A digit handed back reaches stock select-tab-N,
// which picks a tab of the shared strip and moves the window to that tab's
// space, so a missing space would still look like a space switch.
//
// kCreateSpace: no default combo, nothing has asked for one.
// kToggleDeveloperMode: the extensions Developer Mode pref, distinct from
// kToggleDevBar's URL toolbar.
enum class NativeHotkeyAction {
  kOpenCommandBar,
  kToggleSidebar,
  kNextTab,
  kPreviousTab,
  kEditCurrentURL,
  kNextSpace,
  kPreviousSpace,
  kLittleWindow,
  kToggleOverview,
  kToggleDevBar,
  kPinToggleTab,
  kGoToSidebarItem1,
  kGoToSidebarItem2,
  kGoToSidebarItem3,
  kGoToSidebarItem4,
  kGoToSidebarItem5,
  kGoToSidebarItem6,
  kGoToSidebarItem7,
  kGoToSidebarItem8,
  kGoToSidebarItemLast,
  kGoToSpace1,
  kGoToSpace2,
  kGoToSpace3,
  kGoToSpace4,
  kGoToSpace5,
  kGoToSpace6,
  kGoToSpace7,
  kGoToSpace8,
  kGoToSpace9,
  kCreateSpace,
  kOpenArchive,
  kCloseSplitPane,
  kToggleSplitOrientation,
  kToggleDeveloperMode,
  kReload,
  kHardReload,
  kReloadClearingCache,
  kReloadClearingSiteData,
  // Arc's Clear Unpinned Tabs chord, the sidebar's Clear icon by key.
  kClearTabs,
  // Opens or closes the AI panel. No Arc equivalent, so it ships unassigned
  // in the Arc preset.
  kToggleAiPanel,
  kMaxValue = kToggleAiPanel,
};

// How many actions the table below carries. A constant rather than a call on
// the span, so the enum can be static_asserted against it: an action added to
// one and not the other stops the build.
inline constexpr size_t kNativeHotkeyActionCount = 40;

// One native action: the action itself, the id hotkeys.json spells it with,
// and the combo it is bound to before that file is read.
struct NativeHotkeyDefault {
  NativeHotkeyAction action;
  std::string_view action_id;
  std::string_view combo;
};

// Every action NativeHotkeyMap owns, one row each. Each row names its action,
// so nothing here depends on the order the rows are written in.
//
// Two things live in one column on purpose. `combo` is both the binding in
// force until hotkeys.json is read AND the STOCK combo the IDC intercepts key
// off (see NativeHotkeyMap::IsDefaultComboBound): Cmd+T reaches the palette
// and Cmd+S the sidebar only while each is still among its action's bindings.
// Splitting them would let a build release Cmd+T back to the stock new-tab
// handler while still starting up bound to it.
//
// NOT the same list as the core's default_hotkeys(), and not a subset of its
// action names either - the core knows every action that exists, including the
// ones no C++ code can run. What the two must agree on is asserted in
// atrium_native_hotkey_defaults_unittest.cc.
base::span<const NativeHotkeyDefault> NativeHotkeyDefaults();

// The row for `action`. Every action has exactly one, which the unit test
// asserts over the whole enum, so this always finds it.
const NativeHotkeyDefault& NativeHotkeyDefaultFor(NativeHotkeyAction action);

// The action `action_id` names, or nullopt when this build has no handler for
// it. The hotkeys store carries every action that exists and most are not this
// map's, so nullopt is an ordinary answer rather than an error.
std::optional<NativeHotkeyAction> NativeHotkeyActionForId(
    std::string_view action_id);

// kGoToSpace1..9 for rail position 0..8, nullopt past the ninth space - there
// is no tenth action to name it with.
std::optional<NativeHotkeyAction> NativeHotkeyActionForSpaceIndex(int index);

// The glyph a key produces WITHOUT shift, given the glyph it produces with
// shift held. Empty when the two are the same, or when the glyph is unknown.
//
// AppKit's charactersIgnoringModifiers ignores command and option but NOT
// shift, so cmd+shift+] arrives as "}" while the hotkeys store spells that
// binding "]" - the comparison missed and the chord did nothing, while
// unshifted ones like cmd+k worked. Letters were never affected,
// because the match lowercases first.
//
// Here rather than beside the matcher so it can be tested without Cocoa, which
// is why this file exists at all. US layout, matching the kVK_* constants and
// the harness keycodes the rest of this code already assumes.
std::string_view UnshiftedGlyph(std::string_view shifted);

// The chords AtriumCommandDispatcherDelegate claims in its raw NSEvent hook,
// which runs before this map - an action bound to one would be shadowed for
// good, so ParseCombo refuses them. `key` is the lowercased last segment.
bool IsReservedHotkeyCombo(std::string_view key,
                           bool cmd,
                           bool shift,
                           bool opt,
                           bool ctrl);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_NATIVE_HOTKEY_DEFAULTS_H_
