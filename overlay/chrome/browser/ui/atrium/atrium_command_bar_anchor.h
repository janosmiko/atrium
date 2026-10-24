// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_BAR_ANCHOR_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_BAR_ANCHOR_H_

#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace atrium {

// Where the anchored palette card goes, in the parent window's own
// coordinates. The palette widget covers the whole window, so these are the
// page's viewport coordinates too.
struct PalettePlacement {
  int left = 0;
  int top = 0;
  int width = 0;
  // The anchor row's own height. The card's expand starts here and runs out
  // to its full height, so a row that is 34pt tall grows from 34pt.
  int height = 0;
};

// What a full card needs vertically: the query field, the fixed five-row
// list and the footer, per commandbar.css. A row this close to the bottom
// has its box lifted, or the suggestions would draw off the window.
inline constexpr int kAnchoredPaletteHeight = 240;

// Narrower than this is not a prompt anyone can type a URL into. The
// sidebar's own field is about 240pt at the default sidebar width.
inline constexpr int kAnchoredPaletteMinWidth = 320;

// The card laid on `anchor_in_window` and clamped inside `window`: the top
// stays on the anchor's top until the card would overflow the bottom, a
// narrow anchor widens to `min_width`, and a box that would hang off an edge
// slides back in.
PalettePlacement PlaceAnchoredPalette(const gfx::Rect& anchor_in_window,
                                      const gfx::Size& window,
                                      int min_width);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_BAR_ANCHOR_H_
