// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The hotkeys write as C++ calls it, mirroring abi_routing.rs: read,
//! rebind an action, write, read again the way the next launch does.

use atrium_core::atrium_core_save_hotkeys;

mod common;
use common::{TempDir, c};

const EMPTY: &str = r#"{"schemaVersion":2,"actions":{}}"#;

fn read_and_query(path: &std::path::Path) -> String {
    common::read_and_query("hotkeys", path)
}

fn save(path: &std::path::Path, json: &str) -> i32 {
    common::save(atrium_core_save_hotkeys, path, json)
}

#[test]
fn a_rebind_in_the_pane_crosses_the_abi_and_the_next_read_finds_it() {
    let dir = TempDir::new("hotkeys-round-trip");
    let path = dir.join("hotkeys.json");
    std::fs::write(&path, EMPTY).unwrap();

    let mut file: serde_json::Value = serde_json::from_str(&read_and_query(&path)).unwrap();
    assert_eq!(
        file["actions"]["toggleSidebar"],
        serde_json::json!(["cmd+s"])
    );

    file["actions"]["toggleSidebar"] = serde_json::json!(["cmd+shift+s"]);
    assert_eq!(save(&path, &file.to_string()), 1, "the file is written");

    let after: serde_json::Value = serde_json::from_str(&read_and_query(&path)).unwrap();
    assert_eq!(
        after["actions"]["toggleSidebar"],
        serde_json::json!(["cmd+shift+s"])
    );
}

/// An explicit empty list is a deliberate unbind, and it round-trips
/// through the write as one - not as the action reverting to its default.
#[test]
fn an_explicit_unbind_survives_the_write() {
    let dir = TempDir::new("hotkeys-unbind");
    let path = dir.join("hotkeys.json");
    std::fs::write(&path, EMPTY).unwrap();

    let mut file: serde_json::Value = serde_json::from_str(&read_and_query(&path)).unwrap();
    file["actions"]["toggleSidebar"] = serde_json::json!([]);
    assert_eq!(save(&path, &file.to_string()), 1);

    let after: serde_json::Value = serde_json::from_str(&read_and_query(&path)).unwrap();
    assert_eq!(after["actions"]["toggleSidebar"], serde_json::json!([]));
}

/// R1.2: a hotkeys.json this build cannot read is declined and left alone.
#[test]
fn a_hotkeys_file_this_build_cannot_read_is_declined_and_left_alone() {
    let dir = TempDir::new("hotkeys-declined");
    let path = dir.join("hotkeys.json");
    let future = r#"{"schemaVersion":999,"actions":{}}"#;
    std::fs::write(&path, future).unwrap();

    assert_eq!(save(&path, EMPTY), -2);
    assert_eq!(
        std::fs::read_to_string(&path).unwrap(),
        future,
        "a build that cannot read the file must not replace it"
    );
}

/// A payload this build cannot parse writes nothing.
#[test]
fn an_unparsable_payload_is_refused_rather_than_written() {
    let dir = TempDir::new("hotkeys-refused");
    let path = dir.join("hotkeys.json");
    std::fs::write(&path, EMPTY).unwrap();

    assert_eq!(save(&path, "not json at all"), 0);
    assert_eq!(std::fs::read_to_string(&path).unwrap(), EMPTY);
}

/// An argument that names nothing writes nothing.
#[test]
fn an_argument_that_names_nothing_writes_nothing() {
    let payload = c(EMPTY);
    let path = c("/tmp/atrium-abi-hotkeys-unused/hotkeys.json");
    let empty = c("");

    assert_eq!(
        unsafe { atrium_core_save_hotkeys(std::ptr::null(), payload.as_ptr()) },
        0
    );
    assert_eq!(
        unsafe { atrium_core_save_hotkeys(path.as_ptr(), std::ptr::null()) },
        0
    );
    assert_eq!(
        unsafe { atrium_core_save_hotkeys(empty.as_ptr(), payload.as_ptr()) },
        0
    );
}

/// A write that cannot land reports the failure rather than the decline.
#[test]
fn a_write_that_cannot_land_says_so() {
    let dir = TempDir::new("hotkeys-blocked");
    let blocker = dir.join("blocker");
    std::fs::write(&blocker, "not a directory").unwrap();

    assert_eq!(save(&blocker.join("hotkeys.json"), EMPTY), -1);
}
