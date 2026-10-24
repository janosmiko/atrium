// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_external_link_hook.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTERNAL_LINK_HOOK_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTERNAL_LINK_HOOK_H_

#include <vector>

class GURL;

namespace atrium {

// called from -[AppController application:openURLs:] with the
// URLs another macOS app handed over, before they reach the stock
// openUrlsReplacingNTP path.
//
// Returns true when Atrium opened a little window for EVERY url (the caller
// drops them); false when it opened none, and the stock path runs unchanged
// with the whole list. There is no half answer on purpose - a partial handover
// would open some links in one window type and some in another for reasons the
// user cannot see, and this hook fails soft rather than clever.
//
// False, and today's behaviour, on: the setting off, a non-http(s) url, an
// empty list, and no resident profile to open with (a link that COLD-STARTS
// Atrium arrives before any controller exists - see the .cc).
//
// Thin forwarder, matching the atrium_*_hook convention, so
// app_controller_mac.mm does not include the controller header.
bool HandleExternalLinks(const std::vector<GURL>& urls);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_EXTERNAL_LINK_HOOK_H_
