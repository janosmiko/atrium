// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_NAV_CARVE_DECISION_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_NAV_CARVE_DECISION_H_

#include "ui/gfx/geometry/point.h"

namespace atrium {

// The sidebar nav row's window geometry, as AtriumClientView::
// NonClientHitTest has it at the moment of the hit test.
struct NavCarveGeometry {
  int win_w = 0;
  int sidebar_width = 0;
  bool on_right = false;
  // False while the floating address bar hosts back/forward/reload, which the
  // row then does not draw at all. Carving their strip anyway would take those
  // clicks off the window drag for buttons that are not there.
  bool nav_group_in_row = true;
};

// Whether `point` (window coordinates) is inside the toggle's carve or the
// back/forward/reload group's carve - regions that take clicks instead of
// starting a window drag. Callers gate y and collapsed state beforehand.
bool PointInNavCarve(const gfx::Point& point, const NavCarveGeometry& geometry);

// The width the toggle and the nav group share inside the row, which is the
// sidebar minus the traffic-light clearance and the row's right inset.
int NavControlsWidth(int sidebar_width);

// How many of back/forward/reload survive in `controls_width`. The page runs
// the same ladder in sidebar-nav-row.js visibleNavButtons(), off the same
// constants - the carve must cover what the page actually draws.
int VisibleNavButtonCount(int controls_width);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_NAV_CARVE_DECISION_H_
