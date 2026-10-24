// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The Ctrl-Tab / Ctrl-Shift-Tab recency list.
//!
//! Nothing on the C++ side ever kept this - the Swift model did, and
//! it went away with the rest of that layer. Ctrl-Tab has
//! fallen through to stock strip cycling ever since (see
//! `atrium_command_dispatcher_delegate.mm`'s `RecencyCycleDirectionForEvent`
//! and its trailing early return, both left standing for this task to wire
//! up rather than rewrite).
//!
//! Pure, like `sidebar` and the row choice in `atrium_tab_cycle` - it takes
//! tab ids in and hands rows back out, and touches no ABI, no C++ and no
//! view. `lib.rs` owns the one process-wide instance behind the C ABI.
//!
//! # Why a held cycle, not a live re-sort
//!
//! A naive most-recent list reorders on every activation. If Ctrl-Tab both
//! activates a tab AND reorders the list on every press, two presses land
//! back where you started: press once, B jumps to the front ahead of A;
//! press again from that new order, and you're back on A. That is exactly
//! the toggle acceptance criterion #2 forbids.
//!
//! So a cycle, once started, walks a SNAPSHOT of the order taken at the
//! first press ([`TabRecencyList::step_cycle`]), and the real order does not
//! move until the cycle commits ([`TabRecencyList::end_cycle`]). Only then
//! does the tab the user landed on jump to the front.
//!
//! # How a cycle ends
//!
//! Chosen: the modifier key's release (Control, for this chord) - not a
//! timeout and not a second key.
//!
//! A timeout has no correct value: pick it too short and a deliberate pause
//! mid-walk drops the hold early: reorders the list under the user's thumb
//! and turns the next press into a fresh cycle instead of the next step of
//! the one already in progress; pick it too long and a quick tap-release-
//! retap reads as one held cycle instead of two independent taps. A second
//! key (Enter, Escape) adds a keystroke the built-in app switcher never
//! asks for and gives the user a mode to forget they are in.
//!
//! Releasing Control is what the user's hand already does to end the
//! gesture - it is the same shape as Cmd+Tab's own switcher, and it is a
//! signal the Cocoa side can already observe on `NSEventTypeFlagsChanged`
//! without adding new state to track. `lib.rs` exposes it as
//! `atrium_core_tab_recency_end_cycle`; the Objective-C++ call site is
//! documented, not wired, in this task - see the task's implementation
//! notes for why.

/// One held Ctrl-Tab / Ctrl-Shift-Tab walk.
#[derive(Debug, Clone, PartialEq)]
struct Cycle {
    /// The order at the moment the cycle started, MINUS the tab that was
    /// active then - that tab is what's already on screen, so stepping to
    /// it would look like the key did nothing while it did something (the
    /// same call `atrium_tab_cycle::NextCycleTarget` makes for a single
    /// row).
    candidates: Vec<u64>,
    /// Index into `candidates` of where the walk currently sits. `None`
    /// until the first step, so `step` on a freshly opened cycle still runs
    /// the "nothing selected yet, enter from the end you're moving towards"
    /// rule rather than a redundant zero-then-wrap.
    position: Option<usize>,
}

/// The recency list plus whatever cycle is in progress, if any.
///
/// One instance is enough for this milestone the same way `cache::Core`'s
/// loaded document is one instance - the app is a single window over a
/// single profile today.
#[derive(Debug, Clone, Default, PartialEq)]
pub struct TabRecencyList {
    /// Most-recently-activated first. `order[0]` is the active tab whenever
    /// the list is non-empty and nothing has gone stale - `record_closed`
    /// is what keeps that true.
    order: Vec<u64>,
    cycle: Option<Cycle>,
}

impl TabRecencyList {
    pub fn new() -> Self {
        Self::default()
    }

    /// Records that `tab_id` became the active tab through the ORDINARY
    /// path - a click, a new tab, anything that isn't this module's own
    /// cycle stepping.
    ///
    /// A no-op while a cycle is held: `BrowserController` activates each
    /// candidate as the user steps through it so the tab is visible, and
    /// that activation must not perturb the frozen order underneath the
    /// walk. `end_cycle` is the only thing that commits a new order once a
    /// cycle is open.
    pub fn record_activation(&mut self, tab_id: u64) {
        if self.cycle.is_some() {
            return;
        }
        self.order.retain(|&id| id != tab_id);
        self.order.insert(0, tab_id);
    }

    /// Drops `tab_id` from the list and from any cycle in progress, so a
    /// closed tab is never a target - criterion #3.
    pub fn record_closed(&mut self, tab_id: u64) {
        self.order.retain(|&id| id != tab_id);
        if let Some(cycle) = &mut self.cycle {
            if let Some(removed_idx) = cycle.candidates.iter().position(|&id| id == tab_id) {
                cycle.candidates.remove(removed_idx);
                cycle.position = match cycle.position {
                    None => None,
                    Some(_) if cycle.candidates.is_empty() => None,
                    // A removal before the current position shifts everything
                    // after it down by one; keep pointing at the same tab.
                    Some(pos) if removed_idx < pos => Some(pos - 1),
                    // The removed row WAS the current target: land on
                    // whichever row slid into its slot, clamped to the new
                    // (shorter) end.
                    Some(pos) => Some(pos.min(cycle.candidates.len() - 1)),
                };
            }
            if cycle.candidates.is_empty() {
                self.cycle = None;
            }
        }
    }

    /// Advances the held cycle one step, opening one first if none is in
    /// progress, and returns the tab id to show - or `None` when there is
    /// nowhere to go (no tabs, or only the active one).
    ///
    /// `direction` is +1 for Ctrl-Tab, -1 for Ctrl-Shift-Tab, matching
    /// `RecencyCycleDirectionForEvent`. Called again with the opposite
    /// direction mid-cycle retreats a step rather than starting over - a
    /// held Ctrl-Tab, Ctrl-Tab, Ctrl-Shift-Tab lands back where the first
    /// press did.
    pub fn step_cycle(&mut self, direction: i32) -> Option<u64> {
        if self.cycle.is_none() {
            let candidates: Vec<u64> = self.order.iter().skip(1).copied().collect();
            if candidates.is_empty() {
                return None;
            }
            self.cycle = Some(Cycle {
                candidates,
                position: None,
            });
        }
        let cycle = self.cycle.as_mut().expect("just set above");
        let len = cycle.candidates.len();
        if len == 0 {
            return None;
        }
        let next = match cycle.position {
            // Nothing selected yet: enter from the end being moved towards -
            // the same rule `NextCycleTarget` uses for a fresh walk.
            None if direction >= 0 => 0,
            None => len - 1,
            Some(pos) => {
                let signed_len = len as i64;
                let advanced = (pos as i64 + direction as i64).rem_euclid(signed_len);
                advanced as usize
            }
        };
        cycle.position = Some(next);
        Some(cycle.candidates[next])
    }

    /// Ends the held cycle, committing the tab it currently sits on to the
    /// front of the real order. A no-op when no cycle is open, or when one
    /// opened but was never stepped (a Control tap with no Tab in between).
    pub fn end_cycle(&mut self) {
        let Some(cycle) = self.cycle.take() else {
            return;
        };
        let Some(position) = cycle.position else {
            return;
        };
        let landed = cycle.candidates[position];
        self.order.retain(|&id| id != landed);
        self.order.insert(0, landed);
    }

    #[cfg(test)]
    fn order(&self) -> &[u64] {
        &self.order
    }

    #[cfg(test)]
    fn is_cycling(&self) -> bool {
        self.cycle.is_some()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn activated(ids: &[u64]) -> TabRecencyList {
        let mut list = TabRecencyList::new();
        for &id in ids {
            list.record_activation(id);
        }
        list
    }

    // Ctrl-Tab walks in most-recently-visited order.
    #[test]
    fn first_step_lands_on_the_previously_active_tab() {
        let mut list = activated(&[1, 2, 3]); // order: 3, 2, 1
        assert_eq!(list.step_cycle(1), Some(2));
    }

    #[test]
    fn backward_direction_enters_from_the_oldest_end() {
        let mut list = activated(&[1, 2, 3]); // candidates: [2, 1]
        assert_eq!(list.step_cycle(-1), Some(1));
    }

    // Repeated presses walk deeper rather than toggling between two
    // tabs, and three presses reach the third-most-recent tab.
    #[test]
    fn three_presses_reach_the_third_most_recent_tab_without_oscillating() {
        let mut list = activated(&[1, 2, 3, 4]); // active 4; candidates: [3, 2, 1]
        let first = list.step_cycle(1);
        let second = list.step_cycle(1);
        let third = list.step_cycle(1);
        assert_eq!(first, Some(3));
        assert_eq!(second, Some(2));
        assert_eq!(third, Some(1));
        // The failure mode this guards: a naive re-sort-on-every-step would
        // have made the second press equal the first (oscillation).
        assert_ne!(second, first);
        assert_ne!(third, first);
    }

    #[test]
    fn opposite_direction_mid_cycle_retreats_a_step() {
        let mut list = activated(&[1, 2, 3, 4]); // candidates: [3, 2, 1]
        assert_eq!(list.step_cycle(1), Some(3));
        assert_eq!(list.step_cycle(1), Some(2));
        assert_eq!(list.step_cycle(-1), Some(3));
    }

    #[test]
    fn cycle_wraps_at_both_ends() {
        let mut list = activated(&[1, 2, 3]); // candidates: [2, 1]
        assert_eq!(list.step_cycle(1), Some(2));
        assert_eq!(list.step_cycle(1), Some(1));
        // Wraps back to the start rather than stopping.
        assert_eq!(list.step_cycle(1), Some(2));
    }

    #[test]
    fn a_single_tab_has_nothing_to_cycle_to() {
        let mut list = activated(&[1]);
        assert_eq!(list.step_cycle(1), None);
        assert_eq!(list.step_cycle(-1), None);
    }

    #[test]
    fn an_empty_list_has_nothing_to_cycle_to() {
        let mut list = TabRecencyList::new();
        assert_eq!(list.step_cycle(1), None);
    }

    // Cycle-hold behaviour: activation calls made while a cycle is open
    // (BrowserController showing each candidate as the user steps through
    // it) must not perturb the order the cycle is walking.
    #[test]
    fn activation_during_a_held_cycle_does_not_reorder_the_list() {
        let mut list = activated(&[1, 2, 3]); // order: 3, 2, 1
        assert_eq!(list.step_cycle(1), Some(2));
        // BrowserController shows tab 2, which fires the ordinary
        // activation-observed path.
        list.record_activation(2);
        assert_eq!(list.order(), &[3, 2, 1], "order must stay frozen mid-cycle");
        // The walk continues from where it was, not from a reordered list.
        assert_eq!(list.step_cycle(1), Some(1));
    }

    #[test]
    fn ending_the_cycle_commits_the_landed_tab_to_the_front() {
        let mut list = activated(&[1, 2, 3]); // order: 3, 2, 1
        list.step_cycle(1); // lands on 2
        list.step_cycle(1); // lands on 1
        list.end_cycle();
        assert_eq!(list.order(), &[1, 3, 2]);
        assert!(!list.is_cycling());
    }

    #[test]
    fn ending_an_unstepped_cycle_changes_nothing() {
        let mut list = activated(&[1, 2, 3]);
        list.end_cycle(); // no cycle open at all
        assert_eq!(list.order(), &[3, 2, 1]);
    }

    #[test]
    fn a_fresh_cycle_after_commit_starts_from_the_new_order() {
        let mut list = activated(&[1, 2, 3]); // order: 3, 2, 1
        list.step_cycle(1); // lands on 2
        list.end_cycle(); // order becomes 2, 3, 1
        assert_eq!(list.step_cycle(1), Some(3));
    }

    // A closed tab leaves the list rather than being walked to.
    #[test]
    fn closing_a_tab_removes_it_from_a_future_cycle() {
        let mut list = activated(&[1, 2, 3]); // order: 3, 2, 1
        list.record_closed(2);
        assert_eq!(list.step_cycle(1), Some(1));
    }

    #[test]
    fn closing_the_tab_a_live_cycle_is_sitting_on_moves_past_it() {
        let mut list = activated(&[1, 2, 3, 4]); // candidates: [3, 2, 1]
        assert_eq!(list.step_cycle(1), Some(3));
        list.record_closed(3);
        // The step already landed on 3; the next step must not be able to
        // walk back to it - it is gone from the candidate list entirely.
        assert_eq!(list.step_cycle(1), Some(1));
    }

    #[test]
    fn closing_every_candidate_ends_the_cycle_instead_of_stalling() {
        let mut list = activated(&[1, 2]); // candidates: [1]
        list.step_cycle(1);
        list.record_closed(1);
        assert!(!list.is_cycling());
        // A fresh cycle over what remains still works.
        assert_eq!(list.step_cycle(1), None);
    }

    #[test]
    fn closing_the_active_tab_does_not_crash_a_future_cycle() {
        let mut list = activated(&[1, 2, 3]); // order: 3, 2, 1
        list.record_closed(3);
        assert_eq!(list.order(), &[2, 1]);
        assert_eq!(list.step_cycle(1), Some(1));
    }
}
