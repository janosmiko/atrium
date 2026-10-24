// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_capture_overlay_math.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_CAPTURE_OVERLAY_MATH_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_CAPTURE_OVERLAY_MATH_H_

#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

namespace atrium {

// the region-select overlay's widget-local drag -> content DIP
// rect mapping, extracted so it's a fast, standalone gtest unit (the
// atrium_capture_math / slot_presentation_table posture) - //ui/gfx/geometry
// only, no views:: or content:: dependency.
//
// `drag_start`/`drag_end` are the two corners of the user's mouse-down/
// mouse-up drag, in the overlay root view's own local coordinates (which,
// per AtriumCaptureOverlayWidget's header doc, coincide with the content
// pane's DIP coordinate space - the same space atrium::CaptureRegion's
// `content_rect` parameter expects). Not required to be ordered (a drag
// toward up/left yields `drag_end` above/left of `drag_start`) - normalized
// internally.
//
// `pane_bounds_in_widget` is the content pane's rect within the overlay
// widget's own local coordinate space - ordinarily gfx::Rect(0, 0, w, h)
// (the overlay is sized exactly to the pane, so its origin is already the
// pane's origin), but accepted as a parameter rather than assumed so this
// function is independently testable and the "subtract the pane origin" step
// is real code, not a no-op left implicit.
//
// Returns an empty rect - the caller's signal to cancel rather than commit -
// if the normalized drag rect is zero-size (a click with no drag) or clamps
// to empty after intersecting `pane_bounds_in_widget` (the drag never
// entered the pane). Otherwise returns the normalized, pane-clamped rect,
// translated so it is relative to `pane_bounds_in_widget`'s origin.
gfx::Rect ComputeCaptureRegionRect(const gfx::Point& drag_start,
                                   const gfx::Point& drag_end,
                                   const gfx::Rect& pane_bounds_in_widget);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_CAPTURE_OVERLAY_MATH_H_
