// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_hint_overlay_widget.mm
#include "chrome/browser/ui/atrium/atrium_hint_overlay_widget.h"

#include <utility>

#include "base/functional/bind.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_chrome_context.h"
#include "content/public/browser/web_contents.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"

namespace atrium {

HintOverlayWidget::HintOverlayWidget(AtriumBrowserWindow* parent)
    : AtriumWebSurfaceWidget(parent->widget(),
                             "hint",
                             "AtriumHintOverlay",
                             /*activatable=*/false),
      parent_(parent) {
  // Display-only, and it covers the whole window: while a hint was up this
  // surface hit-tested the lot and ate every click meant for the page
  //. A toast cannot do this - it has a close button.
  if (NSWindow* ns_window = widget_->GetNativeWindow().GetNativeNSWindow()) {
    ns_window.ignoresMouseEvents = YES;
  }
  widget_->AddObserver(this);
  // Removed in the destructor, which AtriumBrowserWindow's destructor runs
  // before resetting its own widget_, so this can never dangle across the
  // parent's teardown.
  parent_->widget()->AddObserver(this);

  AtriumChromeContext::PostWhenReady(
      base::BindOnce(&HintOverlayWidget::OnChromeContextReady,
                     weak_factory_.GetWeakPtr()));
}

HintOverlayWidget::~HintOverlayWidget() {
  parent_->widget()->RemoveObserver(this);
  widget_->RemoveObserver(this);
}

void HintOverlayWidget::ApplyParentBounds() {
  widget_->SetBounds(parent_->widget()->GetWindowBoundsInScreen());
}

void HintOverlayWidget::Show(const std::string& text,
                             const std::string& keys,
                             const gfx::Rect& anchor_in_window,
                             int ttl_ms) {
  BuildContents();
  if (!web_contents_) {
    return;
  }
  parent_->PushOverlayTheme(web_contents_.get());
  // Full window, not just the anchor's neighborhood: hint.js's own place()
  // clamps against window.innerWidth/innerHeight, and this is what lets that
  // clamp read the whole browser window instead of the sidebar's narrower one.
  ApplyParentBounds();

  pending_show_ = true;
  pending_text_ = text;
  pending_keys_ = keys;
  pending_anchor_ = anchor_in_window;
  pending_ttl_ms_ = ttl_ms;
  PushPending();

  widget_->ShowInactive();
  parent_->OrderOverlayChild(widget_.get(),
                             AtriumBrowserWindow::OverlayLayer::kHintOverlay);
}

void HintOverlayWidget::PushPending() {
  if (!pending_show_) {
    return;
  }
  if (!renderer_ready_) {
    return;
  }
  const std::u16string call =
      u"window.atriumShowHint && window.atriumShowHint({\"text\":" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(pending_text_)) +
      u",\"keys\":" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(pending_keys_)) +
      u",\"anchor\":{\"left\":" +
      base::NumberToString16(pending_anchor_.x()) + u",\"top\":" +
      base::NumberToString16(pending_anchor_.y()) + u",\"width\":" +
      base::NumberToString16(pending_anchor_.width()) + u",\"height\":" +
      base::NumberToString16(pending_anchor_.height()) + u"},\"ttlMs\":" +
      base::NumberToString16(pending_ttl_ms_) + u"})";
  CallPage(call);
  pending_show_ = false;
}

void HintOverlayWidget::Hide() {
  pending_show_ = false;
  CallPage(u"window.atriumHideHint && window.atriumHideHint()");
  if (widget_->IsVisible()) {
    widget_->Hide();
  }
}

bool HintOverlayWidget::IsShowingForTesting() const {
  return widget_->IsVisible();
}

void HintOverlayWidget::OnWidgetBoundsChanged(views::Widget* widget,
                                              const gfx::Rect& new_bounds) {
  if (widget != parent_->widget()) {
    return;
  }
  // A resize while hidden counts: macOS refits the window to the screen during
  // the show handshake, and dropping it left the bubble at the pre-refit rect.
  ApplyParentBounds();
}

void HintOverlayWidget::OnSurfaceMessage(std::string_view name,
                                         const base::ListValue& args) {
  if (name == "hintReady") {
    renderer_ready_ = true;
    // Covers the first show: Show()'s own push landed before this document
    // committed, so the bubble came up on overlay.css's defaults while every
    // other surface wore the space theme.
    parent_->PushOverlayTheme(web_contents_.get());
    PushPending();
  }
}

}  // namespace atrium
