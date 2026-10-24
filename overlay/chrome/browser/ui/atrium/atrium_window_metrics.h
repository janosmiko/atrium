// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_window_metrics.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_WINDOW_METRICS_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_WINDOW_METRICS_H_

namespace atrium {

// Height of the invisible titlebar band. The window's own chrome is measured
// from it, and on macOS BrowserWindowFrame reads it back to centre the
// floating window controls inside that band.
inline constexpr float kTitlebarHeight = 40;

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_WINDOW_METRICS_H_
