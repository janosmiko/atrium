// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_theme_studio_widget.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_THEME_STUDIO_WIDGET_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_THEME_STUDIO_WIDGET_H_

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

// The theme studio in a frameless child widget above the sidebar and the
// page. Built from FindBarWidget: activatable so the sliders take the
// pointer and the keyboard, sized to its card rather than the parent so
// clicks outside it still reach the page. The page moves it: the header
// sends deltas and DragBy applies them, clamped into the parent frame.
class ThemeStudioWidget : public views::WidgetObserver,
                          public AtriumWebSurfaceWidget {
 public:
  // `parent` outlives this object (owns it via AtriumBrowserWindow's
  // theme_studio_widget_ member).
  explicit ThemeStudioWidget(AtriumBrowserWindow* parent);
  ~ThemeStudioWidget() override;

  ThemeStudioWidget(const ThemeStudioWidget&) = delete;
  ThemeStudioWidget& operator=(const ThemeStudioWidget&) = delete;

  // Builds the page on first use, pushes `payload_json` to it, places the
  // widget above `anchor_in_screen`, shows and activates it. Reshowing an
  // open widget re-pushes and re-places rather than building a second page.
  void Show(const std::string& payload_json,
            int rail_index,
            const gfx::Rect& anchor_in_screen);

  // Offsets the widget, then clamps it into the parent frame.
  void DragBy(int dx, int dy);

  // Hides the widget and hands focus back to the parent. Idempotent.
  void Close();

  bool IsShowing() const;

  // The rail index the open studio edits, or -1 when closed.
  int SpaceIndex() const;

  gfx::Rect BoundsInScreen() const;

  bool PageHoldsKeyboardForTesting() const;

  // views::WidgetObserver, on the parent widget: the studio keeps its offset
  // from the parent origin and re-clamps.
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;

  // SurfaceHost: the /theme route's three own messages, plus the two theme
  // edit messages it shares with the settings window.
  void OnSurfaceMessage(std::string_view name,
                        const base::ListValue& args) override;

 private:
  // AtriumWebSurfaceWidget:
  void OnChromeContextReady() override;

  void PushPayload();
  void FocusPage();

  raw_ptr<AtriumBrowserWindow> parent_;

  bool intended_visible_ = false;
  int rail_index_ = -1;
  // The last payload, kept so a page that is not ready yet, or a Show()
  // before the chrome context exists, still gets it.
  std::string payload_json_;
  bool payload_pending_ = false;
  gfx::Rect anchor_in_screen_;
  // The parent frame the current bounds were clamped against, so a parent
  // move keeps the offset rather than snapping back to the anchor.
  gfx::Rect last_parent_bounds_;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_THEME_STUDIO_WIDGET_H_
