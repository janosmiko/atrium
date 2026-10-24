// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_DROP_TARGET_DECISION_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_DROP_TARGET_DECISION_H_

#include <vector>

#include "ui/gfx/geometry/point.h"
#include "ui/gfx/geometry/rect.h"

namespace atrium {

// what a tab-drag release point (screen coordinates) lands on,
// exactly one of three outcomes, no more.
enum class DropTargetKind {
  kNoMatch,  // No other Atrium window is under the point - the
             // little-window tear-off.
  kSidebar,  // Over another Atrium window's sidebar.
  kContent,  // Over another Atrium window's web content.
};

// One other, live Atrium window as the classifier sees it - both rects in
// screen coordinates. `sidebar_bounds` is ignored when `has_sidebar` is
// false (a little window has no sidebar, so any release
// over its bounds falls to kContent).
struct DropTargetCandidate {
  gfx::Rect window_bounds;
  gfx::Rect sidebar_bounds;
  bool has_sidebar = true;
};

struct DropTargetDecision {
  DropTargetKind kind;
  // Index into the candidates vector passed to ChooseDropTarget(), or -1
  // for kNoMatch.
  int index;
};

// Finds the first candidate whose window_bounds contains `point`, then
// resolves kSidebar vs kContent for it; kNoMatch if no candidate's
// window_bounds contains the point at all. Callers exclude the drag's own
// source window from `candidates` before calling this (so
// a release back onto the source window's own bounds is indistinguishable
// from an empty-desktop release and takes the same kNoMatch tear-off path).
DropTargetDecision ChooseDropTarget(
    const gfx::Point& point,
    const std::vector<DropTargetCandidate>& candidates);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_DROP_TARGET_DECISION_H_
