// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_dock_reopen_hook.cc
#include "chrome/browser/ui/atrium/atrium_dock_reopen_hook.h"

#include "chrome/browser/ui/atrium/atrium_browser_controller.h"

namespace atrium {

bool HandleDockReopen() {
  return BrowserController::ReopenFromDock();
}

}  // namespace atrium
