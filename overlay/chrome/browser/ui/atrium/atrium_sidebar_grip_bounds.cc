// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_sidebar_grip_bounds.h"

#include "ui/gfx/geometry/insets.h"

namespace atrium {

gfx::Rect SidebarGripBounds(const gfx::Rect& layout_bounds,
                            bool sidebar_on_right) {
  gfx::Rect bounds = layout_bounds;
  // A negative inset grows the rect on that side. The sidebar sits to the right
  // of the column when it docks right, to its left otherwise.
  bounds.Inset(sidebar_on_right
                   ? gfx::Insets::TLBR(0, 0, 0, -kSidebarGripReach)
                   : gfx::Insets::TLBR(0, -kSidebarGripReach, 0, 0));
  return bounds;
}

int SidebarWidthForDrag(int press_width,
                        int pointer_dx,
                        bool sidebar_on_right) {
  return press_width + (sidebar_on_right ? -pointer_dx : pointer_dx);
}

}  // namespace atrium
