// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_quick_menu_widget.mm
#include "chrome/browser/ui/atrium/atrium_quick_menu_widget.h"

#import <Cocoa/Cocoa.h>

#include <climits>
#include <utility>

#include "base/functional/callback_helpers.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/strings/sys_string_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_favicon_source.h"
#include "chrome/browser/ui/browser.h"
#include "components/input/native_web_keyboard_event.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "third_party/blink/public/common/input/web_input_event.h"
#include "ui/events/keycodes/keyboard_codes.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/widget/widget.h"

namespace atrium {

namespace {

// The open's click lands in the sidebar's WebContents, a live contender for
// key, so the widget's own activation can bounce back within a beat.
constexpr base::TimeDelta kQuickMenuFocusBounceGuard = base::Milliseconds(250);

}  // namespace

AtriumQuickMenuWidget::AtriumQuickMenuWidget(AtriumBrowserWindow* parent)
    : AtriumWebSurfaceWidget(parent->widget(),
                             "quickmenu",
                             "AtriumQuickMenu",
                             /*activatable=*/true),
      parent_(parent) {
  widget_->AddObserver(this);
  parent_->widget()->AddObserver(this);
}

AtriumQuickMenuWidget::~AtriumQuickMenuWidget() {
  if (web_contents_) {
    web_contents_->SetDelegate(nullptr);
  }
  parent_->widget()->RemoveObserver(this);
  widget_->RemoveObserver(this);
}

bool AtriumQuickMenuWidget::RetriesOnMissingChromeContext() const {
  return false;
}

void AtriumQuickMenuWidget::BeforeAttachToContents() {
  // The reserved chrome context, never the presenter's profile - that is what
  // keeps every chrome surface in one renderer process.
  web_contents_->SetDelegate(this);
}

void AtriumQuickMenuWidget::OnRenderWidgetHostViewCreated(
    content::RenderWidgetHostView* rwhv) {
  rwhv->EnableAutoResize(gfx::Size(1, 1), gfx::Size(INT_MAX, INT_MAX));
}

void AtriumQuickMenuWidget::ApplyBounds() {
  const gfx::Rect window = parent_->widget()->GetWindowBoundsInScreen();
  if (window.IsEmpty()) {
    return;
  }
  const gfx::Rect in_window =
      PlaceQuickMenu(anchor_in_window_, window.size(), content_size_);
  // Screen coordinates: this is a top-level widget, the same reason
  // CommandBarWidget::ApplyParentBounds gives.
  widget_->SetBounds(in_window + window.OffsetFromOrigin());
}

void AtriumQuickMenuWidget::Show(const gfx::Rect& anchor_in_window) {
  anchor_in_window_ = anchor_in_window;
  BuildContents();
  if (web_contents_) {
    parent_->PushOverlayTheme(web_contents_.get());
  }
  PushIconScope();
  SetSiteJSON(parent_->CurrentQuickMenuSiteJSON());
  ApplyBounds();
  intended_visible_ = true;
  shown_at_ = base::TimeTicks::Now();
  VLOG(1) << "Atrium: quick menu showing at " << anchor_in_window_.ToString();
  parent_->ReleaseStartupHold();
  widget_->Show();
  // Show() alone does not reliably hand an activatable child window key.
  widget_->Activate();
  FocusPage();
}

void AtriumQuickMenuWidget::FocusPage() {
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

void AtriumQuickMenuWidget::Close() {
  // Marked BEFORE Hide() so the deactivation Hide() causes is not misread as
  // a click-away and fought with a reentrant Activate.
  intended_visible_ = false;
  anchor_in_window_ = gfx::Rect();
  if (!widget_->IsVisible()) {
    return;
  }
  VLOG(1) << "Atrium: quick menu closing";
  widget_->Hide();
}

bool AtriumQuickMenuWidget::IsShowing() const {
  return widget_ && widget_->IsVisible();
}

void AtriumQuickMenuWidget::PushIconScope() {
  if (!renderer_ready_ || !web_contents_) {
    return;
  }
  // This document renders in the shared chrome context, not the presenter's
  // profile, so its icon requests need the scope token.
  const std::u16string script =
      u"window.atriumSetQuickMenuIconScope && atriumSetQuickMenuIconScope(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(
          IconScopeFor(parent_->browser()->profile()))) +
      u")";
  web_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(script,
                                                          base::NullCallback());
}

void AtriumQuickMenuWidget::SetActionsJSON(const std::string& json) {
  pending_actions_json_ = json;
  if (!renderer_ready_ || !web_contents_ || json.empty()) {
    return;
  }
  // JSON.parse rather than interpolation: the snapshot carries extension
  // names, which are attacker influenced, and this runs at browser privilege.
  const std::u16string script =
      u"window.atriumSetQuickMenuActions && atriumSetQuickMenuActions("
      u"JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))";
  web_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(script,
                                                          base::NullCallback());
}

void AtriumQuickMenuWidget::SetSiteJSON(const std::string& json) {
  pending_site_json_ = json;
  if (!renderer_ready_ || !web_contents_ || json.empty()) {
    return;
  }
  const std::u16string script =
      u"window.atriumSetQuickMenuSite && atriumSetQuickMenuSite(JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))";
  web_contents_->GetPrimaryMainFrame()->ExecuteJavaScript(script,
                                                          base::NullCallback());
}

void AtriumQuickMenuWidget::ShowSharePicker() {
  content::WebContents* contents = parent_->CurrentlyDisplayedWebContents();
  if (!contents) {
    return;
  }
  const GURL url = contents->GetVisibleURL();
  if (!url.SchemeIsHTTPOrHTTPS()) {
    return;
  }
  NSURL* ns_url = [NSURL URLWithString:base::SysUTF8ToNSString(url.spec())];
  if (!ns_url) {
    return;
  }
  NSSharingServicePicker* picker =
      [[NSSharingServicePicker alloc] initWithItems:@[ ns_url ]];
  NSView* anchor_view =
      parent_->widget()->GetNativeWindow().GetNativeNSWindow().contentView;
  if (!anchor_view) {
    return;
  }
  // The button's rect, flipped into the content view's bottom-left space.
  const NSRect rect = NSMakeRect(
      anchor_in_window_.x(),
      NSHeight(anchor_view.bounds) - anchor_in_window_.bottom(),
      anchor_in_window_.width(), anchor_in_window_.height());
  [picker showRelativeToRect:rect ofView:anchor_view preferredEdge:NSMinYEdge];
}

void AtriumQuickMenuWidget::OnSurfaceMessage(std::string_view name,
                                             const base::ListValue& args) {
  if (name == "quickMenuReady") {
    renderer_ready_ = true;
    PushIconScope();
    SetActionsJSON(pending_actions_json_);
    SetSiteJSON(pending_site_json_);
    return;
  }
  if (name == "pillActionClicked") {
    if (args.size() >= 1 && args[0].is_string()) {
      const std::string id = args[0].GetString();
      // read before Close, because the tile is gone after it. Screen
      // coordinates, so the rect still means the same place once this widget
      // has closed and the anchor lands in the main window.
      const gfx::Rect tile =
          args.size() >= 2 ? ScreenRectFromSurfaceArg(args[1]) : gfx::Rect();
      // Closed FIRST: the popup anchors on the surface view that owns the
      // clicked button, never on this widget.
      Close();
      parent_->SetExtensionPopupAnchorRect(tile);
      parent_->ClickExtensionActionFromSurface(id);
    }
    return;
  }
  if (name == "extensionPinToggled") {
    if (args.size() >= 1 && args[0].is_string()) {
      parent_->ToggleExtensionPin(args[0].GetString());
    }
    return;
  }
  if (name == "extensionChipContextMenu") {
    if (args.size() >= 1 && args[0].is_string()) {
      const std::string id = args[0].GetString();
      const gfx::Rect tile =
          args.size() >= 2 ? ScreenRectFromSurfaceArg(args[1]) : gfx::Rect();
      // Closed FIRST, same as pillActionClicked: otherwise this widget's
      // deactivation lands inside the focus-bounce guard and Activate()s
      // itself back over the menu that just opened.
      Close();
      parent_->ShowExtensionContextMenuFromSurface(id, tile);
    }
    return;
  }
  if (name == "quickMenuManage") {
    Close();
    parent_->OpenExtensionsPage();
    return;
  }
  if (name == "quickMenuStore") {
    Close();
    parent_->OpenExtensionStorePage();
    return;
  }
  if (name == "quickMenuSiteInfo") {
    const gfx::Rect anchor = anchor_in_window_;
    Close();
    parent_->ShowPageInfoForQuickMenu(anchor);
    return;
  }
  if (name == "quickMenuShare") {
    ShowSharePicker();
    return;
  }
  if (name == "quickMenuCopy") {
    Close();
    parent_->CopyCurrentPageUrl();
    return;
  }
  // Unknown names are ignored: a renderer running ahead of this build
  // degrades to a no-op.
}

void AtriumQuickMenuWidget::ResizeDueToAutoResize(
    content::WebContents* web_contents,
    const gfx::Size& new_size) {
  DCHECK_EQ(web_contents_.get(), web_contents);
  if (new_size.IsEmpty()) {
    return;
  }
  content_size_ = new_size;
  if (widget_->IsVisible()) {
    ApplyBounds();
  }
}

bool AtriumQuickMenuWidget::HandleKeyboardEvent(
    content::WebContents* source,
    const input::NativeWebKeyboardEvent& event) {
  if (event.GetType() == blink::WebInputEvent::Type::kRawKeyDown &&
      event.windows_key_code == ui::VKEY_ESCAPE) {
    Close();
    return true;
  }
  return false;
}

void AtriumQuickMenuWidget::OnWidgetBoundsChanged(
    views::Widget* widget,
    const gfx::Rect& new_bounds) {
  // Only the PARENT's moves matter. This widget's own bounds changes (the
  // ones ApplyBounds causes) are ignored, so no self-recursion.
  if (widget != parent_->widget() || !widget_->IsVisible()) {
    return;
  }
  ApplyBounds();
}

void AtriumQuickMenuWidget::OnWidgetActivationChanged(views::Widget* widget,
                                                      bool active) {
  // This observer also watches the parent (for bounds). The parent losing key
  // to this menu on every open would read as an instant dismissal.
  if (widget != widget_.get()) {
    return;
  }
  if (active) {
    // Key alone leaves keystrokes at the widget's root view, so the page has
    // to be refocused on every gain, not only the one Show() drives.
    if (intended_visible_) {
      FocusPage();
    }
    return;
  }
  // A deactivation caused by a deliberate Close()/Hide() must not re-enter.
  if (!intended_visible_) {
    return;
  }
  const base::TimeDelta since_shown = base::TimeTicks::Now() - shown_at_;
  VLOG(1) << "Atrium: quick menu lost key " << since_shown.InMilliseconds()
          << "ms after show";
  if (since_shown < kQuickMenuFocusBounceGuard) {
    // Merely not closing would leave the card visible but not key, looking
    // interactive while dropping every keystroke.
    widget_->Activate();
    return;
  }
  last_closed_at_ = base::TimeTicks::Now();
  Close();
}

}  // namespace atrium
