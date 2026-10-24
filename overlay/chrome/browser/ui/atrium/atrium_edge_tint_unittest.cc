// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_edge_tint.h"

#include <algorithm>
#include <cstdlib>

#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkBitmap.h"
#include "ui/gfx/canvas.h"

namespace atrium {
namespace {

// A card sitting inside a window, with room on every side for a glow.
constexpr gfx::Rect kCard(300, 60, 600, 400);

TEST(AtriumEdgeTintTest, AUniformTintCarriesOneColourOnEveryEdge) {
  const EdgeTint tint = UniformEdgeTint(SK_ColorRED);
  EXPECT_TRUE(tint.IsUniform());
  for (Edge edge : {Edge::kLeft, Edge::kRight, Edge::kTop, Edge::kBottom}) {
    EXPECT_EQ(tint.For(edge), SK_ColorRED);
  }
}

// The whole point of the feature is that opposite sides can disagree, so the
// uniform test above must not be the only shape IsUniform ever sees.
TEST(AtriumEdgeTintTest, OneDifferentEdgeMakesATintNonUniform) {
  EdgeTint tint = UniformEdgeTint(SK_ColorRED);
  tint.SetEdge(Edge::kRight, SK_ColorBLUE);
  EXPECT_FALSE(tint.IsUniform());
  EXPECT_EQ(tint.For(Edge::kLeft), SK_ColorRED);
  EXPECT_EQ(tint.For(Edge::kRight), SK_ColorBLUE);
}

TEST(AtriumEdgeTintTest, EachBandSitsOutsideItsEdgeAndFadesAway) {
  const EdgeBand left = BandForEdge(kCard, Edge::kLeft, 40, 0);
  EXPECT_EQ(left.from.x(), 300);
  EXPECT_EQ(left.to.x(), 260);
  EXPECT_EQ(left.rect, gfx::Rect(260, 60, 40, 400));

  const EdgeBand right = BandForEdge(kCard, Edge::kRight, 40, 0);
  EXPECT_EQ(right.from.x(), 900);
  EXPECT_EQ(right.to.x(), 940);
  EXPECT_EQ(right.rect, gfx::Rect(900, 60, 40, 400));

  const EdgeBand top = BandForEdge(kCard, Edge::kTop, 40, 0);
  EXPECT_EQ(top.from.y(), 60);
  EXPECT_EQ(top.to.y(), 20);
  EXPECT_EQ(top.rect, gfx::Rect(300, 20, 600, 40));

  const EdgeBand bottom = BandForEdge(kCard, Edge::kBottom, 40, 0);
  EXPECT_EQ(bottom.from.y(), 460);
  EXPECT_EQ(bottom.to.y(), 500);
  EXPECT_EQ(bottom.rect, gfx::Rect(300, 460, 600, 40));
}

// A border around the page, not a wash over it: the page's own pixels are the
// one place the glow must never land.
TEST(AtriumEdgeTintTest, NoBandOverlapsTheCard) {
  for (Edge edge : {Edge::kLeft, Edge::kRight, Edge::kTop, Edge::kBottom}) {
    const EdgeBand band = BandForEdge(kCard, edge, kEdgeGlowDepth, 12);
    EXPECT_FALSE(band.rect.Intersects(kCard));
  }
}

// The straight runs stop where the corner arc begins. A band that overhung
// the corner held its colour at full strength above the card, which read as
// a square notch behind the rounded corner.
TEST(AtriumEdgeTintTest, TheStraightRunsStopAtTheCornerTangents) {
  const EdgeBand left = BandForEdge(kCard, Edge::kLeft, 40, 12);
  EXPECT_EQ(left.rect, gfx::Rect(260, 72, 40, 376));
  const EdgeBand top = BandForEdge(kCard, Edge::kTop, 40, 12);
  EXPECT_EQ(top.rect, gfx::Rect(312, 20, 576, 40));
}

TEST(AtriumEdgeTintTest, AnEmptyCardOrNoDepthYieldsEmptyBands) {
  EXPECT_TRUE(BandForEdge(gfx::Rect(), Edge::kLeft, kEdgeGlowDepth, 12).rect.IsEmpty());
  EXPECT_TRUE(BandForEdge(kCard, Edge::kLeft, 0, 12).rect.IsEmpty());
}

SkBitmap PaintOnto(const gfx::Rect& canvas_bounds,
                   const gfx::Rect& card,
                   const EdgeTint& tint,
                   int depth) {
  gfx::Canvas canvas(canvas_bounds.size(), 1.0f, /*is_opaque=*/true);
  canvas.FillRect(canvas_bounds, SK_ColorBLACK);
  PaintEdgeGlow(&canvas, card, tint, depth, 12);
  return canvas.GetBitmap();
}

constexpr gfx::Rect kSmallCanvas(0, 0, 400, 200);
constexpr gfx::Rect kSmallCard(100, 20, 200, 160);

// And the whole reason the feature is not one flat wash: the chrome on
// one side of the page must be able to carry a different colour from the other.
TEST(AtriumEdgeTintTest, OpposingEdgesPaintTheirOwnColours) {
  EdgeTint tint = UniformEdgeTint(SK_ColorBLACK);
  tint.SetEdge(Edge::kLeft, SK_ColorRED);
  tint.SetEdge(Edge::kRight, SK_ColorBLUE);

  const SkBitmap painted = PaintOnto(kSmallCanvas, kSmallCard, tint, 40);
  const SkColor at_left = painted.getColor(98, 100);
  const SkColor at_right = painted.getColor(301, 100);

  EXPECT_GT(SkColorGetR(at_left), SkColorGetB(at_left));
  EXPECT_GT(SkColorGetB(at_right), SkColorGetR(at_right));
  EXPECT_NE(at_left, at_right);
}

// The glow has to READ as a glow, brightest against the card and gone at the
// far side. A band that painted one flat block would pass the test above and
// still look nothing like an ambilight.
TEST(AtriumEdgeTintTest, AGlowFadesAwayFromTheCardInsteadOfFillingABlock) {
  EdgeTint tint = UniformEdgeTint(SK_ColorBLACK);
  tint.SetEdge(Edge::kLeft, SK_ColorRED);

  const SkBitmap painted = PaintOnto(kSmallCanvas, kSmallCard, tint, 40);
  const int near = SkColorGetR(painted.getColor(98, 100));
  const int mid = SkColorGetR(painted.getColor(80, 100));
  const int past = SkColorGetR(painted.getColor(40, 100));

  EXPECT_GT(near, mid);
  EXPECT_GT(mid, past);
  EXPECT_EQ(past, 0);
}

// A linear fade at 64px reads as a painted bar with a soft outer edge. The
// glow has to drop fast and then trail off: halfway out it holds well under
// half of its strength at the card.
TEST(AtriumEdgeTintTest, TheGlowDropsFastAndTrailsOff) {
  EdgeTint tint = UniformEdgeTint(SK_ColorBLACK);
  tint.SetEdge(Edge::kLeft, SK_ColorRED);

  const SkBitmap painted = PaintOnto(kSmallCanvas, kSmallCard, tint, 40);
  const int near = SkColorGetR(painted.getColor(99, 100));
  const int half = SkColorGetR(painted.getColor(80, 100));
  const int far = SkColorGetR(painted.getColor(65, 100));
  EXPECT_GT(near, 220);
  EXPECT_LT(half, near * 0.4);
  EXPECT_GT(half, 0);
  EXPECT_GT(far, 0);
}

TEST(AtriumEdgeTintTest, TheCardItselfIsNeverPainted) {
  const SkBitmap painted =
      PaintOnto(kSmallCanvas, kSmallCard, UniformEdgeTint(SK_ColorRED), 40);
  EXPECT_EQ(painted.getColor(200, 100), SK_ColorBLACK);
  // Inside the corner's bounding square but inside the arc too - the corner
  // fan overlaps this square by design, and the clip is what keeps it off.
  EXPECT_EQ(painted.getColor(108, 174), SK_ColorBLACK);
}

// The corner fan falls off with DISTANCE from the arc. The old overhang held
// the edge colour at full strength alongside the corner, so a point just past
// the card's foot read the same as one halfway down its side.
TEST(AtriumEdgeTintTest, TheGlowFallsOffAroundTheCorner) {
  EdgeTint tint = UniformEdgeTint(SK_ColorBLACK);
  tint.SetEdge(Edge::kLeft, SK_ColorRED);
  tint.SetEdge(Edge::kBottom, SK_ColorRED);
  const SkBitmap painted = PaintOnto(kSmallCanvas, kSmallCard, tint, 40);
  const int beside = SkColorGetR(painted.getColor(98, 100));
  const int past_the_foot = SkColorGetR(painted.getColor(98, 195));
  EXPECT_GT(beside, 200);
  EXPECT_LT(past_the_foot, beside - 50);
}

// A corner where red meets blue is lit purple, not red on one side of a seam
// and blue on the other.
TEST(AtriumEdgeTintTest, ACornerTakesBothOfItsNeighbours) {
  EdgeTint tint = UniformEdgeTint(SK_ColorBLACK);
  tint.SetEdge(Edge::kLeft, SK_ColorRED);
  tint.SetEdge(Edge::kBottom, SK_ColorBLUE);
  const SkBitmap painted = PaintOnto(kSmallCanvas, kSmallCard, tint, 40);
  const SkColor at_corner = painted.getColor(96, 184);
  EXPECT_GT(static_cast<int>(SkColorGetR(at_corner)), 40);
  EXPECT_GT(static_cast<int>(SkColorGetB(at_corner)), 40);
}

// Fading to a transparent BLACK rather than to the edge colour's own zero
// alpha darkens the middle of the band below the base it sits on.
TEST(AtriumEdgeTintTest, AGlowNeverPaintsDarkerThanWhatItSitsOn) {
  gfx::Canvas canvas(kSmallCanvas.size(), 1.0f, /*is_opaque=*/true);
  canvas.FillRect(kSmallCanvas, SkColorSetRGB(0x40, 0x40, 0x40));
  EdgeTint tint = UniformEdgeTint(SkColorSetRGB(0x40, 0x40, 0x40));
  tint.SetEdge(Edge::kLeft, SkColorSetRGB(0xE0, 0x50, 0x50));
  PaintEdgeGlow(&canvas, kSmallCard, tint, 40, 12);

  const SkBitmap painted = canvas.GetBitmap();
  for (int x = 60; x < 100; ++x) {
    const int red = SkColorGetR(painted.getColor(x, 100));
    EXPECT_GE(red, 0x40) << "darkened at x=" << x;
  }
}

SkBitmap FrameOf(int size) {
  SkBitmap frame;
  frame.allocN32Pixels(size, size);
  frame.eraseColor(SK_ColorBLACK);
  return frame;
}

void FillRegion(SkBitmap* frame, int x0, int y0, int x1, int y1, SkColor c) {
  for (int y = y0; y < y1; ++y) {
    for (int x = x0; x < x1; ++x) {
      *frame->getAddr32(x, y) = SkPreMultiplyColor(c);
    }
  }
}

TEST(AtriumEdgeTintTest, AFrameSplitDownTheMiddleGivesTwoDifferentSides) {
  SkBitmap frame = FrameOf(24);
  FillRegion(&frame, 0, 0, 12, 24, SkColorSetRGB(0xC0, 0x20, 0x20));
  FillRegion(&frame, 12, 0, 24, 24, SkColorSetRGB(0x20, 0x20, 0xC0));

  const std::optional<EdgeTint> tint = EdgeTintFromFrame(frame);
  ASSERT_TRUE(tint.has_value());
  EXPECT_EQ(tint->left, SkColorSetARGB(0xFF, 0xC0, 0x20, 0x20));
  EXPECT_EQ(tint->right, SkColorSetARGB(0xFF, 0x20, 0x20, 0xC0));
  EXPECT_FALSE(tint->IsUniform());
}

TEST(AtriumEdgeTintTest, AFrameSplitAcrossGivesTwoDifferentEnds) {
  SkBitmap frame = FrameOf(24);
  FillRegion(&frame, 0, 0, 24, 12, SkColorSetRGB(0x10, 0xB0, 0x30));
  FillRegion(&frame, 0, 12, 24, 24, SkColorSetRGB(0xB0, 0x10, 0x90));

  const std::optional<EdgeTint> tint = EdgeTintFromFrame(frame);
  ASSERT_TRUE(tint.has_value());
  EXPECT_EQ(tint->top, SkColorSetARGB(0xFF, 0x10, 0xB0, 0x30));
  EXPECT_EQ(tint->bottom, SkColorSetARGB(0xFF, 0xB0, 0x10, 0x90));
}

// A site header is a few percent of the viewport. A plain mean over the top
// segment drowns it in the white below, so the band reads pink where the
// page is red. Colour has to outweigh white.
TEST(AtriumEdgeTintTest, AWhitePageWithAThinRedHeaderReadsRedAtTheTop) {
  SkBitmap frame = FrameOf(kAmbilightSampleSize);
  FillRegion(&frame, 0, 0, kAmbilightSampleSize, kAmbilightSampleSize,
             SK_ColorWHITE);
  FillRegion(&frame, 0, 0, kAmbilightSampleSize, 2,
             SkColorSetRGB(0xC0, 0x20, 0x20));

  const std::optional<EdgeTint> tint = EdgeTintFromFrame(frame);
  ASSERT_TRUE(tint.has_value());
  for (SkColor top : {tint->left_run.front(), tint->right_run.front(),
                      tint->top_run.front()}) {
    EXPECT_GT(static_cast<int>(SkColorGetR(top)), 0xA0);
    EXPECT_LT(static_cast<int>(SkColorGetG(top)), 0x60);
  }
  EXPECT_EQ(tint->left_run.back(), SK_ColorWHITE);
}

// A patch that touches no edge's strip must not tint any edge: this is the
// telex.hu case, a coloured band that sits away from every side.
TEST(AtriumEdgeTintTest, AColourInTheMiddleOfThePageReachesNoEdge) {
  SkBitmap frame = FrameOf(kAmbilightSampleSize);
  FillRegion(&frame, 0, 0, kAmbilightSampleSize, kAmbilightSampleSize,
             SK_ColorWHITE);
  FillRegion(&frame, 10, 20, 30, 28, SkColorSetRGB(0x80, 0x20, 0xC0));

  const std::optional<EdgeTint> tint = EdgeTintFromFrame(frame);
  ASSERT_TRUE(tint.has_value());
  for (Edge edge : {Edge::kLeft, Edge::kRight, Edge::kTop, Edge::kBottom}) {
    EXPECT_EQ(tint->For(edge), SK_ColorWHITE);
    for (SkColor c : tint->Run(edge)) {
      EXPECT_EQ(c, SK_ColorWHITE);
    }
  }
}

// A header along the top edge should darken top without pulling the sides
// down with it.
TEST(AtriumEdgeTintTest, ADarkTopStripeLeavesTheSidesNearWhite) {
  SkBitmap frame = FrameOf(kAmbilightSampleSize);
  frame.eraseColor(SK_ColorWHITE);
  FillRegion(&frame, 0, 0, kAmbilightSampleSize, 6, SK_ColorBLACK);

  const std::optional<EdgeTint> tint = EdgeTintFromFrame(frame);
  ASSERT_TRUE(tint.has_value());
  EXPECT_LT(static_cast<int>(SkColorGetR(tint->top)), 0x60);
  EXPECT_GT(static_cast<int>(SkColorGetR(tint->left)), 0xC0);
  EXPECT_GT(static_cast<int>(SkColorGetR(tint->right)), 0xC0);
  EXPECT_EQ(tint->bottom, SK_ColorWHITE);
}

// Mirror of the above along the left edge.
TEST(AtriumEdgeTintTest, ADarkLeftStripeLeavesTopAndBottomNearWhite) {
  SkBitmap frame = FrameOf(kAmbilightSampleSize);
  frame.eraseColor(SK_ColorWHITE);
  FillRegion(&frame, 0, 0, 6, kAmbilightSampleSize, SK_ColorBLACK);

  const std::optional<EdgeTint> tint = EdgeTintFromFrame(frame);
  ASSERT_TRUE(tint.has_value());
  EXPECT_LT(static_cast<int>(SkColorGetR(tint->left)), 0x60);
  EXPECT_GT(static_cast<int>(SkColorGetR(tint->top)), 0xC0);
  EXPECT_GT(static_cast<int>(SkColorGetR(tint->bottom)), 0xC0);
  EXPECT_EQ(tint->right, SK_ColorWHITE);
}

// Weighting by colour must not turn a grey page into nothing: with no colour
// anywhere the mean is the grey itself, which the brand blend then lifts.
TEST(AtriumEdgeTintTest, APageWithNoColourReadsItsOwnGrey) {
  SkBitmap frame = FrameOf(kAmbilightSampleSize);
  frame.eraseColor(SkColorSetRGB(0x10, 0x10, 0x10));
  const std::optional<EdgeTint> tint = EdgeTintFromFrame(frame);
  ASSERT_TRUE(tint.has_value());
  EXPECT_EQ(tint->left, SkColorSetARGB(0xFF, 0x10, 0x10, 0x10));
  EXPECT_TRUE(tint->IsUniform());
}

// A surface copy that failed comes back as a frame with no pixels, and a tint
// built from one would paint the chrome black.
TEST(AtriumEdgeTintTest, AFrameWithNothingInItYieldsNoTint) {
  EXPECT_FALSE(EdgeTintFromFrame(SkBitmap()).has_value());
  EXPECT_FALSE(EdgeTintFromFrame(FrameOf(2)).has_value());
}

// A page renders onto a transparent surface until it paints, and a mean taken
// over that is a colour the user never saw.
TEST(AtriumEdgeTintTest, EveryEdgeComesBackOpaque) {
  SkBitmap frame = FrameOf(24);
  frame.eraseColor(SkColorSetARGB(0x40, 0x20, 0x60, 0xA0));

  const std::optional<EdgeTint> tint = EdgeTintFromFrame(frame);
  ASSERT_TRUE(tint.has_value());
  for (Edge edge : {Edge::kLeft, Edge::kRight, Edge::kTop, Edge::kBottom}) {
    EXPECT_EQ(SkColorGetA(tint->For(edge)), SK_AlphaOPAQUE);
  }
}

// A white page with a red icon is the YouTube case: the edges say nothing and
// the icon says everything, so most of the red has to come through.
TEST(AtriumEdgeTintTest, AGreyEdgeTakesMostOfTheBrand) {
  const EdgeTint blended = BlendTowardBrand(
      UniformEdgeTint(SkColorSetRGB(0x10, 0x10, 0x10)),
      SkColorSetRGB(0xC0, 0x20, 0x20));
  EXPECT_EQ(blended.left, SkColorSetARGB(0xFF, 0x7A, 0x1A, 0x1A));
  EXPECT_TRUE(blended.IsUniform());
}

// A page that is already a colour must keep it. Pulling a saturated edge
// toward the brand would make every site glow like its favicon.
TEST(AtriumEdgeTintTest, ASaturatedEdgeIsLeftAlone) {
  const SkColor teal = SkColorSetRGB(0x00, 0x80, 0x80);
  const EdgeTint blended =
      BlendTowardBrand(UniformEdgeTint(teal), SkColorSetRGB(0xC0, 0x20, 0x20));
  EXPECT_EQ(blended.left, teal);
}

TEST(AtriumEdgeTintTest, NoBrandLeavesTheEdgesUntouched) {
  EdgeTint edges = UniformEdgeTint(SK_ColorWHITE);
  edges.SetEdge(Edge::kRight, SK_ColorBLACK);
  const EdgeTint blended = BlendTowardBrand(edges, std::nullopt);
  EXPECT_EQ(blended.left, SK_ColorWHITE);
  EXPECT_EQ(blended.right, SK_ColorBLACK);
}

// Each edge is weighted by ITS OWN greyness, so a page grey on one side and
// coloured on the other ends up brand-tinted only where it was grey.
TEST(AtriumEdgeTintTest, TheBlendIsPerEdge) {
  EdgeTint edges = UniformEdgeTint(SkColorSetRGB(0x00, 0x80, 0x80));
  edges.SetEdge(Edge::kLeft, SK_ColorWHITE);
  const EdgeTint blended =
      BlendTowardBrand(edges, SkColorSetRGB(0xC0, 0x20, 0x20));
  EXPECT_NE(blended.left, SK_ColorWHITE);
  EXPECT_EQ(blended.right, SkColorSetRGB(0x00, 0x80, 0x80));
}

// --- colours run along an edge -------------------------------------

TEST(AtriumEdgeTintTest, AnEdgeSplitAlongItsLengthGivesARunOfTwoColours) {
  SkBitmap frame = FrameOf(24);
  FillRegion(&frame, 0, 0, 24, 12, SkColorSetRGB(0x20, 0x20, 0xC0));
  FillRegion(&frame, 0, 12, 24, 24, SkColorSetRGB(0xC0, 0x20, 0x20));

  const std::optional<EdgeTint> tint = EdgeTintFromFrame(frame);
  ASSERT_TRUE(tint.has_value());
  EXPECT_EQ(tint->left_run.front(), SkColorSetARGB(0xFF, 0x20, 0x20, 0xC0));
  EXPECT_EQ(tint->left_run.back(), SkColorSetARGB(0xFF, 0xC0, 0x20, 0x20));
  EXPECT_EQ(tint->right_run.front(), SkColorSetARGB(0xFF, 0x20, 0x20, 0xC0));
  EXPECT_EQ(tint->right_run.back(), SkColorSetARGB(0xFF, 0xC0, 0x20, 0x20));
  EXPECT_FALSE(tint->IsUniform());
}

TEST(AtriumEdgeTintTest, ATintSetEdgeByEdgePaintsItsRunFlat) {
  EdgeTint tint;
  tint.left = SK_ColorRED;
  for (SkColor c : tint.PaintRun(Edge::kLeft)) {
    EXPECT_EQ(c, SK_ColorRED);
  }
  EXPECT_EQ(tint.Run(Edge::kLeft).front(), SK_ColorTRANSPARENT);
}

TEST(AtriumEdgeTintTest, TheBandBlendsFromOneSegmentToTheNext) {
  EdgeTint tint = UniformEdgeTint(SK_ColorBLACK);
  tint.left_run.fill(SK_ColorBLUE);
  tint.left_run.front() = SK_ColorBLUE;
  tint.left_run.back() = SK_ColorRED;
  tint.left_run[kEdgeSegments - 2] = SK_ColorRED;

  const SkBitmap painted = PaintOnto(kSmallCanvas, kSmallCard, tint, 40);
  const SkColor near_top = painted.getColor(98, kSmallCard.y() + 12);
  const SkColor near_bottom = painted.getColor(98, kSmallCard.bottom() - 12);
  EXPECT_GT(SkColorGetB(near_top), SkColorGetR(near_top));
  EXPECT_GT(SkColorGetR(near_bottom), SkColorGetB(near_bottom));
}

// Flat slices along the edge showed as steps on a real window: twelve
// colours over a 600px card is a 12px stair at every seam. Neighbouring
// pixels along the band must differ by no more than a rounding step.
TEST(AtriumEdgeTintTest, TheBandFlowsAlongTheEdgeWithoutSteps) {
  EdgeTint tint = UniformEdgeTint(SK_ColorBLACK);
  for (int i = 0; i < kEdgeSegments; ++i) {
    const int red = 255 * i / (kEdgeSegments - 1);
    tint.left_run[i] = SkColorSetRGB(red, 0, 255 - red);
  }

  const SkBitmap painted = PaintOnto(kSmallCanvas, kSmallCard, tint, 40);
  int worst = 0;
  for (int y = kSmallCard.y() + 20; y < kSmallCard.bottom() - 20; ++y) {
    const int a = SkColorGetR(painted.getColor(98, y));
    const int b = SkColorGetR(painted.getColor(98, y + 1));
    worst = std::max(worst, std::abs(a - b));
  }
  EXPECT_LE(worst, 3);
  EXPECT_LT(SkColorGetR(painted.getColor(98, kSmallCard.y() + 20)),
            SkColorGetR(painted.getColor(98, kSmallCard.bottom() - 20)));
}

TEST(AtriumEdgeTintTest, MappedAndZippedReachEverySegment) {
  EdgeTint a = UniformEdgeTint(SkColorSetRGB(10, 10, 10));
  a.top_run.back() = SkColorSetRGB(50, 50, 50);
  const EdgeTint doubled = a.Mapped([](SkColor c) {
    return SkColorSetRGB(SkColorGetR(c) * 2, SkColorGetG(c) * 2,
                         SkColorGetB(c) * 2);
  });
  EXPECT_EQ(doubled.top_run.back(), SkColorSetRGB(100, 100, 100));
  EXPECT_EQ(doubled.left, SkColorSetRGB(20, 20, 20));
  const EdgeTint summed = EdgeTint::Zipped(a, doubled, [](SkColor x, SkColor y) {
    return SkColorSetRGB(SkColorGetR(x) + SkColorGetR(y), 0, 0);
  });
  EXPECT_EQ(static_cast<int>(SkColorGetR(summed.top_run.back())), 150);
  EXPECT_TRUE(a == a);
  EXPECT_FALSE(a == doubled);
}

// The glow hugs the card the page layer shows, not the slot it sits in: a
// page stepped back under a peek is the same card scaled about its centre.
TEST(AtriumEdgeTintTest, ScaledCardKeepsItsCentre) {
  const gfx::Rect scaled = ScaledAboutCenter(gfx::Rect(100, 50, 1000, 600), 0.9f);
  EXPECT_EQ(scaled, gfx::Rect(150, 80, 900, 540));
  EXPECT_EQ(ScaledAboutCenter(kCard, 1.0f), kCard);
}

}  // namespace
}  // namespace atrium
