// Copyright 2026 The Atrium Authors

#ifndef CHROME_BROWSER_UI_WEBUI_ATRIUM_ATRIUM_UI_H_
#define CHROME_BROWSER_UI_WEBUI_ATRIUM_ATRIUM_UI_H_

#include "chrome/browser/ui/webui/top_chrome/top_chrome_webui_config.h"
#include "content/public/browser/web_ui_controller.h"

namespace content {
class BrowserContext;
class Page;
class WebUI;
}  // namespace content

class GURL;

// Atrium's chrome layer. One origin,
// chrome://atrium.top-chrome, serving every chrome surface as a path route.
//
// Derives from content::WebUIController, NOT TopChromeWebUIController. The
// latter would work - passing enable_chrome_send=true does grant chrome.send -
// but it derives from ui::MojoWebUIController, which sets kMojoWebUi in the
// bindings set UNCONDITIONALLY (ui/webui/mojo_web_ui_controller.cc:14-16).
// The transport decision is chrome.send, so Mojo bindings would be unused
// privilege on a page that already holds browser-level reach. What the base
// class actually contributes is the zoom pin and the display disposition,
// replicated below in twenty lines.
//
// The .top-chrome host suffix is load-bearing beyond naming: it is what makes
// ChromeRenderWidgetHostViewMacDelegate return kWhenInActiveApp, so clicks
// land on an inactive window without a first click to focus
// (chrome/browser/renderer_host/chrome_render_widget_host_view_mac_delegate.mm
// :445-450). No Atrium code is needed for that.
class AtriumUI : public content::WebUIController {
 public:
  // Exactly ONE constructor form is permitted: DefaultTopChromeWebUIConfig<T>
  // static_asserts that T is constructible from WebUI* or (WebUI*, GURL) but
  // not both (top_chrome_webui_config.h:102-105). The URL form is required
  // here because the route is carried in the path.
  AtriumUI(content::WebUI* web_ui, const GURL& url);
  AtriumUI(const AtriumUI&) = delete;
  AtriumUI& operator=(const AtriumUI&) = delete;
  ~AtriumUI() override;

  static constexpr std::string_view GetWebUIName() { return "Atrium"; }

  // content::WebUIController:
  void WebUIPrimaryPageChanged(content::Page& page) override;
  DisplayDisposition GetDisplayDisposition() const override;

  WEB_UI_CONTROLLER_TYPE_DECL();
};

class AtriumUIConfig : public DefaultTopChromeWebUIConfig<AtriumUI> {
 public:
  AtriumUIConfig();

  // Restricts the UI to Atrium's own reserved BrowserContext. The base returns
  // true unconditionally (top_chrome_webui_config.h:89-91), which would let a
  // user profile navigate a tab to this URL and obtain a second privileged
  // WebContents in a second context - defeating the one-renderer-process
  // property and handing an ordinary tab chrome.send reach.
  //
  // Must be null-safe and must tolerate arbitrary contexts:
  // GetWebUIConfigList calls it with whatever it is given, long before the
  // reserved context exists (content/public/browser/webui_config_map.cc:139).
  bool IsWebUIEnabled(content::BrowserContext* browser_context) override;
};

#endif  // CHROME_BROWSER_UI_WEBUI_ATRIUM_ATRIUM_UI_H_
