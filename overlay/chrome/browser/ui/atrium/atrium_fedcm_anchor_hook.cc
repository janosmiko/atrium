// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_fedcm_anchor_hook.cc
#include "chrome/browser/ui/atrium/atrium_fedcm_anchor_hook.h"

#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"

namespace atrium {

views::View* GetFedCmFallbackAnchorView(
    BrowserWindowInterface* browser_window_interface) {
  if (!browser_window_interface) {
    return nullptr;
  }
  Browser* browser = browser_window_interface->GetBrowserForMigrationOnly();
  // ControllerForBrowser returning non-null is the "this is an
  // Atrium-managed Browser" gate that makes the static_cast below safe -
  // same invariant atrium_status_bubble_hook.cc's MaybeUpdateStatusBubbleURL
  // relies on.
  if (!browser || !BrowserController::ControllerForBrowser(browser)) {
    return nullptr;
  }
  AtriumBrowserWindow* window =
      static_cast<AtriumBrowserWindow*>(browser->window());
  return window->GetAutofillBubbleAnchor().GetIfView();
}

}  // namespace atrium
