// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_sad_tab_hook.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SAD_TAB_HOOK_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SAD_TAB_HOOK_H_

namespace content {
class WebContents;
}  // namespace content

namespace views {
class WebView;
}  // namespace views

namespace atrium {

// where sad_tab_controller.cc parents a SadTabView when
// BrowserView::GetBrowserViewForBrowser answers null, which it always does
// here - an AtriumBrowserWindow is not a BrowserView and its pane is a plain
// views::WebView, not a ContentsWebView. Without it the SadTabView stays
// unparented and a dead renderer leaves the card blank. Null when no Atrium
// pane shows `web_contents`.
views::WebView* PaneWebViewForSadTab(content::WebContents* web_contents);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SAD_TAB_HOOK_H_
