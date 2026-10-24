// Copyright 2026 The Atrium Authors

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_TAB_CYCLE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_TAB_CYCLE_H_

#include <optional>
#include <string>
#include <vector>

#include "base/values.h"

namespace atrium {

// One row of the sidebar as the cycle sees it, in the order the surface draws
// them: the favourite tiles that hold a tab first, then pinned, then each
// folder's rows, then the ordinary tabs.
//
// The sidebar's order is NOT the strip's. A space keeps rows for tabs that are
// not open, and the user reorders rows by dragging, so stepping through the
// strip would walk a different list from the one on screen - which is the
// whole point of this axis existing beside stock Ctrl-Tab.
struct CycleRow {
  // Empty for a COLD row: the tab is in the space but has no live counterpart,
  // so there is nothing to activate and its URL has to be reopened instead.
  std::string tab_id;
  std::string url;
  bool selected = false;
};

// `root`'s rows in the order the surface draws them, `root` being the snapshot
// BuildSidebarTabsJSON produced. Tiles are not rows, so the grid is not here.
std::vector<CycleRow> DrawnOrderRows(const base::DictValue& root);

// The grid, then DrawnOrderRows. A tile that claimed a tab hides that tab's
// row (sidebar.rs's is_favorite filter), so the drawn rows alone leave a
// three-tab space with one row and every chord declining.
std::vector<CycleRow> CycleOrderRows(const base::DictValue& root);

// The row to move to, or nothing when there is nowhere to go.
//
// `direction` is +1 for the next row and -1 for the previous one. The walk
// wraps at both ends, which is what makes it a cycle rather than a scroll.
std::optional<size_t> NextCycleTarget(const std::vector<CycleRow>& rows,
                                      int direction);

// The space to move to, or nothing when there is nowhere to go.
//
// `direction` is +1 for the space after the active one in rail order and -1
// for the one before it; both ends wrap, as they do for rows above.
//
// `active` is allowed to be out of range: it crosses from the document, which
// can still name a space that has been deleted, and the core answers -1 when
// nothing is open. The walk then enters from the end it is moving towards
// rather than refusing, so the key stays useful on a stale document.
std::optional<size_t> NextSpaceIndex(size_t count, int active, int direction);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_TAB_CYCLE_H_
