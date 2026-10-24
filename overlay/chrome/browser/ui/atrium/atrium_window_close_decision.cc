// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_window_close_decision.cc
#include "chrome/browser/ui/atrium/atrium_window_close_decision.h"

namespace atrium {

bool ShouldKeepWindowOpen(const WindowCloseInputs& inputs) {
  return inputs.tab_strip_empty && inputs.space_window &&
         !inputs.trying_to_quit && !inputs.off_the_record &&
         !inputs.user_initiated;
}

bool UserInitiatedCloseWasAbandoned(bool deferred_on_prompt,
                                    bool attempting_to_close) {
  return deferred_on_prompt && !attempting_to_close;
}

AppKitCloseAction ChooseAppKitCloseAction(bool expect_commit_close,
                                          bool close_post_pending) {
  // The expectation is armed for exactly one pass, so it outranks the other:
  // a commit must never be mistaken for a fresh close.
  if (expect_commit_close) {
    return AppKitCloseAction::kLetCommitThrough;
  }
  if (close_post_pending) {
    return AppKitCloseAction::kAlreadyStarting;
  }
  return AppKitCloseAction::kStartUserInitiated;
}

}  // namespace atrium
