// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Separate binary, for the reason `abi_writes.rs` gives: these
//! functions address the process-wide core `atrium_core_open` fills.

use atrium_core::{
    atrium_core_add_doorway, atrium_core_close, atrium_core_open, atrium_core_save, spaces,
};

mod common;
use common::{TempDir, abi_serial, c};

const INSTANT: f64 = 788_918_400.0;

/// -1 for the window's slot: C++ does not track which space a window shows, so
/// the document's own activeSpaceID answers.
const OWN: i32 = -1;

const DOCUMENT: &str = r##"{"spaces":[
{"id":"A","name":"Work","emoji":"W","tint":"#111111","profileID":"Work",
 "pinnedTabs":[{"url":"https://mail.example/","title":"Mail"}],
 "tabs":[],"order":0,"groups":[]},
{"id":"B","name":"Home","emoji":"H","tint":"#222222","profileID":"Home",
 "pinnedTabs":[{"url":"https://mail.example/","title":"Mail"}],
 "tabs":[],"order":1,"groups":[]}],
"appState":{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":3,
"archiveEpoch":1,"sidebarWidthMigrated":true}}"##;

#[test]
fn add_doorway_records_the_site_leaves_the_tab_and_refuses_a_duplicate() {
    let _guard = abi_serial();
    let dir = TempDir::new("abi-doorway");
    let path = dir.join("spaces.json");
    std::fs::write(&path, DOCUMENT).unwrap();

    let c_path = c(path.to_str().unwrap());
    assert!(unsafe { atrium_core_open(c_path.as_ptr(), INSTANT) });

    // creates a record naming the site, the owning space and its profile.
    assert!(atrium_core_add_doorway(OWN, 0));
    // asking again for the same pinned tab is a no-op, not a duplicate.
    assert!(!atrium_core_add_doorway(OWN, 0));
    // A pinned index that resolves nothing is refused the same way.
    assert!(!atrium_core_add_doorway(OWN, 9));

    assert_eq!(atrium_core_save(), 1);

    let doc = spaces::load(&std::fs::read_to_string(&path).unwrap(), INSTANT).unwrap();
    assert_eq!(doc.doorways.len(), 1);
    let doorway = &doc.doorways[0];
    assert_eq!(doorway.url, "https://mail.example/");
    assert_eq!(doorway.space_id, "A");
    assert_eq!(doorway.profile_id, "Work");

    // the pinned tab it was made from is untouched.
    assert_eq!(doc.spaces[0].pinned_tabs.len(), 1);
    assert_eq!(doc.spaces[0].pinned_tabs[0].url, "https://mail.example/");
    assert_eq!(doc.spaces[0].pinned_tabs[0].title, "Mail");

    atrium_core_close();
}

#[test]
fn the_same_site_pinned_in_two_spaces_gets_a_doorway_each() {
    let _guard = abi_serial();
    let dir = TempDir::new("abi-doorway-two");
    let path = dir.join("spaces.json");
    std::fs::write(&path, DOCUMENT).unwrap();

    let c_path = c(path.to_str().unwrap());
    assert!(unsafe { atrium_core_open(c_path.as_ptr(), INSTANT) });

    assert!(atrium_core_add_doorway(OWN, 0));
    assert!(atrium_core::atrium_core_set_active_space_index(1));
    assert!(atrium_core_add_doorway(OWN, 0));

    assert_eq!(atrium_core_save(), 1);

    let doc = spaces::load(&std::fs::read_to_string(&path).unwrap(), INSTANT).unwrap();
    assert_eq!(
        doc.doorways.len(),
        2,
        "bound to its space, not deduped across spaces"
    );

    atrium_core_close();
}
