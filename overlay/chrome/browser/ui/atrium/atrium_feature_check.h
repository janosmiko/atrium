// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_FEATURE_CHECK_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_FEATURE_CHECK_H_

#include <string>

class BrowserWindowInterface;

namespace views {
class Widget;
}  // namespace views

namespace content {
class WebContents;
struct OpenURLParams;
}  // namespace content

namespace atrium {

// True once the AtriumUI dylib is connected, i.e. AtriumBrowserWindow (not
// BrowserView) is serving as BrowserWindow for this process. Split into its
// own header/target (mirrors webui_browser::IsWebUIBrowserEnabled() and its
// ":headers" target) so callers outside chrome/browser/ui/atrium, such as
// chrome/browser/ui/read_anything, can query it without depending on the
// rest of the mac-only atrium sources. Defined on every platform; always
// false where Atrium never creates a BrowserWindow.
bool IsAtriumBrowserWindowEnabled();

// overrides BrowserElementsViewsImpl::GetPrimaryWindowWidget() for
// `browser_window_interface`'s Browser when its own AtriumBrowserWindow is
// currently swapped out of view during a cross-profile in-window presenter
// swap (BrowserController::ActivateWindow's kInWindowSwap branch) - the tab's
// own window stays hidden while a different profile's window presents its
// WebContents, so the stock per-window resolution would hand
// TabDialogManager a widget the user cannot see (CreateTabScopedDialog parents
// the dialog's NativeView there; GetModalDialogBounds's screen-coordinate
// adjustment reads it too). Returns null (stock resolution, unchanged) when
// `browser_window_interface` isn't Atrium-managed or is already its own
// slot's presenter - the overwhelmingly common case.
//
// SCOPE: because the hook lives in the shared
// GetPrimaryWindowWidget() accessor, it redirects EVERY consumer of that
// accessor during a swap, not only TabDialogManager - today that's also the
// toast controller's focus-clear, the Lens overlay's native-focus clear,
// and the bookmarks side-panel context-menu anchor. Deliberate: during a
// swap the presenter is the ONLY visible surface, so anything that wants
// "the window the user is looking at" must get the presenter; anchoring or
// clearing focus against the hidden widget is exactly the bug class this
// fixes. A consumer that ever needs the tab's OWN (hidden) widget during a
// swap must not use GetPrimaryWindowWidget() for it.
//
// Split into this
// dependency-free header for the same reason as IsAtriumBrowserWindowEnabled
// above: callers like browser_elements_views_impl.cc
// (//chrome/browser/ui/views/interaction:impl) must not depend on the rest of
// the mac-only atrium sources, which live inline in //chrome/browser/ui.
// Always null on non-mac platforms.
views::Widget* GetTabModalDialogHostOverride(
    BrowserWindowInterface* browser_window_interface);

// re-routes "Open Link in Split View" into Atrium's own
// AtriumPaneContainerView split instead of stock split_tabs/TabCollection,
// which NOTREACHEDs for an Atrium window (AddToNewSplit ->
// TabCollection::GetTabAtIndexRecursive - Atrium's TabStripModel never
// populates the collection tree stock split relies on). Split into this
// dependency-free header for the same reason as the declarations above:
// render_view_context_menu.cc must not depend on the rest of the mac-only
// atrium sources.
//
// `browser_window_interface`: the window the context menu was invoked in.
// `source_web_contents`: the tab the link was right-clicked on - used to find
// this window's OTHER split pane when it's already split.
// `url_params`: the link's navigation params (url/referrer/initiator_origin),
// built by the caller exactly as stock's OpenLinkInSplitView builds them.
// `stacked`: true for a top/bottom split, false for side-by-side. A bool
// rather than atrium's own SplitOrientation enum, again to keep this header
// free of the mac-only atrium_browser_window.h dependency.
//
// Returns false (no-op) if `browser_window_interface` isn't Atrium-managed.
// Callers must still gate on IsAtriumBrowserWindowEnabled() themselves before
// choosing this path over stock's AddToNewSplit - same posture as every
// other call in this header. Always false stub on non-mac platforms.
bool OpenLinkInAtriumSplit(BrowserWindowInterface* browser_window_interface,
                           content::WebContents* source_web_contents,
                           const content::OpenURLParams& url_params,
                           bool stacked);

// Cmd+Option on a link: shows the link in a brand-new little window
// (BrowserController::CreateLittleWindow). Takes the whole `url_params`, not
// just the url, so the window's one navigation carries the link's referrer and
// initiator origin the way OpenLinkInAtriumSplit's does. Declared here, not in
// atrium_browser_controller.h, for the same reason as the declarations above -
// browser.cc must not depend on the rest of the mac-only atrium sources.
// False (no-op) when the window isn't Atrium-managed, so the caller can fall
// back. Always false stub on non-mac platforms.
bool OpenLinkInAtriumLittleWindow(
    BrowserWindowInterface* browser_window_interface,
    const content::OpenURLParams& url_params);

// stores the page selection as a Marginalia highlight on the
// active tab's URL and opens the Notes pad beside it. Split into this
// dependency-free header for the same reason as the declarations above:
// render_view_context_menu.cc must not depend on the rest of the mac-only
// atrium sources.
//
// `quote` is the raw selected text. Returns false - and stores nothing - when
// the window is not Atrium-managed or the selection is under three
// characters, which is the same rule the store enforces. Always false stub on
// non-mac platforms.
bool HighlightSelectionInAtriumNotes(
    BrowserWindowInterface* browser_window_interface,
    const std::string& quote);

// resolves "is this window already split" via AtriumBrowserWindow::
// IsSplit() rather than stock tabs::TabInterface::IsSplit(), which is always
// false for Atrium tabs for the same reason OpenLinkInAtriumSplit's doc above
// gives. Used to pick the split-view link menu's label/icon so wording
// matches what OpenLinkInAtriumSplit will actually do (replace the partner
// pane vs create a new split) instead of always reading "not split".
//
// `stacked`, if non-null and the window IS split, is set to whether the
// split is top/bottom (true) or side-by-side (false); left untouched
// otherwise. A bool out-param rather than atrium's own SplitOrientation
// enum, same reason OpenLinkInAtriumSplit's `stacked` param above is a bool
// - keeps this header free of the mac-only atrium_browser_window.h
// dependency. Defaults to null so the existing is-it-split-only call site
// doesn't need to change.
//
// Returns false if `browser_window_interface` isn't Atrium-managed. Callers
// must gate on IsAtriumBrowserWindowEnabled() first if they need to
// distinguish "not Atrium" from "Atrium but not split" - same posture as
// every other call in this header. Always false stub on non-mac platforms.
bool IsAtriumBrowserWindowSplit(
    BrowserWindowInterface* browser_window_interface,
    bool* stacked = nullptr);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_FEATURE_CHECK_H_
