// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The favourite keep-alive keys: the ladder, the default, and the answer C++
//! reads when it parks a page.

use std::ffi::CString;

use atrium_core::atrium_core_favorite_keep_alive_seconds;
use atrium_core::lifecycle::favorite_keep_alive_threshold_seconds;
use atrium_core::settings;

const MINUTE: f64 = 60.0;

/// The four keys every settings.json carries. Every later key is optional.
const MINIMAL: &str = r#"{"schemaVersion":1,"appIconVariant":"auto",
"animationsEnabled":true,"startupBehavior":"restoreLastActive"}"#;

fn with(extra: &str) -> String {
    format!(
        r#"{{"schemaVersion":1,"appIconVariant":"auto","animationsEnabled":true,"startupBehavior":"restoreLastActive",{extra}}}"#
    )
}

fn seconds_for(json: &str) -> f64 {
    let json = CString::new(json).unwrap();
    // SAFETY: a NUL-terminated string that outlives the call.
    unsafe { atrium_core_favorite_keep_alive_seconds(json.as_ptr()) }
}

#[test]
fn the_keep_alive_ladder_answers_in_seconds() {
    assert_eq!(
        favorite_keep_alive_threshold_seconds("oneMinute"),
        Some(MINUTE)
    );
    assert_eq!(
        favorite_keep_alive_threshold_seconds("fiveMinutes"),
        Some(5.0 * MINUTE)
    );
    assert_eq!(
        favorite_keep_alive_threshold_seconds("fifteenMinutes"),
        Some(15.0 * MINUTE)
    );
    assert_eq!(
        favorite_keep_alive_threshold_seconds("thirtyMinutes"),
        Some(30.0 * MINUTE)
    );
    assert_eq!(
        favorite_keep_alive_threshold_seconds("oneHour"),
        Some(60.0 * MINUTE)
    );
    assert_eq!(favorite_keep_alive_threshold_seconds("twoHours"), None);
}

#[test]
fn a_file_without_the_keys_keeps_favourites_for_five_minutes() {
    let s = settings::load(MINIMAL);
    assert!(s.favorite_keep_alive_enabled);
    assert_eq!(s.favorite_keep_alive_threshold, "fiveMinutes");
}

#[test]
fn an_unknown_rung_falls_back_to_five_minutes() {
    let s = settings::load(&with(r#""favoriteKeepAliveThreshold":"twoWeeks""#));
    assert_eq!(s.favorite_keep_alive_threshold, "fiveMinutes");
}

#[test]
fn the_abi_answers_the_chosen_rung_or_off() {
    assert_eq!(seconds_for(MINIMAL), 5.0 * MINUTE);
    assert_eq!(
        seconds_for(&with(r#""favoriteKeepAliveThreshold":"oneHour""#)),
        60.0 * MINUTE
    );
    assert_eq!(
        seconds_for(&with(r#""favoriteKeepAliveEnabled":false"#)),
        -1.0
    );
    assert_eq!(
        seconds_for("not json"),
        5.0 * MINUTE,
        "an unreadable object means the defaults"
    );
    // SAFETY: NULL is part of the contract.
    assert_eq!(
        unsafe { atrium_core_favorite_keep_alive_seconds(std::ptr::null()) },
        -1.0
    );
}
