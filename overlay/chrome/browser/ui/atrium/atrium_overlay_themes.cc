// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
#include "chrome/browser/ui/atrium/atrium_overlay_themes.h"

#include <cstdint>

namespace atrium {

namespace {

struct ThemeRow {
  const char* slug;
  uint32_t accent;
  uint32_t ink;
  uint32_t paper;
  uint32_t soft;
};

// docs/brand/brand-book.html's themes object, one row per theme, in its order.
// Kept as plain RGB so the node test can read the rows back.
constexpr ThemeRow kThemes[] = {
    {"crimson", 0xB03A48, 0x221016, 0xF9F0F1, 0xE06A76},
    {"vermilion", 0xE8481C, 0x1D0D06, 0xFFF2EA, 0xFF8A5C},
    {"magenta", 0xD62D8A, 0x23081E, 0xFBEFF7, 0xF27AB8},
    {"ultraviolet", 0x6C4CE8, 0x120E2E, 0xF0EEFB, 0x9D8AF2},
    {"cyan", 0x00A6C0, 0x04212B, 0xEAF7F9, 0x56D0E2},
    {"lime", 0x7CB518, 0x131A06, 0xF5F8EA, 0xA9D94E},
    {"amber", 0xE8960C, 0x1D1406, 0xFBF4E6, 0xF5B84D},
    {"tokyo-night", 0x7AA2F7, 0x1A1B26, 0xE6E9F5, 0xBB9AF7},
    {"catppuccin", 0xCBA6F7, 0x1E1E2E, 0xEFF1F5, 0xF5C2E7},
    {"gruvbox", 0xD65D0E, 0x282828, 0xFBF1C7, 0xFABD2F},
    {"kanagawa", 0x7E9CD8, 0x1F1F28, 0xDCD7BA, 0x957FB8},
    {"synthwave-everything", 0xFF7EDB, 0x241B2F, 0xF6EBFF, 0x72F1B8},
    {"bluloco", 0x3691FF, 0x282C34, 0xF9F9F9, 0x9F7EFE},
    {"monokai-pro", 0xFF6188, 0x2D2A2E, 0xFCFCFA, 0xFFD866},
    {"dracula", 0xBD93F9, 0x282A36, 0xF8F8F2, 0xFF79C6},
    {"nord", 0x88C0D0, 0x2E3440, 0xECEFF4, 0x81A1C1},
    {"rose-pine", 0xEB6F92, 0x191724, 0xFAF4ED, 0xC4A7E7},
    {"everforest", 0xA7C080, 0x2D353B, 0xFDF6E3, 0xE69875},
    {"solarized", 0x268BD2, 0x002B36, 0xFDF6E3, 0xCB4B16},
};

SkColor Opaque(uint32_t rgb) {
  return SkColorSetA(static_cast<SkColor>(rgb), SK_AlphaOPAQUE);
}

}  // namespace

std::optional<OverlayThemePalette> BrandBookThemePalette(
    std::string_view slug) {
  for (const ThemeRow& row : kThemes) {
    if (slug == row.slug) {
      return OverlayThemePalette{
          .accent = Opaque(row.accent),
          .ink = Opaque(row.ink),
          .paper = Opaque(row.paper),
          .soft = Opaque(row.soft),
      };
    }
  }
  return std::nullopt;
}

}  // namespace atrium
