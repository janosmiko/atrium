// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_page_color.h"

#include "base/strings/stringprintf.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/image/image.h"

namespace atrium {
namespace {

gfx::Image IconOf(SkColor color) {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(32, 32);
  bitmap.eraseColor(color);
  return gfx::Image::CreateFrom1xBitmap(bitmap);
}

TEST(AtriumPageColorTest, AnEmptyImageHasNoColour) {
  EXPECT_FALSE(DominantColorOfFavicon(gfx::Image()).has_value());
}

TEST(AtriumPageColorTest, ASolidMarkReadsAsItsOwnColour) {
  const std::optional<SkColor> color = DominantColorOfFavicon(IconOf(
      SkColorSetRGB(0x1E, 0x5A, 0xA8)));
  ASSERT_TRUE(color.has_value());
  EXPECT_EQ(*color, SkColorSetARGB(0xFF, 0x1E, 0x5A, 0xA8));
}

// The k-means call answers with its heaviest cluster even when none fall in
// the useful band, so without the re-read every white mark on the web would
// drag its chrome the same way - and that is a lot of marks.
TEST(AtriumPageColorTest, AMonochromeMarkIsRefused) {
  EXPECT_FALSE(DominantColorOfFavicon(IconOf(SK_ColorWHITE)).has_value());
  EXPECT_FALSE(DominantColorOfFavicon(IconOf(SK_ColorBLACK)).has_value());
}

// A mark that is mostly white with a coloured quarter is the common real
// shape, and the band is there so the quarter wins.
TEST(AtriumPageColorTest, AColouredQuarterBeatsTheWhiteAroundIt) {
  SkBitmap bitmap;
  bitmap.allocN32Pixels(32, 32);
  bitmap.eraseColor(SK_ColorWHITE);
  for (int y = 0; y < 16; ++y) {
    for (int x = 0; x < 16; ++x) {
      *bitmap.getAddr32(x, y) = SkPreMultiplyColor(
          SkColorSetRGB(0xE0, 0x78, 0x20));
    }
  }
  const std::optional<SkColor> color =
      DominantColorOfFavicon(gfx::Image::CreateFrom1xBitmap(bitmap));
  ASSERT_TRUE(color.has_value());
  EXPECT_EQ(*color, SkColorSetARGB(0xFF, 0xE0, 0x78, 0x20));
}

TEST(InkForBackdropTest, WhiteStaysOnADarkOrMidBackdrop) {
  EXPECT_EQ(InkForBackdrop(SkColorSetRGB(0x20, 0x24, 0x30)), SK_ColorWHITE);
  EXPECT_EQ(InkForBackdrop(SkColorSetRGB(0x60, 0x40, 0x90)), SK_ColorWHITE);
}

TEST(InkForBackdropTest, ALightBackdropTurnsTheInkBlack) {
  EXPECT_EQ(InkForBackdrop(SkColorSetRGB(0xF2, 0xF2, 0xF7)), SK_ColorBLACK);
  EXPECT_EQ(InkForBackdrop(SkColorSetRGB(0xE0, 0xE8, 0xFF)), SK_ColorBLACK);
}

TEST(InkForBackdropTest, TheAlphaOfTheBackdropIsIgnored) {
  EXPECT_EQ(InkForBackdrop(SkColorSetARGB(0x40, 0xF2, 0xF2, 0xF7)),
            SK_ColorBLACK);
}

// The shipped glass, and the light appearance's neutral.
constexpr SkAlpha kGlass = 0xB8;
constexpr SkColor kLightBase = SkColorSetRGB(0xF2, 0xF2, 0xF7);

// A light space tint graded against white ink refuses every page shift, so the
// ambilight has to ask for the ink this answers.
TEST(TintOverGlassTest, ALightTintTakesBlackInk) {
  EXPECT_EQ(InkForBackdrop(TintOverGlass(SkColorSetRGB(0xF0, 0xF4, 0xF8),
                                         kGlass, kLightBase)),
            SK_ColorBLACK);
}

TEST(TintOverGlassTest, ADarkTintKeepsWhiteInk) {
  EXPECT_EQ(InkForBackdrop(TintOverGlass(SkColorSetRGB(0x12, 0x28, 0x2E),
                                         kGlass, kLightBase)),
            SK_ColorWHITE);
}

// Thin glass hands most of the surface to the appearance, so an ink read off
// the tint alone would be the wrong one.
TEST(TintOverGlassTest, ThinGlassLetsTheAppearanceDecide) {
  EXPECT_EQ(InkForBackdrop(TintOverGlass(SkColorSetRGB(0xF0, 0xF4, 0xF8), 0x40,
                                         SkColorSetRGB(0x1E, 0x1E, 0x1E))),
            SK_ColorWHITE);
}

// The core grades the drift on the TINT, so an ink the tint itself cannot
// clear refuses every shift and the band paints the raw tint.
constexpr double kMinChromeContrast = 3.0;

TEST(InkForSpaceTintTest, EveryPresetKeepsAnInkItsOwnTintCanClear) {
  for (SkColor tint :
       {SkColorSetRGB(0x12, 0x28, 0x2E), SkColorSetRGB(0x2E, 0x12, 0x19),
        SkColorSetRGB(0x12, 0x2E, 0x27), SkColorSetRGB(0x0F, 0x2B, 0x46),
        SkColorSetRGB(0x5A, 0x2E, 0x0D), SkColorSetRGB(0x3A, 0x1A, 0x42),
        SkColorSetRGB(0x25, 0x28, 0x2C), SkColorSetRGB(0xF0, 0xF4, 0xF8),
        SkColorSetRGB(0xDC, 0xE3, 0xEA), SkColorSetRGB(0x80, 0x80, 0x80)}) {
    SCOPED_TRACE(base::StringPrintf("tint %06X", tint & 0xFFFFFF));
    EXPECT_GE(color_utils::GetContrastRatio(InkForSpaceTint(tint), tint),
              kMinChromeContrast);
  }
}

// The glass composite the sidebar's ink follows is exactly what this must not
// use: at the shipped 0.2 glass a light appearance turns #12282E into #C5CACF,
// whose ink is black, and black clears 1.37 against #12282E.
TEST(InkForSpaceTintTest, ADarkPresetTakesWhiteAndALightTintBlack) {
  const SkColor dark = SkColorSetRGB(0x12, 0x28, 0x2E);
  EXPECT_EQ(InkForSpaceTint(dark), SK_ColorWHITE);
  EXPECT_EQ(InkForBackdrop(TintOverGlass(dark, 0x33, kLightBase)),
            SK_ColorBLACK);
  EXPECT_EQ(InkForSpaceTint(SkColorSetRGB(0x0F, 0x2B, 0x46)), SK_ColorWHITE);
  EXPECT_EQ(InkForSpaceTint(SkColorSetRGB(0xF0, 0xF4, 0xF8)), SK_ColorBLACK);
}

// rust/atrium-core/src/tint.rs's ENTRIES, in its own order, plus the light
// custom tint the fixtures use - the whole range a window can be handed.
constexpr SkColor kShippedTints[] = {
    SkColorSetRGB(0x12, 0x28, 0x2E), SkColorSetRGB(0x2E, 0x12, 0x19),
    SkColorSetRGB(0x12, 0x2E, 0x27), SkColorSetRGB(0x0F, 0x2B, 0x46),
    SkColorSetRGB(0x5A, 0x2E, 0x0D), SkColorSetRGB(0x3A, 0x1A, 0x42),
    SkColorSetRGB(0x25, 0x28, 0x2C), SkColorSetRGB(0xF0, 0xF4, 0xF8)};

TEST(EffectiveSpaceTintTest, AnOrdinaryWindowKeepsTheTintItWasHanded) {
  for (SkColor tint : kShippedTints) {
    SCOPED_TRACE(base::StringPrintf("tint %06X", tint & 0xFFFFFF));
    EXPECT_EQ(EffectiveSpaceTint(/*incognito=*/false, tint), tint);
  }
}

TEST(EffectiveSpaceTintTest, AnIncognitoWindowTakesTheDarkTintOnEveryTheme) {
  for (SkColor tint : kShippedTints) {
    SCOPED_TRACE(base::StringPrintf("tint %06X", tint & 0xFFFFFF));
    EXPECT_EQ(EffectiveSpaceTint(/*incognito=*/true, tint), kIncognitoTint);
  }
}

// The page-drift path hands this the colour the CORE mixed, and the core mixes
// over the active space's tint - the window next door's colour, carrying what
// this window's page looks like. Whatever comes back, it loses.
TEST(EffectiveSpaceTintTest, NoMixedPageColourMovesAnIncognitoWindow) {
  for (SkColor shifted :
       {SkColorSetRGB(0xC8, 0x1E, 0x3C), SkColorSetRGB(0xFF, 0xFF, 0xFF),
        SkColorSetRGB(0x00, 0x00, 0x00), SkColorSetRGB(0x1D, 0x9B, 0xF0),
        SkColorSetRGB(0x4A, 0x30, 0x3E)}) {
    SCOPED_TRACE(base::StringPrintf("shifted %06X", shifted & 0xFFFFFF));
    EXPECT_EQ(EffectiveSpaceTint(/*incognito=*/true, shifted), kIncognitoTint);
  }
}

// The reason the constant is this dark rather than merely dark: an incognito
// window has to be unmistakable beside any space, and a tint that only ties
// with the darkest preset is one a glance can confuse with it.
TEST(EffectiveSpaceTintTest, TheIncognitoTintIsDarkerThanEveryTheme) {
  for (SkColor tint : kShippedTints) {
    SCOPED_TRACE(base::StringPrintf("tint %06X", tint & 0xFFFFFF));
    EXPECT_LT(color_utils::GetRelativeLuminance(kIncognitoTint),
              color_utils::GetRelativeLuminance(tint));
  }
}

// Measured: at the shipped 0.55 glass the light appearance base reads 242, so
// #14121A washed out to a mid grey and PushSidebarInk graded its ink BLACK.
// The surface must BE the tint for the ink to be the tint's.
TEST(EffectiveGlassAlphaTest, TranslucentGlassWouldWashTheIncognitoTintOut) {
  constexpr SkAlpha kShippedGlass = 140;  // 0.55, the settings default
  EXPECT_EQ(InkForBackdrop(
                TintOverGlass(kIncognitoTint, kShippedGlass, kLightBase)),
            SK_ColorBLACK);

  const SkAlpha effective =
      EffectiveGlassAlpha(/*incognito=*/true, kShippedGlass);
  EXPECT_EQ(effective, SK_AlphaOPAQUE);
  EXPECT_EQ(TintOverGlass(kIncognitoTint, effective, kLightBase),
            SkColorSetA(kIncognitoTint, SK_AlphaOPAQUE));
  EXPECT_EQ(InkForBackdrop(TintOverGlass(kIncognitoTint, effective, kLightBase)),
            InkForSpaceTint(kIncognitoTint));
}

TEST(EffectiveGlassAlphaTest, AnOrdinaryWindowKeepsTheGlassTheUserPicked) {
  for (SkAlpha glass : {SkAlpha{0}, SkAlpha{5}, SkAlpha{140}, SkAlpha{255}}) {
    EXPECT_EQ(EffectiveGlassAlpha(/*incognito=*/false, glass), glass);
  }
}

TEST(InkForSpaceTintTest, TheIncognitoTintTakesTheSameWhiteInkAsADarkPreset) {
  EXPECT_EQ(InkForSpaceTint(kIncognitoTint), SK_ColorWHITE);
  EXPECT_GE(
      color_utils::GetContrastRatio(InkForSpaceTint(kIncognitoTint),
                                    kIncognitoTint),
      kMinChromeContrast);
}

}  // namespace
}  // namespace atrium
