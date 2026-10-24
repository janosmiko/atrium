// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! deleting a pinned row, and the write behind it.
//!
//! Its own file rather than a case in `abi_writes`: every integration file is
//! its own process, and these calls go through one shared open document - two
//! tests opening different paths in one binary race each other.

use atrium_core::mutate::test_support::urls;
use atrium_core::{
    atrium_core_close, atrium_core_open, atrium_core_remove_pinned_tab, atrium_core_save, spaces,
};

mod common;
use common::{TempDir, c};

const INSTANT: f64 = 788_918_400.0;

const DOCUMENT: &str = r##"{"spaces":[
{"id":"A","name":"Work","emoji":"W","tint":"#111111","profileID":"p",
 "pinnedTabs":[{"url":"https://pin.test/","title":"Pin"}],
 "tabs":[{"url":"https://one.test/","title":"One"},
         {"url":"https://two.test/","title":"Two"},
         {"url":"https://three.test/","title":"Three"}],
 "order":0,"groups":[]}],
"appState":{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":3,
"archiveEpoch":1,"sidebarWidthMigrated":true}}"##;

const OWN: i32 = -1;

/// Its own document rather than a step in the sequence
/// above: deleting a pin consumes a row, and that sequence asserts an exact end
/// state which a consumed row would change.
#[test]
fn a_deleted_pin_does_not_come_back_on_the_next_read() {
    let dir = TempDir::new("abi-unpin");
    let path = dir.join("spaces.json");
    std::fs::write(&path, DOCUMENT).unwrap();

    let path_c = c(path.to_str().unwrap());
    assert!(unsafe { atrium_core_open(path_c.as_ptr(), INSTANT) });

    // The fixture opens with one pinned row. Take the tile away rather than
    // demoting it, which is what atrium_core_unpin_tab would have done.
    assert!(atrium_core_remove_pinned_tab(OWN, 0));
    assert!(
        !atrium_core_remove_pinned_tab(OWN, 0),
        "removing what is already gone does not dirty the file"
    );
    assert_eq!(atrium_core_save(), 1);

    let doc = spaces::load(&std::fs::read_to_string(&path).unwrap(), INSTANT).unwrap();
    assert!(
        doc.spaces[0].pinned_tabs.is_empty(),
        "the pin survived the write"
    );
    assert_eq!(
        urls(&doc.spaces[0]),
        vec![
            "https://one.test/",
            "https://two.test/",
            "https://three.test/"
        ],
        "and it was not demoted into the regular list"
    );

    atrium_core_close();
}
