// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_courtyard_widget.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_COURTYARD_WIDGET_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_COURTYARD_WIDGET_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "chrome/browser/ui/atrium/atrium_web_surface_widget.h"
#include "ui/gfx/native_ui_types.h"
#include "ui/views/widget/widget_observer.h"

namespace content {
class WebContents;
}

namespace views {
class Widget;
}  // namespace views

namespace atrium {

class AtriumBrowserWindow;

// The Courtyard's child window - a full-window overlay
// (docs/lab/swipe-lab.html:1772-1815), not a sidebar or content-pane panel.
//
// ACTIVATABLE: Escape needs real key focus to reach the page at all.
class CourtyardWidget : public views::WidgetObserver,
                        public AtriumWebSurfaceWidget {
 public:
  explicit CourtyardWidget(AtriumBrowserWindow* parent);
  ~CourtyardWidget() override;

  CourtyardWidget(const CourtyardWidget&) = delete;
  CourtyardWidget& operator=(const CourtyardWidget&) = delete;

  void Show();
  void Close();
  bool IsShowing() const;
  double Zoom() const { return zoom_; }

  // The Courtyard's own child NSWindow, for driving its CommandDispatcher
  // delegate directly in tests (the Cmd+Q regression).
  gfx::NativeWindow NativeWindowForTesting() const;

  // Called by AtriumBrowserWindow::PushSidebarSnapshot() whenever this
  // widget is showing, so a room's mini tab list stays live across any
  // sidebar gesture, not just ones the Courtyard itself made.
  void PushSnapshot();

  // hands one pinch sample to the plane. `delta_y` is the wheel
  // deltaY the AppKit event carried, so the monitor and the page agree on
  // which way is closer without a second unit.
  void ForwardPinch(double delta_y);

  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;

  // The single seam where the parent learns the Courtyard is up or down, so
  // every hide path - Escape, a card click, a window teardown - hands the
  // sidebar and the page back.
  void OnWidgetVisibilityChanged(views::Widget* widget, bool visible) override;

  // Also forwards the sidebar drop messages a canvas card drag reuses, whole,
  // to the browser window: this widget's own WebContents is what routes them
  // here, not the name.
  void OnSurfaceMessage(std::string_view name,
                        const base::ListValue& args) override;

 private:
  // AtriumWebSurfaceWidget:
  bool RetriesOnMissingChromeContext() const override;

  void ApplyParentBounds();
  void NotifyPageClosed();

  const raw_ptr<AtriumBrowserWindow> parent_;

  // The plane's own zoom, pushed by courtyardZoom. The pinch decision closes
  // only once this is at or above 1.0, defaulted here so a spread still
  // closes a Courtyard that never reported a zoom.
  double zoom_ = 1.0;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_COURTYARD_WIDGET_H_
