// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_peek_widget.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_PEEK_WIDGET_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_PEEK_WIDGET_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/functional/callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/ui/atrium/atrium_web_surface_widget.h"
#include "content/public/browser/render_widget_host_view.h"
#include "content/public/browser/web_contents_delegate.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/controls/webview/unhandled_keyboard_event_handler.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget_observer.h"

namespace content {
class WebContents;
}  // namespace content

namespace views {
class Label;
class Throbber;
class WebView;
class Widget;
class WidgetDelegate;
}  // namespace views

class Profile;

namespace atrium {

class AtriumBrowserWindow;
class PeekWidget;
class PeekSnapshotView;

// Peek mode: the peek card's root view:
// nothing but the previewed page now - a views::WebView (plus the loading
// placeholder sharing its slot) filling the card edge to edge. It used to
// reserve a fixed-height top strip for the Swift title/URL/close/promote
// chrome; those controls moved OUT of the card into PeekControlsWidget
// below, so the card carries no chrome of its own.
class PeekRootView : public views::View {
 public:
  explicit PeekRootView(Profile* profile);
  ~PeekRootView() override;

  views::WebView* web_view() { return web_view_; }

  // Escape dismiss (widget-level accelerator only, no close button or
  // scrim-click). The accelerator is
  // registered in the constructor; PeekWidget supplies the callback.
  void SetEscapeCallback(base::RepeatingClosure callback);

  // The origin chip. A card carries no window frame, so without it a popup
  // a page opened reads as first-party UI - the one thing a real popup
  // window can never be mistaken for. `secure` drops the "Not secure" mark.
  void SetOriginChip(const std::u16string& host, bool secure);
  void ClearOriginChip();
  std::u16string OriginChipHostForTesting() const;

  // VM-observed: a fresh peek WebContents has no compositor frame
  // yet when the card's open animation starts, so the WebView paints its
  // default blank-white background for ~100-200ms before the real page
  // appears - a "flash of unstyled content",
  // confirmed in the VM. loading_placeholder_ occupies the SAME card slot as
  // web_view_ (FillLayout, AtriumRootView's web_view_/snapshot_view_
  // precedent) and is shown on every Show() (see PeekWidget::Show), hidden
  // once PeekWidget::HideLoadingPlaceholder is called (wired to the peek
  // WebContents' DidFirstVisuallyNonEmptyPaint - see PeekEventForwarder).
  void SetLoadingPlaceholderVisible(bool visible);
  bool LoadingPlaceholderVisibleForTesting() const;

  // A still of the page over the WebView for the pop-out: the live contents
  // leave for the tab strip while the card is still on screen. An empty
  // bitmap shows the card surface colour instead.
  void ShowPromoteSnapshot(const SkBitmap& bitmap, float device_scale_factor);
  void HidePromoteSnapshot();

  // views::View:
  bool AcceleratorPressed(const ui::Accelerator& accelerator) override;

  // The chip floats over the page rather than reflowing it, so the card is
  // still edge-to-edge content. AtriumPaneView's mirror badge does the same.
  void Layout(PassKey) override;

 private:
  raw_ptr<views::WebView> web_view_ = nullptr;
  raw_ptr<views::View> loading_placeholder_ = nullptr;
  raw_ptr<views::Throbber> loading_throbber_ = nullptr;
  raw_ptr<PeekSnapshotView> promote_snapshot_ = nullptr;
  raw_ptr<views::View> content_container_ = nullptr;
  raw_ptr<views::View> origin_chip_ = nullptr;
  raw_ptr<views::Label> origin_mark_label_ = nullptr;
  raw_ptr<views::Label> origin_host_label_ = nullptr;
  base::RepeatingClosure escape_callback_;
};

// Fixed in C++ rather than measured from the page, derived from the same
// numbers peek-controls.css sets for its border-box .ov-btn and .pkc column.
inline constexpr int kPeekControlButtonBoxSize = 28;
inline constexpr int kPeekControlsGap = 6;
inline constexpr int kPeekControlsPadding = 6;
inline constexpr gfx::Size kPeekControlsSize(
    kPeekControlButtonBoxSize + 2 * kPeekControlsPadding,
    kPeekControlButtonBoxSize * 2 + kPeekControlsGap +
        2 * kPeekControlsPadding);

// the peek card's control cluster (close and pop out), in
// its own frameless, translucent, NON-activatable child views::Widget
// floating just OUTSIDE the card's top-right corner, over the dimmed
// backdrop rather than over the previewed page.
//
// Outside is the whole point, and it is why this cannot live inside
// PeekRootView: the card's rounded corners come from a masksToBounds on the
// peek window's root content layer, so anything parented into the card is
// clipped to the card. A sibling window is the only way for the controls to
// sit beyond that edge.
//
// NOT activatable (Activatable::kNo, SetCanActivate(false), ShowInactive):
// the peek card dismisses itself on genuine key loss (see PeekWidget's
// focus-bounce guard), so a cluster that could take key would dismiss the
// very card its buttons act on, the moment you clicked one.
//
// Owned by PeekWidget, built once and reused for every open/close. Its
// content is the /peekctl WebUI document, which it owns and hosts in a
// views::WebView - the same shape PermissionSheetWidget uses.
class PeekControlsWidget : public AtriumWebSurfaceWidget {
 public:
  // `parent` and `owner` both outlive this object (PeekWidget owns it, and
  // the parent window outlives the peek widget).
  PeekControlsWidget(AtriumBrowserWindow* parent, PeekWidget* owner);
  ~PeekControlsWidget() override;

  PeekControlsWidget(const PeekControlsWidget&) = delete;
  PeekControlsWidget& operator=(const PeekControlsWidget&) = delete;

  // Pins the cluster just outside `card_bounds`' top-right corner (screen
  // coordinates, the same space PeekWidget::ApplyParentBounds works in) and
  // shows it inactive. Also the retry for a document the constructor could
  // not build: AtriumChromeContext arrives ~670ms into startup, well after
  // BrowserController builds this widget.
  void ShowForCard(const gfx::Rect& card_bounds);

  // Hides the cluster. Idempotent. Called whenever the card goes away -
  // including the non-animated promote path, so the controls never outlive
  // the card they belong to by even a frame.
  void Hide();

  views::Widget* widget() { return widget_.get(); }

  PeekWidget* owner_for_testing() { return owner_; }

  // Bound against the peek's owning controller, not resolved from
  // parent_->browser() - in single-window mode that can be the wrong window.
  void SetPromoteCallback(base::RepeatingClosure callback);

  // SurfaceHost: peekControlsReady and peekControl, the two names the
  // /peekctl route sends.
  void OnSurfaceMessage(std::string_view name,
                        const base::ListValue& args) override;

 private:
  // AtriumWebSurfaceWidget:
  bool RetriesOnMissingChromeContext() const override;

  const raw_ptr<AtriumBrowserWindow> parent_;
  const raw_ptr<PeekWidget> owner_;
  base::RepeatingClosure promote_callback_;
};

// Peek mode: a frameless, activatable, translucent child views::Widget
// showing a standalone previewed page - the C++ half of shift+click link
// preview. Modeled directly on CommandBarWidget
// (atrium_command_bar_widget.h) - parent-bounds tracking and the
// focus-bounce guard are copied wholesale, see that class's header doc for
// both - but NOT full-parent-bounds: a near-full card
// sized against the parent's web-content pane - see
// AtriumBrowserWindow::GetWebContentBoundsInScreen - inset by a small
// margin so the sidebar stays fully visible and the real page peeks out on
// all sides, rounded corners, and a real window drop shadow.
//
// One instance per AtriumBrowserWindow, built once at BrowserController
// construction (peek_widget_ member) and reused for every open/close - never
// rebuilt. Owns no content::WebContents itself - BrowserController owns the
// standalone peek WebContents and binds/unbinds it
// via SetWebContents.
class PeekWidget : public views::WidgetObserver,
                   public content::WebContentsDelegate {
 public:
  explicit PeekWidget(AtriumBrowserWindow* parent);
  ~PeekWidget() override;

  PeekWidget(const PeekWidget&) = delete;
  PeekWidget& operator=(const PeekWidget&) = delete;

  // Binds (or, with null, unbinds) `contents` into the card's WebView -
  // non-owning, same contract as views::WebView::SetWebContents itself.
  // Also binds this widget as `contents`' WebContentsDelegate (see
  // HandleKeyboardEvent's doc for why) - unbinds it from the outgoing
  // WebContents first, so a promoted tab never carries a stale delegate
  // pointer back to this widget once it moves into the real tab strip.
  void SetWebContents(content::WebContents* contents);

  // the WebContents currently bound into the card, or null. A
  // WebContents can only be attached to ONE views::WebView at a time -
  // ViewsHostableAttach overwrites the WebContents' views_host_ without
  // complaint, but ViewsHostableDetach DCHECKs it non-null, so whichever
  // holder detaches second aborts. AtriumBrowserWindow::ShowTab consults this
  // so a pane claims a tab exclusively - but the peek-promote overlap it was
  // written for was DISPROVED (PromotePeek detaches
  // first, and peek_contents_ is its own WebContents). Defence in depth
  // against a path never demonstrated; see AtriumBrowserWindow's
  // ReleasePeekClaim doc.
  content::WebContents* peeked_web_contents() const;

  // The control cluster beside the card, and its /peekctl document. Both
  // null-safe for a widget whose chrome context was not up yet.
  PeekControlsWidget* controls() { return controls_.get(); }
  content::WebContents* controls_web_contents();

  // Runs the dismiss callback below. Public for PeekControlsWidget: its close
  // button must reach the same one place that decides a dismiss happened.
  void RunDismissCallback();

  // Shows the widget, sized against the parent's web-content pane per this
  // class's header doc (re-measured on every Show(), and re-applied by
  // OnWidgetBoundsChanged while visible if the parent moves/resizes, or by
  // ReapplyBoundsForContentChange if the sidebar pane itself resizes), and
  // takes key focus (peek needs key so the user can scroll/click/type inside
  // the preview immediately - CommandBarWidget::Show() precedent). Animates
  // the card's layer in (scale up + opacity 0->1, ease-out) from a point
  // anchored at `click_origin_screen` - the cursor's screen position at the
  // moment the shift+click that triggered this Show() happened (captured in
  // atrium_peek_intercept.cc, the earliest atrium-owned code on that path)
  // - so the card visibly grows out of the clicked link instead of its own
  // center. Falls back to a plain center-anchored grow (the pre-Task-8
  // behavior) if `click_origin_screen` is unset - a defensive fallback, not
  // an expected path (MaybeOpenPeek always supplies one). The SAME point is
  // reused by Close()'s reverse animation, so open and dismiss zoom toward
  // the same spot.
  void Show(std::optional<gfx::Point> click_origin_screen = std::nullopt);

  // Hides the widget. Idempotent. Does NOT destroy the bound WebContents or
  // run the dismiss callback below - the caller (BrowserController::
  // ClosePeek) calls this AND separately resets its own peek_contents_.
  //
  // `animate`: true (default) plays the reverse open animation (scale down
  // + opacity 1->0, ease-in, anchored at the SAME point Show() grew from -
  // see that method's doc) and defers the actual views::Widget::Hide() until
  // it completes (see FinishHide) - used by every dismiss trigger (Escape,
  // click-out, close button). false hides instantly with no animation - used
  // only by PromotePeek: the card's content visually "becomes" the real tab
  // the instant control returns, so animating the card away here would
  // double-animate against the tab strip's own insert. Both paths fall
  // through the same widget_->Hide() call (FinishHide) - this parameter is
  // the ONE dismiss funnel, not a second
  // code path.
  //
  // `on_hidden` runs once FinishHide actually hides the widget (or right
  // away if it was already hidden) - BrowserController::ClosePeek uses it
  // to keep the outgoing WebContents alive until then, so the dismiss
  // animation shows the real page retreating instead of going blank the
  // instant this call returns.
  void Close(bool animate = true, base::OnceClosure on_hidden = base::OnceClosure());

  // Pop-out. Takes a still of the page, runs `insert_tab` (the caller moves
  // the WebContents into the tab strip, and must NOT close this widget),
  // then springs the card out to the pane over the new tab and hides itself.
  // The still is what the user watches grow: the live view re-embeds in the
  // pane meanwhile, which blanks for a few frames. Close() mid-grow wins.
  void Promote(base::OnceClosure insert_tab);

  bool IsShowing() const;

  // Unlike Show()/Close(), leaves the bound WebContents, placeholder and
  // origin chip untouched - a tab-switch toggle, not an open or a dismiss.
  void SetCardVisibleForActiveTab(bool visible);

  // Re-applies the card's bounds against the parent's CURRENT web-content
  // pane (ApplyParentBounds), with no animation - for a sidebar width change
  // while the peek is showing (AtriumBrowserWindow::StepSidebarResizeAnimation
  // - see GetWebContentBoundsInScreen's doc for why this needs to be pushed
  // explicitly rather than observed). A no-op if the widget isn't currently
  // showing.
  void ReapplyBoundsForContentChange();

  // Runs on every dismiss trigger this widget detects itself (Escape, and
  // the focus-bounce guard's genuine-loss-of-key branch) - NOT run by
  // Close() itself, so there is exactly one place that decides "a dismiss
  // happened" regardless of trigger. Set once by BrowserController before
  // the first Show(); its implementation (ClosePeek) calls Close() and
  // destroys peek_contents_.
  void SetDismissCallback(base::RepeatingClosure callback);

  // Whether a key loss right now would be swallowed as a side effect of a tab
  // this card opened - see kSelfOpenedTabGuard.
  bool SelfOpenedTabGuardArmedForTesting() const;
  // Counts the open springs Show() has started.
  int ShowGenerationForTesting() const { return show_generation_; }
  bool PromotingForTesting() const { return promoting_; }

  views::Widget* card_widget() { return widget_.get(); }

  // Hides the loading placeholder covering the WebView - see
  // PeekRootView::SetLoadingPlaceholderVisible's doc. Called by
  // BrowserController once the peek WebContents reports its first real
  // paint. A no-op if root_view_ has no placeholder to hide (already
  // hidden - idempotent, since a peek that navigates again in place, since
  // a peek never opens another peek, re-fires this harmlessly).
  void HideLoadingPlaceholder();
  bool LoadingPlaceholderVisibleForTesting() const;

  // content::WebContentsDelegate:
  //
  // Root fix for the widget-level Escape accelerator (PeekRootView::
  // AddAccelerator) never firing while the WebView's renderer has
  // focus. On Mac, keys destined for a WebContents' RenderWidgetHostView go
  // straight into the renderer process (remote_cocoa's BridgedContentView
  // keyDown: -> RenderWidgetHostViewMac), bypassing views::FocusManager
  // entirely - a views::View accelerator alone only fires for a widget whose
  // focused view routes unhandled keys back through FocusManager::OnKeyEvent.
  // The browser only gets a second look at a key the renderer did NOT
  // consume via WebContentsDelegate::HandleKeyboardEvent - and
  // content::WebContents::Create (OpenPeek) leaves that delegate null, so
  // WebContentsImpl::HandleKeyboardEvent's `delegate_ && ...` short-circuits
  // to false unconditionally, before the event ever reaches an accelerator
  // table. SetWebContents binds this widget as that delegate; this override
  // forwards the unhandled event into the SAME FocusManager the accelerator
  // above is registered with, via the stock
  // views::UnhandledKeyboardEventHandler - the exact pattern
  // ui/views/controls/webview/web_dialog_view.cc uses for the same problem.
  // PreHandleKeyboardEvent is deliberately NOT overridden: its default
  // (NOT_HANDLED) lets the page's own renderer-side handler see Escape
  // FIRST (e.g. preventDefault()), matching stock Chromium's
  // Escape-in-a-focused-field behavior instead of always stealing it.
  bool HandleKeyboardEvent(content::WebContents* source,
                           const input::NativeWebKeyboardEvent& event) override;

  // window.close() from inside the card. A sign-in popup closes itself once it
  // has reported back, and the card has to go with it rather than sit there
  // holding a dead page.
  void CloseContents(content::WebContents* source) override;

  // A nested window.open from the card. The default delegate drops
  // `new_contents` on the floor, so the page silently gets nothing.

  // Routed to the parent window's Browser for a window of its own. Adopting
  // it into this card would destroy the page that opened it, breaking the
  // opener chain the flow reports back on.
  content::WebContents* AddNewContents(
      content::WebContents* source,
      std::unique_ptr<content::WebContents> new_contents,
      const GURL& target_url,
      WindowOpenDisposition disposition,
      const blink::mojom::WindowFeatures& window_features,
      bool user_gesture,
      bool* was_blocked) override;

  // A same-tab link click. The peek holds no strip slot to be
  // CURRENT_TAB for, so that case opens a new foreground tab instead. Other
  // dispositions (Cmd/Cmd+Shift-click) pass through unchanged.
  content::WebContents* OpenURLFromTab(
      content::WebContents* source,
      const content::OpenURLParams& params,
      base::OnceCallback<void(content::NavigationHandle&)>
          navigation_handle_callback) override;

  // Forwards to PeekRootView - see SetOriginChip there.
  void SetOriginChip(const std::u16string& host, bool secure);
  void ClearOriginChip();
  std::u16string OriginChipHostForTesting() const;

  // views::WidgetObserver:
  //
  // Close-on-deactivate safety net, copied wholesale from CommandBarWidget's
  // OnWidgetActivationChanged - see that class's header doc for the risk
  // this guards against (taking key focus away from the parent's web content
  // the instant this widget shows can itself deliver a deactivation in the
  // same beat). Acts only on widget_'s own events.
  //
  // a genuine (past-guard) key loss no longer dismisses directly -
  // it must first be distinguished from an app switch (Finder, another app)
  // vs a within-Atrium focus move (parent window, another Atrium window).
  // See MaybeDismissAfterAppActivationLoss's doc for why that decision is
  // deferred a runloop turn instead of decided here synchronously. This is
  // also how a click on the sidebar or window chrome dismisses the peek -
  // both are within-Atrium focus moves, same as clicking the parent's tab
  // content would be, and get no dedicated click-catcher of their own.
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;

  // Continuous parent-bounds tracking, copied from CommandBarWidget's
  // OnWidgetBoundsChanged - re-centers the card whenever the parent window
  // moves or resizes while the card is visible. Acts only on the parent
  // widget's events.
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;

 private:
  // Centered, size-clamped rect in SCREEN coordinates - see
  // CommandBarWidget::ApplyParentBounds's comment for why this widget's
  // SetBounds takes screen coordinates (top-level widget, InitParams::child
  // is never set).
  void ApplyParentBounds();

  void OnEscapePressed();

  // runs one runloop turn after a genuine (past-kFocusBounceGuard)
  // key loss on widget_. On Mac, an app switch (e.g. Cmd+Tab to Finder)
  // delivers the outgoing key window's resignKeyWindow (-> this
  // OnWidgetActivationChanged deactivation) and the app's own
  // resignActiveNotification (-> NSApp.isActive flipping to NO)
  // independently, and the OS does not guarantee their relative order - so
  // reading NSApp.isActive synchronously inside OnWidgetActivationChanged
  // can observe a stale YES for an app switch that is, in fact, already
  // underway. Posting this check to the next iteration of the run loop lets
  // both notifications land first, so NSApp.isActive reflects the settled
  // state: still YES means the key loss was a within-Atrium focus move (the
  // primary click-away dismiss path, unaffected), NO means Atrium itself
  // deactivated and the peek must stay open (persists across app switches,
  // reappearing untouched when the user returns - the fix this task adds).
  //
  // `epoch`: review fix for a fast Cmd+Tab-away-and-back (both within the
  // SAME runloop turn this task is deferred across) - see
  // activation_loss_epoch_'s doc for why a captured epoch, checked here
  // against the current value, is required in addition to the
  // widget_->IsActive() check below: the epoch also covers the case where
  // the peek was re-Shown (not just reactivated) in the interim.
  //
  // Guarded by intended_visible_: the peek may already have been dismissed
  // or closed by something else (Escape, promote) in the interim, in which
  // case this is a no-op rather than an incorrect re-dismiss.
  void MaybeDismissAfterAppActivationLoss(int epoch);

  // Completes an animated Close(): the dismiss animation finished (or was
  // skipped, or animate=false) - actually hides the widget. Guarded by
  // intended_visible_: the no-stacking replace path (OpenPeek() calls
  // ClosePeek() then synchronously Show()s the new peek, all before the
  // outstanding dismiss CATransaction's completion block fires) can leave a
  // stale FinishHide queued for a widget that has since been re-shown; if
  // intended_visible_ is true when this runs, it belongs to a superseded
  // Close() and must not hide the widget that replaced it.
  void FinishHide();

  const raw_ptr<AtriumBrowserWindow> parent_;
  std::unique_ptr<views::WidgetDelegate> widget_delegate_;
  std::unique_ptr<views::Widget> widget_;
  // the control cluster floating outside the card's top-right
  // corner. Built once with this widget and kept for its lifetime; shown and
  // hidden in lockstep with the card so the buttons never outlive it.
  std::unique_ptr<PeekControlsWidget> controls_;
  raw_ptr<PeekRootView> root_view_ = nullptr;
  base::RepeatingClosure dismiss_callback_;

  // Close()'s `on_hidden`, stashed here so FinishHide (or Show(), for a
  // churned close it interrupts) can run it - see Close()'s doc.
  base::OnceClosure on_hide_complete_;

  // Promote()'s `insert_tab`, held until the still arrives. Set only while
  // promoting_.
  base::OnceClosure on_promote_insert_;
  bool promoting_ = false;

  // Promote()'s second half, once the still is in.
  void OnPromoteSnapshot(int generation,
                         float device_scale_factor,
                         const content::CopyFromSurfaceResult& result);

  // Focus-bounce guard state, copied wholesale from CommandBarWidget - see
  // its header doc for shown_at_/intended_visible_'s meaning. Close()'s
  // animation reuses intended_visible_ as the FinishHide staleness guard
  // above.
  base::TimeTicks shown_at_;
  bool intended_visible_ = false;

  // When a link on this card's own page last opened a tab - see
  // kSelfOpenedTabGuard.
  base::TimeTicks self_opened_tab_at_;

  // Staleness guard for MaybeDismissAfterAppActivationLoss's
  // deferred check, same removal-fires-completion-style hazard as
  // show_generation_ below but for a PostTask instead of a CATransaction
  // completion block. Bumped every time widget_ reclaims key (the active==true
  // branch of OnWidgetActivationChanged) - a fast Cmd+Tab-away-and-back can
  // land the reactivation BEFORE the deferred task the earlier deactivation
  // queued runs; without this, that queued task would still see
  // NSApp.active==YES and intended_visible_==YES and wrongly dismiss a peek
  // the user just brought back. MaybeDismissAfterAppActivationLoss captures
  // the current value when posted and no-ops if it no longer matches.
  int activation_loss_epoch_ = 0;

  // The click origin the current Show()
  // grew from, in screen coordinates - stashed so Close() can zoom back
  // toward the SAME point. Unset (nullopt) means "no origin supplied" (or
  // no peek shown yet), which both Show() and Close() treat as a
  // center-anchored grow/shrink.
  std::optional<gfx::Point> open_origin_screen_;

  // Staleness guard for the open animation's anchor-recenter completion
  // block (incremented by every Show()) - removing an animation early still
  // fires its completion, so a dismiss-during-open or open/close/open churn
  // can deliver a PRIOR Show()'s block after a newer call repositioned the
  // anchor; the block rejects itself when this no longer matches. See
  // Show()'s comment for the full hazard.
  int show_generation_ = 0;

  // Backs HandleKeyboardEvent above - stateful (tracks whether to swallow a
  // trailing Char event after a RawKeyDown was consumed as an accelerator),
  // same per-instance ownership as WebDialogView's identically-named member.
  views::UnhandledKeyboardEventHandler unhandled_keyboard_event_handler_;

  // Guards the dismiss animation's async CATransaction completion against
  // teardown (it may fire after this object is destroyed - window close,
  // rapid churn, etc). Chromium style: must be the last member.
  base::WeakPtrFactory<PeekWidget> weak_factory_{this};
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_PEEK_WIDGET_H_
