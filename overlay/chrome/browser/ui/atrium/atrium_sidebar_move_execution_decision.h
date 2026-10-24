// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_MOVE_EXECUTION_DECISION_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_MOVE_EXECUTION_DECISION_H_

#include <string>

namespace atrium {

// A WebContents is bound to its creating BrowserContext, so only a
// same-profile pair may take the live-move path (see
// MoveLittleTabToSpace's cross-profile branch).
enum class SidebarMoveExecution {
  kLiveMove,        // Same profile - detach/insert, keeps history.
  kReopenAndClose,  // Different profile - reopen the URL, close the source.
};

SidebarMoveExecution ChooseSidebarMoveExecution(
    const std::string& source_profile_id,
    const std::string& target_profile_id);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_MOVE_EXECUTION_DECISION_H_
