// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_status_bubble_color_math_unittest.cc
#include "chrome/browser/ui/atrium/atrium_status_bubble_color_math.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

constexpr uint32_t kNearBlack = 0xFF0A0A0A;
constexpr uint32_t kNearWhite = 0xFFF5F5F5;

TEST(AtriumStatusBubbleColorMathTest, DarkBackgroundGetsWhiteText) {
  EXPECT_EQ(StatusBubbleTextColorForBackground(0xFF101010), kNearWhite);
}

TEST(AtriumStatusBubbleColorMathTest, LightBackgroundGetsBlackText) {
  EXPECT_EQ(StatusBubbleTextColorForBackground(0xFFF0F0F0), kNearBlack);
}

TEST(AtriumStatusBubbleColorMathTest, SaturatedDarkBlueTintGetsWhiteText) {
  // A deep, saturated space tint - the common case, low luma despite a
  // vivid blue channel.
  EXPECT_EQ(StatusBubbleTextColorForBackground(0xFF1A2A6C), kNearWhite);
}

TEST(AtriumStatusBubbleColorMathTest, PastelYellowTintGetsBlackText) {
  // A light/pastel tint - high luma even though it isn't neutral gray.
  EXPECT_EQ(StatusBubbleTextColorForBackground(0xFFF5F0A0), kNearBlack);
}

TEST(AtriumStatusBubbleColorMathTest, IgnoresBackgroundAlpha) {
  // Same RGB, different alpha (0xB8 is the live kGlassBackdropAlpha) - the
  // ink choice must depend only on hue/luma, not on how translucent the
  // background is.
  EXPECT_EQ(StatusBubbleTextColorForBackground(0xFFF0F0F0),
            StatusBubbleTextColorForBackground(0xB8F0F0F0));
}

TEST(AtriumStatusBubbleColorMathTest, MidGrayTieBreaksToWhite) {
  // Luma of 0x808080 is exactly 128 - the >= boundary in the implementation
  // must resolve this to near-black; pick a value just below the boundary
  // instead to pin down which side of 128 flips, without hard-coding the
  // internal formula's exact output for the boundary itself.
  EXPECT_EQ(StatusBubbleTextColorForBackground(0xFF7E7E7E), kNearWhite);
  EXPECT_EQ(StatusBubbleTextColorForBackground(0xFF828282), kNearBlack);
}

}  // namespace
}  // namespace atrium
