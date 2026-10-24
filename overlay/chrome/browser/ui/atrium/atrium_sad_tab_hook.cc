// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_sad_tab_hook.cc
#include "chrome/browser/ui/atrium/atrium_sad_tab_hook.h"

#include "base/logging.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"

namespace atrium {

namespace {

views::WebView* PaneWebViewIn(AtriumBrowserWindow* window,
                              content::WebContents* web_contents) {
  return window ? window->PaneWebViewForContents(web_contents) : nullptr;
}

}  // namespace

views::WebView* PaneWebViewForSadTab(content::WebContents* web_contents) {
  if (!web_contents) {
    return nullptr;
  }
  tabs::TabInterface* tab =
      tabs::TabInterface::MaybeGetFromContents(web_contents);
  if (!tab) {
    return nullptr;
  }
  BrowserWindowInterface* browser_window_interface =
      tab->GetBrowserWindowInterface();
  if (!browser_window_interface) {
    return nullptr;
  }
  Browser* browser = browser_window_interface->GetBrowserForMigrationOnly();
  // ControllerForBrowser returning non-null is the "this is an Atrium-managed
  // Browser" gate that makes the static_cast below safe - same invariant
  // atrium_fedcm_anchor_hook.cc relies on.
  if (!browser || !BrowserController::ControllerForBrowser(browser)) {
    return nullptr;
  }
  // A cross-profile in-window swap leaves the tab's own Browser
  // pointing at a window that is not on screen, so the presenter is asked
  // first - same order as atrium_status_bubble_hook.cc.
  views::WebView* pane_web_view = PaneWebViewIn(
      BrowserController::PresentedWindowForBrowser(browser), web_contents);
  if (!pane_web_view) {
    pane_web_view = PaneWebViewIn(
        static_cast<AtriumBrowserWindow*>(browser->window()), web_contents);
  }
  if (pane_web_view) {
    VLOG(1) << "Atrium: sad tab pane for tab " << TabId(web_contents);
  }
  return pane_web_view;
}

}  // namespace atrium
