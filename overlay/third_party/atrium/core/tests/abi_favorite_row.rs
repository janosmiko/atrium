// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! A pinned row dragged into the favourites grid, then back out to the tabs.

use atrium_core::{
    atrium_core_close, atrium_core_favorite_row, atrium_core_open, atrium_core_open_tab,
    atrium_core_pin_tab_by_tab_id, atrium_core_remove_favorite, atrium_core_save, spaces,
};

mod common;
use common::{TempDir, c};

const INSTANT: f64 = 788_918_400.0;
const URL: &str = "https://fav.test/";
const DOCUMENT: &str = r##"{"spaces":[
{"id":"A","name":"Work","emoji":"W","tint":"#111111","profileID":"Default",
 "pinnedTabs":[],"tabs":[],"order":0,"groups":[]}],
"appState":{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":3,
"archiveEpoch":1,"sidebarWidthMigrated":true}}"##;

fn open(tab_id: u64) -> bool {
    let (url, title) = (c(URL), c("Fav"));
    unsafe { atrium_core_open_tab(-1, tab_id, url.as_ptr(), title.as_ptr(), -1) }
}

#[test]
fn a_pinned_row_moved_through_the_grid_gets_its_tab_row_back() {
    let _serial = common::abi_serial();
    let dir = TempDir::new("abi-favorite-row");
    let path = dir.join("spaces.json");
    std::fs::write(&path, DOCUMENT).unwrap();
    atrium_core_close();
    let c_path = c(path.to_str().unwrap());
    assert!(unsafe { atrium_core_open(c_path.as_ptr(), INSTANT) });

    assert!(open(10));
    assert!(atrium_core_pin_tab_by_tab_id(10, -1));
    assert!(
        atrium_core_favorite_row(-1, 0, 1, -1),
        "the pinned row moves"
    );
    let url = c(URL);
    assert!(unsafe { atrium_core_remove_favorite(-1, url.as_ptr()) });

    // The move-out opens a row for the tab that the tile showed.
    assert!(
        open(10),
        "the pinned row is gone, so its tab must not still hold it"
    );
    atrium_core_save();
    let doc = spaces::load(&std::fs::read_to_string(&path).unwrap(), INSTANT).unwrap();
    assert_eq!(doc.spaces[0].tabs.len(), 1, "the page has a row again");

    atrium_core_close();
}
