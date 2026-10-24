// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_find_action.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_FIND_ACTION_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_FIND_ACTION_H_

namespace atrium {

// What FindSession::Apply (atrium_find_session.h) was called with: which of
// the bar's five user actions this call answers.
enum class FindTrigger { kOpen, kQueryChanged, kNext, kPrevious, kClose };

// The FindTabHelper calls FindRequestFor's answer maps to, in the posture of
// atrium_peek_decision.h: values in, an answer out, no content:: or
// find_in_page:: dependency.
struct FindRequest {
  bool start = false;          // call StartFinding
  bool stop = false;           // call StopFinding
  bool forward = true;         // StartFinding's forward_direction
  bool find_match = false;     // jump to a match, or only count and highlight
  bool keep_selection = true;  // StopFinding's SelectionAction: kKeep vs kClear
};

// The request FindSession::Apply should issue for `trigger`, given whether the
// bar's query field is empty. Read off find_bar_controller.cc:42-89 and
// find_bar_view.cc:607-633.
FindRequest FindRequestFor(FindTrigger trigger, bool query_empty);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_FIND_ACTION_H_
