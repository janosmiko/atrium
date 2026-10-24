// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_profile_page_url.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_PROFILE_PAGE_URL_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_PROFILE_PAGE_URL_H_

#include <string>
#include <string_view>

namespace atrium {

// The pages a settings row may open in a profile's own tab. A name this
// build does not know answers with an empty string, so the caller drops the
// message rather than navigating somewhere it was not asked to.
std::string ProfilePageUrl(std::string_view page);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_PROFILE_PAGE_URL_H_
