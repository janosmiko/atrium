// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SPLIT_ORIENTATION_DECISION_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SPLIT_ORIENTATION_DECISION_H_

#include "chrome/browser/ui/atrium/atrium_split_tree_insertion.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

namespace atrium {

// Reproduces docs/lab/swipe-lab.html's zoneOf()
// geometry for a cross-window content-area drop - no prompt, position alone
// decides.
enum class ContentDropOrientation {
  kHorizontalSplit,  // Release in the left or right 25% band.
  kVerticalSplit,    // Release in the top or bottom 25% band.
  kSwap,             // Release in the center 50%.
};

// `content_bounds` and `drop_point` are both screen coordinates. Zone check
// order matches the lab's zoneOf() exactly (L/R before T/B), so a corner
// point resolves to a horizontal split.
ContentDropOrientation ChooseSplitOrientation(const gfx::Rect& content_bounds,
                                               const gfx::Point& drop_point);

// InsertAtLeaf needs a specific edge, not just an orientation.
SplitEdge ResolveSplitEdge(const gfx::Rect& content_bounds,
                           const gfx::Point& drop_point);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SPLIT_ORIENTATION_DECISION_H_
