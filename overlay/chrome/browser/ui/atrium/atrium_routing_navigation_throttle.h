// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_routing_navigation_throttle.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_ROUTING_NAVIGATION_THROTTLE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_ROUTING_NAVIGATION_THROTTLE_H_

#include "content/public/browser/navigation_throttle.h"

namespace content {
class NavigationThrottleRegistry;
}  // namespace content

namespace atrium {

// Traffic routing: a URL a rule claims opens in that rule's space.
//
// ONE throttle rather than the three hooks the feature used to have. The old
// shape - a chrome::Navigate hook, a window.open hook and a renderer-initiated
// link hook - existed because Swift needed three entry points, not because the
// problem has three cases. A typed URL, a window.open and a link click are all
// navigations, and a throttle sees every browser-mediated one, which is the
// same reason AtriumPeekNavigationThrottle lives at this layer.
//
// Unlike the peek throttle it does NOT require a user gesture, a renderer
// initiator or PAGE_TRANSITION_LINK: a typed URL has none of those and is the
// case routing exists for.
//
// It cancels only when it has already posted the reopen. Every miss, and every
// case where a lookup fails, PROCEEDs - routing is an enhancement, so a URL it
// cannot place must still open where it was going.
class AtriumRoutingNavigationThrottle : public content::NavigationThrottle {
 public:
  static void MaybeCreateAndAdd(content::NavigationThrottleRegistry& registry);

  explicit AtriumRoutingNavigationThrottle(
      content::NavigationThrottleRegistry& registry);
  ~AtriumRoutingNavigationThrottle() override;

  AtriumRoutingNavigationThrottle(const AtriumRoutingNavigationThrottle&) =
      delete;
  AtriumRoutingNavigationThrottle& operator=(
      const AtriumRoutingNavigationThrottle&) = delete;

  // content::NavigationThrottle:
  ThrottleCheckResult WillStartRequest() override;
  const char* GetNameForLogging() override;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_ROUTING_NAVIGATION_THROTTLE_H_
