// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_move_target_decision.h"

namespace atrium {

MoveTargetDecision ChooseMoveTarget(
    const std::string& current_profile_id,
    const std::vector<MoveTargetCandidate>& candidates) {
  int any_index = -1;
  for (size_t i = 0; i < candidates.size(); ++i) {
    const MoveTargetCandidate& candidate = candidates[i];
    if (candidate.is_little) {
      continue;
    }
    if (any_index == -1) {
      any_index = static_cast<int>(i);
    }
    if (candidate.profile_id == current_profile_id) {
      return {MoveTargetKind::kSameProfile, static_cast<int>(i)};
    }
  }
  if (any_index != -1) {
    return {MoveTargetKind::kCrossProfile, any_index};
  }
  return {MoveTargetKind::kNone, -1};
}

}  // namespace atrium
