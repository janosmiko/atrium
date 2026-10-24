// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_quick_menu_widget.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_QUICK_MENU_WIDGET_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_QUICK_MENU_WIDGET_H_

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

// A press on the puzzle button makes the parent key before the renderer sees
// the click, so the close arrives as a deactivation and the quickMenuToggled
// that follows must not read as a fresh open.
inline constexpr base::TimeDelta kQuickMenuReopenGuard =
    base::Milliseconds(300);

// The quick menu's child window, hosting the /quickmenu route.
//
//   1. ACTIVATABLE. The menu takes key so Escape reaches it, and
//      OnWidgetActivationChanged(false) hides it - the same click-away rule
//      CommandBarWidget uses.
//   2. Anchored. Show(anchor_in_window) places the card under the puzzle
//      button rather than pinning it to a window corner.
//
// The extension popup never anchors here: AtriumBrowserWindow::
// ExtensionPopupAnchorView returns the surface view that owns the clicked
// button, and the menu closes before the popup opens.
//
// One instance per AtriumBrowserWindow, built on the first ToggleQuickMenu
// and reused for the window's lifetime.
class AtriumQuickMenuWidget : public views::WidgetObserver,
                              public content::WebContentsDelegate,
                              public AtriumWebSurfaceWidget {
 public:
  // `parent` outlives this object (owns it via AtriumBrowserWindow's
  // quick_menu_widget_ member).
  explicit AtriumQuickMenuWidget(AtriumBrowserWindow* parent);
  ~AtriumQuickMenuWidget() override;

  AtriumQuickMenuWidget(const AtriumQuickMenuWidget&) = delete;
  AtriumQuickMenuWidget& operator=(const AtriumQuickMenuWidget&) = delete;

  // Places the card under `anchor_in_window` (the puzzle button's rect in the
  // parent window's coordinates), shows it and takes key.
  void Show(const gfx::Rect& anchor_in_window);

  // Hides the widget. Idempotent.
  void Close();

  bool IsShowing() const;

  // When the deactivation path last closed the menu, so ToggleQuickMenu can
  // tell the close half of a button press from a fresh open. Null otherwise.
  base::TimeTicks last_closed_at() const { return last_closed_at_; }

  // Cached and replayed on quickMenuReady: the widget is given a snapshot
  // well before the renderer finishes loading.
  void SetActionsJSON(const std::string& json);
  void SetSiteJSON(const std::string& json);

  content::WebContents* web_contents() const { return web_contents_.get(); }

  // The button rect the open menu was anchored on, for the share picker and
  // the page info bubble. Empty while closed.
  gfx::Rect anchor_in_window() const { return anchor_in_window_; }

  // views::WidgetObserver: follows the parent's moves while visible, and
  // hides on losing key.
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;

  // content::WebContentsDelegate: the card's size arrives here rather than
  // being measured.
  void ResizeDueToAutoResize(content::WebContents* web_contents,
                             const gfx::Size& new_size) override;
  // Escape closes the menu. Answered here and not by a view accelerator: on
  // Mac the forwarder redispatches the native NSEvent, which a synthetic key
  // has none of, so an accelerator never sees it under test.
  bool HandleKeyboardEvent(content::WebContents* source,
                           const input::NativeWebKeyboardEvent& event) override;

  // SurfaceHost: chrome.send from the /quickmenu route.
  void OnSurfaceMessage(std::string_view name,
                        const base::ListValue& args) override;

 private:
  // AtriumWebSurfaceWidget:
  bool RetriesOnMissingChromeContext() const override;
  void BeforeAttachToContents() override;
  void OnRenderWidgetHostViewCreated(
      content::RenderWidgetHostView* rwhv) override;

  void ApplyBounds();
  void PushIconScope();
  void FocusPage();
  // Runs the share sheet for the active tab's URL, anchored on the button.
  void ShowSharePicker();

  gfx::Size content_size_ = kQuickMenuFallbackSize;
  gfx::Rect anchor_in_window_;
  std::string pending_actions_json_;
  std::string pending_site_json_;
  // Set by Close()/Show() so a deliberate hide is not fought by the
  // deactivation handler - CommandBarWidget's intended_visible_ rule.
  bool intended_visible_ = false;
  // When Show() last handed the widget key. A deactivation inside
  // kQuickMenuFocusBounceGuard of it is the open's own bounce, not a
  // click-away - see OnWidgetActivationChanged.
  base::TimeTicks shown_at_;
  // See last_closed_at().
  base::TimeTicks last_closed_at_;

  const raw_ptr<AtriumBrowserWindow> parent_;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_QUICK_MENU_WIDGET_H_
