// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_devtools_card_corners.h"

namespace atrium {

gfx::RoundedCornersF CardCornerRadii(const gfx::Rect& card,
                                     const gfx::Rect& part,
                                     float radius) {
  if (part.IsEmpty()) {
    return gfx::RoundedCornersF();
  }
  // Inclusive comparisons, not equality: a tile that reaches or overhangs an
  // edge still owns that edge's corners. Covers the hide_inspected_contents
  // case, where devtools fills the whole card and takes all four back.
  const bool left = part.x() <= card.x();
  const bool right = part.right() >= card.right();
  const bool top = part.y() <= card.y();
  const bool bottom = part.bottom() >= card.bottom();
  return gfx::RoundedCornersF(top && left ? radius : 0.f,
                              top && right ? radius : 0.f,
                              bottom && right ? radius : 0.f,
                              bottom && left ? radius : 0.f);
}

}  // namespace atrium
