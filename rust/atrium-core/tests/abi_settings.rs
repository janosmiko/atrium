// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! the settings write as C++ calls it.
//!
//! Settings has no in-memory document behind it. The read path hands the file's
//! contents to `atrium_core_query("settings", ...)` on every call, so the write
//! is its mirror - the whole object back, plus the path - and the walk below is
//! the round trip the General pane's "Warn before quitting" makes: read, toggle,
//! write, read again the way the next launch does.
//!
//! What the return codes mean is asserted here rather than only in the header,
//! because the caller logs a different line for each and the difference between
//! "the write failed" and "this build declined to write" is the difference
//! between a retry and leaving the user's file alone.

use atrium_core::{atrium_core_save_settings, atrium_core_settings_writable};

mod common;
use common::{TempDir, c};

/// The four keys a settings.json must carry, and nothing else. Everything added
/// after v1 is optional, so this is what an old file looks like and what the
/// loader fills in from.
const MINIMAL: &str = r#"{"schemaVersion":1,"appIconVariant":"crimson-ink",
"animationsEnabled":true,"startupBehavior":"restoreLastActive"}"#;

/// The same file plus a key this build has no field for, spelled with more
/// digits than an f64 round trip survives. A save that goes through the writer
/// keeps both; one that goes around it loses the key or moves the number.
const LATER_BUILD: &str = r#"{"schemaVersion":1,"appIconVariant":"crimson-ink",
"animationsEnabled":true,"startupBehavior":"restoreLastActive",
"confirmToQuitEnabled":false,
"laterBuildOnly":{"kept": 0.1234567890123456789}}"#;

fn read_and_query(path: &std::path::Path) -> String {
    common::read_and_query("settings", path)
}

fn save(path: &std::path::Path, json: &str) -> i32 {
    common::save(atrium_core_save_settings, path, json)
}

/// The round trip, minus the relaunch: a rendered value, the
/// toggle the surface sends, and what the next read finds.
#[test]
fn toggling_warn_before_quitting_crosses_the_abi_and_the_next_read_finds_it() {
    let dir = TempDir::new("round-trip");
    let path = dir.join("settings.json");
    std::fs::write(&path, LATER_BUILD).unwrap();

    let mut settings: serde_json::Value = serde_json::from_str(&read_and_query(&path)).unwrap();
    assert_eq!(settings["confirmToQuitEnabled"], serde_json::json!(false));

    // The whole object back with one field changed, which is the only shape the
    // surface can send: it has no field-level protocol.
    settings["confirmToQuitEnabled"] = serde_json::json!(true);
    assert_eq!(save(&path, &settings.to_string()), 1, "the file is written");

    let after: serde_json::Value = serde_json::from_str(&read_and_query(&path)).unwrap();
    assert_eq!(after["confirmToQuitEnabled"], serde_json::json!(true));

    // The export goes through `persist::save_settings` rather than around it,
    // so the key this build has no field for is still there, still spelled as
    // the file spelled it.
    let bytes = std::fs::read_to_string(&path).unwrap();
    assert!(
        bytes.contains(r#""laterBuildOnly":{"kept":0.1234567890123456789}"#),
        "{bytes}"
    );
}

/// Everything a newer build's settings.json can carry that this one has no
/// field for, through the export, in one save.
///
/// This is the criterion the export was held back for. The writer's own tests
/// cover each of these shapes; what is asserted here is that the ABI reaches
/// the writer rather than going around it, because that is the only way any of
/// it could be lost once C++ can call this.
///
/// The last assertion is the one that matters most. A preserved `f64::MAX` used
/// to be re-emitted as a 309-digit literal that serde_json then refused, which
/// turned a partial loss into a file that never loaded again.
#[test]
fn a_newer_builds_settings_come_through_the_export_unchanged() {
    let dir = TempDir::new("newer-build");
    let path = dir.join("settings.json");
    let file = r#"{"schemaVersion":1,"appIconVariant":"crimson-ink",
"animationsEnabled":true,"startupBehavior":"someLaterMode",
"confirmToQuitEnabled":false,"glassOpacity":5.0,
"laterNested":{"kept": 0.1234567890123456789},
"laterHuge": 18446744073709551615,
"laterTiny": 1e-400,
"laterMax": 1.7976931348623157e308}"#;
    std::fs::write(&path, file).unwrap();

    let mut settings: serde_json::Value = serde_json::from_str(&read_and_query(&path)).unwrap();
    settings["confirmToQuitEnabled"] = serde_json::json!(true);
    assert_eq!(save(&path, &settings.to_string()), 1);

    let bytes = std::fs::read_to_string(&path).unwrap();
    for spelling in [
        // Keys this build has no field for, spelled as the file spelled them.
        r#""laterNested":{"kept":0.1234567890123456789}"#,
        r#""laterHuge":18446744073709551615"#,
        r#""laterTiny":1e-400"#,
        r#""laterMax":1.7976931348623157e308"#,
        // Keys this build DOES declare, whose values it cannot represent: an
        // enum it does not know and an opacity outside its own range. Both are
        // replaced in memory, and neither may be written back that way.
        r#""startupBehavior":"someLaterMode""#,
        r#""glassOpacity":5.0"#,
    ] {
        assert!(bytes.contains(spelling), "{spelling} is gone from {bytes}");
    }

    // The edit still lands, and the file still loads - which is what the
    // 309-digit f64::MAX literal used to take away for good.
    let after: serde_json::Value = serde_json::from_str(&read_and_query(&path)).unwrap();
    assert_eq!(after["confirmToQuitEnabled"], serde_json::json!(true));
}

/// R1.2 across the ABI. A file this build cannot read is declined with its own
/// code, because the caller has nothing to retry - the file stays unwritable
/// until it is moved aside, and that is worth a different log line from a disk
/// error.
#[test]
fn a_settings_file_this_build_cannot_read_is_declined_and_left_alone() {
    let dir = TempDir::new("declined");
    let path = dir.join("settings.json");
    let future = r#"{"schemaVersion":999,"appIconVariant":"crimson-ink",
"animationsEnabled":true,"startupBehavior":"restoreLastActive"}"#;
    std::fs::write(&path, future).unwrap();

    assert_eq!(save(&path, &common::queried("settings", MINIMAL)), -2);
    assert_eq!(
        std::fs::read_to_string(&path).unwrap(),
        future,
        "a build that cannot read the file must not replace it"
    );
}

/// A payload this build cannot read is a caller bug, and the answer to it is to
/// write nothing.
///
/// The dangerous shape is the partial one. Every key past the first four is
/// optional, so `{"confirmToQuitEnabled":false}` alone parses happily and means
/// "every other field at its default" - which would reset the user's settings on
/// behalf of a surface that only meant to send one row.
#[test]
fn a_partial_settings_object_is_refused_rather_than_completed_from_defaults() {
    let dir = TempDir::new("partial");
    let path = dir.join("settings.json");
    std::fs::write(&path, LATER_BUILD).unwrap();
    let before = std::fs::read_to_string(&path).unwrap();

    assert_eq!(save(&path, r#"{"confirmToQuitEnabled":true}"#), 0);
    assert_eq!(save(&path, "not json at all"), 0);
    assert_eq!(std::fs::read_to_string(&path).unwrap(), before);
}

/// An argument that names nothing writes nothing. `SettingsFilePath` hands back
/// an empty path when it cannot work out where the file is, so the empty case
/// is reachable rather than theoretical, and reporting it as a failed write
/// would send the caller looking at the disk.
#[test]
fn an_argument_that_names_nothing_writes_nothing() {
    let payload = c(&common::queried("settings", MINIMAL));
    let path = c("/tmp/atrium-abi-settings-unused/settings.json");
    let empty = c("");

    assert_eq!(
        unsafe { atrium_core_save_settings(std::ptr::null(), payload.as_ptr()) },
        0
    );
    assert_eq!(
        unsafe { atrium_core_save_settings(path.as_ptr(), std::ptr::null()) },
        0
    );
    assert_eq!(
        unsafe { atrium_core_save_settings(empty.as_ptr(), payload.as_ptr()) },
        0
    );
}

/// A write that cannot land reports the failure rather than the decline. The
/// caller's last edit is in the surface only, which is worth saying.
///
/// The parent is a FILE rather than a missing directory, because a missing
/// directory is not a failure: `write_secure` creates it, which is what lets a
/// fresh profile's first save work at all.
#[test]
fn a_write_that_cannot_land_says_so() {
    let dir = TempDir::new("blocked");
    let blocker = dir.join("blocker");
    std::fs::write(&blocker, "not a directory").unwrap();

    assert_eq!(
        save(
            &blocker.join("settings.json"),
            &common::queried("settings", MINIMAL)
        ),
        -1
    );
}

/// The import asks before it adds engines, so a file that would decline the
/// save costs the engines and leaves no profile naming an engine it lost.
#[test]
fn the_writable_check_answers_as_the_save_would_and_writes_nothing() {
    let dir = TempDir::new("writable");
    let path = dir.join("settings.json");
    let check = |path: &std::path::Path| {
        let path = c(path.to_str().unwrap());
        unsafe { atrium_core_settings_writable(path.as_ptr()) }
    };

    assert_eq!(check(&path), 1, "a fresh profile's first save creates it");
    assert!(!path.exists());
    std::fs::write(&path, MINIMAL).unwrap();
    assert_eq!(check(&path), 1);
    let future = r#"{"schemaVersion":999,"appIconVariant":"crimson-ink",
"animationsEnabled":true,"startupBehavior":"restoreLastActive"}"#;
    std::fs::write(&path, future).unwrap();
    assert_eq!(check(&path), -2);
    assert_eq!(std::fs::read_to_string(&path).unwrap(), future);
    let empty = c("");
    assert_eq!(unsafe { atrium_core_settings_writable(empty.as_ptr()) }, 0);
    assert_eq!(
        unsafe { atrium_core_settings_writable(std::ptr::null()) },
        0
    );
}
