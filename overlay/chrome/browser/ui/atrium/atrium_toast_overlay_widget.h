// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_toast_overlay_widget.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_TOAST_OVERLAY_WIDGET_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_TOAST_OVERLAY_WIDGET_H_

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/atrium/atrium_web_surface_widget.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/views/view_observer.h"
#include "ui/views/widget/widget_observer.h"

namespace content {
class WebContents;
}  // namespace content

namespace views {
class View;
class Widget;
}  // namespace views

namespace atrium {

class AtriumBrowserWindow;

// Hosted in the sidebar's WebContents, a card's top/right resolved against that
// narrow column, so the confirmation landed in the sidebar's upper right rather
// than the window's - reported twice.
//
// Sized to the STACK, not the window, unlike HintOverlayWidget. Size is all
// that keeps the confirmation corner off the page, since its close button
// needs the mouse.
class ToastOverlayWidget : public views::WidgetObserver,
                           public views::ViewObserver,
                           public AtriumWebSurfaceWidget {
 public:
  // A hovered link's URL sits at the bottom of the window and a confirmation
  // at the top. Two instances rather than one surface spanning both: a widget
  // reaching both corners would cover the page between them.
  enum class Corner { kTopTrailing, kBottomLeading };

  ToastOverlayWidget(AtriumBrowserWindow* parent, Corner corner);
  ~ToastOverlayWidget() override;

  ToastOverlayWidget(const ToastOverlayWidget&) = delete;
  ToastOverlayWidget& operator=(const ToastOverlayWidget&) = delete;

  // Shows a confirmation card. `ok` false drops the tick. `icon_name`
  // overrides the ok/close default (icons.js names); `key` replaces an
  // already-showing card with the same key instead of stacking.
  void ShowConfirmation(const std::string& text, bool ok,
                       const std::string& icon_name = "",
                       const std::string& key = "");

  // The hovered link's URL. Unlike a confirmation this does not time out: it
  // stays while the pointer is on the link, retargets in place when the
  // pointer moves to another, and an empty string clears it.
  void ShowHoverUrl(const std::string& text);

  // Which modifiers are held, so the card can say where the link would open.
  // The pointer is over the page, which owns the keyboard, so this comes from
  // the native flagsChanged monitor rather than a keydown in the document.
  void SetNavModifiers(bool shift, bool cmd, bool option);

  bool IsShowingForTesting() const;
  // Reads the NSWindow's own flag, so a test cannot pass on a copy this class
  // kept while the window was never told.
  bool DeclinesMouseForTesting() const;
  // Screen rects, the same ones the shadow layer baked. A test reads them
  // against the widget's own bounds: a card that reported an offset measured
  // in a viewport the widget had not caught up to lands outside them.
  const std::vector<gfx::Rect>& RegisteredShadowRectsForTesting() const {
    return registered_shadow_rects_;
  }
  views::Widget* widget() { return widget_.get(); }

  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;

  // SurfaceHost: toastReady, and the stack's own size reports.
  void OnSurfaceMessage(std::string_view name,
                        const base::ListValue& args) override;

  // views::ViewObserver, on the parent window's root view. A sidebar width
  // or side change reshapes the pane without moving the parent widget, so
  // OnWidgetBoundsChanged above never fires for it - this is the
  // hover corner's own path back to a current position.
  void OnViewLayoutInvalidated(views::View* observed_view) override;

 private:
  // Puts the widget in the parent window's top corner at `stack_size_`, on the
  // side opposite the sidebar. Hides it when the stack reports empty.
  void ApplyStackBounds();

  // Which edge of the pane the widget hangs off. The page anchors the stack
  // inside the widget to the same side, so a card laid out against a viewport
  // one round trip stale still starts where it will end up.
  bool StackOnLeft() const;

  // Deferred off OnViewLayoutInvalidated: the pane's post-layout bounds
  // aren't final until the layout pass that invalidation starts has run.
  void ReapplyBoundsForPaneLayout();

  // The page cannot measure the web card: its own viewport is the stack. Sent
  // only on a change - a resend re-fits the label, and the size report a fit
  // produces is what brings this call back round.
  void PushPaneWidth();

  void PushPending();

  // `json` is a JSON array of card rects, in this document's own
  // coordinates - converted to screen coordinates once, here, since the
  // page cannot know where this widget sits on screen.
  void PushCardShadows(const std::string& json);
  // Unregisters every shadow slot this corner could hold, cheaper than
  // tracking which are currently live.
  void ClearCardShadows();
  std::string ShadowIdForIndex(size_t index) const;

  // Which card held each slot on the last push. The stack prepends, so a slot
  // changes hands, and an away shift or a fade left on it belongs to the card
  // that is gone rather than the one that took its place.
  std::vector<int> slot_card_ids_;

  const raw_ptr<AtriumBrowserWindow> parent_;

  gfx::Size stack_size_;
  // How much of stack_size_ is room for what a card paints outside its own box
  // (toast-stack.css's --ct-ring). The corner inset is measured to the CARD,
  // so the widget starts this much further out.
  int stack_outset_ = 0;
  // GetWindowBoundsInScreen() can still report an older frame mid-burst,
  // over remote_cocoa's async round trip - PushCardShadows reads this instead.
  gfx::Rect last_applied_bounds_;

  bool pending_show_ = false;
  std::string pending_text_;
  bool pending_ok_ = true;
  std::string pending_icon_;
  std::string pending_key_;
  bool pending_is_hover_ = false;
  const Corner corner_;

  // Coalesces bursts of OnViewLayoutInvalidated into one posted re-apply.
  bool pane_layout_reapply_pending_ = false;

  // Last width PushPaneWidth sent, so an unchanged one is not re-sent.
  int pushed_pane_width_ = 0;

  // What PushCardShadows last handed the shadow layer, in screen coordinates.
  std::vector<gfx::Rect> registered_shadow_rects_;

  // Replayed on toastReady: a widget built while a key was already down would
  // otherwise show no hint until the next flagsChanged.
  bool nav_shift_ = false;
  bool nav_cmd_ = false;
  bool nav_option_ = false;

  base::WeakPtrFactory<ToastOverlayWidget> weak_factory_{this};
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_TOAST_OVERLAY_WIDGET_H_
