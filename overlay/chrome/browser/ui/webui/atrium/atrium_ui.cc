// Copyright 2026 The Atrium Authors

#include "chrome/browser/ui/webui/atrium/atrium_ui.h"

#include "base/containers/span.h"
#include "chrome/browser/ui/atrium/atrium_chrome_context.h"
#include "base/functional/bind.h"
#include "chrome/browser/ui/atrium/atrium_favicon_source.h"
#include "chrome/browser/ui/atrium/atrium_icon_source.h"
#include "chrome/browser/ui/atrium/atrium_surface_host.h"
#include "chrome/common/webui_url_constants.h"
#include "chrome/grit/atrium_resources.h"
#include "chrome/grit/atrium_resources_map.h"
#include "components/zoom/zoom_controller.h"
#include "content/public/browser/host_zoom_map.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/url_data_source.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/bindings_policy.h"
#include "content/public/common/url_constants.h"
#include "url/gurl.h"

AtriumUI::AtriumUI(content::WebUI* web_ui, const GURL& url)
    : content::WebUIController(web_ui) {
  // chrome.send only. Deliberately no kMojoWebUi - see the class comment.
  web_ui->SetBindings(
      content::BindingsPolicySet({content::BindingsPolicyValue::kWebUi}));

  content::WebUIDataSource* source = content::WebUIDataSource::CreateAndAdd(
      web_ui->GetWebContents()->GetBrowserContext(),
      chrome::kChromeUIAtriumHost);
  source->AddResourcePaths(base::span(kAtriumResources));
  source->SetDefaultResource(IDR_ATRIUM_ATRIUM_HTML);

  // Icons served from THIS host rather than a host of their own.
  //
  // They used to live at chrome://atrium-favicon/<id>.png and
  // chrome://atrium-exticon/<id>.png, registered here with
  // content::URLDataSource::Add. Every favicon 404'd, and instrumenting both
  // ends settled why: FaviconSource::StartDataRequest never ran at all. The
  // renderer's load failed with net::ERR_FAILED before reaching the data
  // source - not a CSP violation, and not a cache miss, which would have
  // logged and returned a real 404. A control in the same document proved the
  // renderer can load cross-host chrome:// images: chrome://resources/... is
  // fine, because it is allowed for every context by construction.
  //
  // Serving from this host sidesteps the question entirely: the document is
  // already here, so nothing has to be made reachable that was not. The
  // request filter runs before the resource map, so these paths never fall
  // through to SetDefaultResource, and WebUIDataSource infers image/png from
  // the .png suffix.
  //
  // Still no renderer network request - the bytes come from the browser
  // process, which is the property that mattered all along.
  source->SetRequestFilter(
      base::BindRepeating([](const std::string& path) {
        return atrium::IconRequestKind(path) != atrium::IconKind::kNone;
      }),
      base::BindRepeating(
          [](const std::string& path,
             content::WebUIDataSource::GotDataCallback callback) {
            // The app icons are FILES, so they answer late. The other three
            // kinds are bytes the browser already holds and answer here.
            const atrium::IconRequest request = atrium::ParseIconPath(path);
            if (request.kind == atrium::IconKind::kAppIcon) {
              atrium::ServeAppIcon(request.id, std::move(callback));
              return;
            }
            std::move(callback).Run(atrium::ServeIcon(path));
          }));

  // Every chrome.send from any route funnels through one callback and is
  // handed to whichever native host owns this WebContents. The controller
  // knows nothing about individual surfaces beyond their message vocabulary,
  // which lives with the seam (kSurfaceMessages) rather than here - adding a
  // route means editing that one list, not this file.
  for (const char* message : atrium::kSurfaceMessages) {
    web_ui->RegisterMessageCallback(
        message,
        base::BindRepeating(
            [](content::WebUI* ui, const std::string& name,
               const base::ListValue& args) {
              if (atrium::SurfaceHost* host =
                      atrium::SurfaceHostFor(ui->GetWebContents())) {
                host->OnSurfaceMessage(name, args);
              }
            },
            web_ui, std::string(message)));
  }

  // Nothing else. In particular NOT webui::SetupWebUIDataSource, which calls
  // OverrideContentSecurityPolicy(ScriptSrc, ...) and installs a NON-EMPTY
  // trusted-types allowlist (ui/webui/webui_util.cc:50-63, :98-105). Both
  // would undo the choice to rewrite injection sinks against
  // DOM APIs and register no policy at all.
  //
  // The raw defaults from content/public/browser/url_data_source.cc:64-115 are
  // what we want: script-src chrome://resources 'self', object-src 'none',
  // require-trusted-types-for 'script', and an EMPTY trusted-types allowlist
  // so createPolicy() throws for every name. style-src and img-src are not
  // emitted at all for a trusted chrome:// source, which is what lets the
  // favicon subresource host work without widening anything.
}

AtriumUI::~AtriumUI() = default;

void AtriumUI::WebUIPrimaryPageChanged(content::Page& page) {
  content::WebUIController::WebUIPrimaryPageChanged(page);

  // Pin to 100% and disable zoom, replicating TopChromeWebUIController
  // (chrome/browser/ui/webui/top_chrome/top_chrome_web_ui_controller.cc:19-33).
  // Chrome-layer UI must not inherit the user's page zoom.
  content::WebContents* web_contents = web_ui()->GetWebContents();
  auto* zoom_controller = zoom::ZoomController::FromWebContents(web_contents);
  if (!zoom_controller) {
    zoom_controller = zoom::ZoomController::CreateForWebContents(web_contents);
  }
  zoom_controller->SetZoomMode(zoom::ZoomController::ZOOM_MODE_DISABLED);
  content::HostZoomMap* zoom_map =
      content::HostZoomMap::Get(web_contents->GetSiteInstance());
  zoom_map->SetTemporaryZoomLevel(
      web_contents->GetPrimaryMainFrame()->GetGlobalId(), 0.0);
}

content::WebUIController::DisplayDisposition AtriumUI::GetDisplayDisposition()
    const {
  return content::WebUIController::DisplayDisposition::kUIElement;
}

WEB_UI_CONTROLLER_TYPE_IMPL(AtriumUI)

AtriumUIConfig::AtriumUIConfig()
    : DefaultTopChromeWebUIConfig(content::kChromeUIScheme,
                                  chrome::kChromeUIAtriumHost) {}

bool AtriumUIConfig::IsWebUIEnabled(content::BrowserContext* browser_context) {
  // Enabled ONLY in Atrium's reserved chrome context. A user profile
  // navigating a tab to this URL would otherwise obtain a second privileged
  // WebContents in a second BrowserContext, defeating the one-renderer-process
  // property and handing an ordinary tab chrome.send reach.
  //
  // GetIfExists() is null until the async profile creation started in
  // PreBrowserStart completes, and null != browser_context, so the UI is
  // simply disabled until the context is ready. That is the correct
  // fail-closed direction, and it is why this must not CHECK.
  auto* chrome_context = atrium::AtriumChromeContext::GetIfExists();
  return chrome_context && browser_context == chrome_context->context();
}
