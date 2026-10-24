// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_traffic_routing.h"

#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace atrium {
namespace {

constexpr char kRules[] = R"({"schemaVersion":1,"rules":[
    {"id":"r1","enabled":true,"matchType":"contains",
     "pattern":"corp.example","targetSpaceID":"SPACE-A"}]})";

constexpr char kRulesV2[] = R"({"schemaVersion":2,"defaultTargetSpaceID":"SPACE-H",
  "rules":[
    {"id":"r1","enabled":true,"matchType":"contains","pattern":"corp.example",
     "targetSpaceID":"SPACE-A","excludedSourceSpaceIDs":["SPACE-P"]},
    {"id":"r2","enabled":true,"matchType":"contains","pattern":"github.com",
     "targetSpaceID":"current"}]})";

TEST(AtriumTrafficRoutingTest, AMatchingUrlNamesItsSpace) {
  SetRoutingRulesJSON(kRules);
  EXPECT_EQ("SPACE-A",
           RouteForURL(GURL("https://mail.corp.example/inbox"), std::string()));
  EXPECT_FALSE(
      RouteForURL(GURL("https://other.test/"), std::string()).has_value());
}

TEST(AtriumTrafficRoutingTest, NoRulesRoutesNothing) {
  // The state a machine with no routing.json is in, and it must read as
  // routing being off rather than as a failure worth logging on every
  // navigation.
  SetRoutingRulesJSON(std::string());
  EXPECT_FALSE(
      RouteForURL(GURL("https://mail.corp.example/"), std::string())
          .has_value());
}

TEST(AtriumTrafficRoutingTest, AnInvalidUrlRoutesNothing) {
  SetRoutingRulesJSON(kRules);
  EXPECT_FALSE(RouteForURL(GURL(), std::string()).has_value());
  EXPECT_FALSE(RouteForURL(GURL("corp.example"), std::string()).has_value());
}

TEST(AtriumTrafficRoutingTest, TheSourceSpaceReachesTheCore) {
  SetRoutingRulesJSON(kRulesV2);
  const GURL corp("https://mail.corp.example/");
  EXPECT_EQ("SPACE-A", RouteForURL(corp, "SPACE-W"));
  EXPECT_EQ("SPACE-A", RouteForURL(corp, std::string()));
  // Excluded source: the rule is skipped and the fallback answers.
  EXPECT_EQ("SPACE-H", RouteForURL(corp, "SPACE-P"));
}

TEST(AtriumTrafficRoutingTest, ACurrentSpaceHitRoutesNothing) {
  SetRoutingRulesJSON(kRulesV2);
  EXPECT_FALSE(RouteForURL(GURL("https://github.com/a"), "SPACE-W").has_value());
  EXPECT_EQ("SPACE-H", RouteForURL(GURL("https://other.test/"), "SPACE-W"));
}

TEST(AtriumTrafficRoutingTest, TheBypassNestsWithoutEndingTheOuterScope) {
  EXPECT_FALSE(ScopedRoutingBypass::IsActive());
  {
    const ScopedRoutingBypass outer;
    EXPECT_TRUE(ScopedRoutingBypass::IsActive());
    {
      const ScopedRoutingBypass inner;
      EXPECT_TRUE(ScopedRoutingBypass::IsActive());
    }
    // The half that matters: a nested scope ending must not expose the next
    // navigation while the outer reopen is still in flight.
    EXPECT_TRUE(ScopedRoutingBypass::IsActive());
  }
  EXPECT_FALSE(ScopedRoutingBypass::IsActive());
}

}  // namespace
}  // namespace atrium
