// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_drop_target_decision.h"

namespace atrium {

DropTargetDecision ChooseDropTarget(
    const gfx::Point& point,
    const std::vector<DropTargetCandidate>& candidates) {
  for (size_t i = 0; i < candidates.size(); ++i) {
    const DropTargetCandidate& candidate = candidates[i];
    if (!candidate.window_bounds.Contains(point)) {
      continue;
    }
    if (candidate.has_sidebar && candidate.sidebar_bounds.Contains(point)) {
      return {DropTargetKind::kSidebar, static_cast<int>(i)};
    }
    return {DropTargetKind::kContent, static_cast<int>(i)};
  }
  return {DropTargetKind::kNoMatch, -1};
}

}  // namespace atrium
