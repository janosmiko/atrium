// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_profile_page_url.cc
#include "chrome/browser/ui/atrium/atrium_profile_page_url.h"

#include "chrome/common/webui_url_constants.h"

namespace atrium {

std::string ProfilePageUrl(std::string_view page) {
  const std::string settings = chrome::kChromeUISettingsURL;
  if (page == "privacy") {
    return settings + chrome::kPrivacySubPage;
  }
  if (page == "passwords") {
    return settings + chrome::kPasswordManagerSubPage;
  }
  if (page == "payments") {
    return settings + chrome::kPaymentsSubPage;
  }
  if (page == "notifications") {
    // No constant for this leaf. The subpage constant covers the parent only.
    return settings + chrome::kContentSettingsSubPage + "/notifications";
  }
  if (page == "clearBrowsingData") {
    return settings + chrome::kClearBrowserDataSubPage;
  }
  if (page == "extensions") {
    return chrome::kChromeUIExtensionsURL;
  }
  return std::string();
}

}  // namespace atrium
