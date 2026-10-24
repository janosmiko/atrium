// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The in-memory document, and the one property that matters: after `open`,
//! building a snapshot touches no file.
//!
//! `PushSidebarSnapshot` runs on the UI thread, where a read is a fatal
//! DCHECK. Every test here exists to hold that line.

use atrium_core::{cache, sidebar_model};

mod common;
use common::TempDir;

/// The corpus document - the same input `sidebar_snapshot.rs` holds the
/// producer to, so the cache is compared on real data rather than a toy.
fn five_profiles() -> String {
    let path = concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../fixtures/stores/spaces/current.json"
    );
    std::fs::read_to_string(path).expect("the spaces corpus fixture")
}

/// A second, clearly different document - a v2 file, so the tests that need
/// two also carry the migration through the cache.
fn other_document() -> String {
    let path = concat!(
        env!("CARGO_MANIFEST_DIR"),
        "/../../fixtures/stores/spaces/v2-with-epoch.json"
    );
    std::fs::read_to_string(path).expect("the v2 spaces fixture")
}

const LIVE: &str = r#"[{"url":"https://example.com/","id":11}]"#;
const INSTANT: f64 = 774_800_000.0;

#[test]
fn cached_model_equals_the_stateless_producer() {
    let dir = TempDir::new("equal");
    let path = dir.join("spaces.json");
    let json = five_profiles();
    std::fs::write(&path, &json).unwrap();

    let want = sidebar_model(&json, None, LIVE, INSTANT).expect("stateless producer");

    let core = cache::Core::new();
    assert!(core.open(&path, INSTANT).is_ok(), "open should succeed");
    let got = core.sidebar_model(None, LIVE).expect("cached producer");

    assert_eq!(got, want, "the cache must not reshape the document");
}

#[test]
fn a_snapshot_after_the_file_is_gone_still_works() {
    // The point of the cache. If this ever needs the file, the read has crept
    // back onto the UI thread and the app dies three seconds after launch.
    let dir = TempDir::new("nofile");
    let path = dir.join("spaces.json");
    std::fs::write(&path, five_profiles()).unwrap();

    let core = cache::Core::new();
    core.open(&path, INSTANT).expect("open");
    let before = core.sidebar_model(None, LIVE).expect("before");

    std::fs::remove_file(&path).unwrap();

    let after = core
        .sidebar_model(None, LIVE)
        .expect("the cache holds the document");
    assert_eq!(before, after, "the snapshot must come from memory alone");
}

#[test]
fn a_snapshot_before_open_returns_nothing() {
    // Not empty defaults. Nothing. An empty model would render an empty
    // sidebar, which looks exactly like data loss.
    let core = cache::Core::new();
    assert!(core.sidebar_model(None, LIVE).is_err());
}

#[test]
fn a_refused_store_leaves_the_cache_shut() {
    let dir = TempDir::new("refused");
    let path = dir.join("spaces.json");
    // A schema from the future. R1.2: read nothing, write nothing.
    std::fs::write(&path, r#"{"version": 99, "spaces": []}"#).unwrap();

    let core = cache::Core::new();
    assert!(
        core.open(&path, INSTANT).is_err(),
        "a refused file is not open"
    );
    assert!(
        core.sidebar_model(None, LIVE).is_err(),
        "a shut cache must send the caller to the flat tab list"
    );
}

#[test]
fn a_missing_file_leaves_the_cache_shut() {
    let dir = TempDir::new("missing");
    let core = cache::Core::new();
    assert!(core.open(&dir.join("nothing.json"), INSTANT).is_err());
    assert!(core.sidebar_model(None, LIVE).is_err());
}

#[test]
fn opening_twice_on_the_same_path_keeps_the_first_read() {
    // "Loads once and keeps it." Once mutations land in memory, a second open
    // that re-read the file would throw them away.
    let dir = TempDir::new("twice");
    let path = dir.join("spaces.json");
    std::fs::write(&path, five_profiles()).unwrap();

    let core = cache::Core::new();
    core.open(&path, INSTANT).expect("first open");
    let first = core.sidebar_model(None, LIVE).expect("first model");

    // Rewrite the file with a different document. A second open must ignore it.
    std::fs::write(&path, other_document()).unwrap();
    core.open(&path, INSTANT).expect("second open is a no-op");

    assert_eq!(
        core.sidebar_model(None, LIVE).expect("second model"),
        first,
        "a second open on the same path must not re-read"
    );
}

#[test]
fn opening_a_different_path_replaces_the_document() {
    // A different profile is a different document, so this one must reload.
    let dir = TempDir::new("switch");
    let (a, b) = (dir.join("a.json"), dir.join("b.json"));
    std::fs::write(&a, five_profiles()).unwrap();
    std::fs::write(&b, other_document()).unwrap();

    let core = cache::Core::new();
    core.open(&a, INSTANT).unwrap();
    let from_a = core.sidebar_model(None, LIVE).unwrap();
    core.open(&b, INSTANT).unwrap();
    let from_b = core.sidebar_model(None, LIVE).unwrap();

    assert_ne!(
        from_a, from_b,
        "the fixtures differ, so the models must too"
    );
}

#[test]
fn snapshots_and_opens_from_many_threads_do_not_race() {
    let dir = TempDir::new("threads");
    let path = dir.join("spaces.json");
    std::fs::write(&path, five_profiles()).unwrap();

    let core = std::sync::Arc::new(cache::Core::new());
    core.open(&path, INSTANT).unwrap();
    let want = core.sidebar_model(None, LIVE).unwrap();

    let mut handles = Vec::new();
    for _ in 0..8 {
        let core = core.clone();
        let want = want.clone();
        handles.push(std::thread::spawn(move || {
            for _ in 0..50 {
                assert_eq!(core.sidebar_model(None, LIVE).unwrap(), want);
            }
        }));
    }
    // Re-opening the same path while readers run. The lock is the only thing
    // making this safe, which is what the test is for.
    for _ in 0..20 {
        core.open(&path, INSTANT).unwrap();
    }
    for h in handles {
        h.join().expect("no thread panicked or deadlocked");
    }
}
