// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_profile_window_decision.cc
#include "chrome/browser/ui/atrium/atrium_profile_window_decision.h"

namespace atrium {

int ChooseProfileWindow(const std::string& profile_id,
                        const std::vector<ProfileWindowCandidate>& candidates) {
  if (profile_id.empty()) {
    return -1;
  }
  for (size_t i = 0; i < candidates.size(); ++i) {
    const ProfileWindowCandidate& c = candidates[i];
    if (c.profile_id == profile_id && c.realised && c.shown && !c.little) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

}  // namespace atrium
