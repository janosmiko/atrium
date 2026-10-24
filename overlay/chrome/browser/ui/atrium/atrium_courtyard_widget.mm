// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_courtyard_widget.mm
#include "chrome/browser/ui/atrium/atrium_courtyard_widget.h"

#include <limits>

#import <objc/runtime.h>

#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_map.h"
#import "chrome/browser/ui/atrium/atrium_surface_key_route_mac.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/web_contents.h"
#import "ui/base/cocoa/command_dispatcher.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"

// Same gap as CommandBarWidget and the peek card: left with no
// CommandDispatcher delegate, the Courtyard's own WebView claims Cmd+Q before
// it can reach the app main menu. See atrium_command_bar_widget.mm's doc.
@interface AtriumCourtyardCommandDispatcherDelegate
    : NSObject <CommandDispatcherDelegate> {
  // The browser window dies before this delegate's NSWindow deallocs on quit.
  base::WeakPtr<atrium::AtriumBrowserWindow> _parent;
}
- (instancetype)initWithParent:(atrium::AtriumBrowserWindow*)parent;
@end

@implementation AtriumCourtyardCommandDispatcherDelegate

- (instancetype)initWithParent:(atrium::AtriumBrowserWindow*)parent {
  if ((self = [super init])) {
    _parent = parent->GetWeakPtr();
  }
  return self;
}

- (ui::PerformKeyEquivalentResult)prePerformKeyEquivalent:(NSEvent*)event
                                                   window:(NSWindow*)window {
  const std::optional<atrium::NativeHotkeyAction> action =
      atrium::NativeHotkeyMap::Get()->MatchAction(event);
  switch (atrium::RouteForSurfaceKey(
      atrium::SurfaceKeyInputsForEvent(event, action.has_value()))) {
    case atrium::SurfaceKeyRoute::kMainMenu:
      // the quit chord leaves here, on the way IN. postPerform
      // below only runs once the view hierarchy declines the event, and the
      // Courtyard's WebView never does.
      VLOG(1) << "Atrium: courtyard passing key-equivalent to main menu "
                 "(keyCode " << event.keyCode << ")";
      return ui::PerformKeyEquivalentResult::kPassToMainMenu;
    case atrium::SurfaceKeyRoute::kWebContent:
      return ui::PerformKeyEquivalentResult::kUnhandled;
    case atrium::SurfaceKeyRoute::kDropRepeat:
      return ui::PerformKeyEquivalentResult::kDrop;
    case atrium::SurfaceKeyRoute::kAtriumAction:
      break;
  }
  if (!_parent) {
    return ui::PerformKeyEquivalentResult::kUnhandled;
  }
  atrium::BrowserController* controller =
      atrium::BrowserController::ControllerForBrowser(_parent->browser());
  if (controller && atrium::ExecuteNativeHotkeyAction(controller, *action)) {
    return ui::PerformKeyEquivalentResult::kHandled;
  }
  return ui::PerformKeyEquivalentResult::kUnhandled;
}

- (ui::PerformKeyEquivalentResult)postPerformKeyEquivalent:(NSEvent*)event
                                                    window:(NSWindow*)window
                                              isRedispatch:(BOOL)isRedispatch {
  VLOG(1) << "Atrium: courtyard passing key-equivalent to main menu (keyCode "
          << event.keyCode << ")";
  return ui::PerformKeyEquivalentResult::kPassToMainMenu;
}

@end

namespace atrium {

namespace {

// Keeps the delegate alive for the window's lifetime. CommandDispatcher holds
// its delegate weakly.
const char kCourtyardDispatcherDelegateKey = '\0';

// A JS number reaches base::Value as an int when it has no fractional part, so
// a rect of whole plane units would fail every is_double() check.
double AsDouble(const base::Value& value) {
  if (value.is_int()) {
    return static_cast<double>(value.GetInt());
  }
  return value.is_double() ? value.GetDouble()
                            : std::numeric_limits<double>::quiet_NaN();
}

// The sidebar drop messages a canvas card drag reuses. Forwarded whole to the
// browser window, whose handlers already do every one of them - naming them
// here rather than reimplementing keeps ONE meaning per message.
constexpr const char* kForwardedSidebarMessages[] = {
    "sidebarSpaceSelected",   "sidebarTabClicked",
    "sidebarTabReordered",    "sidebarPinnedReordered",
    "sidebarTabPinned",       "sidebarTabUnpinned",
    "sidebarRowFavorited",    "sidebarFavoriteReordered",
    "sidebarFavoriteMovedOut", "sidebarTabMovedToSpace",
    "sidebarTabUngrouped",
};

}  // namespace

CourtyardWidget::CourtyardWidget(AtriumBrowserWindow* parent)
    : AtriumWebSurfaceWidget(parent->widget(),
                             "courtyard",
                             "AtriumCourtyard",
                             /*activatable=*/true),
      parent_(parent) {
  // Route this activatable child window's unconsumed key equivalents to the
  // main menu (Cmd+Q) instead of the parent's web-content redispatch - see
  // AtriumCourtyardCommandDispatcherDelegate's doc above.
  NSWindow* ns_window = widget_->GetNativeWindow().GetNativeNSWindow();
  if ([ns_window conformsToProtocol:@protocol(CommandDispatchingWindow)]) {
    CommandDispatcher* dispatcher =
        [static_cast<id<CommandDispatchingWindow>>(ns_window) commandDispatcher];
    AtriumCourtyardCommandDispatcherDelegate* delegate =
        [[AtriumCourtyardCommandDispatcherDelegate alloc] initWithParent:parent_];
    dispatcher.delegate = delegate;
    objc_setAssociatedObject(ns_window, &kCourtyardDispatcherDelegateKey,
                             delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }

  widget_->AddObserver(this);
  // Removed in the destructor, which AtriumBrowserWindow's destructor runs
  // BEFORE resetting its own widget_ - so this observer can never dangle
  // across the parent widget's destruction.
  parent_->widget()->AddObserver(this);
}

CourtyardWidget::~CourtyardWidget() {
  parent_->widget()->RemoveObserver(this);
  // Deliberately before the hide, so no SetCourtyardOpen runs: the parent
  // window's destructor is the only thing that destroys this, and its root
  // view is already on its way out.
  widget_->RemoveObserver(this);
}

bool CourtyardWidget::RetriesOnMissingChromeContext() const {
  return false;
}

void CourtyardWidget::ApplyParentBounds() {
  widget_->SetBounds(parent_->widget()->GetWindowBoundsInScreen());
}

void CourtyardWidget::Show() {
  BuildContents();
  if (!web_contents_) {
    // Showing anyway blanks the window through SetCourtyardOpen and leaves no
    // page to take Escape or send courtyardDismiss, so there is no way back.
    return;
  }
  parent_->PushOverlayTheme(web_contents_.get());
  ApplyParentBounds();
  widget_->Show();
  widget_->Activate();
  if (web_view_) {
    web_view_->RequestFocus();
  }
  web_contents_->Focus();
  if (renderer_ready_) {
    PushSnapshot();
  }
}

void CourtyardWidget::Close() {
  if (!widget_->IsVisible()) {
    return;
  }
  widget_->Hide();
  NotifyPageClosed();
}

void CourtyardWidget::NotifyPageClosed() {
  if (!web_contents_ || !renderer_ready_) {
    return;
  }
  // The page keeps the session and the viewport, and neither survives the
  // widget being hidden from out here: an open session refuses the next open
  // step, so the plane would come back at the pan and zoom it was left at.
  web_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(
      u"window.atriumCourtyardClosed && window.atriumCourtyardClosed()",
      base::NullCallback());
}

bool CourtyardWidget::IsShowing() const {
  return widget_ && widget_->IsVisible();
}

gfx::NativeWindow CourtyardWidget::NativeWindowForTesting() const {
  return widget_->GetNativeWindow();
}

void CourtyardWidget::PushSnapshot() {
  if (!web_contents_) {
    return;
  }
  const std::string model = parent_->CourtyardSnapshotJSON();
  // JSON.parse rather than interpolation - tab titles are attacker-
  // controlled, same posture as PushSidebarSnapshot's script.
  const std::u16string script =
      u"window.atriumSetCourtyard && window.atriumSetCourtyard(JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(model)) + u"))";
  web_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(script,
                                                           base::NullCallback());
}

void CourtyardWidget::ForwardPinch(double delta_y) {
  if (!web_contents_ || !renderer_ready_) {
    return;
  }
  const std::u16string script =
      u"window.atriumCourtyardPinch && window.atriumCourtyardPinch(" +
      base::UTF8ToUTF16(base::NumberToString(delta_y)) + u")";
  web_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(script,
                                                           base::NullCallback());
}

void CourtyardWidget::OnWidgetBoundsChanged(views::Widget* widget,
                                            const gfx::Rect& new_bounds) {
  if (widget != parent_->widget()) {
    return;
  }
  if (!widget_->IsVisible()) {
    return;
  }
  ApplyParentBounds();
}

void CourtyardWidget::OnWidgetVisibilityChanged(views::Widget* widget,
                                                bool visible) {
  if (widget != widget_.get()) {
    return;
  }
  parent_->SetCourtyardOpen(visible);
}

void CourtyardWidget::OnSurfaceMessage(std::string_view name,
                                       const base::ListValue& args) {
  if (name == "courtyardReady") {
    renderer_ready_ = true;
    // Covers a push landing before this document committed (BuildContents's
    // push, same reasoning as commandBarReady's replay).
    if (web_contents_) {
      parent_->PushOverlayTheme(web_contents_.get());
    }
    PushSnapshot();
    return;
  }
  if (name == "courtyardDismiss") {
    Close();
    return;
  }
  for (const char* forwarded : kForwardedSidebarMessages) {
    if (name == forwarded) {
      parent_->OnSurfaceMessage(name, args);
      return;
    }
  }
  // AsDouble, not is_double: a zoom clamped to the ceiling arrives as the int
  // 2 and the old gate dropped it, so the plane sat at its limit while the
  // widget still believed it was below 1.0 and refused to close.
  if (name == "courtyardZoom" && args.size() >= 1 &&
      (args[0].is_int() || args[0].is_double())) {
    zoom_ = AsDouble(args[0]);
    return;
  }
  if (name == "courtyardRoomRect" && args.size() >= 5 && args[0].is_int()) {
    parent_->ApplyCourtyardRoomRect(args[0].GetInt(), AsDouble(args[1]),
                                    AsDouble(args[2]), AsDouble(args[3]),
                                    AsDouble(args[4]));
    return;
  }
  if (name == "courtyardCardX" && args.size() >= 4 && args[0].is_int() &&
      args[1].is_string() && args[2].is_int()) {
    parent_->ApplyCourtyardCardX(args[0].GetInt(), args[1].GetString(),
                                 args[2].GetInt(), AsDouble(args[3]));
    return;
  }
  // ActivateCourtyardCard existed and was reached only by a test calling it
  // directly, so a real card click was sent here and dropped.
  if (name == "courtyardCardActivated" && args.size() >= 3 &&
      args[0].is_int() && args[1].is_string() && args[2].is_int()) {
    parent_->ActivateCourtyardCard(args[0].GetInt(), args[1].GetString(),
                                   args[2].GetInt());
    return;
  }
}

}  // namespace atrium
