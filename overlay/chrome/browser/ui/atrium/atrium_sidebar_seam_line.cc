// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_sidebar_seam_line.h"

#include <algorithm>

#include "base/numerics/safe_conversions.h"

namespace atrium {

namespace {

// A band too short for the full inset keeps a proportional gap instead.
constexpr int kShortBandDivisor = 3;

}  // namespace

gfx::RectF SidebarSeamLineBounds(const gfx::Rect& band,
                                 bool sidebar_on_right) {
  const int column_x = sidebar_on_right ? 0 : band.width() - kSidebarGripWidth;
  const int inset =
      std::min(kSidebarSeamLineInset, band.height() / kShortBandDivisor);
  return gfx::RectF(
      band.x() + column_x +
          (kSidebarGripWidth - kSidebarSeamLineThickness) / 2.0f,
      static_cast<float>(band.y() + inset),
      static_cast<float>(kSidebarSeamLineThickness),
      static_cast<float>(std::max(0, band.height() - 2 * inset)));
}

float SidebarSeamLineOpacity(float reveal_t, bool dragging) {
  return dragging ? 1.0f : std::clamp(reveal_t, 0.0f, 1.0f);
}

SkColor SidebarSeamLineColor(SkColor accent, float opacity) {
  return SkColorSetA(accent, base::ClampRound<uint8_t>(
                                 kSidebarSeamLineAlpha *
                                 std::clamp(opacity, 0.0f, 1.0f)));
}

}  // namespace atrium
