// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_sidebar_overlay_widget.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_OVERLAY_WIDGET_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_OVERLAY_WIDGET_H_

// This header is included from both Objective-C++ (.mm) and plain C++ (.cc)
// translation units. NSView is only usable as a pointer type in the latter.
#if defined(__OBJC__)
#import <Cocoa/Cocoa.h>
@class AtriumOverlayTracker;
#else
class NSView;
#endif

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "base/timer/timer.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/views/widget/widget_observer.h"

namespace content {
class WebContents;
}

namespace views {
class View;
class WebView;
class Widget;
class WidgetDelegate;
}  // namespace views

namespace atrium {

class AtriumBrowserWindow;

// The hover-reveal overlay ("overfly", Arc/Zen-style).
// When the sidebar pane is collapsed (width 0), a proximity zone on the
// sidebar's docked edge (left by default, right if SetOnRight(true) was
// called - todo 45) reveals the sidebar as a floating panel over the web
// content, auto-hiding once the pointer moves away.
//
// The Swift SidebarOverlay.swift
// spec pins the same three rules under test:
//   * the reveal arms from kProximityRevealWidth away from the edge, not on
//     edge contact, and never with a mouse button already held (an in-flight
//     edge drag must keep the edge);
//   * dismissal reads the pointer's real position against a tolerance band
//     rather than trusting the bare mouseExited, so a few points of slip does
//     not dismiss - bounded by a grace cap so it can never stick open;
//   * a live resize drag suppresses the auto-hide entirely, because resizing
//     moves the overlay's OWN bounds out from under a stationary cursor.
//
// Built on the same primitive as CommandBarWidget - a frameless child
// views::Widget (a borderless child NSWindow on Mac, ordered above the
// parent's web content, which is why it composites ABOVE the
// RenderWidgetHostView the same way the command-bar palette does). Two
// deliberate differences from CommandBarWidget:
//   1. NOT activatable (Activatable::kNo, ShowInactive): revealing the
//      sidebar must not steal key focus from the web content - typing/clicks
//      in the page keep working while the overlay floats.
//   2. Sized to sidebar-width x full-window-height at the docked edge, not
//      the parent's full bounds (the command bar covers everything for its
//      scrim; this is just the sidebar strip).
//
// Owns the hover strip and mouse tracking entirely on the C++ side (the task
// contract: "C++ owns the strip + widget; Swift renders the sidebar content
// as-is inside it"). The hosted NSView is a SECOND sidebar instance for the
// same host (AtriumCreateSidebarForSlot called again) - the docked sidebar
// NSView is already parented into the browser window and an NSView has a
// single superview, so the overlay gets its own instance over the same shared
// Swift stores rather than re-hosting the docked one mid-session.
class AtriumSidebarOverlayWidget : public views::WidgetObserver {
 public:
  // `parent` outlives this object (owns it via AtriumBrowserWindow's
  // sidebar_overlay_widget_ member). `overlay_sidebar_view` is the second
  // sidebar NSView BrowserController already built and retains for this
  // window's lifetime (same non-owning-Attach contract as AttachSidebar /
  // the command bar).
  explicit AtriumSidebarOverlayWidget(AtriumBrowserWindow* parent);
  ~AtriumSidebarOverlayWidget() override;

  AtriumSidebarOverlayWidget(const AtriumSidebarOverlayWidget&) = delete;
  AtriumSidebarOverlayWidget& operator=(const AtriumSidebarOverlayWidget&) =
      delete;

  // Reveals the overlay at the LEADING edge (left, or right if on_right_ -
  // see SetOnRight), sized `width` x the parent's current content height.
  // Non-activating (ShowInactive) so the web content keeps key focus. A
  // no-op if `width` <= 0.
  void Show(int width);

  // Moves the sidebar's WebContents into the panel, or back out when null.
  // The panel and the docked pane are two views over ONE WebContents, so only
  // one of them holds it at a time.
  void HoldContents(content::WebContents* contents);

  // The backdrop under the page follows the window's tint and theme, which
  // change while the panel is up (a space switch from its own dots).
  void RepaintBackdrop();

  // The centre pixel of the backdrop the panel paints under the page.
  SkColor BackdropPixelForTesting() const;

  views::Widget* WidgetForTesting() const { return widget_.get(); }

  // Hides the overlay and cancels any pending hide. Idempotent.
  void Close();

  bool IsShowing() const;

  // Puts the panel away at once, with no slide. For the window commands the
  // page's own dots send: a hover panel has no business floating over a window
  // that is minimising, zooming or closing.
  void DismissForWindowCommand();

  // The panel's width on screen, or 0 while it is not up.
  int WidthForTesting() const;

  // The panel's screen bounds, empty while it is not up.
  gfx::Rect BoundsForTesting() const;

  // The radius the panel's own layer clips to, 0 while it is not up. Read
  // from the layer rather than recomputed, so a test can tell the shipped
  // curve apart from the one OverlayCornerRadius() would return.
  float CornerRadiusForTesting() const;

  // The panel's live sideways offset, in points. The frost travels the same
  // path, so the two must read the same at any frame.
  double SlideDxForTesting() const;

  // Called by AtriumBrowserWindow::SetSidebarWidth when the pane docks/expands
  // to a non-zero width (Cmd+S restore, or a live drag on the overlay's own
  // content): the sidebar is back in-window, so the overlay must go. Idempotent.
  void HideForDock();

  // Live-resizes the visible overlay to `width` (hover-overlay resize: the
  // Swift resize handle on the overlay instance drives this via the
  // set_sidebar_overlay_width op). A no-op while hidden.
  //
  // this is also the only signal that a resize drag is in flight -
  // the op carries no begin/end - so each call timestamps the drag (see
  // IsResizeDragActive). That matters because ApplyBounds recomputes the
  // overlay's x from the new width: the overlay slides out from under a
  // stationary cursor, fires mouseExited, and would otherwise hide itself
  // mid-drag.
  void ResizeTo(int width);

  // Sidebar on right: flips which window edge the overlay reveals
  // from. Re-applies strip tracking (and, while showing, bounds) so an
  // in-flight or already-visible overlay mirrors immediately.
  void SetOnRight(bool on_right);

  // views::WidgetObserver: follows the parent window's moves/resizes so the
  // overlay stays pinned to its docked edge at the right height while
  // visible (same continuous-tracking approach as CommandBarWidget).
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;

  // Internal mouse-tracking callbacks (invoked from the Objective-C tracking
  // helpers). Public only so the trackers' blocks can reach them; not part of
  // the logical interface.
  void OnStripEntered();
  void OnOverlayEntered();
  void OnOverlayExited();

 private:
  // Positions widget_ at the parent's left edge, full height, `width` wide,
  // in SCREEN coordinates (top-level widget - see CommandBarWidget's
  // ApplyParentBounds for why these are screen coords).
  void ApplyBounds(int width);

  // (Re)installs the docked-edge proximity tracking area on the parent
  // window's content view for the parent's current height.
  void UpdateStripTracking();

  // (Re)arms the post-exit dismissal check `delay` from now.
  void ArmHideCheck(base::TimeDelta delay);

  // hide_timer_ fired: decide whether the pointer has really left. Re-arms
  // itself while the answer is "not yet", so the decision never depends on a
  // second mouseExited that may never come.
  void HideAfterDebounce();

  // pointer tests against the overlay's own screen frame, and
  // against that frame grown by kStickyToleranceBand. Both read the live
  // cursor position rather than the stale exit event.
  bool IsPointerOverOverlay() const;
  bool IsPointerWithinToleranceBand() const;

  // true while an overlay resize drag is in flight - recent live
  // width pushes AND the primary button still held.
  bool IsResizeDragActive() const;

  // Completes an animated Close(): the fade-out finished (or was skipped) -
  // actually hides the widget and restores full alpha for the next reveal.
  void FinishHide();



  const raw_ptr<AtriumBrowserWindow> parent_;
  std::unique_ptr<views::WidgetDelegate> widget_delegate_;
  std::unique_ptr<views::Widget> widget_;
  raw_ptr<views::WebView> content_web_ = nullptr;
  // The WebView's parent, which paints the backdrop under it.
  raw_ptr<views::View> backdrop_ = nullptr;

  // Bumped by every Show and every Close. A CAAnimation completion carries the
  // value it was started with and does nothing if the panel has moved on since
  // - without it a dismissal that finished after the next reveal hid a panel
  // that had just been shown, and handed its contents back underneath it. That
  // is what killed twenty rapid cycles half the time.
  uint64_t cycle_ = 0;

  // See SetOnRight. False (left edge) is the stock default.
  bool on_right_ = false;

  // ~200ms debounce so a momentary mouse-out (or grazing the edge) doesn't
  // flicker the overlay away (see SidebarOverlayHideDebounce on the Swift
  // side, which pins the same value under test), then a short re-check
  // cadence for as long as HideAfterDebounce keeps answering "not yet".
  base::OneShotTimer hide_timer_;

  // when ResizeTo last pushed a live width. Null while no drag has
  // been seen since the current reveal.
  base::TimeTicks last_resize_push_;

  // start of the current run outside the overlay, for the sticky
  // grace cap. Refreshed for as long as the pointer still tests inside or a
  // resize drag is in flight, so the cap only measures genuine idle time
  // away; cleared on re-entry.
  base::TimeTicks exit_time_;

  // Objective-C tracking helpers (ARC strong refs). The strip tracker lives on
  // the parent content view (left edge); the overlay tracker lives on the
  // overlay's own content view (mouse-out to hide). Declared ObjC-typed (not
  // void*) so ARC manages them and the chromium-rawptr plugin skips them, the
  // same way sidebar_view_ is a plain ObjC pointer - this header's complete
  // type is only ever seen from Objective-C++ (.mm) TUs (browser_window.h
  // forward-declares this class and holds it via unique_ptr).
#if defined(__OBJC__)
  AtriumOverlayTracker* strip_tracker_ = nil;
  AtriumOverlayTracker* overlay_tracker_ = nil;
#endif

  // Guards the fade-out's async completion against teardown (the animation
  // completion may fire after this object is gone). Chromium style: must be
  // the last member.
  base::WeakPtrFactory<AtriumSidebarOverlayWidget> weak_factory_{this};
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_OVERLAY_WIDGET_H_
