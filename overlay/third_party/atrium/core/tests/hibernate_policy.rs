// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! the per-profile hibernation policy - Chromium runs the idle
//! timer and the exemptions itself, this only resolves on/after-how-long.

use atrium_core::lifecycle::{hibernate_policy, memory_saver_threshold_seconds};
use atrium_core::spaces::Profile;

const HOUR: f64 = 3600.0;

fn profile_with(hibernate: Option<&str>, hib_threshold: Option<&str>) -> Profile {
    Profile {
        id: "p1".into(),
        name: "Work".into(),
        search_engine: None,
        hibernate: hibernate.map(String::from),
        hib_threshold: hib_threshold.map(String::from),
    }
}

#[test]
fn the_memory_saver_ladder_answers_in_seconds() {
    assert_eq!(
        memory_saver_threshold_seconds("fifteenMinutes"),
        Some(15.0 * 60.0)
    );
    assert_eq!(
        memory_saver_threshold_seconds("thirtyMinutes"),
        Some(30.0 * 60.0)
    );
    assert_eq!(memory_saver_threshold_seconds("oneHour"), Some(HOUR));
    assert_eq!(memory_saver_threshold_seconds("twoHours"), Some(2.0 * HOUR));
    assert_eq!(
        memory_saver_threshold_seconds("fourHours"),
        Some(4.0 * HOUR)
    );
    assert_eq!(memory_saver_threshold_seconds("whenever"), None);
}

/// Follow General with the toggle off leaves the profile awake.
#[test]
fn follow_general_off_is_awake() {
    let profile = profile_with(None, None);
    assert_eq!(hibernate_policy(Some(&profile), false, "twoHours"), None);
}

/// Follow General with the toggle on uses the general timeout.
#[test]
fn follow_general_on_uses_the_general_timeout() {
    let profile = profile_with(None, None);
    assert_eq!(
        hibernate_policy(Some(&profile), true, "twoHours"),
        Some(2.0 * HOUR)
    );
}

/// Never keeps a profile awake whatever the general toggle says.
#[test]
fn never_wins_over_the_general_toggle() {
    let profile = profile_with(Some("never"), None);
    assert_eq!(
        hibernate_policy(Some(&profile), true, "fifteenMinutes"),
        None
    );
}

/// Always hibernates a profile even if the general toggle is off.
#[test]
fn always_wins_over_the_general_toggle() {
    let profile = profile_with(Some("always"), None);
    assert_eq!(
        hibernate_policy(Some(&profile), false, "twoHours"),
        Some(2.0 * HOUR)
    );
}

/// A profile's own timeout overrides the general one.
#[test]
fn a_profiles_own_timeout_overrides_the_general_one() {
    let profile = profile_with(Some("always"), Some("fifteenMinutes"));
    assert_eq!(
        hibernate_policy(Some(&profile), false, "fourHours"),
        Some(15.0 * 60.0)
    );
}

/// An unrecognised per-profile timeout falls back to the general one rather
/// than disabling hibernation outright.
#[test]
fn an_unrecognised_own_timeout_falls_back_to_general() {
    let profile = profile_with(Some("always"), Some("whenever"));
    assert_eq!(
        hibernate_policy(Some(&profile), false, "twoHours"),
        Some(2.0 * HOUR)
    );
}

/// An unrecognised mode string is Follow General, not a crash and not Never.
#[test]
fn an_unrecognised_mode_is_follow_general() {
    let profile = profile_with(Some("sometimes"), None);
    assert_eq!(
        hibernate_policy(Some(&profile), true, "oneHour"),
        Some(HOUR)
    );
    assert_eq!(hibernate_policy(Some(&profile), false, "oneHour"), None);
}

/// A space whose profile has no record at all is Follow General, not Never.
/// "We cannot say" and "keep this awake" are different answers.
#[test]
fn no_profile_record_is_follow_general() {
    assert_eq!(hibernate_policy(None, true, "oneHour"), Some(HOUR));
    assert_eq!(hibernate_policy(None, false, "oneHour"), None);
}
