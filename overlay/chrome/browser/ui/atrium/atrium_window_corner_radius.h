// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_WINDOW_CORNER_RADIUS_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_WINDOW_CORNER_RADIUS_H_

namespace atrium {

// Safari's browser-window silhouette on macOS 27. Every mask and
// nested window-edge radius derives from this value, and the AppKit/window-
// server bridge pushes the same value so the visible curve and system border
// stay together.
inline constexpr float kWindowCornerRadius = 16;
inline constexpr float kPreMacOS27WindowCornerRadius = 26;

// The corner radius Atrium paints for its browser window,
// and the value every nested radius derives from (see ConcentricCornerRadius
// in atrium_corner_radius.h). macOS 26's toolbar-frame probe returned 26, but
// Safari moved to the 16pt plain-window silhouette on macOS 27 while
// that private AppKit value remained stale for Atrium's custom frame. Older
// systems keep using the toolbar-frame probe and its measured 26pt fallback.
//
// ATRIUM_WINDOW_CORNER_RADIUS overrides it for visual tuning without a full
// chrome rebuild. Cached after the first call.
float AtriumWindowCornerRadius();

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_WINDOW_CORNER_RADIUS_H_
