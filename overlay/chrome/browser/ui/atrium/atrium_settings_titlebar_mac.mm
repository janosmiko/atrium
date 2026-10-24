// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_settings_titlebar_mac.mm
#import "chrome/browser/ui/atrium/atrium_settings_titlebar_mac.h"

#import <AppKit/AppKit.h>

#include "base/logging.h"
#include "skia/ext/skia_utils_mac.h"
#include "ui/views/widget/widget.h"

namespace atrium {

void RemoveSettingsWindowTitlebar(views::Widget* widget) {
  if (!widget) {
    return;
  }
  NSWindow* ns_window = widget->GetNativeWindow().GetNativeNSWindow();
  if (!ns_window) {
    return;
  }
  ns_window.styleMask |= NSWindowStyleMaskFullSizeContentView;
  ns_window.titlebarAppearsTransparent = YES;
  ns_window.titleVisibility = NSWindowTitleHidden;
}

void InstallSettingsWindowVibrancy(views::Widget* widget) {
  if (!widget) {
    return;
  }
  NSWindow* ns_window = widget->GetNativeWindow().GetNativeNSWindow();
  if (!ns_window || !ns_window.contentView) {
    return;
  }
  NSVisualEffectView* vibrancy =
      [[NSVisualEffectView alloc] initWithFrame:ns_window.contentView.bounds];
  vibrancy.material = NSVisualEffectMaterialSidebar;
  vibrancy.blendingMode = NSVisualEffectBlendingModeBehindWindow;
  vibrancy.state = NSVisualEffectStateFollowsWindowActiveState;
  vibrancy.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [ns_window.contentView addSubview:vibrancy
                         positioned:NSWindowBelow
                         relativeTo:nil];
  VLOG(1) << "Atrium: settings window vibrancy installed";
}

SkColor SystemWindowBackgroundColor(bool dark) {
  NSAppearance* appearance = [NSAppearance
      appearanceNamed:dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
  __block SkColor color = SK_ColorWHITE;
  [appearance performAsCurrentDrawingAppearance:^{
    color = skia::NSSystemColorToSkColor(NSColor.windowBackgroundColor);
  }];
  return color;
}

}  // namespace atrium
