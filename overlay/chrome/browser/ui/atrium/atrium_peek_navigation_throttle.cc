// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_peek_navigation_throttle.cc
#include "chrome/browser/ui/atrium/atrium_peek_navigation_throttle.h"

#include <memory>

#include "base/functional/bind.h"
#include "base/location.h"
#include "base/memory/weak_ptr.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/ui/atrium/atrium_peek_intercept.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/global_browser_collection.h"
#include "content/public/browser/navigation_handle.h"
#include "content/public/browser/navigation_throttle_registry.h"
#include "content/public/browser/web_contents.h"
#include "content/public/common/referrer.h"
#include "ui/base/page_transition_types.h"
#include "url/gurl.h"

namespace atrium {

namespace {

// Re-resolves the Browser from a weak handle to the source contents, so a tab
// torn down across the task boundary is a no-op rather than a use-after-free.
// Nothing raw survives the hop on purpose.
void OpenAutoPeekLater(base::WeakPtr<content::WebContents> weak_source,
                       GURL url,
                       content::Referrer referrer) {
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
  OpenAutoPeek(browser, source, url, referrer);
}

}  // namespace

// static
void AtriumPeekNavigationThrottle::MaybeCreateAndAdd(
    content::NavigationThrottleRegistry& registry) {
  registry.AddThrottle(std::make_unique<AtriumPeekNavigationThrottle>(registry));
}

AtriumPeekNavigationThrottle::AtriumPeekNavigationThrottle(
    content::NavigationThrottleRegistry& registry)
    : content::NavigationThrottle(registry) {}

AtriumPeekNavigationThrottle::~AtriumPeekNavigationThrottle() = default;

content::NavigationThrottle::ThrottleCheckResult
AtriumPeekNavigationThrottle::WillStartRequest() {
  content::NavigationHandle* handle = navigation_handle();

  // Cheap content-side gates first. Almost every navigation fails one of these
  // and never reaches the lookups below. A miss always PROCEEDs - this
  // throttle cancels only when it is about to open a peek instead.
  if (!handle->IsInPrimaryMainFrame() || !handle->IsRendererInitiated() ||
      handle->IsSameDocument() || !handle->HasUserGesture() ||
      !ui::PageTransitionCoreTypeIs(handle->GetPageTransition(),
                                    ui::PAGE_TRANSITION_LINK)) {
    return PROCEED;
  }

  const GURL& url = handle->GetURL();
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return PROCEED;
  }

  content::WebContents* source = handle->GetWebContents();
  Browser* browser = static_cast<Browser*>(
      GlobalBrowserCollection::GetInstance()->FindBrowserWithTab(source));
  if (!browser) {
    return PROCEED;  // Not owned by any Browser - the peek's own contents,
                     // DevTools, and so on.
  }

  if (!ShouldAutoPeek(browser, source, url)) {
    return PROCEED;
  }

  // Never open synchronously from inside a throttle callback: opening a peek
  // re-enters the navigation and WebContents machinery. Post it, then cancel
  // this navigation silently so the pinned tab stays where it is, with no
  // error page.
  PreconnectBeforeRestart(source, url, "peek intercept");
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&OpenAutoPeekLater, source->GetWeakPtr(), url,
                                content::Referrer(handle->GetReferrer())));
  return CANCEL_AND_IGNORE;
}

const char* AtriumPeekNavigationThrottle::GetNameForLogging() {
  return "AtriumPeekNavigationThrottle";
}

}  // namespace atrium
