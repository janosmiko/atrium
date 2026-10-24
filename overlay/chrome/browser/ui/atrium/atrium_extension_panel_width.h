// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTENSION_PANEL_WIDTH_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTENSION_PANEL_WIDTH_H_

#include <optional>

namespace atrium {

inline constexpr int kExtensionPanelMinWidth = 320;
inline constexpr int kExtensionPanelDefaultWidth = 400;
inline constexpr double kExtensionPanelMaxWidthFraction = 0.5;

// Stored per extension in ExtensionPrefs, so an uninstall removes it too.
inline constexpr char kExtensionPanelWidthPref[] = "atrium_side_panel_width";

// A `window_width` of 0 or less means the window is not laid out yet, so only
// the floor applies. The floor beats the half-window cap in a narrow window.
int ClampExtensionPanelWidth(int width, int window_width);

int ResolveExtensionPanelWidth(std::optional<int> stored, int window_width);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTENSION_PANEL_WIDTH_H_
