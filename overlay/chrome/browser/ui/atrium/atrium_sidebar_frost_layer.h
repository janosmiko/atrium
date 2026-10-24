// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_FROST_LAYER_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_FROST_LAYER_H_

#include <memory>

#include "base/time/time.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/native_ui_types.h"

namespace views {
class Widget;
}

namespace atrium {

// frosts the page under the floating sidebar panel, tracking the
// panel's live frame.
class AtriumSidebarFrostLayer {
 public:
  // `insert_below`, when non-null, is the shadow layer's own view - the
  // frost is inserted directly below it so a drop shadow always paints on
  // top instead of getting smeared by the blur.
  AtriumSidebarFrostLayer(views::Widget* widget, gfx::NativeView insert_below);
  ~AtriumSidebarFrostLayer();

  AtriumSidebarFrostLayer(const AtriumSidebarFrostLayer&) = delete;
  AtriumSidebarFrostLayer& operator=(const AtriumSidebarFrostLayer&) = delete;

  // Shows (or moves/resizes) the frost at `rect_in_screen`, masked to
  // `corner_radius` so it does not show past the panel's own rounded
  // corners.
  void SetFrame(const gfx::Rect& rect_in_screen, float corner_radius);

  // Slides the frost `slide_dx` points sideways over `duration`, the panel
  // content's own path. A no-op if already hidden.
  void AnimateHide(base::TimeDelta duration, double slide_dx);

  // Slides the frost in from `slide_dx` points sideways over `duration`.
  // `SetFrame` must run first. Strength never ramps: the panel does not fade
  // either, and a frost that does reads as a thinner panel.
  void AnimateShow(base::TimeDelta duration, double slide_dx);

  // Hides the frost immediately. Idempotent.
  void Hide();

  // Picks the appearance the material renders under. Left unset, HUDWindow
  // renders light whatever the space theme is, so a dark theme met a light
  // frost.
  void SetDarkBackdrop(bool dark);

  // Test-only: whether a slide is on the layer right now, held or running.
  bool sliding_for_testing() const;

  // Test-only: the frost's live sideways offset, in points.
  double slide_dx_for_testing() const;

  // Test-only: an NSVisualEffectView's appearance is not readable in a way a
  // test can assert on, so the decision is mirrored here.
  bool dark_backdrop_for_testing() const { return dark_backdrop_; }

 private:
  struct ObjCState;
  std::unique_ptr<ObjCState> objc_;
  bool dark_backdrop_ = false;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SIDEBAR_FROST_LAYER_H_
