// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_extension_panel_width.h"

#include <algorithm>

namespace atrium {

int ClampExtensionPanelWidth(int width, int window_width) {
  if (window_width <= 0) {
    return std::max(width, kExtensionPanelMinWidth);
  }
  const int max_width =
      std::max(kExtensionPanelMinWidth,
               static_cast<int>(window_width * kExtensionPanelMaxWidthFraction));
  return std::clamp(width, kExtensionPanelMinWidth, max_width);
}

int ResolveExtensionPanelWidth(std::optional<int> stored, int window_width) {
  return ClampExtensionPanelWidth(stored.value_or(kExtensionPanelDefaultWidth),
                                  window_width);
}

}  // namespace atrium
