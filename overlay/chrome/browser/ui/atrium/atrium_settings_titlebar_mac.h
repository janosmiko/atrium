// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_settings_titlebar_mac.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SETTINGS_TITLEBAR_MAC_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SETTINGS_TITLEBAR_MAC_H_

#include "third_party/skia/include/core/SkColor.h"

namespace views {
class Widget;
}

namespace atrium {

// Hides the settings window's native titlebar and its title text while
// keeping the traffic lights and the resize border, mirroring the browser
// window's chromeless style (atrium_native_widget_mac.mm) without the
// Browser-scoped command dispatch that class also carries. Called after
// Init(), the same post-init seam InstallQuitRouteOnSurfaceWindow uses,
// rather than a second NativeWidgetMac subclass.
void RemoveSettingsWindowTitlebar(views::Widget* widget);

// Puts sidebar vibrancy under the page, the way System Settings looks. The
// widget must be kTranslucent: the page's transparent sidebar column shows the
// blur, its opaque content column covers it.
void InstallSettingsWindowVibrancy(views::Widget* widget);

// NSColor.windowBackgroundColor in the light or dark appearance. Takes the
// mode rather than reading the window, because a theme change reaches views
// before the bridge sets the new NSAppearance on the window.
SkColor SystemWindowBackgroundColor(bool dark);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SETTINGS_TITLEBAR_MAC_H_
