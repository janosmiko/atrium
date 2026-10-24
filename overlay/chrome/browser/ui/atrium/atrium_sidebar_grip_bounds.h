// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_GRIP_BOUNDS_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_GRIP_BOUNDS_H_

#include "ui/gfx/geometry/rect.h"

namespace atrium {

// The grip's column in the window's BoxLayout, and the visible gutter between
// the sidebar and the card. Widening it to enlarge the grab area would push the
// pane and grow that gutter, so the reach below widens instead.
inline constexpr int kSidebarGripWidth = 6;

// How far the band reaches back over the sidebar's own trailing pixels. With
// the 6px seam column, this makes a 10px total grab area. A
// pointer 10px inside the sidebar stays out of the resize hit target.
inline constexpr int kSidebarGripReach = 4;

// Where the width grip sits, given the `layout_bounds` the BoxLayout hands it.
// The column stays put - the seam does not move - and the band reaches back
// over the sidebar, so it is contiguous from inside the sidebar to the pane.
gfx::Rect SidebarGripBounds(const gfx::Rect& layout_bounds,
                            bool sidebar_on_right);

// The width a drag asks for, `pointer_dx` being how far it moved from the
// press. Docked right the sidebar's edge travels the other way, so dragging
// LEFT widens. Unclamped - the caller owns the min/max.
int SidebarWidthForDrag(int press_width, int pointer_dx, bool sidebar_on_right);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_GRIP_BOUNDS_H_
