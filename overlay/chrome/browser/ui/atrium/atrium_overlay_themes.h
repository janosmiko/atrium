// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_overlay_themes.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_OVERLAY_THEMES_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_OVERLAY_THEMES_H_

#include <optional>
#include <string_view>

#include "third_party/skia/include/core/SkColor.h"

namespace atrium {

// One brand book theme's four colours, the same four overlay.css carries on
// each html.atrium-theme-<slug> class.
struct OverlayThemePalette {
  SkColor accent;
  SkColor ink;
  SkColor paper;
  SkColor soft;
};

// The brand book's values for `slug`, or nullopt for a slug the book does not
// have. Native surfaces with no WebContents (the confirm-to-quit HUD, the peek
// card) read the theme here, since they cannot read overlay.css.
// build/scripts/overlay-themes-test.mjs pins this table to the book.
std::optional<OverlayThemePalette> BrandBookThemePalette(std::string_view slug);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_OVERLAY_THEMES_H_
