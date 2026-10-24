// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_TRAFFIC_ROUTING_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_TRAFFIC_ROUTING_H_

#include <optional>
#include <string>

class GURL;

namespace atrium {

// The routing rules every seam consults, held as the JSON the core reads.
//
// Set at startup, and again by the settings window after a save lands
// (AtriumSettingsWindow::OnRoutingSaved).
void SetRoutingRulesJSON(std::string rules_json);

// The space `url` should open in, or nullopt when no rule claims it or the
// matching rule excludes `source_space_id` (empty means unknown).
//
// nullopt is also every failure: a caller that cannot tell "no rule" from
// "could not decide" still opens the url where it was already going.
std::optional<std::string> RouteForURL(const GURL& url,
                                       const std::string& source_space_id);

// Suppresses routing for the tab opened inside its scope.
//
// Every AddTabAt this browser makes is non-routed by construction: it is a
// restore, a favourite, or the routed reopen itself, which must not be
// evaluated a second time. Without that last case two rules pointing at each
// other's spaces bounce one tab between them forever, and no cycle detector
// would be as cheap or as certain as not asking twice.
//
// A plain bool rather than anything per-tab: the reopen is synchronous with the
// decision that caused it, so the window between them cannot hold a second
// navigation.
class ScopedRoutingBypass {
 public:
  ScopedRoutingBypass();
  ~ScopedRoutingBypass();

  ScopedRoutingBypass(const ScopedRoutingBypass&) = delete;
  ScopedRoutingBypass& operator=(const ScopedRoutingBypass&) = delete;

  // Whether a routed reopen is in flight, which is what a seam checks before
  // consulting the rules at all.
  static bool IsActive();

 private:
  const bool previous_;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_TRAFFIC_ROUTING_H_
