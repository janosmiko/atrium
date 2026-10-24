// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_custom_endpoint_request_guard.cc
#include "chrome/browser/ui/atrium/atrium_custom_endpoint_request_guard.h"

#include "net/base/ip_address.h"
#include "third_party/atrium/core/include/atrium_core.h"

namespace atrium {

bool ResolvedAddressIsLocal(const net::IPAddress& address) {
  return atrium_core_resolved_ip_is_local(address.ToString().c_str());
}

bool AnyResolvedAddressIsLocal(const std::vector<net::IPAddress>& addresses) {
  if (addresses.empty()) {
    return true;
  }
  for (const net::IPAddress& address : addresses) {
    if (ResolvedAddressIsLocal(address)) {
      return true;
    }
  }
  return false;
}

}  // namespace atrium
