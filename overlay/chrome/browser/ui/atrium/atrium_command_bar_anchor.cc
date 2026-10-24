// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
#include "chrome/browser/ui/atrium/atrium_command_bar_anchor.h"

#include <algorithm>

namespace atrium {

PalettePlacement PlaceAnchoredPalette(const gfx::Rect& anchor_in_window,
                                      const gfx::Size& window,
                                      int min_width) {
  PalettePlacement placement;
  placement.width = std::clamp(std::max(anchor_in_window.width(), min_width),
                               0, window.width());
  placement.left =
      std::clamp(anchor_in_window.x(), 0, window.width() - placement.width);
  placement.top =
      std::clamp(anchor_in_window.y(), 0,
                 std::max(0, window.height() - kAnchoredPaletteHeight));
  // At least one, so the expand has a value to run from on a row the page
  // reported as zero-height mid-layout.
  placement.height = std::max(1, anchor_in_window.height());
  return placement;
}

}  // namespace atrium
