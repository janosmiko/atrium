// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_capture_overlay_math.cc
#include "chrome/browser/ui/atrium/atrium_capture_overlay_math.h"

#include <algorithm>

namespace atrium {

gfx::Rect ComputeCaptureRegionRect(const gfx::Point& drag_start,
                                   const gfx::Point& drag_end,
                                   const gfx::Rect& pane_bounds_in_widget) {
  const int x = std::min(drag_start.x(), drag_end.x());
  const int y = std::min(drag_start.y(), drag_end.y());
  const int width = std::abs(drag_end.x() - drag_start.x());
  const int height = std::abs(drag_end.y() - drag_start.y());

  gfx::Rect selection(x, y, width, height);
  if (selection.IsEmpty()) {
    // Zero-size drag (a plain click, no movement) - the caller's cancel
    // signal, not a degenerate 0x0 capture request.
    return gfx::Rect();
  }

  selection.Intersect(pane_bounds_in_widget);
  if (selection.IsEmpty()) {
    // The drag never entered the pane (or was clamped away entirely) -
    // nothing to capture.
    return gfx::Rect();
  }

  // Translate into the pane's own coordinate space - see this function's
  // header doc for why this is real code rather than an assumed no-op.
  selection.Offset(-pane_bounds_in_widget.x(), -pane_bounds_in_widget.y());
  return selection;
}

}  // namespace atrium
