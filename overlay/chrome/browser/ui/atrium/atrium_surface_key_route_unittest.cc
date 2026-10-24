// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_surface_key_route_unittest.cc
#include "chrome/browser/ui/atrium/atrium_surface_key_route.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

SurfaceKeyInputs Chord(const char* character) {
  SurfaceKeyInputs in;
  in.command = true;
  in.character = character;
  return in;
}

SurfaceKeyInputs QuitChord() {
  return Chord("q");
}

// The regression under test. The palette and the peek card both routed
// the quit to the main menu from postPerformKeyEquivalent, which only runs
// when the surface's view hierarchy declines the event - and a live
// RenderWidgetHostViewCocoa never declines. Answering kWebContent here is
// answering "let the renderer have it", which is where Cmd+Q went.
TEST(AtriumSurfaceKeyRouteTest, QuitChordGoesToTheMainMenuNotTheWebContent) {
  EXPECT_EQ(RouteForSurfaceKey(QuitChord()), SurfaceKeyRoute::kMainMenu);
}

// Quitting must not depend on what else is bound to the chord. A user who
// rebinds an Atrium action onto Cmd+Q loses the action, not the quit.
TEST(AtriumSurfaceKeyRouteTest, QuitChordBeatsABoundAtriumAction) {
  SurfaceKeyInputs in = QuitChord();
  in.atrium_action_matched = true;
  EXPECT_EQ(RouteForSurfaceKey(in), SurfaceKeyRoute::kMainMenu);
}

// And not on how the key was pressed. The repeat guard exists so a held Cmd+T
// cannot strobe the palette; applied to the quit it would turn a leaned-on
// Cmd+Q into nothing at all.
TEST(AtriumSurfaceKeyRouteTest, QuitChordBeatsTheRepeatGuard) {
  SurfaceKeyInputs in = QuitChord();
  in.atrium_action_matched = true;
  in.is_repeat = true;
  EXPECT_EQ(RouteForSurfaceKey(in), SurfaceKeyRoute::kMainMenu);
}

// The modifier mask is exact. Cmd+Shift+Q and friends are other chords, and
// claiming them would take keys the page or the menu is entitled to.
TEST(AtriumSurfaceKeyRouteTest, QuitChordIsExactOverModifiers) {
  EXPECT_TRUE(IsQuitChord(QuitChord()));

  SurfaceKeyInputs with_shift = QuitChord();
  with_shift.shift = true;
  EXPECT_FALSE(IsQuitChord(with_shift));

  SurfaceKeyInputs with_option = QuitChord();
  with_option.option = true;
  EXPECT_FALSE(IsQuitChord(with_option));

  SurfaceKeyInputs with_control = QuitChord();
  with_control.control = true;
  EXPECT_FALSE(IsQuitChord(with_control));

  SurfaceKeyInputs no_command = QuitChord();
  no_command.command = false;
  EXPECT_FALSE(IsQuitChord(no_command));
}

// The caller lowercases charactersIgnoringModifiers; this pins that contract,
// because a raw "Q" arriving here would silently stop matching.
TEST(AtriumSurfaceKeyRouteTest, QuitChordMatchesTheLowercasedCharacterOnly) {
  EXPECT_FALSE(IsQuitChord(Chord("Q")));
  EXPECT_FALSE(IsQuitChord(Chord("")));
  EXPECT_FALSE(IsQuitChord(Chord("w")));
}

// Everything else keeps today's behaviour: an unmatched chord is declined so
// the focused field still gets Cmd+C/V/A.
TEST(AtriumSurfaceKeyRouteTest, UnmatchedChordGoesToTheWebContent) {
  EXPECT_EQ(RouteForSurfaceKey(Chord("c")), SurfaceKeyRoute::kWebContent);
}

TEST(AtriumSurfaceKeyRouteTest, MatchedActionRunsAndItsRepeatIsDropped) {
  SurfaceKeyInputs in = Chord("t");
  in.atrium_action_matched = true;
  EXPECT_EQ(RouteForSurfaceKey(in), SurfaceKeyRoute::kAtriumAction);

  in.is_repeat = true;
  EXPECT_EQ(RouteForSurfaceKey(in), SurfaceKeyRoute::kDropRepeat);
}

}  // namespace
}  // namespace atrium
