// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_dock_reopen_hook.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_DOCK_REOPEN_HOOK_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_DOCK_REOPEN_HOOK_H_

namespace atrium {

// called from AppController's
// -applicationShouldHandleReopen:hasVisibleWindows: after the stock
// visible-browser-windows focus branch has passed on the event. Without
// this, a Dock-icon click on a running Atrium with all windows closed falls
// into stock Chromium's reopen handling - the multi-profile ProfilePicker
// (listing Atrium's own per-space profiles) or a plain CreateBrowser window,
// both without the Atrium chrome layer. The cold LAUNCH path alone is not enough:
// a warm reopen never re-enters that bootstrap.
//
// Returns true when Atrium handled the reopen (the caller must return NO to
// AppKit and skip the stock picker/CreateBrowser path); false to fall
// through to byte-identical stock behavior (vanilla/degraded dylib run, or
// nothing usable to reopen with). Thin forwarder to
// BrowserController::ReopenFromDock() - see that method's doc for the
// surface-existing-window vs open-new-slot branches - kept as a separate
// hook header so app_controller_mac.mm doesn't include the full controller
// header, matching the atrium_*_hook convention.
bool HandleDockReopen();

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_DOCK_REOPEN_HOOK_H_
