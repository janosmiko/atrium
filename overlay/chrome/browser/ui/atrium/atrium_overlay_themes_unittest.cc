// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_overlay_themes_unittest.cc
#include "chrome/browser/ui/atrium/atrium_overlay_themes.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

TEST(OverlayThemesTest, CrimsonIsTheBookEntryOpaque) {
  const std::optional<OverlayThemePalette> theme =
      BrandBookThemePalette("crimson");
  ASSERT_TRUE(theme);
  EXPECT_EQ(theme->accent, SkColorSetRGB(0xB0, 0x3A, 0x48));
  EXPECT_EQ(theme->ink, SkColorSetRGB(0x22, 0x10, 0x16));
  EXPECT_EQ(theme->paper, SkColorSetRGB(0xF9, 0xF0, 0xF1));
  EXPECT_EQ(theme->soft, SkColorSetRGB(0xE0, 0x6A, 0x76));
}

TEST(OverlayThemesTest, ASlugTheBookDoesNotHaveIsNoTheme) {
  EXPECT_FALSE(BrandBookThemePalette("brand"));
  EXPECT_FALSE(BrandBookThemePalette("system"));
  EXPECT_FALSE(BrandBookThemePalette(""));
}

}  // namespace
}  // namespace atrium
