// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_log_safe_url_unittest.cc
#include "chrome/browser/ui/atrium/atrium_log_safe_url.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace atrium {
namespace {

TEST(AtriumLogSafeUrlTest, SchemeAndETLDPlusOne) {
  EXPECT_EQ(LogSafeUrl(GURL("https://sub.example.com/path?q=secret")),
            "https://example.com");
}

TEST(AtriumLogSafeUrlTest, IpAddressFallsBackToBareHost) {
  EXPECT_EQ(LogSafeUrl(GURL("http://127.0.0.1:8000/redirect-to-lab.html")),
            "http://127.0.0.1");
}

TEST(AtriumLogSafeUrlTest, HostlessUrlKeepsOnlyTheScheme) {
  EXPECT_EQ(LogSafeUrl(GURL("about:blank")), "about");
}

TEST(AtriumLogSafeUrlTest, InvalidUrlIsReportedAsInvalid) {
  EXPECT_EQ(LogSafeUrl(GURL()), "invalid");
}

TEST(AtriumLogSafeUrlTest, DropsPathAndQuery) {
  const std::string safe =
      LogSafeUrl(GURL("https://example.com/secret-path?token=abc123"));
  EXPECT_EQ(safe.find("secret-path"), std::string::npos);
  EXPECT_EQ(safe.find("token"), std::string::npos);
}

TEST(AtriumLogSafeUrlTest, Ipv6HostFallsBackToBareHost) {
  const std::string safe = LogSafeUrl(GURL("http://[::1]:8000/secret-path"));
  EXPECT_EQ(safe, "http://[::1]");
  EXPECT_EQ(safe.find("8000"), std::string::npos);
  EXPECT_EQ(safe.find("secret-path"), std::string::npos);
}

TEST(AtriumLogSafeUrlTest, BlobUrlKeepsNoMoreThanSchemeAndHost) {
  const std::string safe =
      LogSafeUrl(GURL("blob:https://example.com/9b1deb4d-3b7d-4bad-9bdd-"
                       "2b0d7b3dcb6d"));
  EXPECT_EQ(safe.find("9b1deb4d-3b7d-4bad-9bdd-2b0d7b3dcb6d"),
            std::string::npos);
}

TEST(AtriumLogSafeUrlTest, DataUrlKeepsOnlyTheScheme) {
  EXPECT_EQ(LogSafeUrl(GURL("data:text/plain;base64,c2VjcmV0")), "data");
}

// Twelve scenarios wait on a `.localhost` SUBDOMAIN in this line, so
// collapsing the label away would time every one of them out.
TEST(AtriumLogSafeUrlTest, LocalhostSubdomainKeepsItsLabel) {
  EXPECT_EQ(LogSafeUrl(GURL("http://dl.localhost:8000/fixture.html")),
            "http://dl.localhost");
  EXPECT_EQ(LogSafeUrl(GURL("http://0y.localhost/")), "http://0y.localhost");
}

// The same for a reserved TLD, which other scenarios gate on.
TEST(AtriumLogSafeUrlTest, ReservedTldKeepsItsSubdomain) {
  EXPECT_EQ(LogSafeUrl(GURL("http://corp.example/intranet")),
            "http://corp.example");
}

}  // namespace
}  // namespace atrium
