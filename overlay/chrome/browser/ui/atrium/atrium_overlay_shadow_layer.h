// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_OVERLAY_SHADOW_LAYER_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_OVERLAY_SHADOW_LAYER_H_

#include <memory>
#include <string>

#include "base/time/time.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/vector2d.h"
#include "ui/gfx/native_ui_types.h"

namespace views {
class Widget;
}

namespace atrium {

// One floating surface's drop shadow, resolved from overlay.css - a C++
// constant cannot see --ov-shadow-tight's light, dark and native-chrome
// variants.
struct AtriumOverlaySurfaceShadow {
  float corner_radius = 0;
  float blur = 0;
  gfx::Vector2d offset;
  SkColor color = SK_ColorTRANSPARENT;
};

// A plain NSView on the window's contentView, ranked above Views paint by
// SubviewSorter (native_widget_ns_window_bridge.mm:273-307) - unlike a
// View's own OnPaint, so the shadow reaches the screen instead of sitting
// under the page (see AtriumEdgeRevealZone for the same shape).
class AtriumOverlayShadowLayer {
 public:
  explicit AtriumOverlayShadowLayer(views::Widget* widget);
  ~AtriumOverlayShadowLayer();

  AtriumOverlayShadowLayer(const AtriumOverlayShadowLayer&) = delete;
  AtriumOverlayShadowLayer& operator=(const AtriumOverlayShadowLayer&) =
      delete;

  // Paints (or replaces) one surface's shadow with the surface's own footprint
  // erased: this view outranks the surface's own NSView, so an unerased core
  // lands on the surface instead of around it.
  void Register(const std::string& surface_id,
               const gfx::Rect& rect_in_screen,
               const AtriumOverlaySurfaceShadow& shadow);

  // Stops painting a surface's shadow, e.g. when it hides.
  void Unregister(const std::string& surface_id);

  // Slides and fades a surface's shadow the way the surface's own CSS
  // transition moves it, so the two leave together. `shift` is y-down like
  // that transform. A zero `duration` snaps.
  void SetSurfaceAway(const std::string& surface_id,
                      bool away,
                      const gfx::Vector2d& shift,
                      base::TimeDelta duration);

  // Lets another unassociated NSView (the sidebar's frost) insert itself below
  // this one explicitly, rather than leaving the two to SubviewSorter's
  // unspecified tie-break for two views it ranks equally.
  gfx::NativeView view() const;

  // A shadow is converted to view coordinates once, at Register time, so a
  // widget that moves has to release its own. Nothing else can see whether it
  // did.
  size_t ShadowCountForTesting() const;

  // Whether this surface's shadow has ever been REGISTERED while away, which
  // SetSurfaceAway called after Register instead of before it cannot give.
  // Never cleared - see the field it reads.
  bool ShadowStartedAwayForTesting(const std::string& surface_id) const;

  // How many times this surface's layer was built from nothing. More than one
  // for a shadow that only moved means a fade in flight was dropped.
  size_t ShadowBuildCountForTesting(const std::string& surface_id) const;

 private:
  struct ObjCState;
  std::unique_ptr<ObjCState> objc_;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_OVERLAY_SHADOW_LAYER_H_
