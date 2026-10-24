// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! one way to read a fixture, and it fails when the file is missing.
//!
//! Every fixture test in this crate used to return early when the read failed,
//! and an early return is reported as a pass. Run from the Chromium copy of the
//! crate - where `../../fixtures` and `../../build/generated` resolve to
//! nothing - whole files went green while asserting nothing.
//!
//! Lives in a subdirectory so cargo does not compile it as its own test binary.

// Each test binary compiles its own copy, so any helper one binary skips
// would warn there. Allowed, or every binary must use every helper.
#![allow(dead_code)]

use std::ffi::{CString, c_char};
use std::path::{Path, PathBuf};

/// A path under the repo root, which is two levels above the crate.
pub fn repo_path(relative: &str) -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .join("../..")
        .join(relative)
}

/// Reads a fixture, or fails the test naming the path.
pub fn read_fixture(path: &Path) -> String {
    std::fs::read_to_string(path).unwrap_or_else(|err| {
        panic!(
            "no fixture at {}: {err}\n\
             A fixture test that cannot read its fixture is a FAILURE, not a \
             skip. Run the crate's tests from the Atrium repo, which carries \
             fixtures/ next to rust/.",
            path.display()
        )
    })
}

#[path = "../../src/test_temp_dir.rs"]
mod test_temp_dir;
pub use test_temp_dir::TempDir;

/// Serialises the tests that drive the PROCESS-WIDE core through the ABI.
///
/// `atrium_core_open` and friends act on one global document, so two tests
/// touching it at once make each other flaky. Each test BINARY needs its own
/// guard - cargo runs them as separate processes, so a mutex here cannot
/// serialise against one in another binary and does not need to. That is why
/// `src/lib.rs` keeps a copy for the unit tests rather than importing this one.
pub fn abi_serial() -> std::sync::MutexGuard<'static, ()> {
    static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
    LOCK.lock().unwrap_or_else(|e| e.into_inner())
}

/// Takes ownership of a string the ABI returned, freeing it through the ABI.
///
/// `None` for a null result, which is how every export here reports "no
/// answer". The manual form - `CStr::from_ptr`, `to_str`, then
/// `atrium_core_string_free` - was written out fifty times, and each one is a
/// chance to leak or to forget the free. These tests are the only exercise the
/// free path gets.
///
/// # Safety
/// `raw` must be null or a string this library returned and has not yet freed.
pub unsafe fn take_string(raw: *mut std::ffi::c_char) -> Option<String> {
    if raw.is_null() {
        return None;
    }
    // SAFETY: the caller promises the pointer came from this library.
    let owned = unsafe { std::ffi::CStr::from_ptr(raw) }
        .to_str()
        .expect("the ABI returns UTF-8")
        .to_string();
    unsafe { atrium_core::atrium_core_string_free(raw) };
    Some(owned)
}

pub fn c(text: &str) -> CString {
    CString::new(text).unwrap()
}

/// One Arc fixture folder as the files map C++ builds: file name to contents.
/// A file the folder lacks is left out, as C++ leaves out a missing file.
pub fn arc_fixture_files(dir: &str) -> std::collections::BTreeMap<String, String> {
    let root = repo_path(&format!("fixtures/import/arc/{dir}"));
    [
        ("StorableSidebar.json", "StorableSidebar.json"),
        ("StorableLinkRouting.json", "StorableLinkRouting.json"),
        ("Local State", "User Data/Local State"),
    ]
    .into_iter()
    .filter(|(_, rel)| root.join(rel).exists())
    .map(|(key, rel)| (key.to_string(), read_fixture(&root.join(rel))))
    .collect()
}

/// One Zen fixture root as the files map C++ builds. C++ decodes
/// zen-sessions.jsonlz4 first, so the map holds the fixture's .json twin
/// under that name.
pub fn zen_fixture_files(dir: &str) -> std::collections::BTreeMap<String, String> {
    let root = repo_path(&format!("fixtures/import/zen/{dir}"));
    let mut files = std::collections::BTreeMap::new();
    files.insert("profiles.ini".to_string(), read_fixture(&root.join("profiles.ini")));
    for entry in std::fs::read_dir(root.join("Profiles")).expect("the fixture has a Profiles folder") {
        let folder = entry.unwrap().path();
        let prefix = format!("Profiles/{}", folder.file_name().unwrap().to_str().unwrap());
        files.insert(format!("{prefix}/zen-sessions.jsonlz4"), read_fixture(&folder.join("zen-sessions.json")));
        if folder.join("containers.json").exists() {
            files.insert(format!("{prefix}/containers.json"), read_fixture(&folder.join("containers.json")));
        }
    }
    files
}

/// One Chrome fixture root as the files map C++ builds. C++ runs the keywords
/// query on each Web Data file, so the map holds the fixture's keywords.json
/// twin under `<folder>/Web Data#keywords`.
pub fn chrome_fixture_files(dir: &str) -> std::collections::BTreeMap<String, String> {
    let root = repo_path(&format!("fixtures/import/chrome/{dir}"));
    let mut files = std::collections::BTreeMap::new();
    files.insert("Local State".to_string(), read_fixture(&root.join("Local State")));
    for entry in std::fs::read_dir(&root).expect("the fixture root exists") {
        let folder = entry.unwrap().path();
        if !folder.is_dir() {
            continue;
        }
        let name = folder.file_name().unwrap().to_str().unwrap().to_string();
        for (file, key) in [
            ("Bookmarks", "Bookmarks"),
            ("AccountBookmarks", "AccountBookmarks"),
            ("Preferences", "Preferences"),
            ("keywords.json", "Web Data#keywords"),
        ] {
            if folder.join(file).exists() {
                files.insert(format!("{name}/{key}"), read_fixture(&folder.join(file)));
            }
        }
    }
    files
}

/// One Firefox fixture root as the files map C++ builds. C++ runs the
/// bookmarks query on places.sqlite and decodes the session and search files,
/// so the map holds each file's .json twin under the name C++ uses.
pub fn firefox_fixture_files(dir: &str) -> std::collections::BTreeMap<String, String> {
    let root = repo_path(&format!("fixtures/import/firefox/{dir}"));
    let mut files = std::collections::BTreeMap::new();
    files.insert("profiles.ini".to_string(), read_fixture(&root.join("profiles.ini")));
    for entry in std::fs::read_dir(root.join("Profiles")).expect("the fixture has a Profiles folder") {
        let folder = entry.unwrap().path();
        let prefix = format!("Profiles/{}", folder.file_name().unwrap().to_str().unwrap());
        for (file, key) in [
            ("bookmarks.json", "places.sqlite#bookmarks"),
            ("sessionstore-backups/recovery.json", "sessionstore.jsonlz4"),
            ("search.json", "search.json.mozlz4"),
        ] {
            if folder.join(file).exists() {
                files.insert(format!("{prefix}/{key}"), read_fixture(&folder.join(file)));
            }
        }
    }
    files
}

/// Queries one small store by name - "settings", "routing", "hotkeys" - the
/// way its own loader would.
pub fn queried(store: &str, contents: &str) -> String {
    let (store_c, contents_c) = (c(store), c(contents));
    let out = unsafe { atrium_core::atrium_core_query(store_c.as_ptr(), contents_c.as_ptr(), 0.0) };
    unsafe { take_string(out) }.unwrap_or_else(|| panic!("the {store} store must load"))
}

pub fn read_and_query(store: &str, path: &Path) -> String {
    queried(store, &std::fs::read_to_string(path).unwrap())
}

/// Calls the store's own `atrium_core_save_*` export with `path` and `json`.
pub fn save(
    save_fn: unsafe extern "C" fn(*const c_char, *const c_char) -> i32,
    path: &Path,
    json: &str,
) -> i32 {
    let (path_c, json_c) = (c(path.to_str().unwrap()), c(json));
    unsafe { save_fn(path_c.as_ptr(), json_c.as_ptr()) }
}
