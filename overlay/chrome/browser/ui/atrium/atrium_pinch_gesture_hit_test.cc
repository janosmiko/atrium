// Copyright 2026 The Atrium Authors
#include "chrome/browser/ui/atrium/atrium_pinch_gesture_hit_test.h"

namespace atrium {

gfx::Point WindowPointForEventLocation(double location_x,
                                       double location_y,
                                       double content_view_height) {
  return gfx::Point(location_x, content_view_height - location_y);
}

}  // namespace atrium
