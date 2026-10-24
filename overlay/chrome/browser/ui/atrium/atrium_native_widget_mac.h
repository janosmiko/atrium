// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_native_widget_mac.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_NATIVE_WIDGET_MAC_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_NATIVE_WIDGET_MAC_H_

class Browser;

namespace views {
class NativeWidget;
class Widget;
}  // namespace views

namespace atrium {

class AtriumBrowserWindow;

// The native widget behind an Atrium window on macOS: a chromeless frame, the
// command dispatcher that gives user-bound hotkeys first refusal, and the
// command routing that sends tab-scoped commands to the VISIBLE tab's browser
// rather than this window's own.
//
// returned as views::NativeWidget so the caller needs no
// platform type, and the class itself stays private to the implementation.
// Each port supplies its own file of the same shape.
//
// `window` is passed directly since browser->window() is still unset here.
views::NativeWidget* CreateAtriumNativeWidget(Browser* browser,
                                              views::Widget* widget,
                                              AtriumBrowserWindow* window);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_NATIVE_WIDGET_MAC_H_
