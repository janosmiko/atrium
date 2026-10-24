// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_vim_key_decision.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_VIM_KEY_DECISION_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_VIM_KEY_DECISION_H_

#include <optional>
#include <string>

#include "chrome/browser/ui/atrium/atrium_native_hotkey_defaults.h"

namespace atrium {

// whether PreHandleKeyboardEvent consumes a key for vim mode
// before the renderer sees it.
//
// The browser cannot see which element has focus without asking the renderer,
// so it does not try. `page_in_normal_mode` is the page's own answer, pushed
// from vim-page.js on every focus change. The browser enforces, the page
// decides.
struct VimKeyInputs {
  bool vim_enabled = false;

  // The mode vim-page.js last reported. False means a text field has focus.
  bool page_in_normal_mode = false;

  bool is_key_down = false;

  bool command = false;
  bool control = false;
  bool option = false;

  // Shift is NOT a steal blocker: G is shift+g and belongs to the grammar.
  bool shift = false;

  // The page is in a mode where every key is a label rather than a verb, so
  // the grammar does not apply. Hint mode sets it, and the page clears it.
  bool page_capturing = false;

  // vim-page.js runs its verbs and its veto re-type in the main
  // frame only, so a key stolen from a focused iframe is lost. 1Password's
  // inline unlock field is one.
  bool focus_in_child_frame = false;

  // The character the key produces, lowercased for unshifted keys. Empty for
  // arrows and function keys. A control chord arrives as "ctrl-d" rather than
  // as the 0x04 its text carries, so one string names the whole chord.
  std::string character;
};

enum class VimKeyRoute {
  // The renderer gets the event unchanged, exactly as before vim mode existed.
  kPassToPage,
  // The browser consumes the event and forwards the character to vim-page.js.
  kStealForVim,
};

// True for a character V1's grammar has a verb for.
bool IsVimV1GrammarKey(const std::string& character);

VimKeyRoute RouteVimKey(const VimKeyInputs& in);

// the native action a browser-verb answer from vim-page.js runs.
// Empty for an answer the window handles itself, so a caller that forgets one
// gets nothing rather than a wrong action.
std::optional<NativeHotkeyAction> NativeActionForVimAnswer(
    const std::string& answer);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_VIM_KEY_DECISION_H_
