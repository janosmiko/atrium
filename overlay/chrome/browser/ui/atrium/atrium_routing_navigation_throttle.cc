// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_routing_navigation_throttle.cc
#include "chrome/browser/ui/atrium/atrium_routing_navigation_throttle.h"

#include <memory>
#include <string>

#include "base/functional/bind.h"
#include "base/json/json_reader.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "base/values.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_peek_intercept.h"
#include "chrome/browser/ui/atrium/atrium_traffic_routing.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/web_contents.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace atrium {

namespace {

// A window can show another profile's space, so this also requires browser's
// profile to own space_id.
bool SpaceIsActiveForProfile(const std::string& space_id, Browser* browser) {
  char* raw = atrium_core_space_slot_for_id(space_id.c_str());
  if (!raw) {
    return false;
  }
  const std::string slot_json(raw);
  atrium_core_string_free(raw);
  std::optional<base::Value> parsed =
      base::JSONReader::Read(slot_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return false;
  }
  const std::optional<int> index = parsed->GetDict().FindInt("index");
  const std::string* profile_id = parsed->GetDict().FindString("profileId");
  return index.has_value() && profile_id &&
         *index == BrowserController::SpaceIndexForBrowser(browser) &&
         *profile_id == browser->profile()->GetBaseName().AsUTF8Unsafe();
}

// The space the navigation starts in: the tab's own row, or the window's
// space when browser's profile owns it. Empty otherwise.
std::string SourceSpaceIdFor(content::WebContents* source, Browser* browser) {
  if (char* raw = atrium_core_space_id_for_tab(atrium::TabId(source))) {
    const std::string id(raw);
    atrium_core_string_free(raw);
    return id;
  }
  char* raw =
      atrium_core_space_id_at(BrowserController::SpaceIndexForBrowser(browser));
  if (!raw) {
    return std::string();
  }
  const std::string id(raw);
  atrium_core_string_free(raw);
  return SpaceIsActiveForProfile(id, browser) ? id : std::string();
}

// Whether this navigation would route the tab where it already is.
//
// The route's own reopen arrives a task later, with the bypass released, so
// without this the rule matches its own result forever. Two ways to already be
// there: the tab's row lives in the target space, or - for a tab with no row
// yet - the window is showing that space and belongs to the profile that owns
// it.
bool AlreadyInTargetSpace(content::WebContents* source,
                          Browser* browser,
                          const std::string& space_id) {
  if (char* raw = atrium_core_space_id_for_tab(atrium::TabId(source))) {
    const std::string current(raw);
    atrium_core_string_free(raw);
    return current == space_id;
  }
  return SpaceIsActiveForProfile(space_id, browser);
}

// Re-resolves the Browser from a weak handle to the source contents, so a tab
// torn down across the task boundary is a no-op rather than a use-after-free.
// Nothing raw survives the hop, the same rule the peek throttle follows.
void OpenRoutedLater(base::WeakPtr<content::WebContents> weak_source,
                     std::string space_id,
                     GURL url) {
  content::WebContents* source = weak_source.get();
  if (!source) {
    return;
  }
  // Browser is the only BrowserWindowInterface implementation on this
  // platform, so the downcast is safe.
  Browser* browser = static_cast<Browser*>(
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(source));
  if (!browser) {
    return;
  }
  if (BrowserController::OpenRoutedURL(browser, source, space_id, url)) {
    return;
  }
  // The throttle already cancelled the original navigation, so a decline
  // here must not leave the user with nothing - load where they were headed.
  const ScopedRoutingBypass no_routing;
  content::NavigationController::LoadURLParams params(url);
  params.transition_type = ui::PAGE_TRANSITION_TYPED;
  source->GetController().LoadURLWithParams(params);
}

}  // namespace

// static
void AtriumRoutingNavigationThrottle::MaybeCreateAndAdd(
    content::NavigationThrottleRegistry& registry) {
  registry.AddThrottle(
      std::make_unique<AtriumRoutingNavigationThrottle>(registry));
}

AtriumRoutingNavigationThrottle::AtriumRoutingNavigationThrottle(
    content::NavigationThrottleRegistry& registry)
    : content::NavigationThrottle(registry) {}

AtriumRoutingNavigationThrottle::~AtriumRoutingNavigationThrottle() = default;

content::NavigationThrottle::ThrottleCheckResult
AtriumRoutingNavigationThrottle::WillStartRequest() {
  content::NavigationHandle* handle = navigation_handle();

  // Cheap gates first, and a miss always PROCEEDs. No user-gesture or
  // transition-type test here, unlike the peek throttle: a typed URL carries
  // neither and is exactly the case routing exists for.
  if (!handle->IsInPrimaryMainFrame() || handle->IsSameDocument()) {
    return PROCEED;
  }
  if (ScopedRoutingBypass::IsActive()) {
    return PROCEED;  // The routed reopen itself. Deciding twice is the loop.
  }

  const GURL& url = handle->GetURL();
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return PROCEED;
  }

  content::WebContents* source = handle->GetWebContents();
  Browser* browser = static_cast<Browser*>(
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(source));
  if (!browser || !BrowserController::ControllerForBrowser(browser)) {
    return PROCEED;  // Not an Atrium-managed window - DevTools, the peek's own
                     // contents, and so on.
  }

  std::optional<std::string> space_id =
      RouteForURL(url, SourceSpaceIdFor(source, browser));
  if (!space_id.has_value()) {
    return PROCEED;
  }

  if (AlreadyInTargetSpace(source, browser, *space_id)) {
    return PROCEED;  // Routing it here is what put it here.
  }

  // Never open synchronously from inside a throttle callback: it re-enters the
  // navigation and WebContents machinery. Post it, then cancel this navigation
  // silently so no error page appears where the tab used to be going.
  atrium::PreconnectBeforeRestart(source, url, "routing");
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&OpenRoutedLater, source->GetWeakPtr(),
                                *space_id, url));
  return CANCEL_AND_IGNORE;
}

const char* AtriumRoutingNavigationThrottle::GetNameForLogging() {
  return "AtriumRoutingNavigationThrottle";
}

}  // namespace atrium
