// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_window_corner_radius.h"

#import <Cocoa/Cocoa.h>

#include <cstdlib>

#include "base/strings/string_number_conversions.h"

// Private AppKit selector: the frame view's cached window corner radius.
@interface NSView (AtriumWindowCornerRadius)
- (CGFloat)_getCachedWindowCornerRadius;
@end

namespace atrium {

float AtriumWindowCornerRadius() {
  static const float radius = [] {
    if (const char* override_radius = getenv("ATRIUM_WINDOW_CORNER_RADIUS")) {
      double parsed = 0;
      if (base::StringToDouble(override_radius, &parsed) && parsed > 0) {
        return static_cast<float>(parsed);
      }
    }
    if (@available(macOS 27.0, *)) {
      return kWindowCornerRadius;
    }
    // Before macOS 27, Safari and Finder used the toolbar-bearing AppKit
    // window radius. Keep following that older default instead of tightening
    // windows on systems where Apple did not make the matching change.
    NSWindow* probe =
        [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 64, 64)
                                    styleMask:NSWindowStyleMaskTitled
                                      backing:NSBackingStoreBuffered
                                        defer:NO];
    probe.toolbar = [[NSToolbar alloc] initWithIdentifier:@"AtriumRadiusProbe"];
    NSView* probe_frame = probe.contentView.superview;
    if ([probe_frame
            respondsToSelector:@selector(_getCachedWindowCornerRadius)]) {
      const float probed =
          static_cast<float>([probe_frame _getCachedWindowCornerRadius]);
      if (probed > 0) {
        return probed;
      }
    }
    return kPreMacOS27WindowCornerRadius;
  }();
  return radius;
}

}  // namespace atrium
