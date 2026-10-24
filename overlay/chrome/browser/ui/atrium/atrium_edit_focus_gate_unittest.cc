// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_edit_focus_gate_unittest.cc
#include "chrome/browser/ui/atrium/atrium_edit_focus_gate.h"

#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

// The gate that lets a hotkey be bound to a chord macOS already uses
// for text editing. Two properties are load-bearing and everything below exists
// to pin one of them:
//   1. It fails CLOSED. Every way of not knowing (no signal, a stale snapshot,
//      an unrecognized wire value) refuses to fire, because eating a
//      "extend selection to end of line" keystroke destroys the user's edit
//      while a hotkey that declines to fire merely annoys.
//   2. Both dispatch routes get the SAME answer. A gate that only covered the
//      SwiftUI route would leave the raw-NSEvent route (which runs before the
//      renderer sees the key at all) as an open hole.

constexpr uint64_t kTab = 42;
constexpr uint64_t kOtherTab = 43;

// Both routes, so every case below can be asserted on each.
constexpr HotkeyRoute kAllRoutes[] = {HotkeyRoute::kSwiftShortcut,
                                      HotkeyRoute::kNativeKeyEquivalent};

// A fully-healthy gated ask: signal up, snapshot fresh, chord is one macOS
// edits text with. Individual tests spoil exactly one field.
EditFocusGateInputs HealthyGatedInputs(HotkeyRoute route) {
  EditFocusGateInputs inputs;
  inputs.route = route;
  inputs.signal_connected = true;
  inputs.kind = EditFocusKind::kNone;
  inputs.snapshot_tab_id = kTab;
  inputs.active_tab_id = kTab;
  inputs.combo_edits_text = true;
  return inputs;
}

// MARK: - the gate must not touch chords that were never at risk

TEST(AtriumEditFocusGateTest, AnUngatedComboFiresEvenWithAnEditableFieldFocused) {
  for (HotkeyRoute route : kAllRoutes) {
    EditFocusGateInputs inputs = HealthyGatedInputs(route);
    inputs.combo_edits_text = false;
    inputs.kind = EditFocusKind::kPageEditable;
    EXPECT_EQ(EditFocusGateDecision::kFire, DecideEditFocusGate(inputs))
        << "cmd+t must still open the palette while the user is in a text "
           "field - it is not a text-editing chord, so it was never at risk";
  }
}

TEST(AtriumEditFocusGateTest, AnUngatedComboFiresEvenWithTheSignalUnavailable) {
  for (HotkeyRoute route : kAllRoutes) {
    EditFocusGateInputs inputs = HealthyGatedInputs(route);
    inputs.combo_edits_text = false;
    inputs.signal_connected = false;
    inputs.kind = EditFocusKind::kUnknown;
    EXPECT_EQ(EditFocusGateDecision::kFire, DecideEditFocusGate(inputs))
        << "one missing dylib symbol must not disable every hotkey the user "
           "has - only the ones a text field could plausibly want";
  }
}

// MARK: - fail closed, one axis at a time

TEST(AtriumEditFocusGateTest, AGatedComboFiresWhenNothingEditableHasFocus) {
  for (HotkeyRoute route : kAllRoutes) {
    EXPECT_EQ(EditFocusGateDecision::kFire,
              DecideEditFocusGate(HealthyGatedInputs(route)))
        << "the whole point: with a healthy signal saying no editable surface "
           "has the keyboard, the hotkey is free to fire";
  }
}

TEST(AtriumEditFocusGateTest, AGatedComboIsBlockedByAFocusedPageField) {
  for (HotkeyRoute route : kAllRoutes) {
    EditFocusGateInputs inputs = HealthyGatedInputs(route);
    inputs.kind = EditFocusKind::kPageEditable;
    EXPECT_EQ(EditFocusGateDecision::kPassPageEditable,
              DecideEditFocusGate(inputs))
        << "The exact case: cmd+shift+left in a page text field must "
           "extend the selection, not switch workspace";
  }
}

TEST(AtriumEditFocusGateTest, AGatedComboIsBlockedByAtriumsOwnTextSurface) {
  for (HotkeyRoute route : kAllRoutes) {
    EditFocusGateInputs inputs = HealthyGatedInputs(route);
    inputs.kind = EditFocusKind::kAtriumText;
    EXPECT_EQ(EditFocusGateDecision::kPassAtriumText,
              DecideEditFocusGate(inputs))
        << "the command bar query field is a text field too - C++ only learns "
           "about it because Swift pushes it over set_atrium_text_focus";
  }
}

TEST(AtriumEditFocusGateTest, AGatedComboIsBlockedWhenTheSignalNeverResolved) {
  for (HotkeyRoute route : kAllRoutes) {
    EditFocusGateInputs inputs = HealthyGatedInputs(route);
    inputs.signal_connected = false;
    EXPECT_EQ(EditFocusGateDecision::kPassSignalUnavailable,
              DecideEditFocusGate(inputs))
        << "a mismatched dylib/binary pair cannot see one half of the signal, "
           "so it must not act on the half it can see";
  }
}

TEST(AtriumEditFocusGateTest,
     AGatedComboIsBlockedWhenTheSignalIsConnectedButSaysNothingYet) {
  for (HotkeyRoute route : kAllRoutes) {
    EditFocusGateInputs inputs = HealthyGatedInputs(route);
    inputs.kind = EditFocusKind::kUnknown;
    EXPECT_EQ(EditFocusGateDecision::kPassFocusUnknown,
              DecideEditFocusGate(inputs))
        << "the group resolved but no push has landed yet - the cache is still "
           "at its zero value, which must read as 'do not fire'";
  }
}

TEST(AtriumEditFocusGateTest, AGatedComboIsBlockedByASnapshotFromAnotherTab) {
  for (HotkeyRoute route : kAllRoutes) {
    EditFocusGateInputs inputs = HealthyGatedInputs(route);
    inputs.snapshot_tab_id = kOtherTab;
    EXPECT_EQ(EditFocusGateDecision::kPassSnapshotStale,
              DecideEditFocusGate(inputs))
        << "a tab switch overtakes the push in flight: the kNone the old tab "
           "reported says nothing about the field on the new one";
  }
}

TEST(AtriumEditFocusGateTest, AStaleSnapshotIsBlockedEvenWhenItClaimsKNone) {
  for (HotkeyRoute route : kAllRoutes) {
    EditFocusGateInputs inputs = HealthyGatedInputs(route);
    inputs.kind = EditFocusKind::kNone;
    inputs.snapshot_tab_id = kTab;
    inputs.active_tab_id = kOtherTab;
    EXPECT_NE(EditFocusGateDecision::kFire, DecideEditFocusGate(inputs))
        << "kNone is only trustworthy for the tab it was observed on - the "
           "direction of the mismatch must not matter";
  }
}

// MARK: - exhaustive matrix: route x kind x connected x gated

TEST(AtriumEditFocusGateTest, ExhaustiveMatrixFiresOnlyForAProvenIdleSurface) {
  const EditFocusKind kinds[] = {
      EditFocusKind::kUnknown, EditFocusKind::kNone,
      EditFocusKind::kPageEditable, EditFocusKind::kAtriumText};
  int fired = 0;
  int considered = 0;
  for (HotkeyRoute route : kAllRoutes) {
    for (EditFocusKind kind : kinds) {
      for (bool connected : {false, true}) {
        for (bool gated : {false, true}) {
          for (bool fresh : {false, true}) {
            EditFocusGateInputs inputs;
            inputs.route = route;
            inputs.kind = kind;
            inputs.signal_connected = connected;
            inputs.combo_edits_text = gated;
            inputs.snapshot_tab_id = kTab;
            inputs.active_tab_id = fresh ? kTab : kOtherTab;
            ++considered;

            // The one and only condition under which a gated chord fires.
            const bool should_fire =
                !gated || (connected && fresh && kind == EditFocusKind::kNone);
            const EditFocusGateDecision decision = DecideEditFocusGate(inputs);
            EXPECT_EQ(should_fire, MayFireHotkey(decision))
                << "kind=" << static_cast<int32_t>(kind)
                << " connected=" << connected << " gated=" << gated
                << " fresh=" << fresh
                << " decision=" << EditFocusGateDecisionName(decision);
            if (MayFireHotkey(decision)) {
              ++fired;
            }
          }
        }
      }
    }
  }
  EXPECT_EQ(64, considered);
  // 2 routes x (16 ungated cases each, all firing) + 2 routes x 1 gated case
  // each where connected && fresh && kNone.
  EXPECT_EQ(34, fired);
}

TEST(AtriumEditFocusGateTest, BothRoutesAlwaysAgree) {
  const EditFocusKind kinds[] = {
      EditFocusKind::kUnknown, EditFocusKind::kNone,
      EditFocusKind::kPageEditable, EditFocusKind::kAtriumText};
  for (EditFocusKind kind : kinds) {
    for (bool connected : {false, true}) {
      for (bool gated : {false, true}) {
        for (bool fresh : {false, true}) {
          EditFocusGateInputs swift_inputs;
          swift_inputs.route = HotkeyRoute::kSwiftShortcut;
          swift_inputs.kind = kind;
          swift_inputs.signal_connected = connected;
          swift_inputs.combo_edits_text = gated;
          swift_inputs.snapshot_tab_id = kTab;
          swift_inputs.active_tab_id = fresh ? kTab : kOtherTab;

          EditFocusGateInputs native_inputs = swift_inputs;
          native_inputs.route = HotkeyRoute::kNativeKeyEquivalent;

          EXPECT_EQ(DecideEditFocusGate(swift_inputs),
                    DecideEditFocusGate(native_inputs))
              << "a route-dependent answer is a hole: whichever route did not "
                 "gate would still steal the keystroke";
        }
      }
    }
  }
}

// MARK: - wire decoding fails closed

TEST(AtriumEditFocusGateTest, KnownWireValuesDecode) {
  EXPECT_EQ(EditFocusKind::kUnknown, EditFocusKindFromWire(0));
  EXPECT_EQ(EditFocusKind::kNone, EditFocusKindFromWire(1));
  EXPECT_EQ(EditFocusKind::kPageEditable, EditFocusKindFromWire(2));
  EXPECT_EQ(EditFocusKind::kAtriumText, EditFocusKindFromWire(3));
}

TEST(AtriumEditFocusGateTest, UnrecognizedWireValuesDegradeToUnknownNotToNone) {
  for (int32_t wire : {-1, 4, 99, 2147483647}) {
    EXPECT_EQ(EditFocusKind::kUnknown, EditFocusKindFromWire(wire))
        << "wire=" << wire
        << ": a value this build does not understand must fail closed - a "
           "static_cast would have made 4 look like a valid kind and let the "
           "hotkey through";
  }
}

// MARK: - which chords need gating at all

TEST(AtriumEditFocusGateTest, CmdArrowAndCmdShiftArrowEditText) {
  for (int key_code :
       {kVKLeftArrow, kVKRightArrow, kVKUpArrow, kVKDownArrow}) {
    TextEditingCombo cmd_arrow;
    cmd_arrow.command = true;
    cmd_arrow.key_code = key_code;
    EXPECT_TRUE(ComboEditsText(cmd_arrow))
        << "key_code=" << key_code << ": cmd+arrow is line/document movement";

    TextEditingCombo cmd_shift_arrow = cmd_arrow;
    cmd_shift_arrow.shift = true;
    EXPECT_TRUE(ComboEditsText(cmd_shift_arrow))
        << "key_code=" << key_code
        << ": cmd+shift+arrow extends the selection";
  }
}

TEST(AtriumEditFocusGateTest, EveryShippedDefaultHotkeyChordIsLeftUngated) {
  // Every chord HotkeySettings.defaults binds today. Classifying any of these
  // would silently gate a hotkey that works fine in a text field now, which is
  // a behavior change this task must not make.
  struct Named {
    const char* label;
    TextEditingCombo combo;
  };
  std::vector<Named> defaults;
  for (const char* digit : {"1", "2", "3", "4", "5", "6", "7", "8", "9"}) {
    TextEditingCombo combo;
    combo.command = true;
    combo.character = digit;
    defaults.push_back({"cmd+digit (goToSpace)", combo});
  }
  for (const char* letter : {"t", "s", "l", "j", "k", "d"}) {
    TextEditingCombo combo;
    combo.command = true;
    combo.character = letter;
    defaults.push_back({"cmd+letter (palette/sidebar/url/tab/pin)", combo});
  }
  for (const char* bracket : {"[", "]"}) {
    TextEditingCombo combo;
    combo.command = true;
    combo.shift = true;
    combo.character = bracket;
    defaults.push_back({"cmd+shift+bracket (tab cycle)", combo});
  }
  {
    TextEditingCombo combo;
    combo.command = true;
    combo.shift = true;
    combo.character = "a";
    defaults.push_back({"cmd+shift+a (openArchive)", combo});
  }
  for (int key_code : {kVKLeftArrow, kVKRightArrow, kVKUpArrow, kVKDownArrow}) {
    TextEditingCombo cmd_opt;
    cmd_opt.command = true;
    cmd_opt.option = true;
    cmd_opt.key_code = key_code;
    defaults.push_back({"cmd+opt+arrow (space/tab cycle)", cmd_opt});

    TextEditingCombo ctrl_opt;
    ctrl_opt.control = true;
    ctrl_opt.option = true;
    ctrl_opt.key_code = key_code;
    defaults.push_back({"ctrl+opt+arrow (space cycle)", ctrl_opt});
  }
  {
    TextEditingCombo combo;
    combo.control = true;
    combo.option = true;
    combo.character = "d";
    defaults.push_back({"ctrl+opt+d (developer mode)", combo});
  }
  {
    TextEditingCombo combo;
    combo.control = true;
    combo.shift = true;
    combo.character = "-";
    defaults.push_back({"ctrl+shift+- (closeSplitPane)", combo});
  }

  for (const Named& named : defaults) {
    EXPECT_FALSE(ComboEditsText(named.combo))
        << named.label
        << " ships as a default binding and is not a macOS text-editing chord "
           "- gating it would break a hotkey that works today";
  }
}

TEST(AtriumEditFocusGateTest, CmdOptArrowIsNotATextEditingChord) {
  // Called out separately from the defaults sweep above because it is the one
  // exclusion that looks wrong at a glance: cmd+arrow IS text editing and
  // opt+arrow IS text editing, but AppKit binds neither with the other's
  // modifier also held, so the pair is free - which is exactly why space
  // cycling could take it.
  TextEditingCombo combo;
  combo.command = true;
  combo.option = true;
  combo.key_code = kVKLeftArrow;
  EXPECT_FALSE(ComboEditsText(combo));
}

TEST(AtriumEditFocusGateTest, CmdClipboardAndUndoChordsEditText) {
  for (const char* letter : {"a", "c", "v", "x", "z"}) {
    TextEditingCombo combo;
    combo.command = true;
    combo.character = letter;
    EXPECT_TRUE(ComboEditsText(combo))
        << "cmd+" << letter << " is selectAll/copy/paste/cut/undo";
  }
  TextEditingCombo redo;
  redo.command = true;
  redo.shift = true;
  redo.character = "z";
  EXPECT_TRUE(ComboEditsText(redo)) << "cmd+shift+z is redo";
}

TEST(AtriumEditFocusGateTest, ControlEmacsBindingsEditText) {
  for (const char* letter :
       {"a", "b", "d", "e", "f", "h", "k", "n", "o", "p", "t", "v", "y"}) {
    TextEditingCombo combo;
    combo.control = true;
    combo.character = letter;
    EXPECT_TRUE(ComboEditsText(combo))
        << "ctrl+" << letter
        << " is one of AppKit's emacs bindings, live in every text field";
  }
}

TEST(AtriumEditFocusGateTest, ControlShiftMovementBindingsExtendSelection) {
  for (const char* letter : {"a", "b", "e", "f", "n", "p"}) {
    TextEditingCombo combo;
    combo.control = true;
    combo.shift = true;
    combo.character = letter;
    EXPECT_TRUE(ComboEditsText(combo))
        << "ctrl+shift+" << letter
        << " extends the selection by the same unit ctrl+" << letter << " moves";
  }
}

TEST(AtriumEditFocusGateTest, ControlSAndOtherNonBindingsAreUngated) {
  // ctrl+s is the VM fixture's rebound toggleSidebar, and it is NOT an AppKit
  // text binding (incremental search is an emacs thing, not a StandardKeyBinding
  // one), so a rebind onto it must keep working inside a text field.
  for (const char* letter : {"s", "g", "j", "q", "r", "u", "z"}) {
    TextEditingCombo combo;
    combo.control = true;
    combo.character = letter;
    EXPECT_FALSE(ComboEditsText(combo)) << "ctrl+" << letter;
  }
}

TEST(AtriumEditFocusGateTest, AModifierlessOrOptionOnlyChordIsUngated) {
  // Atrium cannot bind these at all (NativeHotkeyPayload drops any combo
  // without cmd or ctrl), so classifying them would be dead weight that only
  // risked catching a future chord by accident.
  TextEditingCombo bare_arrow;
  bare_arrow.key_code = kVKLeftArrow;
  EXPECT_FALSE(ComboEditsText(bare_arrow));

  TextEditingCombo shift_arrow;
  shift_arrow.shift = true;
  shift_arrow.key_code = kVKLeftArrow;
  EXPECT_FALSE(ComboEditsText(shift_arrow));

  TextEditingCombo opt_arrow;
  opt_arrow.option = true;
  opt_arrow.key_code = kVKLeftArrow;
  EXPECT_FALSE(ComboEditsText(opt_arrow));
}

TEST(AtriumEditFocusGateTest, AnEmptyComboIsUngated) {
  EXPECT_FALSE(ComboEditsText(TextEditingCombo()));
}

}  // namespace
}  // namespace atrium
