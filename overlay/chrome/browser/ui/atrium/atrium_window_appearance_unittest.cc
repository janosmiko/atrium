// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_window_appearance_unittest.cc
#include "chrome/browser/ui/atrium/atrium_window_appearance.h"

#include "chrome/browser/ui/atrium/atrium_corner_radius.h"
#include "chrome/browser/ui/atrium/atrium_window_corner_radius.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

// Partial objects throughout, for the reason the confirm-to-quit suite gives:
// this function is asked about four fields, and the whole-object rule belongs
// to the writer, which refuses a partial one.

TEST(AtriumWindowAppearanceTest, TheThreeFieldsAreReadOffAWholeObject) {
  const WindowAppearance appearance = WindowAppearanceFromSettings(
      R"({"sidebarPosition":"right","opaqueSurround":true,)"
      R"("surroundColor":"#12282E","glassOpacity":0.4})");
  EXPECT_EQ(appearance.sidebar_on_right, std::make_optional(true));
  EXPECT_EQ(appearance.opaque_surround, std::make_optional(true));
  EXPECT_EQ(appearance.surround_argb, std::make_optional(0x12282Eu));
  EXPECT_EQ(appearance.glass_alpha_percent, std::make_optional(40));
}

TEST(AtriumWindowAppearanceTest, BothSidebarSidesAreAnAnswer) {
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"sidebarPosition":"left"})")
                .sidebar_on_right,
            std::make_optional(false));
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"sidebarPosition":"right"})")
                .sidebar_on_right,
            std::make_optional(true));
}

// A third spelling reads LEFT, which is what the core's own one_of writes to
// disk for it. Answering "leave the window alone" instead would leave a window
// on the right while the file it just saved says left.
TEST(AtriumWindowAppearanceTest, AnUnknownSidebarSideReadsLeft) {
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"sidebarPosition":"top"})")
                .sidebar_on_right,
            std::make_optional(false));
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"sidebarPosition":"Right"})")
                .sidebar_on_right,
            std::make_optional(false));
}

// The retired "space" slug now reads as "brand" does: crimson, the accent
// "brand" pinned.
TEST(AtriumWindowAppearanceTest, ARetiredSpaceAccentReadsCrimson) {
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"overlayAccent":"space"})")
                .overlay_theme,
            std::make_optional(std::string("crimson")));
}

// A value that is not a string at all is a different case: the core's own
// FileShape refuses the whole file over it, so there is no normalised value to
// agree with and the window keeps the side it is on.
TEST(AtriumWindowAppearanceTest, ASidebarSideOfTheWrongTypeIsNotAnAnswer) {
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"sidebarPosition":true})")
                .sidebar_on_right,
            std::nullopt);
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"sidebarPosition":null})")
                .sidebar_on_right,
            std::nullopt);
}

// Every field absent, which is what a settings file from a build that predates
// them looks like. A window must stay where it is rather than be moved to a
// default this reader invented.
TEST(AtriumWindowAppearanceTest, AnAbsentFieldIsNotADefault) {
  const WindowAppearance appearance =
      WindowAppearanceFromSettings(R"({"confirmToQuitEnabled":true})");
  EXPECT_EQ(appearance.sidebar_on_right, std::nullopt);
  EXPECT_EQ(appearance.opaque_surround, std::nullopt);
  EXPECT_EQ(appearance.surround_argb, std::nullopt);
  EXPECT_EQ(appearance.glass_alpha_percent, std::nullopt);
  EXPECT_EQ(appearance.sidebar_width, std::nullopt);
}

// The width is not in this document at all, so the settings reader never
// answers for it - not even when the object carries the key. A settings write
// that appeared to answer would push a width the store does not hold.
TEST(AtriumWindowAppearanceTest, TheSettingsReaderNeverAnswersForTheWidth) {
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"sidebarWidth":380})").sidebar_width,
            std::nullopt);
}

TEST(AtriumWindowAppearanceTest, NothingUsableYieldsNoAnswer) {
  EXPECT_EQ(WindowAppearanceFromSettings("").glass_alpha_percent, std::nullopt);
  EXPECT_EQ(WindowAppearanceFromSettings("not json").opaque_surround,
            std::nullopt);
  EXPECT_EQ(WindowAppearanceFromSettings("[1,2,3]").sidebar_on_right,
            std::nullopt);
}

// The store holds 0.02..1.0 and the applier takes 0..100.
TEST(AtriumWindowAppearanceTest, GlassOpacityBecomesAPercent) {
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"glassOpacity":0.55})")
                .glass_alpha_percent,
            std::make_optional(55));
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"glassOpacity":1.0})")
                .glass_alpha_percent,
            std::make_optional(100));
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"glassOpacity":0.02})")
                .glass_alpha_percent,
            std::make_optional(2));
  // A whole number is legal JSON for this field and the store's own writer
  // never spells one, so only a hand-edited file gets here.
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"glassOpacity":1})")
                .glass_alpha_percent,
            std::make_optional(100));
}

// The store's own 4..10, and the applier takes the same
// points.
TEST(AtriumWindowAppearanceTest, BorderWidthComesThroughInPoints) {
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"borderWidth":10})").border_width,
            std::make_optional(10));
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"borderWidth":4})").border_width,
            std::make_optional(4));
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"borderWidth":10})").border_width,
            std::make_optional(10));
  // The settings surface sends numbers back as doubles, so the reader has to
  // take 12.0 as 12 rather than refuse it the way FindInt would.
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"borderWidth":12.0})").border_width,
            std::make_optional(12));
}

// The same posture glassOpacity takes below: the nearest width this build can
// paint beats not painting the setting at all.
TEST(AtriumWindowAppearanceTest, ABorderWidthOutsideTheRangeIsClampedNotDropped) {
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"borderWidth":40})").border_width,
            std::make_optional(10));
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"borderWidth":0})").border_width,
            std::make_optional(4));
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"borderWidth":-6})").border_width,
            std::make_optional(4));
}

// Asked through the parse rather than the constant, so
// raising the maximum past the radius floor fails here instead of shipping a
// gap that pinches at the corner.
TEST(AtriumWindowAppearanceTest, TheWidestBorderStillLeavesAConcentricCorner) {
  const std::optional<int> widest =
      WindowAppearanceFromSettings(R"({"borderWidth":9999})").border_width;
  ASSERT_TRUE(widest.has_value());
  EXPECT_FLOAT_EQ(kWindowCornerRadius - *widest,
                  ConcentricCornerRadius(kWindowCornerRadius, *widest))
      << "the maximum border width is past the radius floor, so the gap "
         "pinches around the corner at " << *widest;
  // The presence half: one point wider is where it stops being concentric.
  EXPECT_NE(kWindowCornerRadius - (*widest + 1),
            ConcentricCornerRadius(kWindowCornerRadius, *widest + 1));
}

TEST(AtriumWindowAppearanceTest, ABorderWidthOfTheWrongTypeIsNotAnAnswer) {
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"borderWidth":"10"})").border_width,
            std::nullopt);
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"borderWidth":null})").border_width,
            std::nullopt);
  EXPECT_EQ(WindowAppearanceFromSettings("{}").border_width, std::nullopt);
}

// A later build may widen the range. The file keeps that value - the core
// preserves it verbatim - and this build paints the nearest thing it can,
// which is the edge of its own range rather than nothing.
TEST(AtriumWindowAppearanceTest, AnOpacityFromALaterBuildIsClampedNotDropped) {
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"glassOpacity":5.0})")
                .glass_alpha_percent,
            std::make_optional(100));
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"glassOpacity":-3})")
                .glass_alpha_percent,
            std::make_optional(2));
}

TEST(AtriumWindowAppearanceTest, AnOpacityOfTheWrongTypeIsNotAnAnswer) {
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"glassOpacity":"0.4"})")
                .glass_alpha_percent,
            std::nullopt);
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"glassOpacity":null})")
                .glass_alpha_percent,
            std::nullopt);
}

// Absent, malformed and a spelling from a later build all mean AUTOMATIC,
// which is a real stored value. The mode still applies - the window resolves
// the neutral itself.
TEST(AtriumWindowAppearanceTest, AnUnusableSurroundColourStillLeavesTheMode) {
  for (const char* json : {
           R"({"opaqueSurround":true})",
           R"({"opaqueSurround":true,"surroundColor":"red"})",
           R"({"opaqueSurround":true,"surroundColor":"#12282"})",
           // A custom delimiter: the value's own ")" would end a plain R"(...)".
           R"json({"opaqueSurround":true,"surroundColor":"oklch(0.2 0.05 210)"})json",
           R"({"opaqueSurround":true,"surroundColor":null})",
       }) {
    const WindowAppearance appearance = WindowAppearanceFromSettings(json);
    EXPECT_EQ(appearance.opaque_surround, std::make_optional(true)) << json;
    EXPECT_EQ(appearance.surround_argb, std::nullopt) << json;
  }
}

// The top byte is dropped here rather than passed on: SetOpaqueSurround forces
// SK_AlphaOPAQUE anyway, and a surround that let the glass through would stop
// being a surround.
TEST(AtriumWindowAppearanceTest, TheSurroundColourCarriesNoAlpha) {
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"surroundColor":"#FFFFFF"})")
                .surround_argb,
            std::make_optional(0x00FFFFFFu));
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"surroundColor":"#000000"})")
                .surround_argb,
            std::make_optional(0x00000000u));
}

// normalized_surround_color trims before it validates, so a padded colour is
// one the store keeps and this must not read as automatic.
TEST(AtriumWindowAppearanceTest, ASurroundColourIsTrimmedBeforeItIsJudged) {
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"surroundColor":" #12282E "})")
                .surround_argb,
            std::make_optional(0x12282Eu));
}

TEST(AtriumWindowAppearanceTest, ASurroundMayBeTurnedOff) {
  EXPECT_EQ(WindowAppearanceFromSettings(R"({"opaqueSurround":false})")
                .opaque_surround,
            std::make_optional(false));
}

// the sidebar's width, which is the one window value the SPACES
// document holds.

// Both spellings, because both occur: the fixtures on disk write a whole
// number and the core's own serializer writes an f64.
TEST(AtriumWindowAppearanceTest, TheWidthIsReadOffAWholeSpacesDocument) {
  EXPECT_EQ(SidebarWidthFromSpaces(
                R"({"spaces":[],"appState":{"sidebarWidth":380,)"
                R"("schemaVersion":3}})"),
            std::make_optional(380.0));
  EXPECT_EQ(SidebarWidthFromSpaces(
                R"({"spaces":[],"appState":{"sidebarWidth":380.0,)"
                R"("schemaVersion":3}})"),
            std::make_optional(380.0));
}

// A document with no appState, and one whose appState has no width. Neither is
// a default - a window must stay at the width it was built with.
TEST(AtriumWindowAppearanceTest, AnAbsentWidthIsNotADefault) {
  EXPECT_EQ(SidebarWidthFromSpaces(R"({"spaces":[]})"), std::nullopt);
  EXPECT_EQ(SidebarWidthFromSpaces(R"({"appState":{"schemaVersion":3}})"),
            std::nullopt);
}

// The trap this field has and the other four do not. Nothing in this build
// writes appState.sidebarWidth, so a 0 is a Swift session that quit collapsed;
// seeding it would open every window with no sidebar and, docked left, no
// traffic lights either.
TEST(AtriumWindowAppearanceTest, ACollapsedWidthIsNotAnAnswer) {
  for (const char* json : {
           R"({"appState":{"sidebarWidth":0}})",
           R"({"appState":{"sidebarWidth":0.0}})",
           // Under the window's own collapse threshold: it would read this as
           // collapse too, so admitting it here would resize to 160 and then
           // collapse anyway.
           R"({"appState":{"sidebarWidth":0.4}})",
           R"({"appState":{"sidebarWidth":-40}})",
       }) {
    EXPECT_EQ(SidebarWidthFromSpaces(json), std::nullopt) << json;
  }
}

// Same terms as glassOpacity: a build with a wider range keeps its value in the
// file, and this one opens at the nearest width it can actually take.
TEST(AtriumWindowAppearanceTest, AWidthOutsideTheRangeIsClampedNotDropped) {
  EXPECT_EQ(SidebarWidthFromSpaces(R"({"appState":{"sidebarWidth":900}})"),
            std::make_optional(500.0));
  EXPECT_EQ(SidebarWidthFromSpaces(R"({"appState":{"sidebarWidth":80}})"),
            std::make_optional(160.0));
}

TEST(AtriumWindowAppearanceTest, AWidthOfTheWrongTypeIsNotAnAnswer) {
  EXPECT_EQ(SidebarWidthFromSpaces(R"({"appState":{"sidebarWidth":"380"}})"),
            std::nullopt);
  EXPECT_EQ(SidebarWidthFromSpaces(R"({"appState":{"sidebarWidth":null}})"),
            std::nullopt);
  EXPECT_EQ(SidebarWidthFromSpaces(R"({"appState":{"sidebarWidth":true}})"),
            std::nullopt);
  EXPECT_EQ(SidebarWidthFromSpaces(R"({"appState":[380]})"), std::nullopt);
}

// An absent or refused spaces file reaches this as the empty string, which is
// what LoadSpacesJSON answers when the core will not read one.
TEST(AtriumWindowAppearanceTest, NothingUsableYieldsNoWidth) {
  EXPECT_EQ(SidebarWidthFromSpaces(""), std::nullopt);
  EXPECT_EQ(SidebarWidthFromSpaces("not json"), std::nullopt);
  EXPECT_EQ(SidebarWidthFromSpaces("[1,2,3]"), std::nullopt);
}

// This is the exact guard ApplyWindowAppearance runs before PushAiPanelInk -
// a false answer here is a repaint the fan-out must not run.
TEST(AtriumWindowAppearanceTest, TheSameThemeDoesNotDiffer) {
  EXPECT_FALSE(OverlayAccentDiffers("crimson", "crimson"));
  EXPECT_FALSE(OverlayAccentDiffers("", ""));
}

TEST(AtriumWindowAppearanceTest, AChangedThemeDiffers) {
  EXPECT_TRUE(OverlayAccentDiffers("dracula", "crimson"));
}

}  // namespace
}  // namespace atrium
