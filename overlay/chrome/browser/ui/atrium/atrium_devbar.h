// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_DEVBAR_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_DEVBAR_H_

#include <memory>
#include <string>
#include <string_view>

#include "base/callback_list.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/time/time.h"
#include "chrome/browser/ui/atrium/atrium_surface_host.h"
#include "content/public/browser/web_contents_observer.h"
#include "ui/gfx/geometry/vector2d.h"

namespace content {
class WebContents;
}

namespace views {
class View;
class WebView;
}  // namespace views

namespace atrium {

class AtriumBrowserWindow;
class AtriumRoundedHitTestView;
struct AtriumOverlaySurfaceShadow;

// One floating address bar. Bar 0 is the window's own, a split adds one per
// extra pane. Its own SurfaceHost, so the window knows which bar
// sent a message, and it watches its pane's tab, which need not be active.
class AtriumDevBar : public SurfaceHost, public content::WebContentsObserver {
 public:
  AtriumDevBar(AtriumBrowserWindow* window,
               views::View* container,
               size_t index);
  AtriumDevBar(const AtriumDevBar&) = delete;
  AtriumDevBar& operator=(const AtriumDevBar&) = delete;
  ~AtriumDevBar() override;

  // Builds the WebContents into the container once the chrome context is up.
  void Build();
  // Detach only. The window calls it while its widget still has an NSWindow.
  void Unwrap();

  content::WebContents* contents() const { return contents_.get(); }
  views::View* container() const { return container_; }
  size_t index() const { return index_; }
  const std::string& shadow_id() const { return shadow_id_; }

  // The pane tab this bar tracks, or null outside a split.
  void SetTab(content::WebContents* tab);

  void SetEntrancePending() { entrance_pending_ = true; }
  // Registers or drops this bar's shadow from the spec devbar.css pushed.
  void PushShadow(const AtriumOverlaySurfaceShadow* spec,
                  const gfx::Vector2d& away_shift,
                  base::TimeDelta away_duration);
  void CallScript(const std::u16string& script);

  // Pushes atriumSetDevBarFocused and records what was sent, so a testing
  // hook can read what actually reached this bar (RULE ZERO)
  // instead of recomputing the answer from pane state.
  void PushFocused(bool focused, const std::string& ring_hex);
  bool FocusedForTesting() const { return focused_for_testing_; }
  const std::string& FocusRingForTesting() const {
    return focus_ring_for_testing_;
  }

  // SurfaceHost:
  void OnSurfaceMessage(std::string_view name,
                        const base::ListValue& args) override;

  // content::WebContentsObserver:
  void DidFinishNavigation(content::NavigationHandle* handle) override;

 private:
  // Puts contents_ inside hit_test_view_, once `web_view` has a Widget.
  // Re-runs whenever the WebView re-attaches the page's own NSView.
  void Wrap(views::WebView* web_view);

  const raw_ptr<AtriumBrowserWindow> window_;
  raw_ptr<views::View> container_;
  const size_t index_;
  const std::string shadow_id_;
  std::unique_ptr<content::WebContents> contents_;
  // The NSView contents_ sits in, so a click in the plate's corner wedges
  // reaches the page instead of the bar.
  std::unique_ptr<AtriumRoundedHitTestView> hit_test_view_;
  raw_ptr<views::WebView> web_view_ = nullptr;
  base::CallbackListSubscription attached_subscription_;
  // The bar descends in its own document, one posted layout before the shadow
  // registers here, so a resting shadow sat over a bar that had not arrived.
  bool entrance_pending_ = false;
  // The last atriumSetDevBarFocused PushFocused sent, for FocusedForTesting.
  bool focused_for_testing_ = false;
  std::string focus_ring_for_testing_;
  base::WeakPtrFactory<AtriumDevBar> weak_factory_{this};
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_DEVBAR_H_
