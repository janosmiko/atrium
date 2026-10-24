// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_OVERVIEW_WIDTH_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_OVERVIEW_WIDTH_H_

namespace atrium {

// overview.css: the 62px .ovrail and its 1px border beside two .ovspaces
// columns (minmax(150px, 1fr), 10px gap, 2px padding) = 377, rounded up. Must
// stay under the 500 kMaxSidebarWidth clamp, which would silently swallow more.
inline constexpr int kOverviewSidebarMinWidth = 380;

// The width to show while `overview_open`. A sidebar the user already dragged
// to `overview_min` or past it keeps its own width.
int EffectiveSidebarWidth(bool overview_open, int user_width, int overview_min);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_OVERVIEW_WIDTH_H_
