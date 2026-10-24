// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_log_safe_url.cc
#include "chrome/browser/ui/atrium/atrium_log_safe_url.h"

#include "net/base/registry_controlled_domains/registry_controlled_domain.h"
#include "url/gurl.h"

namespace atrium {

std::string LogSafeUrl(const GURL& url) {
  if (!url.is_valid()) {
    return "invalid";
  }
  const std::string domain =
      net::registry_controlled_domains::GetDomainAndRegistry(
          url, net::registry_controlled_domains::INCLUDE_PRIVATE_REGISTRIES);
  const std::string host = domain.empty() ? std::string(url.host()) : domain;
  const std::string scheme(url.scheme());
  if (host.empty()) {
    return scheme;
  }
  return scheme + "://" + host;
}

}  // namespace atrium
