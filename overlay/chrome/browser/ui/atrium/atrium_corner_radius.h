// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_CORNER_RADIUS_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_CORNER_RADIUS_H_

namespace atrium {

// smallest radius a derived corner may collapse to. Past roughly a
// 20pt inset the concentric value reaches zero, and a card that reads as
// square is a worse error than a slightly non-concentric gap.
inline constexpr float kMinNestedCornerRadius = 6;

// Radius for a shape inset `inset` from a parent whose own corner radius is
// `outer`, so the two curves stay parallel and the gap between them reads
// uniform all the way around the corner (inner = outer - gap). A tighter
// inner radius pinches the gap at the 45-degree point - which is exactly what
// made Atrium's hardcoded radii look wrong once the window
// corner was corrected.
//
// Only for surfaces actually nested in a rounded parent's corner. Free-
// floating chips (tabs, favicon tiles, status bubble) keep their own scale;
// concentricity binds only where two corners are visible near each other.
//
// Floored at kMinNestedCornerRadius, and a negative `inset` is treated as 0.
//
// Pure arithmetic (capture_math's posture - no AppKit, no views::, unit-
// testable without a Widget). The window's own radius comes from
// AtriumWindowCornerRadius(), which also supports a runtime tuning override.
float ConcentricCornerRadius(float outer, int inset);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_CORNER_RADIUS_H_
