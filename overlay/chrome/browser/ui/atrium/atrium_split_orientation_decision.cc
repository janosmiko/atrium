// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_split_orientation_decision.h"

namespace atrium {

ContentDropOrientation ChooseSplitOrientation(const gfx::Rect& content_bounds,
                                               const gfx::Point& drop_point) {
  if (content_bounds.width() == 0 || content_bounds.height() == 0) {
    return ContentDropOrientation::kSwap;
  }
  const double fx = static_cast<double>(drop_point.x() - content_bounds.x()) /
                     content_bounds.width();
  const double fy = static_cast<double>(drop_point.y() - content_bounds.y()) /
                     content_bounds.height();
  if (fx < 0.25 || fx > 0.75) {
    return ContentDropOrientation::kHorizontalSplit;
  }
  if (fy < 0.25 || fy > 0.75) {
    return ContentDropOrientation::kVerticalSplit;
  }
  return ContentDropOrientation::kSwap;
}

SplitEdge ResolveSplitEdge(const gfx::Rect& content_bounds,
                           const gfx::Point& drop_point) {
  if (content_bounds.width() == 0 || content_bounds.height() == 0) {
    return SplitEdge::kCenter;
  }
  const double fx = static_cast<double>(drop_point.x() - content_bounds.x()) /
                     content_bounds.width();
  const double fy = static_cast<double>(drop_point.y() - content_bounds.y()) /
                     content_bounds.height();
  if (fx < 0.25) {
    return SplitEdge::kLeft;
  }
  if (fx > 0.75) {
    return SplitEdge::kRight;
  }
  if (fy < 0.25) {
    return SplitEdge::kTop;
  }
  if (fy > 0.75) {
    return SplitEdge::kBottom;
  }
  return SplitEdge::kCenter;
}

}  // namespace atrium
