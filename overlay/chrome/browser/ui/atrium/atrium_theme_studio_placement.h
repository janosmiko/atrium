// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_theme_studio_placement.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_THEME_STUDIO_PLACEMENT_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_THEME_STUDIO_PLACEMENT_H_

#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace atrium {

// Pure geometry for ThemeStudioWidget, kept apart from the widget so a
// unit test can drive it without a window.

// The card theme-window.css draws. The widget is this plus kThemeStudioOutset
// on every side, the room the card's .ov-card shadow needs.
inline constexpr gfx::Size kThemeStudioCardSize(320, 620);
inline constexpr int kThemeStudioOutset = 6;
// From the anchor dot's top edge up to the card's bottom edge.
inline constexpr int kThemeStudioAnchorGap = 8;

gfx::Size ThemeStudioSizeFor(const gfx::Rect& parent);

gfx::Rect PlaceThemeStudioAtAnchor(const gfx::Rect& anchor,
                                   const gfx::Rect& parent);

gfx::Rect ClampThemeStudioBounds(const gfx::Rect& bounds,
                                 const gfx::Rect& parent);

gfx::Rect FollowParentBounds(const gfx::Rect& bounds,
                             const gfx::Rect& old_parent,
                             const gfx::Rect& new_parent);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_THEME_STUDIO_PLACEMENT_H_
