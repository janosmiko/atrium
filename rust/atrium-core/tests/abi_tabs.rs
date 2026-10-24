// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The tab open and close, strip and launch exports as C++ calls them.
//!
//! ONE test, for the reason `abi_writes.rs` gives: these functions address the
//! process-wide core that `atrium_core_open` fills, so two of them at once
//! would be two windows fighting over one document. The single walk is also the
//! honest shape - a launch reads its plan, then a session of opens and closes
//! runs against the same document.

use atrium_core::mutate::test_support::urls;
use atrium_core::{
    atrium_core_close, atrium_core_close_tab, atrium_core_forget_tab, atrium_core_launch_plan,
    atrium_core_move_tab_by_tab_id, atrium_core_note_tab_replaced, atrium_core_open,
    atrium_core_open_tab, atrium_core_save, spaces,
};

mod common;
use common::{TempDir, c};

const INSTANT: f64 = 788_918_400.0;

/// -1 for the window's slot: C++ does not track which space a window shows, so
/// the document's own activeSpaceID answers.
const OWN: i32 = -1;

/// Two profiles, so the launch plan has something to say beyond Default, and a
/// cold row plus a pinned tile so a take-over and a kept tile are both exercised.
const DOCUMENT: &str = r##"{"spaces":[
{"id":"A","name":"Work","emoji":"W","tint":"#111111","profileID":"Work",
 "pinnedTabs":[{"url":"https://pin.test/","title":"Pin"}],
 "tabs":[{"url":"https://cold.test/","title":"Cold","lastActiveAt":900}],
 "order":0,"groups":[]},
{"id":"B","name":"Home","emoji":"H","tint":"#222222","profileID":"Home",
 "pinnedTabs":[],"tabs":[],"order":1,"groups":[]}],
"appState":{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":3,
"archiveEpoch":1,"sidebarWidthMigrated":true}}"##;

/// Reads and frees a string the core handed back, the way C++ has to.
fn take(raw: *mut std::os::raw::c_char) -> String {
    unsafe { common::take_string(raw) }.expect("the core must answer")
}

fn open(tab_id: u64, url: &str, title: &str, slot: i32) -> bool {
    let (url, title) = (c(url), c(title));
    unsafe { atrium_core_open_tab(OWN, tab_id, url.as_ptr(), title.as_ptr(), slot) }
}

#[test]
fn the_strip_and_the_launch_plan_cross_the_abi_and_land_on_disk() {
    let dir = TempDir::new("abi-tabs");
    let path = dir.join("spaces.json");
    std::fs::write(&path, DOCUMENT).unwrap();

    // settings.json's Startup row, spelt the way C++ passes it through.
    let (restore, blank) = (c("restoreLastActive"), c("blankTab"));

    // Nothing open: a launch has no plan to read and a tab event must not
    // pretend to have persisted anything.
    atrium_core_close();
    assert!(
        unsafe { atrium_core_launch_plan(restore.as_ptr()) }.is_null(),
        "no plan is not an empty plan - the caller falls back to one Default"
    );
    assert!(!open(1, "https://one.test/", "One", -1));
    assert!(!atrium_core_close_tab(1));

    let c_path = c(path.to_str().unwrap());
    assert!(unsafe { atrium_core_open(c_path.as_ptr(), INSTANT) });

    // what ProfileBootstrap reads before it creates a profile.
    assert_eq!(
        take(unsafe { atrium_core_launch_plan(restore.as_ptr()) }),
        r#"{"requiredProfiles":["Home","Work"],"startupProfiles":["Work"],"startupTab":{"profileID":"Work","url":"https://cold.test/"}}"#,
        "both profiles are loaded, and the shown window restores the active space's last tab"
    );

    // the Startup row reaches the plan. Same document, same cold row
    // to restore - only the row differs, and only the url moves.
    assert_eq!(
        take(unsafe { atrium_core_launch_plan(blank.as_ptr()) }),
        r#"{"requiredProfiles":["Home","Work"],"startupProfiles":["Work"],"startupTab":{"profileID":"Work","url":"about:blank"}}"#,
        "blankTab refuses the tab the document would have restored"
    );

    // A row this build cannot read must not decide the launch for the user.
    assert_eq!(
        take(unsafe { atrium_core_launch_plan(std::ptr::null()) }),
        take(unsafe { atrium_core_launch_plan(restore.as_ptr()) }),
        "an absent row restores, the same as the settings loader's default"
    );

    // The startup tab lands on the cold row rather than beside it.
    assert!(
        !open(1, "https://cold.test/", "Cold", -1),
        "a take-over moves nothing"
    );
    assert_eq!(atrium_core_save(), 0, "so there is nothing to write yet");

    // Two more tabs, one appended and one dropped at the top of the list.
    assert!(open(2, "https://two.test/", "Two", -1));
    assert!(open(3, "https://three.test/", "Three", 0));
    assert!(
        !open(3, "https://three.test/", "Three", -1),
        "the same tab announced twice is still one row"
    );

    // A favourite dropped in the tab list: its row opens at the top, then
    // moves to the drop slot by its tab id.
    assert!(!atrium_core_move_tab_by_tab_id(99, 1), "no row for tab 99");
    assert!(!atrium_core_move_tab_by_tab_id(3, -1));
    assert!(!atrium_core_move_tab_by_tab_id(3, 0), "already there");
    assert!(atrium_core_move_tab_by_tab_id(3, 1));
    assert!(
        atrium_core_move_tab_by_tab_id(3, 3),
        "the slot past the last row appends"
    );
    assert!(atrium_core_move_tab_by_tab_id(3, 0));

    // A pinned tile's tab, opened and closed: the tile stays.
    assert!(!open(4, "https://pin.test/", "Pin", -1));
    assert!(
        !atrium_core_close_tab(4),
        "a pin is the user's, not the tab's"
    );

    // The discard swap: announced first, so the close and add echo that follows
    // is a pair of no-ops instead of a delete and a re-append.
    assert!(atrium_core_note_tab_replaced(2, 20));
    assert!(!atrium_core_note_tab_replaced(2, 21), "and only once");
    assert!(!atrium_core_close_tab(2));
    assert!(!open(20, "https://two.test/", "Two", -1));

    // The genuine closes.
    assert!(atrium_core_close_tab(20));
    assert!(!atrium_core_close_tab(20), "and only once");

    // The window tearing down. Its tabs were not closed by the user, so their
    // rows stay and render cold next launch - only the bindings go.
    assert!(atrium_core_forget_tab(1));
    assert!(atrium_core_forget_tab(3));
    assert!(!atrium_core_forget_tab(3), "and only once");
    assert!(
        !atrium_core_forget_tab(4),
        "the pinned tab was already closed"
    );

    assert_eq!(atrium_core_save(), 1, "the session is written");
    assert_eq!(atrium_core_save(), 0, "and a second save has nothing to do");

    // What the next launch reads.
    let doc = spaces::load(&std::fs::read_to_string(&path).unwrap(), INSTANT).unwrap();
    let work = &doc.spaces[0];
    assert_eq!(
        urls(work),
        vec!["https://three.test/", "https://cold.test/"],
        "the tab dropped at the top is at the top, the swapped one is gone, \
         and the cold row was taken over rather than duplicated"
    );
    assert_eq!(work.pinned_tabs.len(), 1, "the tile survived its tab");
    assert!(
        doc.spaces[1].tabs.is_empty(),
        "the other space was untouched"
    );

    atrium_core_close();
}
