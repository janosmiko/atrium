// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_peek_decision.cc
#include "chrome/browser/ui/atrium/atrium_peek_decision.h"

namespace atrium {

bool ShouldAutoPeekForInputs(const PeekDecisionInputs& inputs) {
  return inputs.source_has_committed_url && inputs.is_http_or_https &&
         !inputs.is_same_domain_or_host && inputs.has_atrium_controller &&
         inputs.is_peek_source_tab;
}

bool ShouldPeekPopupForInputs(const PopupPeekDecisionInputs& inputs) {
  return inputs.has_atrium_controller && inputs.source_is_active_tab &&
         inputs.target_is_peekable_scheme && !inputs.targets_another_display;
}

}  // namespace atrium
