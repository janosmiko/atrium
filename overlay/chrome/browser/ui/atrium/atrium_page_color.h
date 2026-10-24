// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_PAGE_COLOR_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_PAGE_COLOR_H_

#include <optional>

#include "third_party/skia/include/core/SkColor.h"

class Browser;

namespace gfx {
class Image;
}

namespace atrium {

// The colour a page's favicon reads as, for the many pages that declare no
// theme-color. nullopt for an empty image and for a monochrome mark, which
// says nothing about the site that every other monochrome mark does not.
std::optional<SkColor> DominantColorOfFavicon(const gfx::Image& image);

// The chrome's ink is white by rule. When the backdrop it sits on drops it
// under 4.5:1, the ink becomes black or white, whichever contrasts more.
SkColor InkForBackdrop(SkColor backdrop);

// The backdrop the ink actually sits on: `tint` seen through glass of
// `glass_alpha` over `appearance_base`, which stands in for the desktop the
// window cannot read. The tint's own alpha is ignored.
SkColor TintOverGlass(SkColor tint, SkAlpha glass_alpha, SkColor appearance_base);

// The ink the CORE grades the page drift against. Off the tint, not off the
// glass composite: the core measures the drift on the tint, and an ink from
// the lighter composite is one the tint can never clear.
SkColor InkForSpaceTint(SkColor tint);

// The one tint an incognito window wears, on every space theme. Darker than
// every entry in rust/atrium-core/src/tint.rs so no space is mistakable for
// it, and the #7AA2FF accent's hue rather than a neutral grey.
inline constexpr SkColor kIncognitoTint = SkColorSetRGB(0x14, 0x12, 0x1A);

// The tint a window paints: its space's, or the incognito constant no space
// theme may override. Every space tint crosses here, so no call site has to
// ask whose window it is holding.
SkColor EffectiveSpaceTint(bool incognito, SkColor space_tint);
SkColor EffectiveSpaceTint(Browser* browser, SkColor space_tint);

// The alpha that tint is painted at. Opaque for an incognito window: at the
// shipped 0.55 glass a light desktop washes kIncognitoTint out to a mid grey,
// which reads as an ordinary window and grades its ink black.
SkAlpha EffectiveGlassAlpha(bool incognito, SkAlpha glass_alpha);
SkAlpha EffectiveGlassAlpha(Browser* browser, SkAlpha glass_alpha);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_PAGE_COLOR_H_
