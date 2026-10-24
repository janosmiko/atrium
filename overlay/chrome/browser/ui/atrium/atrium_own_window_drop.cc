// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_own_window_drop.h"

#include "chrome/browser/ui/atrium/atrium_split_orientation_decision.h"

namespace atrium {

OwnWindowDrop ChooseOwnWindowDrop(const gfx::Point& point,
                                  const gfx::Rect& sidebar_bounds,
                                  const std::vector<gfx::Rect>& pane_bounds) {
  if (sidebar_bounds.Contains(point)) {
    return {.kind = OwnWindowDropKind::kSidebar};
  }
  for (size_t i = 0; i < pane_bounds.size(); ++i) {
    if (pane_bounds[i].Contains(point)) {
      return {.kind = OwnWindowDropKind::kPane,
              .pane_index = static_cast<int>(i),
              .edge = ResolveSplitEdge(pane_bounds[i], point)};
    }
  }
  return {};
}

}  // namespace atrium
