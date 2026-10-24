// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_command_bar_widget.mm
#include "chrome/browser/ui/atrium/atrium_command_bar_widget.h"

#import <objc/runtime.h>

#include <map>
#include <string>
#include <vector>

#include "base/json/string_escape.h"
#include "base/logging.h"
#include "base/process/kill.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/atrium/atrium_browser_controller.h"
#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/atrium/atrium_child_widget_focus.h"
#include "chrome/browser/ui/atrium/atrium_command_bar_actions.h"
#include "chrome/browser/ui/atrium/atrium_command_bar_anchor.h"
#include "chrome/browser/ui/atrium/atrium_favicon_source.h"
#include "chrome/browser/ui/atrium/atrium_native_hotkey_map.h"
#import "chrome/browser/ui/atrium/atrium_overlay_key_mac.h"
#import "chrome/browser/ui/atrium/atrium_surface_key_route_mac.h"
#include "content/public/browser/render_frame_host.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_contents_observer.h"
#import "ui/base/cocoa/command_dispatcher.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/controls/webview/webview.h"
#include "ui/views/focus/focus_manager.h"
#include "ui/views/widget/widget.h"

// The palette is a borderless, ACTIVATABLE child window: it becomes the key
// window while open. Its CommandDispatcher, left with no delegate, bubbles any
// key equivalent it does not consume up to the parent browser window, whose
// own hierarchy contains the web-content RenderWidgetHostViewCocoa. That view
// claims the event and hands it to the renderer asynchronously; the renderer's
// unhandled-key bounce redispatches on the PARENT window while the palette is
// still key, so the event never reaches the app main menu - Cmd+Q (a main-menu
// key equivalent, not a browser IDC accelerator) is silently eaten whenever the
// palette owns key (reported as "cmd+q not working if there's no web page
// open" - the auto-opened palette is the key window in exactly that state).
//
// This delegate severs that bubble: Atrium's own combos (Cmd+T toggle, Cmd+S
// sidebar) are executed here against the parent controller, preserving today's
// behavior; everything else is passed to the main menu so Quit (and the Edit
// menu's copy/paste against the palette's field) works normally.
//
// "everything else" was routed from postPerformKeyEquivalent, and
// that hook only runs when this window's own view hierarchy DECLINES the
// event. Once the palette got a WebView, it stopped declining -
// the palette's own RenderWidgetHostViewCocoa now claims key equivalents and
// forwards them to its renderer, exactly the redispatch this class was written
// to dodge for the parent, reached one level lower. The quit chord therefore
// leaves from prePerformKeyEquivalent, before any of that; see
// atrium_surface_key_route.h for the ordering and its tests.
@interface AtriumCommandBarDispatcherDelegate
    : NSObject <CommandDispatcherDelegate> {
  // The browser window dies before this delegate's NSWindow deallocs on quit.
  base::WeakPtr<atrium::AtriumBrowserWindow> _parent;
}
- (instancetype)initWithParent:(atrium::AtriumBrowserWindow*)parent;
@end

@implementation AtriumCommandBarDispatcherDelegate

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
      // the quit has to leave here, on the way IN. postPerform below
      // is only reached when this window's view hierarchy declines the event,
      // and once the palette has a real page it never does - the WebContents
      // claims the chord and answers the renderer asynchronously, so Cmd+Q
      // disappeared. Same wording as postPerform's line on purpose: it is the
      // one both the quit scenarios and their absence gates read.
      VLOG(1) << "Atrium: palette passing key-equivalent to main menu (keyCode "
              << event.keyCode << ")";
      return ui::PerformKeyEquivalentResult::kPassToMainMenu;
    case atrium::SurfaceKeyRoute::kWebContent:
      // Not an Atrium combo: let the page's field take edit combos (Cmd+C/V/A);
      // postPerform below routes whatever it declines to the main menu.
      return ui::PerformKeyEquivalentResult::kUnhandled;
    case atrium::SurfaceKeyRoute::kDropRepeat:
      // Match the browser delegate's held-key guard so a held Cmd+T/Cmd+S does
      // not strobe the palette/sidebar.
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
  // The palette's own view hierarchy has already declined this equivalent.
  // Route it to the app main menu instead of bubbling into the parent window's
  // web content, whose async renderer round-trip eats it.
  VLOG(1) << "Atrium: palette passing key-equivalent to main menu (keyCode "
          << event.keyCode << ")";
  return ui::PerformKeyEquivalentResult::kPassToMainMenu;
}

@end

namespace atrium {

namespace {

// Associated-object key: keeps the dispatcher delegate alive for the palette
// window's lifetime (CommandDispatcher holds its delegate weakly).
const char kCommandBarDispatcherDelegateKey = '\0';

// See OnWidgetActivationChanged's comment: the widget taking key focus on
// Show() can otherwise deliver a spurious deactivation in the same beat.
constexpr base::TimeDelta kFocusBounceGuard = base::Milliseconds(250);

// M4 decided model item 2 (VM-observed): process-launch window-server churn
// can deliver a real (not same-beat) deactivation over 1s after the startup
// window's Show() - well past kFocusBounceGuard, but not a genuine
// user-driven focus loss either. Wide enough for the observed ~1s churn plus
// margin; only used for Show(is_startup_open=true).
constexpr base::TimeDelta kStartupFocusBounceGuard = base::Milliseconds(3000);

}  // namespace

// AtriumChromeContentsObserver reloads a dead chrome renderer rather than
// closing it, which leaves the palette standing as a blank card with the
// sidebar's address row still hidden.
class CommandBarWidget::RendererGoneWatcher
    : public content::WebContentsObserver {
 public:
  RendererGoneWatcher(content::WebContents* contents, CommandBarWidget* host)
      : content::WebContentsObserver(contents), host_(host) {}
  ~RendererGoneWatcher() override = default;

  // content::WebContentsObserver:
  void PrimaryMainFrameRenderProcessGone(
      base::TerminationStatus status) override {
    LOG(WARNING) << "atrium: command bar renderer died (status="
                 << static_cast<int>(status) << "), closing the palette";
    host_->Close();
  }

 private:
  const raw_ptr<CommandBarWidget> host_;
};

CommandBarWidget::CommandBarWidget(AtriumBrowserWindow* parent)
    : AtriumWebSurfaceWidget(parent->widget(),
                             "commandbar",
                             "AtriumCommandBar",
                             /*activatable=*/true),
      parent_(parent) {
  // Route this activatable child window's unconsumed key equivalents to the
  // main menu (Cmd+Q) instead of the parent's web-content redispatch, which
  // eats them - see AtriumCommandBarDispatcherDelegate's doc above.
  NSWindow* ns_window = widget_->GetNativeWindow().GetNativeNSWindow();
  if ([ns_window conformsToProtocol:@protocol(CommandDispatchingWindow)]) {
    CommandDispatcher* dispatcher =
        [static_cast<id<CommandDispatchingWindow>>(ns_window)
            commandDispatcher];
    AtriumCommandBarDispatcherDelegate* delegate =
        [[AtriumCommandBarDispatcherDelegate alloc] initWithParent:parent_];
    dispatcher.delegate = delegate;
    // CommandDispatcher holds its delegate weakly; retain it for the window's
    // lifetime via an associated object.
    objc_setAssociatedObject(ns_window, &kCommandBarDispatcherDelegateKey,
                             delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
  }

  widget_->AddObserver(this);
  // Parent-bounds tracking (m4-palettepos fix, see OnWidgetBoundsChanged):
  // removed in the destructor, which AtriumBrowserWindow's destructor runs
  // BEFORE resetting its own widget_ - so this observer can never dangle
  // across the parent widget's destruction.
  parent_->widget()->AddObserver(this);
}

CommandBarWidget::~CommandBarWidget() {
  parent_->widget()->RemoveObserver(this);
  widget_->RemoveObserver(this);
}

void CommandBarWidget::OnChromeContextReady() {
  awaiting_chrome_context_ = false;
  if (!intended_visible_) {
    return;
  }
  BuildContents();
  if (!web_contents_) {
    return;
  }
  // The widget is already on screen, so the size and the reset Show() performs
  // both happened against a scrim with nothing in it.
  ApplyParentBounds();
  ResetPage(std::string(), std::nullopt);
  // Show() had no WebView to focus, so its hand-off was a no-op and the
  // startup palette came up key but deaf - typing went nowhere.
  FocusPage();
  VLOG(1) << "Atrium: command bar contents built on chrome-context retry";
}

void CommandBarWidget::OnWebContentsCreated() {
  renderer_gone_watcher_ =
      std::make_unique<RendererGoneWatcher>(web_contents_.get(), this);
}

void CommandBarWidget::SetMatchesJSON(const std::string& json) {
  pending_matches_json_ = json;
  // JSON.parse rather than interpolation: match titles and URLs are
  // attacker-controlled and this executes at browser privilege.
  CallPage(u"window.atriumSetCommandBar && window.atriumSetCommandBar("
           u"JSON.parse(" +
           base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))");
}

void CommandBarWidget::PushActions() {
  std::map<std::string, std::vector<std::string>> bindings;
  for (const NativeHotkeyDefault& entry : NativeHotkeyDefaults()) {
    bindings[std::string(entry.action_id)] =
        NativeHotkeyMap::Get()->CombosFor(entry.action);
  }
  const std::string json =
      BuildCommandBarActionsJSON(CommandBarActionCatalog(bindings));
  CallPage(u"window.atriumSetCommandBarActions && "
           u"window.atriumSetCommandBarActions(JSON.parse(" +
           base::UTF8ToUTF16(base::GetQuotedJSONString(json)) + u"))");
}

void CommandBarWidget::PushIconScope() {
  Browser* browser = parent_->browser();
  if (!web_contents_ || !browser) {
    return;
  }
  const std::string scope = IconScopeFor(browser->profile());
  CallPage(u"window.atriumSetCommandBarIconScope && "
           u"window.atriumSetCommandBarIconScope(" +
           base::UTF8ToUTF16(base::GetQuotedJSONString(scope)) + u")");
}

void CommandBarWidget::SetPendingAnchor(const gfx::Rect& anchor_in_window) {
  pending_anchor_ = anchor_in_window;
}

void CommandBarWidget::ResetPage(
    const std::string& prefill,
    const std::optional<PalettePlacement>& placement) {
  // JSON.parse-grade quoting for the same reason SetMatchesJSON uses it: the
  // URL is page-controlled text and this runs at browser privilege. The
  // placement is four ints this process computed, so it needs none.
  std::u16string call =
      u"window.atriumResetCommandBar && window.atriumResetCommandBar(" +
      base::UTF8ToUTF16(base::GetQuotedJSONString(prefill));
  if (placement) {
    call += u", {\"left\":" + base::NumberToString16(placement->left) +
            u",\"top\":" + base::NumberToString16(placement->top) +
            u",\"width\":" + base::NumberToString16(placement->width) +
            u",\"height\":" + base::NumberToString16(placement->height) +
            u"}";
  }
  CallPage(call + u")");
}

std::string CommandBarWidget::TakePrefillForOpenReason(bool* opens_new_tab) {
  *opens_new_tab = false;
  BrowserController* controller =
      BrowserController::ControllerForBrowser(parent_->browser());
  if (!controller) {
    return std::string();
  }
  const CommandBarOpenReason reason =
      controller->TakePendingCommandBarOpenReason();
  *opens_new_tab = reason == CommandBarOpenReason::kNewTab;
  if (reason != CommandBarOpenReason::kEditCurrentURL &&
      reason != CommandBarOpenReason::kEditAtAnchor) {
    return std::string();
  }
  // The displayed contents rather than browser_'s active tab, for the reason
  // CopyCurrentPageLink names: a window presenting another profile's tab must
  // edit the URL that is on screen.
  content::WebContents* contents = parent_->CurrentlyDisplayedWebContents();
  if (!contents) {
    return std::string();
  }
  const GURL url = contents->GetVisibleURL();
  // An invalid or empty URL leaves the field empty, which is a Cmd+T open -
  // there is nothing to edit on a blank tab.
  return url.is_valid() ? url.spec() : std::string();
}

void CommandBarWidget::ApplyParentBounds() {
  // Full parent rect, origin included - see this method's header comment
  // for why these are screen coordinates and why (0,0,w,h) was wrong.
  widget_->SetBounds(parent_->widget()->GetWindowBoundsInScreen());
}

void CommandBarWidget::Show(bool is_startup_open) {
  BuildContents();
  // The document keeps its tint across opens, but a space switch while the
  // bar was closed changed it. Every open re-pushes, like the catalog does.
  parent_->PushOverlayTheme(web_contents_.get());

  // Cover the parent's whole window (Q3: the scrim must dim all of it, not
  // just the content card) - re-measured on every Show() since the parent
  // may have moved or resized since the last open, and re-applied by
  // OnWidgetBoundsChanged while visible if it moves again.
  ApplyParentBounds();

  // After ApplyParentBounds, which is where the parent's current size is
  // known. The anchor is consumed here whether or not a page takes it.
  std::optional<PalettePlacement> placement;
  if (!pending_anchor_.IsEmpty()) {
    placement = PlaceAnchoredPalette(
        pending_anchor_, parent_->widget()->GetWindowBoundsInScreen().size(),
        kAnchoredPaletteMinWidth);
    pending_anchor_ = gfx::Rect();
    VLOG(1) << "Atrium: command bar anchored at " << placement->left << ","
            << placement->top << " w" << placement->width;
  }

  // Clear the field and the last pass BEFORE the first visible frame, so a
  // palette reopened after an accept does not come back holding the old query.
  bool opens_new_tab = false;
  const std::string prefill = TakePrefillForOpenReason(&opens_new_tab);
  // Before the first visible frame, so the sidebar never shows the old row
  // selected under an open bar. Close() clears it on every path.
  parent_->PushCommandBarNewTabPending(opens_new_tab);
  // Held only when the page cannot take it: the first Show() runs before the
  // document exists and CallPage drops it, so commandBarReady replays it.
  pending_prefill_ = renderer_ready_ ? std::string() : prefill;
  pending_placement_ = renderer_ready_ ? std::nullopt : placement;
  ResetPage(prefill, placement);
  // After the reset, which clears the list but not the catalog. Dropped on the
  // first Show() like the reset is, and replayed from commandBarReady.
  PushActions();
  PushIconScope();

  // Before the guard is armed: the release also ends the palette's own guard.
  parent_->ReleaseStartupHold();
  shown_at_ = base::TimeTicks::Now();
  startup_guard_active_ = is_startup_open;
  intended_visible_ = true;
  VLOG(1) << "Atrium: command bar widget showing";
  widget_->Show();
  // Show() alone does not reliably hand this activatable child
  // window key status (the query field can only become first responder once
  // its window is key) - explicitly Activate(), same as the reclaim below
  // does after a bounce. Redundant, not harmful, if Show() already took key.
  widget_->Activate();

  // And hand the keyboard to the page. A key window is not enough: the widget
  // owns a views focus manager, and until the WebView is the focused view
  // every keystroke goes to the widget's root instead of the document. The
  // palette is then visible, activated, and completely deaf - which is exactly
  // what the first VM run of command-bar measured, with the page
  // loaded and neither typing nor Escape reaching it.
  FocusPage();
}

void CommandBarWidget::FocusPage() {
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
  // Taking key is not safe inline - see MaybeReclaimKey for why the decision
  // waits a runloop turn.
  base::SequencedTaskRunner::GetCurrentDefault()->PostTask(
      FROM_HERE, base::BindOnce(&CommandBarWidget::MaybeReclaimKey,
                                weak_factory_.GetWeakPtr(),
                                activation_loss_epoch_));
}

void CommandBarWidget::MaybeReclaimKey(int epoch) {
  // Nil-safe both ways: a parent with no NSWindow, or key held by another app
  // (nil keyWindow), both answer false and decline the reclaim.
  NSWindow* parent_window =
      parent_->widget()->GetNativeWindow().GetNativeNSWindow();
  const bool key_is_parent =
      parent_window != nil &&
      KeyWindowIsInGroup(widget_->GetNativeWindow().GetNativeNSWindow(),
                         parent_window);
  if (!ShouldReclaimKey(intended_visible_ && widget_->IsVisible(),
                        epoch == activation_loss_epoch_, widget_->IsActive(),
                        NSApp.active, key_is_parent)) {
    VLOG(1) << "Atrium: command bar declined to reclaim key";
    return;
  }
  // Only the launch window presenting itself over the palette gets undone.
  // Widget::Activate() is [NSApp activateIgnoringOtherApps:YES], so every
  // other case above has to decline rather than haul the app forward.
  VLOG(1) << "Atrium: command bar reclaiming key from the parent window";
  widget_->Activate();
  if (web_view_) {
    web_view_->RequestFocus();
  }
  if (web_contents_) {
    web_contents_->Focus();
  }
}

void CommandBarWidget::EndFocusGuard() {
  shown_at_ = base::TimeTicks();
  startup_guard_active_ = false;
}

void CommandBarWidget::Close() {
  // Mark the close as deliberate BEFORE Hide() so the deactivation Hide()
  // triggers is not misread as a spurious focus bounce and fought with a
  // reentrant widget_->Activate() (freeze on rapid Cmd+T toggling; toggle
  // desync otherwise) - see intended_visible_ and OnWidgetActivationChanged.
  intended_visible_ = false;
  // Stop the warm AutocompleteController on EVERY close path, not only the
  // ones that come back through commandBarDismiss - this is the one path (the
  // deactivation-guarded auto-close below, and any other caller of Close())
  // that would otherwise never reach the bridge at all. Stop() is idempotent,
  // so a redundant call costs nothing.
  if (BrowserController* controller =
          BrowserController::ControllerForBrowser(parent_->browser())) {
    controller->StopCommandBar();
  }
  // unconditional, since a plain (non-anchored) open never hid the
  // row and this is then just a harmless no-op on the page side.
  parent_->PushAddressRowHidden(false);
  parent_->PushCommandBarNewTabPending(false);
  if (!widget_->IsVisible()) {
    return;
  }
  VLOG(1) << "Atrium: command bar widget closing";
  // Read before Hide() moves key. A bar closed by a click elsewhere has
  // already lost key, and the click decides where focus goes.
  NSWindow* bar_window = widget_->GetNativeWindow().GetNativeNSWindow();
  const bool hand_focus_back = bar_window && NSApp.keyWindow == bar_window;
  ReleaseChildWidgetFocus(widget_.get());
  widget_->Hide();
  if (!hand_focus_back) {
    return;
  }
  parent_->widget()->Activate();
  parent_->FocusDisplayedPage();
}

bool CommandBarWidget::IsShowing() const {
  return widget_ && widget_->IsVisible();
}

content::WebContents* CommandBarWidget::WebContentsForTesting() const {
  return web_contents_.get();
}

bool CommandBarWidget::PageHoldsKeyboardForTesting() const {
  return web_view_ && web_view_->HasFocus();
}

bool CommandBarWidget::IsActiveForTesting() const {
  return widget_->IsActive();
}

void CommandBarWidget::ClearPageFocusForTesting() {
  if (views::FocusManager* focus = widget_->GetFocusManager()) {
    focus->ClearFocus();
  }
}

void CommandBarWidget::RetryContentsForTesting() {
  OnChromeContextReady();
}

bool CommandBarWidget::FirstResponderIsPageForTesting() const {
  NSWindow* window = widget_->GetNativeWindow().GetNativeNSWindow();
  content::RenderWidgetHostView* rwhv =
      web_contents_ ? web_contents_->GetRenderWidgetHostView() : nullptr;
  if (!window || !rwhv) {
    return false;
  }
  NSView* page = rwhv->GetNativeView().GetNativeNSView();
  NSResponder* responder = [window firstResponder];
  if (!page || ![responder isKindOfClass:[NSView class]]) {
    return false;
  }
  // The responder is the RenderWidgetHostViewCocoa itself, so isDescendantOf:
  // covers both it and anything it nests.
  return [static_cast<NSView*>(responder) isDescendantOf:page];
}

bool CommandBarWidget::HasTransientUserActivation() const {
  content::RenderFrameHost* frame =
      web_contents_ ? web_contents_->GetPrimaryMainFrame() : nullptr;
  return frame && frame->HasTransientUserActivation();
}

void CommandBarWidget::OnSurfaceMessage(std::string_view name,
                                        const base::ListValue& args) {
  BrowserController* controller =
      BrowserController::ControllerForBrowser(parent_->browser());
  if (name == "commandBarReady") {
    renderer_ready_ = true;
    // The deferred build focused a WebContents with no render widget behind it
    // yet, so the content half of the hand-off had nothing to land on. This is
    // the first moment it does.
    FocusPage();
    // Before the branches below, each of which returns.
    PushActions();
    PushIconScope();
    // Covers the first open: Show()'s push landed before this document
    // committed. A reopen has no ready event, so Show()'s push covers that.
    parent_->PushOverlayTheme(web_contents_.get());
    // The open that built this document: its reset was dropped, and the page
    // asks for the URL query itself, so nothing else runs below. A placement
    // with no prefill is an anchored open on a blank tab, which still has to
    // land on its row.
    if (!pending_prefill_.empty() || pending_placement_) {
      ResetPage(pending_prefill_, pending_placement_);
      pending_prefill_.clear();
      pending_placement_.reset();
      return;
    }
    // A pass may have landed while the document was still loading.
    if (!pending_matches_json_.empty()) {
      SetMatchesJSON(pending_matches_json_);
      return;
    }
    // Nothing has landed, so ask for the empty query. An empty query matches
    // every favourite by design (FavoriteMatchesForQuery), and the palette used
    // to open blank and stay blank until a keystroke produced a finished
    // autocomplete pass - which on a fresh profile never came.
    if (controller) {
      controller->QueryCommandBar(std::string());
    }
    return;
  }
  if (name == "commandBarQuery" && args.size() >= 1 && args[0].is_string()) {
    if (controller) {
      controller->QueryCommandBar(args[0].GetString());
    }
    return;
  }
  if (name == "commandBarAccept" && args.size() >= 1 && args[0].is_int()) {
    // Open BEFORE closing, and not the other way around: Close() stops the
    // AutocompleteController with kClobbered, which resets its result - so a
    // close-first accept resolves the index against an empty list and opens
    // nothing at all.
    if (controller) {
      controller->AcceptCommandBarMatch(args[0].GetInt());
    }
    Close();
    return;
  }
  // A url, not an index: a favourite is not in the
  // AutocompleteResult, so there is no position to resolve. Routed to the same
  // verb a favourite tile click takes, so the palette and the sidebar cannot
  // open one differently.
  if (name == "commandBarFavoriteAccept" && args.size() >= 1 &&
      args[0].is_string()) {
    const std::string url = args[0].GetString();
    if (controller) {
      controller->AcceptCommandBarFavorite(url);
    }
    Close();
    return;
  }
  // A keyword rather than an index, for the reason above: Atrium builds the
  // engine rows, so none of them sits in the AutocompleteResult.
  if (name == "commandBarEngineAccept" && args.size() >= 1 &&
      args[0].is_string()) {
    if (controller) {
      controller->AcceptCommandBarEngine(args[0].GetString());
    }
    Close();
    return;
  }
  if (name == "commandBarDismiss") {
    Close();
    return;
  }
  if (name == "commandBarDeleteMatch" && args.size() >= 1 && args[0].is_int()) {
    // The renderer is not trusted to name a real match: DeleteMatch's own
    // range and SupportsDeletion checks are what make a stale index a logged
    // no-op rather than a crash.
    if (controller) {
      controller->DeleteCommandBarMatch(args[0].GetInt());
    }
    return;
  }
  // Anything else is a window verb the palette offers - Settings, Courtyard,
  // Overview, New Space. This WebContents has exactly one SurfaceHost and it is
  // this widget, so without the forward those messages arrived here and
  // stopped, and the palette's Settings action had never once run.
  //
  if (parent_) {
    parent_->OnSurfaceMessage(name, args);
  }
}

void CommandBarWidget::OnWidgetBoundsChanged(views::Widget* widget,
                                             const gfx::Rect& new_bounds) {
  // Only the PARENT's moves/resizes matter here; widget_'s own bounds
  // changes (including the ones this method itself causes) are ignored, so
  // this can never self-recurse.
  if (widget != parent_->widget()) {
    return;
  }
  // While hidden there is nothing to keep aligned - Show() re-derives the
  // bounds on the next open anyway.
  if (!widget_->IsVisible()) {
    return;
  }
  VLOG(2) << "Atrium: command bar follows parent to " << new_bounds.ToString();
  ApplyParentBounds();
}

void CommandBarWidget::OnWidgetActivationChanged(views::Widget* widget,
                                                 bool active) {
  // This observer also watches the parent widget (for bounds); the parent's
  // activation changes must not drive the palette's auto-close logic - the
  // parent losing key to the palette itself on every open would read as an
  // instant dismissal.
  if (widget != widget_.get()) {
    return;
  }
  if (active) {
    // Key-churn breadcrumb (m4-appearance-report): logs the GAINED half of
    // every activation transition, with a timestamp, so a real-machine
    // repro can show the full gain/lose cadence during ordinary mouse
    // interaction (mouse-down on the card, drag)
    // (`--vmodule=atrium_command_bar_widget=2`).
    VLOG(1) << "Atrium: command bar gained key at "
            << base::TimeTicks::Now().since_origin().InMilliseconds() << "ms";
    // Invalidates any MaybeReclaimKey still in flight - the palette got key
    // on its own, so that task must not fire a second Activate.
    ++activation_loss_epoch_;
    // Key alone leaves keystrokes at the widget's root view: the in-guard
    // reclaim below re-Activates but never restored this half, so a palette
    // that bounced during launch churn stayed deaf.
    if (intended_visible_) {
      FocusPage();
    }
    return;  // Only losing key is a candidate for auto-close.
  }
  // A deactivation caused by a deliberate Close()/Hide() (Cmd+T toggle,
  // Escape, accept) must not be fought: reclaiming key here would re-Activate
  // the widget reentrantly while Hide() is still on the stack (freeze on
  // rapid toggling) and leave visibility desynced (the toggle then "does
  // nothing"). The timing guard below cannot distinguish this from a spurious
  // Show()-driven bounce, so intent is tracked explicitly.
  if (!intended_visible_) {
    return;
  }
  const base::TimeDelta since_shown = base::TimeTicks::Now() - shown_at_;
  // M4 decided model item 2: the startup open uses the wider 3000ms
  // kStartupFocusBounceGuard, every other open the 250ms kFocusBounceGuard.
  const base::TimeDelta guard =
      startup_guard_active_ ? kStartupFocusBounceGuard : kFocusBounceGuard;
  // Breadcrumb (m4-lifecycle-fix): log the actual settling delta on EVERY key
  // loss, not just the guarded one. The "new-window palette self-closes"
  // report was never reproducible in the headless VM (key state always
  // settles well under the guard there, so the child keeps key and no
  // deactivation arrives at all). If it ever recurs on a real, loaded machine,
  // this line shows whether the deactivation crossed the active guard.
  VLOG(1) << "Atrium: command bar lost key at "
          << base::TimeTicks::Now().since_origin().InMilliseconds() << "ms, "
          << since_shown.InMilliseconds() << "ms after show (guard "
          << guard.InMilliseconds() << "ms)";
  if (since_shown < guard) {
    // Ignore-AND-RECLAIM: this is the widget's OWN Show()-driven activation
    // bounce, a startup-open's launch-time
    // window-server churn, or (M4 decided model item 1, VM-observed) a newly
    // created tab's WebContents requesting focus a beat after the palette
    // already took key - not a real user-driven focus loss. Not Close()-ing is
    // not enough on its own: it leaves the widget VISIBLE but no longer the
    // KEY window, so it looks interactive while silently dropping every
    // keystroke (confirmed empirically - the query field stayed unfocused for
    // the widget's whole remaining lifetime with no further
    // activation-changed notifications to retry on). Re-activate so the widget
    // is genuinely key again, not just un-hidden. Bounded by the same guard
    // window as the ignore itself, so a real contender for key status past the
    // guard still wins via the Close() below.
    widget_->Activate();
    return;
  }
  Close();
}

}  // namespace atrium
