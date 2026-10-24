// Copyright 2026 The Atrium Authors

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_TAB_RECENCY_BRIDGE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_TAB_RECENCY_BRIDGE_H_

#include <cstdint>
#include <optional>

namespace atrium {

// the C++ side of Ctrl-Tab / Ctrl-Shift-Tab recency cycling. Thin
// wrappers over the atrium_core_tab_recency_* C ABI - the list and the walk
// live in Rust (rust/atrium-core/src/tab_recency.rs), not here, for the same
// reason the sidebar-order axis's row choice lives in atrium_tab_cycle: a
// reader should be able to check the wrap, the hold-during-a-cycle behaviour
// and the closed-tab removal against tests, not read them off a call site.
//
// UNBUILT, UNRUN: this file is not yet in a compiled GN target. Both call
// sites now exist - BrowserController::OnTabStripModelChanged
// (atrium_browser_controller.cc) calls RecordTabRecencyActivation on the
// active-tab-changed branch and RecordTabRecencyClosed on a genuine tab
// close, and atrium_command_dispatcher_delegate.mm's -init installs a local
// NSEventMaskFlagsChanged monitor that calls EndTabRecencyCycle on Control's
// release. Neither has compiled or run - the tab_recency_bridge source_set
// is not in this iteration's build.

// Records `tab_id` as newly active for recency purposes. A no-op while a
// cycle is held - see the Rust module doc for why.
void RecordTabRecencyActivation(uint64_t tab_id);

// Drops `tab_id` from the recency list and from any cycle in progress.
void RecordTabRecencyClosed(uint64_t tab_id);

// Advances the held cycle one step, opening one first if none is in
// progress. `direction` is +1 for Ctrl-Tab, -1 for Ctrl-Shift-Tab. Nothing
// when there is nowhere to go - no other tabs to cycle to.
std::optional<uint64_t> StepTabRecencyCycle(int32_t direction);

// Ends the held cycle, committing the tab it currently sits on to the front
// of the recency list. Call on the Control key's release - the chosen end-
// of-cycle signal; see the Rust module doc for why a timeout or a second key
// were not.
void EndTabRecencyCycle();

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_TAB_RECENCY_BRIDGE_H_
