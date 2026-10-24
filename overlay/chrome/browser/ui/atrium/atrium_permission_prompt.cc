// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// chrome/browser/ui/atrium/atrium_permission_prompt.cc
#include "chrome/browser/ui/atrium/atrium_permission_prompt.h"

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_permission_sheet_widget.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_window/public/browser_window_interface.h"
#include "chrome/browser/ui/webui/webui_embedding_context.h"
#include "components/permissions/permission_request.h"
#include "components/tabs/public/tab_interface.h"
#include "content/public/browser/web_contents.h"

namespace atrium {

namespace {

BrowserWindowInterface* GetBrowserWindowInterface(
    content::WebContents* web_contents) {
  tabs::TabInterface* tab =
      web_contents ? tabs::TabInterface::MaybeGetFromContents(web_contents)
                   : nullptr;
  BrowserWindowInterface* browser = tab ? tab->GetBrowserWindowInterface()
                                       : nullptr;
  return browser ? browser : webui::GetBrowserWindowInterface(web_contents);
}

}  // namespace

AtriumPermissionPrompt::AtriumPermissionPrompt(
    content::WebContents* web_contents, Delegate* delegate)
    : web_contents_(web_contents), delegate_(delegate) {
  BrowserWindowInterface* browser = GetBrowserWindowInterface(web_contents_);
  Browser* underlying_browser =
      browser ? browser->GetBrowserForMigrationOnly() : nullptr;
  // static_cast, not BrowserView::GetBrowserViewForBrowser: the caller
  // (permission_prompt_factory.cc) only reaches this constructor once
  // IsAtriumBrowserWindow(browser) is true, so window() is always an
  // AtriumBrowserWindow here - the same accessor
  // atrium_browser_window_browsertest.cc uses on every test in that fixture.
  atrium::AtriumBrowserWindow* window =
      underlying_browser ? static_cast<atrium::AtriumBrowserWindow*>(
                               underlying_browser->window())
                          : nullptr;
  if (!window || !window->widget()) {
    // No window to anchor a sheet to (e.g. torn-down tab); the request stays
    // pending until PermissionRequestManager tries again with a live window.
    return;
  }
  sheet_ = std::make_unique<PermissionSheetWidget>(
      window->widget(), window, BuildRequestJSON(),
      base::BindOnce(&AtriumPermissionPrompt::OnResponse,
                     base::Unretained(this)));
}

AtriumPermissionPrompt::~AtriumPermissionPrompt() = default;

std::string AtriumPermissionPrompt::BuildRequestJSON() const {
  base::DictValue payload;
  payload.Set("origin", delegate_->GetRequestingOrigin().spec());
  base::ListValue permission_names;
  for (const auto& request : delegate_->Requests()) {
    permission_names.Append(
        base::UTF16ToUTF8(request->GetMessageTextFragment()));
  }
  payload.Set("permissions", std::move(permission_names));
  std::string json;
  base::JSONWriter::Write(payload, &json);
  return json;
}

void AtriumPermissionPrompt::OnResponse(bool allow) {
  // PromptOptions defaults to its FIRST variant alternative
  // (GeolocationPromptOptions), not monostate - PermissionRequestManager
  // CHECK-fails on a non-geolocation request carrying non-monostate options.
  PromptOptions options = std::monostate{};
  if (allow) {
    delegate_->Accept(options);
  } else {
    delegate_->Deny(options);
  }
}

bool AtriumPermissionPrompt::UpdateAnchor() {
  if (sheet_) {
    sheet_->ApplyParentBounds();
  }
  return true;
}

permissions::PermissionPrompt::TabSwitchingBehavior
AtriumPermissionPrompt::GetTabSwitchingBehavior() {
  return permissions::PermissionPrompt::TabSwitchingBehavior::
      kDestroyPromptButKeepRequestPending;
}

permissions::PermissionPromptDisposition
AtriumPermissionPrompt::GetPromptDisposition() const {
  return permissions::PermissionPromptDisposition::CUSTOM_MODAL_DIALOG;
}

bool AtriumPermissionPrompt::IsAskPrompt() const {
  return true;
}

std::optional<gfx::Rect> AtriumPermissionPrompt::GetViewBoundsInScreen()
    const {
  return std::nullopt;
}

bool AtriumPermissionPrompt::ShouldFinalizeRequestAfterDecided() const {
  return true;
}

std::vector<permissions::ElementAnchoredBubbleVariant>
AtriumPermissionPrompt::GetPromptVariants() const {
  return {};
}

std::optional<permissions::feature_params::PermissionElementPromptPosition>
AtriumPermissionPrompt::GetPromptPosition() const {
  return std::nullopt;
}

}  // namespace atrium
