// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_profile_window_decision.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_PROFILE_WINDOW_DECISION_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_PROFILE_WINDOW_DECISION_H_

#include <string>
#include <vector>

namespace atrium {

// One live Atrium window as BrowserForProfileId's scan sees it. `realised`
// (an unrealised window aborts on AddTabAt) and `little` (chromeless,
// no room for a settings page) both hold for a hidden bootstrap placeholder
// too, so `shown` (AtriumBrowserWindow::IsExcludedFromWindowCycling, negated)
// is what excludes it - unlike widget visibility this is synchronous, so a
// window mid-startup-hold still counts.
struct ProfileWindowCandidate {
  std::string profile_id;
  bool realised = false;
  bool little = false;
  bool shown = false;
};

// The index of the window a data link for `profile_id` opens in, or -1 for
// "none of these; open one". `candidates` is in ACTIVATION order, most
// recently activated first, so the first match is the last active window on
// that profile.
int ChooseProfileWindow(const std::string& profile_id,
                        const std::vector<ProfileWindowCandidate>& candidates);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_PROFILE_WINDOW_DECISION_H_
