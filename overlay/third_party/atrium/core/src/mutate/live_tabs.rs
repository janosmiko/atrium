// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The bridge between a live Chromium tab and its persisted row.
//!
//! A tab id cannot address a row on its own - Chromium never reuses one, and a
//! discard or a prerender activation replaces a tab's WebContents so the same
//! tab arrives with a new id. So a BINDING carries the row's token, and these
//! functions are the only ones that create, follow or drop one.
//!
//! `open_live_tab` may ADOPT a cold row rather than adding one, which is why it
//! reports whether the document moved separately from whether a binding was
//! written: adopting writes no file and is exactly the case where the next
//! close depends on the binding existing.

use super::{
    Section, active_space, archive_taken, locate_token, persisted_index_of_visible,
    persisted_insertion_index_of_visible_slot,
};
use crate::SwiftDate;
use crate::spaces::{CloseReason, Document, RowToken, Space, SpaceTab, UNTOKENED};
use crate::tabs::Bindings;

/// What opening a live tab did to the document.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Opened {
    /// It took over a row that was already there - a cold placeholder the user
    /// clicked. NOTHING moved, so the file does not need writing.
    Adopted(RowToken),
    /// A row was added for it.
    Added(RowToken),
}

/// The row a newly opened tab on `url` should take over, if the space has one
/// going spare.
///
/// PINNED FIRST, matching the Swift lookup: a cold pinned tile's URL has to
/// resolve to its pinned entry, or clicking it spawns a second regular row
/// beside the tile that is still there.
///
/// A row another live tab is already showing is skipped, which is the whole
/// reason two tabs on one site do not collapse onto one entry.
fn adoptable_row(space: &Space, url: &str, bindings: &Bindings) -> Option<RowToken> {
    space
        .pinned_tabs
        .iter()
        .chain(space.tabs.iter())
        .find(|t| t.url == url && t.token != UNTOKENED && !bindings.is_taken(t.token))
        .map(|t| t.token)
}

/// Persists a tab the browser just opened in `space`.
///
/// A new row lands on the first rendered slot. `to_slot` is the Chromium strip
/// index, which counts up from the oldest tab and would append, so it is
/// ignored. A restored tab adopts its cold row above and keeps its place.
///
/// Returns `None` only for a URL or a space that cannot carry a row. The
/// caller binds the returned token to the live tab id either way: adoption
/// changes nothing on disk but is exactly the case where the binding matters
/// most, because the next close has to delete THAT row.
pub fn open_tab(
    doc: &mut Document,
    space: usize,
    url: &str,
    title: &str,
    _to_slot: Option<usize>,
    bindings: &Bindings,
) -> Option<Opened> {
    if url.is_empty() || space >= doc.spaces.len() {
        return None;
    }
    if let Some(token) = adoptable_row(&doc.spaces[space], url, bindings) {
        return Some(Opened::Adopted(token));
    }
    let token = doc.allocate_token();
    let mut tab = SpaceTab::new(url, title);
    tab.token = token;
    let at = persisted_insertion_index_of_visible_slot(0, &doc.spaces[space], &doc.hidden_rows)
        .unwrap_or(0);
    let space = &mut doc.spaces[space];
    space.tabs.insert(at.min(space.tabs.len()), tab);
    Some(Opened::Added(token))
}

/// Appends at the END, unlike `open_tab`'s prepend, since an import adds rows
/// in source order and prepending would reverse them. A grouped row lands
/// after its group's last member.
pub fn append_cold_tab(
    doc: &mut Document,
    space: usize,
    section: Section,
    url: &str,
    title: &str,
    last_active_at: Option<SwiftDate>,
    group_id: Option<&str>,
) -> bool {
    if url.is_empty() || space >= doc.spaces.len() {
        return false;
    }
    if let Some(id) = group_id {
        let s = &doc.spaces[space];
        if !s.groups.iter().any(|g| g.id == id) {
            return false;
        }
        let elsewhere = match section {
            Section::Regular => &s.pinned_tabs,
            Section::Pinned => &s.tabs,
        };
        if elsewhere.iter().any(|t| t.group_id.as_deref() == Some(id)) {
            return false;
        }
    }
    let mut tab = SpaceTab::new(url, title);
    tab.token = doc.allocate_token();
    tab.last_active_at = last_active_at;
    tab.group_id = group_id.map(str::to_string);
    if section == Section::Pinned {
        tab.pinned_url = Some(url.to_string());
    }
    let list = match section {
        Section::Regular => &mut doc.spaces[space].tabs,
        Section::Pinned => &mut doc.spaces[space].pinned_tabs,
    };
    let at = group_id
        .and_then(|id| list.iter().rposition(|t| t.group_id.as_deref() == Some(id)))
        .map_or(list.len(), |last| last + 1);
    list.insert(at, tab);
    true
}

/// What closing a live tab did to the document.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Closed {
    /// The row is gone. A closed regular tab is NOT resurrected as a cold
    /// placeholder on the next space switch or restart.
    Removed,
    /// The row stayed, and now renders cold.
    KeptCold,
}

/// Moves the row a closing live tab was showing into the graveyard
///, instead of deleting it outright, so Restore can bring it back.
///
/// A PINNED row is kept (DATA-SAFETY INVARIANT, ported from
/// `SpacesViewModel.syncTabClosed`): a pin the user set deliberately reverts
/// to its cold tile rather than landing in the graveyard.
///
/// `None` when the token names no row - a tab that was never persisted, or one
/// whose row has already left.
pub(crate) fn close_tab(doc: &mut Document, token: RowToken, now: SwiftDate) -> Option<Closed> {
    let (space, section, index) = locate_token(doc, token)?;
    if section == Section::Pinned {
        return Some(Closed::KeptCold);
    }
    let space_id = doc.spaces[space].id.clone();
    let tab = doc.spaces[space].tabs.remove(index);
    archive_taken(doc, vec![(space_id, tab)], now, CloseReason::UserClosed);
    Some(Closed::Removed)
}

/// The x on a cold regular row. `visible` counts rendered rows. A row a live
/// tab is bound to is refused: that close is the strip's, and taking the row
/// first would leave the tab showing a row that is gone.
pub fn remove_cold_tab(
    doc: &mut Document,
    bindings: &Bindings,
    space: usize,
    visible: usize,
    now: SwiftDate,
) -> bool {
    let Some(index) = persisted_index_of_visible(
        visible,
        &doc.spaces[space],
        &doc.hidden_rows,
    ) else {
        return false;
    };
    if bindings.is_taken(doc.spaces[space].tabs[index].token) {
        return false;
    }
    let space_id = doc.spaces[space].id.clone();
    let tab = doc.spaces[space].tabs.remove(index);
    archive_taken(doc, vec![(space_id, tab)], now, CloseReason::UserClosed);
    true
}

/// The whole open gesture: find the space, take over or add a row, and record
/// which row this live tab is showing.
///
/// One function rather than three steps at each call site, because the steps
/// have to happen together - the binding is what a later close resolves through,
/// and an open that added a row without recording one leaves that row orphaned
/// for the rest of the session.
///
/// An ALREADY BOUND tab id is a no-op. That covers the defensive replay in the
/// adopt constructor, and a tab landing back in a strip it never really left;
/// both would otherwise duplicate the row the tab is already showing.
///
/// Returns whether the DOCUMENT changed, which is what decides a save.
pub fn open_live_tab(
    doc: &mut Document,
    bindings: &mut Bindings,
    active_space_index: Option<usize>,
    tab_id: u64,
    url: &str,
    title: &str,
    to_slot: Option<usize>,
) -> bool {
    if bindings.token_of(tab_id).is_some() {
        return false;
    }
    let Some(space) = active_space(doc, active_space_index) else {
        return false;
    };
    let Some(opened) = open_tab(doc, space, url, title, to_slot, bindings) else {
        return false;
    };
    bindings.bind(tab_id, opened.token());
    opened.changed_the_document()
}

/// Binds a live tab to the cold row it was restored from, adding no row.
///
/// For a favourite's claimed tab: the row hides behind the tile only through
/// a binding, and a claimed startup tab got none, so its row stayed drawn
/// beside the tile. False when the tab is bound already or no row matches.
pub fn bind_cold_row(
    doc: &Document,
    bindings: &mut Bindings,
    active_space_index: Option<usize>,
    tab_id: u64,
    url: &str,
) -> bool {
    if bindings.token_of(tab_id).is_some() {
        return false;
    }
    let Some(space) = active_space(doc, active_space_index) else {
        return false;
    };
    match adoptable_row(&doc.spaces[space], url, bindings) {
        Some(token) => bindings.bind(tab_id, token),
        None => false,
    }
}

/// Catches a row's persisted url up with the page its tab has navigated to.
///
/// The sibling of `note_page_title`, and the more load-bearing of the two. A
/// row's LIVE TAB ID is assigned by matching its persisted url against the live
/// strip's current url, exactly (`sidebar::LiveTabIds::take`). Nothing wrote the
/// url after the tab was opened, so a tab that navigated stopped matching its
/// own row - and a row with no live id cannot be drawn as selected, cannot be
/// closed by id, and its click falls through to the cold-row branch, which
/// reopens the page as a SECOND tab. One stale string, three symptoms.
///
/// Leaves both titles alone. A navigation does not know what the new page is
/// called - that arrives separately, from `TitleWasSet` - and a rename is the
/// user's and outlives any navigation.
///
/// An EMPTY url is refused rather than stored: a row with no url is a row
/// nothing can match or reopen, which is worse than a stale one.
///
/// Returns whether the document changed, so a same-document navigation
/// reporting the url it already had does not rewrite the file.
pub fn note_page_url(doc: &mut Document, bindings: &Bindings, tab_id: u64, url: &str) -> bool {
    if url.is_empty() {
        return false;
    }
    let Some(token) = bindings.token_of(tab_id) else {
        return false;
    };
    let Some((space, section, index)) = locate_token(doc, token) else {
        return false;
    };
    let list = match section {
        Section::Pinned => &mut doc.spaces[space].pinned_tabs,
        Section::Regular => &mut doc.spaces[space].tabs,
    };
    let Some(tab) = list.get_mut(index) else {
        return false;
    };
    if tab.url == url {
        return false;
    }
    tab.url = url.to_string();
    true
}

/// Catches a row's persisted page title up with the page its tab is showing.
///
/// A tab is added before its page has a title, so `open_live_tab` writes the
/// row with an empty one and nothing afterwards filled it in. The
/// sidebar model reads the title off the ROW, and the live strip it is merged
/// with carries only urls and ids, so a blank row stays blank however many
/// times the surface re-pushes.
///
/// Writes `title`, never `custom_title`: a rename is the user's and keeps
/// winning in the model, which is why this is not `rename_tab`.
///
/// Returns whether the document changed, so an unchanged title does not
/// rewrite the file on every same-title update Chromium sends.
pub fn note_page_title(doc: &mut Document, bindings: &Bindings, tab_id: u64, title: &str) -> bool {
    let Some(token) = bindings.token_of(tab_id) else {
        return false;
    };
    let Some((space, section, index)) = locate_token(doc, token) else {
        return false;
    };
    let list = match section {
        Section::Pinned => &mut doc.spaces[space].pinned_tabs,
        Section::Regular => &mut doc.spaces[space].tabs,
    };
    let Some(tab) = list.get_mut(index) else {
        return false;
    };
    if tab.title == title {
        return false;
    }
    tab.title = title.to_string();
    true
}

/// The whole close gesture: release the binding and remove the row it named.
///
/// The release happens even when the row is kept - a pinned tab's close reverts
/// it to a cold tile, and leaving the binding behind would make that tile look
/// permanently taken to the next tab that opened on the same URL.
///
/// Returns whether the DOCUMENT changed.
pub fn close_live_tab(
    doc: &mut Document,
    bindings: &mut Bindings,
    tab_id: u64,
    now: SwiftDate,
) -> bool {
    let Some(token) = bindings.release(tab_id) else {
        return false;
    };
    close_tab(doc, token, now) == Some(Closed::Removed)
}

/// Stamps `now` as the last active time of the row `tab_id` shows, so the
/// archive sweep ages the row from a real visit and not from the document's
/// migration epoch.
///
/// Returns whether the document changed. A stamp less than a minute newer
/// than the last one is skipped, so a run of tab switches does not rewrite
/// the file on each one.
pub fn touch_tab(doc: &mut Document, bindings: &Bindings, tab_id: u64, now: SwiftDate) -> bool {
    let Some(token) = bindings.token_of(tab_id) else {
        return false;
    };
    let Some((space, section, index)) = locate_token(doc, token) else {
        return false;
    };
    let s = &mut doc.spaces[space];
    let tab = match section {
        Section::Pinned => &mut s.pinned_tabs[index],
        Section::Regular => &mut s.tabs[index],
    };
    if tab
        .last_active_at
        .is_some_and(|at| now - at < TOUCH_WRITE_GAP)
    {
        return false;
    }
    tab.last_active_at = Some(now);
    true
}

/// ponytail: one write a minute per row, raise it if the file churns.
const TOUCH_WRITE_GAP: SwiftDate = 60.0;

#[cfg(test)]
mod tests {
    use super::*;
    #[allow(unused_imports)]
    use crate::mutate::test_support::*;
    use crate::spaces::assign_tokens;

    #[test]
    fn touching_a_bound_tab_stamps_its_row_once_a_minute() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("a")];
        assign_tokens(&mut doc);
        let mut bindings = Bindings::new();
        assert!(bindings.bind(7, doc.spaces[0].tabs[0].token));

        assert!(!touch_tab(&mut doc, &bindings, 8, 100.0), "an unbound tab");
        assert!(touch_tab(&mut doc, &bindings, 7, 100.0));
        assert_eq!(doc.spaces[0].tabs[0].last_active_at, Some(100.0));
        assert!(!touch_tab(&mut doc, &bindings, 7, 130.0), "inside the gap");
        assert_eq!(doc.spaces[0].tabs[0].last_active_at, Some(100.0));
        assert!(touch_tab(&mut doc, &bindings, 7, 161.0));
        assert_eq!(doc.spaces[0].tabs[0].last_active_at, Some(161.0));
    }

    /// The reason the stamp exists: a row visited after the epoch is not due
    /// on the epoch's age.
    #[test]
    fn a_touched_row_ages_from_its_visit_not_the_epoch() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("a")];
        assign_tokens(&mut doc);
        doc.app_state.archive_epoch = Some(0.0);
        let mut bindings = Bindings::new();
        assert!(bindings.bind(7, doc.spaces[0].tabs[0].token));
        let day = 24.0 * 3600.0;
        assert!(touch_tab(&mut doc, &bindings, 7, 10.0 * day));

        let due = crate::lifecycle::archive_due(&doc, "twelveHours", 10.0 * day + 3600.0);
        assert!(due.is_empty(), "one hour after the visit");
        let due = crate::lifecycle::archive_due(&doc, "twelveHours", 11.0 * day);
        assert_eq!(due.len(), 1, "a day after the visit");
    }

    /// Clicking a cold row opens a live tab on its URL. Appending a second row
    /// for it would leave the user looking at their tab twice.
    #[test]
    fn opening_a_tab_on_a_cold_rows_url_takes_over_that_row() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("cold")];
        assign_tokens(&mut doc);
        let cold = doc.spaces[0].tabs[0].token;

        let opened = open_tab(&mut doc, 0, "cold", "Cold", None, &Bindings::new()).unwrap();
        assert_eq!(opened, Opened::Adopted(cold));
        assert!(!opened.changed_the_document());
        assert_eq!(doc.spaces[0].tabs.len(), 1);
    }

    /// A cold PINNED tile has to win the lookup, or clicking it spawns a
    /// regular row beside the tile that is still sitting there.
    #[test]
    fn a_pinned_row_wins_the_lookup_over_a_regular_one() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].pinned_tabs = vec![tab("same")];
        doc.spaces[0].tabs = vec![tab("same")];
        assign_tokens(&mut doc);
        let pinned = doc.spaces[0].pinned_tabs[0].token;

        let opened = open_tab(&mut doc, 0, "same", "Same", None, &Bindings::new()).unwrap();
        assert_eq!(opened, Opened::Adopted(pinned));
    }

    /// The sidebar lets a click on a cold PINNED row reopen its page. One might
    /// expect the tab to come back as an ordinary row and lose its pinning.
    /// Adoption is what makes that false, so the claim is asserted rather than
    /// assumed.
    #[test]
    fn adopting_a_pinned_row_leaves_it_pinned() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].pinned_tabs = vec![tab("same")];
        assign_tokens(&mut doc);

        open_tab(&mut doc, 0, "same", "Same", None, &Bindings::new()).unwrap();

        assert_eq!(doc.spaces[0].pinned_tabs.len(), 1, "still pinned");
        assert!(doc.spaces[0].tabs.is_empty(), "and not also a regular row");
    }

    /// Two tabs on one site are two tabs. The row the first took is off the
    /// table, so the second gets its own.
    #[test]
    fn a_second_tab_on_the_same_url_gets_its_own_row() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("shared")];
        assign_tokens(&mut doc);

        let mut bindings = Bindings::new();
        let first = open_tab(&mut doc, 0, "shared", "One", None, &bindings).unwrap();
        bindings.bind(1, first.token());
        let second = open_tab(&mut doc, 0, "shared", "Two", None, &bindings).unwrap();
        bindings.bind(2, second.token());

        assert!(matches!(second, Opened::Added(_)));
        assert_ne!(first.token(), second.token());
        assert_eq!(doc.spaces[0].tabs.len(), 2);
    }

    /// A row's url must follow the page too, not only its title. This is the
    /// defect behind three reported symptoms at once: LiveTabIds::take matches a
    /// persisted url against the live one EXACTLY, so a row whose tab has
    /// navigated stops resolving to any live tab - and a row with no live id
    /// cannot be selected, cannot be closed, and its click falls through to the
    /// cold-row branch, which reopens the page as a second tab.
    #[test]
    fn a_navigation_moves_the_row_the_tab_is_showing() {
        let mut doc = tokened(vec![space("A", 0)]);
        let mut bindings = Bindings::new();
        assert!(open_live_tab(
            &mut doc,
            &mut bindings,
            Some(0),
            7,
            "https://start.example/",
            "Start",
            None
        ));

        assert!(note_page_url(
            &mut doc,
            &bindings,
            7,
            "https://moved.example/"
        ));
        assert_eq!(doc.spaces[0].tabs[0].url, "https://moved.example/");
    }

    /// Only the row whose tab navigated may move. A write that touched every row
    /// in the space would satisfy the file assertion in the VM scenario and be a
    /// worse defect than the one being fixed - and the harness cannot express
    /// this comparison, because the untouched row's correct end state is the
    /// value the fixture staged, which it refuses as vacuous.
    #[test]
    fn a_navigation_moves_only_the_row_its_tab_is_showing() {
        let mut doc = tokened(vec![space("A", 0)]);
        let mut bindings = Bindings::new();
        assert!(open_live_tab(
            &mut doc,
            &mut bindings,
            Some(0),
            7,
            "https://first.example/",
            "First",
            None
        ));
        assert!(open_live_tab(
            &mut doc,
            &mut bindings,
            Some(0),
            8,
            "https://second.example/",
            "Second",
            None
        ));

        assert!(note_page_url(
            &mut doc,
            &bindings,
            8,
            "https://moved.example/"
        ));
        assert_eq!(
            doc.spaces[0].tabs[1].url, "https://first.example/",
            "the row nobody navigated must not move"
        );
        assert_eq!(doc.spaces[0].tabs[0].url, "https://moved.example/");
    }

    /// The title is the user's view of the row and a url change does not know
    /// what the new page is called, so it must not blank what is there. The
    /// title's own catch-up arrives separately, from TitleWasSet.
    #[test]
    fn a_navigation_leaves_the_title_alone() {
        let mut doc = tokened(vec![space("A", 0)]);
        let mut bindings = Bindings::new();
        assert!(open_live_tab(
            &mut doc,
            &mut bindings,
            Some(0),
            7,
            "https://start.example/",
            "Start",
            None
        ));
        doc.spaces[0].tabs[0].custom_title = Some("My Name".into());

        assert!(note_page_url(
            &mut doc,
            &bindings,
            7,
            "https://moved.example/"
        ));
        assert_eq!(doc.spaces[0].tabs[0].title, "Start");
        assert_eq!(
            doc.spaces[0].tabs[0].custom_title.as_deref(),
            Some("My Name"),
            "a rename survives a navigation"
        );
    }

    /// Nothing to write is not a write. A same-document navigation reports the
    /// url it already had, and a tab with no row has nowhere to put one.
    #[test]
    fn an_unchanged_or_unbound_url_does_not_dirty_the_document() {
        let mut doc = tokened(vec![space("A", 0)]);
        let mut bindings = Bindings::new();
        assert!(open_live_tab(
            &mut doc,
            &mut bindings,
            Some(0),
            7,
            "https://same.example/",
            "Same",
            None
        ));

        assert!(
            !note_page_url(&mut doc, &bindings, 7, "https://same.example/"),
            "unchanged"
        );
        assert!(
            !note_page_url(&mut doc, &bindings, 9, "https://other.example/"),
            "unbound tab"
        );
        assert!(
            !note_page_url(&mut doc, &bindings, 7, ""),
            "an empty url would strand the row"
        );
    }

    /// a tab is added before its page has a title, so the row lands
    /// blank and the sidebar has nothing to draw. The live strip carries only
    /// urls and ids (sidebar.rs), so no re-push can supply the title either -
    /// it has to reach the document.
    #[test]
    fn a_page_title_arriving_late_reaches_the_row_the_tab_opened() {
        let mut doc = tokened(vec![space("A", 0)]);
        let mut bindings = Bindings::new();
        assert!(open_live_tab(
            &mut doc,
            &mut bindings,
            Some(0),
            7,
            "https://late.example/",
            "",
            None
        ));
        assert_eq!(doc.spaces[0].tabs[0].title, "", "opened with no title yet");

        assert!(note_page_title(&mut doc, &bindings, 7, "Late Title"));
        assert_eq!(doc.spaces[0].tabs[0].title, "Late Title");
    }

    /// The user's own rename is a different thing from the page's title and
    /// must survive one arriving. `custom_title` is what the model prefers
    /// (sidebar.rs), so writing `title` under it is invisible to the row.
    #[test]
    fn a_late_page_title_does_not_overwrite_a_rename() {
        let mut doc = tokened(vec![space("A", 0)]);
        let mut bindings = Bindings::new();
        assert!(open_live_tab(
            &mut doc,
            &mut bindings,
            Some(0),
            7,
            "https://renamed.example/",
            "",
            None
        ));
        doc.spaces[0].tabs[0].custom_title = Some("My Name".into());

        assert!(note_page_title(&mut doc, &bindings, 7, "Page Title"));
        assert_eq!(
            doc.spaces[0].tabs[0].custom_title.as_deref(),
            Some("My Name")
        );
        assert_eq!(doc.spaces[0].tabs[0].title, "Page Title");
    }

    /// Nothing to write is not a write: a title that has not moved, and a tab
    /// with no row, must both leave the file alone.
    #[test]
    fn an_unchanged_or_unbound_title_does_not_dirty_the_document() {
        let mut doc = tokened(vec![space("A", 0)]);
        let mut bindings = Bindings::new();
        assert!(open_live_tab(
            &mut doc,
            &mut bindings,
            Some(0),
            7,
            "https://same.example/",
            "Same",
            None
        ));

        assert!(
            !note_page_title(&mut doc, &bindings, 7, "Same"),
            "unchanged"
        );
        assert!(
            !note_page_title(&mut doc, &bindings, 9, "Other"),
            "unbound tab"
        );
    }

    /// The first slot counts RENDERED rows, so a group at the head of the list
    /// does not take the place the new row has to land on.
    #[test]
    fn a_new_row_lands_at_the_top_whatever_slot_it_was_given() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![grouped("g", "G"), tab("a"), tab("b")];
        assign_tokens(&mut doc);

        open_tab(&mut doc, 0, "new", "New", Some(1), &Bindings::new()).unwrap();
        assert_eq!(urls(&doc.spaces[0]), vec!["g", "new", "a", "b"]);

        open_tab(&mut doc, 0, "newer", "Newer", Some(99), &Bindings::new()).unwrap();
        assert_eq!(urls(&doc.spaces[0]), vec!["g", "newer", "new", "a", "b"]);
    }

    #[test]
    fn a_tab_with_no_url_persists_nothing() {
        let mut doc = tokened(vec![space("A", 0)]);
        assert_eq!(open_tab(&mut doc, 0, "", "", None, &Bindings::new()), None);
        assert_eq!(
            open_tab(&mut doc, 9, "u", "U", None, &Bindings::new()),
            None
        );
        assert!(doc.spaces[0].tabs.is_empty());
    }

    /// A closed regular tab is not resurrected as a cold placeholder - the
    /// rule the whole task exists to restore. It lands in the graveyard
    /// instead of vanishing.
    #[test]
    fn closing_a_regular_tab_deletes_its_row() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("a"), tab("b")];
        assign_tokens(&mut doc);
        let a = doc.spaces[0].tabs[0].token;

        assert_eq!(close_tab(&mut doc, a, 100.0), Some(Closed::Removed));
        assert_eq!(urls(&doc.spaces[0]), vec!["b"]);
        assert_eq!(close_tab(&mut doc, a, 100.0), None, "and only once");

        assert_eq!(doc.archived.len(), 1);
        assert_eq!(doc.archived[0].url, "a");
        assert_eq!(doc.archived[0].reason, CloseReason::UserClosed);
        assert_eq!(doc.archived[0].archived_at, 100.0);
    }

    /// DATA-SAFETY INVARIANT: a pinned tab's close reverts it to its cold
    /// tile. Archiving the entry would throw away a pin the user set.
    #[test]
    fn closing_a_pinned_tab_leaves_its_tile_behind() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].pinned_tabs = vec![tab("p")];
        assign_tokens(&mut doc);
        let p = doc.spaces[0].pinned_tabs[0].token;

        assert_eq!(close_tab(&mut doc, p, 100.0), Some(Closed::KeptCold));
        assert_eq!(doc.spaces[0].pinned_tabs.len(), 1);
        assert!(doc.archived.is_empty());
    }

    /// The x on a cold row. The row goes where a closed tab goes, so Restore
    /// can bring it back.
    #[test]
    fn removing_a_cold_row_archives_it() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("live"), tab("cold")];
        assign_tokens(&mut doc);
        let mut bindings = Bindings::new();
        bindings.bind(7, doc.spaces[0].tabs[0].token);

        assert!(remove_cold_tab(&mut doc, &bindings, 0, 1, 10.0));
        assert_eq!(urls(&doc.spaces[0]), ["live"]);
        assert_eq!(doc.archived.len(), 1);
        assert_eq!(doc.archived[0].url, "cold");
        assert_eq!(doc.archived[0].reason, CloseReason::UserClosed);
    }

    /// A row a live tab is bound to is the strip's to close. Taking the row
    /// from under the tab would leave the tab showing a row that is gone.
    #[test]
    fn a_row_with_a_live_tab_is_not_removed_as_cold() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("live"), tab("cold")];
        assign_tokens(&mut doc);
        let mut bindings = Bindings::new();
        bindings.bind(7, doc.spaces[0].tabs[0].token);

        assert!(!remove_cold_tab(&mut doc, &bindings, 0, 0, 10.0));
        assert_eq!(urls(&doc.spaces[0]), ["live", "cold"]);
        assert!(doc.archived.is_empty());
    }

    /// The index counts RENDERED rows, like every other regular-row gesture:
    /// a folder's rows are drawn under their folder, not in the flat list.
    #[test]
    fn a_cold_row_is_found_by_its_rendered_position() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![grouped("filed", "G"), tab("cold")];
        assign_tokens(&mut doc);

        assert!(remove_cold_tab(&mut doc, &Bindings::new(), 0, 0, 10.0));
        assert_eq!(urls(&doc.spaces[0]), ["filed"]);
        assert!(
            !remove_cold_tab(&mut doc, &Bindings::new(), 0, 0, 10.0),
            "nothing is rendered any more, so there is nothing to remove"
        );
    }

    /// A favourite's startup tab is claimed, so no row is opened for it. It
    /// still has to take the row it was restored from, or that row is drawn
    /// cold beside the tile.
    #[test]
    fn a_claimed_tab_binds_the_cold_row_it_came_from_without_adding_one() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("https://fav.test/")];
        assign_tokens(&mut doc);
        let token = doc.spaces[0].tabs[0].token;

        let mut bindings = Bindings::new();
        assert!(bind_cold_row(
            &doc,
            &mut bindings,
            Some(0),
            7,
            "https://fav.test/"
        ));
        assert_eq!(bindings.token_of(7), Some(token));
        assert_eq!(doc.spaces[0].tabs.len(), 1, "no row was added");

        assert!(
            !bind_cold_row(&doc, &mut bindings, Some(0), 7, "https://fav.test/"),
            "bound already"
        );
        assert!(
            !bind_cold_row(&doc, &mut bindings, Some(0), 8, "https://fav.test/"),
            "the row is taken"
        );
        assert!(
            !bind_cold_row(&doc, &mut bindings, Some(0), 9, "https://other.test/"),
            "no such row"
        );
    }
}

#[cfg(test)]
mod append_cold_tab_tests {
    use super::*;
    use crate::mutate::Section;
    use crate::mutate::test_support::{document, group, grouped, pinned_urls, space, tab, urls};

    fn one_space() -> Document {
        let mut s = space("A", 0);
        s.tabs = vec![tab("https://one.example.test/"), tab("https://two.example.test/")];
        document(vec![s])
    }

    #[test]
    fn a_regular_row_goes_to_the_end() {
        let mut doc = one_space();
        assert!(append_cold_tab(&mut doc, 0, Section::Regular, "https://three.example.test/", "Three", Some(5.0), None));
        assert_eq!(urls(&doc.spaces[0]), ["https://one.example.test/", "https://two.example.test/", "https://three.example.test/"]);
        assert_eq!(doc.spaces[0].tabs[2].last_active_at, Some(5.0));
        assert_eq!(doc.spaces[0].tabs[2].title, "Three");
    }

    #[test]
    fn a_pinned_row_keeps_its_url_as_its_pinned_home() {
        let mut doc = one_space();
        assert!(append_cold_tab(&mut doc, 0, Section::Pinned, "https://pin.example.test/", "Pin", None, None));
        assert_eq!(pinned_urls(&doc.spaces[0]), ["https://pin.example.test/"]);
        assert_eq!(doc.spaces[0].pinned_tabs[0].pinned_url.as_deref(), Some("https://pin.example.test/"));
    }

    #[test]
    fn a_grouped_row_lands_after_its_groups_last_member() {
        let mut s = space("A", 0);
        s.groups = vec![group("G")];
        s.tabs = vec![grouped("https://a.example.test/", "G"), grouped("https://b.example.test/", "G"), tab("https://c.example.test/")];
        let mut doc = document(vec![s]);
        assert!(append_cold_tab(&mut doc, 0, Section::Regular, "https://d.example.test/", "D", None, Some("G")));
        assert_eq!(urls(&doc.spaces[0]), ["https://a.example.test/", "https://b.example.test/", "https://d.example.test/", "https://c.example.test/"]);
        assert_eq!(doc.spaces[0].tabs[2].group_id.as_deref(), Some("G"));
    }

    #[test]
    fn a_group_with_members_in_the_other_list_refuses_the_row() {
        let mut s = space("A", 0);
        s.groups = vec![group("G")];
        s.pinned_tabs = vec![grouped("https://p.example.test/", "G")];
        let mut doc = document(vec![s]);
        let before = doc.clone();
        assert!(!append_cold_tab(&mut doc, 0, Section::Regular, "https://r.example.test/", "R", None, Some("G")));
        assert_eq!(doc, before);
    }

    #[test]
    fn an_unknown_group_an_empty_url_and_a_bad_space_are_refused() {
        let mut doc = one_space();
        let before = doc.clone();
        assert!(!append_cold_tab(&mut doc, 0, Section::Regular, "https://x.example.test/", "X", None, Some("nope")));
        assert!(!append_cold_tab(&mut doc, 0, Section::Regular, "", "X", None, None));
        assert!(!append_cold_tab(&mut doc, 9, Section::Regular, "https://x.example.test/", "X", None, None));
        assert_eq!(doc, before);
    }

    #[test]
    fn every_appended_row_gets_its_own_token() {
        let mut doc = one_space();
        append_cold_tab(&mut doc, 0, Section::Regular, "https://x.example.test/", "X", None, None);
        append_cold_tab(&mut doc, 0, Section::Pinned, "https://y.example.test/", "Y", None, None);
        let a = doc.spaces[0].tabs[2].token;
        let b = doc.spaces[0].pinned_tabs[0].token;
        assert_ne!(a, crate::spaces::UNTOKENED);
        assert_ne!(a, b);
    }
}
