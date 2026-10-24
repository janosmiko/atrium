// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! a session of opening and closing tabs, and what the next launch
//! reads.
//!
//! Nothing persisted either between the Swift cut and this task. The document
//! kept rows for tabs that were gone, missed the ones that were opened, and the
//! gap grew with every session - so the sidebar a user came back to had not
//! been true since the last time Swift ran.
//!
//! What is asserted is the FILE, reloaded, because that is the only thing the
//! next launch sees. A test that stopped at the in-memory document would pass
//! for a save that never happened.

use atrium_core::{cache, mutate, persist, spaces, tabs};

mod common;
use common::TempDir;

const INSTANT: f64 = 788_918_400.0;

/// The window's slot: C++ does not track which space a window shows, so the
/// document's own activeSpaceID answers.
const OWN: Option<usize> = None;

/// One space with a cold row, a pinned tile and a grouped tab - the three
/// shapes an open or a close has to treat differently.
const DOCUMENT: &str = r##"{"spaces":[
{"id":"A","name":"Work","emoji":"W","tint":"#111111","profileID":"p",
 "pinnedTabs":[{"url":"https://pin.test/","title":"Pin"}],
 "tabs":[{"url":"https://cold.test/","title":"Cold"},
         {"url":"https://grouped.test/","title":"Grouped","groupID":"G"}],
 "order":0,"groups":[{"id":"G","name":"Reading","collapsed":false}]}],
"appState":{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":3,
"archiveEpoch":1,"sidebarWidthMigrated":true}}"##;

fn seeded(tag: &str) -> (TempDir, std::path::PathBuf) {
    let dir = TempDir::new(tag);
    let path = dir.join("spaces.json");
    std::fs::write(&path, DOCUMENT).unwrap();
    (dir, path)
}

fn reload(path: &std::path::Path) -> spaces::Document {
    let json = std::fs::read_to_string(path).expect("the store must be readable");
    spaces::load(&json, INSTANT).expect("the store must load")
}

fn urls(space: &spaces::Space) -> Vec<&str> {
    space.tabs.iter().map(|t| t.url.as_str()).collect()
}

/// The one gesture pair the task is about, driven through the core the way
/// `BrowserController::AddTab` and `RemoveTab` will.
fn open(core: &cache::Core, tab_id: u64, url: &str, title: &str, slot: Option<usize>) -> bool {
    core.apply_bound(|doc, bindings| {
        mutate::open_live_tab(doc, bindings, OWN, tab_id, url, title, slot)
    })
}

fn close(core: &cache::Core, tab_id: u64) -> bool {
    core.apply_bound(|doc, bindings| mutate::close_live_tab(doc, bindings, tab_id, INSTANT))
}

/// The discard/prerender announcement, as `kReplaced` will send it. It moves a
/// binding and never the document, so it can never dirty the file.
fn carry(core: &cache::Core, old_tab_id: u64, new_tab_id: u64) -> bool {
    let mut carried = false;
    core.apply_bound(|_doc, bindings| {
        carried = bindings.rekey(old_tab_id, new_tab_id);
        false
    });
    carried
}

/// A window tearing down: the binding goes, the row stays.
fn forget(core: &cache::Core, tab_id: u64) -> bool {
    let mut forgotten = false;
    core.apply_bound(|_doc, bindings| {
        forgotten = bindings.release(tab_id).is_some();
        false
    });
    forgotten
}

/// The walk the acceptance criteria describe: a session of opening and closing
/// tabs, saved, and then read back as the next launch would read it.
#[test]
fn a_session_of_opening_and_closing_tabs_is_what_the_next_launch_reads() {
    let (_dir, path) = seeded("session");
    let core = cache::Core::new();
    core.open(&path, INSTANT).unwrap();

    // Three tabs opened, each one landing above the last.
    assert!(open(&core, 1, "https://one.test/", "One", None));
    assert!(open(&core, 2, "https://two.test/", "Two", None));
    assert!(open(&core, 3, "https://three.test/", "Three", None));

    // The middle one closed again. Its row goes, and only its row.
    assert!(close(&core, 2));

    // A fourth. The slot the strip offers is ignored, so it lands on top too.
    assert!(open(&core, 4, "https://four.test/", "Four", Some(0)));

    // Closing a tab that was never persisted changes nothing.
    assert!(!close(&core, 99));

    assert_eq!(core.save().unwrap(), true, "the session is written");
    assert_eq!(core.save().unwrap(), false, "and only once");

    let doc = reload(&path);
    let space = &doc.spaces[0];
    assert_eq!(
        urls(space),
        vec![
            "https://four.test/",
            "https://three.test/",
            "https://one.test/",
            "https://cold.test/",
            "https://grouped.test/",
        ],
        "the strip's opens and closes, and nothing else moved"
    );
    assert_eq!(
        space.pinned_tabs.len(),
        1,
        "the pinned tile was never in play"
    );
    assert_eq!(
        space.tabs[4].group_id.as_deref(),
        Some("G"),
        "the grouped row kept its folder"
    );
}

/// Clicking a cold row opens a live tab on its URL. Appending a second row for
/// it would leave the user looking at their tab twice - and then a close would
/// delete the wrong one.
#[test]
fn opening_a_cold_row_takes_it_over_instead_of_duplicating_it() {
    let (_dir, path) = seeded("adopt");
    let core = cache::Core::new();
    core.open(&path, INSTANT).unwrap();

    assert!(
        !open(&core, 1, "https://cold.test/", "Cold", None),
        "a take-over moves nothing, so there is nothing to write"
    );
    assert_eq!(core.save().unwrap(), false);

    // And the binding still points at that row, so its close deletes it.
    assert!(close(&core, 1));
    core.save().unwrap();

    let doc = reload(&path);
    assert_eq!(urls(&doc.spaces[0]), vec!["https://grouped.test/"]);
}

/// Two tabs on one site are two tabs. The URL join alone cannot tell them
/// apart, which is why the core keeps a binding.
#[test]
fn two_tabs_on_one_url_get_two_rows_and_close_independently() {
    let (_dir, path) = seeded("duplicate-url");
    let core = cache::Core::new();
    core.open(&path, INSTANT).unwrap();

    // The first takes over the cold row; the second needs its own.
    assert!(!open(&core, 1, "https://cold.test/", "Cold", None));
    assert!(open(&core, 2, "https://cold.test/", "Cold again", None));

    assert!(close(&core, 2));
    core.save().unwrap();

    let doc = reload(&path);
    assert_eq!(
        urls(&doc.spaces[0]),
        vec!["https://cold.test/", "https://grouped.test/"],
        "the surviving tab still has exactly one row"
    );
}

/// DATA-SAFETY INVARIANT: a pinned tab's close reverts it to its cold tile. The
/// pin is the user's, not the tab's.
#[test]
fn closing_a_pinned_tab_leaves_the_tile_behind() {
    let (_dir, path) = seeded("pinned-close");
    let core = cache::Core::new();
    core.open(&path, INSTANT).unwrap();

    assert!(!open(&core, 1, "https://pin.test/", "Pin", None));
    assert!(
        !close(&core, 1),
        "the entry is kept, so there is nothing to write"
    );
    assert_eq!(core.save().unwrap(), false);
    assert_eq!(reload(&path).spaces[0].pinned_tabs.len(), 1);
}

/// A discard or prerender activation is the same tab with a new WebContents,
/// and Chromium reports it as an ordinary close followed by an ordinary add.
/// Read literally that deletes the row and re-appends it at the end, which is
/// how a tab loses everything the document knew about it.
#[test]
fn a_discard_swap_keeps_the_row_where_it_was() {
    let (_dir, path) = seeded("replace");
    let core = cache::Core::new();
    core.open(&path, INSTANT).unwrap();

    assert!(open(&core, 1, "https://first.test/", "First", None));
    assert!(open(&core, 2, "https://second.test/", "Second", None));
    core.save().unwrap();
    let before = std::fs::read(&path).unwrap();

    // The swap, announced before the echo pair - then the echo pair itself.
    assert!(carry(&core, 1, 7));
    assert!(!close(&core, 1), "the old id holds nothing now");
    assert!(
        !open(&core, 7, "https://first.test/", "First", None),
        "and the new id is already on that row"
    );

    assert_eq!(
        core.save().unwrap(),
        false,
        "the swap moved no row, so there is nothing to write"
    );
    assert_eq!(std::fs::read(&path).unwrap(), before, "and nothing moved");

    // The swapped tab's close still deletes the right row afterwards.
    assert!(close(&core, 7));
    core.save().unwrap();
    assert_eq!(
        urls(&reload(&path).spaces[0]),
        vec![
            "https://second.test/",
            "https://cold.test/",
            "https://grouped.test/"
        ]
    );
}

/// Without the announcement the pair really is a delete and a re-append, which
/// is the bug the announcement exists to prevent. Pinned here so the difference
/// between the two paths stays visible.
#[test]
fn an_unannounced_swap_is_the_delete_and_reappend_it_looks_like() {
    let (_dir, path) = seeded("replace-unannounced");
    let core = cache::Core::new();
    core.open(&path, INSTANT).unwrap();

    assert!(open(&core, 1, "https://first.test/", "First", None));
    assert!(open(&core, 2, "https://second.test/", "Second", None));

    assert!(close(&core, 1));
    assert!(open(&core, 7, "https://first.test/", "First", None));
    core.save().unwrap();

    assert_eq!(
        urls(&reload(&path).spaces[0]),
        vec![
            "https://first.test/",
            "https://second.test/",
            "https://cold.test/",
            "https://grouped.test/"
        ],
        "the row moved to the top - what the announcement prevents"
    );
}

/// A save has to stay a FIXED POINT after a session of opens and closes: write,
/// read it back, write again, same bytes. Without it every later save rewrites
/// the whole file for no reason.
#[test]
fn a_session_leaves_the_file_a_fixed_point() {
    let (_dir, path) = seeded("fixed-point");
    let core = cache::Core::new();
    core.open(&path, INSTANT).unwrap();

    open(&core, 1, "https://one.test/", "One", None);
    open(&core, 2, "https://two.test/", "Two", Some(0));
    close(&core, 1);
    core.save().unwrap();
    let once = std::fs::read(&path).unwrap();

    persist::save_document(&reload(&path), &path).unwrap();
    assert_eq!(std::fs::read(&path).unwrap(), once);
}

/// A closing WINDOW is not a user closing tabs. Routing its teardown through
/// the close path would empty the space's persisted tab list on every quit -
/// which is the one way this task could be worse than the drift it fixes.
#[test]
fn a_window_teardown_keeps_every_row_and_still_frees_the_bindings() {
    let (_dir, path) = seeded("teardown");
    let core = cache::Core::new();
    core.open(&path, INSTANT).unwrap();

    open(&core, 1, "https://one.test/", "One", None);
    open(&core, 2, "https://two.test/", "Two", None);
    core.save().unwrap();
    let before = std::fs::read(&path).unwrap();

    for tab_id in [1, 2] {
        assert!(forget(&core, tab_id));
        assert!(!forget(&core, tab_id), "and only once");
    }
    assert_eq!(
        core.save().unwrap(),
        false,
        "forgetting a tab moves no row, so there is nothing to write"
    );
    assert_eq!(std::fs::read(&path).unwrap(), before);

    // The rows are free again, so the next launch's tabs take them over rather
    // than adding duplicates beside them.
    assert!(!open(&core, 11, "https://one.test/", "One", None));
    assert!(!open(&core, 12, "https://two.test/", "Two", None));
    assert_eq!(core.save().unwrap(), false);
}

/// The map is bounded by the live tabs, which is the only reason it is safe to
/// keep for the life of a document.
#[test]
fn the_binding_map_is_bounded_by_the_live_tabs() {
    let mut doc = spaces::load(DOCUMENT, INSTANT).unwrap();
    let mut bindings = tabs::Bindings::new();

    for tab_id in 0..50u64 {
        mutate::open_live_tab(
            &mut doc,
            &mut bindings,
            OWN,
            tab_id,
            &format!("https://{tab_id}.test/"),
            "T",
            None,
        );
    }
    assert_eq!(bindings.len(), 50);

    for tab_id in 0..50u64 {
        mutate::close_live_tab(&mut doc, &mut bindings, tab_id, INSTANT);
    }
    assert!(bindings.is_empty(), "every open is undone by its close");
}
