// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_MENU_DISMISS_DECISION_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_MENU_DISMISS_DECISION_H_

#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

namespace atrium {

// Whether a mouse-down at `screen_point` (screen DIPs, as the bounds are) has
// to push a dismiss. A press inside the surface is the page's own to handle -
// a second dismiss would race the item under the cursor.
bool MouseDownDismissesSidebarMenu(bool menu_open,
                                   const gfx::Rect& sidebar_screen_bounds,
                                   const gfx::Point& screen_point);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_MENU_DISMISS_DECISION_H_
