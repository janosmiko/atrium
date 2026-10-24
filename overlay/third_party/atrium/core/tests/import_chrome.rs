// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The Chrome reader against the hand-built fixtures in fixtures/import/chrome.

use std::collections::BTreeMap;

use atrium_core::import::{
    self, ImportPlan, ImportSummary, PlannedEngine, PlannedGroup, PlannedProfile,
};

mod common;

fn files() -> BTreeMap<String, String> {
    common::chrome_fixture_files("basic")
}

fn basic() -> ImportPlan {
    import::read("chrome", &files()).expect("the basic fixture reads")
}

fn engine(
    name: &str,
    keyword: &str,
    url: &str,
    profile_key: &str,
    is_default: bool,
) -> PlannedEngine {
    PlannedEngine {
        name: name.into(),
        keyword: keyword.into(),
        url: url.into(),
        profile_key: profile_key.into(),
        is_default,
    }
}

#[test]
fn each_folder_with_bookmarks_becomes_a_profile_and_a_space() {
    let plan = basic();
    assert_eq!(
        plan.profiles,
        [
            PlannedProfile {
                key: "Default".into(),
                name: "Personal".into(),
                is_default: true
            },
            PlannedProfile {
                key: "Profile 1".into(),
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
        [
            ("Default", "Personal", "Default"),
            ("Profile 1", "Work", "Profile 1")
        ]
    );
}

#[test]
fn bar_links_are_pinned_and_folders_become_groups() {
    let plan = basic();
    let default = &plan.spaces[0];
    assert!(
        default.tabs.is_empty(),
        "bookmarks are pinned, never open tabs"
    );
    let view: Vec<(&str, &str, Option<usize>)> = default
        .pinned
        .iter()
        .map(|t| (t.url.as_str(), t.title.as_str(), t.group))
        .collect();
    assert_eq!(
        view,
        [
            ("https://news.example.test/", "News", None),
            ("https://docs.example.test/", "Docs Home", Some(0)),
            ("https://docs.example.test/specs", "Specs", Some(0)),
            ("https://recipes.example.test/", "Recipes", Some(1)),
            ("https://old.example.test/", "Old", Some(1)),
            ("https://phone.example.test/", "Phone", Some(2)),
        ]
    );
    assert_eq!(
        default.groups,
        [
            PlannedGroup {
                name: "Docs".into()
            },
            PlannedGroup {
                name: "Other bookmarks".into()
            },
            PlannedGroup {
                name: "Mobile bookmarks".into()
            },
        ],
        "an empty folder makes no group"
    );
    assert_eq!(default.pinned[0].last_active_at, Some(788_918_400.0));
    assert_eq!(default.pinned[1].last_active_at, None, "0 means never used");
}

#[test]
fn account_bookmarks_join_the_same_space_without_a_second_copy() {
    let urls: Vec<String> = basic().spaces[1]
        .pinned
        .iter()
        .map(|t| t.url.clone())
        .collect();
    assert_eq!(
        urls,
        [
            "https://wiki.example.test/",
            "https://tickets.example.test/"
        ]
    );
}

#[test]
fn only_the_users_own_engines_arrive_with_percent_s() {
    assert_eq!(
        basic().engines,
        [
            engine(
                "Docs",
                "docs",
                "https://docs.example.test/search?q=%s",
                "Default",
                false
            ),
            engine(
                "Docs",
                "docs",
                "https://docs.example.test/search?q=%s",
                "Profile 1",
                true
            ),
            engine(
                "Wiki",
                "wiki",
                "https://wiki.example.test/w?search=%s",
                "Profile 1",
                false
            ),
            engine(
                "Legacy",
                "legacy",
                "https://legacy.example.test/?q=%s",
                "Profile 1",
                false
            ),
        ]
    );
}

#[test]
fn the_notes_name_what_was_skipped() {
    assert_eq!(
        basic().notes,
        [
            "Skipped Chrome bookmarks that are not web pages: 1.",
            "Chrome bookmarks moved from a nested folder to its top-level folder: 1.",
            "Skipped Chrome's own and unused search engines: 4.",
            "Skipped Chrome search engines with a URL that Atrium cannot use: 2.",
            "Chrome's open tabs are not imported, only its bookmarks.",
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
            tabs: 8,
            favorites: 0,
            routes: 0,
            engines: 3,
            notes: 5
        }
    );
}

#[test]
fn an_empty_bookmarks_file_fails_the_source_with_one_message() {
    let mut files = files();
    files.insert("Default/Bookmarks".into(), String::new());
    assert_eq!(
        import::read("chrome", &files).unwrap_err(),
        "Chrome's bookmarks file is damaged or too large."
    );
}

#[test]
fn an_empty_local_state_fails_the_source_with_one_message() {
    let mut files = files();
    files.insert("Local State".into(), String::new());
    assert_eq!(
        import::read("chrome", &files).unwrap_err(),
        "Chrome's Local State file is damaged or too large."
    );
}

#[test]
fn unread_keyword_rows_only_add_a_note() {
    let mut files = files();
    files.insert("Default/Web Data#keywords".into(), String::new());
    let plan = import::read("chrome", &files).unwrap();
    assert!(
        plan.notes
            .contains(&"Chrome's search engines could not be read.".to_string())
    );
    assert_eq!(plan.engines.len(), 3, "Profile 1's engines still arrive");
    assert_eq!(plan.spaces.len(), 2);
}

#[test]
fn a_preferences_file_with_no_default_engine_gives_no_note() {
    let mut files = files();
    files.insert("Default/Preferences".into(), "{}".into());
    let plan = import::read("chrome", &files).unwrap();
    assert!(
        plan.notes
            .iter()
            .all(|note| !note.contains("could not be read")),
        "{:?}",
        plan.notes
    );
}

#[test]
fn a_damaged_preferences_file_leaves_no_default_engine() {
    let mut files = files();
    files.insert("Profile 1/Preferences".into(), String::new());
    let plan = import::read("chrome", &files).unwrap();
    assert!(plan.engines.iter().all(|e| !e.is_default));
}

#[test]
fn a_preferences_file_that_is_not_json_notes_it_and_still_imports_bookmarks() {
    let mut files = files();
    files.insert("Default/Preferences".into(), "not json".into());
    let plan = import::read("chrome", &files).unwrap();
    assert!(plan
        .notes
        .contains(&"Chrome's default search engine could not be read.".to_string()));
    assert_eq!(plan.spaces[0].pinned.len(), 6, "bookmarks still import");
}

#[test]
fn a_missing_local_state_names_profiles_by_folder() {
    let mut files = files();
    files.remove("Local State");
    let plan = import::read("chrome", &files).unwrap();
    assert_eq!(plan.profiles[1].name, "Profile 1");
    assert!(plan.notes.contains(
        &"Chrome's Local State file is missing, so profiles use their folder names.".to_string()
    ));
}

#[test]
fn a_bookmarks_file_nested_past_the_parser_limit_fails_cleanly() {
    let mut node =
        serde_json::json!({"type": "url", "name": "Deep", "url": "https://deep.example.test/"});
    for _ in 0..100 {
        node = serde_json::json!({"type": "folder", "name": "F", "children": [node]});
    }
    let bookmarks =
        serde_json::json!({"roots": {"bookmark_bar": {"type": "folder", "children": [node]}}});
    let files = BTreeMap::from([("Default/Bookmarks".to_string(), bookmarks.to_string())]);
    let error = import::read("chrome", &files).unwrap_err();
    assert!(
        error.starts_with("Chrome's Bookmarks could not be read"),
        "{error}"
    );
}

#[test]
fn twenty_thousand_bookmarks_all_arrive() {
    let links: Vec<serde_json::Value> = (0..20_000)
        .map(|n| serde_json::json!({"type": "url", "name": "L", "url": format!("https://l{n}.example.test/")}))
        .collect();
    let bookmarks =
        serde_json::json!({"roots": {"bookmark_bar": {"type": "folder", "children": links}}});
    let files = BTreeMap::from([("Default/Bookmarks".to_string(), bookmarks.to_string())]);
    assert_eq!(
        import::read("chrome", &files).unwrap().spaces[0]
            .pinned
            .len(),
        20_000
    );
}
