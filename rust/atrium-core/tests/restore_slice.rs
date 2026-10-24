// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! the one verb that puts a whole window or space slice back.
//!
//! Rewind computes a state to return to (chrome/browser/resources/atrium/
//! rewind.js) and the per-gesture sidebar verbs cannot reach it. This is the
//! core half: the payload, its validation, and what it does to the document.
//!
//! The tests assert on the DOCUMENT and on the snapshot the sidebar would
//! render from it, never on the return value alone - a restore that reported
//! success and wrote nothing is the failure this suite exists to catch.

use atrium_core::mutate::test_support;
use atrium_core::restore::{self, Refusal};
use atrium_core::sidebar::{self, LiveTabIds};
use atrium_core::spaces::{Document, Favorite, TabGroup, assign_tokens};
use atrium_core::tabs::Bindings;

/// Two spaces on one profile, the first with a pinned row, a folder and two
/// loose rows, the second with one row. Enough for "the other space is
/// untouched" to mean something.
fn document() -> Document {
    let mut work = test_support::space("Work", 0);
    work.pinned_tabs = vec![test_support::tab("https://pin.example/")];
    work.groups = vec![TabGroup {
        id: "g1".into(),
        name: "Reading".into(),
        collapsed: false,
        icon: None,
    }];
    work.tabs = vec![
        test_support::tab("https://one.example/"),
        test_support::grouped("https://in-folder.example/", "g1"),
        test_support::tab("https://two.example/"),
    ];

    let mut home = test_support::space("Home", 1);
    home.tabs = vec![test_support::tab("https://home.example/")];

    let mut doc = test_support::document(vec![work, home]);
    doc.app_state.active_space_id = Some("id-Work".into());
    doc.app_state.sidebar_width = 260.0;
    doc.app_state.archive_epoch = None;
    doc.favorites = vec![Favorite {
        url: "https://fav.example/".into(),
        title: "Fav".into(),
        profile_id: "p".into(),
        space_id: None,
    }];
    assign_tokens(&mut doc);
    doc
}

/// The snapshot the sidebar would render for `position`, with no live tabs -
/// which is what makes it a statement about the DOCUMENT and not about a
/// window that happens to be open.
fn chip_at(doc: &Document, position: usize) -> sidebar::SpaceChip {
    let mut live = LiveTabIds::new(&[], None, &[], None);
    let snap = sidebar::snapshot(
        position,
        &doc.spaces,
        &doc.favorites,
        &mut live,
        &doc.hidden_rows,
        &[],
    );
    snap.spaces[position].clone()
}

fn payload_from(doc: &Document, positions: &[usize]) -> restore::Payload {
    let slices = positions
        .iter()
        .map(|&at| restore::Slice::from_chip(at, &chip_at(doc, at)))
        .collect();
    restore::Payload {
        scope: restore::Scope::Window,
        active_space: 0,
        spaces: slices,
    }
}

// --- every field survives -------------------------------------------

#[test]
fn a_round_trip_puts_every_structural_field_back() {
    let doc = document();
    let before = chip_at(&doc, 0);

    // Start from a document mangled in every way the payload claims to carry:
    // name, order, pinned order, folder fold state, folder membership.
    let mut mangled = document();
    mangled.spaces[0].name = "Renamed".into();
    mangled.spaces[0].pinned_collapsed = true;
    mangled.spaces[0].groups[0].collapsed = true;
    mangled.spaces[0].groups[0].name = "Clobbered".into();
    mangled.spaces[0].tabs.reverse();
    mangled.spaces[0].pinned_tabs.clear();

    let payload = payload_from(&doc, &[0, 1]);
    let mut bindings = Bindings::new();
    assert!(restore::apply(&mut mangled, &mut bindings, &payload).is_ok());

    assert_eq!(
        chip_at(&mangled, 0),
        before,
        "the whole slice must come back"
    );
}

#[test]
fn the_payload_carries_the_space_name_and_the_pinned_fold() {
    let mut doc = document();
    doc.spaces[0].name = "Renamed".into();
    doc.spaces[0].pinned_collapsed = true;
    let payload = payload_from(&doc, &[0]);

    let mut target = document();
    let mut bindings = Bindings::new();
    assert!(restore::apply(&mut target, &mut bindings, &payload).is_ok());

    assert_eq!(target.spaces[0].name, "Renamed");
    assert!(target.spaces[0].pinned_collapsed);
}

#[test]
fn favorites_come_back_for_the_slices_profile() {
    let doc = document();
    let payload = payload_from(&doc, &[0]);

    let mut target = document();
    target.favorites.clear();
    let mut bindings = Bindings::new();
    assert!(restore::apply(&mut target, &mut bindings, &payload).is_ok());

    assert_eq!(target.favorites.len(), 1);
    assert_eq!(target.favorites[0].url, "https://fav.example/");
    assert_eq!(target.favorites[0].profile_id, "p");
}

/// Favourites follow the PROFILE, so a restore must not touch another
/// profile's grid just because it rewrote this space.
#[test]
fn another_profiles_favorites_are_left_alone() {
    let doc = document();
    let payload = payload_from(&doc, &[0]);

    let mut target = document();
    target.favorites.push(Favorite {
        url: "https://other.example/".into(),
        title: "Other".into(),
        profile_id: "p2".into(),
        space_id: None,
    });
    let mut bindings = Bindings::new();
    assert!(restore::apply(&mut target, &mut bindings, &payload).is_ok());

    assert!(
        target
            .favorites
            .iter()
            .any(|f| f.profile_id == "p2" && f.url == "https://other.example/"),
        "p2's grid is not this slice's to rewrite"
    );
}

// --- whole-window restore -------------------------------------------

#[test]
fn a_whole_window_restore_reinstates_every_space_and_the_active_one() {
    let doc = document();
    let mut payload = payload_from(&doc, &[0, 1]);
    payload.active_space = 1;

    let mut target = document();
    target.spaces[0].tabs.clear();
    target.spaces[1].tabs.clear();
    target.app_state.active_space_id = Some("id-Work".into());

    let mut bindings = Bindings::new();
    assert!(restore::apply(&mut target, &mut bindings, &payload).is_ok());

    assert_eq!(chip_at(&target, 0), chip_at(&doc, 0));
    assert_eq!(chip_at(&target, 1), chip_at(&doc, 1));
    assert_eq!(target.app_state.active_space_id.as_deref(), Some("id-Home"));
}

// --- a per-space restore touches one space --------------------------

#[test]
fn a_per_space_restore_leaves_every_other_space_identical() {
    let doc = document();
    let mut payload = payload_from(&doc, &[0]);
    payload.scope = restore::Scope::Space(0);
    payload.active_space = 0;

    let mut target = document();
    target.spaces[0].tabs.clear();
    target.spaces[1]
        .tabs
        .push(test_support::tab("https://added-since.example/"));
    let untouched = target.spaces[1].clone();

    let mut bindings = Bindings::new();
    assert!(restore::apply(&mut target, &mut bindings, &payload).is_ok());

    assert_eq!(chip_at(&target, 0), chip_at(&doc, 0));
    assert_eq!(
        target.spaces[1], untouched,
        "a per-space restore must not reach into a neighbour"
    );
}

/// The scope names one space and the payload must carry exactly that one. A
/// slice for a space the scope did not name would be a whole-window restore
/// wearing a per-space label.
#[test]
fn a_per_space_payload_carrying_another_slice_is_refused() {
    let doc = document();
    let mut payload = payload_from(&doc, &[0, 1]);
    payload.scope = restore::Scope::Space(0);

    let mut target = document();
    let before = target.clone();
    let mut bindings = Bindings::new();
    assert_eq!(
        restore::apply(&mut target, &mut bindings, &payload),
        Err(Refusal::ScopeMismatch)
    );
    assert_eq!(target, before, "a refused restore writes nothing");
}

// --- open rows move, gone rows are reopened -------------------------

#[test]
fn a_row_a_live_tab_is_showing_keeps_its_identity() {
    let doc = document();
    let mut target = document();
    let mut bindings = Bindings::new();

    // Tab 7 is showing the second loose row. Bind it, then restore an order
    // that moves that row to the front.
    let token = target.spaces[0]
        .tabs
        .iter()
        .find(|t| t.url == "https://two.example/")
        .expect("the row is in the fixture")
        .token;
    assert!(bindings.bind(7, token));

    let mut live = LiveTabIds::new(&[("https://two.example/".into(), 7)], Some(7), &[], None);
    let snap = sidebar::snapshot(
        0,
        &doc.spaces,
        &doc.favorites,
        &mut live,
        &doc.hidden_rows,
        &[],
    );
    let mut chip = snap.spaces[0].clone();
    chip.tabs.reverse();
    let payload = restore::Payload {
        scope: restore::Scope::Window,
        active_space: 0,
        spaces: vec![restore::Slice::from_chip(0, &chip)],
    };

    let plan = restore::apply(&mut target, &mut bindings, &payload).expect("accepted");

    assert_eq!(
        bindings.token_of(7),
        Some(token),
        "the live tab still points at the row it was showing"
    );
    assert_eq!(
        target.spaces[0]
            .tabs
            .iter()
            .find(|t| t.url == "https://two.example/")
            .map(|t| t.token),
        Some(token),
        "the row was moved, not reopened"
    );
    assert!(
        !plan.reopen.iter().any(|r| r.url == "https://two.example/"),
        "a row still open must not be handed to the browser to reopen"
    );
}

/// Two rows on ONE url, one of them under a live tab. The URL is not an
/// identity, so only the live-tab join can say which row the payload means -
/// and getting it wrong moves the user's tab under the other row.
#[test]
fn two_rows_on_one_url_are_told_apart_by_the_live_tab() {
    let mut target = document();
    target.spaces[0].tabs = vec![
        test_support::tab("https://same.example/"),
        test_support::tab("https://same.example/"),
    ];
    assign_tokens(&mut target);
    let first = target.spaces[0].tabs[0].token;
    let second = target.spaces[0].tabs[1].token;
    assert_ne!(first, second);

    let mut bindings = Bindings::new();
    assert!(bindings.bind(7, second));

    // The moment had the live row FIRST and the cold one after it.
    let payload = restore::Payload {
        scope: restore::Scope::Window,
        active_space: 0,
        spaces: vec![restore::Slice {
            index: 0,
            name: "Work".into(),
            tabs: vec![
                restore::Row {
                    id: "7".into(),
                    url: "https://same.example/".into(),
                    title: "Same".into(),
                    selected: false,
                },
                restore::Row {
                    id: String::new(),
                    url: "https://same.example/".into(),
                    title: "Same".into(),
                    selected: false,
                },
            ],
            ..Default::default()
        }],
    };

    assert!(restore::apply(&mut target, &mut bindings, &payload).is_ok());

    assert_eq!(
        target.spaces[0].tabs[0].token, second,
        "the row the live tab is on must land where the payload put its id"
    );
    assert_eq!(target.spaces[0].tabs[1].token, first);
    assert_eq!(bindings.token_of(7), Some(second));
}

#[test]
fn a_row_that_is_gone_comes_back_from_its_url() {
    let doc = document();
    let payload = payload_from(&doc, &[0]);

    let mut target = document();
    target.spaces[0]
        .tabs
        .retain(|t| t.url != "https://two.example/");

    let mut bindings = Bindings::new();
    let plan = restore::apply(&mut target, &mut bindings, &payload).expect("accepted");

    assert!(
        target.spaces[0]
            .tabs
            .iter()
            .any(|t| t.url == "https://two.example/"),
        "the row is back in the document"
    );
    assert_eq!(chip_at(&target, 0), chip_at(&doc, 0));
    assert!(
        plan.reopen.is_empty(),
        "a COLD row is restored as a cold row; nothing asks the browser to \
         open a tab the moment did not have open"
    );
}

/// The moment held a live tab on a row, and that tab is gone now. The row
/// comes back cold AND the browser is told to reopen it, which is the half a
/// document write cannot do.
#[test]
fn a_row_whose_live_tab_is_gone_is_handed_back_to_the_browser() {
    let doc = document();
    let mut live = LiveTabIds::new(&[("https://one.example/".into(), 42)], Some(42), &[], None);
    let snap = sidebar::snapshot(
        0,
        &doc.spaces,
        &doc.favorites,
        &mut live,
        &doc.hidden_rows,
        &[],
    );
    let payload = restore::Payload {
        scope: restore::Scope::Window,
        active_space: 0,
        spaces: vec![restore::Slice::from_chip(0, &snap.spaces[0])],
    };

    let mut target = document();
    let mut bindings = Bindings::new();
    let plan = restore::apply(&mut target, &mut bindings, &payload).expect("accepted");

    assert_eq!(
        plan.reopen
            .iter()
            .map(|r| r.url.as_str())
            .collect::<Vec<_>>(),
        vec!["https://one.example/"],
    );
    assert_eq!(plan.reopen[0].space, 0);
    assert_eq!(
        plan.select_tab_id, None,
        "42 is the moment's tab id and that tab is gone; Chromium never reuses \
         an id, so naming it would send the browser after a tab that can never \
         exist again"
    );
    assert!(
        plan.reopen[0].selected,
        "the selection rides on the reopen entry instead - the browser selects \
         whatever tab it opens for this row"
    );
}

/// The other half of the same contract: the selected row's tab is still open,
/// so the browser is told its id and nothing needs reopening.
#[test]
fn a_selected_row_whose_tab_is_still_live_is_named_by_its_id() {
    let doc = document();
    let mut live = LiveTabIds::new(&[("https://one.example/".into(), 42)], Some(42), &[], None);
    let snap = sidebar::snapshot(
        0,
        &doc.spaces,
        &doc.favorites,
        &mut live,
        &doc.hidden_rows,
        &[],
    );
    let payload = restore::Payload {
        scope: restore::Scope::Window,
        active_space: 0,
        spaces: vec![restore::Slice::from_chip(0, &snap.spaces[0])],
    };

    // The same tab is still on the same row, so the moment's id is still good.
    let mut target = document();
    let token = target.spaces[0]
        .tabs
        .iter()
        .find(|t| t.url == "https://one.example/")
        .expect("the row is there")
        .token;
    let mut bindings = Bindings::new();
    assert!(bindings.bind(42, token));

    let plan = restore::apply(&mut target, &mut bindings, &payload).expect("accepted");

    assert_eq!(plan.select_tab_id, Some(42));
    assert!(
        plan.reopen.is_empty(),
        "a live row is moved, not reopened, so no entry can carry the selection"
    );
}

/// Only ONE reopen entry is the selected one. Without the marker the browser
/// would have to guess which of several reopened rows to land on.
#[test]
fn only_the_selected_row_is_flagged_among_several_reopened_ones() {
    let doc = document();
    let mut live = LiveTabIds::new(
        &[
            ("https://one.example/".into(), 42),
            ("https://two.example/".into(), 43),
        ],
        Some(43),
        &[],
        None,
    );
    let snap = sidebar::snapshot(
        0,
        &doc.spaces,
        &doc.favorites,
        &mut live,
        &doc.hidden_rows,
        &[],
    );
    let payload = restore::Payload {
        scope: restore::Scope::Window,
        active_space: 0,
        spaces: vec![restore::Slice::from_chip(0, &snap.spaces[0])],
    };

    let mut target = document();
    let mut bindings = Bindings::new();
    let plan = restore::apply(&mut target, &mut bindings, &payload).expect("accepted");

    let flagged: Vec<&str> = plan
        .reopen
        .iter()
        .filter(|r| r.selected)
        .map(|r| r.url.as_str())
        .collect();
    assert_eq!(flagged, vec!["https://two.example/"]);
    assert_eq!(plan.reopen.len(), 2, "both gone rows are handed back");
    assert_eq!(plan.select_tab_id, None);
}

/// A tab opened SINCE the moment has no row in it. The row goes, the binding
/// goes with it, and the browser is told to close the tab - otherwise a
/// restore adds tabs rather than putting a state back.
#[test]
fn a_tab_the_moment_did_not_hold_is_handed_back_to_be_closed() {
    let doc = document();
    let payload = payload_from(&doc, &[0]);

    let mut target = document();
    let mut since = test_support::tab("https://since.example/");
    since.token = 900;
    target.spaces[0].tabs.push(since);
    let mut bindings = Bindings::new();
    assert!(bindings.bind(11, 900));

    let plan = restore::apply(&mut target, &mut bindings, &payload).expect("accepted");

    assert_eq!(plan.close_tab_ids, vec![11]);
    assert_eq!(bindings.token_of(11), None, "the binding is released");
    assert!(
        !target.spaces[0]
            .tabs
            .iter()
            .any(|t| t.url == "https://since.example/"),
        "the row the moment did not hold is gone"
    );
}

// --- a space that does not exist ------------------------------------

#[test]
fn a_slice_for_a_space_that_does_not_exist_is_refused() {
    let doc = document();
    let mut payload = payload_from(&doc, &[0]);
    payload.spaces[0].index = 9;

    let mut target = document();
    let before = target.clone();
    let mut bindings = Bindings::new();
    assert_eq!(
        restore::apply(&mut target, &mut bindings, &payload),
        Err(Refusal::NoSuchSpace(9))
    );
    assert_eq!(
        target, before,
        "an unknown space is neither created nor emptied"
    );
}

#[test]
fn a_scope_naming_a_space_that_does_not_exist_is_refused() {
    let doc = document();
    let mut payload = payload_from(&doc, &[0]);
    payload.scope = restore::Scope::Space(9);

    let mut target = document();
    let before = target.clone();
    let mut bindings = Bindings::new();
    assert_eq!(
        restore::apply(&mut target, &mut bindings, &payload),
        Err(Refusal::NoSuchSpace(9))
    );
    assert_eq!(target, before);
}

// --- malformed payloads leave the window as it was ------------------

#[test]
fn an_empty_payload_is_refused() {
    let mut target = document();
    let before = target.clone();
    let mut bindings = Bindings::new();
    let payload = restore::Payload {
        scope: restore::Scope::Window,
        active_space: 0,
        spaces: Vec::new(),
    };
    assert_eq!(
        restore::apply(&mut target, &mut bindings, &payload),
        Err(Refusal::NoSlices)
    );
    assert_eq!(target, before);
}

#[test]
fn two_slices_for_one_space_are_refused() {
    let doc = document();
    let mut payload = payload_from(&doc, &[0, 1]);
    payload.spaces[1].index = 0;

    let mut target = document();
    let before = target.clone();
    let mut bindings = Bindings::new();
    assert_eq!(
        restore::apply(&mut target, &mut bindings, &payload),
        Err(Refusal::DuplicateSlice(0))
    );
    assert_eq!(target, before);
}

#[test]
fn a_row_with_no_url_is_refused_and_nothing_is_written() {
    let doc = document();
    let mut payload = payload_from(&doc, &[0, 1]);
    payload.spaces[1].tabs[0].url.clear();

    let mut target = document();
    let before = target.clone();
    let mut bindings = Bindings::new();
    assert_eq!(
        restore::apply(&mut target, &mut bindings, &payload),
        Err(Refusal::EmptyUrl)
    );
    assert_eq!(
        target, before,
        "the first slice must not survive the second slice's refusal"
    );
}

#[test]
fn a_folder_row_naming_no_folder_is_refused() {
    let doc = document();
    let mut payload = payload_from(&doc, &[0]);
    payload.spaces[0].folders[0].id.clear();

    let mut target = document();
    let before = target.clone();
    let mut bindings = Bindings::new();
    assert_eq!(
        restore::apply(&mut target, &mut bindings, &payload),
        Err(Refusal::EmptyFolderId)
    );
    assert_eq!(target, before);
}

#[test]
fn an_active_space_out_of_range_is_refused() {
    let doc = document();
    let mut payload = payload_from(&doc, &[0, 1]);
    payload.active_space = 9;

    let mut target = document();
    let before = target.clone();
    let mut bindings = Bindings::new();
    assert_eq!(
        restore::apply(&mut target, &mut bindings, &payload),
        Err(Refusal::NoSuchSpace(9))
    );
    assert_eq!(target, before);
}

// --- The JSON the ABI actually carries ------------------------------------

#[test]
fn the_payload_parses_from_the_shape_rewind_computes() {
    let json = r#"{
      "scope": "all",
      "activeSpace": 1,
      "spaces": [
        {
          "index": 0,
          "name": "Work",
          "pinnedCollapsed": true,
          "profileID": "p1",
          "pinned": [{"id": "", "url": "https://pin.example/", "title": "Pin"}],
          "tabs": [{"id": "7", "url": "https://one.example/", "title": "One", "selected": true}],
          "folders": [
            {"id": "g1", "name": "Reading", "closed": false,
             "tabs": [{"id": "", "url": "https://in-folder.example/", "title": "In"}]}
          ],
          "favorites": [{"id": "https://fav.example/", "url": "https://fav.example/", "title": "Fav"}]
        }
      ]
    }"#;
    let payload = restore::parse(json).expect("the rewind shape parses");
    assert_eq!(payload.scope, restore::Scope::Window);
    assert_eq!(payload.active_space, 1);
    assert_eq!(payload.spaces.len(), 1);
    let slice = &payload.spaces[0];
    assert_eq!(slice.index, 0);
    assert_eq!(slice.name, "Work");
    assert!(slice.pinned_collapsed);
    assert_eq!(slice.pinned.len(), 1);
    assert_eq!(slice.tabs[0].id, "7");
    assert!(slice.tabs[0].selected);
    assert_eq!(slice.folders[0].id, "g1");
    assert_eq!(slice.folders[0].tabs.len(), 1);
    assert_eq!(slice.favorites[0].url, "https://fav.example/");
}

// rewind.js flattens a pinned folder to member rows before sending, same as
// Slice::from_chip - the wire pinned array never carries a folder shape.
#[test]
fn the_payload_parses_a_pinned_folder_flattened_to_member_rows() {
    let json = r#"{
      "scope": "all",
      "activeSpace": 0,
      "spaces": [
        {
          "index": 0,
          "name": "Work",
          "pinned": [
            {"id": "p1", "url": "https://one.example/", "title": "First", "selected": false},
            {"id": "p2", "url": "https://two.example/", "title": "Second", "selected": false}
          ]
        }
      ]
    }"#;
    let payload = restore::parse(json).expect("a flattened pinned folder parses");
    let slice = &payload.spaces[0];
    assert_eq!(slice.pinned.len(), 2);
    assert_eq!(slice.pinned[0].url, "https://one.example/");
    assert_eq!(slice.pinned[1].url, "https://two.example/");
}

#[test]
fn a_pinned_folder_flattened_by_rewind_restores_successfully() {
    let doc = document();
    let mut payload = payload_from(&doc, &[0]);
    payload.spaces[0].pinned = vec![
        restore::Row {
            id: "p1".into(),
            url: "https://one.example/".into(),
            title: "First".into(),
            selected: false,
        },
        restore::Row {
            id: "p2".into(),
            url: "https://two.example/".into(),
            title: "Second".into(),
            selected: false,
        },
    ];

    let mut target = document();
    let mut bindings = Bindings::new();
    assert!(restore::apply(&mut target, &mut bindings, &payload).is_ok());
    assert_eq!(target.spaces[0].pinned_tabs.len(), 2);
    assert_eq!(target.spaces[0].pinned_tabs[0].url, "https://one.example/");
    assert_eq!(target.spaces[0].pinned_tabs[1].url, "https://two.example/");
}

#[test]
fn a_numeric_scope_parses_as_that_space() {
    let payload = restore::parse(
        r#"{"scope": "2", "activeSpace": 2, "spaces": [{"index": 2, "name": "X"}]}"#,
    )
    .expect("a per-space scope parses");
    assert_eq!(payload.scope, restore::Scope::Space(2));
    assert_eq!(payload.spaces[0].tabs.len(), 0);
}

#[test]
fn json_that_is_not_a_payload_is_refused_rather_than_guessed() {
    assert!(restore::parse("not json").is_none());
    assert!(restore::parse(r#"{"scope": "sideways", "activeSpace": 0, "spaces": []}"#).is_none());
    assert!(restore::parse(r#"{"activeSpace": 0, "spaces": []}"#).is_none());
}

// the reopen allowlist was bypassable by persisting instead of
// reopening. Every reopen path scheme-checks - OpenSidebarColdTab allows http,
// https and about - but validate only rejected an EMPTY url, and the startup
// consumer applied nothing but is_valid(). So a compromised chrome://atrium
// renderer could send a restore payload whose row URL is file:/// or
// javascript:, and the next launch navigated to it with PAGE_TRANSITION_TYPED.
#[test]
fn a_row_with_a_disallowed_scheme_is_refused() {
    let doc = document();
    for url in [
        "file:///etc/passwd",
        "javascript:alert(1)",
        "chrome://settings",
        "data:text/html,<script>alert(1)</script>",
    ] {
        let mut payload = payload_from(&doc, &[0]);
        payload.spaces[0].tabs = vec![restore::Row {
            url: url.into(),
            ..Default::default()
        }];
        let mut target = document();
        let mut bindings = Bindings::default();
        assert_eq!(
            restore::apply(&mut target, &mut bindings, &payload).err(),
            Some(Refusal::DisallowedScheme),
            "{url} was accepted into the persisted document"
        );
        // And nothing was written on the way to refusing.
        assert_eq!(
            target.spaces[0].tabs.len(),
            document().spaces[0].tabs.len(),
            "{url} was refused but the document changed anyway"
        );
    }
}

#[test]
fn the_allowed_schemes_still_restore() {
    // The other direction, because a validate that refused everything would pass
    // the test above and break every ordinary restore.
    let doc = document();
    for url in [
        "http://example.com/a",
        "https://example.com/b",
        "about:blank",
    ] {
        let mut payload = payload_from(&doc, &[0]);
        payload.spaces[0].tabs = vec![restore::Row {
            url: url.into(),
            ..Default::default()
        }];
        let mut target = document();
        let mut bindings = Bindings::default();
        assert!(
            restore::apply(&mut target, &mut bindings, &payload).is_ok(),
            "{url} was refused and should not have been"
        );
        assert_eq!(target.spaces[0].tabs[0].url, url);
    }
}

// A favourite is persisted by the same payload and reopened by the same rules.
#[test]
fn a_favourite_with_a_disallowed_scheme_is_refused_too() {
    let doc = document();
    let mut payload = payload_from(&doc, &[0]);
    payload.spaces[0].favorites = vec![restore::Row {
        url: "javascript:alert(1)".into(),
        title: "trap".into(),
        ..Default::default()
    }];
    let mut target = document();
    let mut bindings = Bindings::default();
    assert_eq!(
        restore::apply(&mut target, &mut bindings, &payload).err(),
        Some(Refusal::DisallowedScheme)
    );
}

// the restore path wrote slice.favorites straight through, so one payload
// with a large array bypassed MAX_FAVORITES_PER_PROFILE - and a renderer is what
// sends the payload.
#[test]
fn a_restore_payload_cannot_exceed_the_favourite_cap() {
    let doc = document();
    let mut payload = payload_from(&doc, &[0]);
    payload.spaces[0].favorites = (0..500)
        .map(|n| restore::Row {
            url: format!("https://fav{n}.example/"),
            title: format!("Fav {n}"),
            ..Default::default()
        })
        .collect();

    let mut target = document();
    let mut bindings = Bindings::default();
    assert!(restore::apply(&mut target, &mut bindings, &payload).is_ok());
    assert_eq!(
        target.favorites.len(),
        atrium_core::mutate::MAX_FAVORITES_PER_PROFILE,
        "a restore payload wrote past the favourite cap"
    );
}
