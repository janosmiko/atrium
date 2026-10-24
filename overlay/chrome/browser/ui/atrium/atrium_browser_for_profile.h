// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_browser_for_profile.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_BROWSER_FOR_PROFILE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_BROWSER_FOR_PROFILE_H_

#include <string>

class Browser;

namespace atrium {

// The Browser a tab for `profile_id` belongs in: the last active realised,
// shown, non-little Atrium window on that profile, or a freshly opened one
// when no such window exists. Null when none could be opened.
Browser* BrowserForProfileId(const std::string& profile_id);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_BROWSER_FOR_PROFILE_H_
