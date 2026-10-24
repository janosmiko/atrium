// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Rows that left the strip but not the document: archived and snoozed.
//!
//! One module, because the two are the same operation with different reasons
//! and the same overview shape - a row is taken out of a space, keeps where it
//! sat, and is listed with the space it came from.
//!
//! `sweep_archive` is the only function here that reads a THRESHOLD rather than
//! acting on one row, and its answer is the ids it took, not a bool: the caller
//! needs to know which rows to animate away.

use std::collections::HashSet;

use super::{Section, is_drawn_row, locate_token, persisted_index_of_visible, sidebar_position_of};
use crate::SwiftDate;
use crate::lifecycle;
use crate::spaces::{
    ArchivedOverviewEntry, ArchivedTab, CloseReason, Document, RowToken, SnoozedOverviewEntry,
    SnoozedTab, SpaceTab,
};
use crate::tabs::Bindings;

/// The Archived overview list, newest first,
/// each row's own space resolved to a name and a sidebar position Restore
/// can jump to.
pub fn archived_overview(doc: &Document) -> Vec<ArchivedOverviewEntry> {
    let mut rows: Vec<&ArchivedTab> = doc.archived.iter().collect();
    rows.sort_by(|a, b| b.archived_at.total_cmp(&a.archived_at));
    rows.into_iter()
        .map(|tab| ArchivedOverviewEntry {
            id: tab.id.clone(),
            url: tab.url.clone(),
            title: tab
                .custom_title
                .clone()
                .unwrap_or_else(|| tab.title.clone()),
            space_id: Some(tab.space_id.clone()),
            space_index: sidebar_position_of(doc, &tab.space_id),
            space_name: doc
                .spaces
                .iter()
                .find(|s| s.id == tab.space_id)
                .map(|s| s.name.clone()),
            archived_at: tab.archived_at,
            reason: tab.reason,
        })
        .collect()
}

/// Removes an ArchivedTab by id. Used both by Restore (the caller reopens the
/// tab first, using the row this returned true for) and by delete-forever.
pub fn remove_archived_tab(doc: &mut Document, id: &str) -> bool {
    let Some(pos) = doc.archived.iter().position(|a| a.id == id) else {
        return false;
    };
    doc.archived.remove(pos);
    true
}

/// The Snoozed overview list, each row's own space resolved
/// to a name and a sidebar position Wake can jump to.
pub fn snoozed_overview(doc: &Document) -> Vec<SnoozedOverviewEntry> {
    doc.snoozed
        .iter()
        .map(|tab| SnoozedOverviewEntry {
            id: tab.id.clone(),
            url: tab.url.clone(),
            title: tab.title.clone(),
            space_id: Some(tab.space_id.clone()),
            space_index: sidebar_position_of(doc, &tab.space_id),
            space_name: doc
                .spaces
                .iter()
                .find(|s| s.id == tab.space_id)
                .map(|s| s.name.clone()),
            wake_at: tab.wake_at,
            alert: tab.alert,
        })
        .collect()
}

/// Removes a SnoozedTab by id. Used both by Wake (the caller reopens the tab
/// first) and by delete-forever.
pub fn remove_snoozed_tab(doc: &mut Document, id: &str) -> bool {
    let Some(pos) = doc.snoozed.iter().position(|s| s.id == id) else {
        return false;
    };
    doc.snoozed.remove(pos);
    true
}

/// Moves every row `lifecycle::archive_due` selects into `doc.archived`.
/// A row bound to a live tab stays: the page is on screen, and a swept row
/// leaves that tab with no row for the rest of the session.
/// Two passes: draining `doc.spaces` first frees the borrow `allocate_token()` needs.
pub fn sweep_archive(
    doc: &mut Document,
    bindings: &Bindings,
    threshold: &str,
    now: SwiftDate,
) -> Vec<String> {
    let due: HashSet<RowToken> = lifecycle::archive_due(doc, threshold, now)
        .into_iter()
        .filter(|token| !bindings.is_taken(*token))
        .collect();
    if due.is_empty() {
        return Vec::new();
    }

    let mut taken: Vec<(String, SpaceTab)> = Vec::new();
    for space in &mut doc.spaces {
        let space_id = space.id.clone();
        let mut i = 0;
        while i < space.tabs.len() {
            if due.contains(&space.tabs[i].token) {
                taken.push((space_id.clone(), space.tabs.remove(i)));
            } else {
                i += 1;
            }
        }
    }

    archive_taken(doc, taken, now, CloseReason::Archived)
}

/// The graveyard's size cap: the oldest row is dropped past
/// this many, so the file stays bounded without a second retention policy.
pub const MAX_ARCHIVED_TABS: usize = 200;

/// Split out because `allocate_token` needs `doc` mutably while the drain
/// above still holds `doc.spaces`.
pub(crate) fn archive_taken(
    doc: &mut Document,
    taken: Vec<(String, SpaceTab)>,
    now: SwiftDate,
    reason: CloseReason,
) -> Vec<String> {
    let ids = taken
        .into_iter()
        .map(|(space_id, tab)| {
            let id = format!("archived-{}", doc.allocate_token());
            doc.archived.push(ArchivedTab {
                id: id.clone(),
                url: tab.url,
                title: tab.title,
                space_id,
                archived_at: now,
                last_active_at: tab.last_active_at,
                group_id: tab.group_id,
                custom_title: tab.custom_title,
                reason,
            });
            id
        })
        .collect();
    if doc.archived.len() > MAX_ARCHIVED_TABS {
        let over = doc.archived.len() - MAX_ARCHIVED_TABS;
        doc.archived.drain(0..over);
    }
    ids
}

/// Archives every REGULAR row in one space - the lab's Clear control
/// (swipe-lab.html:3416-3430).
///
/// Pinned rows and folder members stay: Clear sits in the divider above the
/// ungrouped list and the lab's own sweep takes that list alone, so a pin the
/// user set deliberately is not swept away by a control aimed at clutter.
pub fn archive_space_tabs(doc: &mut Document, space: usize, now: SwiftDate) -> Vec<String> {
    let Some(target) = doc.spaces.get_mut(space) else {
        return Vec::new();
    };
    let space_id = target.id.clone();
    let mut taken: Vec<(String, SpaceTab)> = Vec::new();
    let mut i = 0;
    while i < target.tabs.len() {
        if target.tabs[i].group_id.is_none() {
            taken.push((space_id.clone(), target.tabs.remove(i)));
        } else {
            i += 1;
        }
    }
    archive_taken(doc, taken, now, CloseReason::Archived)
}

/// Archives the single regular row the surface draws at `visible` - the lab's
/// Archive Tab entry (swipe-lab.html:4497). `None` when the index names no row.
pub fn archive_tab(
    doc: &mut Document,
    space: usize,
    visible: usize,
    now: SwiftDate,
) -> Option<String> {
    let target = doc.spaces.get(space)?;
    let index = persisted_index_of_visible(visible, target, &doc.hidden_rows)?;
    let target = &mut doc.spaces[space];
    let space_id = target.id.clone();
    let taken = target.tabs.remove(index);
    archive_taken(doc, vec![(space_id, taken)], now, CloseReason::Archived).pop()
}

/// Archives the drawn regular rows after the one at `visible`, or every drawn
/// row when `visible` is `None` - the lab's Archive Tabs Below
/// (swipe-lab.html:4501). Folder members stay for the same reason Clear leaves
/// them: a row the user filed is structure, not the clutter this sweeps.
pub fn archive_tabs_below(
    doc: &mut Document,
    space: usize,
    visible: Option<usize>,
    now: SwiftDate,
) -> Vec<String> {
    let Some(target) = doc.spaces.get(space) else {
        return Vec::new();
    };
    let first = visible.map_or(0, |v| v + 1);
    let doomed: Vec<RowToken> = target
        .tabs
        .iter()
        .filter(|tab| is_drawn_row(tab, &doc.hidden_rows))
        .skip(first)
        .map(|tab| tab.token.clone())
        .collect();
    let target = &mut doc.spaces[space];
    let space_id = target.id.clone();
    let mut taken: Vec<(String, SpaceTab)> = Vec::new();
    let mut i = 0;
    while i < target.tabs.len() {
        if doomed.contains(&target.tabs[i].token) {
            taken.push((space_id.clone(), target.tabs.remove(i)));
        } else {
            i += 1;
        }
    }
    archive_taken(doc, taken, now, CloseReason::Archived)
}

/// The snoozed list's cap. Refused rather than draining the oldest row, since
/// that row is scheduled to come back and dropping it would lose it early.
const MAX_SNOOZED_TABS: usize = 200;

/// Moves the row `token` names out of its space and into `doc.snoozed`, due to
/// wake at `wake_at`. `None` when the token names no row, or at `MAX_SNOOZED_TABS`.
pub fn snooze_tab(
    doc: &mut Document,
    token: RowToken,
    wake_at: SwiftDate,
    alert: bool,
    now: SwiftDate,
) -> Option<String> {
    if doc.snoozed.len() >= MAX_SNOOZED_TABS {
        return None;
    }
    let (space, section, index) = locate_token(doc, token)?;
    let tab = match section {
        Section::Pinned => doc.spaces[space].pinned_tabs.remove(index),
        Section::Regular => doc.spaces[space].tabs.remove(index),
    };
    let space_id = doc.spaces[space].id.clone();
    let id = format!("snoozed-{}", doc.allocate_token());
    doc.snoozed.push(SnoozedTab {
        id: id.clone(),
        url: tab.url,
        title: tab.title,
        space_id,
        snoozed_at: now,
        wake_at,
        alert,
        group_id: tab.group_id,
    });
    Some(id)
}

#[cfg(test)]
mod tests {
    use super::*;
    #[allow(unused_imports)]
    use crate::mutate::test_support::*;
    use crate::spaces::assign_tokens;

    #[test]
    fn clear_archives_the_ungrouped_rows_and_leaves_the_rest() {
        let mut doc = tokened(vec![space("A", 0), space("B", 1)]);
        doc.spaces[0].pinned_tabs = vec![tab("pinned")];
        doc.spaces[0].tabs = vec![tab("loose"), grouped("in-folder", "G"), tab("also-loose")];
        doc.spaces[1].tabs = vec![tab("other-space")];
        assign_tokens(&mut doc);

        let ids = archive_space_tabs(&mut doc, 0, 1.0);
        assert_eq!(ids.len(), 2, "both ungrouped rows archived");

        // Pinned rows and folder members are NOT clutter: Clear sits above the
        // ungrouped list and takes only that list.
        assert_eq!(urls(&doc.spaces[0]), vec!["in-folder"]);
        assert_eq!(doc.spaces[0].pinned_tabs.len(), 1);
        // And it is one space's control, not the window's.
        assert_eq!(urls(&doc.spaces[1]), vec!["other-space"]);

        let archived: Vec<&str> = doc.archived.iter().map(|a| a.url.as_str()).collect();
        assert_eq!(archived, vec!["loose", "also-loose"]);
        assert!(
            doc.archived.iter().all(|a| a.space_id == doc.spaces[0].id),
            "an archived row must remember the space it came from"
        );
    }

    #[test]
    fn archiving_one_row_takes_that_row_alone() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("first"), tab("second"), tab("third")];
        assign_tokens(&mut doc);

        let id = archive_tab(&mut doc, 0, 1, 1.0).expect("the row exists");
        assert_eq!(urls(&doc.spaces[0]), vec!["first", "third"]);
        assert_eq!(doc.archived.len(), 1);
        assert_eq!(doc.archived[0].url, "second");
        assert_eq!(doc.archived[0].id, id);
    }

    #[test]
    fn archiving_a_row_the_model_lost_changes_nothing() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("only")];
        assign_tokens(&mut doc);

        assert!(archive_tab(&mut doc, 0, 5, 1.0).is_none());
        // One PAST the last row, the boundary a length check gets wrong first.
        assert!(archive_tab(&mut doc, 0, 1, 1.0).is_none());
        assert!(
            archive_tab(&mut doc, 9, 0, 1.0).is_none(),
            "a space the model lost"
        );
        assert!(doc.archived.is_empty(), "a miss must not dirty the file");
    }

    #[test]
    fn archive_below_takes_the_rows_after_it_and_not_the_row_itself() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("above"), tab("clicked"), tab("below"), tab("lower")];
        assign_tokens(&mut doc);

        let ids = archive_tabs_below(&mut doc, 0, Some(1), 1.0);
        assert_eq!(ids.len(), 2);
        assert_eq!(urls(&doc.spaces[0]), vec!["above", "clicked"]);
        let archived: Vec<&str> = doc.archived.iter().map(|a| a.url.as_str()).collect();
        assert_eq!(archived, vec!["below", "lower"]);
    }

    #[test]
    fn archive_below_leaves_folder_members_where_clear_would() {
        // The same rule as Clear: a row the user filed in a folder is
        // structure, not the clutter these controls sweep.
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("clicked"), grouped("in-folder", "G"), tab("loose")];
        assign_tokens(&mut doc);

        assert_eq!(archive_tabs_below(&mut doc, 0, Some(0), 1.0).len(), 1);
        assert_eq!(urls(&doc.spaces[0]), vec!["clicked", "in-folder"]);
    }

    #[test]
    fn archive_below_the_last_row_changes_nothing() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("only")];
        assign_tokens(&mut doc);

        assert!(archive_tabs_below(&mut doc, 0, Some(0), 1.0).is_empty());
        assert!(archive_tabs_below(&mut doc, 9, Some(0), 1.0).is_empty());
        assert!(
            doc.archived.is_empty(),
            "an empty sweep must not dirty the file"
        );
    }

    #[test]
    fn clearing_a_space_with_nothing_loose_changes_nothing() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![grouped("in-folder", "G")];
        assign_tokens(&mut doc);

        assert!(archive_space_tabs(&mut doc, 0, 1.0).is_empty());
        assert!(
            doc.archived.is_empty(),
            "an empty sweep must not dirty the file"
        );
        assert!(
            archive_space_tabs(&mut doc, 9, 1.0).is_empty(),
            "a space the model lost"
        );
    }

    /// The sweep runs on a timer with no view of the screen, so the live
    /// binding is the only thing that says a due row is a page the user sees.
    #[test]
    fn sweep_archive_leaves_a_row_bound_to_a_live_tab() {
        let mut s = space("Work", 0);
        s.tabs = vec![tab("open"), tab("stale")];
        let mut doc = tokened(vec![s]);
        doc.spaces[0].tabs[0].last_active_at = Some(0.0);
        doc.spaces[0].tabs[1].last_active_at = Some(0.0);
        let mut bindings = Bindings::new();
        assert!(bindings.bind(7, doc.spaces[0].tabs[0].token));

        let ids = sweep_archive(&mut doc, &bindings, "twelveHours", 13.0 * 3600.0);

        assert_eq!(ids.len(), 1);
        assert_eq!(doc.spaces[0].tabs.len(), 1);
        assert_eq!(doc.spaces[0].tabs[0].url, "open");
        assert_eq!(doc.archived[0].url, "stale");
    }

    /// a due row actually leaves the space and lands in archived,
    /// carrying its own space id and a fresh id from the document's counter.
    #[test]
    fn sweep_archive_moves_a_due_row_out_of_its_space_and_into_archived() {
        let mut s = space("Work", 0);
        s.tabs = vec![tab("stale")];
        let mut doc = tokened(vec![s]);
        doc.spaces[0].tabs[0].last_active_at = Some(0.0);

        let ids = sweep_archive(&mut doc, &Bindings::new(), "twelveHours", 13.0 * 3600.0);

        assert_eq!(ids.len(), 1);
        assert!(
            doc.spaces[0].tabs.is_empty(),
            "the row must leave the space"
        );
        assert_eq!(doc.archived.len(), 1);
        assert_eq!(doc.archived[0].id, ids[0]);
        assert_eq!(doc.archived[0].url, "stale");
        assert_eq!(doc.archived[0].space_id, "id-Work");
        assert_eq!(doc.archived[0].reason, CloseReason::Archived);
    }

    /// past `MAX_ARCHIVED_TABS`, the oldest row is dropped
    /// so the graveyard stays bounded rather than growing without limit.
    #[test]
    fn archive_taken_evicts_the_oldest_row_past_the_cap() {
        let mut s = space("Work", 0);
        s.tabs = (0..MAX_ARCHIVED_TABS + 1)
            .map(|i| tab(&format!("t{i}")))
            .collect();
        let mut doc = tokened(vec![s]);
        for t in &mut doc.spaces[0].tabs {
            t.last_active_at = Some(0.0);
        }

        sweep_archive(&mut doc, &Bindings::new(), "twelveHours", 13.0 * 3600.0);

        assert_eq!(doc.archived.len(), MAX_ARCHIVED_TABS);
        assert!(
            !doc.archived.iter().any(|a| a.url == "t0"),
            "the oldest row (pushed first) must be the one evicted"
        );
        assert!(doc.archived.iter().any(|a| a.url == "t1"));
    }

    /// the graveyard reads newest first regardless of push
    /// order.
    #[test]
    fn archived_overview_sorts_newest_first() {
        let mut doc = tokened(vec![space("Work", 0)]);
        doc.archived.push(ArchivedTab {
            id: "old".to_string(),
            url: "https://old.example/".to_string(),
            title: "Old".to_string(),
            space_id: "id-Work".to_string(),
            archived_at: 10.0,
            last_active_at: None,
            group_id: None,
            custom_title: None,
            reason: CloseReason::Archived,
        });
        doc.archived.push(ArchivedTab {
            id: "new".to_string(),
            url: "https://new.example/".to_string(),
            title: "New".to_string(),
            space_id: "id-Work".to_string(),
            archived_at: 20.0,
            last_active_at: None,
            group_id: None,
            custom_title: None,
            reason: CloseReason::UserClosed,
        });

        let overview = archived_overview(&doc);

        assert_eq!(overview[0].id, "new");
        assert_eq!(overview[1].id, "old");
        assert_eq!(overview[0].reason, CloseReason::UserClosed);
    }

    /// A fresh row is not due, so the sweep must not touch it.
    #[test]
    fn sweep_archive_leaves_a_fresh_row_alone() {
        let mut s = space("Work", 0);
        s.tabs = vec![tab("fresh")];
        let mut doc = tokened(vec![s]);
        doc.spaces[0].tabs[0].last_active_at = Some(12.0 * 3600.0);

        let ids = sweep_archive(&mut doc, &Bindings::new(), "twelveHours", 13.0 * 3600.0);

        assert!(ids.is_empty());
        assert_eq!(doc.spaces[0].tabs.len(), 1);
        assert!(doc.archived.is_empty());
    }

    /// Two due rows in different spaces each keep their own originating space
    /// id, not whichever space the sweep visited last.
    #[test]
    fn sweep_archive_keeps_each_rows_own_space_id() {
        let mut a = space("A", 0);
        a.tabs = vec![tab("from-a")];
        let mut b = space("B", 1);
        b.tabs = vec![tab("from-b")];
        let mut doc = tokened(vec![a, b]);
        for space in &mut doc.spaces {
            for t in &mut space.tabs {
                t.last_active_at = Some(0.0);
            }
        }

        sweep_archive(&mut doc, &Bindings::new(), "twelveHours", 13.0 * 3600.0);

        let a_row = doc.archived.iter().find(|r| r.url == "from-a").unwrap();
        let b_row = doc.archived.iter().find(|r| r.url == "from-b").unwrap();
        assert_eq!(a_row.space_id, "id-A");
        assert_eq!(b_row.space_id, "id-B");
    }

    /// Both rows are 13 hours idle - past A's own TTL, inside B's - and the
    /// general fallback ("sevenDays") is neither space's own value.
    #[test]
    fn sweep_archive_uses_each_spaces_own_threshold() {
        let mut a = space("A", 0);
        a.archive_ttl = Some("twelveHours".to_string());
        a.tabs = vec![tab("from-a")];
        let mut b = space("B", 1);
        b.archive_ttl = Some("thirtyDays".to_string());
        b.tabs = vec![tab("from-b")];
        let mut doc = tokened(vec![a, b]);
        for space in &mut doc.spaces {
            for t in &mut space.tabs {
                t.last_active_at = Some(0.0);
            }
        }

        let ids = sweep_archive(&mut doc, &Bindings::new(), "sevenDays", 13.0 * 3600.0);

        assert_eq!(ids.len(), 1);
        assert_eq!(doc.archived[0].url, "from-a");
        assert_eq!(
            doc.spaces[1].tabs[0].url, "from-b",
            "B's own thirty-day TTL keeps it, though the fallback would not have"
        );
    }

    /// snoozing a live row moves it out of the space and into
    /// `doc.snoozed`, keeping its wake time and alert flag.
    #[test]
    fn snooze_tab_moves_the_row_into_snoozed() {
        let mut s = space("Work", 0);
        s.tabs = vec![tab("later")];
        let mut doc = tokened(vec![s]);
        let token = doc.spaces[0].tabs[0].token;

        let id = snooze_tab(&mut doc, token, 100.0, true, 1.0).expect("token must resolve");

        assert!(doc.spaces[0].tabs.is_empty());
        assert_eq!(doc.snoozed.len(), 1);
        assert_eq!(doc.snoozed[0].id, id);
        assert_eq!(doc.snoozed[0].url, "later");
        assert_eq!(doc.snoozed[0].wake_at, 100.0);
        assert!(doc.snoozed[0].alert);
        assert_eq!(doc.snoozed[0].space_id, "id-Work");
    }

    /// An unknown token names no row, so nothing moves anywhere.
    #[test]
    fn snooze_tab_does_nothing_for_an_unknown_token() {
        let mut doc = tokened(vec![space("Work", 0)]);

        assert_eq!(snooze_tab(&mut doc, 9999, 100.0, false, 1.0), None);
        assert!(doc.snoozed.is_empty());
    }

    /// At the cap, the row stays put rather than evicting an older snooze -
    /// the row it names might wake first.
    #[test]
    fn snooze_tab_refuses_past_the_cap() {
        let mut s = space("Work", 0);
        s.tabs = (0..MAX_SNOOZED_TABS + 1)
            .map(|i| tab(&format!("t{i}")))
            .collect();
        let mut doc = tokened(vec![s]);
        assign_tokens(&mut doc);

        for _ in 0..MAX_SNOOZED_TABS {
            let token = doc.spaces[0].tabs[0].token;
            assert!(snooze_tab(&mut doc, token, 100.0, false, 1.0).is_some());
        }
        assert_eq!(doc.snoozed.len(), MAX_SNOOZED_TABS);
        let last_token = doc.spaces[0].tabs[0].token;
        assert_eq!(snooze_tab(&mut doc, last_token, 100.0, false, 1.0), None);
        assert_eq!(doc.snoozed.len(), MAX_SNOOZED_TABS);
        assert_eq!(doc.spaces[0].tabs.len(), 1, "the refused row stays put");
    }
}
