// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_restore_plan.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_RESTORE_PLAN_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_RESTORE_PLAN_H_

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace atrium {

// the reading half of the plan atrium_core_restore_slice returns,
// extracted from AtriumBrowserWindow::RestoreSidebarSlice so it is a fast,
// standalone gtest unit (cdp_response's posture) - //base's JSON reader only,
// no Browser and no views::. The browser half (close, reopen, activate) stays
// in the window; what a plan MEANS is asserted here.
struct RestoreReopenStep {
  std::string url;
  // The row the moment was showing. The core flags at most one, and only when
  // that row's tab is gone - a live one is named by select_tab_id instead.
  // Without the flag every reopen would come up in the foreground and the last
  // one would take the selection.
  bool selected = false;
  // Sidebar position of the space this row belongs to. a
  // background space looks exactly as "not live" as a genuinely gone tab, so
  // this is what tells the two apart before opening it.
  size_t space = 0;
};

struct RestorePlan {
  // Out-of-line special members: the chromium-style plugin requires them for
  // any struct with a non-trivial member.
  RestorePlan();
  RestorePlan(RestorePlan&&);
  RestorePlan& operator=(RestorePlan&&);
  ~RestorePlan();

  // False when the JSON was not a plan at all. The core has already rewritten
  // the document by then, so the caller still has to save and redraw - it just
  // cannot do the browser half.
  bool parsed = false;
  // Live tabs the moment did not hold.
  std::vector<uint64_t> close_tab_ids;
  // Rows the moment had live and the document now holds cold.
  std::vector<RestoreReopenStep> reopen;
  // The tab to show, when the selected row is still live.
  std::optional<uint64_t> select_tab_id;
  // The space the window ends up showing. Reopen entries for any other space
  // are for a strip that is not on screen - see RestoreReopenStep::space.
  size_t active_space = 0;
};

// Parses the plan JSON. Entries that carry no usable url or id are dropped
// rather than passed on as empty strings.
RestorePlan ParseRestorePlan(std::string_view json);

// Reopen rows for the space the window is about to show. A row for any other
// space stays cold - see RestoreReopenStep::space.
std::vector<RestoreReopenStep> ReopenRowsForActiveSpace(
    const RestorePlan& plan);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_RESTORE_PLAN_H_
