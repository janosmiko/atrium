// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_little_marker_unittest.cc
#include "chrome/browser/ui/atrium/atrium_little_marker.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

// The global starts false regardless of test order - previous tests below
// must always leave it reset, since it's a process-wide static (mirrors
// BrowserController's own file-local CreatingOwnBrowser() invariant).
class AtriumLittleMarkerTest : public testing::Test {
 protected:
  void TearDown() override {
    // Never leak an armed marker into a later test - same defensive posture
    // as ExtensionPopupWatchdogPolicyTest's per-test isolation.
    CreatingLittleBrowser() = false;
  }
};

TEST_F(AtriumLittleMarkerTest, DefaultsFalse) {
  EXPECT_FALSE(CreatingLittleBrowser());
}

TEST_F(AtriumLittleMarkerTest, SetTrueIsObservedByLaterReaders) {
  CreatingLittleBrowser() = true;
  // A second call site reads the SAME global - this is the whole point of
  // exporting a `bool&` rather than a value: BrowserController::
  // CreateLittleWindow writes it, browser_window_factory.cc reads it, in
  // that order, within one synchronous Browser::Create() call.
  EXPECT_TRUE(CreatingLittleBrowser());
}

TEST_F(AtriumLittleMarkerTest, ResetToFalseAfterBracket) {
  CreatingLittleBrowser() = true;
  ASSERT_TRUE(CreatingLittleBrowser());
  CreatingLittleBrowser() = false;
  EXPECT_FALSE(CreatingLittleBrowser());
}

}  // namespace
}  // namespace atrium
