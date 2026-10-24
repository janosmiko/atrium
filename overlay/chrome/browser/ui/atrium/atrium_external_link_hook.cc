// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_external_link_hook.cc
#include "chrome/browser/ui/atrium/atrium_external_link_hook.h"

#include "base/logging.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_external_link.h"
#include "url/gurl.h"

namespace atrium {

bool HandleExternalLinks(const std::vector<GURL>& urls) {
  if (urls.empty()) {
    return false;
  }
  // Every url or none. The decision is per-url because the scheme is, so a
  // list carrying a mailto: alongside a page is not one Atrium answers for -
  // splitting it would open half the handover in one window type and half in
  // another, which the user cannot see a reason for.
  const bool setting_enabled = ExternalLinksOpenInLittle();
  for (const GURL& url : urls) {
    if (!ExternalLinkOpensLittleWindow(setting_enabled, url.is_valid(),
                                       url.SchemeIsHTTPOrHTTPS())) {
      return false;
    }
  }
  for (const GURL& url : urls) {
    if (!BrowserController::CreateLittleWindowForExternalLink(url)) {
      // No resident profile. Nothing has opened yet on the first url, so the
      // caller's stock path still has the whole list; a later url failing is
      // not reachable, because the profile that answered the first is still
      // there.
      return false;
    }
  }
  VLOG(1) << "Atrium: external link opened a little window";
  return true;
}

}  // namespace atrium
