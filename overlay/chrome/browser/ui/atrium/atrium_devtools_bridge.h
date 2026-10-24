// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_DEVTOOLS_BRIDGE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_DEVTOOLS_BRIDGE_H_

class BrowserWindowInterface;

namespace content {
class WebContents;
}  // namespace content

namespace atrium {

// Docked DevTools, split-view Phase C: true when `browser`'s
// AtriumBrowserWindow can host docked DevTools inline in the inspected
// tab's pane. Dependency-free seam header (mirrors atrium_feature_check.h's
// IsAtriumBrowserWindowEnabled/GetTabModalDialogHostOverride split) so
// chrome/browser/devtools can consult it - from DevToolsWindow::Create's
// can-dock probe (devtools_window.cc, ~line 1392) - without depending on the
// rest of the mac-only atrium sources, which live inline in
// //chrome/browser/ui. Always false where Atrium never creates a
// BrowserWindow (dylib not connected, or non-mac) or `browser` isn't
// Atrium-managed. The mac definition lives in atrium_browser_controller.cc,
// not atrium_devtools_bridge.cc, for the same chromium-rawptr reason
// GetTabModalDialogHostOverride's does - see that header's doc.
bool AtriumCanDockDevtools(BrowserWindowInterface* browser);

// whether `web_contents` is one of Atrium's own chrome surfaces - the
// sidebar, the palette, the notes pad and their siblings.
//
// They are built by AtriumChromeContext in the OTR System Profile, and that
// profile has no ThemeService, no CookieSettings and presumably more besides:
// ProfileSelections names WithRegular, WithGuest and WithAshInternals, and the
// System Profile falls through to kNone. Building a DevTools FRONTEND against one
// dereferences those missing services in turn, so opening a window on a surface
// is refused rather than guarded service by service.
//
// CDP attachment is untouched. The frontend is what needs the profile services;
// attaching an agent host does not, which is what the scenario harness and every
// remote-debugging session rely on.
bool AtriumIsChromeSurface(content::WebContents* web_contents);

// Re-runs the docked-devtools layout for `browser`'s presented
// AtriumBrowserWindow against `inspected_web_contents` (may be null - hides
// docked devtools). Called from the same three stock seams
// DevtoolsUIController::UpdateDevtools would otherwise own for a BrowserView
// window - DevToolsWindow::Show()'s inline dispatch (devtools_window.cc,
// ~line 1085) and DevToolsWindow::UpdateBrowserWindow()
// (devtools_window_views.cc, ~line 138) - each gated on
// DevtoolsUIController::From(browser) returning null, i.e. reached only for
// an Atrium-managed browser or a browser type Atrium never manages. A no-op
// (touches no view) when AtriumCanDockDevtools(browser) is false, so a
// vanilla/degraded build (dylib never connects) never runs this path -
// byte-identical stock behavior at every call site.
void MaybeUpdateAtriumDockedDevtools(
    BrowserWindowInterface* browser,
    content::WebContents* inspected_web_contents);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_DEVTOOLS_BRIDGE_H_
