// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_custom_endpoint_request_guard.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_CUSTOM_ENDPOINT_REQUEST_GUARD_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_CUSTOM_ENDPOINT_REQUEST_GUARD_H_

#include <vector>

namespace net {
class IPAddress;
}

namespace atrium {

// The request-time counterpart of the Rust crate's host_is_local: asks the
// same question of an address DNS actually returned rather than of a literal
// host, so a rebind after save time still gets refused.
bool ResolvedAddressIsLocal(const net::IPAddress& address);

// Whether any resolved address for the custom endpoint's host is unsafe to
// connect to. Empty is unsafe too - nothing resolved is not a host the loader
// should connect to either.
bool AnyResolvedAddressIsLocal(const std::vector<net::IPAddress>& addresses);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_CUSTOM_ENDPOINT_REQUEST_GUARD_H_
