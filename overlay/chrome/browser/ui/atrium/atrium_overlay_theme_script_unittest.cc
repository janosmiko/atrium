// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_overlay_theme_script.h"

#include <optional>

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"

namespace atrium {

namespace {

OverlayAccentTokens SystemBlue() {
  return OverlayAccentTokens{"#007AFF", "5 21 38", "#E7F2FF", "#61ADFF",
                             "#FFFFFF"};
}

}  // namespace

TEST(OverlayThemeScript, WritesTheTintAsATriplet) {
  const std::string js = OverlayThemeScript(
      SkColorSetRGB(176, 58, 72), /*has_tint=*/true,
      /*native=*/false, 0xFFF2F2F7, SK_ColorBLACK, /*theme=*/"",
      /*system_accent=*/std::nullopt);
  EXPECT_NE(js.find("'--atrium-space-tint', '176 58 72'"), std::string::npos);
  EXPECT_NE(js.find("'atrium-native-chrome', false"), std::string::npos);
}

TEST(OverlayThemeScript, RemovesTheTintWhenThereIsNone) {
  const std::string js = OverlayThemeScript(
      SK_ColorTRANSPARENT, /*has_tint=*/false,
      /*native=*/false, 0xFFF2F2F7, SK_ColorBLACK, /*theme=*/"",
      /*system_accent=*/std::nullopt);
  EXPECT_NE(js.find("removeProperty('--atrium-space-tint')"),
            std::string::npos);
  EXPECT_NE(js.find("removeProperty('--atrium-space-tint-ink')"),
            std::string::npos);
}

// The ink is graded against the tint, not fixed at white, because a filled
// button on a pale custom tint is unreadable in white.
TEST(OverlayThemeScript, GradesTheAccentInkAgainstTheTint) {
  const std::string on_dark = OverlayThemeScript(
      SkColorSetRGB(15, 43, 70), /*has_tint=*/true,
      /*native=*/false, 0xFFF2F2F7, SK_ColorBLACK, /*theme=*/"",
      /*system_accent=*/std::nullopt);
  EXPECT_NE(on_dark.find("'--atrium-space-tint-ink', '255 255 255'"),
            std::string::npos);

  const std::string on_light = OverlayThemeScript(
      SkColorSetRGB(240, 244, 248), /*has_tint=*/true,
      /*native=*/false, 0xFFF2F2F7, SK_ColorBLACK, /*theme=*/"",
      /*system_accent=*/std::nullopt);
  EXPECT_NE(on_light.find("'--atrium-space-tint-ink', '0 0 0'"),
            std::string::npos);
}

TEST(OverlayThemeScript, WritesTheSurroundForNativeMode) {
  const std::string js = OverlayThemeScript(
      SkColorSetRGB(176, 58, 72), /*has_tint=*/true,
      /*native=*/true, 0xFFF2F2F7, SkColorSetRGB(0, 0, 0), /*theme=*/"",
      /*system_accent=*/std::nullopt);
  EXPECT_NE(js.find("'--atrium-surround-color', '#f2f2f7'"), std::string::npos);
  EXPECT_NE(js.find("'--atrium-surround-ink', '0 0 0'"), std::string::npos);
  EXPECT_NE(js.find("'atrium-native-chrome', true"), std::string::npos);
}

TEST(OverlayThemeScript, TogglesTheBrandAccentClassWhenNoThemeIsChosen) {
  const std::string js = OverlayThemeScript(
      SkColorSetRGB(15, 43, 70), /*has_tint=*/true,
      /*native=*/false, 0xFFF2F2F7, SK_ColorBLACK, /*theme=*/"",
      /*system_accent=*/std::nullopt);
  EXPECT_NE(js.find("'atrium-overlay-accent-brand', true"), std::string::npos);
}

TEST(OverlayThemeScript, AThemeArrivesAsItsOwnClass) {
  const std::string js = OverlayThemeScript(
      SkColorSetRGB(176, 58, 72), /*has_tint=*/true,
      /*native=*/false, 0xFFF2F2F7, SK_ColorBLACK, /*theme=*/"nord",
      /*system_accent=*/std::nullopt);
  EXPECT_NE(js.find("classList.add('atrium-theme-nord')"), std::string::npos);
}

// The classes are alternatives. Adding without clearing would leave the
// previous theme to win or lose on stylesheet order rather than on the choice.
TEST(OverlayThemeScript, EveryThemeClassIsClearedBeforeOneIsAdded) {
  const std::string js = OverlayThemeScript(
      SkColorSetRGB(176, 58, 72), /*has_tint=*/true,
      /*native=*/false, 0xFFF2F2F7, SK_ColorBLACK, /*theme=*/"dracula",
      /*system_accent=*/std::nullopt);
  EXPECT_NE(js.find("startsWith('atrium-theme-')"), std::string::npos);
  EXPECT_NE(js.find("classList.remove(c)"), std::string::npos);
}

// A theme carries its own accent, so the brand class must not also be on or
// the two fight over --ov-accent and the winner is decided by file order.
TEST(OverlayThemeScript, AThemeTurnsTheBrandAccentClassOff) {
  const std::string js = OverlayThemeScript(
      SkColorSetRGB(176, 58, 72), /*has_tint=*/true,
      /*native=*/false, 0xFFF2F2F7, SK_ColorBLACK, /*theme=*/"gruvbox",
      /*system_accent=*/std::nullopt);
  EXPECT_NE(js.find("'atrium-overlay-accent-brand', false"),
            std::string::npos);
}

TEST(OverlayThemeScript, TheSystemAccentRidesTheRootAsProperties) {
  const std::string js = OverlayThemeScript(
      SkColorSetRGB(176, 58, 72), /*has_tint=*/true,
      /*native=*/false, 0xFFF2F2F7, SK_ColorBLACK, /*theme=*/"", SystemBlue());
  EXPECT_NE(js.find("'--atrium-system-accent', '#007AFF'"), std::string::npos);
  EXPECT_NE(js.find("'--atrium-system-ink', '5 21 38'"), std::string::npos);
  EXPECT_NE(js.find("'--atrium-system-paper', '#E7F2FF'"), std::string::npos);
  EXPECT_NE(js.find("'--atrium-system-soft', '#61ADFF'"), std::string::npos);
  EXPECT_NE(js.find("'--atrium-system-accent-ink', '#FFFFFF'"),
            std::string::npos);
  EXPECT_NE(js.find("'atrium-overlay-accent-system', true"), std::string::npos);
}

// The brand class pins --ov-accent to crimson at the same specificity as the
// system class, so leaving it on hands the accent to whichever rule sits later.
TEST(OverlayThemeScript, TheSystemAccentTurnsTheBrandAccentClassOff) {
  const std::string js = OverlayThemeScript(
      SkColorSetRGB(176, 58, 72), /*has_tint=*/true,
      /*native=*/false, 0xFFF2F2F7, SK_ColorBLACK, /*theme=*/"", SystemBlue());
  EXPECT_NE(js.find("'atrium-overlay-accent-brand', false"),
            std::string::npos);
}

// An inline property on the root outranks every class, so a leftover system
// value would keep painting over the brand book theme picked after it.
TEST(OverlayThemeScript, LeavingTheSystemAccentClearsItsProperties) {
  const std::string js = OverlayThemeScript(
      SkColorSetRGB(176, 58, 72), /*has_tint=*/true,
      /*native=*/false, 0xFFF2F2F7, SK_ColorBLACK, /*theme=*/"nord",
      /*system_accent=*/std::nullopt);
  EXPECT_NE(js.find("removeProperty('--atrium-system-accent')"),
            std::string::npos);
  EXPECT_NE(js.find("removeProperty('--atrium-system-ink')"),
            std::string::npos);
  EXPECT_NE(js.find("removeProperty('--atrium-system-paper')"),
            std::string::npos);
  EXPECT_NE(js.find("'atrium-overlay-accent-system', false"),
            std::string::npos);
}

TEST(OverlayThemeScript, TheSystemAccentAddsNoThemeClass) {
  const std::string js = OverlayThemeScript(
      SkColorSetRGB(176, 58, 72), /*has_tint=*/true,
      /*native=*/false, 0xFFF2F2F7, SK_ColorBLACK, /*theme=*/"", SystemBlue());
  EXPECT_EQ(js.find("classList.add('atrium-theme-"), std::string::npos);
}

// The core is the one derivation. A second copy in C++ is how the two drift.
TEST(OverlayAccentTokens, TheCoreDerivesThePaletteFromOneAccent) {
  const std::optional<OverlayAccentTokens> tokens =
      OverlayAccentTokensForHex("#007AFF", kNoMacosAccentIndex);
  ASSERT_TRUE(tokens.has_value());
  EXPECT_EQ(tokens->accent, "#007AFF");
  EXPECT_EQ(tokens->ink, "5 21 38");
  EXPECT_EQ(tokens->paper, "#E7F2FF");
  EXPECT_EQ(tokens->soft, "#61ADFF");
  EXPECT_EQ(tokens->accent_ink, "#FFFFFF");
}

TEST(OverlayAccentTokens, TwoMacosAccentsDeriveDifferentPapers) {
  const std::optional<OverlayAccentTokens> blue =
      OverlayAccentTokensForHex("#007AFF", kNoMacosAccentIndex);
  const std::optional<OverlayAccentTokens> pink =
      OverlayAccentTokensForHex("#F74F9E", kNoMacosAccentIndex);
  ASSERT_TRUE(blue.has_value() && pink.has_value());
  EXPECT_NE(blue->paper, pink->paper);
  EXPECT_NE(blue->ink, pink->ink);
}

// The hex a long-lived window reads is AppKit's answer from process start, so
// the live index has to beat it or the accent never moves without a relaunch.
TEST(OverlayAccentTokens, TheLiveIndexBeatsAStaleHex) {
  const std::optional<OverlayAccentTokens> tokens =
      OverlayAccentTokensForHex("#007AFF", /*accent_index=*/6);
  ASSERT_TRUE(tokens.has_value());
  EXPECT_EQ(tokens->accent, "#F74F9E");
}

// Multicolor writes no default, and AppKit paints its controls blue, so the
// hex the window read is the whole answer.
TEST(OverlayAccentTokens, NoIndexLeavesTheReadColourAlone) {
  const std::optional<OverlayAccentTokens> tokens =
      OverlayAccentTokensForHex("#007AFF", kNoMacosAccentIndex);
  ASSERT_TRUE(tokens.has_value());
  EXPECT_EQ(tokens->accent, "#007AFF");
}

}  // namespace atrium
