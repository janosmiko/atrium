// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_edge_tint.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "cc/paint/paint_flags.h"
#include "cc/paint/paint_shader.h"
#include "third_party/skia/include/core/SkPath.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/canvas.h"
#include "ui/gfx/color_utils.h"
#include "ui/gfx/geometry/insets_f.h"
#include "ui/gfx/geometry/rect_conversions.h"
#include "ui/gfx/geometry/rect_f.h"
#include "ui/gfx/geometry/skia_conversions.h"
#include "ui/gfx/image/image_skia.h"

namespace atrium {

SkColor EdgeTint::For(Edge edge) const {
  switch (edge) {
    case Edge::kLeft:
      return left;
    case Edge::kRight:
      return right;
    case Edge::kTop:
      return top;
    case Edge::kBottom:
      return bottom;
  }
}

const EdgeRun& EdgeTint::Run(Edge edge) const {
  switch (edge) {
    case Edge::kLeft:
      return left_run;
    case Edge::kRight:
      return right_run;
    case Edge::kTop:
      return top_run;
    case Edge::kBottom:
      return bottom_run;
  }
}

EdgeRun& EdgeTint::Run(Edge edge) {
  return const_cast<EdgeRun&>(std::as_const(*this).Run(edge));
}

EdgeRun EdgeTint::PaintRun(Edge edge) const {
  const EdgeRun& run = Run(edge);
  const bool empty = std::all_of(run.begin(), run.end(), [](SkColor c) {
    return c == SK_ColorTRANSPARENT;
  });
  if (!empty) {
    return run;
  }
  EdgeRun flat;
  flat.fill(For(edge));
  return flat;
}

void EdgeTint::SetEdge(Edge edge, SkColor color) {
  switch (edge) {
    case Edge::kLeft:
      left = color;
      break;
    case Edge::kRight:
      right = color;
      break;
    case Edge::kTop:
      top = color;
      break;
    case Edge::kBottom:
      bottom = color;
      break;
  }
  Run(edge).fill(color);
}

bool EdgeTint::IsUniform() const {
  for (Edge edge : {Edge::kLeft, Edge::kRight, Edge::kTop, Edge::kBottom}) {
    if (For(edge) != left) {
      return false;
    }
    for (SkColor c : PaintRun(edge)) {
      if (c != left) {
        return false;
      }
    }
  }
  return true;
}

EdgeTint UniformEdgeTint(SkColor color) {
  EdgeTint tint;
  for (Edge edge : {Edge::kLeft, Edge::kRight, Edge::kTop, Edge::kBottom}) {
    tint.SetEdge(edge, color);
  }
  return tint;
}

EdgeBand BandForEdge(const gfx::Rect& card, Edge edge, int depth, int radius) {
  const int reach = std::max(depth, 0);
  if (card.IsEmpty() || reach == 0) {
    return EdgeBand();
  }
  const int r = std::clamp(radius, 0, std::min(card.width(), card.height()) / 2);
  const int side_y = card.y() + r;
  const int side_h = card.height() - r * 2;
  const int run_x = card.x() + r;
  const int run_w = card.width() - r * 2;

  EdgeBand band;
  switch (edge) {
    case Edge::kLeft:
      band.rect = gfx::Rect(card.x() - reach, side_y, reach, side_h);
      band.from = gfx::Point(card.x(), card.y());
      band.to = gfx::Point(card.x() - reach, card.y());
      break;
    case Edge::kRight:
      band.rect = gfx::Rect(card.right(), side_y, reach, side_h);
      band.from = gfx::Point(card.right(), card.y());
      band.to = gfx::Point(card.right() + reach, card.y());
      break;
    case Edge::kTop:
      band.rect = gfx::Rect(run_x, card.y() - reach, run_w, reach);
      band.from = gfx::Point(card.x(), card.y());
      band.to = gfx::Point(card.x(), card.y() - reach);
      break;
    case Edge::kBottom:
      band.rect = gfx::Rect(run_x, card.bottom(), run_w, reach);
      band.from = gfx::Point(card.x(), card.bottom());
      band.to = gfx::Point(card.x(), card.bottom() + reach);
      break;
  }
  return band;
}

namespace {

// Weighted by chroma plus one: a thin red header outweighs the white under
// it, and the plus one keeps a grey page its own grey.
SkColor MeanOf(const SkBitmap& frame, int x0, int y0, int x1, int y1) {
  int64_t r = 0, g = 0, b = 0, n = 0;
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      const SkColor c = frame.getColor(x, y);
      const int cr = SkColorGetR(c), cg = SkColorGetG(c), cb = SkColorGetB(c);
      const int64_t w = std::max({cr, cg, cb}) - std::min({cr, cg, cb}) + 1;
      r += cr * w;
      g += cg * w;
      b += cb * w;
      n += w;
    }
  }
  if (n == 0) {
    return SK_ColorTRANSPARENT;
  }
  return SkColorSetARGB(SK_AlphaOPAQUE, static_cast<uint8_t>(r / n),
                        static_cast<uint8_t>(g / n),
                        static_cast<uint8_t>(b / n));
}

// The strip `x0..x1 x y0..y1` cut into kEdgeSegments along its long side.
EdgeRun RunOf(const SkBitmap& frame, int x0, int y0, int x1, int y1) {
  EdgeRun run;
  const bool along_y = (y1 - y0) >= (x1 - x0);
  const int length = along_y ? y1 - y0 : x1 - x0;
  for (int i = 0; i < kEdgeSegments; ++i) {
    const int a = length * i / kEdgeSegments;
    const int b = std::max(a + 1, length * (i + 1) / kEdgeSegments);
    run[i] = along_y ? MeanOf(frame, x0, y0 + a, x1, std::min(y1, y0 + b))
                     : MeanOf(frame, x0 + a, y0, std::min(x1, x0 + b), y1);
  }
  return run;
}

double SaturationOf(SkColor c) {
  color_utils::HSL hsl;
  color_utils::SkColorToHSL(c, &hsl);
  return hsl.s;
}

SkColor MixToward(SkColor from, SkColor to, double t) {
  const auto mix = [t](int a, int b) {
    return static_cast<uint8_t>(std::lround(a + (b - a) * t));
  };
  return SkColorSetARGB(SkColorGetA(from), mix(SkColorGetR(from), SkColorGetR(to)),
                        mix(SkColorGetG(from), SkColorGetG(to)),
                        mix(SkColorGetB(from), SkColorGetB(to)));
}

}  // namespace

EdgeTint BlendTowardBrand(const EdgeTint& edges, std::optional<SkColor> brand) {
  if (!brand) {
    return edges;
  }
  return edges.Mapped([&](SkColor edge) {
    return MixToward(edge, *brand, kBrandWeight * (1.0 - SaturationOf(edge)));
  });
}

std::optional<EdgeTint> EdgeTintFromFrame(const SkBitmap& frame) {
  const int w = frame.width();
  const int h = frame.height();
  if (frame.drawsNothing() || w < 4 || h < 4) {
    return std::nullopt;
  }
  const int strip = std::min({kEdgeStripDepth, w / 2, h / 2});
  EdgeTint tint;
  tint.left = MeanOf(frame, 0, 0, strip, h);
  tint.right = MeanOf(frame, w - strip, 0, w, h);
  tint.top = MeanOf(frame, 0, 0, w, strip);
  tint.bottom = MeanOf(frame, 0, h - strip, w, h);
  tint.left_run = RunOf(frame, 0, 0, strip, h);
  tint.right_run = RunOf(frame, w - strip, 0, w, h);
  tint.top_run = RunOf(frame, 0, 0, w, strip);
  tint.bottom_run = RunOf(frame, 0, h - strip, w, h);
  return tint;
}

namespace {

// The fade across a band's depth, as gradient stops from `start` to 1. The
// alpha goes as (1 - t) squared: a linear fade at 64 DIP read as a painted
// bar, this drops to a quarter by the middle and trails out.
constexpr int kFadeStops = 5;
struct Fade {
  std::array<SkColor4f, kFadeStops> colors;
  std::array<SkScalar, kFadeStops> pos;
};

Fade FadeOf(SkColor color, float start) {
  Fade fade;
  const SkColor4f full = SkColor4f::FromColor(color);
  for (int i = 0; i < kFadeStops; ++i) {
    const float t = static_cast<float>(i) / (kFadeStops - 1);
    fade.pos[i] = start + (1.0f - start) * t;
    fade.colors[i] = full;
    fade.colors[i].fA = full.fA * (1.0f - t) * (1.0f - t);
  }
  return fade;
}

void PaintCornerFan(gfx::Canvas* canvas,
                    const gfx::Point& center,
                    const gfx::Rect& quadrant,
                    SkColor color,
                    int radius,
                    int depth) {
  const float outer = static_cast<float>(radius + depth);
  const Fade fade = FadeOf(color, static_cast<float>(radius) / outer);
  cc::PaintFlags flags;
  flags.setShader(cc::PaintShader::MakeRadialGradient(
      gfx::PointToSkPoint(center), outer, fade.colors.data(), fade.pos.data(),
      kFadeStops, SkTileMode::kClamp));
  canvas->DrawRect(gfx::RectF(quadrant), flags);
}

// How many texels the fade across a band's depth gets. Flat slices along the
// edge showed as 12px stairs on a real window, so a band is one small bitmap
// scaled up with bilinear filtering: the GPU interpolates both ways per pixel.
constexpr int kFadeTexels = 16;

void PaintBand(gfx::Canvas* canvas, const EdgeBand& band, const EdgeRun& run) {
  const bool along_y = band.rect.height() >= band.rect.width();
  // Which side of the band the card sits on: the fade is full there.
  const bool card_at_max = along_y ? band.from.x() > band.to.x()
                                   : band.from.y() > band.to.y();
  // One texel of padding on every side, a copy of the texel beside it. The
  // canvas tiles the image with repeat, so a border sample would otherwise
  // blend the first segment with the last and the card side with the outer.
  const int across = kFadeTexels + 2;
  const int along = kEdgeSegments + 2;
  SkBitmap strip;
  strip.allocN32Pixels(along_y ? across : along, along_y ? along : across);
  const auto texel_at = [&](int i, int j) -> SkColor {
    const SkColor color = run[std::clamp(i, 0, kEdgeSegments - 1)];
    const int k = std::clamp(j, 0, kFadeTexels - 1);
    const float out = static_cast<float>(k) / (kFadeTexels - 1);
    const float t = card_at_max ? 1.0f - out : out;
    const float alpha = SkColorGetA(color) * (1.0f - t) * (1.0f - t);
    return SkPreMultiplyColor(
        SkColorSetA(color, static_cast<uint8_t>(std::lround(alpha))));
  };
  for (int i = -1; i <= kEdgeSegments; ++i) {
    for (int j = -1; j <= kFadeTexels; ++j) {
      const SkColor texel = texel_at(i, j);
      if (along_y) {
        *strip.getAddr32(j + 1, i + 1) = texel;
      } else {
        *strip.getAddr32(i + 1, j + 1) = texel;
      }
    }
  }
  canvas->DrawImageInt(gfx::ImageSkia::CreateFrom1xBitmap(strip), 1, 1,
                       strip.width() - 2, strip.height() - 2, band.rect.x(),
                       band.rect.y(), band.rect.width(), band.rect.height(),
                       /*filter=*/true);
}

}  // namespace

gfx::Rect ScaledAboutCenter(const gfx::Rect& card, float scale) {
  gfx::RectF scaled(card);
  scaled.Inset(gfx::InsetsF::VH(card.height() * (1.0f - scale) / 2,
                                card.width() * (1.0f - scale) / 2));
  return gfx::ToRoundedRect(scaled);
}

void PaintEdgeGlow(gfx::Canvas* canvas,
                   const gfx::Rect& card,
                   const EdgeTint& tint,
                   int depth,
                   int radius) {
  if (card.IsEmpty() || depth <= 0) {
    return;
  }
  const int r = std::clamp(radius, 0, std::min(card.width(), card.height()) / 2);
  canvas->Save();
  canvas->ClipPath(SkPath::RRect(gfx::RectToSkRect(card), r, r),
                   /*do_anti_alias=*/true, SkClipOp::kDifference);

  for (Edge edge : {Edge::kLeft, Edge::kRight, Edge::kTop, Edge::kBottom}) {
    const EdgeBand band = BandForEdge(card, edge, depth, r);
    if (band.rect.IsEmpty()) {
      continue;
    }
    PaintBand(canvas, band, tint.PaintRun(edge));
  }

  const EdgeRun left = tint.PaintRun(Edge::kLeft);
  const EdgeRun right = tint.PaintRun(Edge::kRight);
  const EdgeRun top = tint.PaintRun(Edge::kTop);
  const EdgeRun bottom = tint.PaintRun(Edge::kBottom);
  const int span = r + depth;
  const auto corner = [&](int cx, int cy, int qx, int qy, SkColor a,
                          SkColor b) {
    PaintCornerFan(canvas, gfx::Point(cx, cy), gfx::Rect(qx, qy, span, span),
                   MixToward(a, b, 0.5), r, depth);
  };
  corner(card.x() + r, card.y() + r, card.x() - depth, card.y() - depth,
         left.front(), top.front());
  corner(card.right() - r, card.y() + r, card.right() - r, card.y() - depth,
         right.front(), top.back());
  corner(card.x() + r, card.bottom() - r, card.x() - depth, card.bottom() - r,
         left.back(), bottom.front());
  corner(card.right() - r, card.bottom() - r, card.right() - r,
         card.bottom() - r, right.back(), bottom.back());
  canvas->Restore();
}

}  // namespace atrium
