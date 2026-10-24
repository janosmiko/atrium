// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// chrome/browser/ui/atrium/atrium_permission_prompt.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_PERMISSION_PROMPT_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_PERMISSION_PROMPT_H_

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "components/permissions/permission_prompt.h"
#include "ui/gfx/geometry/rect.h"

namespace content {
class WebContents;
}

namespace atrium {

class PermissionSheetWidget;

// routes a permission request to the /permsheet glass-sheet
// surface instead of Chromium's chip/bubble, which have no LocationBarView
// to anchor against in an Atrium (chromeless) window.
class AtriumPermissionPrompt : public permissions::PermissionPrompt {
 public:
  AtriumPermissionPrompt(content::WebContents* web_contents, Delegate* delegate);
  ~AtriumPermissionPrompt() override;

  AtriumPermissionPrompt(const AtriumPermissionPrompt&) = delete;
  AtriumPermissionPrompt& operator=(const AtriumPermissionPrompt&) = delete;

  // permissions::PermissionPrompt:
  bool UpdateAnchor() override;
  TabSwitchingBehavior GetTabSwitchingBehavior() override;
  permissions::PermissionPromptDisposition GetPromptDisposition()
      const override;
  bool IsAskPrompt() const override;
  std::optional<gfx::Rect> GetViewBoundsInScreen() const override;
  bool ShouldFinalizeRequestAfterDecided() const override;
  std::vector<permissions::ElementAnchoredBubbleVariant> GetPromptVariants()
      const override;
  std::optional<permissions::feature_params::PermissionElementPromptPosition>
  GetPromptPosition() const override;

  // Test-only seam, same pattern as PermissionPromptBubble::
  // GetPromptBubbleWidgetForTesting.
  PermissionSheetWidget* GetSheetForTesting() const { return sheet_.get(); }

 private:
  // Builds the {origin, permissions[]} JSON the sheet renders, from
  // delegate_->Requests().
  std::string BuildRequestJSON() const;

  // The sheet's Allow/Block callback - resolves the request through
  // delegate_ and lets PermissionRequestManager destroy this prompt.
  void OnResponse(bool allow);

  const raw_ptr<content::WebContents> web_contents_;
  const raw_ptr<Delegate> delegate_;
  std::unique_ptr<PermissionSheetWidget> sheet_;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_PERMISSION_PROMPT_H_
