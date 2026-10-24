// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_theme_studio_placement.cc
#include "chrome/browser/ui/atrium/atrium_theme_studio_placement.h"

#include <algorithm>

namespace atrium {

gfx::Size ThemeStudioSizeFor(const gfx::Rect& parent) {
  const int width = kThemeStudioCardSize.width() + 2 * kThemeStudioOutset;
  const int height = kThemeStudioCardSize.height() + 2 * kThemeStudioOutset;
  // Only the height gives way: the studio scrolls, and a narrower card would
  // fold the colour picker.
  return gfx::Size(width, std::min(height, parent.height()));
}

gfx::Rect ClampThemeStudioBounds(const gfx::Rect& bounds,
                                 const gfx::Rect& parent) {
  const int max_x = std::max(parent.x(), parent.right() - bounds.width());
  const int max_y = std::max(parent.y(), parent.bottom() - bounds.height());
  return gfx::Rect(std::clamp(bounds.x(), parent.x(), max_x),
                   std::clamp(bounds.y(), parent.y(), max_y), bounds.width(),
                   bounds.height());
}

gfx::Rect PlaceThemeStudioAtAnchor(const gfx::Rect& anchor,
                                   const gfx::Rect& parent) {
  const gfx::Size size = ThemeStudioSizeFor(parent);
  const int x = anchor.x() - kThemeStudioOutset;
  const int card_bottom = anchor.y() - kThemeStudioAnchorGap;
  const int y = card_bottom + kThemeStudioOutset - size.height();
  return ClampThemeStudioBounds(gfx::Rect(gfx::Point(x, y), size), parent);
}

gfx::Rect FollowParentBounds(const gfx::Rect& bounds,
                             const gfx::Rect& old_parent,
                             const gfx::Rect& new_parent) {
  gfx::Rect moved = bounds;
  moved.Offset(new_parent.x() - old_parent.x(),
               new_parent.y() - old_parent.y());
  return ClampThemeStudioBounds(moved, new_parent);
}

}  // namespace atrium
