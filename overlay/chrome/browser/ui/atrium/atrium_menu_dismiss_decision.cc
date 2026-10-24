// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_menu_dismiss_decision.h"

namespace atrium {

bool MouseDownDismissesSidebarMenu(bool menu_open,
                                   const gfx::Rect& sidebar_screen_bounds,
                                   const gfx::Point& screen_point) {
  return menu_open && !sidebar_screen_bounds.Contains(screen_point);
}

}  // namespace atrium
