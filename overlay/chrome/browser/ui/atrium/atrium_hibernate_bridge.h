// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_hibernate_bridge.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_HIBERNATE_BRIDGE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_HIBERNATE_BRIDGE_H_

#include <cstdint>

namespace content {
class WebContents;
}  // namespace content

namespace atrium {

// Applies the hibernate_policy for `contents`'s Space via
// TabLifecycleUnitExternal::SetAutoDiscardable. No-op if either lookup fails.
void ApplyHibernatePolicyToTab(content::WebContents* contents,
                                uint64_t tab_id);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_HIBERNATE_BRIDGE_H_
