// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! the bytes Rust writes against the bytes Swift writes.
//!
//! `fixtures/stores/<store>/current.raw.json` is each store's OWN output,
//! unformatted - sorted keys, no whitespace. The pretty copies next to them are
//! for reading and hide exactly the things a second writer has to get right.
//!
//! Byte comparisons on purpose. Equal documents are not enough: if the two
//! writers disagree about key order, number formatting or escaping, every save
//! during the transition rewrites the whole file for no reason, and a save
//! stops being a fixed point.
//!
//! ONLY spaces.json can be compared this way, and finding that out was worth
//! the exercise. Its encoder sets `.sortedKeys`; the other five stores write in
//! dictionary order, which Swift randomises per process. Seeding settings.json
//! twice produces two different files - measured, not assumed. So for those
//! five the byte question has no answer, and what IS worth asserting is that
//! the document survives and that RUST's own output is stable.

use std::path::PathBuf;

use atrium_core::persist::to_sorted_json;

mod common;
use common::{read_fixture, repo_path};

/// The same fixed instant the corpus harnesses use.
const INSTANT: f64 = 788_918_400.0;

fn fixture(name: &str) -> PathBuf {
    repo_path("fixtures/stores").join(name)
}

/// A store's pretty input. A missing corpus fails the test - it used to return
/// None, which read as a pass.
fn input(store: &str) -> String {
    read_fixture(&fixture(&format!("{store}/current.json")))
}

/// A store's raw byte reference. Only spaces has one; see the note above.
fn raw(store: &str) -> String {
    read_fixture(&fixture(&format!("{store}/current.raw.json")))
        .trim_end()
        .to_string()
}

#[test]
fn spaces_writes_the_same_bytes_swift_wrote() {
    let doc = atrium_core::spaces::load(&input("spaces"), INSTANT).expect("the fixture must load");
    assert_eq!(to_sorted_json(&doc).unwrap(), raw("spaces"));
}

/// The five stores whose Swift bytes are not stable. What matters for them is
/// that Rust's own output does not move: a save has to be a fixed point, or
/// every save rewrites the file for no reason. That property is Rust's to keep
/// whatever Swift did.
#[test]
fn every_store_writes_a_stable_document() {
    let stable = |store: &str, once: String, reload: &dyn Fn(&str) -> String| {
        assert_eq!(reload(&once), once, "{store} is not a fixed point");
    };

    let text = input("settings");
    let f = |j: &str| to_sorted_json(&atrium_core::settings::load(j)).unwrap();
    stable("settings", f(&text), &f);

    let text = input("themes");
    let f = |j: &str| to_sorted_json(&atrium_core::small_stores::load_themes(j)).unwrap();
    stable("themes", f(&text), &f);

    let text = input("hotkeys");
    let f = |j: &str| to_sorted_json(&atrium_core::small_stores::load_hotkeys(j)).unwrap();
    stable("hotkeys", f(&text), &f);

    let text = input("routing");
    let f = |j: &str| to_sorted_json(&atrium_core::small_stores::load_routing(j)).unwrap();
    stable("routing", f(&text), &f);

    let text = input("command-bar-selection-memory");
    let f = |j: &str| to_sorted_json(&atrium_core::small_stores::load_selection_memory(j)).unwrap();
    stable("selection-memory", f(&text), &f);
}

/// spaces.json too, through a full load-write-load cycle.
#[test]
fn a_spaces_save_is_a_fixed_point() {
    let doc = atrium_core::spaces::load(&input("spaces"), INSTANT).unwrap();
    let once = to_sorted_json(&doc).unwrap();
    let reloaded = atrium_core::spaces::load(&once, INSTANT).unwrap();
    assert_eq!(to_sorted_json(&reloaded).unwrap(), once);
}
