// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_surface_message_router.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

class FakeOwner {
 public:
  struct Route {
    const char* name;
    void (FakeOwner::*handler)(const base::ListValue& args);
  };

  void SetSeenToArg(const base::ListValue& args) {
    seen_ = args.front().GetInt();
  }
  void SetSeenTo1(const base::ListValue& args) { seen_ = 1; }
  void SetSeenTo2(const base::ListValue& args) { seen_ = 2; }

  int seen() const { return seen_; }

 private:
  int seen_ = 0;
};

TEST(SurfaceMessageRouterTest, ARegisteredNameReachesItsHandlerWithItsArgs) {
  static constexpr FakeOwner::Route kRoutes[] = {
      {"sidebarRowsDrawn", &FakeOwner::SetSeenToArg},
  };
  FakeOwner owner;
  SurfaceMessageRouter router;
  router.Bind(&owner, base::span(kRoutes));
  base::ListValue args;
  args.Append(7);
  EXPECT_TRUE(router.Route("sidebarRowsDrawn", args));
  EXPECT_EQ(owner.seen(), 7);
}

// The window used to fall off the end of its chain for a name it did not
// know, and the caller needs the same silence to keep a capture verb parser
// behind the table.
TEST(SurfaceMessageRouterTest, AnUnknownNameIsRefusedRatherThanCrashing) {
  SurfaceMessageRouter router;
  EXPECT_FALSE(router.Route("nothingRegisteredThis", base::ListValue()));
}

TEST(SurfaceMessageRouterTest, ALaterRouteForTheSameNameWinsOverAnEarlierOne) {
  static constexpr FakeOwner::Route kRoutes[] = {
      {"x", &FakeOwner::SetSeenTo1},
      {"x", &FakeOwner::SetSeenTo2},
  };
  FakeOwner owner;
  SurfaceMessageRouter router;
  router.Bind(&owner, base::span(kRoutes));
  EXPECT_TRUE(router.Route("x", base::ListValue()));
  EXPECT_EQ(owner.seen(), 2);
}

}  // namespace
}  // namespace atrium
