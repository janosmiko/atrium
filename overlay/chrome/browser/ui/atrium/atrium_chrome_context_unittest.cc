// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

#include "chrome/browser/ui/atrium/atrium_chrome_context.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

// Moved from a browser test that could never observe it: the VM harness
// deploys the dylib, so the context was always started before that test ran.
// A regression that started it unconditionally would fail here instead.
TEST(AtriumChromeContextTest, NothingStartsTheContextOnItsOwn) {
  EXPECT_EQ(nullptr, AtriumChromeContext::GetIfExists());
}

}  // namespace
}  // namespace atrium
