// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_OVERLAY_THEME_SCRIPT_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_OVERLAY_THEME_SCRIPT_H_

#include <cstdint>
#include <optional>
#include <string>

#include "third_party/skia/include/core/SkColor.h"

namespace atrium {

// The overlay palette one accent colour derives, as the core's
// theme_math::accent_tokens answers it. `ink` is an "R G B" triplet because
// overlay.css consumes it as rgb(var(--ov-ink) / alpha); the rest are hexes.
struct OverlayAccentTokens {
  std::string accent;
  std::string ink;
  std::string paper;
  std::string soft;
  std::string accent_ink;
};

// No AppleAccentColor default at all, which is what Multicolor writes.
inline constexpr int kNoMacosAccentIndex = -1000;

// The palette for one "#RRGGBB" accent, or nullopt when the core refuses the
// colour or answers anything that would not be safe inside the pushed script.

// `accent_index` is the live AppleAccentColor, and the core prefers it over
// `accent_hex`: AppKit resolves controlAccentColor once per process, so the
// hex a long-lived window reads goes stale the moment the setting moves.
std::optional<OverlayAccentTokens> OverlayAccentTokensForHex(
    const std::string& accent_hex,
    int accent_index);

// The palette for the live macOS accent, or nullopt when AppKit has no answer.
// Multicolor reads as the blue AppKit paints controls with, so it needs no
// mapping of its own.
std::optional<OverlayAccentTokens> SystemOverlayAccentTokens();

// The script every overlay document runs to learn the space tint, the native
// chrome mode, and the accent: `theme` is a brand book slug carried as a
// class, empty for the fixed brand fallback.

// `system_accent` is the live macOS accent, which has no class to name, so it
// rides the root as --atrium-system-* properties instead. It and a non-empty
// `theme` are alternatives, never both.
std::string OverlayThemeScript(
    SkColor tint,
    bool has_tint,
    bool native,
    uint32_t surround_argb,
    SkColor surround_ink,
    const std::string& theme,
    const std::optional<OverlayAccentTokens>& system_accent);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_OVERLAY_THEME_SCRIPT_H_
