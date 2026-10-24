// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_space_icon_picker_widget.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SPACE_ICON_PICKER_WIDGET_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SPACE_ICON_PICKER_WIDGET_H_

#include <memory>
#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/ui/atrium/atrium_quick_menu_anchor.h"
#include "chrome/browser/ui/atrium/atrium_web_surface_widget.h"
#include "content/public/browser/web_contents_delegate.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
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

// Reopening the same dot's picker while OnWidgetActivationChanged's close is
// still landing would read as a fresh open - the same reopen guard
// kQuickMenuReopenGuard gives ToggleQuickMenu.
inline constexpr base::TimeDelta kSpaceIconPickerReopenGuard =
    base::Milliseconds(300);

// The /spaceicon route's child window - native, not in-sidebar, because the
// sidebar can be as narrow as kMinSidebarWidth (160px). Modeled on
// AtriumQuickMenuWidget.
class AtriumSpaceIconPickerWidget : public views::WidgetObserver,
                                    public content::WebContentsDelegate,
                                    public AtriumWebSurfaceWidget {
 public:
  // `parent` outlives this object (owns it via AtriumBrowserWindow's
  // space_icon_picker_widget_ member).
  explicit AtriumSpaceIconPickerWidget(AtriumBrowserWindow* parent);
  ~AtriumSpaceIconPickerWidget() override;

  AtriumSpaceIconPickerWidget(const AtriumSpaceIconPickerWidget&) = delete;
  AtriumSpaceIconPickerWidget& operator=(const AtriumSpaceIconPickerWidget&) =
      delete;

  // Places the card next to `anchor_in_window` (the dot's rect in the parent
  // window's coordinates), shows it and takes key. `space_index`/`value` are
  // replayed to the page once it reports spaceIconPickerReady.
  void Show(int space_index, const std::string& value,
           const gfx::Rect& anchor_in_window);

  // Hides the widget. `return_focus` hands focus back to the parent window's
  // page and re-selects the dot (a deliberate close); false leaves focus
  // alone (a deactivation, or a stale-index close from a snapshot push).
  void Close(bool return_focus);

  bool IsShowing() const;

  base::TimeTicks last_closed_at() const { return last_closed_at_; }

  int space_index() const { return space_index_; }

  // The native space id at the position Show() opened on - stable across a
  // reorder, unlike space_index(). Empty when no core document is open.
  const std::string& space_id() const { return space_id_; }

  content::WebContents* web_contents() const { return web_contents_.get(); }

  // views::WidgetObserver: follows the parent's moves while visible, and
  // hides on losing key.
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;

  // content::WebContentsDelegate: the card's size arrives here rather than
  // being measured.
  void ResizeDueToAutoResize(content::WebContents* web_contents,
                             const gfx::Size& new_size) override;
  // Escape closes the picker. Answered here and not by a view accelerator:
  // on Mac the forwarder redispatches the native NSEvent, which a synthetic
  // key has none of, so an accelerator never sees it under test.
  bool HandleKeyboardEvent(content::WebContents* source,
                           const input::NativeWebKeyboardEvent& event) override;

  // SurfaceHost: chrome.send from the /spaceicon route.
  void OnSurfaceMessage(std::string_view name,
                        const base::ListValue& args) override;

 private:
  // AtriumWebSurfaceWidget:
  bool RetriesOnMissingChromeContext() const override;
  void BeforeAttachToContents() override;
  void OnRenderWidgetHostViewCreated(
      content::RenderWidgetHostView* rwhv) override;

  void ApplyBounds();
  void FocusPage();
  void ReplayOpenValue();

  gfx::Size content_size_ = kSpaceIconPickerFallbackSize;
  gfx::Rect anchor_in_window_;
  int space_index_ = -1;
  std::string space_id_;
  std::string pending_value_;
  // Set by Close()/Show() so a deliberate hide is not fought by the
  // deactivation handler - AtriumQuickMenuWidget's intended_visible_ rule.
  bool intended_visible_ = false;
  base::TimeTicks last_closed_at_;
  // For the "showing" VLOG: inside=the rect fit fully on screen,
  // full_size=width matched kSpaceIconPickerFallbackSize rather than being
  // squeezed to fit a narrow sidebar window.
  bool last_placement_inside_ = false;
  bool last_placement_full_size_ = false;

  const raw_ptr<AtriumBrowserWindow> parent_;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SPACE_ICON_PICKER_WIDGET_H_
