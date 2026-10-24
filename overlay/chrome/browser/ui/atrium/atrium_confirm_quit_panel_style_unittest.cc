// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_confirm_quit_panel_style_unittest.cc
#include "chrome/browser/ui/atrium/atrium_confirm_quit_panel_style.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "ui/gfx/color_utils.h"

namespace atrium {
namespace {

constexpr double kMinChromeContrast = 3.0;

TEST(ConfirmQuitPanelStyleTest, BrandModeUsesTheDocumentedInkPair) {
  EXPECT_EQ(ComputeConfirmQuitPanelStyle(/*dark=*/false, /*native=*/false,
                                         std::nullopt)
                .ink,
            SkColorSetRGB(0x22, 0x10, 0x16));
  EXPECT_EQ(ComputeConfirmQuitPanelStyle(/*dark=*/true, /*native=*/false,
                                         std::nullopt)
                .ink,
            SkColorSetRGB(0xF9, 0xF0, 0xF1));
}

TEST(ConfirmQuitPanelStyleTest, BrandModeUsesTheBrandCardRadius) {
  EXPECT_EQ(ComputeConfirmQuitPanelStyle(false, false, std::nullopt)
                .corner_radius,
            16.0);
}

TEST(ConfirmQuitPanelStyleTest, BrandKeycapIsTintedAndReadableOnTheSurface) {
  const ConfirmQuitPanelStyle style =
      ComputeConfirmQuitPanelStyle(false, false, std::nullopt);
  EXPECT_NE(style.key_background, style.surface);
  EXPECT_GE(color_utils::GetContrastRatio(style.ink, style.surface),
            kMinChromeContrast);
}

// The native look used to drop the keycap chips, and "Hold Cmd Q to Quit"
// then read as one sentence with no keys in it.
TEST(ConfirmQuitPanelStyleTest, NativeModeKeepsTheKeycapChipsInTheInk) {
  for (bool dark : {false, true}) {
    const ConfirmQuitPanelStyle style =
        ComputeConfirmQuitPanelStyle(dark, true, std::nullopt);
    EXPECT_NE(SkColorGetA(style.key_background), 0u);
    EXPECT_NE(SkColorGetA(style.key_underline), 0u);
    EXPECT_EQ(SkColorSetA(style.key_background, SK_AlphaOPAQUE), style.ink);
    EXPECT_GT(style.key_padding_horizontal, 1.0);
  }
  EXPECT_EQ(ComputeConfirmQuitPanelStyle(false, true, std::nullopt)
                .corner_radius,
            12.0);
}

TEST(ConfirmQuitPanelStyleTest, NativeModeReadsTheSurroundColourAsTheSurface) {
  constexpr SkColor kCustomSurround = SkColorSetRGB(0x10, 0x22, 0x30);
  EXPECT_EQ(ComputeConfirmQuitPanelStyle(false, true, kCustomSurround)
                .surface,
            SkColorSetA(kCustomSurround, SK_AlphaOPAQUE));
}

TEST(ConfirmQuitPanelStyleTest, NativeModeFallsBackToANeutralWhenAutomatic) {
  EXPECT_NE(ComputeConfirmQuitPanelStyle(false, true, std::nullopt).surface,
            ComputeConfirmQuitPanelStyle(true, true, std::nullopt).surface);
}

TEST(ConfirmQuitPanelStyleTest, NativeInkAlwaysClearsTheSurfaceItSitsOn) {
  for (bool dark : {false, true}) {
    const ConfirmQuitPanelStyle style =
        ComputeConfirmQuitPanelStyle(dark, true, std::nullopt);
    EXPECT_GE(color_utils::GetContrastRatio(style.ink, style.surface),
              kMinChromeContrast);
  }
}

// The peek card's origin chip and placeholder draw this ink on this surface
// at text size, the way a toast does, so the pair needs body-text contrast.
TEST(ConfirmQuitPanelStyleTest, BrandInkReadsAsTextOnTheSurfaceInBothModes) {
  constexpr double kMinTextContrast = 4.5;
  for (bool dark : {false, true}) {
    const ConfirmQuitPanelStyle style =
        ComputeConfirmQuitPanelStyle(dark, false, std::nullopt);
    EXPECT_GE(color_utils::GetContrastRatio(style.ink, style.surface),
              kMinTextContrast)
        << (dark ? "dark" : "light");
  }
}

TEST(ConfirmQuitPanelStyleTest, AnAbsentThemeFallsBackToBrandNotBlack) {
  const ConfirmQuitPanelStyle explicit_nullopt = ComputeConfirmQuitPanelStyle(
      /*dark=*/false, /*native=*/false, std::nullopt, std::nullopt);
  const ConfirmQuitPanelStyle default_arg = ComputeConfirmQuitPanelStyle(
      /*dark=*/false, /*native=*/false, std::nullopt);
  EXPECT_EQ(explicit_nullopt.surface, default_arg.surface);
}

// The HUD ignored the chosen overlay theme and always mixed the brand
// crimson. A theme brings its own four colours, and they all have to land.
TEST(ConfirmQuitPanelStyleTest, AThemePaintsItsOwnPaperInkAndSoft) {
  const OverlayThemePalette dracula = *BrandBookThemePalette("dracula");
  const ConfirmQuitPanelStyle style = ComputeConfirmQuitPanelStyle(
      /*dark=*/false, /*native=*/false, std::nullopt, dracula);
  EXPECT_EQ(style.surface, dracula.paper);
  EXPECT_EQ(style.ink, dracula.ink);
  EXPECT_EQ(style.key_underline, dracula.soft);
  EXPECT_NE(style.key_background, style.surface);
  EXPECT_NE(style.surface,
            ComputeConfirmQuitPanelStyle(false, false, std::nullopt).surface);
}

// overlay.css's theme class outranks the dark-mode :root block, so a theme's
// paper is the paper in both appearances.
TEST(ConfirmQuitPanelStyleTest, AThemeKeepsItsPaperInDarkMode) {
  const OverlayThemePalette lime = *BrandBookThemePalette("lime");
  EXPECT_EQ(ComputeConfirmQuitPanelStyle(true, false, std::nullopt, lime)
                .surface,
            lime.paper);
}

TEST(ConfirmQuitPanelStyleTest, ATranslucentThemeColourIsForcedOpaque) {
  OverlayThemePalette theme = *BrandBookThemePalette("cyan");
  theme.paper = SkColorSetA(theme.paper, 0x40);
  EXPECT_EQ(SkColorGetA(ComputeConfirmQuitPanelStyle(false, false,
                                                     std::nullopt, theme)
                            .surface),
            SK_AlphaOPAQUE);
}

TEST(SplitConfirmQuitShortcutCapsTest, ModifierGlyphsAndALetterEachSplit) {
  EXPECT_EQ(SplitConfirmQuitShortcutCaps(u"⇧⌘Z"),
            std::vector<std::u16string>({u"⇧", u"⌘", u"Z"}));
}

TEST(SplitConfirmQuitShortcutCapsTest, TheOrdinaryQuitShortcutIsTwoCaps) {
  EXPECT_EQ(SplitConfirmQuitShortcutCaps(u"⌘Q"),
            std::vector<std::u16string>({u"⌘", u"Q"}));
}

TEST(SplitConfirmQuitShortcutCapsTest, AllFourModifiersSplit) {
  EXPECT_EQ(SplitConfirmQuitShortcutCaps(u"⌃⌥⇧⌘T"),
            std::vector<std::u16string>({u"⌃", u"⌥", u"⇧", u"⌘", u"T"}));
}

TEST(SplitConfirmQuitShortcutCapsTest, AnEmptyShortcutYieldsNoCaps) {
  EXPECT_TRUE(SplitConfirmQuitShortcutCaps(u"").empty());
}

TEST(SplitConfirmQuitShortcutCapsTest, AllModifiersNoKeyYieldsOnlyCaps) {
  EXPECT_EQ(SplitConfirmQuitShortcutCaps(u"⌘"),
            std::vector<std::u16string>({u"⌘"}));
}

// generated_resources.grd's IDS_CONFIRM_TO_QUIT_DESCRIPTION, verbatim.
constexpr std::u16string_view kDescriptionTemplate = u"Hold $1 to Quit";

TEST(SplitConfirmQuitLabelTest, TheShippingStringKeepsTheWordsOnBothSides) {
  const ConfirmQuitLabelParts parts =
      SplitConfirmQuitLabel(kDescriptionTemplate, u"⌘Q");
  EXPECT_EQ(parts.prefix, u"Hold ");
  EXPECT_EQ(parts.caps, std::vector<std::u16string>({u"⌘", u"Q"}));
  EXPECT_EQ(parts.suffix, u" to Quit");
}

TEST(SplitConfirmQuitLabelTest, ALeadingShortcutLeavesAnEmptyPrefix) {
  const ConfirmQuitLabelParts parts =
      SplitConfirmQuitLabel(u"$1 gedrückt halten zum Beenden", u"⌘Q");
  EXPECT_EQ(parts.prefix, u"");
  EXPECT_EQ(parts.suffix, u" gedrückt halten zum Beenden");
}

TEST(SplitConfirmQuitLabelTest, ATrailingShortcutLeavesAnEmptySuffix) {
  const ConfirmQuitLabelParts parts =
      SplitConfirmQuitLabel(u"Kilépéshez tartsa lenyomva: $1", u"⌘Q");
  EXPECT_EQ(parts.prefix, u"Kilépéshez tartsa lenyomva: ");
  EXPECT_EQ(parts.suffix, u"");
}

TEST(SplitConfirmQuitLabelTest, AMultiModifierShortcutStillSplitsTheSentence) {
  const ConfirmQuitLabelParts parts =
      SplitConfirmQuitLabel(kDescriptionTemplate, u"⇧⌘Q");
  EXPECT_EQ(parts.prefix, u"Hold ");
  EXPECT_EQ(parts.caps, std::vector<std::u16string>({u"⇧", u"⌘", u"Q"}));
  EXPECT_EQ(parts.suffix, u" to Quit");
}

TEST(SplitConfirmQuitLabelTest, ATemplateWithoutAPlaceholderKeepsItsWords) {
  const ConfirmQuitLabelParts parts =
      SplitConfirmQuitLabel(u"Hold to Quit", u"⌘Q");
  EXPECT_EQ(parts.prefix, u"Hold to Quit");
  EXPECT_EQ(parts.suffix, u"");
}

}  // namespace
}  // namespace atrium
