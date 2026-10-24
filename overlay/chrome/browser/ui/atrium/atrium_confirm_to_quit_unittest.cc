// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_confirm_to_quit_unittest.cc
#include "chrome/browser/ui/atrium/atrium_confirm_to_quit.h"

#include "base/test/task_environment.h"
#include "chrome/common/pref_names.h"
#include "chrome/test/base/testing_browser_process.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkColor.h"

namespace atrium {
namespace {

// The two readings that reach the pref. Spelled as partial objects because
// this function is asked about ONE field - the whole-object rule belongs to
// the writer, which refuses a partial one (atrium_core_abi_unittest).
TEST(AtriumConfirmToQuitTest, TheFieldIsReadBothWaysRound) {
  EXPECT_EQ(ConfirmToQuitFromSettings(R"({"confirmToQuitEnabled":true})"),
            std::make_optional(true));
  EXPECT_EQ(ConfirmToQuitFromSettings(R"({"confirmToQuitEnabled":false})"),
            std::make_optional(false));
}

// A settings file written by a build that predates the field, which is every
// key past the first four. The caller must leave the pref alone rather than
// read absence as false - the app menu's own toggle may have set it, and
// overwriting that would make the menu item stop meaning anything.
TEST(AtriumConfirmToQuitTest, AnAbsentFieldIsNotFalse) {
  EXPECT_EQ(ConfirmToQuitFromSettings(R"({"glassOpacity":0.55})"),
            std::nullopt);
}

// The reader answers an unreadable file with defaults rather than an error, so
// an empty string does reach here.
TEST(AtriumConfirmToQuitTest, NothingUsableYieldsNoAnswer) {
  EXPECT_EQ(ConfirmToQuitFromSettings(""), std::nullopt);
  EXPECT_EQ(ConfirmToQuitFromSettings("not json"), std::nullopt);
  EXPECT_EQ(ConfirmToQuitFromSettings("[1,2,3]"), std::nullopt);
}

// A file from a later build could carry any shape here. Turning the quit gate
// on for a value that is not a bool would be a guess, and this one is felt
// every time the user presses Cmd+Q.
TEST(AtriumConfirmToQuitTest, AFieldOfTheWrongTypeIsNotAnAnswer) {
  EXPECT_EQ(ConfirmToQuitFromSettings(R"({"confirmToQuitEnabled":"true"})"),
            std::nullopt);
  EXPECT_EQ(ConfirmToQuitFromSettings(R"({"confirmToQuitEnabled":1})"),
            std::nullopt);
  EXPECT_EQ(ConfirmToQuitFromSettings(R"({"confirmToQuitEnabled":null})"),
            std::nullopt);
}

// Asserts the pref CurrentPanelStyle actually reads, not a parsed value.
class AtriumConfirmQuitPanelPrefTest : public ::testing::Test {
 public:
  PrefService* local_state() {
    return TestingBrowserProcess::GetGlobal()->local_state();
  }

  int SurroundColorPref() {
    return local_state()->GetInteger(
        prefs::kAtriumConfirmQuitPanelSurroundColor);
  }

 private:
  content::BrowserTaskEnvironment task_environment_{
      base::test::TaskEnvironment::MainThreadType::UI};
};

// CurrentPanelStyle reads this stored value back the same way the real
// reader does: negative means AUTOMATIC, otherwise the bits are an SkColor
// with ComputeConfirmQuitPanelStyle forcing it back to opaque.
SkColor DecodeStoredPanelSurroundColor(int stored) {
  return SkColorSetA(static_cast<SkColor>(stored), SK_AlphaOPAQUE);
}

TEST_F(AtriumConfirmQuitPanelPrefTest, AnOpaqueColorRoundTripsToItself) {
  const SkColor color = SkColorSetARGB(0xFF, 0x11, 0x22, 0x33);
  SetConfirmQuitPanelAppearance(std::nullopt, color);
  EXPECT_GE(SurroundColorPref(), 0);
  EXPECT_EQ(DecodeStoredPanelSurroundColor(SurroundColorPref()), color);
}

TEST_F(AtriumConfirmQuitPanelPrefTest, WhiteDoesNotCollideWithAutomatic) {
  SetConfirmQuitPanelAppearance(std::nullopt, SK_ColorWHITE);
  EXPECT_GE(SurroundColorPref(), 0);
  EXPECT_EQ(DecodeStoredPanelSurroundColor(SurroundColorPref()),
            SK_ColorWHITE);
}

TEST_F(AtriumConfirmQuitPanelPrefTest, NulloptStoresTheAutomaticSentinel) {
  SetConfirmQuitPanelAppearance(std::nullopt, std::nullopt);
  EXPECT_EQ(SurroundColorPref(), -1);
}

TEST_F(AtriumConfirmQuitPanelPrefTest,
       TransparentZeroColorIsNotConfusedWithAutomatic) {
  SetConfirmQuitPanelAppearance(std::nullopt, SK_ColorTRANSPARENT);
  EXPECT_NE(SurroundColorPref(), -1);
  EXPECT_EQ(DecodeStoredPanelSurroundColor(SurroundColorPref()),
            SK_ColorBLACK);
}

TEST_F(AtriumConfirmQuitPanelPrefTest, AThemeRoundTripsThroughThePrefs) {
  const OverlayThemePalette nord = *BrandBookThemePalette("nord");
  SetConfirmQuitPanelTheme(nord);
  const std::optional<OverlayThemePalette> back =
      ConfirmQuitPanelThemeFromPrefs(*local_state());
  ASSERT_TRUE(back);
  EXPECT_EQ(back->accent, nord.accent);
  EXPECT_EQ(back->ink, nord.ink);
  EXPECT_EQ(back->paper, nord.paper);
  EXPECT_EQ(back->soft, nord.soft);
}

TEST_F(AtriumConfirmQuitPanelPrefTest, AThemeColourIsStoredAsRgbOnly) {
  OverlayThemePalette theme = *BrandBookThemePalette("nord");
  theme.accent = SkColorSetARGB(0x40, 0x0F, 0x2B, 0x46);
  SetConfirmQuitPanelTheme(theme);
  const int stored =
      local_state()->GetInteger(prefs::kAtriumConfirmQuitPanelAccentColor);
  EXPECT_GE(stored, 0);
  EXPECT_EQ(DecodeStoredPanelSurroundColor(stored),
            SkColorSetRGB(0x0F, 0x2B, 0x46));
}

TEST_F(AtriumConfirmQuitPanelPrefTest, AnAbsentThemeReadsBackAsTheFallback) {
  SetConfirmQuitPanelTheme(*BrandBookThemePalette("nord"));
  SetConfirmQuitPanelTheme(std::nullopt);
  EXPECT_EQ(
      local_state()->GetInteger(prefs::kAtriumConfirmQuitPanelAccentColor),
      -1);
  EXPECT_FALSE(ConfirmQuitPanelThemeFromPrefs(*local_state()));
}

}  // namespace
}  // namespace atrium
