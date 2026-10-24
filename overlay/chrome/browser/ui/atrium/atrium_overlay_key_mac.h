// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_overlay_key_mac.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_OVERLAY_KEY_MAC_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_OVERLAY_KEY_MAC_H_

#import <AppKit/AppKit.h>

namespace atrium {

// views::Widget::IsActive() keeps answering true after a sibling browser
// window takes key, so only the live key window says whose group holds it.
inline bool KeyWindowIsInGroup(NSWindow* bar, NSWindow* parent) {
  for (NSWindow* window = NSApp.keyWindow; window != nil;
       window = window.parentWindow) {
    if (window == bar || window == parent) {
      return true;
    }
  }
  return false;
}

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_OVERLAY_KEY_MAC_H_
