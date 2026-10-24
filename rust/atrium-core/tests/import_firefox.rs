// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The Firefox reader against the hand-built fixtures in
//! fixtures/import/firefox.

use std::collections::BTreeMap;

use atrium_core::import::{
    self, ImportPlan, ImportSummary, PlannedEngine, PlannedProfile, PlannedTab,
};

mod common;

const PERSONAL: &str = "Profiles/fixture02.default-release";
const WORK: &str = "Profiles/fixture03.work";

fn files() -> BTreeMap<String, String> {
    common::firefox_fixture_files("basic")
}

fn basic() -> ImportPlan {
    import::read("firefox", &files()).expect("the basic fixture reads")
}

fn with(key: &str, value: &str) -> BTreeMap<String, String> {
    let mut files = files();
    files.insert(key.into(), value.into());
    files
}

fn tab_view(tabs: &[PlannedTab]) -> Vec<(&str, &str, Option<usize>)> {
    tabs.iter()
        .map(|t| (t.url.as_str(), t.title.as_str(), t.group))
        .collect()
}

#[test]
fn the_packed_fixtures_decode_to_their_json_twins() {
    let dir = common::repo_path(&format!("fixtures/import/firefox/basic/{PERSONAL}"));
    for (packed, twin) in [
        (
            "sessionstore-backups/recovery.jsonlz4",
            "sessionstore-backups/recovery.json",
        ),
        ("search.json.mozlz4", "search.json"),
    ] {
        let bytes = std::fs::read(dir.join(packed)).expect("the packed fixture exists");
        let decoded = import::mozlz4::decode(&bytes).expect("the packed fixture decodes");
        assert_eq!(
            String::from_utf8(decoded).unwrap(),
            common::read_fixture(&dir.join(twin)),
            "re-pack it: python3 build/scripts/mozlz4-pack.py <{twin}> <{packed}>"
        );
    }
}

#[test]
fn each_profile_folder_becomes_a_profile_and_a_space() {
    let plan = basic();
    assert_eq!(
        plan.profiles,
        [
            PlannedProfile {
                key: PERSONAL.into(),
                name: "Personal".into(),
                is_default: true
            },
            PlannedProfile {
                key: WORK.into(),
                name: "Work".into(),
                is_default: false
            },
        ]
    );
    let spaces: Vec<(&str, &str, &str)> = plan
        .spaces
        .iter()
        .map(|s| (s.key.as_str(), s.name.as_str(), s.profile_key.as_str()))
        .collect();
    assert_eq!(
        spaces,
        [(PERSONAL, "Personal", PERSONAL), (WORK, "Work", WORK)]
    );
}

#[test]
fn toolbar_links_are_pinned_and_each_root_is_a_group() {
    let plan = basic();
    let personal = &plan.spaces[0];
    assert_eq!(
        tab_view(&personal.pinned),
        [
            ("https://news.example.test/", "News", None),
            ("https://docs.example.test/", "Docs Home", Some(0)),
            ("https://docs.example.test/specs", "Specs", Some(0)),
            ("https://recipes.example.test/", "Recipes", Some(1)),
            ("https://old.example.test/", "Old", Some(1)),
            ("https://later.example.test/", "Later", Some(2)),
            ("https://phone.example.test/", "Phone", Some(3)),
            ("https://mail.example.test/", "Mail", None),
        ]
    );
    let groups: Vec<&str> = personal.groups.iter().map(|g| g.name.as_str()).collect();
    assert_eq!(
        groups,
        [
            "Docs",
            "Bookmarks Menu",
            "Other Bookmarks",
            "Mobile Bookmarks",
            "Reading"
        ]
    );
    assert_eq!(personal.pinned[0].last_active_at, Some(725_760_000.0));
}

#[test]
fn open_tabs_come_from_the_session_and_skip_a_bookmarked_page() {
    let plan = basic();
    let personal = &plan.spaces[0];
    assert_eq!(
        tab_view(&personal.tabs),
        [
            ("https://a.example.test/2", "Article", Some(4)),
            ("https://bank.example.test/", "Bank", None),
        ]
    );
    assert_eq!(
        personal.pinned[7].last_active_at,
        Some(725_760_000.0),
        "Mail"
    );
    let work = &plan.spaces[1];
    assert_eq!(
        tab_view(&work.pinned),
        [("https://tickets.example.test/", "Tickets", None)]
    );
    assert!(
        work.tabs.is_empty(),
        "no session file is no tabs and no note"
    );
}

#[test]
fn user_engines_arrive_and_firefox_own_are_skipped() {
    assert_eq!(
        basic().engines,
        [
            PlannedEngine {
                name: "Docs".into(),
                keyword: "docs".into(),
                url: "https://docs.example.test/search?q=%s".into(),
                profile_key: PERSONAL.into(),
                is_default: true,
            },
            PlannedEngine {
                name: "Wiki".into(),
                keyword: "wiki.example.test".into(),
                url: "https://wiki.example.test/w?search=%s".into(),
                profile_key: PERSONAL.into(),
                is_default: false,
            },
        ]
    );
}

#[test]
fn the_notes_say_what_was_skipped() {
    assert_eq!(
        basic().notes,
        [
            "Skipped Firefox profiles outside the Firefox folder: 1.",
            "Skipped Firefox bookmarks that are not web pages: 1.",
            "Firefox bookmarks moved from a nested folder to its top-level folder: 1.",
            "Skipped Firefox tabs that are not web pages: 1.",
            "Firefox container tabs kept in their profile's space: 1.",
            "Skipped Firefox's own search engines: 2.",
            "Skipped Firefox search engines with a URL that Atrium cannot use: 2.",
        ]
    );
}

#[test]
fn the_summary_of_the_basic_fixture() {
    assert_eq!(
        import::summary(&basic()),
        ImportSummary {
            profiles: 2,
            spaces: 2,
            tabs: 11,
            favorites: 0,
            routes: 0,
            engines: 2,
            notes: 7
        }
    );
}

#[test]
fn unreadable_bookmarks_or_session_fail_the_source() {
    let rows = with(&format!("{WORK}/places.sqlite#bookmarks"), "");
    assert_eq!(
        import::read("firefox", &rows).unwrap_err(),
        "Firefox's bookmarks could not be read or are too large. If Firefox is open, quit it and import again."
    );
    let session = with(&format!("{PERSONAL}/sessionstore.jsonlz4"), "");
    assert_eq!(
        import::read("firefox", &session).unwrap_err(),
        "Firefox's session file is damaged or too large."
    );
}

#[test]
fn a_damaged_search_file_costs_only_the_engines() {
    let plan = import::read(
        "firefox",
        &with(&format!("{PERSONAL}/search.json.mozlz4"), ""),
    )
    .unwrap();
    assert!(plan.engines.is_empty());
    assert_eq!(import::summary(&plan).tabs, 11);
    assert!(plan
        .notes
        .iter()
        .any(|n| n == "Firefox's search engines could not be read."));
}

#[test]
fn without_profiles_ini_profiles_use_their_folder_names() {
    let mut files = files();
    files.remove("profiles.ini");
    let plan = import::read("firefox", &files).unwrap();
    let names: Vec<(&str, bool)> = plan
        .profiles
        .iter()
        .map(|p| (p.name.as_str(), p.is_default))
        .collect();
    assert_eq!(names, [("default-release", false), ("work", false)]);
    assert_eq!(
        plan.notes[0],
        "Firefox's profiles.ini is missing, so profiles use their folder names."
    );
}
