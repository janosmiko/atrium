// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_peek_intercept.cc
#include "chrome/browser/ui/atrium/atrium_peek_intercept.h"

#include <string>

#include "base/logging.h"
#include "chrome/browser/predictors/loading_predictor.h"
#include "chrome/browser/predictors/loading_predictor_factory.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_log_safe_url.h"
#include "chrome/browser/ui/atrium/atrium_peek_decision.h"
#include "chrome/browser/ui/atrium/atrium_sidebar_tabs.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "components/download/public/common/download_url_parameters.h"
#include "net/base/network_anonymization_key.h"
#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "net/base/schemeful_site.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "services/network/public/cpp/constants.h"
#include "third_party/atrium/core/include/atrium_core.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/download_manager.h"
#include "content/public/browser/navigation_controller.h"
#include "content/public/browser/page_navigator.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "ui/base/page_transition_types.h"
#include "ui/display/screen.h"
#include "ui/gfx/geometry/point.h"

namespace atrium {

bool MaybeOpenPeek(Browser* browser,
                   content::WebContents* source,
                   const GURL& url,
                   const content::Referrer& referrer) {
  BrowserController* controller =
      BrowserController::ControllerForBrowser(browser);
  if (!controller) {
    return false;
  }
  // The cursor is still on the clicked
  // link at this point in the call stack (Browser::OpenURLFromTab runs
  // synchronously off the click that triggered it) - captured here, the
  // earliest atrium-owned code on this path, so the open animation can grow
  // from where the user actually clicked instead of the card's center.
  // display::Screen::GetCursorScreenPoint() returns the SAME top-left,
  // y-down screen-coordinate convention views::Widget bounds already use
  // (see PeekWidget::ApplyParentBounds), so no platform-specific coordinate
  // flip is needed here or anywhere else this point travels.
  const gfx::Point click_origin_screen =
      display::Screen::Get()->GetCursorScreenPoint();
  return controller->OpenPeek(url, referrer, click_origin_screen, source);
}

void DownloadOptionClickedLink(content::WebContents* source,
                               const content::OpenURLParams& params) {
  content::RenderFrameHost* frame = content::RenderFrameHost::FromID(
      params.source_render_process_id, params.source_render_frame_id);
  if (!frame) {
    frame = source->GetPrimaryMainFrame();
  }
  net::NetworkTrafficAnnotationTag traffic_annotation =
      net::DefineNetworkTrafficAnnotation("atrium_option_click_download", R"(
        semantics {
          sender: "Atrium Option-click download"
          description: "Saves an Option-clicked link's target to disk."
          trigger:
            "The user Option-clicked a link in a window with no Atrium peek "
            "surface (a popup, DevTools, or app window)."
          data: "None."
          destination: WEBSITE
        }
        policy {
          cookies_allowed: YES
          cookies_store: "user"
          setting:
            "This feature cannot be disabled by settings. It only runs when "
            "a user Option-clicks a link."
          policy_exception_justification: "Not implemented."
        })");
  std::unique_ptr<download::DownloadUrlParameters> dl_params =
      frame->CreateDownloadUrlParameters(params.url, traffic_annotation);
  dl_params->set_referrer(params.referrer.url);
  dl_params->set_referrer_policy(
      content::Referrer::ReferrerPolicyForUrlRequest(params.referrer.policy));
  dl_params->set_download_source(download::DownloadSource::FROM_RENDERER);
  source->GetBrowserContext()->GetDownloadManager()->DownloadUrl(
      std::move(dl_params));
}

void PreconnectBeforeRestart(content::WebContents* source,
                             const GURL& url,
                             const char* what) {
  VLOG(1) << "Atrium: " << what << " cancelled the navigation to "
          << LogSafeUrl(url) << " to reopen it";
  if (!source) {
    return;
  }
  predictors::LoadingPredictor* predictor =
      predictors::LoadingPredictorFactory::GetForProfile(
          Profile::FromBrowserContext(source->GetBrowserContext()));
  if (!predictor) {
    return;
  }
  predictor->PreconnectURLIfAllowed(
      url, /*allow_credentials=*/true,
      net::NetworkAnonymizationKey::CreateSameSite(net::SchemefulSite(url)),
      network::GetNoOpNetworkRestrictionsId());
}

bool ShouldAutoPeek(Browser* browser,
                    content::WebContents* source,
                    const GURL& url) {
  if (!browser || !source) {
    return false;
  }
  const GURL& from = source->GetLastCommittedURL();

  PeekDecisionInputs inputs;
  inputs.source_has_committed_url = from.is_valid();
  inputs.is_http_or_https = url.SchemeIsHTTPOrHTTPS();
  inputs.is_same_domain_or_host =
      net::registry_controlled_domains::SameDomainOrHost(
          from, url,
          net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  inputs.has_atrium_controller =
      BrowserController::ControllerForBrowser(browser) != nullptr;

  // Asked last, and only when the cheap fields already agree - it takes the
  // core's lock and walks the active space. The profile id is derived the same
  // way BrowserController derives its own cached one.
  if (inputs.source_has_committed_url && inputs.is_http_or_https &&
      !inputs.is_same_domain_or_host && inputs.has_atrium_controller) {
    // The claim first: a favourite's tab has no row, so its URL stops matching
    // the stored one after the first move within its site.
    const std::string profile_id =
        browser->profile()->GetBaseName().AsUTF8Unsafe();
    inputs.is_peek_source_tab =
        !FavoriteKeyForTab(source).empty() ||
        atrium_core_is_peek_source_url(
            BrowserController::SpaceIndexForBrowser(browser),
            profile_id.c_str(), from.spec().c_str());
  }

  return ShouldAutoPeekForInputs(inputs);
}

void OpenAutoPeek(Browser* browser,
                  content::WebContents* source,
                  const GURL& url,
                  const content::Referrer& referrer) {
  BrowserController* controller =
      BrowserController::ControllerForBrowser(browser);
  if (!controller) {
    return;
  }
  // See MaybeOpenPeek's comment on why the cursor position is captured here:
  // the throttle posts this task off the click that triggered the navigation,
  // so the cursor is still on the link.
  const gfx::Point click_origin_screen =
      display::Screen::Get()->GetCursorScreenPoint();
  if (controller->OpenPeek(url, referrer, click_origin_screen, source) ||
      !source) {
    return;
  }
  // The throttle already cancelled the click, so a refused peek opens the link
  // where it was clicked. Browser-initiated, so the throttle lets it through.
  content::NavigationController::LoadURLParams params(url);
  params.referrer = referrer;
  params.transition_type = ui::PAGE_TRANSITION_LINK;
  source->GetController().LoadURLWithParams(params);
}

bool ShouldPeekPopup(Browser* browser,
                     content::WebContents* source,
                     const GURL& target_url,
                     bool targets_another_display) {
  if (!browser || !source) {
    return false;
  }

  PopupPeekDecisionInputs inputs;
  inputs.has_atrium_controller =
      BrowserController::ControllerForBrowser(browser) != nullptr;
  inputs.source_is_active_tab =
      browser->tab_strip_model()->GetActiveWebContents() == source;
  // An empty target is the blank popup a page fills in itself, which is how
  // every sign-in library opens one.
  inputs.target_is_peekable_scheme = target_url.is_empty() ||
                                     target_url.IsAboutBlank() ||
                                     target_url.SchemeIsHTTPOrHTTPS();
  inputs.targets_another_display = targets_another_display;

  // Every field, not just the verdict: a scenario that sees no card needs to
  // tell "the popup never got here" from "it got here and was refused". No
  // URLs - this runs on every page-opened popup.
  const bool peek = ShouldPeekPopupForInputs(inputs);
  VLOG(1) << "Atrium: popup peek " << peek
          << " controller " << inputs.has_atrium_controller
          << " activetab " << inputs.source_is_active_tab
          << " scheme " << inputs.target_is_peekable_scheme
          << " otherdisplay " << inputs.targets_another_display;
  return peek;
}

std::unique_ptr<content::WebContents> AdoptPopupAsPeek(
    Browser* browser,
    std::unique_ptr<content::WebContents> contents,
    const GURL& target_url) {
  BrowserController* controller =
      BrowserController::ControllerForBrowser(browser);
  if (!controller || !contents) {
    VLOG(1) << "Atrium: popup adopt refused, no controller for this browser";
    return contents;
  }
  // See MaybeOpenPeek on the cursor: window.open runs off the click that
  // triggered it, so the pointer is still on the button that opened this.
  return controller->AdoptPopupAsPeek(
      std::move(contents), display::Screen::Get()->GetCursorScreenPoint(),
      target_url);
}

}  // namespace atrium
