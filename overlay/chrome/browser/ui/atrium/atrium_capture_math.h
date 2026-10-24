// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_capture_math.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_CAPTURE_MATH_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_CAPTURE_MATH_H_

#include <string>

#include "base/time/time.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace atrium {

// clamps a region capture rect to the capturing view, both in DIP.
//
// DIP on the way in AND on the way out, which is the correction this replaces
// RegionToDevicePixels with. RenderWidgetHostView::CopyFromSurface takes its
// `src_rect` in VIEW coordinates and applies the device scale itself - its own
// header says the resulting bitmap's size may differ from src_rect.size() "due
// to the pixel scale used by the underlying device". Converting to device
// pixels here applied the factor twice: a 200x200 point selection at dpr 2 came
// back as an 800x276 image, the 4x rect clamped at the surface edge.
//
// `content_rect` is the caller's selected region in DIP, relative to the view's
// origin. `view_bounds_dip` is the view's own size in DIP
// (RenderWidgetHostView::GetViewBounds().size()).
//
// Returns an empty rect - the caller's signal to treat the capture as failed -
// if either input is empty or the region falls entirely outside the view.
gfx::Rect ClampRegionToView(const gfx::Rect& content_rect,
                            const gfx::Size& view_bounds_dip);

// the file name a capture is saved under, in the user's Downloads
// directory. Pure so the format is a unit rather than something only a manual
// save can check.
//
// `local` is the capture's wall-clock time, already in the user's own time zone
// (base::Time::LocalExploded). Seconds are in the name because two captures a
// minute apart are ordinary and two in the same second are not, and a colon is
// not used at all: it is the path separator in the classic Mac path form and
// Finder still renders one in a file name as a slash.
std::string CaptureFileName(const base::Time::Exploded& local);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_CAPTURE_MATH_H_
