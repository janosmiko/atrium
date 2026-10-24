// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! the write exports as C++ calls them.
//!
//! These functions address the process-wide core `atrium_core_open` fills, so
//! every test here takes `abi_serial()` - two running at once would be two
//! windows fighting over one document.

use atrium_core::mutate::test_support::urls;
use atrium_core::{
    atrium_core_add_doorway, atrium_core_add_profile, atrium_core_assign_tab_to_group,
    atrium_core_close, atrium_core_delete_profile, atrium_core_favorite_row,
    atrium_core_move_tab_run, atrium_core_move_tab_to_space, atrium_core_open,
    atrium_core_pin_folder, atrium_core_pin_tab, atrium_core_profile_search_engine,
    atrium_core_remove_favorite, atrium_core_remove_pinned_tab, atrium_core_rename_profile,
    atrium_core_reorder_favorite, atrium_core_reorder_group_member,
    atrium_core_reorder_pinned_item, atrium_core_reorder_tab, atrium_core_save,
    atrium_core_set_active_space_index, atrium_core_set_group_collapsed,
    atrium_core_set_pinned_collapsed, atrium_core_set_profile_hib_threshold,
    atrium_core_set_profile_hibernate, atrium_core_set_profile_search_engine,
    atrium_core_set_space_canvas_rect, atrium_core_set_space_profile, atrium_core_set_tab_canvas_x,
    atrium_core_ungroup_member, atrium_core_unpin_tab, spaces,
};

mod common;
use common::{TempDir, abi_serial, c};

const INSTANT: f64 = 788_918_400.0;

const DOCUMENT: &str = r##"{"spaces":[
{"id":"A","name":"Work","emoji":"W","tint":"#111111","profileID":"p",
 "pinnedTabs":[{"url":"https://pin.test/","title":"Pin"},
                {"url":"https://pin2.test/","title":"Pin Two"}],
 "tabs":[{"url":"https://one.test/","title":"One"},
         {"url":"https://two.test/","title":"Two","groupID":"G"},
         {"url":"https://three.test/","title":"Three"}],
 "order":0,"groups":[{"id":"G","name":"Reading","collapsed":false}]},
{"id":"B","name":"Home","emoji":"H","tint":"#222222","profileID":"p",
 "pinnedTabs":[],"tabs":[],"order":1,"groups":[]}],
"appState":{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":3,
"archiveEpoch":1,"sidebarWidthMigrated":true}}"##;

/// -1 for the window's slot everywhere below: C++ does not track which space a
/// window shows, so the document's own activeSpaceID answers - the same
/// convention the read calls already use.
const OWN: i32 = -1;

#[test]
fn every_sidebar_gesture_crosses_the_abi_and_lands_on_disk() {
    let _guard = abi_serial();
    let dir = TempDir::new("abi-writes");
    let path = dir.join("spaces.json");
    std::fs::write(&path, DOCUMENT).unwrap();

    // A gesture with nothing open must not pretend to have worked.
    atrium_core_close();
    assert!(!atrium_core_set_pinned_collapsed(OWN, true));
    assert_eq!(atrium_core_save(), 0, "nothing open, nothing to write");

    let c_path = c(path.to_str().unwrap());
    assert!(unsafe { atrium_core_open(c_path.as_ptr(), INSTANT) });

    // The two folds. Explicit state, so the same value twice is not a change.
    assert!(atrium_core_set_pinned_collapsed(OWN, true));
    assert!(!atrium_core_set_pinned_collapsed(OWN, true));
    let group = c("G");
    assert!(unsafe { atrium_core_set_group_collapsed(OWN, group.as_ptr(), true) });
    let unknown = c("not-a-group");
    assert!(
        !unsafe { atrium_core_set_group_collapsed(OWN, unknown.as_ptr(), true) },
        "an id from the DOM that names nothing does nothing"
    );

    // Drag the first visible row past the second. The grouped tab between them
    // is why the index is translated.
    assert!(atrium_core_reorder_tab(OWN, 0, 1));
    assert!(
        !atrium_core_reorder_tab(OWN, 9, 0),
        "a position the model no longer has is ignored, not clamped"
    );

    // Favourite one row from each section, then reorder the grid.
    assert!(atrium_core_favorite_row(OWN, 0, 0, -1));
    assert!(atrium_core_favorite_row(OWN, 0, 1, -1));
    assert!(atrium_core_reorder_favorite(OWN, 0, 1));

    // Removing one leaves the other, and the write below proves it survives a
    // round trip rather than only leaving the in-memory list.
    let gone = c("https://three.test/");
    assert!(unsafe { atrium_core_remove_favorite(OWN, gone.as_ptr()) });
    assert!(
        !unsafe { atrium_core_remove_favorite(OWN, gone.as_ptr()) },
        "removing what is already gone does not dirty the file"
    );

    // Across the section boundary and back. Two pinned rows in the fixture
    // because favouriting one above unpins it: the drag is a move, so the
    // pinned source goes (atrium_core_favorite_row).
    assert!(atrium_core_pin_tab(OWN, 1, 0));
    assert!(atrium_core_reorder_pinned_item(OWN, 0, 1));
    assert!(atrium_core_unpin_tab(OWN, 1, 0));

    // Into a folder, then across to the other space.
    assert!(unsafe { atrium_core_assign_tab_to_group(OWN, 0, 1, group.as_ptr(), -1) });
    assert!(atrium_core_move_tab_to_space(0, 0, 0, 1, -1));

    // Last, because every call above resolves against the active space.
    assert!(atrium_core_set_active_space_index(1));
    assert!(
        !atrium_core_set_active_space_index(9),
        "a stale index describes a model that no longer exists"
    );

    assert_eq!(atrium_core_save(), 1, "the gestures are written");
    assert_eq!(atrium_core_save(), 0, "and a second save has nothing to do");

    // What the next launch reads.
    let doc = spaces::load(&std::fs::read_to_string(&path).unwrap(), INSTANT).unwrap();
    let work = &doc.spaces[0];
    let home = &doc.spaces[1];

    assert!(work.pinned_collapsed);
    assert!(work.groups[0].collapsed);
    assert_eq!(
        urls(work),
        vec!["https://two.test/", "https://three.test/"],
        "one tab left for the other space and the rest kept their order"
    );
    assert!(
        work.tabs.iter().all(|t| t.group_id.as_deref() == Some("G")),
        "the assign landed on the translated row"
    );
    assert_eq!(
        work.pinned_tabs.len(),
        1,
        "pinned, reordered and unpinned again is where it started"
    );
    assert_eq!(urls(home), vec!["https://one.test/"]);
    assert_eq!(doc.app_state.active_space_id.as_deref(), Some("B"));

    let favorites: Vec<&str> = doc.favorites.iter().map(|f| f.url.as_str()).collect();
    assert_eq!(
        favorites,
        vec!["https://pin.test/"],
        "the grid reorder moved the profile's own list, and the removal took the other"
    );

    atrium_core_close();
}

/// The two canvas setters, on the same walk shape as the gesture test above:
/// the process-wide core is one document, so this runs as its own single walk.
#[test]
fn the_canvas_setters_cross_the_abi_and_land_on_disk() {
    let _guard = abi_serial();
    let dir = TempDir::new("abi-canvas");
    let path = dir.join("spaces.json");
    std::fs::write(&path, DOCUMENT).unwrap();
    let c_path = c(path.to_str().unwrap());
    // SAFETY: a NUL-terminated path that outlives the call, the header's
    // contract. Match the unsafe block the existing walk in this file uses.
    assert!(unsafe { atrium_core_open(c_path.as_ptr(), INSTANT) });

    assert!(atrium_core_set_space_canvas_rect(
        1, 480.0, -120.0, 300.0, 220.0
    ));
    // Visible row 1, because the grouped tab at persisted 1 is not drawn.
    assert!(atrium_core_set_tab_canvas_x(0, 0, 1, 24.5));
    assert!(atrium_core_set_tab_canvas_x(0, 1, 1, -8.0));

    assert!(
        !atrium_core_set_space_canvas_rect(0, f64::NAN, 0.0, 300.0, 220.0),
        "NaN is refused"
    );
    assert!(
        !atrium_core_set_space_canvas_rect(0, 0.0, 0.0, 0.0, 220.0),
        "a rect with no width is refused"
    );
    assert!(
        !atrium_core_set_space_canvas_rect(0, 1.0e9, 0.0, 300.0, 220.0),
        "a value outside the plane is refused"
    );
    assert!(
        !atrium_core_set_tab_canvas_x(0, 0, 2, f64::INFINITY),
        "an infinite offset is refused"
    );
    assert!(
        !atrium_core_set_tab_canvas_x(0, 2, 0, 4.0),
        "there is no section 2 - favourites are per profile and carry no offset"
    );
    assert!(
        !atrium_core_set_tab_canvas_x(0, 0, 99, 4.0),
        "an index past the end is refused rather than clamped"
    );
    assert!(
        !atrium_core_set_tab_canvas_x(0, 0, 2, 4.0),
        "the grouped tab leaves two drawn rows, so there is no visible row 2"
    );

    // 1 means a file was written, 0 means nothing had moved, -1 means the
    // write failed. A write export only bumps the in-memory version, so
    // without this save there is nothing on disk to read back.
    assert_eq!(atrium_core_save(), 1);
    let saved = std::fs::read_to_string(&path).unwrap();
    let doc = spaces::load(&saved, INSTANT).unwrap();

    assert_eq!(
        doc.spaces[1].canvas,
        Some(spaces::CanvasRect {
            x: 480.0,
            y: -120.0,
            w: 300.0,
            h: 220.0
        })
    );
    assert_eq!(doc.spaces[0].tabs[2].canvas_x, Some(24.5));
    assert_eq!(doc.spaces[0].pinned_tabs[1].canvas_x, Some(-8.0));
    assert_eq!(
        doc.spaces[0].canvas, None,
        "the refused rects left space 0 unplaced"
    );

    atrium_core_close();
}

/// A grouped tab sits between the first drawn row and the target, so a raw
/// index would write the offset two rows early. The favourited url is a drawn
/// row: no token hides it.
const CROWDED: &str = r##"{"spaces":[
{"id":"A","name":"Work","emoji":"W","tint":"#111111","profileID":"p",
 "pinnedTabs":[],
 "tabs":[{"url":"https://first.test/","title":"First"},
         {"url":"https://grouped.test/","title":"Grouped","groupID":"G"},
         {"url":"https://fav.test/","title":"Fav"},
         {"url":"https://target.test/","title":"Target"}],
 "order":0,"groups":[{"id":"G","name":"Reading","collapsed":false}]}],
"favorites":[{"url":"https://fav.test/","title":"Fav","profileID":"p"}],
"appState":{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":3,
"archiveEpoch":1,"sidebarWidthMigrated":true}}"##;

#[test]
fn the_card_offset_counts_the_rows_the_courtyard_draws() {
    let _guard = abi_serial();
    let dir = TempDir::new("abi-canvas-visible");
    let path = dir.join("spaces.json");
    std::fs::write(&path, CROWDED).unwrap();
    let c_path = c(path.to_str().unwrap());
    // SAFETY: a NUL-terminated path that outlives the call, the header's
    // contract.
    assert!(unsafe { atrium_core_open(c_path.as_ptr(), INSTANT) });

    assert!(atrium_core_set_tab_canvas_x(0, 0, 2, 42.0));
    assert!(
        !atrium_core_set_tab_canvas_x(0, 0, 3, 42.0),
        "one of the four tabs is not drawn, so visible row 3 is past the end"
    );

    assert_eq!(atrium_core_save(), 1);
    let doc = spaces::load(&std::fs::read_to_string(&path).unwrap(), INSTANT).unwrap();
    let tabs = &doc.spaces[0].tabs;
    assert_eq!(
        tabs[3].canvas_x,
        Some(42.0),
        "visible row 2 is the fourth persisted tab"
    );
    assert_eq!(tabs[0].canvas_x, None);
    assert_eq!(tabs[1].canvas_x, None);
    assert_eq!(tabs[2].canvas_x, None);

    atrium_core_close();
}

/// A pinned folder ("G") sits between two plain pins, so item 1 is the folder
/// and item 2 is the trailing pin at raw index 3 - the divergence these tests
/// exist to catch.
const PINNED_FOLDER_DOCUMENT: &str = r##"{"spaces":[
{"id":"A","name":"Work","emoji":"W","tint":"#111111","profileID":"p",
 "pinnedTabs":[{"url":"https://p0.test/","title":"P0"},
                {"url":"https://g0.test/","title":"G0","groupID":"G"},
                {"url":"https://g1.test/","title":"G1","groupID":"G"},
                {"url":"https://p1.test/","title":"P1"}],
 "tabs":[],
 "order":0,"groups":[{"id":"G","name":"Reading","collapsed":false}]}],
"appState":{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":3,
"archiveEpoch":1,"sidebarWidthMigrated":true}}"##;

#[test]
fn remove_pinned_tab_takes_an_item_index_and_refuses_a_folder() {
    let _guard = abi_serial();
    let dir = TempDir::new("abi-remove-pinned-item");
    let path = dir.join("spaces.json");
    std::fs::write(&path, PINNED_FOLDER_DOCUMENT).unwrap();
    let c_path = c(path.to_str().unwrap());
    assert!(unsafe { atrium_core_open(c_path.as_ptr(), INSTANT) });

    assert!(
        !atrium_core_remove_pinned_tab(OWN, 1),
        "item 1 is the folder"
    );
    assert!(
        atrium_core_remove_pinned_tab(OWN, 2),
        "item 2 is the trailing pin"
    );

    assert_eq!(atrium_core_save(), 1);
    let doc = spaces::load(&std::fs::read_to_string(&path).unwrap(), INSTANT).unwrap();
    let pinned: Vec<&str> = doc.spaces[0]
        .pinned_tabs
        .iter()
        .map(|t| t.url.as_str())
        .collect();
    assert_eq!(
        pinned,
        vec!["https://p0.test/", "https://g0.test/", "https://g1.test/"],
        "the trailing pin is gone and the folder is untouched"
    );

    atrium_core_close();
}

#[test]
fn add_doorway_takes_an_item_index_and_refuses_a_folder() {
    let _guard = abi_serial();
    let dir = TempDir::new("abi-doorway-pinned-item");
    let path = dir.join("spaces.json");
    std::fs::write(&path, PINNED_FOLDER_DOCUMENT).unwrap();
    let c_path = c(path.to_str().unwrap());
    assert!(unsafe { atrium_core_open(c_path.as_ptr(), INSTANT) });

    assert!(!atrium_core_add_doorway(OWN, 1), "item 1 is the folder");
    assert!(
        atrium_core_add_doorway(OWN, 2),
        "item 2 is the trailing pin"
    );

    assert_eq!(atrium_core_save(), 1);
    let doc = spaces::load(&std::fs::read_to_string(&path).unwrap(), INSTANT).unwrap();
    assert_eq!(doc.doorways.len(), 1);
    assert_eq!(doc.doorways[0].url, "https://p1.test/");

    atrium_core_close();
}

#[test]
fn favorite_row_pinned_section_takes_an_item_index_and_refuses_a_folder() {
    let _guard = abi_serial();
    let dir = TempDir::new("abi-favorite-pinned-item");
    let path = dir.join("spaces.json");
    std::fs::write(&path, PINNED_FOLDER_DOCUMENT).unwrap();
    let c_path = c(path.to_str().unwrap());
    assert!(unsafe { atrium_core_open(c_path.as_ptr(), INSTANT) });

    assert!(
        !atrium_core_favorite_row(OWN, 1, 1, -1),
        "item 1 is the folder"
    );
    assert!(
        atrium_core_favorite_row(OWN, 2, 1, -1),
        "item 2 is the trailing pin"
    );

    assert_eq!(atrium_core_save(), 1);
    let doc = spaces::load(&std::fs::read_to_string(&path).unwrap(), INSTANT).unwrap();
    assert_eq!(doc.favorites.len(), 1);
    assert_eq!(doc.favorites[0].url, "https://p1.test/");
    assert_eq!(
        doc.spaces[0].pinned_tabs.len(),
        3,
        "the favourited pin left the pinned list"
    );

    atrium_core_close();
}

#[test]
fn set_tab_canvas_x_pinned_section_takes_an_item_index_and_refuses_a_folder() {
    let _guard = abi_serial();
    let dir = TempDir::new("abi-canvas-pinned-item");
    let path = dir.join("spaces.json");
    std::fs::write(&path, PINNED_FOLDER_DOCUMENT).unwrap();
    let c_path = c(path.to_str().unwrap());
    assert!(unsafe { atrium_core_open(c_path.as_ptr(), INSTANT) });

    assert!(
        !atrium_core_set_tab_canvas_x(0, 1, 1, 5.0),
        "item 1 is the folder"
    );
    assert!(
        atrium_core_set_tab_canvas_x(0, 1, 2, 5.0),
        "item 2 is the trailing pin"
    );

    assert_eq!(atrium_core_save(), 1);
    let doc = spaces::load(&std::fs::read_to_string(&path).unwrap(), INSTANT).unwrap();
    assert_eq!(doc.spaces[0].pinned_tabs[3].canvas_x, Some(5.0));

    atrium_core_close();
}

/// A document already at schema 6, with its own profile record - the profile
/// verbs need one to exist rather than backfilling it from a legacy field.
const PROFILE_DOCUMENT: &str = r##"{"spaces":[
{"id":"A","name":"Work","emoji":"W","tint":"t","profileID":"Default",
 "pinnedTabs":[],"tabs":[],"order":0,"groups":[]}],
"appState":{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":6},
"profiles":[{"id":"Default","name":"Work"}]}"##;

/// The profile verbs across the ABI, end to end: add, rename, point a space at
/// it, set its engine, and refuse to delete it while that space is there.
#[test]
fn the_profile_verbs_round_trip_across_the_abi() {
    let _guard = abi_serial();
    let dir = TempDir::new("abi-profiles");
    let path = dir.join("spaces.json");
    std::fs::write(&path, PROFILE_DOCUMENT).unwrap();
    let c_path = c(path.to_str().unwrap());
    assert!(unsafe { atrium_core_open(c_path.as_ptr(), INSTANT) });

    let raw = atrium_core_add_profile();
    let added = unsafe { common::take_string(raw) }.expect("a fresh profile id");

    let id = c(&added);
    let name = c("Personal");
    assert!(unsafe { atrium_core_rename_profile(id.as_ptr(), name.as_ptr()) });

    let engine = c("kagi");
    assert!(unsafe { atrium_core_set_profile_search_engine(id.as_ptr(), engine.as_ptr()) });
    let read_back = unsafe { common::take_string(atrium_core_profile_search_engine(id.as_ptr())) };
    assert_eq!(read_back, Some("kagi".to_string()));
    // Null clears it, the omittable_str contract.
    assert!(unsafe { atrium_core_set_profile_search_engine(id.as_ptr(), std::ptr::null()) });
    assert_eq!(
        unsafe { common::take_string(atrium_core_profile_search_engine(id.as_ptr())) },
        None,
        "cleared, so the getter must not still answer the old value"
    );

    // Each setter on its own value, not just its null-id refusal: wiring two
    // of these to the same mutate verb passes every other assertion here.
    let never = c("never");
    assert!(unsafe { atrium_core_set_profile_hibernate(id.as_ptr(), never.as_ptr()) });
    let threshold = c("twoHours");
    assert!(unsafe { atrium_core_set_profile_hib_threshold(id.as_ptr(), threshold.as_ptr()) });
    assert_eq!(atrium_core_save(), 1);
    let doc = spaces::load(&std::fs::read_to_string(&path).unwrap(), INSTANT).unwrap();
    let profile = doc
        .profiles
        .iter()
        .find(|p| p.id == added)
        .expect("the profile just added is in the document");
    assert_eq!(profile.hibernate.as_deref(), Some("never"));
    assert_eq!(profile.hib_threshold.as_deref(), Some("twoHours"));

    assert!(unsafe { atrium_core_set_space_profile(0, id.as_ptr()) });
    assert!(
        !unsafe { atrium_core_delete_profile(id.as_ptr()) },
        "a profile a space uses cannot be deleted"
    );

    let default = c("Default");
    assert!(unsafe { atrium_core_set_space_profile(0, default.as_ptr()) });
    assert!(unsafe { atrium_core_delete_profile(id.as_ptr()) });

    atrium_core_close();
}

/// A NULL id is refused rather than read as the empty string: an id names a
/// directory, and the empty one is not a profile.
#[test]
fn a_null_profile_id_is_refused_by_every_profile_verb() {
    let _guard = abi_serial();
    let name = c("Personal");
    assert!(!unsafe { atrium_core_rename_profile(std::ptr::null(), name.as_ptr()) });
    assert!(!unsafe { atrium_core_delete_profile(std::ptr::null()) });
    assert!(!unsafe { atrium_core_set_space_profile(0, std::ptr::null()) });
    assert!(!unsafe { atrium_core_set_profile_hibernate(std::ptr::null(), std::ptr::null()) });
}

/// The folder verbs across the ABI, on their own walk because the core is one
/// process-wide document.
#[test]
fn the_pinned_folder_verbs_cross_the_abi_and_land_on_disk() {
    let _guard = abi_serial();
    let dir = TempDir::new("abi-pinned-folders");
    let path = dir.join("spaces.json");
    std::fs::write(&path, DOCUMENT).unwrap();
    let c_path = c(path.to_str().unwrap());
    // SAFETY: a NUL-terminated path that outlives the call, the header's
    // contract.
    assert!(unsafe { atrium_core_open(c_path.as_ptr(), INSTANT) });

    let group = c("G");
    // The folder's one member is a regular row, so pinning the folder is what
    // moves it - slot 1 puts it between the two pins.
    assert!(unsafe { atrium_core_pin_folder(OWN, group.as_ptr(), 1) });
    assert!(
        !unsafe { atrium_core_pin_folder(OWN, group.as_ptr(), 1) },
        "a folder already pinned is reordered instead"
    );

    // Item 1 is the folder now, so item 2 is the second pin.
    assert!(atrium_core_reorder_pinned_item(OWN, 1, 2));
    // And the surface can drop a pin into the folder by its item index.
    assert!(unsafe { atrium_core_assign_tab_to_group(OWN, 1, 0, group.as_ptr(), -1) });

    assert_eq!(atrium_core_save(), 1);
    let doc = spaces::load(&std::fs::read_to_string(&path).unwrap(), INSTANT).unwrap();
    let work = &doc.spaces[0];
    let pinned: Vec<(&str, Option<&str>)> = work
        .pinned_tabs
        .iter()
        .map(|t| (t.url.as_str(), t.group_id.as_deref()))
        .collect();
    assert_eq!(
        pinned,
        vec![
            ("https://pin2.test/", None),
            ("https://two.test/", Some("G")),
            ("https://pin.test/", Some("G")),
        ],
        "the folder moved past the second pin, and the first pin joined it"
    );
    assert_eq!(
        urls(work),
        vec!["https://one.test/", "https://three.test/"],
        "the member left the regular list when the folder was pinned"
    );

    // A member leaves the folder back into the regular list at a visible slot.
    assert!(unsafe { atrium_core_ungroup_member(OWN, group.as_ptr(), 1, 0, 1) });
    assert_eq!(atrium_core_save(), 1);
    let doc = spaces::load(&std::fs::read_to_string(&path).unwrap(), INSTANT).unwrap();
    assert_eq!(
        urls(&doc.spaces[0]),
        vec![
            "https://one.test/",
            "https://pin.test/",
            "https://three.test/"
        ]
    );

    atrium_core_close();
}

#[test]
fn a_folder_member_reorders_over_the_abi() {
    let _guard = abi_serial();
    let dir = TempDir::new("abi-reorder-member");
    let path = dir.join("spaces.json");
    // One joins G, so the folder has two rows to swap.
    let document = DOCUMENT.replace(
        r#"{"url":"https://one.test/","title":"One"}"#,
        r#"{"url":"https://one.test/","title":"One","groupID":"G"}"#,
    );
    std::fs::write(&path, document).unwrap();
    let c_path = c(path.to_str().unwrap());
    assert!(unsafe { atrium_core_open(c_path.as_ptr(), INSTANT) });

    let group = c("G");
    assert!(unsafe { atrium_core_reorder_group_member(OWN, group.as_ptr(), 0, 1) });
    assert!(
        !unsafe { atrium_core_reorder_group_member(OWN, group.as_ptr(), 1, 1) },
        "a drop on its own slot is not a move"
    );
    assert_eq!(atrium_core_save(), 1);
    let doc = spaces::load(&std::fs::read_to_string(&path).unwrap(), INSTANT).unwrap();
    assert_eq!(
        urls(&doc.spaces[0]),
        vec![
            "https://two.test/",
            "https://one.test/",
            "https://three.test/"
        ]
    );

    atrium_core_close();
}

/// The slots count the rows the sidebar draws, so the grouped Two in between
/// is skipped and stays behind.
#[test]
fn a_split_run_moves_across_the_abi() {
    let _guard = abi_serial();
    let dir = TempDir::new("abi-move-run");
    let path = dir.join("spaces.json");
    std::fs::write(&path, DOCUMENT).unwrap();
    let c_path = c(path.to_str().unwrap());
    assert!(unsafe { atrium_core_open(c_path.as_ptr(), INSTANT) });

    for bad in ["[0,9]", "[0,0]", "[]", "not json", "[-1]"] {
        let slots = c(bad);
        assert!(
            !unsafe { atrium_core_move_tab_run(0, slots.as_ptr(), 1, -1) },
            "{bad} is refused"
        );
    }
    assert!(!unsafe { atrium_core_move_tab_run(0, std::ptr::null(), 1, -1) });
    let slots = c("[0,1]");
    assert!(
        !unsafe { atrium_core_move_tab_run(0, slots.as_ptr(), 1, 5) },
        "a slot past the target's rows is refused"
    );
    assert_eq!(atrium_core_save(), 0, "every refusal left the file alone");

    assert!(unsafe { atrium_core_move_tab_run(0, slots.as_ptr(), 1, -1) });
    assert_eq!(atrium_core_save(), 1);
    let doc = spaces::load(&std::fs::read_to_string(&path).unwrap(), INSTANT).unwrap();
    assert_eq!(urls(&doc.spaces[0]), vec!["https://two.test/"]);
    assert_eq!(
        urls(&doc.spaces[1]),
        vec!["https://one.test/", "https://three.test/"]
    );

    atrium_core_close();
}
