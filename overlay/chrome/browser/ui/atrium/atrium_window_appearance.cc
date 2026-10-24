// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_window_appearance.cc
#include "chrome/browser/ui/atrium/atrium_window_appearance.h"

#include <algorithm>
#include <cmath>
#include <string>

#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/values.h"

namespace atrium {

namespace {

// third_party/atrium/core/src/settings.rs's MIN_GLASS_OPACITY and
// MAX_GLASS_OPACITY. Mirrored rather than shared: the core exports no C
// constant for either, and a window painting an opacity the store refuses to
// hold would stop agreeing with the file the moment it was written back.
constexpr double kMinGlassOpacity = 0.02;
constexpr double kMaxGlassOpacity = 1.0;

// third_party/atrium/core/src/settings.rs's MIN_BORDER_WIDTH and
// MAX_BORDER_WIDTH, mirrored for the same reason the opacity bounds are.
constexpr int kMinBorderWidth = 4;
constexpr int kMaxBorderWidth = 10;

// atrium_browser_window.cc's kMinSidebarWidth, kMaxSidebarWidth and
// kSidebarCollapseThreshold. Mirrored for the same reason the opacity bounds
// above are: those three sit in an anonymous namespace in an Objective-C++ TU
// this one cannot include, and a reader that admitted a width the window then
// clamps would report a width the sidebar never takes.
constexpr double kMinSidebarWidth = 160.0;
constexpr double kMaxSidebarWidth = 500.0;
constexpr double kSidebarCollapseThreshold = 0.5;

// "#RRGGBB", the one spelling the store keeps (normalized_surround_color).
// Anything else - a colour name, a short hex, an oklch() from a later build -
// is nullopt, which means automatic and is itself a valid answer.
std::optional<uint32_t> ParseSurroundColor(const std::string* hex) {
  if (!hex) {
    return std::nullopt;
  }
  // Trimmed first, because normalized_surround_color trims. A colour the store
  // accepts and this reader did not would paint the automatic neutral over a
  // file that says otherwise.
  const std::string_view trimmed =
      base::TrimWhitespaceASCII(*hex, base::TRIM_ALL);
  if (trimmed.size() != 7 || trimmed[0] != '#') {
    return std::nullopt;
  }
  const std::string_view digits = trimmed.substr(1);
  // Each of the six checked here rather than left to HexStringToUInt, which
  // tolerates a leading "0x" and would take "#0x1234". The store's own
  // normalized_surround_color requires six hex digits, and two validators that
  // accept different strings is how a colour round-trips into something else.
  for (char digit : digits) {
    if (!base::IsHexDigit(digit)) {
      return std::nullopt;
    }
  }
  uint32_t rgb = 0;
  if (!base::HexStringToUInt(digits, &rgb)) {
    return std::nullopt;
  }
  return rgb & 0x00FFFFFFu;
}

}  // namespace

WindowAppearance WindowAppearanceFromSettings(std::string_view settings_json) {
  WindowAppearance appearance;
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return appearance;
  }
  const base::DictValue& dict = parsed->GetDict();

  // Present but unrecognised reads LEFT, which is `one_of`'s own answer for
  // this field. It has to be: the save path reads the surface's object BEFORE
  // the core normalises it, so a value that is neither spelling lands on disk
  // as "left", and a window left where it was would then disagree with the
  // file it just wrote. Absent is still no answer at all - that is a key this
  // build's settings.json always carries, so its absence means an object from
  // somewhere else.
  if (const std::string* position = dict.FindString("sidebarPosition")) {
    appearance.sidebar_on_right = (*position == "right");
  }

  appearance.opaque_surround = dict.FindBool("opaqueSurround");
  appearance.surround_argb = ParseSurroundColor(dict.FindString("surroundColor"));

  if (const std::string* accent = dict.FindString("overlayAccent")) {
    // "brand" and the retired "space" both become crimson, the accent
    // "brand" pinned. A spelling outside [a-z0-9-] falls back too - this
    // reaches a class name in a generated script.
    const bool slug_safe =
        !accent->empty() &&
        accent->find_first_not_of(
            "abcdefghijklmnopqrstuvwxyz0123456789-") == std::string::npos;
    appearance.overlay_theme =
        (*accent == "brand" || *accent == "space" || !slug_safe) ? "crimson"
                                                                  : *accent;
  }

  // FindDouble rather than FindInt: the store writes an f64 and a hand-edited
  // file can spell a whole one `1`. FindDouble answers both.
  if (std::optional<double> opacity = dict.FindDouble("glassOpacity")) {
    if (std::isfinite(*opacity)) {
      const double clamped =
          std::clamp(*opacity, kMinGlassOpacity, kMaxGlassOpacity);
      appearance.glass_alpha_percent =
          static_cast<int>(std::lround(clamped * 100.0));
    }
  }

  // FindDouble, on glassOpacity's terms: the surface can send a whole number
  // back as 12.0, and FindInt refuses that.
  if (std::optional<double> border = dict.FindDouble("borderWidth")) {
    if (std::isfinite(*border)) {
      appearance.border_width =
          std::clamp(static_cast<int>(std::lround(*border)), kMinBorderWidth,
                     kMaxBorderWidth);
    }
  }
  return appearance;
}

std::optional<double> SidebarWidthFromSpaces(std::string_view spaces_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(spaces_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  const base::DictValue* app_state = parsed->GetDict().FindDict("appState");
  if (!app_state) {
    return std::nullopt;
  }
  // FindDouble rather than FindInt, for the reason glassOpacity gives: the
  // store holds an f64, and the fixture on disk spells a whole one `380`.
  std::optional<double> width = app_state->FindDouble("sidebarWidth");
  if (!width || !std::isfinite(*width)) {
    return std::nullopt;
  }
  // Collapsed is not a width - see the header. The threshold rather than an
  // exact 0 because that is where the window's own reader draws the line, and
  // two collapse tests that disagree is how a 0.4 becomes a 160pt sidebar.
  if (*width <= kSidebarCollapseThreshold) {
    return std::nullopt;
  }
  return std::clamp(*width, kMinSidebarWidth, kMaxSidebarWidth);
}

bool OverlayAccentDiffers(const std::string& new_theme,
                          const std::string& current_theme) {
  return new_theme != current_theme;
}

}  // namespace atrium
