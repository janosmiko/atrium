// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_status_bubble_color_math.cc
#include "chrome/browser/ui/atrium/atrium_status_bubble_color_math.h"

namespace atrium {

uint32_t StatusBubbleTextColorForBackground(uint32_t background_argb) {
  const uint32_t r = (background_argb >> 16) & 0xFF;
  const uint32_t g = (background_argb >> 8) & 0xFF;
  const uint32_t b = background_argb & 0xFF;
  // Standard YIQ luma approximation (integer form, no float rounding to
  // reason about) - cheap and good enough to pick ink, not a WCAG contrast
  // ratio.
  const uint32_t luma = (r * 299 + g * 587 + b * 114) / 1000;
  constexpr uint32_t kNearBlack = 0xFF0A0A0A;
  constexpr uint32_t kNearWhite = 0xFFF5F5F5;
  return luma >= 128 ? kNearBlack : kNearWhite;
}

}  // namespace atrium
