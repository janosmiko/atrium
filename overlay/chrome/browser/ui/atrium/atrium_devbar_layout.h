// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_DEVBAR_LAYOUT_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_DEVBAR_LAYOUT_H_

#include "ui/gfx/geometry/rect.h"

namespace atrium {

// matches swipe-lab.html's .devbar height (~596).
inline constexpr int kDevBarHeight = 34;
inline constexpr int kDevBarMaxWidth = 560;
inline constexpr int kDevBarMargin = 10;
// room for --ov-shadow-tight (0 5px 14px, so 7px of reach), or it
// clips at the viewport edge. devbar.css's padding must match.
inline constexpr int kDevBarShadowPadX = 16;
inline constexpr int kDevBarShadowPadTop = 10;
inline constexpr int kDevBarShadowPadBottom = 20;

// The bar's widget rect over a pane at `pane`: centred, kDevBarMargin below
// the pane's top, padded by the shadow room. Never wider than the pane allows.
gfx::Rect DevBarBoundsInRect(const gfx::Rect& pane);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_DEVBAR_LAYOUT_H_
