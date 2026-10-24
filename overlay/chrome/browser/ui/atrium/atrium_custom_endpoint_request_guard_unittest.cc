// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_custom_endpoint_request_guard_unittest.cc
#include "chrome/browser/ui/atrium/atrium_custom_endpoint_request_guard.h"

#include "net/base/ip_address.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

net::IPAddress Parse(const char* literal) {
  net::IPAddress address;
  CHECK(address.AssignFromIPLiteral(literal));
  return address;
}

// a hostname that resolves to loopback or a private range
// is refused, proven here against the resolved address itself rather than a
// literal host string - the DNS-rebind case save-time validation cannot see.
TEST(CustomEndpointRequestGuardTest, RefusesLoopbackLinkLocalAndPrivate) {
  for (const char* literal :
       {"127.0.0.1", "169.254.169.254", "10.1.2.3", "172.16.0.5",
        "192.168.1.1", "0.0.0.0", "::1", "fe80::1", "::ffff:127.0.0.1"}) {
    EXPECT_TRUE(ResolvedAddressIsLocal(Parse(literal))) << literal;
  }
}

TEST(CustomEndpointRequestGuardTest, AcceptsAPublicAddress) {
  for (const char* literal : {"8.8.8.8", "1.1.1.1", "2606:4700:4700::1111"}) {
    EXPECT_FALSE(ResolvedAddressIsLocal(Parse(literal))) << literal;
  }
}

TEST(CustomEndpointRequestGuardTest, AnyLocalAddressRefusesTheWholeAnswer) {
  std::vector<net::IPAddress> addresses = {Parse("8.8.8.8"), Parse("127.0.0.1")};
  EXPECT_TRUE(AnyResolvedAddressIsLocal(addresses));
}

TEST(CustomEndpointRequestGuardTest, AllPublicAddressesAreAccepted) {
  std::vector<net::IPAddress> addresses = {Parse("8.8.8.8"), Parse("1.1.1.1")};
  EXPECT_FALSE(AnyResolvedAddressIsLocal(addresses));
}

TEST(CustomEndpointRequestGuardTest, NoResolvedAddressIsRefused) {
  EXPECT_TRUE(AnyResolvedAddressIsLocal({}));
}

}  // namespace
}  // namespace atrium
