// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_pin_toggle.cc
#include "chrome/browser/ui/atrium/atrium_pin_toggle.h"

namespace atrium {

PinToggleAction DecidePinToggle(const std::vector<PinToggleRow>& pinned,
                                const std::vector<PinToggleRow>& regular) {
  for (size_t i = 0; i < pinned.size(); ++i) {
    if (pinned[i].selected) {
      return {PinToggleKind::kUnpin, i, std::string()};
    }
    const std::vector<PinToggleRow>& members = pinned[i].members;
    for (size_t m = 0; m < members.size(); ++m) {
      if (members[m].selected) {
        return {PinToggleKind::kUnpinMember, 0, std::string(),
                pinned[i].folder_id, m};
      }
    }
  }
  for (const PinToggleRow& row : regular) {
    // A cold row has no live tab, and the core binds a pin by tab id. Nothing
    // rather than a guess: the key falls through and the sidebar stays put.
    if (row.selected && !row.tab_id.empty()) {
      return {PinToggleKind::kPin, 0, row.tab_id};
    }
  }
  return PinToggleAction();
}

}  // namespace atrium
