// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_DROP_DECISION_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_DROP_DECISION_H_

#include <string>

namespace atrium {

// What a tab-drag
// release classified as landing on ANOTHER Atrium window's sidebar
// (atrium_drop_target_decision.h's kSidebar) does next. The user's own
// words: "Ask for permission if the workspace is not the same in the target
// window. If it's the same, it should happen without ask."
enum class SidebarDropAction {
  kMoveSilently,      // Same workspace - move the tab, no prompt.
  kAskBeforeMoving,   // Different workspace - confirm with the user first.
};

// `source_workspace_id` and `target_workspace_id` are BrowserController::
// profile_id_ values (the same workspace identity ChooseMoveTarget,
// atrium_move_target_decision.h, already compares for its own same-profile
// vs cross-profile split) - a plain string equality, no other business
// logic.
SidebarDropAction ChooseSidebarDropAction(
    const std::string& source_workspace_id,
    const std::string& target_workspace_id);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_DROP_DECISION_H_
