// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_devtools_bridge.h"

#include "build/build_config.h"

#if !BUILDFLAG(IS_MAC)

namespace atrium {

// The mac definitions live in atrium_browser_controller.cc: they need the
// controller/window headers, whose NSView* members trip the chromium-rawptr
// clang plugin from a plain C++ TU like this one (.mm TUs are exempt, and
// those headers are ObjC++-only by design). This file stays the
// every-platform home of the null stubs only - see atrium_feature_check.cc's
// identical split for GetTabModalDialogHostOverride.
bool AtriumCanDockDevtools(BrowserWindowInterface* browser) {
  return false;
}

bool AtriumIsChromeSurface(content::WebContents* web_contents) {
  return false;
}

void MaybeUpdateAtriumDockedDevtools(
    BrowserWindowInterface* browser,
    content::WebContents* inspected_web_contents) {}

}  // namespace atrium

#endif  // !BUILDFLAG(IS_MAC)
