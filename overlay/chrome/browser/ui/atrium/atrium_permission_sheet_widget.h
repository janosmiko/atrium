// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// chrome/browser/ui/atrium/atrium_permission_sheet_widget.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_PERMISSION_SHEET_WIDGET_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_PERMISSION_SHEET_WIDGET_H_

#include <memory>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/ui/atrium/atrium_web_surface_widget.h"

namespace content {
class WebContents;
}

namespace views {
class Widget;
}  // namespace views

namespace atrium {

class AtriumBrowserWindow;

// The glass sheet over the web content, parented to a plain
// `views::Widget*` since its caller (permission_prompt_factory.cc) is
// generic Chromium code.
class PermissionSheetWidget : public AtriumWebSurfaceWidget {
 public:
  // Fires exactly once, when the sheet's JS reports Allow or Block.
  using ResponseCallback = base::OnceCallback<void(bool allow)>;

  // `parent` and `window` are both valid here, but `window` can be torn down
  // first - see DismissForWindowTeardown.
  PermissionSheetWidget(views::Widget* parent, AtriumBrowserWindow* window,
                       std::string request_json,
                       ResponseCallback on_response);
  ~PermissionSheetWidget() override;

  PermissionSheetWidget(const PermissionSheetWidget&) = delete;
  PermissionSheetWidget& operator=(const PermissionSheetWidget&) = delete;

  // Resizes to the parent's current window bounds and shows it.
  void ApplyParentBounds();

  // Called by the window's destructor while this sheet is still live -
  // AtriumPermissionPrompt owns it, not the window, so it can outlive one.
  void DismissForWindowTeardown();

  // SurfaceHost: the three permissionSheet* messages the /permsheet route
  // sends.
  // backdates shown_at_ so an Allow counts as deliberate.
  //
  // A seam rather than a mock clock: overriding TimeTicks globally in a
  // browsertest trips content's responsiveness watcher DCHECK
  // (queue_time <= execution_start_time), which is a harness failure and says
  // nothing about this guard.
  void ExpireAllowGuardForTesting();

  void OnSurfaceMessage(std::string_view name,
                        const base::ListValue& args) override;

 private:
  // AtriumWebSurfaceWidget:
  bool RetriesOnMissingChromeContext() const override;

  // Pushes request_json_ to the page. Retried from permissionSheetReady if
  // this runs before the renderer announces itself.
  void PushRequest();

  const raw_ptr<views::Widget> parent_;
  // Cleared by DismissForWindowTeardown() - not const, since that is exactly
  // what keeps it from dangling once the window it points at is gone.
  raw_ptr<AtriumBrowserWindow> window_;
  const std::string request_json_;
  ResponseCallback on_response_;

  // Guards on_response_ against firing twice.
  bool responded_ = false;

  // when the origin was pushed to the page (permissionSheetReady),
  // not when the widget was built: a slow load must not use up the window
  // before the user can read the origin. An Allow arriving inside
  // kAllowGracePeriod of that is refused - the anti-clickjacking guard
  // Chromium's own bubble gets from InputEventActivationProtector and this
  // sheet had nothing equivalent to. Browser-side on purpose: the page drives
  // the renderer, so a renderer-side delay is one the attacker controls.
  base::TimeTicks shown_at_;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_PERMISSION_SHEET_WIDGET_H_
