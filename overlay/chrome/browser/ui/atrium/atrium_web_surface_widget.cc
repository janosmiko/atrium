// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_web_surface_widget.cc
#include "chrome/browser/ui/atrium/atrium_web_surface_widget.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_chrome_context.h"
#include "chrome/common/webui_url_constants.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/layout/fill_layout.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"
#include "ui/views/widget/widget_delegate.h"
#include "url/gurl.h"

namespace atrium {

AtriumWebSurfaceWidget::AtriumWebSurfaceWidget(views::Widget* parent,
                                               std::string_view route,
                                               std::string_view name,
                                               bool activatable,
                                               bool opaque)
    : route_(route), opaque_(opaque) {
  widget_ = std::make_unique<views::Widget>();
  views::Widget::InitParams params(
      views::Widget::InitParams::CLIENT_OWNS_WIDGET,
      views::Widget::InitParams::TYPE_WINDOW_FRAMELESS);
  params.name = std::string(name);
  params.opacity = opaque_
                      ? views::Widget::InitParams::WindowOpacity::kOpaque
                      : views::Widget::InitParams::WindowOpacity::kTranslucent;
  params.activatable = activatable
                          ? views::Widget::InitParams::Activatable::kYes
                          : views::Widget::InitParams::Activatable::kNo;
  params.SetParent(parent);
  // Every card draws its own shadow in the page - a native NSWindow shadow
  // would trace the antialiased alpha edge into a jagged outline.
  params.shadow_type = views::Widget::InitParams::ShadowType::kNone;

  widget_delegate_ = std::make_unique<views::WidgetDelegate>();
  widget_delegate_->SetCanActivate(activatable);
  auto container = std::make_unique<views::View>();
  container->SetLayoutManager(std::make_unique<views::FillLayout>());
  contents_container_ = widget_delegate_->SetContentsView(std::move(container));
  params.delegate = widget_delegate_.get();

  widget_->Init(std::move(params));
}

AtriumWebSurfaceWidget::AtriumWebSurfaceWidget() = default;

AtriumWebSurfaceWidget::~AtriumWebSurfaceWidget() {
  if (web_contents_) {
    DetachFromContents(web_contents_.get());
  }
}

void AtriumWebSurfaceWidget::BuildContents() {
  if (web_contents_) {
    return;
  }
  AtriumChromeContext* context = AtriumChromeContext::GetIfExists();
  if (!context) {
    LOG(INFO) << "atrium: " << route_ << " has no chrome context yet";
    if (RetriesOnMissingChromeContext() && !awaiting_chrome_context_) {
      awaiting_chrome_context_ = true;
      AtriumChromeContext::PostWhenReady(base::BindOnce(
          &AtriumWebSurfaceWidget::OnChromeContextReady,
          weak_factory_.GetWeakPtr()));
    }
    return;
  }
  web_contents_ = context->CreateChromeContents(
      GURL(std::string(chrome::kChromeUIAtriumURL).append(route_)));
  if (!web_contents_) {
    return;
  }
  BeforeAttachToContents();
  AttachToContents(web_contents_.get());

  auto web_view = std::make_unique<views::WebView>(
      Profile::FromBrowserContext(context->context()));
  web_view->SetWebContents(web_contents_.get());
  web_view_ = contents_container_->AddChildView(std::move(web_view));

  if (content::RenderWidgetHostView* rwhv =
          web_contents_->GetRenderWidgetHostView()) {
    OnRenderWidgetHostViewCreated(rwhv);
    if (!opaque_) {
      rwhv->SetBackgroundColor(SK_ColorTRANSPARENT);
    }
  }
  if (!opaque_) {
    web_contents_->SetPageBaseBackgroundColor(SK_ColorTRANSPARENT);
  }
  OnWebContentsCreated();
}

void AtriumWebSurfaceWidget::OnChromeContextReady() {
  awaiting_chrome_context_ = false;
  BuildContents();
}

bool AtriumWebSurfaceWidget::RetriesOnMissingChromeContext() const {
  return true;
}

void AtriumWebSurfaceWidget::CallPage(const std::u16string& script) {
  if (!renderer_ready_ || !web_contents_) {
    return;
  }
  web_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(script,
                                                          base::NullCallback());
}

}  // namespace atrium
