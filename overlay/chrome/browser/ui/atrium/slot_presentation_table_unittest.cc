// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/slot_presentation_table_unittest.cc
#include "chrome/browser/ui/atrium/slot_presentation_table.h"

#include <algorithm>
#include <vector>

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

// Opaque controller ids. Any distinct non-zero values work; the table never
// dereferences them. Two profiles' controllers sharing one slot are the
// cross-profile in-window-swap case.
constexpr ControllerId kControllerA = 1;
constexpr ControllerId kControllerB = 2;
// a THIRD controller in the same slot - neither its visible one nor
// its presenter. Needed to tell "background" apart from "is the presenter but
// is not visible", which the two-controller tests below cannot distinguish.
constexpr ControllerId kControllerC = 3;
constexpr SlotId kSlot = 7;
constexpr SlotId kOtherSlot = 9;

// Every test gets a fresh table: production keeps one process-lifetime
// instance, so a stack-local instance here is the reset guarantee in practice.
class SlotPresentationTableTest : public testing::Test {
 protected:
  SlotPresentationTable table_;
};

// Regression: a shown window must seed the presenter too, not just
// the visible entry. Before the fix ResolvePresenter returned nullopt and
// theme/Cmd+Q/resize ops fell back to the wrong window.
TEST_F(SlotPresentationTableTest, OnWindowShownSeedsBothVisibleAndPresenter) {
  table_.OnWindowShown(kSlot, kControllerA);

  EXPECT_EQ(table_.ResolveVisible(kSlot), kControllerA);
  EXPECT_EQ(table_.ResolvePresenter(kSlot), kControllerA);
}

// Regression, stated as the symptom: presenter is resolvable right
// after a show, with no intervening activation.
TEST_F(SlotPresentationTableTest, ResolvePresenterAfterShownIsNotEmpty) {
  EXPECT_EQ(table_.ResolvePresenter(kSlot), std::nullopt);

  table_.OnWindowShown(kSlot, kControllerA);

  ASSERT_TRUE(table_.ResolvePresenter(kSlot).has_value());
  EXPECT_EQ(*table_.ResolvePresenter(kSlot), kControllerA);
}

TEST_F(SlotPresentationTableTest, ResolveOnEmptySlotIsNullopt) {
  EXPECT_EQ(table_.ResolveVisible(kSlot), std::nullopt);
  EXPECT_EQ(table_.ResolvePresenter(kSlot), std::nullopt);
}

// First activation on an empty slot seeds visible and presenter to target.
TEST_F(SlotPresentationTableTest, FirstActivationSeedsBoth) {
  ActivateResult r = table_.OnActivate(kSlot, kControllerA,
                                       /*previous_visible_live=*/false,
                                       /*presenter_live=*/false);

  EXPECT_EQ(r.action, ActivateAction::kFirstActivation);
  EXPECT_EQ(r.visible, kControllerA);
  EXPECT_EQ(r.presenter, kControllerA);
  EXPECT_EQ(table_.ResolveVisible(kSlot), kControllerA);
  EXPECT_EQ(table_.ResolvePresenter(kSlot), kControllerA);
}

// Re-activating the already-visible controller is a no-op confirmation: no
// window is touched and neither entry changes.
TEST_F(SlotPresentationTableTest, ReactivateVisibleIsAlreadyVisibleNoOp) {
  table_.OnWindowShown(kSlot, kControllerA);

  ActivateResult r = table_.OnActivate(kSlot, kControllerA,
                                       /*previous_visible_live=*/true,
                                       /*presenter_live=*/true);

  EXPECT_EQ(r.action, ActivateAction::kAlreadyVisible);
  EXPECT_EQ(table_.ResolveVisible(kSlot), kControllerA);
  EXPECT_EQ(table_.ResolvePresenter(kSlot), kControllerA);
}

// The core cross-profile bug: activating a foreign controller in a slot that
// already has a live presenter must NOT move the presenter - ops must keep
// routing to the on-screen window, not the newly-active foreign controller.
TEST_F(SlotPresentationTableTest, CrossProfileSwapKeepsPresenter) {
  table_.OnWindowShown(kSlot, kControllerA);

  ActivateResult r = table_.OnActivate(kSlot, kControllerB,
                                       /*previous_visible_live=*/true,
                                       /*presenter_live=*/true);

  EXPECT_EQ(r.action, ActivateAction::kInWindowSwap);
  EXPECT_EQ(r.visible, kControllerB);
  EXPECT_EQ(r.presenter, kControllerA);
  // Visible content followed the switch; the physical presenter did not.
  EXPECT_EQ(table_.ResolveVisible(kSlot), kControllerB);
  EXPECT_EQ(table_.ResolvePresenter(kSlot), kControllerA);
}

// Switching back to the presenter's own controller after a swap is again an
// in-window swap (the presenter window never moved), returning to the same
// presenter.
TEST_F(SlotPresentationTableTest, SwapBackToPresenterStillPresentsPresenter) {
  table_.OnWindowShown(kSlot, kControllerA);
  table_.OnActivate(kSlot, kControllerB, true, true);  // A presents, B visible.

  ActivateResult r = table_.OnActivate(kSlot, kControllerA,
                                       /*previous_visible_live=*/true,
                                       /*presenter_live=*/true);

  EXPECT_EQ(r.action, ActivateAction::kInWindowSwap);
  EXPECT_EQ(r.presenter, kControllerA);
  EXPECT_EQ(table_.ResolveVisible(kSlot), kControllerA);
  EXPECT_EQ(table_.ResolvePresenter(kSlot), kControllerA);
}

// Single-embed guard condition. The cross-profile swap only releases
// the incoming contents from target's OWN pane when target is NOT the
// presenter (else target->window() == presenter->window() and the release
// would detach the pane it is about to attach into). This asserts the exact
// `r.presenter != target` distinction that gate depends on, for both swap
// shapes: a genuine cross-profile switch (presenter stays behind, differs from
// target -> release runs) vs a switch back to the presenter's own controller
// (presenter == target -> release skipped).
TEST_F(SlotPresentationTableTest, InWindowSwapPresenterVsTargetGatesRelease) {
  table_.OnWindowShown(kSlot, kControllerA);

  // Cross to B: A presents, B is the activation target. Release must run.
  ActivateResult cross = table_.OnActivate(kSlot, kControllerB, true, true);
  ASSERT_EQ(cross.action, ActivateAction::kInWindowSwap);
  EXPECT_NE(cross.presenter, kControllerB);  // presenter != target -> release.

  // Switch back to A: A both presents and is the target. Release must skip.
  ActivateResult back = table_.OnActivate(kSlot, kControllerA, true, true);
  ASSERT_EQ(back.action, ActivateAction::kInWindowSwap);
  EXPECT_EQ(back.presenter, kControllerA);  // presenter == target -> skip.
}

// If the previously-visible controller is torn down, activation reduces to a
// first activation (plain show), reseeding both entries to target.
TEST_F(SlotPresentationTableTest, DeadPreviousVisibleReducesToFirstActivation) {
  table_.OnWindowShown(kSlot, kControllerA);

  ActivateResult r = table_.OnActivate(kSlot, kControllerB,
                                       /*previous_visible_live=*/false,
                                       /*presenter_live=*/false);

  EXPECT_EQ(r.action, ActivateAction::kFirstActivation);
  EXPECT_EQ(r.presenter, kControllerB);
  EXPECT_EQ(table_.ResolveVisible(kSlot), kControllerB);
  EXPECT_EQ(table_.ResolvePresenter(kSlot), kControllerB);
}

// Defensive fallback: a live previous-visible but a dead/missing presenter
// falls back to presenting through `previous`, never dereferencing a gone
// presenter window.
TEST_F(SlotPresentationTableTest, DeadPresenterFallsBackToPreviousVisible) {
  table_.OnWindowShown(kSlot, kControllerA);

  ActivateResult r = table_.OnActivate(kSlot, kControllerB,
                                       /*previous_visible_live=*/true,
                                       /*presenter_live=*/false);

  EXPECT_EQ(r.action, ActivateAction::kInWindowSwap);
  EXPECT_EQ(r.presenter, kControllerA);
}

// Teardown clears entries only when they still name the departing controller.
TEST_F(SlotPresentationTableTest, TeardownClearsOnlyOwningId) {
  table_.OnWindowShown(kSlot, kControllerA);
  // B is now visible via an in-window swap; A still presents.
  table_.OnActivate(kSlot, kControllerB, true, true);

  // A tears down: it owns the presenter entry (cleared) but not the visible
  // entry (B still visible, untouched).
  table_.OnTeardown(kSlot, kControllerA);
  EXPECT_EQ(table_.ResolvePresenter(kSlot), std::nullopt);
  EXPECT_EQ(table_.ResolveVisible(kSlot), kControllerB);

  // A second controller's teardown must not stomp B's still-live visible entry
  // when it owns neither entry.
  table_.OnTeardown(kSlot, kControllerA);
  EXPECT_EQ(table_.ResolveVisible(kSlot), kControllerB);
}

TEST_F(SlotPresentationTableTest, TeardownOfNonOwnerIsNoOp) {
  table_.OnWindowShown(kSlot, kControllerA);

  table_.OnTeardown(kSlot, kControllerB);

  EXPECT_EQ(table_.ResolveVisible(kSlot), kControllerA);
  EXPECT_EQ(table_.ResolvePresenter(kSlot), kControllerA);
}

// Slots are independent: activity in one slot never touches another.
TEST_F(SlotPresentationTableTest, SlotsAreIndependent) {
  table_.OnWindowShown(kSlot, kControllerA);
  table_.OnWindowShown(kOtherSlot, kControllerB);

  table_.OnActivate(kSlot, kControllerB, true, true);

  EXPECT_EQ(table_.ResolveVisible(kOtherSlot), kControllerB);
  EXPECT_EQ(table_.ResolvePresenter(kOtherSlot), kControllerB);
}

// Reset returns the table to its pristine, empty state.
TEST_F(SlotPresentationTableTest, ResetClearsAllState) {
  table_.OnWindowShown(kSlot, kControllerA);
  table_.OnWindowShown(kOtherSlot, kControllerB);

  table_.Reset();

  EXPECT_EQ(table_.ResolveVisible(kSlot), std::nullopt);
  EXPECT_EQ(table_.ResolvePresenter(kSlot), std::nullopt);
  EXPECT_EQ(table_.ResolveVisible(kOtherSlot), std::nullopt);
  EXPECT_EQ(table_.ResolvePresenter(kOtherSlot), std::nullopt);
}

// ---------------------------------------------------------------------------
// display routing and the single-embed invariant.
//
// Reproduces, at the only layer that can hold it without a browser, the state
// machine behind the 2026-07-26 DCHECK(views_host_) abort. The log for that
// crash shows ONE slot toggling between two profiles' hosts, so `visible`
// flips between kControllerA and kControllerB while the presenter stays put -
// exactly the sequence these tests drive.
//
// The invariant under test: whenever a controller's tab-strip update is routed
// to a different window than the previous event was, the previous window is
// named as `stale` so the caller can release the WebContents from it BEFORE
// the new window attaches. Two views::WebViews hosting one WebContents is
// what nulls views_host_ under the loser and aborts on its next detach.
// ---------------------------------------------------------------------------

// Baseline: with no swap in play a controller shows its own tab in its own
// window, and there is no second holder to release.
TEST_F(SlotPresentationTableTest, DisplayRoutingWithoutASwapIsSelfAndNoStale) {
  table_.OnWindowShown(kSlot, kControllerA);

  DisplayRouting r = table_.ResolveDisplayRouting(kSlot, kControllerA,
                                                  /*presenter_live=*/true,
                                                  /*visible_live=*/true);

  EXPECT_EQ(r.display, kControllerA);
  EXPECT_EQ(r.stale, kInvalidControllerId);
}

// After a cross-profile in-window swap the visible controller's tab is shown
// in the PRESENTER's window - the routing this class already encoded.
TEST_F(SlotPresentationTableTest, VisibleControllerRoutesThroughThePresenter) {
  table_.OnWindowShown(kSlot, kControllerA);
  ASSERT_EQ(table_.OnActivate(kSlot, kControllerB, true, true).action,
            ActivateAction::kInWindowSwap);

  DisplayRouting r = table_.ResolveDisplayRouting(kSlot, kControllerB,
                                                  /*presenter_live=*/true,
                                                  /*visible_live=*/true);

  EXPECT_EQ(r.display, kControllerA);
}

// A controller that is NOT its slot's visible one shows its own tab in its own
// (off-screen) window. This is the attach that creates the first, invisible
// holder in the crash sequence.
//
// Narrowed: it used to be driven with kControllerA, which after
// the swap below is the slot's PRESENTER - so it asserted that the presenter
// displays its own content in its own on-screen window, which is precisely the
// eviction bug (see PresenterThatIsNotVisibleDisplaysNothing). The property it
// was written for - "not visible means your own window" - is real for a
// genuinely background controller, so it is driven with one here.
TEST_F(SlotPresentationTableTest, NonVisibleControllerRoutesToItsOwnWindow) {
  table_.OnWindowShown(kSlot, kControllerA);
  table_.OnActivate(kSlot, kControllerB, true, true);

  DisplayRouting r =
      table_.ResolveDisplayRouting(kSlot, kControllerC,
                                   /*presenter_live=*/true,
                                   /*visible_live=*/true);

  EXPECT_EQ(r.display, kControllerC);
}

// THE CRASH. kControllerB's tab-strip events first route to B's own window
// (B is not visible yet), then to the presenter A once B becomes visible. The
// second routing must name B as stale: B's own pane still holds that
// WebContents, and unless it is released first, A's attach leaves two
// views::WebViews hosting one WebContents and the next detach aborts.
TEST_F(SlotPresentationTableTest,
       RoutingFlipToPresenterNamesTheControllersOwnWindowStale) {
  table_.OnWindowShown(kSlot, kControllerA);
  // B is in the slot's pool but not visible: its events land on its own window.
  ASSERT_EQ(table_.ResolveDisplayRouting(kSlot, kControllerB, true, true).display,
            kControllerB);

  // The cross-profile space switch. OnActivate writes visible_ synchronously,
  // so the very next tab-strip event for B routes somewhere else.
  ASSERT_EQ(table_.OnActivate(kSlot, kControllerB, true, true).action,
            ActivateAction::kInWindowSwap);

  DisplayRouting r = table_.ResolveDisplayRouting(kSlot, kControllerB,
                                                  /*presenter_live=*/true,
                                                  /*visible_live=*/true);

  EXPECT_EQ(r.display, kControllerA);
  EXPECT_EQ(r.stale, kControllerB)
      << "the controller's own window still holds the WebContents from before "
         "the flip and must release it before the presenter attaches";
}

// The reverse flip, which the crash log's repeated back-and-forth toggling
// also exercises: once B stops being visible its events go back to its own
// window, and now the PRESENTER is the one still holding the WebContents.
TEST_F(SlotPresentationTableTest,
       RoutingFlipBackToOwnWindowNamesThePresenterStale) {
  table_.OnWindowShown(kSlot, kControllerA);
  table_.OnActivate(kSlot, kControllerB, true, true);
  ASSERT_EQ(table_.ResolveDisplayRouting(kSlot, kControllerB, true, true).display,
            kControllerA);

  // Switch back to A's space. A is the presenter, so it becomes visible again
  // and B's events stop routing through it.
  table_.OnActivate(kSlot, kControllerA, true, true);

  DisplayRouting r = table_.ResolveDisplayRouting(kSlot, kControllerB,
                                                  /*presenter_live=*/true,
                                                  /*visible_live=*/true);

  EXPECT_EQ(r.display, kControllerB);
  EXPECT_EQ(r.stale, kControllerA)
      << "the presenter's pane still holds the WebContents it was showing "
         "while B was visible";
}

// A dead presenter must not be reported as a holder to release - the caller
// would resolve it to a torn-down window.
TEST_F(SlotPresentationTableTest, DeadPresenterIsNeverReportedStale) {
  table_.OnWindowShown(kSlot, kControllerA);
  table_.OnActivate(kSlot, kControllerB, true, true);

  DisplayRouting r = table_.ResolveDisplayRouting(kSlot, kControllerB,
                                                  /*presenter_live=*/false,
                                                  /*visible_live=*/true);

  EXPECT_EQ(r.display, kControllerB);
  EXPECT_EQ(r.stale, kInvalidControllerId);
}

// ---------------------------------------------------------------------------
// the third routing state - presenter but NOT visible.
//
// The old rule split the world in two ("am I the visible one?") and routed
// everything else to the controller's own window. That misses the state a
// cross-profile swap leaves behind for the OUTGOING controller: it is still
// the slot's PRESENTER, so its own NSWindow is the one physically on screen -
// showing the INCOMING controller's content. Routing its own tab-strip updates
// into that window does not double-embed anything (so nothing aborts), it just
// silently evicts the tab the user is actually looking at.
// ---------------------------------------------------------------------------

// THE GAP. A is the presenter, B is visible: A's window is on screen showing
// B's tab. A's own content is attached nowhere, so A's tab-strip updates must
// be displayed NOWHERE - naming A here hands its content to the pane the user
// is watching.
TEST_F(SlotPresentationTableTest, PresenterThatIsNotVisibleDisplaysNothing) {
  table_.OnWindowShown(kSlot, kControllerA);
  ASSERT_EQ(table_.OnActivate(kSlot, kControllerB, true, true).action,
            ActivateAction::kInWindowSwap);

  DisplayRouting r = table_.ResolveDisplayRouting(kSlot, kControllerA,
                                                  /*presenter_live=*/true,
                                                  /*visible_live=*/true);

  EXPECT_EQ(r.display, kInvalidControllerId)
      << "A's own window is the slot's on-screen presenter and is showing B's "
         "tab - attaching A's content there evicts it";
  EXPECT_EQ(r.stale, kInvalidControllerId)
      << "nothing attaches, so nothing needs releasing first";
}

// The genuinely-background case must keep the old behavior: a controller
// that is neither visible nor presenter owns an off-screen window of its own,
// and priming it is both harmless and what makes a later activation instant.
TEST_F(SlotPresentationTableTest, BackgroundControllerStillUsesItsOwnWindow) {
  table_.OnWindowShown(kSlot, kControllerA);
  table_.OnActivate(kSlot, kControllerB, true, true);

  DisplayRouting r = table_.ResolveDisplayRouting(kSlot, kControllerC,
                                                  /*presenter_live=*/true,
                                                  /*visible_live=*/true);

  EXPECT_EQ(r.display, kControllerC);
  EXPECT_EQ(r.stale, kControllerA);
}

// A controller is never its own stale holder: releasing there would detach the
// very pane about to attach.
//
// scoped to routings that actually name a display window. The
// "displayed nowhere" answer sets BOTH fields to kInvalidControllerId, which
// trivially compares equal without meaning "release the pane you are attaching
// into" - there is no attach at all (see NotDisplayedPlanTouchesNoPane).
TEST_F(SlotPresentationTableTest, StaleIsNeverTheDisplayWindow) {
  table_.OnWindowShown(kSlot, kControllerA);
  table_.OnActivate(kSlot, kControllerB, true, true);

  for (ControllerId self : {kControllerA, kControllerB, kControllerC}) {
    DisplayRouting r = table_.ResolveDisplayRouting(kSlot, self, true, true);
    if (r.display == kInvalidControllerId) {
      continue;
    }
    EXPECT_NE(r.stale, r.display) << "self=" << self;
  }
}

// ---------------------------------------------------------------------------
// MakePaneAttachPlan - the CALLER-SIDE glue in
// BrowserController::OnTabStripModelChanged.
//
// ResolveDisplayRouting's tests above cover which windows are candidates. These
// cover what the caller then does with that: whether it releases at all, and in
// which ORDER. The caller executes plan.steps directly, so an inverted sequence
// here is an inverted sequence in production.
// ---------------------------------------------------------------------------

// Builds the inputs for the crash case: a flip that leaves kControllerB's own
// pane holding the contents while kControllerA is about to attach it.
PaneAttachInputs FlipInputs() {
  PaneAttachInputs in;
  in.routing = {/*display=*/kControllerA, /*stale=*/kControllerB};
  in.self = kControllerB;
  in.display_live = true;
  in.stale_live = true;
  in.has_contents = true;
  in.stale_shares_display_window = false;
  return in;
}

// THE ORDERING. Release must precede attach: attaching first repoints the
// WebContents' single views_host_ at the new pane, and the release then nulls it
// out from under the window now showing the tab.
TEST(PaneAttachPlanTest, ReleasePrecedesAttach) {
  const PaneAttachPlan plan = MakePaneAttachPlan(FlipInputs());

  ASSERT_EQ(plan.steps.size(), 2u);
  EXPECT_EQ(plan.steps[0], PaneStep::kRelease)
      << "releasing after the attach nulls views_host_ under the live pane";
  EXPECT_EQ(plan.steps[1], PaneStep::kAttach);
  EXPECT_EQ(plan.attach_to, kControllerA);
  EXPECT_EQ(plan.release_from, kControllerB);
}

// No stale holder: attach only, and nothing to release.
TEST(PaneAttachPlanTest, NoStaleHolderAttachesOnly) {
  PaneAttachInputs in = FlipInputs();
  in.routing.stale = kInvalidControllerId;

  const PaneAttachPlan plan = MakePaneAttachPlan(in);

  EXPECT_EQ(plan.steps, std::vector<PaneStep>{PaneStep::kAttach});
  EXPECT_EQ(plan.release_from, kInvalidControllerId);
}

// A null new_contents (the last tab closing) has nothing to release - the
// attach IS the detach, and ReleaseDisplayedContents would be handed nullptr.
TEST(PaneAttachPlanTest, NullContentsNeverReleases) {
  PaneAttachInputs in = FlipInputs();
  in.has_contents = false;

  const PaneAttachPlan plan = MakePaneAttachPlan(in);

  EXPECT_EQ(plan.steps, std::vector<PaneStep>{PaneStep::kAttach});
  EXPECT_EQ(plan.release_from, kInvalidControllerId);
}

// A torn-down stale controller must not be released - the caller would
// dereference a dead window.
TEST(PaneAttachPlanTest, DeadStaleControllerIsNeverReleased) {
  PaneAttachInputs in = FlipInputs();
  in.stale_live = false;

  const PaneAttachPlan plan = MakePaneAttachPlan(in);

  EXPECT_EQ(plan.steps, std::vector<PaneStep>{PaneStep::kAttach});
  EXPECT_EQ(plan.release_from, kInvalidControllerId);
}

// THE GUARD. If both controllers resolve to one AtriumBrowserWindow, releasing
// would detach the very pane about to attach - blanking live content.
TEST(PaneAttachPlanTest, SharedWindowIsNeverReleased) {
  PaneAttachInputs in = FlipInputs();
  in.stale_shares_display_window = true;

  const PaneAttachPlan plan = MakePaneAttachPlan(in);

  EXPECT_EQ(plan.steps, std::vector<PaneStep>{PaneStep::kAttach})
      << "releasing the display window detaches the pane we are attaching into";
  EXPECT_EQ(plan.release_from, kInvalidControllerId);
}

// A dead display controller falls back to `self`'s own window rather than
// dropping the attach, which would strand the pane on the previous tab.
TEST(PaneAttachPlanTest, DeadDisplayFallsBackToSelf) {
  PaneAttachInputs in = FlipInputs();
  in.display_live = false;

  const PaneAttachPlan plan = MakePaneAttachPlan(in);

  EXPECT_EQ(plan.attach_to, kControllerB);
}

// The fallback above can land `attach_to` on `self` while `stale` also names
// `self`. Releasing then detaches the pane being attached - the same hazard as
// SharedWindowIsNeverReleased, reached through liveness rather than pointers.
TEST(PaneAttachPlanTest, FallbackNeverReleasesTheWindowItAttachesInto) {
  PaneAttachInputs in;
  in.routing = {/*display=*/kControllerA, /*stale=*/kControllerB};
  in.self = kControllerB;
  in.display_live = false;  // forces attach_to = self = kControllerB
  in.stale_live = true;
  in.has_contents = true;

  const PaneAttachPlan plan = MakePaneAttachPlan(in);

  EXPECT_EQ(plan.attach_to, kControllerB);
  EXPECT_EQ(plan.release_from, kInvalidControllerId);
  EXPECT_EQ(plan.steps, std::vector<PaneStep>{PaneStep::kAttach});
}

// "displayed nowhere" (ResolveDisplayRouting returning an invalid
// display) must produce a plan that touches NO pane. Falling back to `self`
// here - which is what the dead-display fallback above does - would attach into
// the on-screen presenter pane and evict the visible controller's tab.
TEST(PaneAttachPlanTest, NotDisplayedPlanTouchesNoPane) {
  PaneAttachInputs in;
  in.routing = {/*display=*/kInvalidControllerId,
                /*stale=*/kInvalidControllerId};
  in.self = kControllerA;
  in.display_live = false;
  in.stale_live = false;
  in.has_contents = true;

  const PaneAttachPlan plan = MakePaneAttachPlan(in);

  EXPECT_TRUE(plan.steps.empty())
      << "a controller whose content is displayed nowhere must not attach";
  EXPECT_EQ(plan.attach_to, kInvalidControllerId);
  EXPECT_EQ(plan.release_from, kInvalidControllerId);
}

// Every plan attaches exactly once: a plan that skipped the attach would leave
// the pane showing the wrong tab, and one that attached twice would re-enter
// ShowTab's damage-heal scheduling.
TEST(PaneAttachPlanTest, EveryPlanAttachesExactlyOnce) {
  for (bool display_live : {true, false}) {
    for (bool stale_live : {true, false}) {
      for (bool has_contents : {true, false}) {
        for (bool shared : {true, false}) {
          PaneAttachInputs in = FlipInputs();
          in.display_live = display_live;
          in.stale_live = stale_live;
          in.has_contents = has_contents;
          in.stale_shares_display_window = shared;

          const PaneAttachPlan plan = MakePaneAttachPlan(in);

          EXPECT_EQ(std::count(plan.steps.begin(), plan.steps.end(),
                               PaneStep::kAttach),
                    1)
              << "display_live=" << display_live << " stale_live=" << stale_live
              << " has_contents=" << has_contents << " shared=" << shared;
        }
      }
    }
  }
}

// End-to-end through both halves: the table decides the flip, the plan turns it
// into release-then-attach. This is the sequence that aborted on 2026-07-26.
TEST_F(SlotPresentationTableTest, FlipProducesAReleaseThenAttachPlan) {
  table_.OnWindowShown(kSlot, kControllerA);
  ASSERT_EQ(table_.ResolveDisplayRouting(kSlot, kControllerB, true, true).display,
            kControllerB);
  table_.OnActivate(kSlot, kControllerB, true, true);

  const DisplayRouting routing =
      table_.ResolveDisplayRouting(kSlot, kControllerB, true, true);

  PaneAttachInputs in;
  in.routing = routing;
  in.self = kControllerB;
  in.display_live = true;
  in.stale_live = true;
  in.has_contents = true;
  const PaneAttachPlan plan = MakePaneAttachPlan(in);

  EXPECT_EQ(plan.attach_to, kControllerA);
  EXPECT_EQ(plan.release_from, kControllerB);
  EXPECT_EQ(plan.steps, (std::vector<PaneStep>{PaneStep::kRelease,
                                               PaneStep::kAttach}));
}

// ---------------------------------------------------------------------------
// the id-level decision behind PresentedWindowForBrowser.
//
// SetSplitOp/UpdateSplitOp/CloseSplitOp already route through
// PresentedWindowForBrowser(browser)'s presenter resolution rather than the
// calling controller's own window(); the split-collapse and extension-pill
// refresh sites in OnTabStripModelChanged/OnActiveTabNavigated previously did
// not, and silently read/wrote split_primary_tab_id_/split_secondary_tab_id_/
// extension_pill_widget_ on the wrong (off-screen) AtriumBrowserWindow
// instance. PresentedWindowForBrowser's own body is two lines around
// ResolvePresenter: `presenter_id ? ControllerOf(*presenter_id)->window()
// : nullptr`, with callers falling back to their own window() on nullptr -
// this pins the ResolvePresenter half of that decision, which is the only
// half expressible without a live Browser/AtriumBrowserWindow. It cannot pin
// the ControllerOf/->window() dereference or the null-fallback wiring itself
// - those need a live BrowserController pair, which is a browser_test, not a
// unit test (see the class doc above for why this file stays pure-logic).
// ---------------------------------------------------------------------------

// The presenter the fix must route to is a DIFFERENT controller than the one
// whose tab-strip event is being handled: after the cross-profile swap, B is
// the controller asking (its own OnTabStripModelChanged is firing), but split
// state and the extension pill live on A's AtriumBrowserWindow - the one
// physically on screen. Resolving self (B) instead of the presenter (A) is
// exactly the bug being pinned.
TEST_F(SlotPresentationTableTest,
       PresenterResolutionDiffersFromTheAskingController) {
  table_.OnWindowShown(kSlot, kControllerA);
  ASSERT_EQ(table_.OnActivate(kSlot, kControllerB, true, true).action,
            ActivateAction::kInWindowSwap);

  const std::optional<ControllerId> presenter = table_.ResolvePresenter(kSlot);

  ASSERT_TRUE(presenter.has_value());
  EXPECT_EQ(*presenter, kControllerA);
  EXPECT_NE(*presenter, kControllerB)
      << "B is asking, but split/pill state lives on A's on-screen window";
}

// The null-fallback case every PresentedWindowForBrowser call site depends
// on: when nothing presents the slot, resolution yields nullopt, and
// production falls back to the asking controller's own window() rather than
// dereferencing a missing presenter.
TEST_F(SlotPresentationTableTest,
       PresenterResolutionIsNulloptWhenNothingPresentsTheSlot) {
  EXPECT_EQ(table_.ResolvePresenter(kSlot), std::nullopt);
}

}  // namespace
}  // namespace atrium
