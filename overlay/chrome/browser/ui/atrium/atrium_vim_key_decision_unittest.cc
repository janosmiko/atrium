// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_vim_key_decision_unittest.cc
#include "chrome/browser/ui/atrium/atrium_vim_key_decision.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

VimKeyInputs NormalModeKey(const char* character) {
  VimKeyInputs in;
  in.vim_enabled = true;
  in.page_in_normal_mode = true;
  in.is_key_down = true;
  in.character = character;
  return in;
}

TEST(AtriumVimKeyDecisionTest, MotionKeyInNormalModeIsStolen) {
  EXPECT_EQ(RouteVimKey(NormalModeKey("j")), VimKeyRoute::kStealForVim);
  EXPECT_EQ(RouteVimKey(NormalModeKey("k")), VimKeyRoute::kStealForVim);
  EXPECT_EQ(RouteVimKey(NormalModeKey("g")), VimKeyRoute::kStealForVim);
}

// The whole point of the toggle. With vim off the window must behave exactly
// as it did before this feature existed.
TEST(AtriumVimKeyDecisionTest, NothingIsStolenWhileVimIsOff) {
  VimKeyInputs in = NormalModeKey("j");
  in.vim_enabled = false;
  EXPECT_EQ(RouteVimKey(in), VimKeyRoute::kPassToPage);
}

// The failure that would make vim mode feel broken: a keystroke meant for a
// text field swallowed as a motion.
TEST(AtriumVimKeyDecisionTest, NothingIsStolenWhileThePageReportsInsert) {
  VimKeyInputs in = NormalModeKey("j");
  in.page_in_normal_mode = false;
  EXPECT_EQ(RouteVimKey(in), VimKeyRoute::kPassToPage);
}

// 1Password's inline unlock field lives in an iframe, and the main
// frame reads a focused iframe as NORMAL.
TEST(AtriumVimKeyDecisionTest, NothingIsStolenWhileAChildFrameHasFocus) {
  VimKeyInputs in = NormalModeKey("j");
  in.focus_in_child_frame = true;
  EXPECT_EQ(RouteVimKey(in), VimKeyRoute::kPassToPage);

  in.page_capturing = true;
  EXPECT_EQ(RouteVimKey(in), VimKeyRoute::kPassToPage);
}

// Cmd and Option chords belong to Atrium's own hotkeys and to the system.
// Shift is not a modifier here: G is shift+g.
TEST(AtriumVimKeyDecisionTest, ModifiedChordsArePassedThrough) {
  VimKeyInputs command = NormalModeKey("j");
  command.command = true;
  EXPECT_EQ(RouteVimKey(command), VimKeyRoute::kPassToPage);

  VimKeyInputs option = NormalModeKey("k");
  option.option = true;
  EXPECT_EQ(RouteVimKey(option), VimKeyRoute::kPassToPage);
}

// control used to block the steal outright, so the four page
// motions never reached vim.
TEST(AtriumVimKeyDecisionTest, TheFourControlMotionsAreStolen) {
  for (const char* chord : {"ctrl-d", "ctrl-u", "ctrl-f", "ctrl-b"}) {
    VimKeyInputs in = NormalModeKey(chord);
    in.control = true;
    EXPECT_EQ(RouteVimKey(in), VimKeyRoute::kStealForVim) << chord;
  }
}

// Every other chord still belongs to the page, and hint mode belongs to the
// labels rather than to a motion.
TEST(AtriumVimKeyDecisionTest, OtherControlChordsStillReachThePage) {
  VimKeyInputs other = NormalModeKey("ctrl-a");
  other.control = true;
  EXPECT_EQ(RouteVimKey(other), VimKeyRoute::kPassToPage);

  VimKeyInputs hinting = NormalModeKey("ctrl-d");
  hinting.control = true;
  hinting.page_capturing = true;
  EXPECT_EQ(RouteVimKey(hinting), VimKeyRoute::kPassToPage);

  // The chord is named from the physical key, so ctrl+shift+d arrives as
  // "ctrl-d" and would take the site shortcut bound to it.
  VimKeyInputs shifted = NormalModeKey("ctrl-d");
  shifted.control = true;
  shifted.shift = true;
  EXPECT_EQ(RouteVimKey(shifted), VimKeyRoute::kPassToPage);
}

// history had no motion at all.
TEST(AtriumVimKeyDecisionTest, HistoryLettersAreStolen) {
  VimKeyInputs back = NormalModeKey("H");
  back.shift = true;
  EXPECT_EQ(RouteVimKey(back), VimKeyRoute::kStealForVim);

  VimKeyInputs forward = NormalModeKey("L");
  forward.shift = true;
  EXPECT_EQ(RouteVimKey(forward), VimKeyRoute::kStealForVim);
}

TEST(AtriumVimKeyDecisionTest, ShiftAloneStillReachesVim) {
  VimKeyInputs in = NormalModeKey("G");
  in.shift = true;
  EXPECT_EQ(RouteVimKey(in), VimKeyRoute::kStealForVim);
}

// Key up must not fire a motion twice, and a key with no character (arrows,
// function keys) is not part of the V1 grammar.
TEST(AtriumVimKeyDecisionTest, KeyUpAndCharacterlessKeysArePassedThrough) {
  VimKeyInputs up = NormalModeKey("j");
  up.is_key_down = false;
  EXPECT_EQ(RouteVimKey(up), VimKeyRoute::kPassToPage);

  VimKeyInputs empty = NormalModeKey("");
  EXPECT_EQ(RouteVimKey(empty), VimKeyRoute::kPassToPage);
}

// A key vim has no verb for must reach the page. Stealing every key would
// break every site's own single-key shortcuts.
TEST(AtriumVimKeyDecisionTest, KeysOutsideTheGrammarArePassedThrough) {
  EXPECT_EQ(RouteVimKey(NormalModeKey("q")), VimKeyRoute::kPassToPage);
  EXPECT_EQ(RouteVimKey(NormalModeKey("7")), VimKeyRoute::kPassToPage);
}

// V2 grammar: y for yy, f and F for hints, and the find trio.
TEST(AtriumVimKeyDecisionTest, TheV2GrammarIsStolen) {
  for (const char* character : {"y", "f", "F", "/", "n", "N"}) {
    EXPECT_EQ(RouteVimKey(NormalModeKey(character)), VimKeyRoute::kStealForVim)
        << character;
  }
}

// While hints are up, j is a label rather than a motion, so the page asks for
// every key instead of the browser learning what hint mode is.
TEST(AtriumVimKeyDecisionTest, CaptureStealsAKeyOutsideTheGrammar) {
  VimKeyInputs in = NormalModeKey("q");
  EXPECT_EQ(RouteVimKey(in), VimKeyRoute::kPassToPage);
  in.page_capturing = true;
  EXPECT_EQ(RouteVimKey(in), VimKeyRoute::kStealForVim);
}

// Capture must not eat a chord: Cmd+W still closes the tab with hints up.
TEST(AtriumVimKeyDecisionTest, CaptureDoesNotStealAChord) {
  VimKeyInputs in = NormalModeKey("w");
  in.page_capturing = true;
  in.command = true;
  EXPECT_EQ(RouteVimKey(in), VimKeyRoute::kPassToPage);
}

// Capture sits below the INSERT check so a focused field keeps its typing.
TEST(AtriumVimKeyDecisionTest, CaptureDoesNotStealFromAFocusedField) {
  VimKeyInputs in = NormalModeKey("q");
  in.page_capturing = true;
  in.page_in_normal_mode = false;
  EXPECT_EQ(RouteVimKey(in), VimKeyRoute::kPassToPage);
}

// V3 grammar: the tab, close, open and space keys.
TEST(AtriumVimKeyDecisionTest, TheV3GrammarIsStolen) {
  for (const char* character : {"t", "T", "x", "o", "]", "["}) {
    EXPECT_EQ(RouteVimKey(NormalModeKey(character)), VimKeyRoute::kStealForVim)
        << character;
  }
}

// Cmd+T must still open a tab and Cmd+W must still close one. Adding single
// keys to the grammar is what would break that.
TEST(AtriumVimKeyDecisionTest, TheV3GrammarStillYieldsToAChord) {
  for (const char* character : {"t", "x", "o"}) {
    VimKeyInputs in = NormalModeKey(character);
    in.command = true;
    EXPECT_EQ(RouteVimKey(in), VimKeyRoute::kPassToPage) << character;
  }
}

// Media keys. A key the browser does not steal never reaches the
// page, so the grammar list and the page's verb table move together or the
// verb exists and nothing can trigger it.
TEST(AtriumVimKeyDecisionTest, TheV5MediaGrammarIsStolen) {
  for (const char* character : {"p", ",", ".", "<", ">"}) {
    EXPECT_EQ(RouteVimKey(NormalModeKey(character)), VimKeyRoute::kStealForVim)
        << character;
  }
}

// Cmd+comma is Preferences on macOS and must stay that way.
TEST(AtriumVimKeyDecisionTest, TheV5MediaGrammarStillYieldsToAChord) {
  VimKeyInputs in = NormalModeKey(",");
  in.command = true;
  EXPECT_EQ(RouteVimKey(in), VimKeyRoute::kPassToPage);
}

// The page names a verb, the browser runs it. Every answer here has to reach
// the same action its cmd-chord does, or rebinding the chord would move one
// route and leave vim on the old one.
TEST(AtriumVimKeyDecisionTest, EachBrowserVerbNamesItsNativeAction) {
  EXPECT_EQ(NativeActionForVimAnswer("next-tab"), NativeHotkeyAction::kNextTab);
  EXPECT_EQ(NativeActionForVimAnswer("previous-tab"),
            NativeHotkeyAction::kPreviousTab);
  EXPECT_EQ(NativeActionForVimAnswer("next-space"),
            NativeHotkeyAction::kNextSpace);
  EXPECT_EQ(NativeActionForVimAnswer("previous-space"),
            NativeHotkeyAction::kPreviousSpace);
  EXPECT_EQ(NativeActionForVimAnswer("open-command-bar"),
            NativeHotkeyAction::kOpenCommandBar);
}

// close-tab is deliberately absent: a favourite parks rather than closes, and
// only CloseSidebarTab knows that. Answering it here would lose the parking.
TEST(AtriumVimKeyDecisionTest, AnswersWithNoNativeActionAreRefused) {
  EXPECT_FALSE(NativeActionForVimAnswer("close-tab").has_value());
  EXPECT_FALSE(NativeActionForVimAnswer("copy-url").has_value());
  EXPECT_FALSE(NativeActionForVimAnswer("handled").has_value());
  EXPECT_FALSE(NativeActionForVimAnswer("").has_value());
}

}  // namespace
}  // namespace atrium
