// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! An ordinary tab that opens a favourite's URL is drawn by the sidebar, so
//! every visible-index translation must count it.

use atrium_core::mutate::test_support::urls;
use atrium_core::{
    atrium_core_archive_tab, atrium_core_close, atrium_core_open, atrium_core_reorder_tab,
    atrium_core_save, spaces,
};

mod common;
use common::{TempDir, abi_serial, c};

const INSTANT: f64 = 788_918_400.0;

const FAV_URL_TAB_FIRST: &str = r##"{"spaces":[
{"id":"A","name":"Work","emoji":"W","tint":"#111111","profileID":"p",
 "pinnedTabs":[],
 "tabs":[{"url":"https://fav.test/","title":"Fav"},
         {"url":"https://target.test/","title":"Target"},
         {"url":"https://last.test/","title":"Last"}],
 "order":0,"groups":[]}],
"favorites":[{"url":"https://fav.test/","title":"Fav","profileID":"p"}],
"appState":{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":3,
"archiveEpoch":1,"sidebarWidthMigrated":true}}"##;

fn after(act: impl FnOnce() -> bool) -> Vec<String> {
    let _guard = abi_serial();
    let dir = TempDir::new("abi-drawn-row-favourite");
    let path = dir.join("spaces.json");
    std::fs::write(&path, FAV_URL_TAB_FIRST).unwrap();
    let c_path = c(path.to_str().unwrap());
    // SAFETY: a NUL-terminated path that outlives the call.
    assert!(unsafe { atrium_core_open(c_path.as_ptr(), INSTANT) });
    assert!(act());
    assert_eq!(atrium_core_save(), 1);
    let doc = spaces::load(&std::fs::read_to_string(&path).unwrap(), INSTANT).unwrap();
    atrium_core_close();
    urls(&doc.spaces[0]).into_iter().map(String::from).collect()
}

#[test]
fn reorder_counts_a_favourite_url_tab() {
    let left = after(|| atrium_core_reorder_tab(0, 1, 2));
    assert_eq!(
        left,
        vec![
            "https://fav.test/",
            "https://last.test/",
            "https://target.test/"
        ]
    );
}

#[test]
fn archive_counts_a_favourite_url_tab() {
    let left = after(|| atrium_core_archive_tab(0, 1, INSTANT));
    assert_eq!(left, vec!["https://fav.test/", "https://last.test/"]);
}
