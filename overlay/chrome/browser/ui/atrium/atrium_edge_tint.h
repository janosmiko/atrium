// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_EDGE_TINT_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_EDGE_TINT_H_

#include <array>
#include <optional>

#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

class SkBitmap;

namespace gfx {
class Canvas;
}  // namespace gfx

namespace atrium {

enum class Edge { kLeft, kRight, kTop, kBottom };

// How many colours an edge carries along its length. Twelve so a site header
// five percent tall lands in a segment of its own instead of a sixth of the
// page that is mostly white.
inline constexpr int kEdgeSegments = 12;
using EdgeRun = std::array<SkColor, kEdgeSegments>;

// A mean per edge for logs and the crossfade, and a run of kEdgeSegments per
// edge for the painter. A run left at zero paints its edge's mean flat.
struct EdgeTint {
  SkColor left = SK_ColorTRANSPARENT;
  SkColor right = SK_ColorTRANSPARENT;
  SkColor top = SK_ColorTRANSPARENT;
  SkColor bottom = SK_ColorTRANSPARENT;
  EdgeRun left_run{};
  EdgeRun right_run{};
  EdgeRun top_run{};
  EdgeRun bottom_run{};

  SkColor For(Edge edge) const;
  const EdgeRun& Run(Edge edge) const;
  EdgeRun& Run(Edge edge);
  // The run the painter draws: the edge's own run, or its mean repeated when
  // the run was never filled.
  EdgeRun PaintRun(Edge edge) const;
  // Sets the mean and fills the run with it.
  void SetEdge(Edge edge, SkColor color);
  // True when every edge, and every segment, carries the same colour.
  bool IsUniform() const;
  bool operator==(const EdgeTint& other) const = default;

  // `fn` applied to every colour, mean and segment alike.
  template <typename Fn>
  EdgeTint Mapped(Fn fn) const {
    EdgeTint out;
    for (Edge edge : {Edge::kLeft, Edge::kRight, Edge::kTop, Edge::kBottom}) {
      out.SetEdge(edge, fn(For(edge)));
      for (int i = 0; i < kEdgeSegments; ++i) {
        out.Run(edge)[i] = fn(Run(edge)[i]);
      }
    }
    return out;
  }

  // `fn(a, b)` applied colour by colour across two tints, for a crossfade.
  template <typename Fn>
  static EdgeTint Zipped(const EdgeTint& a, const EdgeTint& b, Fn fn) {
    EdgeTint out;
    for (Edge edge : {Edge::kLeft, Edge::kRight, Edge::kTop, Edge::kBottom}) {
      out.SetEdge(edge, fn(a.For(edge), b.For(edge)));
      for (int i = 0; i < kEdgeSegments; ++i) {
        out.Run(edge)[i] = fn(a.Run(edge)[i], b.Run(edge)[i]);
      }
    }
    return out;
  }
};

EdgeTint UniformEdgeTint(SkColor color);

// Where one edge's glow is painted and which way it fades.
struct EdgeBand {
  gfx::Rect rect;
  gfx::Point from;
  gfx::Point to;
};

// How far a glow reaches OUT from the page card, in DIP. 48 read as paint on
// the sidebar under a linear fade. With the ease-out fade the band can go to
// 64 and still read as light thrown on the chrome.
inline constexpr int kEdgeGlowDepth = 64;

// The glow's alpha at the card edge. Its own value, not a share of the glass
// alpha: at a 0.2 glass the share left 11%, which no eye could find.
inline constexpr double kEdgeGlowStrength = 0.55;

// How far a grey edge is pulled toward the page's brand colour. Scaled by how
// grey the edge is: a white YouTube page takes most of the red from its icon,
// a page that is already saturated is left alone.
inline constexpr double kBrandWeight = 0.6;

// `edges` mixed toward `brand`, each colour by kBrandWeight times its own
// greyness. No brand leaves `edges` unchanged.
EdgeTint BlendTowardBrand(const EdgeTint& edges, std::optional<SkColor> brand);

// The straight run of the glow outside one edge of `card`, between the corner
// tangents of a card with rounded corners of `radius`, fading away from it.
EdgeBand BandForEdge(const gfx::Rect& card, Edge edge, int depth, int radius);

// `card` as a layer scaled by `scale` about its centre shows it.
gfx::Rect ScaledAboutCenter(const gfx::Rect& card, float scale);

// Full alpha at the card's edge, no cap: every colour in `tint` came through
// the core's contrast walk already. Corners take the two segments meeting there.
void PaintEdgeGlow(gfx::Canvas* canvas,
                   const gfx::Rect& card,
                   const EdgeTint& tint,
                   int depth,
                   int radius);

// The square the GPU downscales a viewport to before the readback crosses back.
// Four edge runs is all that comes off it, so the cost of a tick is set here
// and nowhere else: 48x48 is 9KB, against megabytes for a native-size copy.
inline constexpr int kAmbilightSampleSize = 48;

// How wide a strip EdgeTintFromFrame samples along each edge, in texels of
// the downscaled frame. A sixth of the short side, so a header a few rows
// tall tints top without the white body below it washing it out.
inline constexpr int kEdgeStripDepth = kAmbilightSampleSize / 6;

// Each run reads a thin strip along its own edge, weighted by chroma.
// nullopt for a frame too small to have a strip on every edge (an empty
// surface copy).
std::optional<EdgeTint> EdgeTintFromFrame(const SkBitmap& frame);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_EDGE_TINT_H_
