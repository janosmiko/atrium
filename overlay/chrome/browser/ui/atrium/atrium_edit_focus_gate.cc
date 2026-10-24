// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_edit_focus_gate.cc
#include "chrome/browser/ui/atrium/atrium_edit_focus_gate.h"

#include <string_view>

namespace atrium {
namespace {

bool IsArrowKeyCode(int key_code) {
  return key_code == kVKLeftArrow || key_code == kVKRightArrow ||
         key_code == kVKUpArrow || key_code == kVKDownArrow;
}

bool Contains(std::string_view set, std::string_view character) {
  return character.size() == 1 &&
         set.find(character.front()) != std::string_view::npos;
}

// AppKit StandardKeyBinding, cmd-modified: selectAll:, copy:, paste:, cut:,
// undo:.
constexpr std::string_view kCommandEditingLetters = "acvxz";

// AppKit's emacs bindings on a bare Control: ^a/^e paragraph ends, ^b/^f
// character movement, ^n/^p line movement, ^d/^h deletion, ^k kill-to-end,
// ^o insertNewlineIgnoringFieldEditor:, ^t transpose:, ^v pageDown:, ^y yank:.
constexpr std::string_view kControlEditingLetters = "abdefhknoptvy";

// The subset of the above that MOVES the insertion point, so ctrl+shift+<key>
// is its selection-extending sibling. Deletion and transposition have no
// shift form, which is why this is a narrower set than kControlEditingLetters.
constexpr std::string_view kControlMovementLetters = "abefnp";

}  // namespace

EditFocusKind EditFocusKindFromWire(int32_t wire) {
  switch (wire) {
    case static_cast<int32_t>(EditFocusKind::kNone):
      return EditFocusKind::kNone;
    case static_cast<int32_t>(EditFocusKind::kPageEditable):
      return EditFocusKind::kPageEditable;
    case static_cast<int32_t>(EditFocusKind::kAtriumText):
      return EditFocusKind::kAtriumText;
    default:
      // Includes the explicit 0 and every value this build has never heard of.
      return EditFocusKind::kUnknown;
  }
}

bool ComboEditsText(const TextEditingCombo& combo) {
  // Arrows: only the cmd and cmd+shift forms. Adding option (or control) takes
  // the chord out of AppKit's bindings entirely, which is what left
  // cmd+opt+arrow free for space cycling.
  if (IsArrowKeyCode(combo.key_code)) {
    return combo.command && !combo.option && !combo.control;
  }
  if (combo.key_code != -1) {
    // A named key that is not an arrow (tab, escape, space) - none of them has
    // a text-editing meaning worth reserving.
    return false;
  }
  if (combo.character.empty()) {
    return false;
  }

  if (combo.command && !combo.option && !combo.control) {
    if (!combo.shift) {
      return Contains(kCommandEditingLetters, combo.character);
    }
    // cmd+shift+z is redo:. No other cmd+shift letter is a text binding, which
    // is what keeps cmd+shift+a (openArchive) firing inside a field.
    return combo.character == "z";
  }

  if (combo.control && !combo.command && !combo.option) {
    return combo.shift ? Contains(kControlMovementLetters, combo.character)
                       : Contains(kControlEditingLetters, combo.character);
  }

  return false;
}

EditFocusGateDecision DecideEditFocusGate(const EditFocusGateInputs& inputs) {
  // Checked FIRST, ahead of every fail-closed check: a chord macOS gives no
  // text-editing meaning was never at risk, so a degraded signal must not
  // disable it. Getting this order wrong would turn one unresolved dylib
  // symbol into "none of the user's hotkeys work".
  if (!inputs.combo_edits_text) {
    return EditFocusGateDecision::kFire;
  }

  if (!inputs.signal_connected) {
    return EditFocusGateDecision::kPassSignalUnavailable;
  }
  if (inputs.kind == EditFocusKind::kUnknown) {
    return EditFocusGateDecision::kPassFocusUnknown;
  }
  // A kind is only an answer about the surface it was observed on. Checked
  // before the kind switch so a kNone that a tab switch has overtaken cannot
  // authorize a fire.
  if (inputs.snapshot_tab_id != inputs.active_tab_id) {
    return EditFocusGateDecision::kPassSnapshotStale;
  }

  switch (inputs.kind) {
    case EditFocusKind::kPageEditable:
      return EditFocusGateDecision::kPassPageEditable;
    case EditFocusKind::kAtriumText:
      return EditFocusGateDecision::kPassAtriumText;
    case EditFocusKind::kNone:
      return EditFocusGateDecision::kFire;
    case EditFocusKind::kUnknown:
      // Unreachable - handled above. Repeated rather than defaulted so a new
      // kind added to the enum fails to compile here instead of silently
      // falling into whichever branch the default happened to be.
      return EditFocusGateDecision::kPassFocusUnknown;
  }
}

const char* EditFocusGateDecisionName(EditFocusGateDecision decision) {
  switch (decision) {
    case EditFocusGateDecision::kFire:
      return "fire";
    case EditFocusGateDecision::kPassSignalUnavailable:
      return "pass-signal-unavailable";
    case EditFocusGateDecision::kPassFocusUnknown:
      return "pass-focus-unknown";
    case EditFocusGateDecision::kPassSnapshotStale:
      return "pass-snapshot-stale";
    case EditFocusGateDecision::kPassPageEditable:
      return "pass-page-editable";
    case EditFocusGateDecision::kPassAtriumText:
      return "pass-atrium-text";
  }
}

}  // namespace atrium
