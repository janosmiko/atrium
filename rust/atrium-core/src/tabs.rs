// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Which live Chromium tab is showing which persisted row.
//!
//! Ported from `SpacesViewModel.tabBinding`.
//!
//! WHY A BINDING AT ALL. `SpaceTab` has no id in the file, so the only join
//! between a live tab and a persisted row is the URL - and a URL is not unique.
//! Two blank tabs, or two tabs on one site, resolve to the same row, and a
//! close then deletes whichever one `find` reached first. The binding is what
//! makes the second close remove the second row.
//!
//! It is keyed by the row's [`spaces::RowToken`], not its index, because every
//! sidebar gesture moves indices: a reorder, a pin, a cross-space move. An
//! index-keyed binding survives until the user drags something, and then
//! quietly points at a neighbour.
//!
//! Runtime state, never persisted, and it lives in `cache::Loaded` so it is
//! dropped with the document it points into. Bounded by the number of live
//! tabs: an open adds one entry, and a close or a window teardown removes it.

use crate::spaces::{RowToken, UNTOKENED};

/// The live-tab-to-row map for one loaded document.
///
/// A `Vec` rather than a map: a browser has tens of tabs, not thousands, and
/// the two lookups this needs - by tab id and by token - would want two maps
/// kept in step, which is a second thing to get wrong.
#[derive(Debug, Default)]
pub struct Bindings {
    entries: Vec<(u64, RowToken)>,
}

impl Bindings {
    pub fn new() -> Self {
        Self::default()
    }

    /// Points `tab_id` at `token`, replacing whatever either held.
    ///
    /// [`UNTOKENED`] is refused: a row with no identity would make every
    /// unbound row look like this tab's.
    pub fn bind(&mut self, tab_id: u64, token: RowToken) -> bool {
        if token == UNTOKENED {
            return false;
        }
        self.entries
            .retain(|(id, held)| *id != tab_id && *held != token);
        self.entries.push((tab_id, token));
        true
    }

    /// The row `tab_id` is showing.
    pub fn token_of(&self, tab_id: u64) -> Option<RowToken> {
        self.entries
            .iter()
            .find(|(id, _)| *id == tab_id)
            .map(|(_, token)| *token)
    }

    /// Whether some live tab is already showing `token`.
    pub fn is_taken(&self, token: RowToken) -> bool {
        self.entries.iter().any(|(_, held)| *held == token)
    }

    /// The live tab showing `token`, if one is.
    ///
    /// The reverse of [`Self::token_of`], and the lookup a restore needs: it
    /// rewrites rows and has to say which live tabs the rows it dropped were
    /// on.
    pub fn tab_of(&self, token: RowToken) -> Option<u64> {
        self.entries
            .iter()
            .find(|(_, held)| *held == token)
            .map(|(id, _)| *id)
    }

    /// Every (tab id, token) pair, for a join that must know which rows are
    /// shown by tabs OUTSIDE its own strip.
    pub fn iter(&self) -> impl Iterator<Item = (u64, RowToken)> + '_ {
        self.entries.iter().copied()
    }

    /// Forgets `tab_id`, returning the row it was showing.
    pub fn release(&mut self, tab_id: u64) -> Option<RowToken> {
        let at = self.entries.iter().position(|(id, _)| *id == tab_id)?;
        Some(self.entries.remove(at).1)
    }

    /// Moves `old_tab_id`'s row to `new_tab_id` - a discard or prerender swap,
    /// which is the same tab with a new `WebContents` and so a new id.
    ///
    /// Returns whether there was anything to carry. Afterwards `old_tab_id`
    /// holds nothing, which is what makes the close half of the swap's
    /// close-then-add echo a natural no-op instead of a deletion.
    pub fn rekey(&mut self, old_tab_id: u64, new_tab_id: u64) -> bool {
        let Some(token) = self.release(old_tab_id) else {
            return false;
        };
        self.bind(new_tab_id, token)
    }

    /// How many live tabs are bound. The map's whole size, and the bound is the
    /// point: an open adds one entry, and a close or a forget removes it.
    pub fn len(&self) -> usize {
        self.entries.len()
    }

    pub fn is_empty(&self) -> bool {
        self.entries.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_tab_resolves_to_the_row_it_was_bound_to() {
        let mut bindings = Bindings::new();
        assert!(bindings.bind(7, 100));
        assert_eq!(bindings.token_of(7), Some(100));
        assert_eq!(bindings.token_of(8), None);
        assert!(bindings.is_taken(100));
        assert!(!bindings.is_taken(101));
    }

    /// A row with no identity must not become "the row every unbound tab is
    /// showing".
    #[test]
    fn an_untokened_row_cannot_be_bound() {
        let mut bindings = Bindings::new();
        assert!(!bindings.bind(7, UNTOKENED));
        assert_eq!(bindings.token_of(7), None);
    }

    /// Two live tabs showing one row is the state the whole binding exists to
    /// prevent - the second close would delete a row the first is still on.
    #[test]
    fn a_row_is_shown_by_at_most_one_tab() {
        let mut bindings = Bindings::new();
        bindings.bind(7, 100);
        bindings.bind(8, 100);
        assert_eq!(bindings.token_of(7), None);
        assert_eq!(bindings.token_of(8), Some(100));
        assert_eq!(bindings.len(), 1);
    }

    #[test]
    fn releasing_hands_back_the_row_and_frees_it() {
        let mut bindings = Bindings::new();
        bindings.bind(7, 100);
        assert_eq!(bindings.release(7), Some(100));
        assert!(!bindings.is_taken(100));
        assert_eq!(bindings.release(7), None);
    }

    /// The discard/prerender swap. The old id must end up holding nothing, so
    /// its close echo deletes no row.
    #[test]
    fn a_replaced_tab_carries_its_row_to_the_new_id() {
        let mut bindings = Bindings::new();
        bindings.bind(7, 100);
        assert!(bindings.rekey(7, 8));
        assert_eq!(bindings.token_of(8), Some(100));
        assert_eq!(bindings.token_of(7), None);
        assert!(!bindings.rekey(9, 10), "an unbound source carries nothing");
    }
}
