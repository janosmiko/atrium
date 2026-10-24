// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_hint_overlay_widget.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_HINT_OVERLAY_WIDGET_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_HINT_OVERLAY_WIDGET_H_

#include <memory>
#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/atrium/atrium_web_surface_widget.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/views/widget/widget_observer.h"

namespace content {
class WebContents;
}  // namespace content

namespace views {
class Widget;
}  // namespace views

namespace atrium {

class AtriumBrowserWindow;

// The sidebar hover-hint's overlay window. The sidebar's own WebContents
// renders into a compositor surface clipped to its own view rect (the
// docked column, or the reveal panel's rect while it floats), so a hint
// bubble positioned near a row's trailing edge cannot paint past that
// boundary into the content pane no matter what z-index or position:fixed
// hint.js sets - the clip happens below the page, at the render-widget
// surface itself. This widget gives the bubble a canvas of its own: a
// frameless, translucent, NON-activatable `views::Widget` sized to the
// WHOLE parent window, the same primitive CommandBarWidget's scrim uses,
// hosting the /hint WebUI route (hint.js/hint.css, unchanged) so the
// existing anchor-and-clamp math in hint.js's place() runs against the
// full window instead of the sidebar's own narrower viewport.
//
// NOT activatable: a hover hint must never steal key focus from the page or
// the sidebar underneath it.
//
// One instance per AtriumBrowserWindow, built lazily on the first hint and
// reused for the window's lifetime - never rebuilt.
class HintOverlayWidget : public views::WidgetObserver,
                          public AtriumWebSurfaceWidget {
 public:
  // `parent` outlives this object (owns it via AtriumBrowserWindow's
  // hint_overlay_widget_ member).
  explicit HintOverlayWidget(AtriumBrowserWindow* parent);
  ~HintOverlayWidget() override;

  HintOverlayWidget(const HintOverlayWidget&) = delete;
  HintOverlayWidget& operator=(const HintOverlayWidget&) = delete;

  // Shows `text`/`keys` anchored on `anchor_in_window` - the sidebar row's
  // own rect, already converted to the parent window's coordinate space via
  // AtriumBrowserWindow::SurfaceRectToWindow - for `ttl_ms`. A new call
  // replaces whatever hint is showing, the same single-bubble contract
  // hint.js's own showHint() keeps for the local (non-native) surfaces.
  void Show(const std::string& text,
            const std::string& keys,
            const gfx::Rect& anchor_in_window,
            int ttl_ms);

  // Hides the bubble early (before its ttl elapses). Idempotent.
  void Hide();

  bool IsShowingForTesting() const;
  views::Widget* widget() { return widget_.get(); }

  // views::WidgetObserver: follows the parent window's moves/resizes while
  // the bubble is up - the same continuous parent-bounds tracking
  // CommandBarWidget uses. Acts only on the parent widget's events.
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;

  // SurfaceHost: the /hint route's one message, its handshake.
  void OnSurfaceMessage(std::string_view name,
                        const base::ListValue& args) override;

 private:
  // Positions widget_ to exactly cover the parent window, in SCREEN
  // coordinates - same reasoning as CommandBarWidget::ApplyParentBounds.
  void ApplyParentBounds();

  // Delivers a Show() call the page missed (not ready yet, or landed before
  // BuildContents finished) once hintReady arrives.
  void PushPending();

  const raw_ptr<AtriumBrowserWindow> parent_;

  bool pending_show_ = false;
  std::string pending_text_;
  std::string pending_keys_;
  gfx::Rect pending_anchor_;
  int pending_ttl_ms_ = 0;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_HINT_OVERLAY_WIDGET_H_
