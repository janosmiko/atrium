// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_devbar_layout.h"

#include <algorithm>

namespace atrium {

gfx::Rect DevBarBoundsInRect(const gfx::Rect& pane) {
  const int width =
      std::max(0, std::min(kDevBarMaxWidth + 2 * kDevBarShadowPadX,
                           pane.width() - 2 * kDevBarMargin));
  return gfx::Rect(pane.x() + (pane.width() - width) / 2,
                   pane.y() + kDevBarMargin - kDevBarShadowPadTop, width,
                   kDevBarHeight + kDevBarShadowPadTop +
                       kDevBarShadowPadBottom);
}

}  // namespace atrium
