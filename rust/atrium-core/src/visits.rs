// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! a tab's visited states in time order.
//!
//! The back stack cannot answer this. Chromium's NavigationController drops the
//! forward entries the moment a user navigates away from a mid-history point,
//! so a page visited before a branch is gone from the stack while the user
//! still remembers being there. This log is append-only and never prunes on a
//! branch, which is the whole reason it exists.

use serde::{Deserialize, Serialize};

/// The most visits one tab keeps. Chosen to match the rewind bar's own
/// MAX_MOMENTS: both are "how far back can I reach with a gesture", and two
/// different depths would be two different mental models for the same idea.
const MAX_VISITS: usize = 60;

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct Visit {
    pub url: String,
    pub title: String,
    /// Milliseconds since the epoch, supplied by the caller - the core never
    /// reads a clock, so a test can pin one and a scenario can replay one.
    pub at: f64,
}

/// One tab's timeline, oldest first.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
struct VisitLog {
    visits: Vec<Visit>,
}

impl VisitLog {
    #[cfg(test)]
    pub fn new() -> Self {
        Self::default()
    }

    /// Records a visit, unless it repeats the URL already at the end.
    ///
    /// The dedupe is on the URL alone and only against the LAST entry: a
    /// reload, a fragment-free re-commit and a redirect chain settling all
    /// arrive as the same url twice in a row and are one stop to a user. The
    /// same url visited again LATER is a real second stop and is kept, because
    /// dragging past it is how someone gets back to the earlier one.
    ///
    /// A later title for the same url wins - a page commits before its title
    /// arrives, so the first record of any visit is usually untitled.
    pub fn record(&mut self, url: &str, title: &str, at: f64) {
        if url.is_empty() {
            return;
        }
        if let Some(last) = self.visits.last_mut() {
            if last.url == url {
                if !title.is_empty() {
                    last.title = title.to_string();
                }
                return;
            }
        }
        self.visits.push(Visit {
            url: url.to_string(),
            title: title.to_string(),
            at,
        });
        // Oldest first out. A cap that dropped the newest would make the
        // gesture stop working on exactly the tabs it is most used on.
        if self.visits.len() > MAX_VISITS {
            let overflow = self.visits.len() - MAX_VISITS;
            self.visits.drain(0..overflow);
        }
    }

    /// Oldest first, which is the order the scrubber lays out left to right.
    fn visits(&self) -> &[Visit] {
        &self.visits
    }

    pub fn len(&self) -> usize {
        self.visits.len()
    }

    #[cfg(test)]
    pub fn is_empty(&self) -> bool {
        self.visits.is_empty()
    }
}

/// Every live tab's timeline, keyed by Chromium tab id.
///
/// A process-global map rather than a field on the loaded document, and that is
/// the retention rule rather than a policy someone has to remember: a capture that never reaches disk cannot outlive the process. It is
/// also deliberately NOT behind `cache::Core`, because a timeline is not
/// document state - a window whose core is shut still has a history to scrub.
///
/// Bounded on both axes. Each tab keeps `MAX_VISITS` stops, and `forget`
/// drops a tab's whole log when the tab closes, which is what stops the map
/// growing for the rest of the session.
mod store {
    use super::{Visit, VisitLog};
    use std::collections::HashMap;
    use std::sync::{Mutex, OnceLock};

    fn logs() -> &'static Mutex<HashMap<u64, VisitLog>> {
        static LOGS: OnceLock<Mutex<HashMap<u64, VisitLog>>> = OnceLock::new();
        LOGS.get_or_init(|| Mutex::new(HashMap::new()))
    }

    /// Whether this call added a stop rather than folding into the last one.
    /// The caller re-pushes the scrubber on true and does nothing on false.
    pub fn record(tab_id: u64, url: &str, title: &str, at: f64) -> bool {
        if url.is_empty() {
            return false;
        }
        let mut map = logs().lock().unwrap_or_else(|e| e.into_inner());
        let log = map.entry(tab_id).or_default();
        let before = log.len();
        log.record(url, title, at);
        log.len() != before
    }

    pub(crate) fn visits(tab_id: u64) -> Vec<Visit> {
        let map = logs().lock().unwrap_or_else(|e| e.into_inner());
        map.get(&tab_id)
            .map(|log| log.visits().to_vec())
            .unwrap_or_default()
    }

    /// Drops one tab's timeline. Called when the tab closes.
    pub fn forget(tab_id: u64) -> bool {
        let mut map = logs().lock().unwrap_or_else(|e| e.into_inner());
        map.remove(&tab_id).is_some()
    }
}

pub(crate) use store::visits;
pub use store::{forget, record};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_branch_keeps_the_states_the_back_stack_would_drop() {
        // The case the whole feature exists for: A -> B -> C, back to A, then
        // off to D. Chromium drops B and C at that point. The user remembers
        // them.
        let mut log = VisitLog::new();
        log.record("a", "A", 1.0);
        log.record("b", "B", 2.0);
        log.record("c", "C", 3.0);
        log.record("a", "A", 4.0); // Back to A - a new stop in time.
        log.record("d", "D", 5.0); // The branch. B and C leave the back stack.

        let urls: Vec<&str> = log.visits().iter().map(|v| v.url.as_str()).collect();
        assert_eq!(urls, vec!["a", "b", "c", "a", "d"]);
    }

    #[test]
    fn a_repeat_of_the_same_url_in_a_row_is_one_stop() {
        let mut log = VisitLog::new();
        log.record("a", "", 1.0);
        log.record("a", "A", 2.0); // Reload, or the title arriving late.
        assert_eq!(1, log.len());
        assert_eq!("A", log.visits()[0].title, "a later title wins");
        assert_eq!(1.0, log.visits()[0].at, "but it is still the same stop");
    }

    #[test]
    fn a_title_arriving_late_does_not_erase_one_already_there() {
        let mut log = VisitLog::new();
        log.record("a", "A", 1.0);
        log.record("a", "", 2.0);
        assert_eq!("A", log.visits()[0].title);
    }

    #[test]
    fn the_same_url_visited_again_later_is_a_second_stop() {
        let mut log = VisitLog::new();
        log.record("a", "A", 1.0);
        log.record("b", "B", 2.0);
        log.record("a", "A", 3.0);
        assert_eq!(3, log.len(), "dragging past b is how you reach the first a");
    }

    #[test]
    fn the_cap_drops_the_oldest_and_keeps_the_newest() {
        let mut log = VisitLog::new();
        for i in 0..(MAX_VISITS + 10) {
            log.record(&format!("u{i}"), "", i as f64);
        }
        assert_eq!(MAX_VISITS, log.len());
        assert_eq!("u10", log.visits()[0].url, "the oldest went first");
        assert_eq!(
            format!("u{}", MAX_VISITS + 9),
            log.visits().last().unwrap().url
        );
    }

    #[test]
    fn an_empty_url_is_not_a_stop() {
        let mut log = VisitLog::new();
        log.record("", "", 1.0);
        assert!(log.is_empty());
    }

    /// The store is what the ABI talks to, and its own rules are per-TAB: two
    /// tabs never share a timeline, and closing one leaves the other alone.
    ///
    /// Each store test owns its own tab ids. The map is process-global and the
    /// tests run in parallel, so a shared "clear everything" seam made them
    /// wipe each other's stops - which they did, and only when run together.
    #[test]
    fn two_tabs_keep_separate_timelines_and_closing_one_spares_the_other() {
        assert!(record(101, "a", "A", 1.0));
        assert!(record(102, "z", "Z", 1.0));
        assert!(record(101, "b", "B", 2.0));

        let one: Vec<String> = visits(101).into_iter().map(|v| v.url).collect();
        let two: Vec<String> = visits(102).into_iter().map(|v| v.url).collect();
        assert_eq!(one, vec!["a", "b"]);
        assert_eq!(two, vec!["z"]);

        assert!(forget(101));
        assert!(visits(101).is_empty(), "a closed tab keeps nothing");
        assert_eq!(1, visits(102).len(), "and takes nothing with it");
        assert!(
            !forget(101),
            "forgetting twice is not an error, just nothing"
        );
    }

    /// `record` answers whether a STOP was added, which is what decides a
    /// re-push. A late title changes the log without adding a stop.
    #[test]
    fn recording_reports_whether_it_added_a_stop() {
        assert!(record(7, "a", "", 1.0), "the first visit is a stop");
        assert!(!record(7, "a", "A", 2.0), "a late title is not a new stop");
        assert_eq!("A", visits(7)[0].title, "but it did land");
        assert!(record(7, "b", "B", 3.0));
        assert!(!record(7, "", "", 4.0), "an empty url is never a stop");
        assert_eq!(2, visits(7).len());
    }

    /// An unknown tab is empty rather than an error - a tab that has not
    /// navigated yet has no timeline and the scrubber must open on nothing.
    #[test]
    fn a_tab_with_no_visits_reads_as_empty() {
        assert!(visits(999).is_empty());
    }
}
