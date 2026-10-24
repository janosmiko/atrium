// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_command_bar_widget.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_BAR_WIDGET_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_BAR_WIDGET_H_

#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/ui/atrium/atrium_command_bar_anchor.h"
#include "chrome/browser/ui/atrium/atrium_web_surface_widget.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget_observer.h"

namespace content {
class RenderWidgetHostView;
class WebContents;
}

namespace views {
class Widget;
}  // namespace views

namespace atrium {

class AtriumBrowserWindow;

// Whether a deferred key reclaim should still run. Pure, so the policy is
// testable without a window server: only the parent window taking key is ours
// to undo - a sheet or the settings window asked for it and needs to keep it.
inline bool ShouldReclaimKey(bool is_visible,
                             bool epoch_current,
                             bool is_active,
                             bool app_active,
                             bool key_is_parent) {
  return is_visible && epoch_current && !is_active && app_active &&
         key_is_parent;
}

// The command-bar palette's child window (M4, rebuilt as an HTML surface).
// A frameless, activatable, translucent `views::Widget` - on Mac
// this maps directly to a borderless child NSWindow ordered above the parent's
// web content (the same OS primitive the omnibox popup and bubbles use).
//
// It hosts the /commandbar WebUI route, which draws BOTH the dim scrim and the
// centered card; this widget is just a full-parent-bounds transparent surface,
// so there is no scrim or centering logic on the C++ side. That was true of the
// Swift palette it replaces as well, which is why the geometry here did not
// have to change with the surface.
//
// One instance per AtriumBrowserWindow, built on the first OpenCommandBar and
// reused for every later Show()/Close() - the WebContents keeps its document,
// so a reopen costs no page load. Show() clears the field through
// window.atriumResetCommandBar rather than reloading.
class CommandBarWidget : public views::WidgetObserver,
                         public AtriumWebSurfaceWidget {
 public:
  // `parent` outlives this object (owns it via AtriumBrowserWindow's
  // command_bar_widget_ member).
  explicit CommandBarWidget(AtriumBrowserWindow* parent);
  ~CommandBarWidget() override;

  CommandBarWidget(const CommandBarWidget&) = delete;
  CommandBarWidget& operator=(const CommandBarWidget&) = delete;

  // Shows the widget, resized to the parent's current window bounds (the scrim
  // must dim the whole page, not just the content card) and made key so the
  // query field can type.
  //
  // `is_startup_open`: true only for the auto-open on the process-launch
  // bootstrap path (M4 decided model item 2). VM-observed: the newly shown
  // parent window's OS-level key-activation can still be settling over 1s
  // after this call returns (process-launch window-server churn, distinct
  // from the sub-250ms window-to-window churn Cmd+N sees), which would
  // otherwise cross kFocusBounceGuard and auto-close the palette with no
  // user action at all. Widens the guard to kStartupFocusBounceGuard for
  // that one call only - every other trigger (Cmd+T, Cmd+N, the sidebar
  // button) keeps the tight 250ms guard.
  void Show(bool is_startup_open = false);

  // The rect the next Show() anchors on, in the parent window's coordinates.
  // Consumed by that Show(), so an anchor set for an open that never
  // happened cannot leak into the next ordinary one.
  void SetPendingAnchor(const gfx::Rect& anchor_in_window);

  // Stops the focus-bounce guard, so the palette no longer takes key back
  // from another overlay that opens over it.
  void EndFocusGuard();

  // Hides the widget (Escape/click-away from the page, or the
  // focus-bounce-guarded deactivation below). Idempotent.
  void Close();

  // Whether the palette is currently visible - drives the Cmd+T toggle
  // (AtriumBrowserWindow::ToggleCommandBar).
  bool IsShowing() const;

  // Whether the palette's own frame carries a recent user gesture. The capture
  // verbs it offers take no arguments, so nothing else tells a click apart
  // from a message a compromised renderer made up.
  bool HasTransientUserActivation() const;

  // Null until the first Show() builds the page.
  content::WebContents* WebContentsForTesting() const;

  // Whether the page holds the widget's keyboard focus - the property Show()'s
  // hand-off exists to establish, and the one the startup open used to end up
  // without.
  bool PageHoldsKeyboardForTesting() const;

  // Whether the palette's own widget currently holds key.
  bool IsActiveForTesting() const;

  // Drops that focus, standing in for the launch-time state the routes below
  // have to recover from.
  void ClearPageFocusForTesting();

  // Replays the deferred document build the startup open depends on.
  void RetryContentsForTesting();

  // Whether the page's NSView is the window's first responder.
  bool FirstResponderIsPageForTesting() const;

  // Hands the page a fresh result set (BuildCommandBarMatchesJSON's shape).
  // Retained until the renderer announces itself with commandBarReady, so a
  // pass that lands during the first page load is replayed rather than lost -
  // the same handshake lesson the sidebar encodes.
  void SetMatchesJSON(const std::string& json);

  // views::WidgetObserver: (this object observes TWO widgets - its own
  // child widget_ for activation and the PARENT's widget for bounds - so
  // both overrides filter on the `widget` argument.)
  //
  // Close-on-deactivate safety net: taking key focus away from the parent's
  // web content the instant this widget shows can itself deliver a
  // deactivation in the same beat, which would otherwise immediately
  // self-dismiss. Escape and scrim-click already
  // dismiss explicitly through commandBarDismiss; this only catches losing
  // key some OTHER way (e.g. Cmd+Tab away). Acts only on widget_'s own
  // events.
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;

  // Continuous parent-bounds tracking (m4-palettepos fix): re-applies the
  // full parent bounds whenever the PARENT window moves or resizes while
  // the palette is visible, so the palette follows a dragged/resized window
  // and snaps to the settled frame after process-launch placement churn.
  // Acts only on the parent widget's events.
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;

  // SurfaceHost: the five commandBar* messages the /commandbar route sends.
  void OnSurfaceMessage(std::string_view name,
                        const base::ListValue& args) override;

 private:
  // Positions widget_ to exactly cover the parent window, in SCREEN
  // coordinates - full rect, origin included. This widget is top-level
  // (InitParams::child is never set), so NativeWidgetMac::
  // ConvertBoundsToScreenIfNeeded skips its parent-relative translation and
  // SetBounds means screen coordinates: the previous (0,0,w,h) rect anchored
  // the palette to the DESKTOP origin, not the parent (m4-palettepos bug -
  // masked in the headless VM, whose parent window sits at the screen
  // origin; plainly visible on a desktop with a centered window).
  void ApplyParentBounds();

  // AtriumWebSurfaceWidget:
  void OnChromeContextReady() override;
  void OnWebContentsCreated() override;

  // Hands the keyboard to the page, if there is a page yet. Every route that
  // can leave the palette visible without it calls this.
  void FocusPage();

  // Deferred a runloop turn for the reason PeekWidget defers its own: macOS
  // orders the key loss and the app deactivation independently, so an inline
  // NSApp.active can read a stale YES mid app-switch.
  void MaybeReclaimKey(int epoch);

  // Puts the page back in its opening state, with `prefill` in the query field
  // (empty for an ordinary open).
  void ResetPage(const std::string& prefill,
                 const std::optional<PalettePlacement>& placement);

  // Hands the page its action rows and the shortcut each one is bound to.
  // Sent per open rather than per keystroke: the catalog only changes when a
  // binding does, and a rebind is in force by the next open.
  void PushActions();

  // Hands the page its profile's icon scope, sent alongside PushActions -
  // /commandbar is a separate WebContents and gets it no other way.
  void PushIconScope();

  // What the field opens on: the displayed tab's URL when the pending open
  // reason is kEditCurrentURL, empty otherwise. Consumes the reason latch, so
  // it must be called exactly once per Show(). `opens_new_tab` reports a
  // kNewTab reason, the one open that has no tab to select yet.
  std::string TakePrefillForOpenReason(bool* opens_new_tab);

  // A nested observer rather than another base class: WebContentsObserver
  // brings its own web_contents(), which would collide with this class's.
  class RendererGoneWatcher;

  const raw_ptr<AtriumBrowserWindow> parent_;
  // an anchored palette hides the sidebar's address row, and only
  // Close() puts it back. A dead renderer reaches no other close path.
  std::unique_ptr<RendererGoneWatcher> renderer_gone_watcher_;

  std::string pending_matches_json_;

  // A Cmd+L prefill whose Show() ran before the document existed, replayed on
  // commandBarReady. Empty once delivered, and on every ordinary open.
  std::string pending_prefill_;

  // Set by SetPendingAnchor, cleared by the Show() that uses it.
  gfx::Rect pending_anchor_;

  // A placement whose Show() ran before the document existed, replayed on
  // commandBarReady beside the prefill.
  std::optional<PalettePlacement> pending_placement_;

  // Set at the start of every Show(); OnWidgetActivationChanged ignores a
  // deactivation arriving within kFocusBounceGuard (or
  // kStartupFocusBounceGuard, see below) of it (see that method's comment) - a
  // real user-driven loss of focus arrives well after this window closes.
  base::TimeTicks shown_at_;

  // Set by the current Show() call; OnWidgetActivationChanged reads it to
  // pick which guard duration applies to the deactivation it just saw.
  bool startup_guard_active_ = false;

  // Bumped on every key gain, so a reclaim posted before that lands is dropped
  // instead of firing a second Activate. Show() needs no bump: taking key
  // bumps it via the gained-key branch, failing to leaves nothing stale.
  int activation_loss_epoch_ = 0;

  // Whether the palette is INTENDED to be visible: true from Show(), false
  // from Close(). The focus-bounce guard only reclaims key on a deactivation
  // when this is true - a deactivation caused by a deliberate Close()/Hide()
  // (Cmd+T toggle, Escape, accept) must NOT be fought, or the guard would
  // re-Activate() the widget reentrantly during Hide() (freeze on rapid
  // toggling) and desync visibility (the toggle appears to do nothing). The
  // timing check alone cannot tell a deliberate close from a spurious bounce.
  bool intended_visible_ = false;

  base::WeakPtrFactory<CommandBarWidget> weak_factory_{this};
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_COMMAND_BAR_WIDGET_H_
