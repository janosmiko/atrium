// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_edit_focus_gate.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_EDIT_FOCUS_GATE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_EDIT_FOCUS_GATE_H_

#include <cstdint>
#include <string_view>

namespace atrium {

// the wire values AtriumOnEditFocusChanged carries and the ONE
// vocabulary both dispatch routes reason about - see the retired ABI header's EDIT
// FOCUS doc block. APPEND ONLY, never renumber: these cross the ABI.
//
// kUnknown is deliberately 0 so that every way of NOT knowing collapses onto
// the fail-closed value for free - a zero-initialized Swift cache that has
// never received a push, an unrecognized future wire value, and a snapshot
// whose group never resolved all read as kUnknown without anybody having to
// remember to write a default. C++ NEVER pushes kUnknown: the browser process
// is the authority on "has the renderer told me an editable element is
// focused?", and "it has not" is a positive kNone answer, not an absence.
// kUnknown therefore means exactly "the Swift side has no valid snapshot",
// which is the only genuinely unknowable case.
enum class EditFocusKind : int32_t {
  kUnknown = 0,
  // Nothing that consumes typing owns the keyboard for this window.
  kNone = 1,
  // The renderer reports an editable element focused in the active tab
  // (content/public/browser/focused_node_details.h's is_editable_node).
  kPageEditable = 2,
  // One of Atrium's OWN text surfaces owns the keyboard - the command bar
  // query field, a sidebar rename editor. Swift is the only side that can see
  // these, so it pushes them to C++ through AtriumHostOps.set_atrium_text_focus
  // and C++ folds them into the same kind it pushes back.
  kAtriumText = 3,
};

// Maps a wire value onto the enum, collapsing anything unrecognized (an older
// binary reading a newer dylib's value, a corrupted push) onto kUnknown so an
// unexpected number fails CLOSED rather than being treated as kNone by a
// static_cast. The wire contract's "unknown value must degrade" rule, applied
// in the safe direction - unlike CommandBarOpenReason, where degrading to 0
// means "act normally", degrading to 0 here means "refuse to fire".
EditFocusKind EditFocusKindFromWire(int32_t wire);

// Which of the two dispatch routes is asking. Atrium binds hotkeys through
// exactly two mechanisms and BOTH must consult this gate, or the one that does
// not becomes the hole (see the retired ABI header's EDIT FOCUS doc block):
//   kSwiftShortcut       - SwiftUI Button().keyboardShortcut(), resolved inside
//                          the sidebar's own view hierarchy
//                          (SidebarView.shortcutButton).
//   kNativeKeyEquivalent - the raw NSEvent hook that runs BEFORE Chrome's
//                          accelerator lookup and before any renderer sees the
//                          key (AtriumCommandDispatcherDelegate::
//                          prePerformKeyEquivalent -> NativeHotkeyMap).
enum class HotkeyRoute {
  kSwiftShortcut,
  kNativeKeyEquivalent,
};

// Carbon virtual key codes for the four arrows, duplicated here rather than
// pulled from <Carbon/HIToolbox/Events.h> so this stays a dependency-free
// source_set that plain-C++ unit tests can link (the same reason
// atrium_command_dispatcher_delegate.mm duplicates kVKTabForCycling).
inline constexpr int kVKLeftArrow = 0x7B;
inline constexpr int kVKRightArrow = 0x7C;
inline constexpr int kVKDownArrow = 0x7D;
inline constexpr int kVKUpArrow = 0x7E;

// One chord, in the shape NativeHotkeyMap::Combo and Swift's KeyCombo both
// already hold it: an explicit modifier set plus EITHER a lowercased single
// character OR a virtual key code for a named key. Explicit bools rather than
// an NSEventModifierFlags mask so this header needs no AppKit and both sides
// translate at their own edge (Swift's KeyCombo.Modifier set, C++'s
// NSEventModifierFlag bits) instead of agreeing on a bitmask convention.
struct TextEditingCombo {
  bool command = false;
  bool shift = false;
  bool option = false;
  bool control = false;
  // Lowercased single character ("a", "z"); empty when `key_code` is set.
  std::string_view character;
  // kVK_* for a named key; -1 when `character` is set.
  int key_code = -1;
};

// Whether macOS itself gives `combo` a standard text-editing meaning inside an
// editable field, i.e. whether stealing it would destroy an edit the user was
// making. This is the "which combos need gating at all" half of the decision,
// and it is deliberately a property of the CHORD, not of the action bound to
// it: any action a user rebinds onto a text-editing chord is gated
// automatically, with no per-action policy to keep in sync.
//
// The set is the reachable intersection of two things: AppKit's
// StandardKeyBinding bindings for an editable field, and the chords Atrium can
// actually bind (NativeHotkeyPayload drops any combo without cmd or ctrl,
// because performKeyEquivalent: delivery is only guaranteed for those). That
// intersection is:
//   - cmd+arrow and cmd+shift+arrow - line/document movement
//     (moveToLeftEndOfLine:, moveToBeginningOfDocument:) and their
//     selection-extending siblings. THE combo class that needs this gate, and the
//     reason it exists.
//   - cmd+{a,c,v,x,z} - selectAll:, copy:, paste:, cut:, undo:.
//   - cmd+shift+z - redo:.
//   - ctrl+{a,b,d,e,f,h,k,n,o,p,t,v,y} and the ctrl+shift movement variants -
//     AppKit's emacs bindings, live in every NSTextView and every web text
//     field on macOS.
// Anything else is NOT classified, and that exclusion is load-bearing rather
// than incidental: every hotkey Atrium ships a DEFAULT binding for today
// (cmd+1..9, cmd+opt+arrow, ctrl+opt+arrow, opt+tab, cmd+t/s/l/j/k/d,
// cmd+shift+a, cmd+shift+bracket, ctrl+opt+d) must keep firing in a text field
// exactly as it does now, because none of them is a macOS text-editing chord.
// cmd+opt+arrow in particular is NOT one (AppKit binds opt+arrow and cmd+arrow
// separately, never the pair), so space cycling is untouched by this gate.
bool ComboEditsText(const TextEditingCombo& combo);

// Everything either route knows at the instant a key arrives. Resolved by the
// caller so the decision itself stays a pure function of plain values - the
// house pattern (SlotPresentationTable's PaneAttachInputs, Swift's
// RowDropRouting).
struct EditFocusGateInputs {
  // Kept so call sites are self-documenting about which route is asking, and
  // so a test can hand in the SAME facts on both routes and prove the answer
  // does not depend on the route. That independence is the actual invariant
  // this gate ships: a gate that answered differently per route would be a
  // gate with a hole in it, which is exactly the state no hotkey should
  // ship on top of.
  HotkeyRoute route = HotkeyRoute::kSwiftShortcut;
  // Whether the EDIT FOCUS ABI group resolved for the side that is asking.
  // False means this side cannot see one of the two halves of the signal (the
  // page half or the Atrium-text half), so it must not act on the half it
  // does see - the mismatched-dylib-and-binary case.
  bool signal_connected = false;
  EditFocusKind kind = EditFocusKind::kUnknown;
  // The tab the snapshot in `kind` was observed for, and the tab that is
  // active NOW. C++ stamps every push with its active tab's id whatever the
  // kind, so the asker can tell a snapshot that has been overtaken by a tab
  // switch from one that still describes the surface being typed into. 0 on
  // both sides for a window with no tabs (Swift's nil activeTabID), which
  // compares equal and is correct: there is no page to type into.
  uint64_t snapshot_tab_id = 0;
  uint64_t active_tab_id = 0;
  // ComboEditsText's answer for the chord that just arrived.
  bool combo_edits_text = false;
};

// Whether the action fires, and - when it does not - WHY, so the reason can be
// logged as VLOG evidence and asserted in a test without re-deriving it. Every
// kPass* value means the same thing to the caller (do not fire, let the key
// through to the page); they are separate values because "the gate is degraded"
// and "the user is genuinely typing" are different bugs to chase.
enum class EditFocusGateDecision {
  kFire,
  kPassSignalUnavailable,
  kPassFocusUnknown,
  kPassSnapshotStale,
  kPassPageEditable,
  kPassAtriumText,
};

// the ONE decision both dispatch routes run. Fails CLOSED on every
// axis - an unavailable signal, a snapshot from a tab that is no longer active,
// or any kind other than a positive kNone all refuse to fire - because
// swallowing a keystroke the user meant for a text field is strictly worse than
// a hotkey that declines to fire. That asymmetry is the whole reason a combo
// is not just bound directly: a hotkey that
// silently eats "extend selection to end of line" is a data-loss bug, while a
// hotkey that occasionally needs a second press is an annoyance.
//
// Note what is NOT gated: a chord macOS gives no text-editing meaning fires
// unconditionally, before any of the fail-closed checks. A degraded signal must
// not disable hotkeys that were never at risk - that would turn one missing
// dylib symbol into "none of my keyboard shortcuts work".
EditFocusGateDecision DecideEditFocusGate(const EditFocusGateInputs& inputs);

// Sugar for the call sites, which only ever branch two ways.
inline bool MayFireHotkey(EditFocusGateDecision decision) {
  return decision == EditFocusGateDecision::kFire;
}

// Stable short token for VLOG evidence and for the VM scenario's log
// assertions - the runner greps the run log for these (see
// build/scenarios/edit-focus-gate.json).
const char* EditFocusGateDecisionName(EditFocusGateDecision decision);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_EDIT_FOCUS_GATE_H_
