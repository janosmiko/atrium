// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_vim_key_decision.cc
#include "chrome/browser/ui/atrium/atrium_vim_key_decision.h"

#include <array>
#include <optional>

namespace atrium {

namespace {

// V1's whole grammar. g is here for the gg prefix. vim-page.js owns the
// pending-prefix state, so the browser only has to know g can start a verb.
// H and L are history, and the four ctrl chords are the page motions.
constexpr std::array<const char*, 29> kGrammar = {
    "j",      "k",      "d",      "u",      "g",      "G",
    "y",      "f",      "F",      "/",      "n",      "N",
    "t",      "T",      "x",      "o",      "]",      "[",
    "p",      ",",      ".",      "<",      ">",      "H",
    "L",      "ctrl-d", "ctrl-u", "ctrl-f", "ctrl-b"};

}  // namespace

bool IsVimV1GrammarKey(const std::string& character) {
  for (const char* key : kGrammar) {
    if (character == key) {
      return true;
    }
  }
  return false;
}

VimKeyRoute RouteVimKey(const VimKeyInputs& in) {
  if (!in.vim_enabled || !in.page_in_normal_mode || !in.is_key_down ||
      in.focus_in_child_frame) {
    return VimKeyRoute::kPassToPage;
  }
  if (in.command || in.option) {
    return VimKeyRoute::kPassToPage;
  }
  // Only the four page motions, never over hint mode, and never with shift:
  // the chord is named from the physical key, so ctrl+shift+d reads as ctrl+d
  // and would take every site shortcut bound to it.
  if (in.control) {
    return !in.shift && !in.page_capturing && IsVimV1GrammarKey(in.character)
               ? VimKeyRoute::kStealForVim
               : VimKeyRoute::kPassToPage;
  }
  // Below the INSERT check above, so a focused field keeps its own typing.
  if (in.page_capturing) {
    return VimKeyRoute::kStealForVim;
  }
  if (!IsVimV1GrammarKey(in.character)) {
    return VimKeyRoute::kPassToPage;
  }
  return VimKeyRoute::kStealForVim;
}

std::optional<NativeHotkeyAction> NativeActionForVimAnswer(
    const std::string& answer) {
  if (answer == "next-tab") {
    return NativeHotkeyAction::kNextTab;
  }
  if (answer == "previous-tab") {
    return NativeHotkeyAction::kPreviousTab;
  }
  if (answer == "next-space") {
    return NativeHotkeyAction::kNextSpace;
  }
  if (answer == "previous-space") {
    return NativeHotkeyAction::kPreviousSpace;
  }
  if (answer == "open-command-bar") {
    return NativeHotkeyAction::kOpenCommandBar;
  }
  return std::nullopt;
}

}  // namespace atrium
