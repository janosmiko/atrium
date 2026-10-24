// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! the write path end to end - a gesture arrives, the document
//! moves, and the file on disk carries it.
//!
//! Nothing wrote the stores before this. The crate exported nine C functions
//! and every one was a read, so every sidebar gesture - a reorder, a pin, a
//! fold - was lost when the browser quit.
//!
//! What is asserted here is the file, not the document. A save that produces an
//! equal document but different bytes rewrites the whole file on every gesture
//! and stops being a fixed point, and only a byte comparison catches that.

use atrium_core::{cache, mutate, persist, settings, spaces};

mod common;
use common::TempDir;
use common::{read_fixture, repo_path};

const INSTANT: f64 = 788_918_400.0;

/// A space with a GROUP in the middle of its tabs, which is the shape where the
/// rendered row positions and the persisted array diverge.
const DOCUMENT: &str = r##"{"spaces":[
{"id":"A","name":"Work","emoji":"W","tint":"#111111","profileID":"p",
 "pinnedTabs":[{"url":"https://pin.test/","title":"Pin"}],
 "tabs":[{"url":"https://one.test/","title":"One"},
         {"url":"https://two.test/","title":"Two","groupID":"G"},
         {"url":"https://three.test/","title":"Three"}],
 "order":0,"groups":[{"id":"G","name":"Reading","collapsed":false}]},
{"id":"B","name":"Home","emoji":"H","tint":"#222222","profileID":"p",
 "pinnedTabs":[],"tabs":[],"order":1,"groups":[]}],
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

/// The gesture the sidebar sends most: drag the first row below the second.
///
/// With a grouped tab between them, the visible positions (0, 1) are persisted
/// positions (0, 2). An untranslated index would move the tab to persisted 1,
/// which renders as no change at all - the bug that only appears for users who
/// have tab groups.
#[test]
fn a_reorder_lands_on_the_disk_translated() {
    let (_dir, path) = seeded("reorder");
    let core = cache::Core::new();
    core.open(&path, INSTANT).unwrap();

    assert!(core.apply(|doc| {
        let space = mutate::active_space(doc, None).unwrap();
        let from = mutate::persisted_index_of_visible(0, &doc.spaces[space], &[]).unwrap();
        let to = mutate::persisted_index_of_visible(1, &doc.spaces[space], &[]).unwrap();
        mutate::move_tab(doc, space, from, to, mutate::Section::Regular)
    }));
    assert_eq!(core.save().unwrap(), true, "a moved document is written");

    let doc = reload(&path);
    assert_eq!(
        urls(&doc.spaces[0]),
        vec![
            "https://two.test/",
            "https://three.test/",
            "https://one.test/"
        ],
        "the grouped tab stayed put and the dragged row went past the other visible one"
    );
}

/// The copy that makes going back possible, and the `.bak` that is refreshed on
/// every save. The `.pre-rust` one is taken ONCE, before Rust has ever written,
/// and never again - `.bak` is overwritten within a second and would be gone.
#[test]
fn the_first_save_takes_the_one_shot_copy_and_the_bak() {
    let (_dir, path) = seeded("backups");
    let core = cache::Core::new();
    core.open(&path, INSTANT).unwrap();

    let pre_rust = persist::pre_rust_backup_path(&path);
    let bak = persist::backup_path(&path);
    assert!(!pre_rust.exists() && !bak.exists());

    core.apply(|doc| mutate::set_pinned_collapsed(doc, 0, true));
    core.save().unwrap();

    assert_eq!(
        std::fs::read_to_string(&pre_rust).unwrap(),
        DOCUMENT,
        "the pre-Rust copy holds what was there before Rust ever wrote"
    );
    assert_eq!(std::fs::read_to_string(&bak).unwrap(), DOCUMENT);
    assert_ne!(std::fs::read_to_string(&path).unwrap(), DOCUMENT);

    // A second save refreshes .bak from the file Rust wrote, and must NOT touch
    // the one-shot copy - that is exactly the state it exists to escape.
    core.apply(|doc| mutate::set_pinned_collapsed(doc, 0, false));
    core.save().unwrap();
    assert_eq!(
        std::fs::read_to_string(&pre_rust).unwrap(),
        DOCUMENT,
        "the pre-Rust state is still what it was"
    );
    assert_ne!(std::fs::read_to_string(&bak).unwrap(), DOCUMENT);
}

#[cfg(unix)]
#[test]
fn the_written_store_is_owner_only() {
    use std::os::unix::fs::PermissionsExt;
    let (_dir, path) = seeded("mode");
    // Loose to start with, as a pre-migration file is. The rename swaps the
    // inode, so this write fixes it rather than needing a migration pass.
    std::fs::set_permissions(&path, std::fs::Permissions::from_mode(0o644)).unwrap();

    let core = cache::Core::new();
    core.open(&path, INSTANT).unwrap();
    core.apply(|doc| mutate::set_pinned_collapsed(doc, 0, true));
    core.save().unwrap();

    for file in [
        path.clone(),
        persist::backup_path(&path),
        persist::pre_rust_backup_path(&path),
    ] {
        let mode = std::fs::metadata(&file).unwrap().permissions().mode() & 0o777;
        assert_eq!(mode, 0o600, "{}", file.display());
    }
}

/// A save has to be a FIXED POINT: write, read it back, write again, same
/// bytes. Without it every save rewrites the whole file for no reason, and a
/// drift guard or a `git diff` over a user's stores sees noise.
#[test]
fn writing_reading_and_writing_again_gives_the_same_bytes() {
    let (_dir, path) = seeded("fixed-point");
    let doc = reload(&path);

    persist::save_document(&doc, &path).unwrap();
    let once = std::fs::read(&path).unwrap();

    persist::save_document(&reload(&path), &path).unwrap();
    assert_eq!(std::fs::read(&path).unwrap(), once);
}

/// The same fixed point on the real corpus document, which is the store's own
/// output and carries every shape a hand-written one misses.
#[test]
fn the_corpus_document_survives_a_save_byte_for_byte() {
    let corpus = repo_path("fixtures/stores/spaces/current.raw.json");
    let raw = read_fixture(&corpus).trim_end().to_string();

    let dir = TempDir::new("corpus");
    let path = dir.join("spaces.json");
    std::fs::write(&path, &raw).unwrap();

    persist::save_document(&reload(&path), &path).unwrap();
    assert_eq!(
        std::fs::read_to_string(&path).unwrap(),
        raw,
        "the whole save path reproduces the store's own bytes"
    );
}

/// R7.4, and the note in `docs/model-rules.md` calls it the one to watch in the
/// port: redaction is on the WRITE path only, on a copy. The file loses the
/// credential and the in-memory model keeps the real URL, because navigation
/// and autofill need it.
#[test]
fn a_credential_is_stripped_on_the_way_to_disk_and_kept_in_memory() {
    let dir = TempDir::new("redact");
    let path = dir.join("spaces.json");
    std::fs::write(
        &path,
        r##"{"spaces":[{"id":"A","name":"Work","emoji":"W","tint":"#111111",
        "profileID":"p","pinnedTabs":[],
        "tabs":[{"url":"https://example.com/x?token=SECRETVALUE","title":"One"}],
        "order":0,"groups":[]}],
        "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
        "sidebarWidthMigrated":true}}"##,
    )
    .unwrap();

    let core = cache::Core::new();
    core.open(&path, INSTANT).unwrap();
    core.apply(|doc| mutate::set_pinned_collapsed(doc, 0, true));
    core.save().unwrap();

    let written = std::fs::read_to_string(&path).unwrap();
    assert!(!written.contains("SECRETVALUE"), "{written}");

    let model = core.sidebar_model(None, "[]").unwrap();
    assert!(
        model.contains("SECRETVALUE"),
        "the model keeps the real URL - navigation and autofill need it"
    );
}

#[test]
fn a_save_with_nothing_to_write_leaves_the_file_alone() {
    let (_dir, path) = seeded("clean");
    let core = cache::Core::new();
    core.open(&path, INSTANT).unwrap();

    assert!(!core.is_dirty());
    assert_eq!(core.save().unwrap(), false, "nothing has moved");
    assert_eq!(std::fs::read_to_string(&path).unwrap(), DOCUMENT);
    assert!(
        !persist::backup_path(&path).exists(),
        "a save that writes nothing leaves no backup either"
    );

    // A gesture that resolves nothing is not a change, so it must not dirty the
    // document and must not cost a write.
    assert!(!core.apply(|doc| mutate::set_pinned_collapsed(doc, 9, true)));
    assert!(!core.is_dirty());
    assert_eq!(core.save().unwrap(), false);

    core.apply(|doc| mutate::set_pinned_collapsed(doc, 0, true));
    assert!(core.is_dirty());
    assert_eq!(core.save().unwrap(), true);
    assert!(!core.is_dirty(), "the file now holds it");
    assert_eq!(core.save().unwrap(), false, "and a second save is a no-op");
}

/// R1.2, now that there IS a write path: a file at a schema this build cannot
/// read is never written over. It held by construction while nothing wrote at
/// all, and it still holds - a refused file leaves the core shut, and a shut
/// core has nothing to save.
#[test]
fn a_refused_file_is_never_written_over() {
    let dir = TempDir::new("refused");
    let path = dir.join("spaces.json");
    let future = r#"{"spaces":[],"appState":{"sidebarWidth":280,"schemaVersion":999}}"#;
    std::fs::write(&path, future).unwrap();

    let core = cache::Core::new();
    assert!(core.open(&path, INSTANT).is_err());
    assert!(!core.apply(|doc| mutate::set_pinned_collapsed(doc, 0, true)));
    assert_eq!(core.save().unwrap(), false);
    assert_eq!(
        std::fs::read_to_string(&path).unwrap(),
        future,
        "a build that cannot read the file must not replace it"
    );
}

/// R1.2b: `Core::open`'s refusal only answers "could this build
/// read the file when the session started". The crate has no clock (R7.2b),
/// so a session holds its document in memory for as long as it stays open,
/// and `save_document` used to write straight over whatever landed on disk
/// afterward. This reproduces the window a refused-at-open test cannot: open
/// succeeds, a later build's file lands while this process still holds the
/// document it opened, and the next gesture's save must not clobber it.
#[test]
fn a_file_swapped_in_after_open_is_not_overwritten_by_the_next_save() {
    let (_dir, path) = seeded("swapped");
    let core = cache::Core::new();
    core.open(&path, INSTANT).unwrap();

    assert!(core.apply(|doc| mutate::set_pinned_collapsed(doc, 0, true)));
    assert!(core.is_dirty());

    // A newer build - or a sync client - lands its own file while this
    // process still holds the older document it opened in memory.
    let later = r#"{"spaces":[],"appState":{"sidebarWidth":280,"schemaVersion":999}}"#;
    std::fs::write(&path, later).unwrap();

    assert!(
        core.save().is_err(),
        "the file changed underneath the session; the save must refuse"
    );
    assert_eq!(
        std::fs::read_to_string(&path).unwrap(),
        later,
        "the later build's file is untouched"
    );
    assert!(
        core.is_dirty(),
        "the pending gesture is not silently dropped - the next save can still try"
    );
}

/// A shut core has nothing to mutate and nothing to write. It must say so
/// rather than inventing an empty document - an empty sidebar looks exactly
/// like lost data.
#[test]
fn a_shut_core_refuses_both_halves() {
    let core = cache::Core::new();
    assert!(!core.apply(|doc| mutate::set_pinned_collapsed(doc, 0, true)));
    assert_eq!(core.save().unwrap(), false);
    assert!(!core.is_dirty());
}

// --- a leftover temp file ---

/// Set on the child below, and carrying the directory it must work in.
const IN_THE_CHILD: &str = "ATRIUM_TASK476_CHILD_DIR";

/// The live spaces writer, over the exact directory a killed process leaves
/// behind.
///
/// `.atrium-<pid>-<n>.tmp` used to stop the save dead - `create_new` refuses an
/// existing file - and the counter restarts at zero in a new process, so a
/// process that inherited a dead one's pid was blocked from its first save
/// onwards. This is the gesture-to-disk path, which is the one a user notices:
/// every sidebar drag goes through it.
///
/// It runs in a CHILD process, and it has to. The counter behind those names is
/// global to the process, every other test in this binary consumes from it, and
/// only in a fresh process is the name the save will pick knowable. So the
/// child is also the honest reproduction: a browser starting up on a pid whose
/// previous holder died mid-write.
#[test]
fn a_leftover_from_a_dead_process_does_not_stop_a_sidebar_gesture_reaching_the_disk() {
    let Ok(dir) = std::env::var(IN_THE_CHILD) else {
        // Named so it outlives the child by the borrow checker's word, not by
        // run_in_a_fresh_process happening to block until the child exits.
        let parent_owned = TempDir::new("dead-leftover");
        return run_in_a_fresh_process(
            "a_leftover_from_a_dead_process_does_not_stop_a_sidebar_gesture_reaching_the_disk",
            parent_owned.path(),
        );
    };
    let dir = std::path::PathBuf::from(dir);
    let path = dir.join("spaces.json");
    std::fs::write(&path, DOCUMENT).unwrap();

    // The trail a full disk leaves: one file per failed attempt, at the
    // counters this process is about to walk over. Nothing about the contents
    // says which - a killed writer leaves anything from nothing to the whole
    // file - so they are seeded as both.
    let pid = std::process::id();
    for n in 0..6 {
        let junk: &[u8] = if n % 2 == 0 { b"" } else { DOCUMENT.as_bytes() };
        std::fs::write(dir.join(format!(".atrium-{pid}-{n}.tmp")), junk).unwrap();
    }
    // And one from a process that is not this one, which stays whatever happens.
    let someone_else = dir.join(format!(".atrium-{}-0.tmp", u32::MAX));
    std::fs::write(&someone_else, b"not yours to remove").unwrap();

    let core = cache::Core::new();
    core.open(&path, INSTANT).unwrap();
    assert!(core.apply(|doc| mutate::set_pinned_collapsed(doc, 0, true)));
    assert!(core.save().unwrap(), "the gesture must reach the disk");

    let doc = reload(&path);
    assert!(
        doc.spaces[0].pinned_collapsed,
        "the save reported success but the file does not carry the gesture"
    );
    assert!(
        persist::backup_path(&path).exists(),
        "a leftover at the .bak's own name used to skip it and say nothing"
    );

    // The settings store shares this writer, and shares the counter with it.
    let settings_path = dir.join("settings.json");
    persist::save_settings(&settings::Settings::default(), &settings_path).unwrap();
    assert!(settings_path.exists());

    // Only the names it actually needed, and only ones it could account for.
    assert_eq!(
        std::fs::read_to_string(&someone_else).unwrap(),
        "not yours to remove",
        "the reclaim swept the directory instead of taking one name"
    );
    let left: Vec<String> = std::fs::read_dir(&dir)
        .unwrap()
        .filter_map(Result::ok)
        .map(|e| e.file_name().to_string_lossy().into_owned())
        .filter(|n| n.ends_with(".tmp"))
        .collect();
    assert!(
        left.len() < 7,
        "no seeded leftover was reclaimed, so this proved nothing: {left:?}"
    );
}

/// Re-runs one test in a process of its own, so the temp-name counter starts
/// where a freshly launched browser's does.
fn run_in_a_fresh_process(test: &str, dir: &std::path::Path) {
    let out = std::process::Command::new(std::env::current_exe().unwrap())
        .args(["--exact", test, "--nocapture", "--test-threads", "1"])
        .env(IN_THE_CHILD, dir)
        .output()
        .expect("the test binary must be runnable");
    assert!(
        out.status.success(),
        "{}{}",
        String::from_utf8_lossy(&out.stdout),
        String::from_utf8_lossy(&out.stderr)
    );
}
