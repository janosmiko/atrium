// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_web_surface_widget.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_WEB_SURFACE_WIDGET_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_WEB_SURFACE_WIDGET_H_

#include <memory>
#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/atrium/atrium_surface_host.h"

namespace content {
class RenderWidgetHostView;
class WebContents;
}  // namespace content

namespace views {
class View;
class WebView;
class Widget;
class WidgetDelegate;
}  // namespace views

namespace atrium {

// The chrome-context WebContents/WebView plumbing the frameless overlay
// widgets all repeated. The bare constructor is for AtriumSettingsWindow,
// whose top-level window does not fit the other constructor's shape.
class AtriumWebSurfaceWidget : public SurfaceHost {
 public:
  // Null until BuildContents() (or the subclass's own equivalent) creates it.
  content::WebContents* web_contents() const { return web_contents_.get(); }

 protected:
  AtriumWebSurfaceWidget(views::Widget* parent,
                         std::string_view route,
                         std::string_view name,
                         bool activatable,
                         bool opaque = false);

  // Leaves every member below default-constructed. AtriumSettingsWindow
  // builds its own top-level widget in its own Build() instead.
  AtriumWebSurfaceWidget();

  ~AtriumWebSurfaceWidget() override;

  // Creates web_contents_/web_view_ on `route_` against the reserved chrome
  // context. A no-op if web_contents_ already exists, or if the context is
  // not up yet and RetriesOnMissingChromeContext() declines a retry.
  void BuildContents();

  // AtriumChromeContext::PostWhenReady's retry callback. Override where the
  // widget also has to react once the retry lands.
  virtual void OnChromeContextReady();

  // Calls `script` in the page, or drops it when the renderer has not
  // announced itself yet.
  void CallPage(const std::u16string& script);

  // False for a widget that leaves a missing chrome context for its own next
  // explicit Show()/BuildContents() call, instead of scheduling a retry
  // through AtriumChromeContext::PostWhenReady.
  virtual bool RetriesOnMissingChromeContext() const;

  // Runs right before AttachToContents(), for a widget that has to be its own
  // WebContentsDelegate before the first navigation commits.
  virtual void BeforeAttachToContents() {}

  // Runs once the RenderWidgetHostView exists, before the transparent
  // background below is applied.
  virtual void OnRenderWidgetHostViewCreated(
      content::RenderWidgetHostView* rwhv) {}

  // Runs after the WebContents and its WebView are fully wired up.
  virtual void OnWebContentsCreated() {}

  const std::string route_;
  const bool opaque_ = false;
  std::unique_ptr<views::WidgetDelegate> widget_delegate_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<views::View> contents_container_ = nullptr;
  std::unique_ptr<content::WebContents> web_contents_;
  raw_ptr<views::WebView> web_view_ = nullptr;
  bool renderer_ready_ = false;
  bool awaiting_chrome_context_ = false;

  base::WeakPtrFactory<AtriumWebSurfaceWidget> weak_factory_{this};
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_WEB_SURFACE_WIDGET_H_
