// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_status_bubble_hook.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_STATUS_BUBBLE_HOOK_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_STATUS_BUBBLE_HOOK_H_

class Browser;
class GURL;

namespace content {
class WebContents;
}  // namespace content

namespace atrium {

// Link-hover status bubble: called from browser.cc's
// Browser::UpdateTargetURL for every WebContentsDelegate::UpdateTargetURL,
// i.e. whenever the hovered link's target URL changes (including clearing to
// empty on mouse-out). Resolves `browser`'s BrowserController/
// AtriumBrowserWindow and forwards to AtriumBrowserWindow::SetStatusBubbleURL
// - a no-op if there is no controller for this Browser (matches
// MaybeOpenPeek's fallback contract).
//
// Deliberately NOT routed through BrowserWindow::GetStatusBubbles(): stock
// Browser::UpdateTargetURL/ContentsMouseEvent `static_cast` every entry of
// that vector straight to StatusBubbleViews* (and its base_view() further to
// ContentsWebView*) with no dynamic check - AtriumBrowserWindow's real
// content view is a plain views::WebView, not a ContentsWebView, so feeding a
// custom StatusBubble through that vector would be an unchecked, invalid
// cast. GetStatusBubbles() stays stubbed (returns {}), and this free function
// is the seam instead - same "thin C++ header, ObjC++ .mm" split as
// atrium_peek_intercept.h (this header parses fine from browser.cc's plain
// C++ TU; the real logic lives in the .mm where the full ObjC++
// atrium_browser_window.h is safe to include).
void MaybeUpdateStatusBubbleURL(Browser* browser,
                                content::WebContents* source,
                                const GURL& url);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_STATUS_BUBBLE_HOOK_H_
