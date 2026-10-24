// Copyright 2026 The Atrium Authors
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_PINCH_GESTURE_HIT_TEST_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_PINCH_GESTURE_HIT_TEST_H_

#include "ui/gfx/geometry/point.h"

namespace atrium {

// AppKit's locationInWindow is bottom-up. IsPointOverSidebar and
// SurfaceRectToWindow both expect the top-down space Views uses on Mac.
// Its own function so the flip is asserted at both window edges directly.
gfx::Point WindowPointForEventLocation(double location_x,
                                       double location_y,
                                       double content_view_height);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_PINCH_GESTURE_HIT_TEST_H_
