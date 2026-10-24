// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_startup_hold.h"

#include <string>

#include "base/functional/bind.h"
#include "base/test/task_environment.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

class StartupHoldTest : public testing::Test {
 protected:
  base::test::TaskEnvironment env_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  StartupHold hold_;
  std::vector<std::string> shown_;

  StartupHold::ShowCallback Recorder() {
    return base::BindOnce(
        [](std::vector<std::string>* out, std::string_view reason) {
          out->emplace_back(reason);
        },
        &shown_);
  }
};

TEST_F(StartupHoldTest, TheSidebarRowsShowTheWindowAtOnce) {
  hold_.Begin(kStartupHoldCap, Recorder());
  EXPECT_TRUE(hold_.pending());
  hold_.Drawn();
  EXPECT_EQ(shown_, std::vector<std::string>{"drawn"});
  EXPECT_FALSE(hold_.pending());
  env_.FastForwardBy(kStartupHoldCap * 2);
  EXPECT_EQ(shown_.size(), 1u);
}

TEST_F(StartupHoldTest, TheCapShowsTheWindowWhenNothingDraws) {
  hold_.Begin(kStartupHoldCap, Recorder());
  env_.FastForwardBy(kStartupHoldCap);
  EXPECT_EQ(shown_, std::vector<std::string>{"cap"});
  hold_.Drawn();
  EXPECT_EQ(shown_.size(), 1u);
}

// A window that closes inside the hold has left the browser list, and a show
// then activates a browser the list no longer holds.
TEST_F(StartupHoldTest, ACancelledHoldNeverShows) {
  hold_.Begin(kStartupHoldCap, Recorder());
  hold_.Cancel();
  EXPECT_FALSE(hold_.pending());
  env_.FastForwardBy(kStartupHoldCap * 2);
  hold_.Drawn();
  EXPECT_TRUE(shown_.empty());
}

// The sidebar can report its rows before the window's first Show(). A hold
// begun after that waited the whole cap for a report that already came.
TEST_F(StartupHoldTest, ADrawBeforeTheHoldShowsAtBegin) {
  hold_.Drawn();
  EXPECT_TRUE(shown_.empty());
  EXPECT_FALSE(hold_.pending());
  hold_.Begin(kStartupHoldCap, Recorder());
  EXPECT_EQ(shown_, std::vector<std::string>{"drawn"});
  EXPECT_FALSE(hold_.pending());
  env_.FastForwardBy(kStartupHoldCap * 2);
  EXPECT_EQ(shown_.size(), 1u);
}

// An overlay opened inside the hold is a child window, and macOS does not show
// a child of a window that is not on screen yet.
TEST_F(StartupHoldTest, AReleaseShowsTheWindowAtOnce) {
  hold_.Begin(kStartupHoldCap, Recorder());
  hold_.Release();
  EXPECT_EQ(shown_, std::vector<std::string>{"overlay"});
  EXPECT_FALSE(hold_.pending());
  hold_.Drawn();
  env_.FastForwardBy(kStartupHoldCap * 2);
  EXPECT_EQ(shown_.size(), 1u);
}

TEST_F(StartupHoldTest, AReleaseWithNoHoldIsNothing) {
  hold_.Release();
  EXPECT_TRUE(shown_.empty());
}

// A cold profile needed 2.1s to report its rows on the VM. The cap has to
// outlast that whole sidebar load, or it becomes the normal path and the hold
// waits for nothing.
TEST_F(StartupHoldTest, TheCapOutlastsAColdSidebarLoad) {
  EXPECT_GE(kStartupHoldCap, base::Milliseconds(2500));
}

}  // namespace
}  // namespace atrium
