// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_page_color.h"

#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"
#include "ui/gfx/color_analysis.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/image/image.h"
#include "ui/gfx/image/image_skia.h"
#include "ui/gfx/image/image_skia_rep.h"

namespace atrium {

namespace {

// The band CalculateKMeanColorOfBitmap prefers its clusters to fall in. It
// answers with the heaviest cluster when none do, so re-reading its answer
// here is the only way to tell a brand colour from an all-white mark.
constexpr double kMinUsefulLightness = 0.15;
constexpr double kMaxUsefulLightness = 0.85;

}  // namespace

std::optional<SkColor> DominantColorOfFavicon(const gfx::Image& image) {
  if (image.IsEmpty()) {
    return std::nullopt;
  }
  // GetRepresentation, not image_reps(): the driver's reps are generated
  // lazily, so a favicon nothing has drawn yet lists none.
  const gfx::ImageSkiaRep& rep = image.AsImageSkia().GetRepresentation(1.0f);
  if (rep.is_null() || rep.pixel_width() <= 0) {
    return std::nullopt;
  }
  const SkColor dominant =
      color_utils::CalculateKMeanColorOfBitmap(rep.GetBitmap());
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(dominant, &hsl);
  if (hsl.l < kMinUsefulLightness || hsl.l > kMaxUsefulLightness) {
    return std::nullopt;
  }
  return SkColorSetA(dominant, SK_AlphaOPAQUE);
}

}  // namespace atrium

namespace atrium {

SkColor InkForBackdrop(SkColor backdrop) {
  const SkColor opaque = SkColorSetA(backdrop, SK_AlphaOPAQUE);
  const float white = color_utils::GetContrastRatio(SK_ColorWHITE, opaque);
  if (white >= color_utils::kMinimumReadableContrastRatio) {
    return SK_ColorWHITE;
  }
  return color_utils::GetContrastRatio(SK_ColorBLACK, opaque) > white
             ? SK_ColorBLACK
             : SK_ColorWHITE;
}

SkColor TintOverGlass(SkColor tint,
                      SkAlpha glass_alpha,
                      SkColor appearance_base) {
  return color_utils::AlphaBlend(SkColorSetA(tint, SK_AlphaOPAQUE),
                                 appearance_base, glass_alpha);
}

SkColor InkForSpaceTint(SkColor tint) {
  return InkForBackdrop(tint);
}

SkColor EffectiveSpaceTint(bool incognito, SkColor space_tint) {
  return incognito ? kIncognitoTint : space_tint;
}

SkColor EffectiveSpaceTint(Browser* browser, SkColor space_tint) {
  return EffectiveSpaceTint(IsIncognitoWindow(browser), space_tint);
}

SkAlpha EffectiveGlassAlpha(bool incognito, SkAlpha glass_alpha) {
  return incognito ? SK_AlphaOPAQUE : glass_alpha;
}

SkAlpha EffectiveGlassAlpha(Browser* browser, SkAlpha glass_alpha) {
  return EffectiveGlassAlpha(IsIncognitoWindow(browser), glass_alpha);
}

}  // namespace atrium
