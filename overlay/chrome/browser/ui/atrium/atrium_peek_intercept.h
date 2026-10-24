// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_peek_intercept.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_PEEK_INTERCEPT_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_PEEK_INTERCEPT_H_

#include <memory>

class Browser;
class GURL;

namespace content {
class WebContents;
struct OpenURLParams;
struct Referrer;
}  // namespace content

namespace atrium {

// Q1 shift+click intercept (browser.cc's Browser::OpenURLFromTab): resolves
// `browser`'s BrowserController and, if found, opens `url`/`referrer` as a
// peek instead of letting a new window/tab be created - returns true if it
// did (the caller returns early with no NavigateParams/new WebContents).
// Returns false (no controller for this Browser, or a source tab not on
// screen) if the caller should fall through to stock behavior. `source` is
// the tab the click originated from (OpenURLFromTab's own first parameter);
// the shift+click path's existing
// peek_source_tab_id_ derivation (BrowserController::OpenPeek re-reading the
// active tab) is unchanged by this - see that method's doc.
//
// A free function, not a BrowserController static method called directly
// from browser.cc: atrium_browser_controller.h has ObjC-typed (NSView*)
// private member fields that are ARC-managed (memory-safe) under
// Objective-C++ but become plain, unprotected C++ raw pointers - tripping
// the chromium-style raw_ptr clang plugin - when that header is parsed by a
// plain C++ translation unit like browser.cc. This header declares no class
// at all, so browser.cc can include it without pulling those fields into a
// plain-C++ parse; the real call happens in the .mm implementation, where
// the full controller header is safe to include (compiled as
// Objective-C++, the same domain as every other atrium/*.mm file).
bool MaybeOpenPeek(Browser* browser,
                   content::WebContents* source,
                   const GURL& url,
                   const content::Referrer& referrer);

// Downloads an Option-clicked link's target the way upstream would have -
// for a window MaybeOpenPeek above found no controller for, where Blink's
// NEW_WINDOW diversion (navigation_policy.cc) left no other trace of Option.
void DownloadOptionClickedLink(content::WebContents* source,
                               const content::OpenURLParams& params);

// Whether an ordinary link click from `source` to `url` should open the
// Keyhole instead of navigating `source` in place.
//
// the pinned-or-favourited half of the answer comes from the Rust
// core now (atrium_core_is_peek_source_url). It used to come from Swift, and
// that went with the ABI, which is what left the auto path
// inert. Every judgement lives in atrium_peek_decision.h; this function only
// resolves the inputs against live objects.
bool ShouldAutoPeek(Browser* browser,
                    content::WebContents* source,
                    const GURL& url);

// A cancelled navigation loses the connection Chromium was making, and the
// reopen starts a renderer first. `what` names the throttle in the run log.
void PreconnectBeforeRestart(content::WebContents* source,
                             const GURL& url,
                             const char* what);

// Opens the peek. Separate from the decision so the throttle can post it to a
// later run-loop turn - opening one synchronously inside a throttle callback
// re-enters NavigationRequest. `source` is the tab the link was in, and by the
// time this runs a switch may have taken it off screen. The link then opens in
// `source`, as the click would have without a peek.
void OpenAutoPeek(Browser* browser,
                  content::WebContents* source,
                  const GURL& url,
                  const content::Referrer& referrer);

// Whether the popup `source` just opened with window.open should show as a
// peek. Split from the adopt below so browser.cc can ask before it gives up
// ownership, and `targets_another_display` is its reading, not a second one.
bool ShouldPeekPopup(Browser* browser,
                     content::WebContents* source,
                     const GURL& target_url,
                     bool targets_another_display);

// Moves the popup into `browser`'s peek card. Adopting rather than rebuilding
// is the whole mechanism: whatever opener relation content already set (none,
// for a noopener popup), the profile and the partition all survive.

// Returns null once the card holds it, or `contents` back when the card
// refused, in which case the caller still owns the popup and must place it.
std::unique_ptr<content::WebContents> AdoptPopupAsPeek(
    Browser* browser,
    std::unique_ptr<content::WebContents> contents,
    const GURL& target_url);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_PEEK_INTERCEPT_H_
