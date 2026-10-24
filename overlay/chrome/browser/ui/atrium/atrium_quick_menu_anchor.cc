// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
#include "chrome/browser/ui/atrium/atrium_quick_menu_anchor.h"

#include <algorithm>

namespace atrium {

namespace {

// Slides `value` back inside [inset, available - inset - size]. The upper
// clamp runs first so a card wider/taller than the window still starts
// inside it rather than at a negative coordinate.
int ClampToWindow(int value, int inset, int available, int size) {
  value = std::min(value, available - inset - size);
  value = std::max(value, inset);
  return value;
}

}  // namespace

gfx::Rect PlaceQuickMenu(const gfx::Rect& anchor_in_window,
                         const gfx::Size& window,
                         const gfx::Size& menu) {
  const int x = ClampToWindow(anchor_in_window.x(), kQuickMenuEdgeInset,
                              window.width(), menu.width());
  const int y = ClampToWindow(anchor_in_window.bottom() + kQuickMenuAnchorGap,
                              kQuickMenuEdgeInset, window.height(),
                              menu.height());
  return gfx::Rect(x, y, menu.width(), menu.height());
}

gfx::Rect PlaceSpaceIconPicker(const gfx::Rect& anchor_in_window,
                               const gfx::Size& window,
                               const gfx::Size& picker) {
  const int x =
      ClampToWindow(anchor_in_window.x(), kSpaceIconPickerEdgeInset,
                    window.width(), picker.width());

  int y = anchor_in_window.bottom() + kSpaceIconPickerAnchorGap;
  const int fits_below =
      y + picker.height() + kSpaceIconPickerEdgeInset <= window.height();
  if (!fits_below) {
    const int above = anchor_in_window.y() - kSpaceIconPickerAnchorGap -
                      picker.height();
    if (above >= kSpaceIconPickerEdgeInset) {
      y = above;
    }
  }
  y = ClampToWindow(y, kSpaceIconPickerEdgeInset, window.height(),
                    picker.height());

  return gfx::Rect(x, y, picker.width(), picker.height());
}

}  // namespace atrium
