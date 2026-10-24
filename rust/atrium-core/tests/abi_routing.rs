// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The routing write as C++ calls it, mirroring abi_settings.rs: read,
//! add a rule, write, read again the way the next launch does.

use atrium_core::atrium_core_save_routing;

mod common;
use common::{TempDir, c};

const EMPTY: &str = r#"{"schemaVersion":1,"rules":[]}"#;

fn read_and_query(path: &std::path::Path) -> String {
    common::read_and_query("routing", path)
}

fn save(path: &std::path::Path, json: &str) -> i32 {
    common::save(atrium_core_save_routing, path, json)
}

#[test]
fn a_rule_added_in_the_pane_crosses_the_abi_and_the_next_read_finds_it() {
    let dir = TempDir::new("routing-round-trip");
    let path = dir.join("routing.json");
    std::fs::write(&path, EMPTY).unwrap();

    let mut file: serde_json::Value = serde_json::from_str(&read_and_query(&path)).unwrap();
    assert_eq!(file["rules"], serde_json::json!([]));

    file["rules"] = serde_json::json!([{
        "id": "r1", "enabled": true, "matchType": "contains",
        "pattern": "corp.example", "targetSpaceID": "A"
    }]);
    assert_eq!(save(&path, &file.to_string()), 1, "the file is written");

    let after: serde_json::Value = serde_json::from_str(&read_and_query(&path)).unwrap();
    assert_eq!(
        after["rules"][0]["pattern"],
        serde_json::json!("corp.example")
    );
}

/// R1.2: a routing.json this build cannot read is declined and left alone.
#[test]
fn a_routing_file_this_build_cannot_read_is_declined_and_left_alone() {
    let dir = TempDir::new("routing-declined");
    let path = dir.join("routing.json");
    let future = r#"{"schemaVersion":999,"rules":[]}"#;
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
    let dir = TempDir::new("routing-refused");
    let path = dir.join("routing.json");
    std::fs::write(&path, EMPTY).unwrap();

    assert_eq!(save(&path, "not json at all"), 0);
    assert_eq!(std::fs::read_to_string(&path).unwrap(), EMPTY);
}

/// An argument that names nothing writes nothing.
#[test]
fn an_argument_that_names_nothing_writes_nothing() {
    let payload = c(EMPTY);
    let path = c("/tmp/atrium-abi-routing-unused/routing.json");
    let empty = c("");

    assert_eq!(
        unsafe { atrium_core_save_routing(std::ptr::null(), payload.as_ptr()) },
        0
    );
    assert_eq!(
        unsafe { atrium_core_save_routing(path.as_ptr(), std::ptr::null()) },
        0
    );
    assert_eq!(
        unsafe { atrium_core_save_routing(empty.as_ptr(), payload.as_ptr()) },
        0
    );
}

/// A write that cannot land reports the failure rather than the decline.
#[test]
fn a_write_that_cannot_land_says_so() {
    let dir = TempDir::new("routing-blocked");
    let blocker = dir.join("blocker");
    std::fs::write(&blocker, "not a directory").unwrap();

    assert_eq!(save(&blocker.join("routing.json"), EMPTY), -1);
}
