// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The closed-set settings, as one list each rather than a hand-typed list at
//! every call site.
//!
//! `mutate::set_profile_hibernate` and `lifecycle::hibernate_policy` used to
//! type the same two spellings independently: add a third mode to one and not
//! the other and the writer stores it while the policy silently reads it as
//! Follow General. The archive TTL had the same split. Each set
//! below is declared once, so the writer's `allowed.contains` and the
//! reader's `match` cannot drift apart.

pub const APP_ICON_VARIANTS: &[&str] = &[
    "auto",
    "crimson-accent",
    "vermilion-accent",
    "magenta-accent",
    "ultraviolet-accent",
    "cyan-accent",
    "lime-accent",
    "amber-accent",
    "tokyo-night-accent",
    "catppuccin-accent",
    "gruvbox-accent",
    "kanagawa-accent",
    "synthwave-everything-accent",
    "bluloco-accent",
    "monokai-pro-accent",
    "dracula-accent",
    "nord-accent",
    "rose-pine-accent",
    "everforest-accent",
    "crimson-ink",
    "crimson-paper",
    "solarized-accent",
];

pub const SIDEBAR_POSITIONS: &[&str] = &["left", "right"];

pub const SCROLL_OVERRIDES: &[&str] = &["auto", "standard", "inverted"];

/// `"never"` is a recognised rung rather than an absent value, so it stays in
/// this list rather than becoming an `Option`.
pub const ARCHIVE_THRESHOLDS: &[&str] = &[
    "twelveHours",
    "twentyFourHours",
    "sevenDays",
    "thirtyDays",
    "never",
];

/// A duration and nothing else - the retired `"never"` spelling answered a
/// second question that `memorySaverEnabled` answers now, so it is
/// deliberately not in this list.
pub const MEMORY_SAVER_THRESHOLDS: &[&str] = &[
    "fifteenMinutes",
    "thirtyMinutes",
    "oneHour",
    "twoHours",
    "fourHours",
];

/// How long a closed favourite's page stays loaded.
pub const FAVORITE_KEEP_ALIVE_THRESHOLDS: &[&str] = &[
    "oneMinute",
    "fiveMinutes",
    "fifteenMinutes",
    "thirtyMinutes",
    "oneHour",
];

/// A space's own hibernate override. Absent means Follow General, so that
/// state is the absence of a value rather than a listed spelling.
pub const HIBERNATE_MODES: &[&str] = &["never", "always"];

/// chrome/browser/resources/atrium/notification-timeout.js's
/// NOTIFICATION_TIMEOUT_STEPS, in the same order.
pub const NOTIFICATION_TIMEOUTS: &[&str] = &[
    "oneSecond",
    "threeSeconds",
    "fiveSeconds",
    "tenSeconds",
    "keep",
];

pub const SKYLIGHT_WEATHERS: &[&str] = &["clear", "cloudy", "rain", "snow"];

/// chrome/browser/resources/atrium/settings.js's AI pane, "Model" row.
/// Same three choices as the JS `AI_MODELS` array, in
/// the same order.
pub const AI_MODELS: &[&str] = &["onDevice", "ollama", "customEndpoint"];

/// A `set_*_choice` `parse` callback for a plain closed list: `v` canonical if
/// it is in `list`, refused otherwise.
pub fn parse_choice(list: &[&str], v: &str) -> Result<String, ()> {
    list.contains(&v).then(|| v.to_string()).ok_or(())
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The split: the retired `"never"` duration must never
    /// reappear as a legal spelling here.
    #[test]
    fn memory_saver_thresholds_excludes_the_retired_never() {
        assert!(!MEMORY_SAVER_THRESHOLDS.contains(&"never"));
    }
}
