// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/slot_presentation_table.h
#ifndef CHROME_BROWSER_UI_ATRIUM_SLOT_PRESENTATION_TABLE_H_
#define CHROME_BROWSER_UI_ATRIUM_SLOT_PRESENTATION_TABLE_H_

#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace atrium {

// Opaque identity for a BrowserController, so this table is unit-testable
// without real controllers. Production passes the controller's pointer bits;
// tests pass small integers. The table never dereferences it.
using ControllerId = uint64_t;

// No controller. Matches the default-constructed nullptr that operator[]
// yielded in the pointer-keyed maps this class replaced
// (reinterpret_cast<uintptr_t>(nullptr) == 0).
inline constexpr ControllerId kInvalidControllerId = 0;

using SlotId = uint64_t;

// Which branch OnActivate took, so the caller can run the matching Cocoa ops.
enum class ActivateAction {
  // target is already the slot's visible content; the caller only re-confirms
  // to Swift and touches no windows.
  kAlreadyVisible,
  // First activation (or the prior visible controller is gone): target's own
  // window becomes both the shown content AND the physical presenter.
  kFirstActivation,
  // Cross-profile in-window swap: target's pane is shown inside the presenter's
  // already-on-screen window; the presenter is unchanged.
  kInWindowSwap,
};

struct ActivateResult {
  ActivateAction action;
  ControllerId visible;    // The slot's visible controller after the call.
  ControllerId presenter;  // The controller whose window presents the slot.
};

// Where a controller's own tab-strip update must be displayed, and which OTHER
// window may still be holding that WebContents from an earlier routing
// decision. See ResolveDisplayRouting.
struct DisplayRouting {
  // Whose window should show the tab.
  //
  // kInvalidControllerId means NO window shows this controller's
  // content right now, so the caller must not attach anywhere. That is a real
  // routing answer, not a failure - see ResolveDisplayRouting's third state.
  // It is distinguishable from "the display controller turned out dead"
  // (which falls back to `self`) precisely because a live controller's id is
  // never zero.
  ControllerId display;
  // Whose window may still hold it and must release it BEFORE `display`
  // attaches, or kInvalidControllerId when there is no second candidate.
  ControllerId stale;
};

// One pane operation. The caller EXECUTES PaneAttachPlan::steps in order, so
// the sequence below is the production ordering, not a description of it - a
// reordered plan reorders the real calls, which is what makes the ordering
// testable at all.
enum class PaneStep {
  // ReleaseDisplayedContents on the stale holder's window.
  kRelease,
  // ShowTab on the display window.
  kAttach,
};

// Everything BrowserController::OnTabStripModelChanged knows at the moment it
// has to decide whether to release a stale pane. Split out from the plan so the
// decision is a pure function of resolvable facts, with no Browser or views
// dependency - see MakePaneAttachPlan.
struct PaneAttachInputs {
  // The routing decision from ResolveDisplayRouting.
  DisplayRouting routing;
  // The controller whose tab strip changed - the fallback attach target.
  ControllerId self;
  // Whether routing.display resolves to a live controller (one with a Browser).
  bool display_live = false;
  // Whether routing.stale resolves to a live controller.
  bool stale_live = false;
  // False for a null new_contents (the last tab closing): there is nothing to
  // release, and the attach is the detach.
  bool has_contents = false;
  // True when the stale controller's AtriumBrowserWindow is the SAME object as
  // the display controller's. Defence in depth - ResolveDisplayRouting already
  // guarantees stale != display - but releasing there would detach the very
  // pane about to attach, so the caller resolves the pointers and says so.
  bool stale_shares_display_window = false;
};

// What the caller must actually do: which window attaches, which window (if
// any) must let go first, and in which order.
struct PaneAttachPlan {
  // Whose window attaches the contents.
  ControllerId attach_to;
  // Whose window must release it first, or kInvalidControllerId for none.
  ControllerId release_from;
  // The operations to run, in order.
  std::vector<PaneStep> steps;
};

// the caller-side half of the single-embed fix, extracted so the
// release guard and the release-before-attach ORDER are both assertable without
// a browser. Getting either wrong reintroduces the DCHECK(views_host_) abort -
// and Chromium treats one-host-at-a-time as load-bearing, not advisory
// (~WebContentsViewMac itself DCHECKs !views_host_).
PaneAttachPlan MakePaneAttachPlan(const PaneAttachInputs& inputs);

// Per-slot Presenter/Visible decision logic, lifted from the two file-local
// maps in atrium_browser_controller.cc so it can be exercised without a
// browser. "Visible" names whose CONTENT is shown in a slot; "presenter" names
// whose physical NSWindow is on screen for that slot. The two diverge only
// after a cross-profile in-window swap. The Cocoa side (window show/hide, pane
// swap, id<->controller mapping) stays in the .mm; this class only decides.
//
// Single-threaded: production drives it from the UI thread only, so no locking.
class SlotPresentationTable {
 public:
  SlotPresentationTable();
  ~SlotPresentationTable();

  SlotPresentationTable(const SlotPresentationTable&) = delete;
  SlotPresentationTable& operator=(const SlotPresentationTable&) = delete;

  // A shown window is its slot's visible content AND its physical presenter.
  // This joint seeding is deliberate: a constructor-shown window would
  // otherwise leave the presenter unseeded until a first ActivateWindow that
  // never runs (the window is already visible), so theme/Cmd+Q/resize ops
  // resolved no presenter and fell back to the wrong window.
  void OnWindowShown(SlotId slot, ControllerId id);

  // Reproduces ActivateWindow's branching. The caller resolves liveness
  // (controller && controller->browser_) for the current visible and presenter
  // and passes it in, because the table holds only opaque ids. `target` is
  // assumed live - the caller guards it, exactly as ActivateWindow does. The
  // returned visible/presenter tell the caller which window to drive.
  ActivateResult OnActivate(SlotId slot,
                            ControllerId target,
                            bool previous_visible_live,
                            bool presenter_live);

  // Decides where BrowserController::OnTabStripModelChanged must
  // show `self`'s newly-active tab, and - the part that exists to prevent the
  // views_host_ abort - which other window may still be holding that same
  // WebContents.
  //
  // Two windows are candidates for showing one controller's tab: the
  // controller's OWN window, and the slot's presenter (they differ only after
  // a cross-profile in-window swap, see OnActivate's kInWindowSwap). Which one
  // wins is re-decided on every single tab-strip event, and it FLIPS the
  // moment `self` becomes - or stops being - the slot's visible controller,
  // which OnActivate does synchronously on any cross-profile space switch.
  //
  // Each attach only detaches the pane it is handed, so across a flip the
  // previous winner's pane still holds the WebContents while the new winner
  // attaches it too. A views::WebView is a NativeViewHost and
  // WebContentsViewMac keeps a SINGLE views_host_ that ViewsHostableAttach
  // overwrites unguarded, so the loser's next detach finds it already nulled
  // and aborts on DCHECK(views_host_) - the 2026-07-26 crash. Naming the
  // loser here lets the caller release it first.
  //
  // There is a THIRD state the two-way split above missed. There are
  // not two positions a controller can be in but three:
  //   1. it IS the slot's visible one  -> the presenter's window shows it;
  //   2. it IS the slot's presenter but NOT its visible one -> its own window
  //      is on screen showing SOMEONE ELSE's content, and its own content is
  //      attached to no pane at all -> `display` is kInvalidControllerId;
  //   3. neither                       -> its own off-screen window shows it.
  // Collapsing 2 into 3 (which is what "display = self whenever visible !=
  // self" does) hands this controller's tab to the pane the user is watching
  // and silently evicts the visible controller's tab from it - single-embed,
  // so nothing aborts, but the tab you were looking at is simply replaced.
  // State 2 is not exotic: it is what EVERY cross-profile space switch leaves
  // behind for the space being switched away from.
  //
  // `presenter_live` and `visible_live` are resolved by the caller (the table
  // holds only opaque ids). A dead presenter collapses both fields onto
  // `self`; a dead VISIBLE controller means nothing is really on screen for
  // the slot but this window, so state 2 degrades back to state 3 and `self`
  // displays its own content again.
  DisplayRouting ResolveDisplayRouting(SlotId slot,
                                       ControllerId self,
                                       bool presenter_live,
                                       bool visible_live) const;

  // Backs PresentedWindowForBrowser. nullopt when nothing presents the slot
  // (no entry, or the entry was cleared on teardown).
  std::optional<ControllerId> ResolvePresenter(SlotId slot) const;
  std::optional<ControllerId> ResolveVisible(SlotId slot) const;

  // Clears the slot's visible/presenter entry, but only if it still names
  // `id` - never stomping a newer owner's entry.
  void OnTeardown(SlotId slot, ControllerId id);

  // Production keeps one process-lifetime instance; tests want a fresh table.
  void Reset();

 private:
  ControllerId VisibleOrInvalid(SlotId slot) const;
  ControllerId PresenterOrInvalid(SlotId slot) const;

  std::map<SlotId, ControllerId> visible_;
  std::map<SlotId, ControllerId> presenter_;
};

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_SLOT_PRESENTATION_TABLE_H_
