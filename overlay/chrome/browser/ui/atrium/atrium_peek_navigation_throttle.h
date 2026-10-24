// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_peek_navigation_throttle.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_PEEK_NAVIGATION_THROTTLE_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_PEEK_NAVIGATION_THROTTLE_H_

#include "content/public/browser/navigation_throttle.h"

namespace content {
class NavigationThrottleRegistry;
}  // namespace content

namespace atrium {

// Auto-peek's trigger. An ordinary link click never emits the OpenURL IPC that
// reaches Browser::OpenURLFromTab, so the automatic pinned-or-favourited peek
// cannot hook there. It lives here instead, at the navigation-throttle layer,
// which does see every browser-mediated navigation including a same-tab link
// click. The shift+click peek keeps its own hook and is unaffected.
//
// At WillStartRequest, an off-site link click with a user gesture from a
// pinned or favourited tab posts the peek and cancels the navigation, leaving
// the pinned tab where it was. Everything else proceeds untouched, and so does
// every case where a predicate fails - this throttle never cancels on a miss.
//
// it used to refuse to register unless a Swift dylib exported
// AtriumIsPeekSourceTab. The answer comes from the statically linked Rust core
// now, which cannot be absent, so the throttle registers unconditionally and
// the cheap content-side gates carry the per-navigation cost.
class AtriumPeekNavigationThrottle : public content::NavigationThrottle {
 public:
  static void MaybeCreateAndAdd(content::NavigationThrottleRegistry& registry);

  explicit AtriumPeekNavigationThrottle(
      content::NavigationThrottleRegistry& registry);
  ~AtriumPeekNavigationThrottle() override;

  AtriumPeekNavigationThrottle(const AtriumPeekNavigationThrottle&) = delete;
  AtriumPeekNavigationThrottle& operator=(const AtriumPeekNavigationThrottle&) =
      delete;

  // content::NavigationThrottle:
  ThrottleCheckResult WillStartRequest() override;
  const char* GetNameForLogging() override;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_PEEK_NAVIGATION_THROTTLE_H_
