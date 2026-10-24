// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_DEVTOOLS_CARD_CORNERS_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_DEVTOOLS_CARD_CORNERS_H_

#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rounded_corners_f.h"

namespace atrium {

// Docking DevTools splits the content card into two tiles
// (page contents + devtools), and each tile is its own layer that must carry
// its own corner radii - a native web view ignores an ancestor's mask, so the
// card's rounded silhouette can only come from the tiles themselves. Rounding
// every tile on all four corners instead rounds the seam between them and
// leaves the card's real outer corners square.
//
// Rounds only the corners of `part` that sit on `card`'s own corners; the edge
// the two tiles share stays square. Undocked (part == card) that is all four,
// so the single-tile case is unchanged. An empty `part` gets no rounding.
//
// Pure geometry (capture_math's posture - //ui/gfx/geometry only, no views::
// dependency, unit-testable without a Widget).
gfx::RoundedCornersF CardCornerRadii(const gfx::Rect& card,
                                     const gfx::Rect& part,
                                     float radius);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_DEVTOOLS_CARD_CORNERS_H_
