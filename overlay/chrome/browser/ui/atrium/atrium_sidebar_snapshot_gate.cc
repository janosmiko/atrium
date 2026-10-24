// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_sidebar_snapshot_gate.cc
#include "chrome/browser/ui/atrium/atrium_sidebar_snapshot_gate.h"

namespace atrium {

SidebarSnapshotDecision DecideSidebarSnapshot(bool is_presenter) {
  return is_presenter ? SidebarSnapshotDecision::kPush
                       : SidebarSnapshotDecision::kSkip;
}

SidebarPresenterCatchUpDecision DecideSidebarPresenterCatchUp(
    bool is_presenter) {
  return is_presenter ? SidebarPresenterCatchUpDecision::kBuildAndPush
                       : SidebarPresenterCatchUpDecision::kNothing;
}

}  // namespace atrium
