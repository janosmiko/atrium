// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_SEAM_LINE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_SEAM_LINE_H_

#include <cstdint>

#include "chrome/browser/ui/atrium/atrium_sidebar_grip_bounds.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/rect_f.h"

namespace atrium {

// The mark the seam draws while the pointer hovers or drags the sidebar's
// edge. Every number here is an overlay token.

// The seam column less the guide's 1px hairline (.ov-card's border) on each
// side. Anything wider spills over the sidebar's own web layer, which
// composites above Views paint and would clip the line lengthwise.
inline constexpr int kSidebarSeamLineThickness = kSidebarGripWidth - 2;

// --ov-radius-card. The pill stops where an overlay card's corner turns, so
// the seam reads as an inset mark rather than as a full-height divider.
inline constexpr int kSidebarSeamLineInset = 16;

// --ov-fg's .92.
inline constexpr uint8_t kSidebarSeamLineAlpha = 235;

// .ov-hint is a pill, so the ends are half the thickness.
inline constexpr float kSidebarSeamLineRadius =
    kSidebarSeamLineThickness / 2.0f;

// The pill inside `band`, the grip's own bounds. The band reaches back over
// the sidebar and only the seam column paints, so the pill stays in that
// column - the band's near edge while the sidebar docks right.
gfx::RectF SidebarSeamLineBounds(const gfx::Rect& band, bool sidebar_on_right);

// How opaque the seam line paints this frame, 0 to 1, where 1 is the guide's
// own alpha. `dragging` bypasses the reveal fade outright, so a press that
// starts a drag lights the line fully in the frame it lands in.
float SidebarSeamLineOpacity(float reveal_t, bool dragging);

// The pill's paint: `accent` at the seam's own alpha, scaled by `opacity`.
SkColor SidebarSeamLineColor(SkColor accent, float opacity);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_SEAM_LINE_H_
