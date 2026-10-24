// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_CLAIM_LEDGER_DECISION_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_CLAIM_LEDGER_DECISION_H_

#include <string>
#include <string_view>
#include <vector>

namespace atrium {

// a holder tag is "win<ptr>/<role>" or "peek<ptr>", and two tags
// sharing the owner substring before '/' are the same instance working
// through different call sites, not a race with another one.
std::string_view ClaimOwner(std::string_view tag);

// Drops `holder`'s entry from `holders` (matched by owner). Returns true
// when nothing matched - TraceClaim logs that as STALE-DETACH.
bool ApplyDetach(std::vector<std::string>& holders, std::string_view holder);

// Records `holder`'s claim, replacing any existing entry from the same
// owner. Returns true when a DIFFERENT owner already held it - TraceClaim
// logs that as DOUBLE-CLAIM.
bool ApplyClaim(std::vector<std::string>& holders, std::string_view holder);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_CLAIM_LEDGER_DECISION_H_
