// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_confirm_quit_panel_style.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_CONFIRM_QUIT_PANEL_STYLE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_CONFIRM_QUIT_PANEL_STYLE_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "chrome/browser/ui/atrium/atrium_overlay_themes.h"
#include "third_party/skia/include/core/SkColor.h"

namespace atrium {

// The confirm-to-quit HUD's colours, radius and keycap padding, matching
// overlay.css's --ov-* tokens. Reworked in C++ because the panel is a bare
// NSWindow with no WebContents to read the CSS from.
struct ConfirmQuitPanelStyle {
  SkColor surface;
  SkColor ink;
  SkColor key_background;
  SkColor key_underline;
  double corner_radius;
  double key_padding_horizontal;
  double key_padding_vertical;
};

// `theme` is the chosen overlay theme's palette (a brand book entry or the
// live system accent). nullopt paints the brand crimson on the light or dark
// paper. A theme's own paper wins over `dark`, as its CSS class does.
ConfirmQuitPanelStyle ComputeConfirmQuitPanelStyle(
    bool dark,
    bool native,
    std::optional<SkColor> surround,
    std::optional<OverlayThemePalette> theme = std::nullopt);

// keycaps.js's splitBinding, ported: each modifier glyph is its own cap, and
// everything after the last one is one final cap. u"⌘Q" -> [u"⌘", u"Q"].
std::vector<std::u16string> SplitConfirmQuitShortcutCaps(
    std::u16string_view shortcut);

struct ConfirmQuitLabelParts {
  std::u16string prefix;
  std::vector<std::u16string> caps;
  std::u16string suffix;
};

// `description_template` is IDS_CONFIRM_TO_QUIT_DESCRIPTION before
// substitution ("Hold $1 to Quit"), so a locale that puts the shortcut first
// or last still keeps its own words on the correct side.
ConfirmQuitLabelParts SplitConfirmQuitLabel(
    std::u16string_view description_template,
    std::u16string_view shortcut);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_CONFIRM_QUIT_PANEL_STYLE_H_
