// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_sidebar_snapshot_gate_unittest.cc
#include "chrome/browser/ui/atrium/atrium_sidebar_snapshot_gate.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

// A hidden profile's own AtriumBrowserWindow must not run the
// sidebar's JS rebuild - nothing is on screen to show it. A presenting one
// must, on every tab/title/favicon event, or the sidebar goes stale while the
// user is looking at it.

TEST(SidebarSnapshotGateTest, HiddenWindowSkipsThePush) {
  EXPECT_EQ(DecideSidebarSnapshot(/*is_presenter=*/false),
            SidebarSnapshotDecision::kSkip);
}

TEST(SidebarSnapshotGateTest, PresentingWindowPushes) {
  EXPECT_EQ(DecideSidebarSnapshot(/*is_presenter=*/true),
            SidebarSnapshotDecision::kPush);
}

// Models the call sequence BrowserController actually drives: repeated
// tab/title/favicon churn on a hidden profile (every one skipped, none
// queued for replay), then the single, unconditional catch-up call
// ActivateWindow's kFirstActivation branch makes once this window becomes
// its slot's presenter. The skipped calls must not add up to more than the
// one coalesced rebuild the transition itself triggers.
TEST(SidebarSnapshotGateTest, HiddenWindowBecomingPresenterPushesExactlyOnce) {
  int push_count = 0;
  for (int i = 0; i < 5; ++i) {
    if (DecideSidebarSnapshot(/*is_presenter=*/false) ==
        SidebarSnapshotDecision::kPush) {
      ++push_count;
    }
  }
  EXPECT_EQ(push_count, 0);

  if (DecideSidebarSnapshot(/*is_presenter=*/true) ==
      SidebarSnapshotDecision::kPush) {
    ++push_count;
  }
  EXPECT_EQ(push_count, 1);
}

// Boot half. AtriumBrowserWindow's constructor used to build and
// navigate sidebar_contents_ unconditionally, so every hidden-pool profile
// (show=false) paid full renderer/JS cost for a surface nobody could see -
// at boot, competing with every other profile's own construction. The fix
// is CatchUpSidebarForNewPresenter(), the single funnel every "this window
// just became its slot's presenter" call site now uses instead of calling
// BuildHtmlSidebar() and PushSidebarSnapshot() separately by hand.
//
// DecideSidebarPresenterCatchUp is a pure ternary, same shape as
// DecideSidebarSnapshot above - and the previous test file's own history is
// the reason not to stop here. A near-identical pure-function test already
// existed for the per-event push gate, and it proved the enum mapping was
// right while saying nothing about whether every call site that needed to
// invoke it actually did: review found the in-window swap path missed it
// entirely. That gap was in wiring, not in this kind of ternary, and no
// gtest target in this tree constructs a real AtriumBrowserWindow to test
// wiring against (checked: grepped every atrium *_unittest.cc and BUILD.gn's
// unit_tests sources - none does). So the decision this task actually makes
// is tested here, in the same pure style, but CatchUpSidebarForNewPresenter
// is deliberately the ONE place in atrium_browser_window.cc that calls
// BuildHtmlSidebar() and PushSidebarSnapshot() together - every "new
// presenter" call site in atrium_browser_controller.cc (the self-creating
// ctor's `show` block, ActivateWindow's kFirstActivation branch,
// NotifyShownIfAdopted) routes through that one method rather than each
// re-deciding for itself, which is the actual fix for the swap-path class of
// bug: one funnel cannot drift out of sync with itself the way three
// separately-written call sites could. What this test cannot prove - same
// limit as the file's original three cases - is that all three call sites
// really do call CatchUpSidebarForNewPresenter() rather than something else;
// that still wants a fixture nothing in this tree builds yet.

TEST(SidebarSnapshotGateTest, HiddenWindowSkipsBuildAndPush) {
  EXPECT_EQ(DecideSidebarPresenterCatchUp(/*is_presenter=*/false),
            SidebarPresenterCatchUpDecision::kNothing);
}

TEST(SidebarSnapshotGateTest, NewPresenterBuildsAndPushes) {
  EXPECT_EQ(DecideSidebarPresenterCatchUp(/*is_presenter=*/true),
            SidebarPresenterCatchUpDecision::kBuildAndPush);
}

// Models the hidden-pool lifetime CatchUpSidebarForNewPresenter exists for:
// constructed with show=false (never a presenter, so never even considered
// for a build), then eventually switched to. The catch-up call itself is
// idempotent on the build half (BuildHtmlSidebar's own container-emptiness
// guard), so calling it again on an already-current presenter must keep
// deciding kBuildAndPush - that repeat call is exactly how a same-profile
// tab/title/favicon event on the now-visible window re-reaches this
// decision, and it must not start skipping just because it already ran once.
TEST(SidebarSnapshotGateTest, HiddenWindowNeverAsksUntilItBecomesPresenter) {
  int catch_up_count = 0;
  for (int i = 0; i < 5; ++i) {
    if (DecideSidebarPresenterCatchUp(/*is_presenter=*/false) ==
        SidebarPresenterCatchUpDecision::kBuildAndPush) {
      ++catch_up_count;
    }
  }
  EXPECT_EQ(catch_up_count, 0);

  for (int i = 0; i < 2; ++i) {
    if (DecideSidebarPresenterCatchUp(/*is_presenter=*/true) ==
        SidebarPresenterCatchUpDecision::kBuildAndPush) {
      ++catch_up_count;
    }
  }
  EXPECT_EQ(catch_up_count, 2);
}

}  // namespace
}  // namespace atrium
