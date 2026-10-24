// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_sidebar_snapshot_gate.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_SNAPSHOT_GATE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_SNAPSHOT_GATE_H_

namespace atrium {

// whether AtriumBrowserWindow::PushSidebarSnapshot should run its
// JS model rebuild right now. Every profile's own AtriumBrowserWindow stays
// alive off-screen for as long as its slot presents a DIFFERENT profile (see
// slot_presentation_table.h's state 3), and PushSidebarSnapshot used to run
// unconditionally on every tab/title/favicon event in that hidden profile -
// real ExecuteJavaScript work into a WebContents nobody can see. This is the
// house pattern (atrium_edit_focus_gate.h, slot_presentation_table.h) applied
// to that cost: a pure function of a caller-resolved fact, not a second,
// drifting notion of "visible".
enum class SidebarSnapshotDecision {
  // Run the rebuild now.
  kPush,
  // Skip it - this window is not its slot's presenter.
  kSkip,
};

// `is_presenter` is whether this window is currently
// SlotPresentationTable::ResolvePresenter's answer for its own slot -
// resolved by the caller (BrowserController::PresentedWindowForBrowser),
// since this function holds no slot table of its own.
//
// A hidden window's skipped pushes are not lost: BrowserController::
// ActivateWindow's kFirstActivation branch calls PushSidebarSnapshot once,
// unconditionally, right after a window becomes its slot's presenter - one
// coalesced rebuild that replaces however many were skipped while hidden,
// rather than either losing the update or replaying each one.
SidebarSnapshotDecision DecideSidebarSnapshot(bool is_presenter);

// Boot half: AtriumBrowserWindow's constructor no longer builds
// sidebar_contents_ unconditionally - a hidden-pool profile (show=false)
// used to pay for a full WebContents build and navigation at boot for a
// surface nobody would ever see. Construction now builds nothing; every
// place BrowserController records this window as a NEW slot presenter
// (the self-creating ctor's `show` block, ActivateWindow's kFirstActivation
// branch, NotifyShownIfAdopted) calls AtriumBrowserWindow::
// CatchUpSidebarForNewPresenter() instead, which consults this same
// decision before doing anything.
//
// One funnel for both the build and the push, not two independently-called
// steps: the per-event push gate (DecideSidebarSnapshot above) already had
// three catch-up call sites drift out of sync with a fourth (the in-window
// swap path) because each site called PushSidebarSnapshot() by hand. Adding
// a second, separately-called BuildHtmlSidebar() at the same three sites
// would reopen exactly that risk for the build half. Route both through one
// decision and one method instead.
enum class SidebarPresenterCatchUpDecision {
  // Build (idempotent - a no-op if already built) and push.
  kBuildAndPush,
  // This window is not (or is no longer) its slot's presenter; do nothing.
  kNothing,
};

// `is_presenter` is resolved the same way DecideSidebarSnapshot's caller
// resolves it (BrowserController::PresentedWindowForBrowser).
SidebarPresenterCatchUpDecision DecideSidebarPresenterCatchUp(
    bool is_presenter);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_SNAPSHOT_GATE_H_
