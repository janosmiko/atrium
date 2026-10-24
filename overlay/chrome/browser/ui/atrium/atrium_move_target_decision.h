// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_MOVE_TARGET_DECISION_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_MOVE_TARGET_DECISION_H_

#include <string>
#include <vector>

namespace atrium {

// the move-vs-reopen decision BrowserController::
// MoveLittleTabToSpace makes when the foyer's Move-to-Space button fires -
// extracted out of that method's Registry() scan so the decision itself is a
// fast, standalone gtest unit (little_marker's posture), independent of
// TabStripModel/content::WebContents/Registry()'s private state.
enum class MoveTargetKind {
  kNone,
  kSameProfile,
  kCrossProfile,
};

// One resident, non-self controller as MoveLittleTabToSpace's loop sees it:
// its profile id and whether it is itself a little window (never a valid
// target - a little window's tab never lands in another little window).
struct MoveTargetCandidate {
  std::string profile_id;
  bool is_little;
};

struct MoveTargetDecision {
  MoveTargetKind kind;
  // Index into the candidates vector passed to ChooseMoveTarget(), or -1 for
  // kNone.
  int index;
};

// Mirrors MoveLittleTabToSpace's scan exactly: the first same-profile,
// non-little candidate wins outright (kSameProfile) even if a different
// candidate was seen first; absent that, the first non-little candidate of
// any other profile is the cross-profile fallback (kCrossProfile); no
// non-little candidate at all is kNone.
MoveTargetDecision ChooseMoveTarget(
    const std::string& current_profile_id,
    const std::vector<MoveTargetCandidate>& candidates);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_MOVE_TARGET_DECISION_H_
