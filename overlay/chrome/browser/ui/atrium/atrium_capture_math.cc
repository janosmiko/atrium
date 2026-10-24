// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_capture_math.cc
#include "chrome/browser/ui/atrium/atrium_capture_math.h"

#include "base/strings/stringprintf.h"

namespace atrium {

gfx::Rect ClampRegionToView(const gfx::Rect& content_rect,
                           const gfx::Size& view_bounds_dip) {
  if (content_rect.IsEmpty() || view_bounds_dip.IsEmpty()) {
    return gfx::Rect();
  }
  gfx::Rect region = content_rect;
  region.Intersect(gfx::Rect(view_bounds_dip));
  return region;
}

std::string CaptureFileName(const base::Time::Exploded& local) {
  return base::StringPrintf(
      "Atrium Screenshot %04d-%02d-%02d at %02d.%02d.%02d.png", local.year,
      local.month, local.day_of_month, local.hour, local.minute, local.second);
}

}  // namespace atrium
