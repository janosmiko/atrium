// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_status_bubble_color_math.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_STATUS_BUBBLE_COLOR_MATH_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_STATUS_BUBBLE_COLOR_MATH_H_

#include <cstdint>

namespace atrium {

// picks readable ink (opaque near-black or near-white, packed
// 0xAARRGGBB - same byte order as SetBackdropTint's `argb` and SkColor) for
// `background_argb`, the status bubble's tint-derived glass background
// (AtriumBrowserWindow::StatusBubbleBackgroundColor). Pulled out as a pure
// function - no SkColor/gfx dependency - so it is a fast, standalone gtest
// unit (atrium_capture_math's posture); the caller reinterprets its result as
// a SkColor, which packs identically.
//
// Judges contrast from `background_argb`'s RGB alone (its own alpha is
// ignored - the bubble paints over the sidebar/window glass, so the tint's
// hue is what matters, not how transparent it is) via the standard YIQ luma
// approximation. Ties (mid-gray tints) resolve to near-white, matching the
// stock bubble's previous fixed-white text.
uint32_t StatusBubbleTextColorForBackground(uint32_t background_argb);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_STATUS_BUBBLE_COLOR_MATH_H_
