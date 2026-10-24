// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_sidebar_drop_decision.h"

namespace atrium {

SidebarDropAction ChooseSidebarDropAction(
    const std::string& source_workspace_id,
    const std::string& target_workspace_id) {
  return source_workspace_id == target_workspace_id
             ? SidebarDropAction::kMoveSilently
             : SidebarDropAction::kAskBeforeMoving;
}

}  // namespace atrium
