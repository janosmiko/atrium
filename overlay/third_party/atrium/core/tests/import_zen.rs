// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The Zen reader against the hand-built fixtures in fixtures/import/zen.

use atrium_core::import::{
    self, ImportPlan, ImportSummary, PlannedFavorite, PlannedGroup, PlannedProfile,
};

mod common;

const PROFILE: &str = "Profiles/fixture01.default";

fn basic() -> ImportPlan {
    import::read("zen", &common::zen_fixture_files("basic")).expect("the basic fixture reads")
}

fn key(context: u64) -> String {
    format!("{PROFILE}#{context}")
}

fn tab_view(tabs: &[import::PlannedTab]) -> Vec<(&str, &str, Option<usize>)> {
    tabs.iter()
        .map(|t| (t.url.as_str(), t.title.as_str(), t.group))
        .collect()
}

#[test]
fn the_packed_fixture_decodes_to_its_json_twin() {
    let dir = common::repo_path(&format!("fixtures/import/zen/basic/{PROFILE}"));
    let packed =
        std::fs::read(dir.join("zen-sessions.jsonlz4")).expect("the packed fixture exists");
    let decoded = import::mozlz4::decode(&packed).expect("the packed fixture decodes");
    assert_eq!(
        String::from_utf8(decoded).unwrap(),
        common::read_fixture(&dir.join("zen-sessions.json")),
        "re-pack it: python3 build/scripts/mozlz4-pack.py <zen-sessions.json> <zen-sessions.jsonlz4>"
    );
}

#[test]
fn the_folder_and_its_used_containers_become_profiles() {
    assert_eq!(
        basic().profiles,
        [
            PlannedProfile {
                key: key(0),
                name: "Main".into(),
                is_default: true
            },
            PlannedProfile {
                key: key(2),
                name: "Client".into(),
                is_default: false
            },
            PlannedProfile {
                key: key(1),
                name: "Personal".into(),
                is_default: false
            },
        ],
        "Banking has no workspace and no essential, so it is not a profile"
    );
}

#[test]
fn workspaces_become_spaces_with_their_container_emoji_and_theme() {
    let plan = basic();
    let home = &plan.spaces[0];
    assert_eq!(
        (
            home.key.as_str(),
            home.name.as_str(),
            home.profile_key.as_str()
        ),
        ("W-HOME", "Home", key(0).as_str())
    );
    assert_eq!(home.emoji.as_deref(), Some("\u{1F33F}"));
    assert_eq!(home.tint.as_deref(), Some("#FF8000"));
    let theme = home.theme.as_ref().expect("a gradient gives a theme");
    assert_eq!(
        theme
            .stops
            .iter()
            .map(|s| s.color.as_str())
            .collect::<Vec<_>>(),
        ["#FF8000", "#0040FF"]
    );
    assert_eq!(theme.opacity, 0.5);

    let work = &plan.spaces[1];
    assert_eq!(work.profile_key, key(2));
    assert_eq!(work.emoji, None, "an icon URL is not an emoji");
    assert_eq!(
        (work.tint.as_ref(), work.theme.as_ref()),
        (None, None),
        "no colours, no theme"
    );
}

#[test]
fn pinned_tabs_keep_their_pinned_page_and_folders_become_groups() {
    let plan = basic();
    let home = &plan.spaces[0];
    assert_eq!(
        tab_view(&home.pinned),
        [("https://news.example.test/b", "Morning News", None)]
    );
    assert_eq!(
        home.pinned[0].last_active_at,
        Some(788_918_400.0),
        "Unix ms to the Cocoa epoch"
    );
    assert!(
        home.tabs.is_empty(),
        "the glance and the about: tab are skipped"
    );

    let work = &plan.spaces[1];
    assert_eq!(
        work.groups,
        [PlannedGroup {
            name: "Docs".into()
        }]
    );
    assert_eq!(
        tab_view(&work.pinned),
        [
            ("https://docs.example.test/", "Docs Home", Some(0)),
            ("https://docs.example.test/specs", "Specs", Some(0))
        ]
    );
    assert_eq!(
        tab_view(&work.tabs),
        [("https://tickets.example.test/1", "Ticket 1", None)]
    );
}

#[test]
fn essentials_become_favourites_for_their_container() {
    assert_eq!(
        basic().favorites,
        [PlannedFavorite {
            url: "https://mail.example.test/".into(),
            title: "Mail".into(),
            profile_key: key(1)
        }]
    );
}

#[test]
fn every_skip_leaves_one_note() {
    let plan = basic();
    assert_eq!(
        plan.notes,
        [
            "Skipped Zen profiles outside the Zen folder: 1.",
            "Skipped Zen tabs that are not web pages: 1.",
            "Skipped Zen glance and empty tabs: 1.",
            "Skipped Zen tabs whose workspace is gone: 1.",
            "Zen tabs kept in a workspace with another container: 1.",
            "Zen tabs moved from a nested folder to its top-level folder: 1.",
        ]
    );
    assert_eq!(
        import::summary(&plan),
        ImportSummary {
            profiles: 3,
            spaces: 2,
            tabs: 4,
            favorites: 1,
            routes: 0,
            engines: 0,
            notes: 6
        }
    );
}

#[test]
fn a_missing_profiles_ini_names_the_lone_folder_and_makes_it_the_default() {
    let mut files = common::zen_fixture_files("basic");
    files.remove("profiles.ini");
    let plan = import::read("zen", &files).unwrap();
    assert_eq!(
        plan.profiles[0],
        PlannedProfile {
            key: key(0),
            name: "default".into(),
            is_default: true
        }
    );
    assert_eq!(
        plan.notes[0],
        "Zen's profiles.ini is missing, so profiles use their folder names."
    );
}

#[test]
fn a_broken_session_or_containers_file_fails_the_whole_source() {
    for name in ["zen-sessions.jsonlz4", "containers.json"] {
        let mut files = common::zen_fixture_files("basic");
        files.insert(format!("{PROFILE}/{name}"), "{".into());
        assert!(import::read("zen", &files).is_err(), "{name}");
    }
}

#[test]
fn an_empty_session_reports_a_plain_message_not_the_parser_error() {
    let mut files = common::zen_fixture_files("basic");
    files.insert(format!("{PROFILE}/zen-sessions.jsonlz4"), String::new());
    let err = import::read("zen", &files).unwrap_err();
    assert_eq!(err, "Zen's session file is damaged or too large.");
}

fn with_bookmarks(rows: &str) -> ImportPlan {
    let mut files = common::zen_fixture_files("basic");
    files.insert(format!("{PROFILE}/places.sqlite#bookmarks"), rows.into());
    import::read("zen", &files).expect("the basic fixture reads")
}

/// Zen and Firefox write the same places.sqlite, so the Firefox rows serve.
fn places_rows() -> String {
    common::read_fixture(&common::repo_path(
        "fixtures/import/firefox/basic/Profiles/fixture02.default-release/bookmarks.json",
    ))
}

#[test]
fn zen_bookmarks_arrive_in_one_bookmarks_space() {
    let plan = with_bookmarks(&places_rows());
    assert_eq!(plan.spaces.len(), 3);
    let space = plan.spaces.last().unwrap();
    assert_eq!(space.key, format!("{PROFILE}#bookmarks"));
    assert_eq!(space.name, "Bookmarks");
    assert_eq!(space.profile_key, key(0));
    assert_eq!(space.pinned.len(), 7);
    assert!(space.tabs.is_empty());
    let groups: Vec<&str> = space.groups.iter().map(|g| g.name.as_str()).collect();
    assert_eq!(groups, ["Docs", "Bookmarks Menu", "Other Bookmarks", "Mobile Bookmarks"]);
    for note in [
        "Skipped Zen bookmarks that are not web pages: 1.",
        "Zen bookmarks moved from a nested folder to its top-level folder: 1.",
        "Zen bookmarks arrive in one Bookmarks space, not in their workspaces.",
    ] {
        assert!(plan.notes.iter().any(|n| n == note), "{note}: {:?}", plan.notes);
    }
}

#[test]
fn unreadable_zen_bookmarks_cost_only_the_bookmarks() {
    let plan = with_bookmarks("");
    assert_eq!(plan.spaces.len(), 2, "the workspaces still arrive");
    assert!(plan.notes.iter().any(|n| {
        n == "Zen's bookmarks could not be read or are too large. If Zen is open, quit it and import again."
    }));
}

#[test]
fn a_zen_profile_with_no_bookmarks_gets_no_bookmarks_space() {
    let plan = with_bookmarks("[]");
    assert_eq!(plan.spaces.len(), 2);
    assert!(!plan.notes.iter().any(|n| n.contains("Bookmarks space")));
}
