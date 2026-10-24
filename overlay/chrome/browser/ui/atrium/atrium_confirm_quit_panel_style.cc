// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
#include "chrome/browser/ui/atrium/atrium_confirm_quit_panel_style.h"

#include "base/strings/string_util.h"
#include "ui/gfx/color_utils.h"

namespace atrium {

namespace {

// overlay.css's --ov-accent fallback (rgb(176 58 72)) and its light/dark
// --ov-ink, --ov-paper and native-mode surround defaults.
constexpr SkColor kBrandAccent = SkColorSetRGB(0xB0, 0x3A, 0x48);
constexpr SkColor kBrandInkLight = SkColorSetRGB(0x22, 0x10, 0x16);
constexpr SkColor kBrandInkDark = SkColorSetRGB(0xF9, 0xF0, 0xF1);
constexpr SkColor kBrandPaperLight = SkColorSetRGB(0xFB, 0xF7, 0xF8);
constexpr SkColor kBrandPaperDark = SkColorSetRGB(0x24, 0x14, 0x18);
constexpr SkColor kNativeSurroundLight = SkColorSetRGB(0xF6, 0xF6, 0xF6);
constexpr SkColor kNativeSurroundDark = SkColorSetRGB(0x1E, 0x1E, 0x1E);

// --ov-paper mixes the accent in at 6% (light) / 10% (dark); --ov-key-bg
// mixes it into --ov-paper at 10% on both; --ov-soft mixes it into white at
// 60%.
constexpr SkAlpha kLightTintMixAlpha = 15;
constexpr SkAlpha kDarkTintMixAlpha = 26;
constexpr SkAlpha kKeyTintMixAlpha = 26;
constexpr SkAlpha kKeySoftMixAlpha = 153;

// The native keycap: the ink at 10% for the fill and 25% for the line, the
// system look's own key chip rather than a tinted one.
constexpr SkAlpha kNativeKeyFillAlpha = 26;
constexpr SkAlpha kNativeKeyLineAlpha = 64;

constexpr double kBrandCornerRadius = 16.0;
constexpr double kNativeCornerRadius = 12.0;

}  // namespace

ConfirmQuitPanelStyle ComputeConfirmQuitPanelStyle(
    bool dark,
    bool native,
    std::optional<SkColor> surround,
    std::optional<OverlayThemePalette> theme) {
  if (native) {
    const SkColor surface = SkColorSetA(
        surround.value_or(dark ? kNativeSurroundDark : kNativeSurroundLight),
        SK_AlphaOPAQUE);
    const SkColor ink = color_utils::GetColorWithMaxContrast(surface);
    // The caps keep their chips in the native look, drawn in the ink rather
    // than an accent, so they still read as keys and not as a word.
    return {
        .surface = surface,
        .ink = ink,
        .key_background = SkColorSetA(ink, kNativeKeyFillAlpha),
        .key_underline = SkColorSetA(ink, kNativeKeyLineAlpha),
        .corner_radius = kNativeCornerRadius,
        .key_padding_horizontal = 5.0,
        .key_padding_vertical = 3.0,
    };
  }
  if (theme) {
    const SkColor accent = SkColorSetA(theme->accent, SK_AlphaOPAQUE);
    const SkColor surface = SkColorSetA(theme->paper, SK_AlphaOPAQUE);
    return {
        .surface = surface,
        .ink = SkColorSetA(theme->ink, SK_AlphaOPAQUE),
        .key_background =
            color_utils::AlphaBlend(accent, surface, kKeyTintMixAlpha),
        .key_underline = SkColorSetA(theme->soft, SK_AlphaOPAQUE),
        .corner_radius = kBrandCornerRadius,
        .key_padding_horizontal = 5.0,
        .key_padding_vertical = 3.0,
    };
  }
  const SkColor paper = dark ? kBrandPaperDark : kBrandPaperLight;
  const SkColor surface = color_utils::AlphaBlend(
      kBrandAccent, paper, dark ? kDarkTintMixAlpha : kLightTintMixAlpha);
  return {
      .surface = surface,
      .ink = dark ? kBrandInkDark : kBrandInkLight,
      .key_background =
          color_utils::AlphaBlend(kBrandAccent, surface, kKeyTintMixAlpha),
      .key_underline = color_utils::AlphaBlend(kBrandAccent, SK_ColorWHITE,
                                               kKeySoftMixAlpha),
      .corner_radius = kBrandCornerRadius,
      .key_padding_horizontal = 5.0,
      .key_padding_vertical = 3.0,
  };
}

std::vector<std::u16string> SplitConfirmQuitShortcutCaps(
    std::u16string_view shortcut) {
  static constexpr std::u16string_view kModifiers = u"⌃⌥⇧⌘";
  std::vector<std::u16string> caps;
  size_t i = 0;
  while (i < shortcut.size() &&
         kModifiers.find(shortcut[i]) != std::u16string_view::npos) {
    caps.emplace_back(1, shortcut[i]);
    ++i;
  }
  if (i < shortcut.size()) {
    caps.emplace_back(shortcut.substr(i));
  }
  return caps;
}

ConfirmQuitLabelParts SplitConfirmQuitLabel(
    std::u16string_view description_template,
    std::u16string_view shortcut) {
  const std::u16string substitutions[] = {std::u16string(shortcut)};
  std::vector<size_t> offsets;
  std::u16string full = base::ReplaceStringPlaceholders(
      description_template, substitutions, &offsets);

  ConfirmQuitLabelParts parts;
  parts.caps = SplitConfirmQuitShortcutCaps(shortcut);
  // A translation that lost its $1 leaves nothing to split around. Keep the
  // whole sentence rather than dropping half of it.
  if (offsets.empty() || offsets[0] + shortcut.size() > full.size()) {
    parts.prefix = std::move(full);
    return parts;
  }
  parts.prefix = full.substr(0, offsets[0]);
  parts.suffix = full.substr(offsets[0] + shortcut.size());
  return parts;
}

}  // namespace atrium
