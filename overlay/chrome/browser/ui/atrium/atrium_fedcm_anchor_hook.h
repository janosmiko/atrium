// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_fedcm_anchor_hook.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_FEDCM_ANCHOR_HOOK_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_FEDCM_ANCHOR_HOOK_H_

class BrowserWindowInterface;

namespace views {
class View;
}  // namespace views

namespace atrium {

// fallback anchor for the FedCM accounts bubble
// (FedCmAccountSelectionView::GetAnchorView) on a chromeless Atrium window.
// BrowserElementsViews::RetrieveView(kActiveContentsWebViewRetrievalId)
// always misses there - that retrieval callback is registered only by
// BrowserWindowFeatures::InitPostBrowserViewConstruction, which runs solely
// for BrowserView-backed windows - so the bubble would otherwise get a null
// anchor_view and crash at the has_parent()-but-no-anchor_widget() DCHECK in
// ui/views/bubble/bubble_dialog_delegate_view.cc's CreateBubbleWidget.
//
// Returns the active content pane view - the same anchor
// AtriumBrowserWindow::GetAutofillBubbleAnchor already uses for the
// identical toolbar-less reason - or null if `browser_window_interface`
// isn't Atrium-managed or has no active pane. Safe to call unconditionally;
// never null-derefs. Same "thin C++ header, ObjC++ .mm" split as
// atrium_status_bubble_hook.h (this header parses fine from the
// cross-platform fedcm_account_selection_view_desktop.cc TU; the real logic
// lives in the .mm where the full ObjC++ atrium_browser_window.h is safe to
// include).
views::View* GetFedCmFallbackAnchorView(
    BrowserWindowInterface* browser_window_interface);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_FEDCM_ANCHOR_HOOK_H_
