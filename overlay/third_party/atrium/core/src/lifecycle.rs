// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Idle-based tab lifecycle: auto-archive and hibernation policy.
//!
//! `archive_due` is selection only, kept read-only so a caller can sample it
//! for a count on every tick. `mutate::sweep_archive` does the actual move.
//!
//! Hibernation resolves a policy, not a sweep list: Chromium's
//! `MemorySaverModePolicy` already runs the per-tab idle timer and already
//! exempts the visible and audible tab. It has no notion of a Space, so this
//! only answers "is hibernation on for this space, and after how long."

use crate::choices;
use crate::spaces::{Document, Profile, RowToken, Space, SwiftDate};

const HOUR: SwiftDate = 3600.0;
const DAY: SwiftDate = 24.0 * HOUR;

/// A rung of `SettingsStore`'s archive ladder as a duration, or `None` for
/// `never` - and for anything unrecognised, because a hand-edited settings
/// file must not archive on a guess. Matched by position against
/// `choices::ARCHIVE_THRESHOLDS`, whose trailing `"never"` this excludes.
pub fn threshold_seconds(threshold: &str) -> Option<SwiftDate> {
    let durations = [12.0 * HOUR, 24.0 * HOUR, 7.0 * DAY, 30.0 * DAY];
    let position = choices::ARCHIVE_THRESHOLDS[..durations.len()]
        .iter()
        .position(|&rung| rung == threshold)?;
    Some(durations[position])
}

/// `"never"` is a recognised rung, not garbage, so it is not replaced -
/// mirrors `hibernate_policy`'s `hib_threshold` fallback.
pub fn archive_threshold_for<'a>(space: &'a Space, general_threshold: &'a str) -> &'a str {
    match space.archive_ttl.as_deref() {
        Some(t) if t == "never" || threshold_seconds(t).is_some() => t,
        _ => general_threshold,
    }
}

/// The rows a sweep at `now` would archive, as their [`RowToken`]s.
///
/// Pinned tiles never appear: they live in `Space::pinned_tabs`, which is not
/// walked. Foldered tabs are skipped explicitly - both exemptions are intentional.
///
/// A tab ages from its `lastActiveAt`, floored at the document's
/// `archiveEpoch`. Without the floor, a row that never recorded a visit ages
/// from zero and a migration archives the whole sidebar on the first launch.
pub fn archive_due(doc: &Document, general_threshold: &str, now: SwiftDate) -> Vec<RowToken> {
    let epoch = doc.app_state.archive_epoch.unwrap_or(now);

    doc.spaces
        .iter()
        .flat_map(|space| {
            let ttl = threshold_seconds(archive_threshold_for(space, general_threshold));
            space.tabs.iter().filter_map(move |tab| {
                let ttl = ttl?;
                if tab.group_id.is_some() {
                    return None;
                }
                let idle_since = tab.last_active_at.unwrap_or(epoch).max(epoch);
                (now - idle_since >= ttl).then_some(tab.token)
            })
        })
        .collect()
}

/// A rung of `SettingsStore`'s `memorySaverThreshold` ladder as a duration,
/// or `None` for anything unrecognised. Matched by position against
/// `choices::MEMORY_SAVER_THRESHOLDS`.
pub fn memory_saver_threshold_seconds(threshold: &str) -> Option<SwiftDate> {
    let durations = [15.0 * 60.0, 30.0 * 60.0, HOUR, 2.0 * HOUR, 4.0 * HOUR];
    let position = choices::MEMORY_SAVER_THRESHOLDS
        .iter()
        .position(|&rung| rung == threshold)?;
    Some(durations[position])
}

/// A rung of `favoriteKeepAliveThreshold` as a duration, or `None` for
/// anything unrecognised. Matched by position against
/// `choices::FAVORITE_KEEP_ALIVE_THRESHOLDS`.
pub fn favorite_keep_alive_threshold_seconds(threshold: &str) -> Option<SwiftDate> {
    let durations = [60.0, 5.0 * 60.0, 15.0 * 60.0, 30.0 * 60.0, HOUR];
    let position = choices::FAVORITE_KEEP_ALIVE_THRESHOLDS
        .iter()
        .position(|&rung| rung == threshold)?;
    Some(durations[position])
}

/// The space owning `token`, checking both a space's regular and pinned rows -
/// one half of the bridge, the other half being `tabs::Bindings`
/// mapping a live `WebContents` to this same token.
pub fn space_for_token(doc: &Document, token: RowToken) -> Option<&Space> {
    doc.spaces.iter().find(|space| {
        space.tabs.iter().any(|t| t.token == token)
            || space.pinned_tabs.iter().any(|t| t.token == token)
    })
}

/// The profile owning `space`, or `None` when the document has no record for
/// its `profileID` - a hand-edited file, or an id the safety rule refuses.
pub fn profile_for_space<'a>(doc: &'a Document, space: &Space) -> Option<&'a Profile> {
    doc.profiles.iter().find(|p| p.id == space.profile_id)
}

/// Whether hibernation is on and after how long. `None` profile is Follow
/// General, not Never - "we cannot say" must not read as "keep this awake".
pub fn hibernate_policy(
    profile: Option<&Profile>,
    memory_saver_enabled: bool,
    memory_saver_threshold: &str,
) -> Option<SwiftDate> {
    let on = match profile.and_then(|p| p.hibernate.as_deref()) {
        Some("never") => false,
        Some("always") => true,
        _ => memory_saver_enabled,
    };
    if !on {
        return None;
    }
    let threshold = profile
        .and_then(|p| p.hib_threshold.as_deref())
        .filter(|t| memory_saver_threshold_seconds(t).is_some())
        .unwrap_or(memory_saver_threshold);
    memory_saver_threshold_seconds(threshold)
}
