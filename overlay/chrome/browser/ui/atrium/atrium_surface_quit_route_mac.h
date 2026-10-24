// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_surface_quit_route_mac.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SURFACE_QUIT_ROUTE_MAC_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SURFACE_QUIT_ROUTE_MAC_H_

namespace views {
class Widget;
}

namespace atrium {

// gives an activatable Atrium surface window the quit route that
// atrium_surface_key_route.h decides, without asking the surface to own an
// AppKit delegate of its own.
//
// The gap this closes is the one on the palette and the peek
// card, reached a third time: a window that hosts a live WebContents never
// declines a key equivalent, so its RenderWidgetHostViewCocoa claims Cmd+Q and
// answers the renderer asynchronously, and no later main-menu route runs. The
// chord has to leave on the way IN, from prePerformKeyEquivalent.
//
// The settings window is the caller. It is a chrome-layer window with a live
// page and no CommandDispatcher delegate at all, so Cmd+Q vanished into its
// renderer and the process outlived the chord.
//
// Nothing else changes: every chord but the quit is declined, exactly as an
// absent delegate declines it, so installing this cannot alter what the
// surface's own page or the responder chain already do.
//
// No AppKit type appears here, so a portable .cc can call it. The palette and
// the peek card still carry their own delegates because theirs also run Atrium
// hotkey actions against a parent browser window; folding those onto this
// belongs with the duplicate-chord cleanup.
void InstallQuitRouteOnSurfaceWindow(views::Widget* widget);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SURFACE_QUIT_ROUTE_MAC_H_
