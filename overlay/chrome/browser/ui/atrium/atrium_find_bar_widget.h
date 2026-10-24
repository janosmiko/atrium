// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_find_bar_widget.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_FIND_BAR_WIDGET_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_FIND_BAR_WIDGET_H_

#include <memory>
#include <string>
#include <string_view>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "chrome/browser/ui/atrium/atrium_web_surface_widget.h"
#include "ui/gfx/geometry/rect.h"
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
class FindSession;

// Built from CommandBarWidget: an activatable frameless child widget hosting
// a WebUI route, so the field can type the moment it opens. Sized like
// ToastOverlayWidget instead of covering the parent: an activatable
// full-window widget hit-tests the whole page and eats every click, and the
// hint's ignoresMouseEvents escape is closed to a surface with buttons in it.
class FindBarWidget : public views::WidgetObserver,
                      public views::ViewObserver,
                      public AtriumWebSurfaceWidget {
 public:
  // `parent` outlives this object (owns it via AtriumBrowserWindow's
  // find_bar_widget_ member).
  explicit FindBarWidget(AtriumBrowserWindow* parent);
  ~FindBarWidget() override;

  FindBarWidget(const FindBarWidget&) = delete;
  FindBarWidget& operator=(const FindBarWidget&) = delete;

  // Shows the bar over the tab the parent window is displaying, seeded with
  // that tab's last search. Reshowing an open bar reselects the field rather
  // than building a second widget.
  void Show();

  // Walks the matches, opening the bar first when it is closed - Chrome's
  // behaviour for Cmd+G with no bar up.
  void FindNext(bool forward);

  // Hides the bar and ends the session on the tab that holds the highlights.
  // Idempotent.
  void Close();

  bool IsShowing() const;

  // Ends the session on the tab that owns it before the bar hands focus to
  // whatever the window is showing now. A tab or space switch calls this.
  void EndSessionAndClose();

  // Whether the page holds the widget's keyboard focus - the property Show()'s
  // hand-off exists to establish.
  bool PageHoldsKeyboardForTesting() const;

  // Drops that focus, standing in for a click that takes key elsewhere.
  void ClearPageFocusForTesting();

  // Drives one findQueryChanged without a renderer to send it.
  void FindQueryChangedForTesting(const std::u16string& query);

  // Whether the live key window is this bar's own window or its parent's -
  // the check MaybeReclaimKey's decision to reclaim key turns on.
  bool KeyIsInOwnGroupForTesting() const;

  // views::WidgetObserver: the parent moving or resizing drags the bar with
  // it. Acts only on the parent widget's events.
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;

  // views::WidgetObserver. Only the gained-key half of CommandBarWidget's
  // handler: a find bar must not close when the user clicks the page, since
  // the highlights and the count are still there.
  void OnWidgetActivationChanged(views::Widget* widget, bool active) override;

  // views::ViewObserver, on the parent window's root view. A sidebar width or
  // side change reshapes the pane without moving the parent widget, so
  // OnWidgetBoundsChanged never fires for it.
  void OnViewLayoutInvalidated(views::View* observed_view) override;

  // SurfaceHost: the five find* messages the /find route sends.
  void OnSurfaceMessage(std::string_view name,
                        const base::ListValue& args) override;

 private:
  // Puts the widget in the top trailing corner of the content pane, at the
  // fixed bar size plus the ring's outset.
  void ApplyBarBounds();

  // Deferred off OnViewLayoutInvalidated: the pane's post-layout bounds are
  // not final until the layout pass that invalidation starts has run.
  void ReapplyBoundsForPaneLayout();

  // AtriumWebSurfaceWidget: retries BuildContents once the reserved chrome
  // context exists. A find bar cannot open before the browser has a tab, so
  // this is defence rather than the load-bearing path the palette needs.
  void OnChromeContextReady() override;

  // Hands the keyboard to the page, if there is a page yet. A key window is
  // not enough: until the WebView is the focused view every keystroke goes to
  // the widget root and the bar is visible, activated and deaf.
  void FocusPage();

  // Deferred a runloop turn, as CommandBarWidget's is: macOS orders the key
  // loss and the app deactivation independently, so an inline NSApp.active can
  // read a stale YES mid app-switch.
  void MaybeReclaimKey(int epoch);

  // Whether the live key window is this bar's own window or its parent's -
  // the parent-comparison MaybeReclaimKey needs, factored out so a test can
  // read it directly.
  bool KeyIsInOwnGroup() const;

  // The findQueryChanged body, shared with the testing entry point.
  void ApplyQueryChange(const std::u16string& query);

  // Puts `query` in the field and selects it.
  void ResetPage(const std::string& query);

  // Hands the page one result set from the session.
  void PushResult(int active, int total);

  // The tab the bar searches: the one on screen, which under a presenter swap
  // is not browser_'s active tab.
  content::WebContents* TargetContents() const;

  // Ends the session and logs it, so the VM scenario has one line to gate on.
  void StopSession();

  const raw_ptr<AtriumBrowserWindow> parent_;
  std::unique_ptr<FindSession> session_;

  // What the field holds, mirrored here so the walk verbs can reissue it
  // without a round trip to the page.
  std::u16string query_;

  std::string pending_reset_;
  bool has_pending_reset_ = false;
  std::string pending_result_;

  // Coalesces bursts of OnViewLayoutInvalidated into one posted re-apply.
  bool pane_layout_reapply_pending_ = false;

  // Whether the bar is INTENDED to be visible: true from Show(), false from
  // Close(). A reclaim posted before a deliberate close must not fight it.
  bool intended_visible_ = false;

  // Bumped on every key gain, so a reclaim posted before that lands is dropped
  // instead of firing a second Activate.
  int activation_loss_epoch_ = 0;

  base::WeakPtrFactory<FindBarWidget> weak_factory_{this};
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_FIND_BAR_WIDGET_H_
