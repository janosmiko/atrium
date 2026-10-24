// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_url_submit_decision.h"

#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"

namespace atrium {

SubmitTarget DecideUrlSubmitTarget(bool active_is_favourite,
                                   const GURL& favourite_url,
                                   const GURL& typed) {
  // A hostless URL on either side - about:, or the empty claim an unclaimed
  // tab carries - has no site to compare, and SameDomainOrHost answers false
  // for it, which would open a tab on every submission.
  if (!active_is_favourite || !favourite_url.is_valid() || !typed.is_valid() ||
      favourite_url.host().empty() || typed.host().empty()) {
    return SubmitTarget::kSameTab;
  }
  return net::registry_controlled_domains::SameDomainOrHost(
             favourite_url, typed,
             net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES)
             ? SubmitTarget::kSameTab
             : SubmitTarget::kNewTab;
}

}  // namespace atrium
