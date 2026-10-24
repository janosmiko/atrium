// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_ROUNDED_HIT_TEST_VIEW_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_ROUNDED_HIT_TEST_VIEW_H_

#include <memory>

#include "ui/gfx/geometry/point.h"
#include "ui/gfx/native_ui_types.h"

namespace atrium {

// AppKit hit-tests a WebContents' NSView before Views's ViewTargeter runs, and
// that NSView stays a rectangle whatever radius the page draws, so a click in
// the floating bar's corner landed in the bar, not the page beneath it.
//
// Attach native_view() to the views::NativeViewHost that would otherwise hold
// the WebContents. views::WebView::holder() is that host.
class AtriumRoundedHitTestView {
 public:
  // `child` is the WebContents' native view. It becomes this view's only
  // subview and keeps filling it.
  explicit AtriumRoundedHitTestView(gfx::NativeView child);
  ~AtriumRoundedHitTestView();

  AtriumRoundedHitTestView(const AtriumRoundedHitTestView&) = delete;
  AtriumRoundedHitTestView& operator=(const AtriumRoundedHitTestView&) = delete;

  gfx::NativeView native_view() const;

  // The page owns the radius (overlay.css --ov-radius-card, or the
  // native-chrome variant), so it arrives with every devbar shadow push.
  // Clamped to half the shorter side the way CSS clamps a border radius.
  void SetCornerRadius(float radius);
  float corner_radius() const;

  // Runs the real -hitTest: for a point in this view's own bounds, top-left
  // origin. True when the point landed on the plate.
  bool HitTestForTesting(const gfx::Point& point) const;

 private:
  struct ObjCState;
  std::unique_ptr<ObjCState> objc_;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_ROUNDED_HIT_TEST_VIEW_H_
