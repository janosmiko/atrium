// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! which rows an auto-archive sweep would take.
//!
//! Selection only - the move is `mutate::sweep_archive`, tested in `mutate.rs`.
//!
//! The clock is a parameter for the same reason `spaces::load` takes one: a
//! test that reads the wall clock asserts a different thing every run.

use atrium_core::lifecycle;
use atrium_core::spaces::{self, Document, SwiftDate};

const HOUR: SwiftDate = 3600.0;
const EPOCH: SwiftDate = 1_000_000.0;

/// A document with one space, whose tabs are placed by hand so each test says
/// what it is about. `assign_tokens` runs via `load`, so every row has the
/// identity the sweep reports. A name becomes `https://<name>/`, since `load`
/// drops a row no reopen path could carry.
fn doc_with(
    tabs: &[(&str, Option<SwiftDate>, Option<&str>)],
    pinned: &[(&str, SwiftDate)],
) -> Document {
    let rows: Vec<String> = tabs
        .iter()
        .map(|(name, last, group)| {
            let last = last.map_or(String::new(), |t| format!(r#","lastActiveAt":{t}"#));
            let group = group.map_or(String::new(), |g| format!(r#","groupID":"{g}""#));
            format!(r#"{{"url":"https://{name}/","title":"{name}"{last}{group}}}"#)
        })
        .collect();
    let pins: Vec<String> = pinned
        .iter()
        .map(|(name, last)| {
            format!(r#"{{"url":"https://{name}/","title":"{name}","lastActiveAt":{last}}}"#)
        })
        .collect();
    let json = format!(
        r##"{{"spaces":[{{"id":"s1","name":"Work","emoji":"W","tint":"#FFFFFF",
"profileID":"p1","order":0,"pinnedTabs":[{}],"tabs":[{}],
"groups":[{{"id":"g1","name":"Reading","collapsed":false}}]}}],
"appState":{{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":{EPOCH},
"sidebarWidthMigrated":true}}}}"##,
        pins.join(","),
        rows.join(",")
    );
    spaces::load(&json, EPOCH).expect("fixture must load")
}

/// The tokens the sweep names, as the names `doc_with` was given, so a failure
/// reads as the tabs a user would have lost rather than as numbers.
fn taken(doc: &Document, threshold: &str, now: SwiftDate) -> Vec<String> {
    lifecycle::archive_due(doc, threshold, now)
        .into_iter()
        .map(|token| {
            doc.spaces
                .iter()
                .flat_map(|s| s.tabs.iter())
                .find(|t| t.token == token)
                .map(|t| t.title.clone())
                .unwrap_or_else(|| format!("unknown token {token}"))
        })
        .collect()
}

/// The ladder `SettingsStore` normalises to, in seconds. `never` is a rung of
/// the same ladder rather than a separate switch, so it has to answer here.
#[test]
fn the_threshold_ladder_answers_in_seconds() {
    assert_eq!(
        lifecycle::threshold_seconds("twelveHours"),
        Some(12.0 * HOUR)
    );
    assert_eq!(
        lifecycle::threshold_seconds("twentyFourHours"),
        Some(24.0 * HOUR)
    );
    assert_eq!(
        lifecycle::threshold_seconds("sevenDays"),
        Some(7.0 * 24.0 * HOUR)
    );
    assert_eq!(
        lifecycle::threshold_seconds("thirtyDays"),
        Some(30.0 * 24.0 * HOUR)
    );
    assert_eq!(lifecycle::threshold_seconds("never"), None);
    // A hand-edited settings file must not archive on a guess.
    assert_eq!(lifecycle::threshold_seconds("whenever"), None);
}

/// The criterion itself: a tab idle past the TTL is taken, one inside it is
/// not.
#[test]
fn a_tab_idle_past_the_ttl_is_taken_and_a_fresh_one_is_not() {
    let doc = doc_with(
        &[
            ("stale", Some(EPOCH + HOUR), None),
            ("fresh", Some(EPOCH + 20.0 * HOUR), None),
        ],
        &[],
    );

    assert_eq!(
        taken(&doc, "twelveHours", EPOCH + 24.0 * HOUR),
        vec!["stale".to_string()]
    );
}

/// Both exemptions are intended. A pinned tile is the user's furniture and a
/// foldered tab has been filed on purpose; neither is an abandoned tab.
#[test]
fn pinned_and_foldered_tabs_are_exempt() {
    let doc = doc_with(
        &[
            ("loose", Some(EPOCH), None),
            ("filed", Some(EPOCH), Some("g1")),
        ],
        &[("pinned", EPOCH)],
    );

    assert_eq!(
        taken(&doc, "twelveHours", EPOCH + 500.0 * HOUR),
        vec!["loose".to_string()],
        "only the loose tab is a candidate: the pin and the folder are exempt"
    );
}

/// `never` is the off switch. It has to stop the sweep whatever the idle time.
#[test]
fn never_takes_nothing() {
    let doc = doc_with(&[("ancient", Some(EPOCH), None)], &[]);

    assert!(taken(&doc, "never", EPOCH + 10_000.0 * HOUR).is_empty());
}

/// A row with no `lastActiveAt` has never been touched in a session that
/// recorded one. Ageing it from zero makes every such row instantly ancient,
/// which on the first launch after an upgrade empties the sidebar. The epoch
/// is the floor the schema stamps for exactly this.
#[test]
fn a_row_that_never_recorded_a_visit_ages_from_the_archive_epoch() {
    let doc = doc_with(&[("unstamped", None, None)], &[]);

    assert!(
        taken(&doc, "twelveHours", EPOCH + 6.0 * HOUR).is_empty(),
        "six hours after the epoch nothing is twelve hours idle"
    );
    assert_eq!(
        taken(&doc, "twelveHours", EPOCH + 13.0 * HOUR),
        vec!["unstamped".to_string()]
    );
}

/// A `lastActiveAt` older than the epoch is what a document carries the moment
/// it migrates: the rows are old, the grace period is not. Clamping to the
/// epoch is what stops the migration itself archiving the user's tabs.
#[test]
fn a_visit_older_than_the_epoch_still_ages_from_the_epoch() {
    let doc = doc_with(&[("predates", Some(EPOCH - 5_000.0 * HOUR), None)], &[]);

    assert!(taken(&doc, "twelveHours", EPOCH + 6.0 * HOUR).is_empty());
    assert_eq!(
        taken(&doc, "twelveHours", EPOCH + 13.0 * HOUR),
        vec!["predates".to_string()]
    );
}

/// The sweep reads; it must not write. A caller that samples the candidates to
/// show a count has to be able to do it on every tick.
#[test]
fn the_sweep_leaves_the_document_alone() {
    let doc = doc_with(&[("stale", Some(EPOCH), None)], &[]);
    let before = doc.clone();

    let _ = lifecycle::archive_due(&doc, "twelveHours", EPOCH + 100.0 * HOUR);

    assert_eq!(doc, before);
}
