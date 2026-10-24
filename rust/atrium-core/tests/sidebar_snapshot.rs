// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! the Rust sidebar model against the five-profile snapshot
//! pinned in H0.2.
//!
//! `fixtures/sidebar-model/five-profiles.json` is what the SWIFT producer
//! emits for a configuration built through the store's own mutations. This
//! rebuilds the same configuration as plain values and asserts the Rust
//! producer agrees.
//!
//! The two paths differ on purpose. Swift's went through the store, so the
//! fixture already carries what the store does - notably a redacted URL. Here
//! the configuration is written out directly, so the redacted form is what
//! goes in. That is the point: this checks the PRODUCER, and the store's own
//! behaviour is checked by the golden store corpus.
//!
//! The fixture is amended rather than regenerated: Swift is gone,
//! and its producer hardcoded `selected: false` too, so an all-false snapshot
//! matched a producer that could never select anything. The fixture now
//! drives a live strip with an active tab and pins the resolved space tints,
//! because those were the two values the byte-identical test was blind to.

use std::path::PathBuf;

use atrium_core::sidebar::{self, LiveTabIds};
use atrium_core::spaces;
use atrium_core::spaces::{Favorite, Profile, Space, SpaceTab, TabGroup};

const INSTANT: f64 = 788_918_400.0;

mod common;
use common::{read_fixture, repo_path};

fn fixture_path() -> PathBuf {
    repo_path("fixtures/sidebar-model/five-profiles.json")
}

fn store_path() -> PathBuf {
    repo_path("fixtures/stores/spaces/current.json")
}

fn tab(url: &str, title: &str) -> SpaceTab {
    SpaceTab::new(url, title)
}

fn space(id: &str, name: &str, emoji: &str, tint: &str, profile: &str, order: i64) -> Space {
    Space {
        id: id.into(),
        name: name.into(),
        emoji: emoji.into(),
        tint: tint.into(),
        profile_id: profile.into(),
        pinned_tabs: Vec::new(),
        tabs: Vec::new(),
        order,
        groups: Vec::new(),
        split: None,
        pinned_collapsed: false,
        tint_strength: 1.0,
        archive_ttl: None,
        legacy: Default::default(),
        canvas: None,
    }
}

/// The same seven spaces across five profiles the Swift test builds.
///
/// The Ops tab's URL is the REDACTED form. The store strips credentials on
/// write, so that is what the sidebar producer is handed in production - see
/// `docs/model-rules.md` R7.4.
fn configuration() -> (Vec<Space>, Vec<Favorite>) {
    let group_id = "00000000-0000-4000-8000-000000000001";

    let mut work = space("s1", "Work", "W", "#5B8DEF", "work", 0);
    work.pinned_tabs = vec![{
        let mut t = tab("https://example.com/inbox", "Inbox");
        t.pinned_url = Some("https://example.com/inbox".into());
        t
    }];
    let mut grouped_a = tab("https://example.com/grouped-a", "Grouped A");
    grouped_a.group_id = Some(group_id.into());
    let mut grouped_b = tab("https://example.com/grouped-b", "Grouped B");
    grouped_b.group_id = Some(group_id.into());
    let mut renamed = tab("https://example.com/renamed", "Page Title");
    renamed.custom_title = Some("My Name For It".into());
    work.tabs = vec![
        tab("https://example.com/one", "One"),
        grouped_a,
        grouped_b,
        renamed,
    ];
    work.groups = vec![TabGroup {
        id: group_id.into(),
        name: "Reading".into(),
        collapsed: false,
        icon: None,
    }];

    let work_side = space("s2", "Work Side", "S", "#8DEF5B", "work", 1);

    let mut personal = space("s3", "Personal", "P", "#EF5B8D", "personal", 2);
    personal.tabs = vec![tab("https://example.org/", "Home")];

    let mut ops = space("s4", "Ops", "O", "#5BEFEF", "ops", 3);
    ops.tabs = vec![tab("https://example.net/panel", "Panel")];

    let mut study = space("s5", "Study", "T", "#EFEF5B", "study", 4);
    study.tabs = vec![tab("https://example.edu/course", "Course")];

    let archive = space("s6", "Archive", "A", "#B0B0B0", "archive", 5);
    let hostile = space(
        "s7",
        "<script>alert(1)</script>",
        "X",
        "#000000",
        "archive",
        6,
    );

    let favorite = |url: &str, title: &str, profile: &str| Favorite {
        url: url.into(),
        title: title.into(),
        profile_id: profile.into(),
        space_id: None,
    };

    (
        vec![work, work_side, personal, ops, study, archive, hostile],
        vec![
            favorite("https://example.com/", "Example", "work"),
            favorite("https://example.com/two", "Second", "work"),
            favorite("https://example.org/", "Org", "personal"),
            favorite("https://example.net/", "Net", "ops"),
            favorite("https://example.edu/", "Edu", "study"),
        ],
    )
}

/// Compares parsed JSON. Key order is an encoder detail, and the two
/// implementations do not agree on it - what has to match is the document.
fn same_json(a: &str, b: &str) -> bool {
    let (Ok(a), Ok(b)) = (
        serde_json::from_str::<serde_json::Value>(a),
        serde_json::from_str::<serde_json::Value>(b),
    ) else {
        return false;
    };
    a == b
}

/// The Work space's strip: two live tabs, and the FIRST of them is the one
/// the window is showing.
///
/// Two rather than one on purpose. With a single live tab "has an id" and "is
/// the active one" are the same set, so the fixture would pin a producer that
/// merely marked every live row - which is a different bug with the same
/// picture.
fn live_strip() -> (Vec<(String, u64)>, Option<u64>) {
    (
        vec![
            ("https://example.com/one".to_string(), 11),
            ("https://example.com/renamed".to_string(), 12),
        ],
        Some(11),
    )
}

/// The fixture carries the folder id normalised to a counted series, so the
/// configuration above uses the same one.
#[test]
fn the_rust_producer_matches_the_pinned_five_profile_snapshot() {
    let want = read_fixture(&fixture_path());

    let (spaces, favorites) = configuration();
    let (strip, active) = live_strip();
    let mut live = LiveTabIds::new(&strip, active, &[], None);
    let got = sidebar::encode(&sidebar::snapshot(
        0,
        &spaces,
        &favorites,
        &mut live,
        &[],
        &[],
    ));

    assert!(
        same_json(&got, &want),
        "the Rust sidebar model does not match the pinned snapshot\nproduced: {got}"
    );
}

/// The fixture was all-`selected: false`, so the parity test above could not
/// have caught a producer that never selects anything, which matched it
/// perfectly. This asserts the fixture still covers the case, so the same
/// hole cannot reopen by someone regenerating it from a cold window.
#[test]
fn the_pinned_snapshot_covers_a_selected_row() {
    let want = read_fixture(&fixture_path());
    let model: serde_json::Value = serde_json::from_str(&want).unwrap();

    let tabs = model["tabs"].as_array().unwrap();
    let selected: Vec<&serde_json::Value> = tabs
        .iter()
        .filter(|t| t["selected"] == serde_json::Value::Bool(true))
        .collect();

    assert_eq!(selected.len(), 1, "the fixture pins exactly one active row");
    assert_ne!(selected[0]["id"], "", "a selected row is a live row");
    assert!(
        tabs.iter()
            .any(|t| t["id"] != "" && t["selected"] == serde_json::Value::Bool(false)),
        "and a live row that is NOT selected, so the two are told apart"
    );
}

/// The tint the surface paints with, not the name the file stores. The space
/// dots read `spaces[].color` and the row pill reads `spaceTint`.
#[test]
fn the_pinned_snapshot_carries_colours_rather_than_tint_names() {
    let want = read_fixture(&fixture_path());
    let model: serde_json::Value = serde_json::from_str(&want).unwrap();

    let is_hex = |value: &serde_json::Value| {
        value
            .as_str()
            .is_some_and(|s| s.starts_with('#') && s.len() == 7)
    };
    assert!(
        is_hex(&model["spaceTint"]),
        "spaceTint: {}",
        model["spaceTint"]
    );
    for space in model["spaces"].as_array().unwrap() {
        assert!(is_hex(&space["color"]), "space color: {}", space["color"]);
    }
}

/// The entry point the C++ consumer will call, driven end to end from a real
/// store file rather than from hand-built values.
#[test]
fn the_entry_point_builds_a_model_from_a_store_file() {
    let spaces_json = read_fixture(&store_path());

    // The fixture's active space is "Research", which holds two tabs. Only one
    // of them is live here, so the other row has to come back cold.
    let live = r#"[{"url":"https://example.com/one","id":11}]"#;
    let out = atrium_core::sidebar_model(&spaces_json, Some(1), live, 788_918_400.0)
        .expect("the fixture must load");

    let model: serde_json::Value = serde_json::from_str(&out).unwrap();
    assert_eq!(model["spaceName"], "Research");
    let tabs = model["tabs"].as_array().unwrap();
    assert_eq!(tabs.len(), 2);
    assert_eq!(tabs[0]["id"], "11");
    assert_eq!(
        tabs[1]["id"], "",
        "a row with no live tab is cold, not wrong"
    );
}

/// The `active` flag has to survive the JSON boundary, not just the Rust
/// call - the strip is the only place a per-window fact can come from.
#[test]
fn the_entry_point_marks_the_row_holding_the_active_tab() {
    let spaces_json = read_fixture(&store_path());

    let live = r#"[{"url":"https://example.com/one","id":11,"active":true}]"#;
    let out = atrium_core::sidebar_model(&spaces_json, Some(1), live, 788_918_400.0).unwrap();
    let model: serde_json::Value = serde_json::from_str(&out).unwrap();

    let tabs = model["tabs"].as_array().unwrap();
    assert_eq!(tabs[0]["selected"], true);
    assert_eq!(tabs[1]["selected"], false, "the cold row keeps no pill");
}

/// A caller that has not been taught to send `active` still gets a model -
/// one with no pill, which is what the sidebar drew before the flag existed.
#[test]
fn a_strip_without_the_flag_still_produces_a_model() {
    let spaces_json = read_fixture(&store_path());

    let live = r#"[{"url":"https://example.com/one","id":11}]"#;
    let out = atrium_core::sidebar_model(&spaces_json, Some(1), live, 788_918_400.0).unwrap();
    let model: serde_json::Value = serde_json::from_str(&out).unwrap();

    for tab in model["tabs"].as_array().unwrap() {
        assert_eq!(tab["selected"], false);
    }
}

/// The case C++ is actually in once Swift is gone: it does not know which
/// space this window's slot is showing, so the document decides.
#[test]
fn the_document_picks_the_active_space_when_the_caller_cannot() {
    let spaces_json = read_fixture(&store_path());

    let out = atrium_core::sidebar_model(&spaces_json, None, "[]", 788_918_400.0).unwrap();
    let model: serde_json::Value = serde_json::from_str(&out).unwrap();

    // The fixture's activeSpaceID names Research, which sorts second.
    assert_eq!(model["spaceName"], "Research");
    assert_eq!(model["activeSpace"], 1);
}

/// An activeSpaceID naming a space that is gone must not blank the sidebar.
/// The first space is what a fresh profile shows.
#[test]
fn an_unknown_active_space_id_falls_back_to_the_first_space() {
    // r##: the tint carries a #, which would end an r#"..."# string early.
    let spaces = r##"{"spaces":[{"id":"a","name":"Only","emoji":"O","tint":"#111111",
    "profileID":"p","pinnedTabs":[],"tabs":[],"order":0,"groups":[]}],
    "appState":{"activeSpaceID":"gone","sidebarWidth":220,"schemaVersion":3,
    "archiveEpoch":1,"sidebarWidthMigrated":true}}"##;
    let out = atrium_core::sidebar_model(spaces, None, "[]", 0.0).unwrap();
    let model: serde_json::Value = serde_json::from_str(&out).unwrap();
    assert_eq!(model["spaceName"], "Only");
}

/// A window with no live tabs at all is ordinary - every row is cold and the
/// sidebar still renders.
#[test]
fn an_empty_strip_still_produces_a_model() {
    let spaces_json = read_fixture(&store_path());
    let out = atrium_core::sidebar_model(&spaces_json, Some(0), "[]", 788_918_400.0).unwrap();
    let model: serde_json::Value = serde_json::from_str(&out).unwrap();
    assert_eq!(model["spaces"].as_array().unwrap().len(), 2);
}

/// A store file this build refuses to read is an error, not an empty model.
/// An empty model renders as a sidebar with nothing in it, which looks like
/// data loss to the user.
#[test]
fn a_refused_store_file_is_an_error_not_an_empty_model() {
    let refused = r#"{"spaces":[],"appState":{"sidebarWidth":280,"schemaVersion":999}}"#;
    assert!(atrium_core::sidebar_model(refused, Some(0), "[]", 0.0).is_err());
}

/// The password must not be in the fixture either. If it ever is, the snapshot
/// was regenerated from unsaved state and the leak is now pinned as correct.
#[test]
fn the_pinned_snapshot_carries_no_credentials() {
    let want = read_fixture(&fixture_path());
    assert!(
        !want.contains("hunter2"),
        "a password is pinned in the snapshot"
    );
    assert!(
        !want.contains("admin:"),
        "credentials are pinned in the snapshot"
    );
}

/// Mirror View offers "every OTHER profile plus Logged out",
/// so the surface has to know which profile each space belongs to. The chips
/// carried name, colour, icon and contents and nothing that named a profile,
/// which left the choice list underivable from the snapshot alone.
///
/// Asserted per chip rather than as a set, because the menu needs to exclude
/// the CURRENT space's profile - a bare list of the profiles in use cannot say
/// which one the user is already in.
#[test]
fn every_chip_names_the_profile_its_space_belongs_to() {
    let (spaces, favorites) = configuration();
    let (strip, active) = live_strip();
    let mut live = LiveTabIds::new(&strip, active, &[], None);
    let snapshot = sidebar::snapshot(0, &spaces, &favorites, &mut live, &[], &[]);

    let mut ordered: Vec<&atrium_core::spaces::Space> = spaces.iter().collect();
    ordered.sort_by_key(|s| s.order);

    let got: Vec<&str> = snapshot
        .spaces
        .iter()
        .map(|chip| chip.profile_id.as_str())
        .collect();
    let want: Vec<&str> = ordered.iter().map(|s| s.profile_id.as_str()).collect();
    assert_eq!(
        got, want,
        "chips must carry their space's profile, in order"
    );
}

/// The encoded document is what the surface actually reads, and the field has
/// to arrive camelCased like every other key on a chip.
#[test]
fn the_encoded_chip_carries_profile_id_camel_cased() {
    let (spaces, favorites) = configuration();
    let (strip, active) = live_strip();
    let mut live = LiveTabIds::new(&strip, active, &[], None);
    let encoded = sidebar::encode(&sidebar::snapshot(
        0,
        &spaces,
        &favorites,
        &mut live,
        &[],
        &[],
    ));

    let parsed: serde_json::Value = serde_json::from_str(&encoded).expect("encoded snapshot");
    let chips = parsed["spaces"].as_array().expect("spaces array");
    assert_eq!(
        chips[0]["profileId"], "work",
        "the first chip must name its profile as profileId"
    );
    assert!(
        chips.iter().all(|c| c["profileId"].is_string()),
        "every chip must carry profileId"
    );
}

/// The menus draw the profile's display name, which lives on the document's
/// profiles list and not on the space. Filled in after the build, like themes.
#[test]
fn mark_profile_names_labels_each_chip_from_the_profiles_list() {
    let (spaces, favorites) = configuration();
    let (strip, active) = live_strip();
    let mut live = LiveTabIds::new(&strip, active, &[], None);
    let mut snap = sidebar::snapshot(0, &spaces, &favorites, &mut live, &[], &[]);
    let profiles = vec![Profile {
        id: "work".into(),
        name: "Work Account".into(),
        search_engine: None,
        hibernate: None,
        hib_threshold: None,
    }];
    sidebar::mark_profile_names(&mut snap, &profiles);

    let parsed: serde_json::Value = serde_json::from_str(&sidebar::encode(&snap)).unwrap();
    let chips = parsed["spaces"].as_array().unwrap();
    assert_eq!(chips[0]["profileName"], "Work Account");
    let unnamed = chips
        .iter()
        .find(|c| c["profileId"] != "work")
        .expect("a chip on another profile");
    assert!(
        unnamed.get("profileName").is_none(),
        "a profile the document does not name has no profileName key, so the surface falls back to the id"
    );
}

const CANVAS_DOCUMENT: &str = r##"{"spaces":[
{"id":"A","name":"Work","emoji":"W","tint":"#112233","profileID":"Default",
 "canvas":{"x":120.0,"y":-40.0,"w":320.0,"h":260.0},
 "pinnedTabs":[{"url":"https://mail.example/","title":"Mail","canvasX":16.0}],
 "tabs":[{"url":"https://a/","title":"A","canvasX":8.5},
         {"url":"https://b/","title":"B"}],
 "order":0,"groups":[]},
{"id":"B","name":"Home","emoji":"H","tint":"#445566","profileID":"Default",
 "pinnedTabs":[],"tabs":[],"order":1,"groups":[]}],
"appState":{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":5,
"archiveEpoch":700000000,"sidebarWidthMigrated":true}}"##;

/// The snapshot carries both canvas values, so the Courtyard can lay the plane
/// out from the push it already receives rather than asking for a second one.
#[test]
fn the_snapshot_carries_the_room_rect_and_the_card_offset() {
    let doc = spaces::load(CANVAS_DOCUMENT, INSTANT).unwrap();
    let (strip, active) = live_strip();
    let mut live = LiveTabIds::new(&strip, active, &[], None);
    let snap = sidebar::snapshot(
        0,
        &doc.spaces,
        &doc.favorites,
        &mut live,
        &doc.hidden_rows,
        &[],
    );

    let chip = &snap.spaces[0];
    assert_eq!(
        chip.canvas,
        Some(spaces::CanvasRect {
            x: 120.0,
            y: -40.0,
            w: 320.0,
            h: 260.0
        })
    );
    assert_eq!(chip.tabs[0].canvas_x, Some(8.5));
    assert_eq!(
        chip.tabs[1].canvas_x, None,
        "a card with no offset carries none, so the view puts it at x 0"
    );
    assert_eq!(chip.pinned[0].rows()[0].canvas_x, Some(16.0));
    assert_eq!(
        snap.spaces[1].canvas, None,
        "an unplaced room carries no rect, so the view places it"
    );

    let json = serde_json::to_string(&snap).unwrap();
    assert!(
        !json.contains("\"canvasX\":null"),
        "an absent offset must be omitted, not written as null: {json}"
    );
}
