// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_SPLIT_TREE_LAYOUT_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_SPLIT_TREE_LAYOUT_H_

#include <algorithm>
#include <functional>
#include <vector>

#include "base/numerics/safe_conversions.h"
#include "chrome/browser/ui/atrium/atrium_split_tree.h"
#include "ui/gfx/geometry/rect.h"

namespace atrium {

// Child index at each level from the root - identifies one internal branch
// node (and therefore one gutter) across a single layout pass. The root
// branch, if any, is the empty path.
using SplitNodePath = std::vector<size_t>;

// Mirrors AtriumPaneContainerView::ClampRatio's upstream multi_contents_view
// .cc ClampToMinWidth/GetMinViewWidth recipe - shared here so the recursive
// layout applies the SAME per-split minimum at every branch, not a new rule.
struct SplitLayoutParams {
  int divider_extent = 0;
  int min_pane_extent = 0;
  int constrained_min_pane_extent = 0;
  double min_pane_extent_percentage = 0.0;
};

// Pure extraction of AtriumPaneContainerView::ClampRatio - identical math,
// reusable by both the container (one 2-way split) and this file's
// recursion (every 2-kid branch).
inline double ClampSplitRatio(double ratio,
                               int extent,
                               const SplitLayoutParams& params) {
  ratio = std::clamp(ratio, 0.0, 1.0);
  if (extent <= 0) {
    return ratio;
  }
  const int divider = params.divider_extent;
  const int available = std::max(0, extent - divider);
  const int min_extent = std::min(
      params.min_pane_extent,
      std::max(params.constrained_min_pane_extent,
                static_cast<int>(params.min_pane_extent_percentage * extent)));
  double primary = ratio * available;
  double secondary = available - primary;
  if (primary < min_extent) {
    const double diff = min_extent - primary;
    primary += diff;
    secondary -= diff;
  } else if (secondary < min_extent) {
    const double diff = min_extent - secondary;
    secondary += diff;
    primary -= diff;
  }
  return available > 0 ? std::clamp(primary / available, 0.0, 1.0) : ratio;
}

template <typename Leaf>
struct SplitLayoutLeaf {
  Leaf leaf;
  gfx::Rect rect;
};

template <typename Leaf>
struct SplitLayoutGutter {
  SplitNodePath path;
  gfx::Rect rect;
  // kRow: the gutter is a vertical strip separating left/right kids (E-W
  // resize). kCol: a horizontal strip separating top/bottom kids (N-S
  // resize). Matches the branch node's own dir() - see AtriumSplitTree.
  typename AtriumSplitTree<Leaf>::Direction axis;
  // The branch's own extent along `axis` before it was divided - callers
  // convert a drag delta (px) back into a ratio against this, same as
  // AtriumPaneContainerView::SplitExtent() did for the single root split.
  int total_extent = 0;
};

template <typename Leaf>
struct SplitLayoutResult {
  std::vector<SplitLayoutLeaf<Leaf>> leaves;
  std::vector<SplitLayoutGutter<Leaf>> gutters;
};

namespace internal {

template <typename Leaf>
void LayoutNode(const typename AtriumSplitTree<Leaf>::Node& node,
                 const gfx::Rect& rect,
                 const SplitNodePath& path,
                 const SplitLayoutParams& params,
                 const std::function<double(const SplitNodePath&)>& ratio_for,
                 SplitLayoutResult<Leaf>& result) {
  if (node.is_leaf()) {
    result.leaves.push_back({node.leaf(), rect});
    return;
  }

  using Direction = typename AtriumSplitTree<Leaf>::Direction;
  const Direction dir = node.dir();
  const bool horizontal = dir == Direction::kRow;
  const size_t kid_count = node.kids().size();
  const int divider = params.divider_extent;
  const int total_extent = horizontal ? rect.width() : rect.height();
  const int available = std::max(
      0, total_extent - static_cast<int>(kid_count - 1) * divider);

  std::vector<int> extents;
  extents.reserve(kid_count);
  if (kid_count == 2) {
    const double ratio = ClampSplitRatio(ratio_for(path), total_extent, params);
    const int first = base::ClampRound(ratio * available);
    extents.push_back(first);
    extents.push_back(available - first);
  } else {
    // InsertAtLeaf's canonical-grid override means every branch it produces
    // has exactly 2 kids - even split is an unreached defensive fallback.
    const int share = kid_count > 0 ? available / static_cast<int>(kid_count) : 0;
    extents.assign(kid_count, share);
    if (!extents.empty()) {
      extents.back() += available - share * static_cast<int>(kid_count);
    }
  }

  // This node's own gutters are recorded before descending into any child -
  // true pre-order, so gutters_[0] in AtriumPaneContainerView is always the
  // ROOT gutter for any tree shape, not just the single-branch 2-leaf case.
  std::vector<gfx::Rect> kid_rects;
  kid_rects.reserve(kid_count);
  int offset = horizontal ? rect.x() : rect.y();
  for (size_t i = 0; i < kid_count; ++i) {
    kid_rects.push_back(
        horizontal ? gfx::Rect(offset, rect.y(), extents[i], rect.height())
                   : gfx::Rect(rect.x(), offset, rect.width(), extents[i]));
    offset += extents[i];
    if (i + 1 < kid_count) {
      const gfx::Rect gutter_rect =
          horizontal ? gfx::Rect(offset, rect.y(), divider, rect.height())
                     : gfx::Rect(rect.x(), offset, rect.width(), divider);
      result.gutters.push_back({path, gutter_rect, dir, total_extent});
      offset += divider;
    }
  }

  for (size_t i = 0; i < kid_count; ++i) {
    SplitNodePath kid_path = path;
    kid_path.push_back(i);
    LayoutNode<Leaf>(*node.kids()[i], kid_rects[i], kid_path, params,
                      ratio_for, result);
  }
}

}  // namespace internal

// `ratio_for(path)` supplies the primary/secondary split for the 2-kid
// branch at `path` - a per-branch stored ratio, defaulting to 0.5 for a
// branch that hasn't been dragged yet.
template <typename Leaf>
SplitLayoutResult<Leaf> LayoutSplitTree(
    const AtriumSplitTree<Leaf>& tree,
    const gfx::Rect& content_rect,
    const SplitLayoutParams& params,
    const std::function<double(const SplitNodePath&)>& ratio_for) {
  SplitLayoutResult<Leaf> result;
  if (tree.empty()) {
    return result;
  }
  internal::LayoutNode<Leaf>(*tree.root(), content_rect, SplitNodePath(),
                              params, ratio_for, result);
  return result;
}

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_SPLIT_TREE_LAYOUT_H_
