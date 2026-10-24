// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_sidebar_move_execution_decision.h"

namespace atrium {

SidebarMoveExecution ChooseSidebarMoveExecution(
    const std::string& source_profile_id,
    const std::string& target_profile_id) {
  return source_profile_id == target_profile_id
             ? SidebarMoveExecution::kLiveMove
             : SidebarMoveExecution::kReopenAndClose;
}

}  // namespace atrium
