// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_status_bubble_hook.cc
#include "chrome/browser/ui/atrium/atrium_status_bubble_hook.h"

#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "url/gurl.h"

namespace atrium {

void MaybeUpdateStatusBubbleURL(Browser* browser,
                                content::WebContents* source,
                                const GURL& url) {
  // ControllerForBrowser returning non-null is the "this is an Atrium-managed
  // Browser" gate (same one MaybeOpenPeek uses) - it is what makes the
  // following static_cast safe, same invariant BrowserController::window()
  // itself relies on (that accessor is private, so it can't be called from
  // here directly; this mirrors its implementation instead).
  if (!BrowserController::ControllerForBrowser(browser)) {
    return;
  }
  // In single-window mode a cross-profile switch swaps content into the
  // slot's presenter window, so the hover bubble must land there, not on
  // `browser`'s own (possibly invisible) window - `source` may belong to a
  // controller that isn't currently on screen. Falls back to browser->
  // window() when no presenter is mapped (mirrors every other
  // PresentedWindowForBrowser call site).
  AtriumBrowserWindow* window =
      BrowserController::PresentedWindowForBrowser(browser);
  if (!window) {
    window = static_cast<AtriumBrowserWindow*>(browser->window());
  }
  window->SetStatusBubbleURL(source, url);
}

}  // namespace atrium
