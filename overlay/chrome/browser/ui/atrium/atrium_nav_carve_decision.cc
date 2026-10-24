// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_nav_carve_decision.h"

namespace atrium {

namespace {

constexpr int kBandControlsMinX = 82;
constexpr int kBandControlsMaxX = 196;
constexpr int kBandControlsNearInset = 24;
constexpr int kBandControlsFarInset = 138;

// sidebar-nav-row.css: .sbnav .nb is 22x22, .sbnav's flex gap is 2px, and
// the right-aligned group sits right: 10px from the row's own right edge.
constexpr int kNavButtonWidth = 22;
constexpr int kNavButtonGap = 2;
constexpr int kNavGroupRightInset = 10;
constexpr int kNavGroupButtonCount = 3;  // back, forward, reload

// The sidebar toggle sits left of the group, and the overflow button that
// replaces the hidden controls is deliberately narrower than one of them.
constexpr int kNavToggleWidth = 22;
constexpr int kNavMoreButtonWidth = 16;
constexpr int kTrafficLightClearance = 86;

// What the toggle, `visible` group buttons and the overflow button occupy.
int NavRowContentWidth(int visible, bool with_more) {
  const int items = 1 + visible + (with_more ? 1 : 0);
  return kNavToggleWidth + visible * kNavButtonWidth +
         (with_more ? kNavMoreButtonWidth : 0) + (items - 1) * kNavButtonGap;
}

// Just the right group: the visible buttons plus the overflow button, which is
// what the carve has to cover. Never empty - the overflow button stands in for
// whatever is hidden, so `items` is at least one.
int NavGroupCarveWidth(int visible) {
  const bool with_more = visible < kNavGroupButtonCount;
  const int items = visible + (with_more ? 1 : 0);
  return visible * kNavButtonWidth + (with_more ? kNavMoreButtonWidth : 0) +
         (items - 1) * kNavButtonGap;
}

}  // namespace

int NavControlsWidth(int sidebar_width) {
  return sidebar_width - kTrafficLightClearance - kNavGroupRightInset;
}

int VisibleNavButtonCount(int controls_width) {
  // Below the toggle rungs the toggle is shed, never forward, so the group
  // keeps up to two buttons in the room the toggle gave back - the ladder
  // sidebar-nav-row.js's visibleNavButtons runs.
  for (int count = kNavGroupButtonCount; count >= 2; --count) {
    if (NavRowContentWidth(count, count < kNavGroupButtonCount) <=
        controls_width) {
      return count;
    }
  }
  for (int count = 2; count > 0; --count) {
    const int width = count * kNavButtonWidth + kNavMoreButtonWidth +
                      count * kNavButtonGap;
    if (width <= controls_width) {
      return count;
    }
  }
  return 0;
}

bool PointInNavCarve(const gfx::Point& point,
                      const NavCarveGeometry& geometry) {
  const int lo = geometry.on_right
      ? (geometry.win_w - geometry.sidebar_width + kBandControlsNearInset)
      : kBandControlsMinX;
  const int hi = geometry.on_right
      ? (geometry.win_w - geometry.sidebar_width + kBandControlsFarInset)
      : kBandControlsMaxX;
  if (point.x() >= lo && point.x() < hi) {
    return true;
  }
  if (!geometry.nav_group_in_row) {
    return false;
  }

  const int sidebar_origin =
      geometry.on_right ? (geometry.win_w - geometry.sidebar_width) : 0;
  const int group_hi =
      sidebar_origin + geometry.sidebar_width - kNavGroupRightInset;
  const int group_lo = group_hi - NavGroupCarveWidth(VisibleNavButtonCount(
                                     NavControlsWidth(geometry.sidebar_width)));
  return point.x() >= group_lo && point.x() < group_hi;
}

}  // namespace atrium
