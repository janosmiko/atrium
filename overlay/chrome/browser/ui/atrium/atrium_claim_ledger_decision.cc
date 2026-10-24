// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
#include "chrome/browser/ui/atrium/atrium_claim_ledger_decision.h"

#include <algorithm>

namespace atrium {

std::string_view ClaimOwner(std::string_view tag) {
  return tag.substr(0, tag.find('/'));
}

bool ApplyDetach(std::vector<std::string>& holders, std::string_view holder) {
  const size_t before = holders.size();
  std::erase_if(holders, [&](const std::string& recorded) {
    return ClaimOwner(recorded) == ClaimOwner(holder);
  });
  return holders.size() == before;
}

bool ApplyClaim(std::vector<std::string>& holders, std::string_view holder) {
  const bool claimed_by_other =
      std::ranges::any_of(holders, [&](const std::string& recorded) {
        return ClaimOwner(recorded) != ClaimOwner(holder);
      });
  std::erase_if(holders, [&](const std::string& recorded) {
    return ClaimOwner(recorded) == ClaimOwner(holder);
  });
  holders.emplace_back(holder);
  return claimed_by_other;
}

}  // namespace atrium
