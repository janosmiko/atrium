// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_OWN_WINDOW_DROP_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_OWN_WINDOW_DROP_H_

#include <vector>

#include "chrome/browser/ui/atrium/atrium_split_tree_insertion.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

namespace atrium {

// where an address bar grip drag lands inside its OWN window,
// which ChooseDropTarget never sees (BuildDropCandidates leaves it out).
enum class OwnWindowDropKind {
  kElsewhere,  // Outside the window: another window or the desktop.
  kSidebar,    // The sidebar's own drop handling acts on it.
  kPane,
};

struct OwnWindowDrop {
  OwnWindowDropKind kind = OwnWindowDropKind::kElsewhere;
  int pane_index = -1;
  SplitEdge edge = SplitEdge::kCenter;
};

// All rects in screen coordinates. `pane_bounds` is one rect per leaf, in
// leaf order, and an empty `sidebar_bounds` is a collapsed sidebar.
OwnWindowDrop ChooseOwnWindowDrop(const gfx::Point& point,
                                  const gfx::Rect& sidebar_bounds,
                                  const std::vector<gfx::Rect>& pane_bounds);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_OWN_WINDOW_DROP_H_
