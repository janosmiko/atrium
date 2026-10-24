// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// chrome/browser/ui/atrium/atrium_permission_sheet_widget.mm
#include "chrome/browser/ui/atrium/atrium_permission_sheet_widget.h"

#include "base/check.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "content/public/browser/web_contents.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"

namespace atrium {

namespace {

// how long after the sheet appears an Allow is treated as an accident
// rather than an answer. Chromium's own bubble uses
// InputEventActivationProtector for this; 500ms is the same order and is long
// enough to cover a key autorepeat that was already running when the sheet took
// focus.
constexpr base::TimeDelta kAllowGracePeriod = base::Milliseconds(500);

}  // namespace


PermissionSheetWidget::PermissionSheetWidget(views::Widget* parent,
                                             AtriumBrowserWindow* window,
                                             std::string request_json,
                                             ResponseCallback on_response)
    : AtriumWebSurfaceWidget(parent,
                             "permsheet",
                             "AtriumPermissionSheet",
                             /*activatable=*/true),
      parent_(parent),
      window_(window),
      request_json_(std::move(request_json)),
      on_response_(std::move(on_response)) {
  CHECK(window_);
  BuildContents();
  window_->SetLivePermissionSheet(this);
  window_->PushOverlayTheme(web_contents_.get());
  ApplyParentBounds();
  VLOG(1) << "Atrium: permission sheet showing";
  window_->ReleaseStartupHold();
  widget_->Show();
  widget_->Activate();
  // Same reasoning as CommandBarWidget::Show: a key window alone does not
  // hand the WebView keyboard focus, so Allow/Block via Enter/Escape would
  // silently do nothing without these two calls.
  if (web_view_) {
    web_view_->RequestFocus();
  }
  if (web_contents_) {
    web_contents_->Focus();
  }
}

void PermissionSheetWidget::ExpireAllowGuardForTesting() {
  shown_at_ = base::TimeTicks::Now() - kAllowGracePeriod;
}

PermissionSheetWidget::~PermissionSheetWidget() {
  if (window_) {
    window_->ClearLivePermissionSheet(this);
  }
}

void PermissionSheetWidget::ApplyParentBounds() {
  widget_->SetBounds(parent_->GetWindowBoundsInScreen());
}

void PermissionSheetWidget::DismissForWindowTeardown() {
  window_ = nullptr;
  if (widget_) {
    widget_->Hide();
  }
}

bool PermissionSheetWidget::RetriesOnMissingChromeContext() const {
  return false;
}

void PermissionSheetWidget::PushRequest() {
  // JSON.parse rather than interpolation: the origin is attacker-controlled
  // and this executes at browser privilege (CommandBarWidget::SetMatchesJSON
  // makes the same call for the same reason).
  CallPage(u"window.atriumSetPermissionSheet && window.atriumSetPermissionSheet("
           u"JSON.parse(" +
           base::UTF8ToUTF16(base::GetQuotedJSONString(request_json_)) +
           u"))");
}

void PermissionSheetWidget::OnSurfaceMessage(std::string_view name,
                                             const base::ListValue& args) {
  if (name == "permissionSheetReady") {
    renderer_ready_ = true;
    PushRequest();
    shown_at_ = base::TimeTicks::Now();
    // Covers a push landing before this document committed (BuildContents's
    // push, same reasoning as commandBarReady's replay).
    if (window_) {
      window_->PushOverlayTheme(web_contents_.get());
    }
    return;
  }
  if (responded_) {
    return;
  }
  if (name == "permissionSheetAllow") {
    // too soon to be a decision. The sheet takes keyboard focus the
    // moment it appears, so a key already down when it did - a page telling the
    // user to hold Enter, then calling getUserMedia - used to arrive here as a
    // grant. Refused rather than queued: a grant the user did not aim at this
    // sheet must not happen later either, and the request stays open for them to
    // answer deliberately.
    if (!renderer_ready_) {
      LOG(WARNING) << "Atrium: permission sheet refused an allow before the "
                      "page showed the origin";
      return;
    }
    const base::TimeDelta since_shown = base::TimeTicks::Now() - shown_at_;
    if (since_shown < kAllowGracePeriod) {
      LOG(WARNING) << "Atrium: permission sheet refused an allow after "
                   << since_shown.InMilliseconds() << "ms, under the "
                   << kAllowGracePeriod.InMilliseconds() << "ms guard";
      return;
    }
    responded_ = true;
    VLOG(1) << "Atrium: permission sheet allowed";
    std::move(on_response_).Run(/*allow=*/true);
    return;
  }
  if (name == "permissionSheetBlock") {
    responded_ = true;
    VLOG(1) << "Atrium: permission sheet blocked";
    std::move(on_response_).Run(/*allow=*/false);
    return;
  }
}

}  // namespace atrium
