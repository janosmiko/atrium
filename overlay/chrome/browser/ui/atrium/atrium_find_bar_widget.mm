// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_find_bar_widget.mm
#include "chrome/browser/ui/atrium/atrium_find_bar_widget.h"

#import <objc/runtime.h>

#include <utility>

#include "base/functional/bind.h"
#include "base/json/json_writer.h"
#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/task/single_thread_task_runner.h"
#include "base/values.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_child_widget_focus.h"
#include "chrome/browser/ui/atrium/atrium_command_bar_widget.h"
#include "chrome/browser/ui/atrium/atrium_find_action.h"
#include "chrome/browser/ui/atrium/atrium_find_session.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_map.h"
#import "chrome/browser/ui/atrium/atrium_overlay_key_mac.h"
#import "chrome/browser/ui/atrium/atrium_surface_key_route_mac.h"
#include "content/public/browser/web_contents.h"
#import "ui/base/cocoa/command_dispatcher.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget.h"

// Copied from AtriumCommandBarDispatcherDelegate: this bar is activatable too,
// so without it the parent's web content claims every unconsumed key
// equivalent and the renderer's async bounce eats Cmd+Q while the bar owns key.
@interface AtriumFindBarDispatcherDelegate
    : NSObject <CommandDispatcherDelegate> {
  // The browser window dies before this delegate's NSWindow deallocs on quit.
  base::WeakPtr<atrium::AtriumBrowserWindow> _parent;
}
- (instancetype)initWithParent:(atrium::AtriumBrowserWindow*)parent;
@end

@implementation AtriumFindBarDispatcherDelegate

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
      VLOG(1)
          << "Atrium: find bar passing key-equivalent to main menu (keyCode "
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
  VLOG(1) << "Atrium: find bar passing key-equivalent to main menu (keyCode "
          << event.keyCode << ")";
  return ui::PerformKeyEquivalentResult::kPassToMainMenu;
}

@end

namespace atrium {

namespace {

// CommandDispatcher holds its delegate weakly, so an associated object keeps
// it alive for the bar window's lifetime.
const char kFindBarDispatcherDelegateKey = '\0';

// The whole widget, ring included. find.css's .fb carries a 6px margin for
// that ring, so the card itself is this less kFindBarOutset on every side.
constexpr gfx::Size kFindBarSize(348, 52);
constexpr int kFindBarOutset = 6;

// From the pane's corner to the CARD, so the widget hangs kFindBarOutset
// further out and the bar keeps its own 12.
constexpr int kFindBarInset = 12;

}  // namespace

FindBarWidget::FindBarWidget(AtriumBrowserWindow* parent)
    : AtriumWebSurfaceWidget(parent->widget(),
                             "find",
                             "AtriumFindBar",
                             /*activatable=*/true),
      parent_(parent) {
  NSWindow* ns_window = widget_->GetNativeWindow().GetNativeNSWindow();
  if ([ns_window conformsToProtocol:@protocol(CommandDispatchingWindow)]) {
    CommandDispatcher* dispatcher =
        [static_cast<id<CommandDispatchingWindow>>(ns_window)
            commandDispatcher];
    AtriumFindBarDispatcherDelegate* delegate =
        [[AtriumFindBarDispatcherDelegate alloc] initWithParent:parent_];
    dispatcher.delegate = delegate;
    objc_setAssociatedObject(ns_window, &kFindBarDispatcherDelegateKey,
                             delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }

  widget_->AddObserver(this);
  // Removed in the destructor, which AtriumBrowserWindow's destructor runs
  // before resetting its own widget_, so neither observation can dangle.
  parent_->widget()->AddObserver(this);
  parent_->widget()->GetRootView()->AddObserver(this);

  session_ = std::make_unique<FindSession>(base::BindRepeating(
      [](base::WeakPtr<FindBarWidget> self, int active, int total,
         bool final_update) {
        if (self) {
          self->PushResult(active, total);
        }
      },
      weak_factory_.GetWeakPtr()));
}

FindBarWidget::~FindBarWidget() {
  parent_->widget()->GetRootView()->RemoveObserver(this);
  parent_->widget()->RemoveObserver(this);
  widget_->RemoveObserver(this);
}

void FindBarWidget::OnChromeContextReady() {
  awaiting_chrome_context_ = false;
  if (!IsShowing()) {
    return;
  }
  BuildContents();
  if (!web_contents_) {
    return;
  }
  ApplyBarBounds();
  ResetPage(base::UTF16ToUTF8(query_));
  VLOG(1) << "Atrium: find bar contents built on chrome-context retry";
}

void FindBarWidget::ResetPage(const std::string& query) {
  base::DictValue dict;
  dict.Set("query", query);
  std::string json;
  base::JSONWriter::Write(dict, &json);
  if (!renderer_ready_) {
    pending_reset_ = json;
    has_pending_reset_ = true;
    return;
  }
  // JSON.parse rather than interpolation: the query is page-controlled text
  // and this executes at browser privilege.
  CallPage(u"window.atriumResetFind && window.atriumResetFind(JSON.parse(" +
           base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))");
}

void FindBarWidget::PushResult(int active, int total) {
  base::DictValue dict;
  dict.Set("active", active);
  dict.Set("total", total);
  std::string json;
  base::JSONWriter::Write(dict, &json);
  if (!renderer_ready_) {
    pending_result_ = json;
    return;
  }
  CallPage(
      u"window.atriumSetFindResult && window.atriumSetFindResult(JSON.parse(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))");
}

void FindBarWidget::ApplyQueryChange(const std::u16string& query) {
  query_ = query;
  session_->Apply(TargetContents(), FindTrigger::kQueryChanged, query_);
  // An emptied field stops the search, and the page has no count to draw.
  if (query_.empty()) {
    PushResult(-1, -1);
  }
}

content::WebContents* FindBarWidget::TargetContents() const {
  return parent_->CurrentlyDisplayedWebContents();
}

void FindBarWidget::ApplyBarBounds() {
  gfx::Rect pane = parent_->GetWebContentBoundsInScreen();
  if (pane.IsEmpty()) {
    pane = parent_->widget()->GetWindowBoundsInScreen();
  }
  // The pane already excludes the sidebar column, so this only decides which
  // end of it the bar sits at.
  const bool on_left = parent_->IsSidebarOnRight();
  const int x = on_left ? pane.x() + kFindBarInset - kFindBarOutset
                        : pane.right() - kFindBarInset - kFindBarSize.width() +
                              kFindBarOutset;
  const int y = pane.y() + kFindBarInset - kFindBarOutset;
  widget_->SetBounds(
      gfx::Rect(x, y, kFindBarSize.width(), kFindBarSize.height()));
}

void FindBarWidget::Show() {
  BuildContents();
  // The document keeps its tint across opens, but a space switch while the bar
  // was closed changed it.
  parent_->PushOverlayTheme(web_contents_.get());
  ApplyBarBounds();

  content::WebContents* contents = TargetContents();
  // Chrome opens on the tab's own last search, and the request table answers
  // that open with find_match false so the page does not scroll.
  query_ = session_->InitialText(contents);
  ResetPage(base::UTF16ToUTF8(query_));
  session_->Apply(contents, FindTrigger::kOpen, query_);

  intended_visible_ = true;
  VLOG(1) << "Atrium: find bar showing";
  parent_->ReleaseStartupHold();
  widget_->Show();
  // Show() alone does not reliably hand an activatable child window key
  // status, and the field can only become first responder once it has it.
  widget_->Activate();
  FocusPage();
}

void FindBarWidget::FocusPage() {
  if (!intended_visible_ || !widget_->IsVisible()) {
    return;
  }
  // Safe in any app state: this only moves focus inside this widget.
  if (web_view_) {
    web_view_->RequestFocus();
  }
  if (web_contents_) {
    web_contents_->Focus();
  }
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE,
      base::BindOnce(&FindBarWidget::MaybeReclaimKey,
                     weak_factory_.GetWeakPtr(), activation_loss_epoch_));
}

bool FindBarWidget::KeyIsInOwnGroup() const {
  // Nil-safe both ways: a parent with no NSWindow, or key held by another app
  // (nil keyWindow), both answer false and decline the reclaim.
  NSWindow* parent_window =
      parent_->widget()->GetNativeWindow().GetNativeNSWindow();
  return parent_window != nil &&
         KeyWindowIsInGroup(widget_->GetNativeWindow().GetNativeNSWindow(),
                            parent_window);
}

void FindBarWidget::MaybeReclaimKey(int epoch) {
  const bool key_is_parent = KeyIsInOwnGroup();
  if (!ShouldReclaimKey(intended_visible_ && widget_->IsVisible(),
                        epoch == activation_loss_epoch_, widget_->IsActive(),
                        NSApp.active, key_is_parent)) {
    VLOG(1) << "Atrium: find bar declined to reclaim key";
    return;
  }
  // Widget::Activate() is [NSApp activateIgnoringOtherApps:YES], so every
  // other case above has to decline rather than haul the app forward.
  VLOG(1) << "Atrium: find bar reclaiming key from the parent window";
  widget_->Activate();
  if (web_view_) {
    web_view_->RequestFocus();
  }
  if (web_contents_) {
    web_contents_->Focus();
  }
}

void FindBarWidget::OnWidgetActivationChanged(views::Widget* widget,
                                              bool active) {
  // This object also observes the parent widget, whose activation says nothing
  // about the bar.
  if (widget != widget_.get() || !active) {
    return;
  }
  // The bar got key on its own, so a reclaim still in flight must not fire a
  // second Activate.
  ++activation_loss_epoch_;
  // Key alone leaves keystrokes at the widget's root view.
  if (intended_visible_) {
    FocusPage();
  }
}

bool FindBarWidget::PageHoldsKeyboardForTesting() const {
  return web_view_ && web_view_->HasFocus();
}

bool FindBarWidget::KeyIsInOwnGroupForTesting() const {
  return KeyIsInOwnGroup();
}

void FindBarWidget::ClearPageFocusForTesting() {
  if (views::FocusManager* focus = widget_->GetFocusManager()) {
    focus->ClearFocus();
  }
}

void FindBarWidget::FindQueryChangedForTesting(const std::u16string& query) {
  ApplyQueryChange(query);
}

void FindBarWidget::FindNext(bool forward) {
  if (!IsShowing()) {
    Show();
  }
  session_->Apply(TargetContents(),
                  forward ? FindTrigger::kNext : FindTrigger::kPrevious,
                  query_);
}

void FindBarWidget::StopSession() {
  session_->Stop();
  VLOG(1) << "Atrium: find session stopped";
}

void FindBarWidget::Close() {
  StopSession();
  // Before Hide(), so the deactivation it triggers is not misread as a
  // spurious bounce and fought with a reentrant Activate().
  intended_visible_ = false;
  if (!widget_->IsVisible()) {
    return;
  }
  VLOG(1) << "Atrium: find bar closing";
  // Read before Hide() moves key. A tab handoff closes the bar of whichever
  // window held the tab, and a background one that hands focus back pulls
  // itself in front of the window the user is typing in.
  const bool hand_focus_back = KeyWindowIsInGroup(
      widget_->GetNativeWindow().GetNativeNSWindow(),
      parent_->widget()->GetNativeWindow().GetNativeNSWindow());
  ReleaseChildWidgetFocus(widget_.get());
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

void FindBarWidget::EndSessionAndClose() {
  // Close() already stops the session on the remembered tab before it hands
  // focus to whatever the window shows now, which is the order a switch needs.
  Close();
}

bool FindBarWidget::IsShowing() const {
  return widget_ && widget_->IsVisible();
}

void FindBarWidget::OnSurfaceMessage(std::string_view name,
                                     const base::ListValue& args) {
  if (name == "findReady") {
    renderer_ready_ = true;
    parent_->PushOverlayTheme(web_contents_.get());
    if (has_pending_reset_) {
      has_pending_reset_ = false;
      CallPage(u"window.atriumResetFind && window.atriumResetFind(JSON.parse(" +
               base::UTF8ToUTF16(base::GetQuotedJSONString(pending_reset_)) +
               u"))");
      pending_reset_.clear();
    }
    if (!pending_result_.empty()) {
      CallPage(u"window.atriumSetFindResult && "
               u"window.atriumSetFindResult(JSON.parse(" +
               base::UTF8ToUTF16(base::GetQuotedJSONString(pending_result_)) +
               u"))");
      pending_result_.clear();
    }
    return;
  }
  if (name == "findQueryChanged" && args.size() >= 1 && args[0].is_string()) {
    ApplyQueryChange(base::UTF8ToUTF16(args[0].GetString()));
    return;
  }
  if (name == "findNext") {
    session_->Apply(TargetContents(), FindTrigger::kNext, query_);
    return;
  }
  if (name == "findPrevious") {
    session_->Apply(TargetContents(), FindTrigger::kPrevious, query_);
    return;
  }
  if (name == "findClose") {
    Close();
    return;
  }
}

void FindBarWidget::OnWidgetBoundsChanged(views::Widget* widget,
                                          const gfx::Rect& new_bounds) {
  if (widget != parent_->widget() || !IsShowing()) {
    return;
  }
  ApplyBarBounds();
}

void FindBarWidget::OnViewLayoutInvalidated(views::View* observed_view) {
  if (!IsShowing() || pane_layout_reapply_pending_) {
    return;
  }
  pane_layout_reapply_pending_ = true;
  base::SingleThreadTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&FindBarWidget::ReapplyBoundsForPaneLayout,
                                weak_factory_.GetWeakPtr()));
}

void FindBarWidget::ReapplyBoundsForPaneLayout() {
  pane_layout_reapply_pending_ = false;
  // A side flip only schedules the pane's Layout() pass async, so force it now
  // or ApplyBarBounds reads the pre-change geometry.
  parent_->widget()->GetRootView()->DeprecatedLayoutImmediately();
  ApplyBarBounds();
}

}  // namespace atrium
