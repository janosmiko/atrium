// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The Arc reader against the hand-built fixtures in fixtures/import/arc.

use std::collections::BTreeMap;

use atrium_core::import::{self, ImportPlan, ImportSummary, PlannedGroup, PlannedProfile};
use serde_json::json;

mod common;

fn basic() -> ImportPlan {
    import::read("arc", &common::arc_fixture_files("basic")).expect("the basic fixture reads")
}

fn tab_view(tabs: &[import::PlannedTab]) -> Vec<(&str, &str, Option<usize>)> {
    tabs.iter().map(|t| (t.url.as_str(), t.title.as_str(), t.group)).collect()
}

#[test]
fn profiles_take_their_names_from_local_state() {
    assert_eq!(
        basic().profiles,
        [
            PlannedProfile { key: "default".into(), name: "Personal".into(), is_default: true },
            PlannedProfile { key: "Profile 1".into(), name: "Side Project".into(), is_default: false },
        ]
    );
}

#[test]
fn a_space_keeps_its_pinned_tree_with_nested_folders_moved_up() {
    let plan = basic();
    let work = &plan.spaces[0];
    assert_eq!((work.key.as_str(), work.name.as_str(), work.profile_key.as_str()), ("SPACE-WORK", "Work", "default"));
    assert_eq!(work.groups, [PlannedGroup { name: "Reference".into() }]);
    assert_eq!(
        tab_view(&work.pinned),
        [
            ("https://docs.example.test/", "Docs", None),
            ("https://spec.example.test/", "Spec", Some(0)),
            ("https://deep.example.test/", "Deep", Some(0)),
        ]
    );
    assert_eq!(work.pinned[0].last_active_at, Some(700_000_000.0));
}

#[test]
fn an_unpinned_tab_keeps_its_rename_and_its_last_active_time() {
    let plan = basic();
    let work = &plan.spaces[0];
    assert_eq!(tab_view(&work.tabs), [("https://news.example.test/", "My News", None)]);
    assert_eq!(work.tabs[0].last_active_at, Some(700_000_000.5));
}

#[test]
fn every_space_is_read_in_sidebar_order() {
    let plan = basic();
    let names: Vec<&str> = plan.spaces.iter().map(|s| s.name.as_str()).collect();
    assert_eq!(names, ["Work", "Home", "Side"]);
    assert_eq!(tab_view(&plan.spaces[1].tabs), [("https://mail.example.test/", "Mail", None)]);
    assert_eq!(plan.spaces[2].profile_key, "Profile 1");
}

#[test]
fn emoji_come_from_emoji_v2_or_the_code_point() {
    let plan = basic();
    assert_eq!(plan.spaces[0].emoji.as_deref(), Some("\u{1F9EA}"));
    assert_eq!(plan.spaces[1].emoji.as_deref(), Some("\u{1F3E0}"));
    assert_eq!(plan.spaces[2].emoji, None, "an SF Symbol has no emoji");
}

#[test]
fn colours_are_clamped_from_extended_srgb() {
    let plan = basic();
    assert_eq!(plan.spaces[0].tint.as_deref(), Some("#FF8000"));
    assert_eq!(plan.spaces[1].tint, None, "Home has no midTone");

    let work = plan.spaces[0].theme.as_ref().expect("gradient theme");
    let stops: Vec<(&str, f64, f64)> = work.stops.iter().map(|s| (s.color.as_str(), s.x, s.alpha)).collect();
    assert_eq!(stops, [("#FF0000", 0.25, 1.0), ("#0000FF", 0.75, 0.5)]);
    assert_eq!(work.effects.grain, 0.25);
    assert_eq!(work.name, "Work");

    let home = plan.spaces[1].theme.as_ref().expect("single colour theme");
    assert_eq!(home.stops.len(), 1);
    assert_eq!(home.stops[0].color, "#004080");
    assert_eq!(plan.spaces[2].theme, None);
}

#[test]
fn favourites_skip_a_profile_from_another_mac() {
    let plan = basic();
    let favs: Vec<(&str, &str)> = plan.favorites.iter().map(|f| (f.url.as_str(), f.profile_key.as_str())).collect();
    assert_eq!(favs, [("https://fav.example.test/", "default"), ("https://fav-side.example.test/", "Profile 1")]);
}

#[test]
fn routes_map_match_types_and_destinations() {
    let plan = basic();
    let routes: Vec<(&str, &str, Option<&str>)> = plan
        .routes
        .iter()
        .map(|r| (r.match_type.as_str(), r.pattern.as_str(), r.target_space_key.as_deref()))
        .collect();
    assert_eq!(
        routes,
        [
            ("contains", "docs.example.test", Some("SPACE-WORK")),
            ("regex", r"^https://mail\.example\.test/", None),
            ("regex", r"example\.test/reports", Some("SPACE-GONE")),
            ("regex", "([", Some("SPACE-HOME")),
        ]
    );
}

#[test]
fn notes_name_each_kind_of_thing_that_was_skipped() {
    let notes = basic().notes;
    assert_eq!(notes.len(), 4, "{notes:?}");
    for needle in ["another Mac", "inside folders", "easel", "path"] {
        assert!(notes.iter().any(|n| n.contains(needle)), "no note about {needle}: {notes:?}");
    }
}

#[test]
fn the_summary_of_the_basic_fixture() {
    assert_eq!(
        import::summary(&basic()),
        ImportSummary { profiles: 2, spaces: 3, tabs: 6, favorites: 2, routes: 4, engines: 0, notes: 4 }
    );
}

#[test]
fn missing_files_give_an_empty_plan_with_a_note_each() {
    let plan = import::read("arc", &BTreeMap::new()).expect("missing files are not an error");
    assert!(plan.spaces.is_empty() && plan.routes.is_empty() && plan.favorites.is_empty());
    assert_eq!(plan.notes.len(), 3, "{:?}", plan.notes);
}

#[test]
fn a_file_that_does_not_parse_fails_the_whole_source() {
    let err = import::read("arc", &common::arc_fixture_files("broken")).unwrap_err();
    assert!(err.contains("StorableSidebar.json could not be read"), "{err}");
}

#[test]
fn an_unknown_source_is_an_error() {
    assert!(import::read("netscape", &BTreeMap::new()).is_err());
}

/// A pinned root followed by `depth` nested folders, the last holding one tab.
fn sidebar_with_folder_chain(depth: usize) -> String {
    let mut items = vec![json!({
        "id": "ROOT", "parentID": null, "childrenIds": ["F0"], "title": null,
        "data": {"itemContainer": {"containerType": {"spaceItems": {"_0": "SPACE-DEEP"}}}}
    })];
    for i in 0..depth {
        let child = if i + 1 < depth { format!("F{}", i + 1) } else { "TAB-END".to_string() };
        items.push(json!({
            "id": format!("F{i}"), "parentID": null, "childrenIds": [child], "title": format!("Folder {i}"),
            "data": {"list": {}}
        }));
    }
    items.push(json!({
        "id": "TAB-END", "parentID": null, "childrenIds": [], "title": null,
        "data": {"tab": {"savedURL": "https://deep.example.test/", "savedTitle": "Deep tab"}}
    }));
    let items_array: Vec<serde_json::Value> = items
        .iter()
        .flat_map(|item| [item["id"].clone(), item.clone()])
        .collect();
    json!({
        "sidebar": {
            "containers": [
                {"global": {}},
                {
                    "spaces": ["SPACE-DEEP", {
                        "id": "SPACE-DEEP", "title": "Deep", "profile": {"default": true},
                        "containerIDs": ["pinned", "ROOT"], "newContainerIDs": []
                    }],
                    "items": items_array
                }
            ]
        }
    })
    .to_string()
}

#[test]
fn a_folder_chain_past_the_depth_limit_stops_without_a_panic() {
    let mut files = BTreeMap::new();
    files.insert("StorableSidebar.json".to_string(), sidebar_with_folder_chain(100));
    let plan = import::read("arc", &files).expect("a deep folder chain must not panic or error");
    assert!(plan.notes.iter().any(|n| n.contains("64")), "no depth-limit note: {:?}", plan.notes);
}

/// One space with `items` under its unpinned container "U", plus `top_apps`.
fn sidebar(space: serde_json::Value, items: Vec<serde_json::Value>, top_apps: serde_json::Value) -> BTreeMap<String, String> {
    let items: Vec<serde_json::Value> = items.iter().flat_map(|i| [i["id"].clone(), i.clone()]).collect();
    let id = space["id"].clone();
    let file = json!({"sidebar": {"containers": [{"global": {}}, {
        "topAppsContainerIDs": top_apps, "spaces": [id, space], "items": items
    }]}});
    BTreeMap::from([("StorableSidebar.json".to_string(), file.to_string())])
}

fn container(id: &str, children: &[&str]) -> serde_json::Value {
    json!({"id": id, "childrenIds": children, "data": {"itemContainer": {}}})
}

fn tab(id: &str, url: &str, title: &str) -> serde_json::Value {
    json!({"id": id, "childrenIds": [], "data": {"tab": {"savedURL": url, "savedTitle": title}}})
}

fn custom(dir: &str) -> serde_json::Value {
    json!({"custom": {"_0": {"directoryBasename": dir}}})
}

#[test]
fn a_local_profile_with_no_local_state_name_keeps_its_favourites() {
    let space = json!({"id": "S", "title": "Side", "profile": custom("Profile 2"), "containerIDs": ["unpinned", "U"]});
    let items = vec![
        container("U", &[]),
        container("FAV", &["T"]),
        tab("T", "https://fav.example.test/", "Fav"),
        container("OTHER", &["T2"]),
        tab("T2", "https://other.example.test/", "Other"),
    ];
    let top = json!([custom("Profile 2"), "FAV", custom("Profile 9"), "OTHER"]);
    let plan = import::read("arc", &sidebar(space, items, top)).unwrap();
    let favs: Vec<(&str, &str)> = plan.favorites.iter().map(|f| (f.url.as_str(), f.profile_key.as_str())).collect();
    assert_eq!(favs, [("https://fav.example.test/", "Profile 2")], "a profile with spaces here is local");
    assert_eq!(plan.profiles[0].name, "Arc Profile 2");
    assert!(plan.notes.iter().any(|n| n.contains("another Mac")), "Profile 9 has no spaces: {:?}", plan.notes);
}

#[test]
fn a_control_code_point_is_no_emoji_and_long_text_is_clamped() {
    for code in [0, 7, 0x9F] {
        let space = json!({"id": "S", "title": "S", "profile": {"default": true}, "containerIDs": ["unpinned", "U"],
                           "customInfo": {"iconType": {"emoji": code}}});
        let plan = import::read("arc", &sidebar(space, vec![container("U", &[])], json!([]))).unwrap();
        assert_eq!(plan.spaces[0].emoji, None, "code point {code}");
    }
    let long = "x".repeat(500);
    let space = json!({"id": "S", "title": long, "profile": {"default": true}, "containerIDs": ["unpinned", "U"],
                       "customInfo": {"iconType": {"emoji_v2": long}}});
    let items = vec![
        container("U", &["F", "T"]),
        json!({"id": "F", "title": long, "childrenIds": [], "data": {"list": {}}}),
        tab("T", "https://long.example.test/", &long),
    ];
    let plan = import::read("arc", &sidebar(space, items, json!([]))).unwrap();
    let space = &plan.spaces[0];
    assert_eq!(space.name.chars().count(), 200);
    assert_eq!(space.groups[0].name.chars().count(), 200);
    assert_eq!(space.tabs[0].title.chars().count(), 200);
    assert!(space.emoji.as_ref().unwrap().chars().count() <= 16);
}

#[test]
fn a_children_ids_cycle_stops_the_walk() {
    let space = json!({"id": "S", "title": "S", "profile": {"default": true}, "containerIDs": ["unpinned", "U"]});
    let items = vec![
        container("U", &["A"]),
        json!({"id": "A", "title": "A", "childrenIds": ["B"], "data": {"list": {}}}),
        json!({"id": "B", "title": "B", "childrenIds": ["A", "T", "B"], "data": {"list": {}}}),
        tab("T", "https://cycle.example.test/", "Cycle"),
    ];
    let plan = import::read("arc", &sidebar(space, items, json!([]))).expect("a cycle must not fail the read");
    assert_eq!(tab_view(&plan.spaces[0].tabs), [("https://cycle.example.test/", "Cycle", Some(0))]);
}
