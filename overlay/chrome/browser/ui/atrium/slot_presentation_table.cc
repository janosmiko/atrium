// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/slot_presentation_table.cc
#include "chrome/browser/ui/atrium/slot_presentation_table.h"

namespace atrium {

SlotPresentationTable::SlotPresentationTable() = default;
SlotPresentationTable::~SlotPresentationTable() = default;

ControllerId SlotPresentationTable::VisibleOrInvalid(SlotId slot) const {
  auto it = visible_.find(slot);
  return it == visible_.end() ? kInvalidControllerId : it->second;
}

ControllerId SlotPresentationTable::PresenterOrInvalid(SlotId slot) const {
  auto it = presenter_.find(slot);
  return it == presenter_.end() ? kInvalidControllerId : it->second;
}

void SlotPresentationTable::OnWindowShown(SlotId slot, ControllerId id) {
  visible_[slot] = id;
  presenter_[slot] = id;
}

ActivateResult SlotPresentationTable::OnActivate(SlotId slot,
                                                 ControllerId target,
                                                 bool previous_visible_live,
                                                 bool presenter_live) {
  const ControllerId previous = VisibleOrInvalid(slot);

  if (previous == target) {
    // Already visible; the caller just re-confirms to Swift. No mutation.
    return {ActivateAction::kAlreadyVisible, target, PresenterOrInvalid(slot)};
  }

  if (previous == kInvalidControllerId || !previous_visible_live) {
    // First activation, or the previously-visible controller is torn down:
    // target's own window becomes both the shown content and the presenter.
    visible_[slot] = target;
    presenter_[slot] = target;
    return {ActivateAction::kFirstActivation, target, target};
  }

  // In-window swap: the presenter's NSWindow stays exactly where it is; only
  // the pane content changes. Resolve through the presenter entry, falling
  // back to `previous` if it is missing or dead - defensive, since the
  // first-activation branch always seeds it.
  ControllerId presenter = PresenterOrInvalid(slot);
  if (presenter == kInvalidControllerId || !presenter_live) {
    presenter = previous;
  }
  visible_[slot] = target;
  // presenter_ intentionally untouched: no NSWindow was shown, hidden, or
  // moved, so the physical presenter is unchanged.
  //
  // this stickiness - presenter_ only ever changes on presenter
  // death (the `!presenter_live` branch above), never on an ordinary swap -
  // is load-bearing for atrium_browser_controller.cc's peek/split routing.
  // OpenPeek/EnterSplit resolve PresentedWindowForBrowser once and the
  // matching ClosePeek/CloseSplit call resolves it again later, trusting
  // both calls land on the SAME window because nothing in between can have
  // moved the presenter. A future re-presentation feature that reassigns
  // presenter_ mid-lifecycle (e.g. explicitly promoting a different window)
  // would break that assumption; such a feature must thread the resolved
  // window through open->close instead of re-resolving at close time.
  return {ActivateAction::kInWindowSwap, target, presenter};
}

DisplayRouting SlotPresentationTable::ResolveDisplayRouting(
    SlotId slot,
    ControllerId self,
    bool presenter_live,
    bool visible_live) const {
  const ControllerId presenter = PresenterOrInvalid(slot);
  const ControllerId visible = VisibleOrInvalid(slot);
  const bool presenter_usable =
      presenter_live && presenter != kInvalidControllerId && presenter != self;
  // State 2: `self` presents the slot but is not its visible one, so
  // its own on-screen window is showing `visible`'s content and `self`'s own
  // content is attached to no pane. Nothing may be attached anywhere - see
  // the header's three-state doc. Requires `visible` to be a live OTHER
  // controller: a torn-down one (OnTeardown can clear `visible_` while
  // `presenter_` still names `self`, and a controller can lose its Browser
  // long before that) leaves nothing else on screen, so `self` legitimately
  // displays its own content again.
  const bool presents_someone_else =
      presenter == self && presenter_live && visible != kInvalidControllerId &&
      visible != self && visible_live;
  if (presents_someone_else) {
    // No attach, hence nothing to release first.
    return {kInvalidControllerId, kInvalidControllerId};
  }
  // Route through the presenter only while `self` is the slot's visible
  // controller - otherwise `self`'s content is not what this slot is showing,
  // and its own window is where its tab strip belongs.
  const ControllerId display =
      (visible == self && presenter_usable) ? presenter : self;
  // The OTHER candidate is the stale holder: whichever of {self's own window,
  // the presenter} is not displaying now was displaying before the last flip,
  // and its pane still holds the WebContents. Symmetric by construction, so
  // neither direction of the flip can be forgotten, and `stale` is never
  // `display` - releasing there would detach the pane about to attach.
  const ControllerId stale =
      (display == self)
          ? (presenter_usable ? presenter : kInvalidControllerId)
          : self;
  return {display, stale};
}

PaneAttachPlan MakePaneAttachPlan(const PaneAttachInputs& inputs) {
  PaneAttachPlan plan;
  // "displayed nowhere" (see DisplayRouting::display) is an EMPTY
  // plan, and it has to be checked before the dead-display fallback below -
  // that fallback would otherwise turn "attach into no pane" into "attach into
  // self's own pane", which in this state is the slot's on-screen presenter
  // pane showing another controller's tab. Skipping the attach strands nothing:
  // re-activating this controller runs ActivateWindow's kInWindowSwap, which
  // ShowTab's its current active tab into that same pane at that point.
  if (inputs.routing.display == kInvalidControllerId) {
    plan.attach_to = kInvalidControllerId;
    plan.release_from = kInvalidControllerId;
    return plan;
  }
  // A dead display controller falls back to `self`'s own window: the caller has
  // a window to attach into either way, and dropping the attach would leave the
  // pane showing the previous tab.
  plan.attach_to =
      inputs.display_live ? inputs.routing.display : inputs.self;
  plan.release_from = kInvalidControllerId;

  const bool releasable = inputs.has_contents &&
                          inputs.routing.stale != kInvalidControllerId &&
                          inputs.stale_live &&
                          !inputs.stale_shares_display_window &&
                          // Never release the window we are about to attach
                          // into. Reachable via the dead-display fallback
                          // above, which can land `attach_to` on `self` while
                          // `routing.stale` also names `self`.
                          inputs.routing.stale != plan.attach_to;
  if (releasable) {
    plan.release_from = inputs.routing.stale;
    // ORDER IS THE POINT. Releasing first leaves the WebContents' single
    // views_host_ to be repointed cleanly by the attach; attaching first
    // repoints it and then the release nulls it out from under the window that
    // is now showing the tab - the abort this whole task exists to remove.
    plan.steps = {PaneStep::kRelease, PaneStep::kAttach};
  } else {
    plan.steps = {PaneStep::kAttach};
  }
  return plan;
}

std::optional<ControllerId> SlotPresentationTable::ResolvePresenter(
    SlotId slot) const {
  const ControllerId id = PresenterOrInvalid(slot);
  if (id == kInvalidControllerId) {
    return std::nullopt;
  }
  return id;
}

std::optional<ControllerId> SlotPresentationTable::ResolveVisible(
    SlotId slot) const {
  const ControllerId id = VisibleOrInvalid(slot);
  if (id == kInvalidControllerId) {
    return std::nullopt;
  }
  return id;
}

void SlotPresentationTable::OnTeardown(SlotId slot, ControllerId id) {
  if (VisibleOrInvalid(slot) == id) {
    visible_[slot] = kInvalidControllerId;
  }
  if (PresenterOrInvalid(slot) == id) {
    presenter_[slot] = kInvalidControllerId;
  }
}

void SlotPresentationTable::Reset() {
  visible_.clear();
  presenter_.clear();
}

}  // namespace atrium
