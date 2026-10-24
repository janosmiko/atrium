// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_theme_studio_widget.mm
#include "chrome/browser/ui/atrium/atrium_theme_studio_widget.h"

#import <objc/runtime.h>

#include <utility>

#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_map.h"
#import "chrome/browser/ui/atrium/atrium_overlay_key_mac.h"
#import "chrome/browser/ui/atrium/atrium_surface_key_route_mac.h"
#include "chrome/browser/ui/atrium/atrium_theme_studio_placement.h"
#include "content/public/browser/web_contents.h"
#import "ui/base/cocoa/command_dispatcher.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"

// This window is activatable, so without a delegate the parent's web content
// claims every unconsumed key equivalent and the renderer's async bounce eats
// Cmd+Q while the studio owns key.
@interface AtriumThemeStudioDispatcherDelegate
    : NSObject <CommandDispatcherDelegate> {
  // The browser window dies before this delegate's NSWindow deallocs on quit.
  base::WeakPtr<atrium::AtriumBrowserWindow> _parent;
}
- (instancetype)initWithParent:(atrium::AtriumBrowserWindow*)parent;
@end

@implementation AtriumThemeStudioDispatcherDelegate

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
      VLOG(1) << "Atrium: theme studio passing key-equivalent to main menu "
                 "(keyCode "
              << event.keyCode << ")";
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
  VLOG(1) << "Atrium: theme studio passing key-equivalent to main menu "
             "(keyCode "
          << event.keyCode << ")";
  return ui::PerformKeyEquivalentResult::kPassToMainMenu;
}

@end

namespace atrium {

namespace {

// CommandDispatcher holds its delegate weakly, so an associated object keeps
// it alive for the studio window's lifetime.
const char kThemeStudioDispatcherDelegateKey = '\0';

}  // namespace

ThemeStudioWidget::ThemeStudioWidget(AtriumBrowserWindow* parent)
    : AtriumWebSurfaceWidget(parent->widget(),
                             "theme",
                             "AtriumThemeStudio",
                             /*activatable=*/true),
      parent_(parent) {
  NSWindow* ns_window = widget_->GetNativeWindow().GetNativeNSWindow();
  if ([ns_window conformsToProtocol:@protocol(CommandDispatchingWindow)]) {
    CommandDispatcher* dispatcher =
        [static_cast<id<CommandDispatchingWindow>>(ns_window)
            commandDispatcher];
    AtriumThemeStudioDispatcherDelegate* delegate =
        [[AtriumThemeStudioDispatcherDelegate alloc] initWithParent:parent_];
    dispatcher.delegate = delegate;
    objc_setAssociatedObject(ns_window, &kThemeStudioDispatcherDelegateKey,
                             delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }

  // Removed in the destructor, which AtriumBrowserWindow's destructor runs
  // before resetting its own widget_, so the observation cannot dangle.
  parent_->widget()->AddObserver(this);
}

ThemeStudioWidget::~ThemeStudioWidget() {
  parent_->widget()->RemoveObserver(this);
}

void ThemeStudioWidget::OnChromeContextReady() {
  awaiting_chrome_context_ = false;
  if (!IsShowing()) {
    return;
  }
  BuildContents();
  if (!web_contents_) {
    return;
  }
  parent_->PushOverlayTheme(web_contents_.get());
  PushPayload();
  VLOG(1) << "Atrium: theme studio contents built on chrome-context retry";
}

void ThemeStudioWidget::PushPayload() {
  if (!renderer_ready_ || !web_contents_) {
    payload_pending_ = true;
    return;
  }
  payload_pending_ = false;
  // JSON.parse rather than interpolation: the space name is user data and
  // this executes at browser privilege.
  CallPage(u"window.atriumSetThemeStudio && "
           u"window.atriumSetThemeStudio(JSON.parse(" +
           base::UTF8ToUTF16(base::GetQuotedJSONString(payload_json_)) +
           u"))");
}

void ThemeStudioWidget::Show(const std::string& payload_json,
                             int rail_index,
                             const gfx::Rect& anchor_in_screen) {
  payload_json_ = payload_json;
  rail_index_ = rail_index;
  anchor_in_screen_ = anchor_in_screen;
  BuildContents();
  if (web_contents_) {
    parent_->PushOverlayTheme(web_contents_.get());
  }
  PushPayload();
  last_parent_bounds_ = parent_->widget()->GetWindowBoundsInScreen();
  widget_->SetBounds(
      PlaceThemeStudioAtAnchor(anchor_in_screen_, last_parent_bounds_));
  intended_visible_ = true;
  VLOG(1) << "Atrium: theme studio window showing";
  widget_->Show();
  // Show() alone does not reliably hand an activatable child window key
  // status, and the sliders only take the keyboard once it has it.
  widget_->Activate();
  FocusPage();
}

void ThemeStudioWidget::FocusPage() {
  if (!intended_visible_ || !widget_->IsVisible()) {
    return;
  }
  if (web_view_) {
    web_view_->RequestFocus();
  }
  if (web_contents_) {
    web_contents_->Focus();
  }
}

void ThemeStudioWidget::DragBy(int dx, int dy) {
  if (!IsShowing()) {
    return;
  }
  gfx::Rect bounds = widget_->GetWindowBoundsInScreen();
  bounds.Offset(dx, dy);
  widget_->SetBounds(ClampThemeStudioBounds(bounds, last_parent_bounds_));
}

void ThemeStudioWidget::Close() {
  intended_visible_ = false;
  rail_index_ = -1;
  if (!widget_->IsVisible()) {
    return;
  }
  VLOG(1) << "Atrium: theme studio window closing";
  // Read before Hide() moves key. A studio closing in a background window that
  // handed focus back would pull itself in front of the window the user is in.
  const bool hand_focus_back = KeyWindowIsInGroup(
      widget_->GetNativeWindow().GetNativeNSWindow(),
      parent_->widget()->GetNativeWindow().GetNativeNSWindow());
  // Same order as FindBarWidget::Close: resign the first responder before
  // anything else moves, or the WebView takes the focus manager straight back.
  if (NSWindow* ns_window = widget_->GetNativeWindow().GetNativeNSWindow()) {
    [ns_window makeFirstResponder:nil];
  }
  if (views::FocusManager* focus = widget_->GetFocusManager()) {
    focus->ClearFocus();
    focus->SetStoredFocusView(nullptr);
  }
  widget_->Hide();
  if (!hand_focus_back) {
    return;
  }
  if (content::WebContents* contents =
          parent_->CurrentlyDisplayedWebContents()) {
    contents->Focus();
  }
  parent_->widget()->Activate();
}

bool ThemeStudioWidget::IsShowing() const {
  return widget_ && widget_->IsVisible();
}

int ThemeStudioWidget::SpaceIndex() const {
  return IsShowing() ? rail_index_ : -1;
}

gfx::Rect ThemeStudioWidget::BoundsInScreen() const {
  return widget_ ? widget_->GetWindowBoundsInScreen() : gfx::Rect();
}

bool ThemeStudioWidget::PageHoldsKeyboardForTesting() const {
  return web_view_ && web_view_->HasFocus();
}

void ThemeStudioWidget::OnWidgetBoundsChanged(views::Widget* widget,
                                              const gfx::Rect& new_bounds) {
  if (widget != parent_->widget() || !IsShowing()) {
    return;
  }
  const gfx::Rect bounds = FollowParentBounds(
      widget_->GetWindowBoundsInScreen(), last_parent_bounds_, new_bounds);
  last_parent_bounds_ = new_bounds;
  widget_->SetBounds(bounds);
}

void ThemeStudioWidget::OnSurfaceMessage(std::string_view name,
                                         const base::ListValue& args) {
  if (name == "themeStudioReady") {
    renderer_ready_ = true;
    parent_->PushOverlayTheme(web_contents_.get());
    // A reloaded or crashed-and-restarted page announces ready again and
    // needs the payload pushed again, not just a page that was never ready.
    if (!payload_json_.empty()) {
      PushPayload();
    }
    return;
  }
  if (name == "themeStudioDragBy" && args.size() >= 2 && args[0].is_int() &&
      args[1].is_int()) {
    DragBy(args[0].GetInt(), args[1].GetInt());
    return;
  }
  if (name == "themeStudioClose") {
    Close();
    return;
  }
  if (name == "settingsThemePreview" || name == "settingsThemeChanged") {
    parent_->ApplySidebarThemeEdit(name, args);
    return;
  }
}

}  // namespace atrium
