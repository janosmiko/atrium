// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_native_hotkey_map.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_NATIVE_HOTKEY_MAP_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_NATIVE_HOTKEY_MAP_H_

// Included from both Objective-C++ (.mm) and plain C++ TUs - NSEvent is only
// usable as a type in the former (same split as the overlay widget header).
#if defined(__OBJC__)
#import <Cocoa/Cocoa.h>
#endif

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "base/no_destructor.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_defaults.h"

namespace atrium {

class BrowserController;

// NativeHotkeyAction and the default binding of each action live in
// atrium_native_hotkey_defaults.h, which is Cocoa-free so a unit test can hold
// that table next to the Rust core's own default_hotkeys().

// Process-wide store of the user's bindings for the native actions, fed at
// launch from hotkeys.json through the Rust core (AtriumMainExtraParts's
// SeedFromSettings). Global semantics: ONE hotkeys.json for the process.
//
// the feed is the whole point. SetFromJSON had NO caller - the
// set_native_hotkeys host op it was written for went with the Swift cut - so
// the map sat on its compiled-in defaults for the life of the process while
// the Inputs pane drew rows from the file. A shortcut shown in settings was
// not necessarily the one that worked, and rebinding one did nothing.
//
// Holds the built-in defaults until - and unless - a payload arrives, so an
// unreadable or absent file behaves exactly like the pre-rebinding build.
// Main-thread only, like every other bridge-facing piece of state.
class NativeHotkeyMap {
 public:
  static NativeHotkeyMap* Get();

  NativeHotkeyMap(const NativeHotkeyMap&) = delete;
  NativeHotkeyMap& operator=(const NativeHotkeyMap&) = delete;
  ~NativeHotkeyMap();

  // Replaces the bindings from a hotkeys payload, in the shape
  // LoadHotkeysJSON returns. Per-entry defensive (the Swift loader's posture):
  // unknown action ids and unparseable combo strings are skipped and logged,
  // never fatal; a payload that fails to parse at all, or carries a
  // schemaVersion newer than kNativeHotkeySchemaVersion, is ignored
  // wholesale and the current map is kept. A combo without cmd or ctrl is
  // rejected here too (Swift's builder already drops them; a stale or
  // mismatched build must not be trusted alone) - performKeyEquivalent
  // delivery is only guaranteed for cmd/ctrl-modified keys.
  void SetFromJSON(std::string_view json);

  // Whether `action`'s STOCK combo (cmd+t / cmd+s) is still among its
  // bindings - the existing IDC intercepts consult this so a rebound action
  // releases its stock key back to the stock meaning (about:blank tab /
  // native Save Page). True for both actions until a push says otherwise.
  bool IsDefaultComboBound(NativeHotkeyAction action) const;

  // The combos bound to `action` right now, as hotkeys.json spells them. What
  // the key hooks are matching against, so a surface drawing a shortcut cannot
  // disagree with the key that fires it.
  std::vector<std::string> CombosFor(NativeHotkeyAction action) const;

  // Test/diagnostic seam: resets to the built-in defaults.
  void ResetForTesting();

  // What ParseCombo made of `combo`, or nullopt when it refused it - a core
  // default it refuses is dropped silently on every launch, so a test has to
  // be able to ask. `key_code` is negative for a character combo.
  struct ParsedCombo {
    unsigned long long modifiers = 0;
    std::string character;
    int key_code = -1;
  };
  static std::optional<ParsedCombo> ParseComboForTesting(
      std::string_view combo);

#if defined(__OBJC__)
  // The action bound to `event`'s combo, if any - exact match on the
  // cmd/shift/opt/ctrl modifier set (other flags ignored) plus the key
  // (lowercased charactersIgnoringModifiers for characters, keyCode for the
  // named keys), mirroring how the Swift side's SwiftUI KeyEquivalents
  // resolve so both layers agree on what a combo means.
  std::optional<NativeHotkeyAction> MatchAction(NSEvent* event) const;
#endif

 private:
  friend class base::NoDestructor<NativeHotkeyMap>;

  NativeHotkeyMap();

  // Out-of-line special members on both structs: chromium-style requires
  // them for types with complex (std::string/std::vector) members.
  struct Combo {
    Combo();
    Combo(const Combo&);
    Combo& operator=(const Combo&);
    ~Combo();

    // Bitmask over NSEventModifierFlag{Command,Shift,Option,Control} - kept
    // as a plain unsigned so this header stays ObjC-free for C++ TUs.
    unsigned long long modifiers = 0;
    // Single lowercase character ("t"), empty when `key_code` is set.
    std::string character;
    // Virtual key code for the named keys (tab/escape/space/arrows), -1
    // when `character` is set.
    int key_code = -1;

    bool operator==(const Combo& other) const = default;
  };

  struct Binding {
    Binding();
    Binding(const Binding&);
    Binding& operator=(const Binding&);
    ~Binding();

    NativeHotkeyAction action = NativeHotkeyAction::kOpenCommandBar;
    std::vector<Combo> combos;
    // The spelling each combo arrived as, filled beside `combos` so a surface
    // can show the binding without a Combo having to be un-parsed.
    std::vector<std::string> combo_strings;
    bool default_combo_bound = true;
  };

  // Parses one serialized combo ("cmd+shift+t") - nullopt for anything
  // malformed or missing cmd/ctrl. Mirrors KeyCombo.parse (HotkeySettings).
  static std::optional<Combo> ParseCombo(std::string_view raw);

  void ResetToDefaults();
  Binding& binding_for(NativeHotkeyAction action);
  const Binding& binding_for(NativeHotkeyAction action) const;

  std::vector<Binding> bindings_;
};

// Runs `action`'s existing code path for `controller`'s window - the ONE
// implementation both dispatch levels share (the raw
// AtriumCommandDispatcherDelegate hook and the legacy IDC intercepts).
// Returns false when the action's connectivity gate fails
// (command_bar_connected / spaces_connected+can_toggle_sidebar), so the
// caller can fall through to the key's stock meaning.
bool ExecuteNativeHotkeyAction(BrowserController* controller,
                               NativeHotkeyAction action);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_NATIVE_HOTKEY_MAP_H_
