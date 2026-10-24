// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_feature_check.h"

#include "build/build_config.h"

#if BUILDFLAG(IS_MAC)
#endif  // BUILDFLAG(IS_MAC)

namespace atrium {

bool IsAtriumBrowserWindowEnabled() {
#if BUILDFLAG(IS_MAC)
  // was UIBridge::Get()->connected(). The dylib is gone, and
  // gating the whole window on it left the browser with no Atrium chrome at
  // all after the cut.
  return true;
#else
  return false;
#endif  // BUILDFLAG(IS_MAC)
}

#if !BUILDFLAG(IS_MAC)
// The mac definition lives in atrium_browser_controller.cc: it needs the
// controller/window headers, whose NSView* members trip the chromium-rawptr
// clang plugin when included from a plain C++ TU like this one (.mm TUs are
// exempt, and those headers are ObjC++-only by design). This file stays the
// every-platform home of the null stub only.
views::Widget* GetTabModalDialogHostOverride(
    BrowserWindowInterface* browser_window_interface) {
  return nullptr;
}

// The mac definition lives in atrium_browser_controller.cc, same reason as
// GetTabModalDialogHostOverride above.
bool OpenLinkInAtriumSplit(BrowserWindowInterface* browser_window_interface,
                           content::WebContents* source_web_contents,
                           const content::OpenURLParams& url_params,
                           bool stacked) {
  return false;
}

// The mac definition lives in atrium_browser_controller.cc, same reason as
// GetTabModalDialogHostOverride above.
bool OpenLinkInAtriumLittleWindow(
    BrowserWindowInterface* browser_window_interface,
    const content::OpenURLParams& url_params) {
  return false;
}

bool HighlightSelectionInAtriumNotes(
    BrowserWindowInterface* browser_window_interface,
    const std::string& quote) {
  return false;
}

// The mac definition lives in atrium_browser_controller.cc, same reason as
// GetTabModalDialogHostOverride above.
bool IsAtriumBrowserWindowSplit(
    BrowserWindowInterface* browser_window_interface,
    bool* stacked) {
  return false;
}
#endif  // !BUILDFLAG(IS_MAC)

}  // namespace atrium
