// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Archive Tab and Archive Tabs Below take an index into the drawn rows, the
//! same list C++ closes from. A folder member and a row hidden by token sit in
//! `tabs` but not in that list.

use atrium_core::mutate::test_support::urls;
use atrium_core::{
    atrium_core_archive_tab, atrium_core_archive_tabs_below, atrium_core_bind_tab_to_row,
    atrium_core_close, atrium_core_open, atrium_core_save, atrium_core_sidebar_model_cached,
    atrium_core_string_free, spaces,
};

mod common;
use common::{TempDir, abi_serial, c};

const INSTANT: f64 = 788_918_400.0;

/// Drawn rows are first, target, last. The grouped tab and the fav tab are not:
/// the fav tab is hidden by its token once a live tab claims it.
const CROWDED: &str = r##"{"spaces":[
{"id":"A","name":"Work","emoji":"W","tint":"#111111","profileID":"p",
 "pinnedTabs":[],
 "tabs":[{"url":"https://first.test/","title":"First"},
         {"url":"https://grouped.test/","title":"Grouped","groupID":"G"},
         {"url":"https://fav.test/","title":"Fav"},
         {"url":"https://target.test/","title":"Target"},
         {"url":"https://last.test/","title":"Last"}],
 "order":0,"groups":[{"id":"G","name":"Reading","collapsed":false}]}],
"favorites":[{"url":"https://fav.test/","title":"Fav","profileID":"p"}],
"appState":{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":3,
"archiveEpoch":1,"sidebarWidthMigrated":true}}"##;

fn hide_fav_row() {
    let url = c("https://fav.test/");
    // SAFETY: a NUL-terminated string that outlives the call.
    assert!(unsafe { atrium_core_bind_tab_to_row(0, 7, url.as_ptr()) });
    let live = c(r#"[{"url":"https://fav.test/","id":7,"claimed":"https://fav.test/"}]"#);
    // SAFETY: a NUL-terminated string valid for the call, and a result handed
    // back once. The push records the claimed row in `hidden_rows`.
    unsafe { atrium_core_string_free(atrium_core_sidebar_model_cached(0, live.as_ptr())) };
}

fn remaining(act: impl FnOnce() -> bool) -> (bool, Vec<String>) {
    let _guard = abi_serial();
    let dir = TempDir::new("abi-archive-visible");
    let path = dir.join("spaces.json");
    std::fs::write(&path, CROWDED).unwrap();
    let c_path = c(path.to_str().unwrap());
    // SAFETY: a NUL-terminated path that outlives the call.
    assert!(unsafe { atrium_core_open(c_path.as_ptr(), INSTANT) });
    hide_fav_row();
    let changed = act();
    assert_eq!(atrium_core_save(), i32::from(changed));
    let doc = spaces::load(&std::fs::read_to_string(&path).unwrap(), INSTANT).unwrap();
    atrium_core_close();
    (
        changed,
        urls(&doc.spaces[0]).into_iter().map(String::from).collect(),
    )
}

#[test]
fn archive_tab_takes_the_drawn_row() {
    let (changed, left) = remaining(|| atrium_core_archive_tab(0, 1, INSTANT));
    assert!(changed);
    assert!(
        !left.contains(&"https://target.test/".to_string()),
        "{left:?}"
    );
    assert_eq!(left.len(), 4);
}

#[test]
fn archive_tab_past_the_drawn_rows_changes_nothing() {
    let (changed, left) = remaining(|| atrium_core_archive_tab(0, 3, INSTANT));
    assert!(!changed);
    assert_eq!(left.len(), 5);
}

#[test]
fn archive_below_takes_only_drawn_loose_rows_after_it() {
    let (changed, left) = remaining(|| atrium_core_archive_tabs_below(0, 0, INSTANT));
    assert!(changed);
    assert_eq!(
        left,
        vec![
            "https://first.test/",
            "https://grouped.test/",
            "https://fav.test/"
        ]
    );
}

#[test]
fn archive_below_minus_one_takes_every_drawn_loose_row() {
    let (changed, left) = remaining(|| atrium_core_archive_tabs_below(0, -1, INSTANT));
    assert!(changed);
    assert_eq!(left, vec!["https://grouped.test/", "https://fav.test/"]);
}
