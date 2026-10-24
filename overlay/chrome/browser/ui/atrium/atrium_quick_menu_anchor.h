// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_QUICK_MENU_ANCHOR_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_QUICK_MENU_ANCHOR_H_

#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"

namespace atrium {

// Air between the puzzle button and the card under it.
inline constexpr int kQuickMenuAnchorGap = 6;

// How close the card may come to a window edge it has been pushed against.
inline constexpr int kQuickMenuEdgeInset = 6;

// What the widget is sized to before the renderer reports its own size.
// The width is the card's own (quickmenu.css). The height is a guess the
// first ResizeDueToAutoResize replaces.
inline constexpr gfx::Size kQuickMenuFallbackSize(232, 220);

// The menu's rect in the parent window's own coordinates: top-left sits
// kQuickMenuAnchorGap below the anchor's bottom, left aligned with it, slid
// back inside `window` when it would hang off an edge.
gfx::Rect PlaceQuickMenu(const gfx::Rect& anchor_in_window,
                         const gfx::Size& window,
                         const gfx::Size& menu);

// Air between a space dot and the picker card.
inline constexpr int kSpaceIconPickerAnchorGap = 6;

// How close the card may come to a window edge it has been pushed against.
inline constexpr int kSpaceIconPickerEdgeInset = 6;

// What the widget is sized to before the renderer reports its own size.
inline constexpr gfx::Size kSpaceIconPickerFallbackSize(264, 220);

// Below the dot by default, flipped above when it would run off the bottom
// edge - the dots sit at the sidebar's own bottom, so that is the common case.
gfx::Rect PlaceSpaceIconPicker(const gfx::Rect& anchor_in_window,
                               const gfx::Size& window,
                               const gfx::Size& picker);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_QUICK_MENU_ANCHOR_H_
