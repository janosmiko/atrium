// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Browser import: a reader turns another browser's files into an
//! [`ImportPlan`], and the applier adds that plan to Atrium's stores.

mod apply;
pub mod arc;
pub mod chrome;
pub mod firefox;
pub mod mozilla;
pub mod mozlz4;
pub mod places;
pub mod zen;

pub use apply::apply_import;
pub(crate) use apply::apply_import_with_existing;

use std::collections::{BTreeMap, HashSet};

use serde::Serialize;
use serde_json::Value;

use crate::mutate::truncate_to;
use crate::small_stores::{ThemeEffects, ThemeSpec, ThemeStop};
use crate::spaces::SwiftDate;

/// The cap mutate::rename_tab puts on a tab title.
const MAX_TEXT_CHARS: usize = 200;

/// Reads one source's files, keyed by file name, into a plan.
pub fn read(source: &str, files: &BTreeMap<String, String>) -> Result<ImportPlan, String> {
    match source {
        "arc" => arc::read(files),
        "zen" => zen::read(files),
        "chrome" => chrome::read(files),
        "firefox" => firefox::read(files),
        _ => Err(format!("unknown import source: {source}")),
    }
}

pub(crate) fn text<'a>(value: &'a Value, key: &str) -> Option<&'a str> {
    value.get(key).and_then(Value::as_str).filter(|s| !s.is_empty())
}

pub(crate) fn flag(value: &Value, key: &str) -> bool {
    value.get(key).and_then(Value::as_bool).unwrap_or(false)
}

pub(crate) fn clamped(s: &str) -> String {
    truncate_to(s, MAX_TEXT_CHARS).into_owned()
}

/// The objects in a JSON array. Any other value is skipped.
pub(crate) fn objects(list: Option<&Value>) -> impl Iterator<Item = &Value> {
    list.and_then(Value::as_array).into_iter().flatten().filter(|v| v.is_object())
}

/// Chromium's `profile.info_cache` in Local State: folder name to profile
/// name. Arc and Chrome both write it.
pub(crate) fn info_cache_names(local_state: &Value) -> BTreeMap<String, String> {
    local_state
        .pointer("/profile/info_cache")
        .and_then(Value::as_object)
        .map(|cache| {
            cache
                .iter()
                .filter_map(|(dir, info)| Some((dir.clone(), clamped(text(info, "name")?))))
                .collect()
        })
        .unwrap_or_default()
}

/// Chromium's own cap, url::kMaxURLChars. A tab past it already cannot open,
/// so it is dropped here rather than opened cut.
const MAX_URL_BYTES: usize = 2 * 1024 * 1024;

/// Only these schemes become tabs. A `javascript:` bookmarklet must never
/// arrive as something a click runs.
pub(crate) fn importable_url(url: &str) -> bool {
    url.len() <= MAX_URL_BYTES
        && url.split_once(':')
            .is_some_and(|(scheme, _)| ["http", "https", "file"].iter().any(|s| scheme.eq_ignore_ascii_case(s)))
}

/// The Theme Studio defaults for every field a source has no value for.
pub(crate) fn theme_spec(name: &str, stops: Vec<ThemeStop>, opacity: f64, grain: f64) -> ThemeSpec {
    ThemeSpec {
        name: name.into(),
        stops,
        opacity,
        effects: ThemeEffects { grain, vignette: 0.0, frost: 0.0, warmth: 0.0 },
        dark_adaptation: 0.45,
        night_shift_enabled: false,
        ambient_enabled: false,
        version: crate::schema::current::THEMES,
    }
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct ImportPlan {
    /// "arc", "zen", "chrome" or "firefox".
    pub source: String,
    pub profiles: Vec<PlannedProfile>,
    pub spaces: Vec<PlannedSpace>,
    pub favorites: Vec<PlannedFavorite>,
    pub routes: Vec<PlannedRoute>,
    pub engines: Vec<PlannedEngine>,
    /// What the reader skipped, in words the Settings pane shows the user.
    pub notes: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct PlannedProfile {
    pub key: String,
    pub name: String,
    pub is_default: bool,
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct PlannedSpace {
    /// The source browser's own space id. Routes name a space by this key.
    pub key: String,
    pub name: String,
    pub profile_key: String,
    pub emoji: Option<String>,
    /// "#RRGGBB".
    pub tint: Option<String>,
    pub theme: Option<ThemeSpec>,
    pub pinned: Vec<PlannedTab>,
    pub tabs: Vec<PlannedTab>,
    pub groups: Vec<PlannedGroup>,
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct PlannedTab {
    pub url: String,
    pub title: String,
    pub last_active_at: Option<SwiftDate>,
    /// An index into the space's `groups`.
    pub group: Option<usize>,
}

impl PlannedTab {
    pub fn new(url: &str, title: &str) -> Self {
        Self { url: url.into(), title: title.into(), ..Default::default() }
    }
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct PlannedGroup {
    pub name: String,
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct PlannedFavorite {
    pub url: String,
    pub title: String,
    pub profile_key: String,
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct PlannedRoute {
    /// "contains" or "regex", the two match types routing.json knows.
    pub match_type: String,
    pub pattern: String,
    /// None is the current space.
    pub target_space_key: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct PlannedEngine {
    pub name: String,
    pub keyword: String,
    /// Holds exactly one `%s`.
    pub url: String,
    pub profile_key: String,
    /// The source profile's default engine.
    pub is_default: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Serialize)]
pub struct KindCount {
    pub added: usize,
    pub duplicate: usize,
    pub dropped: usize,
}

#[derive(Debug, Clone, PartialEq, Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ImportReport {
    pub source: String,
    pub profiles: KindCount,
    pub spaces: KindCount,
    pub tabs: KindCount,
    pub groups: KindCount,
    pub favorites: KindCount,
    pub routes: KindCount,
    pub engines: KindCount,
    pub notes: Vec<String>,
    /// Source profile key to Atrium profile id. The caller reads each source
    /// profile's history into the Atrium profile named here.
    pub profile_ids: BTreeMap<String, String>,
}

impl ImportReport {
    /// Routes and engines are left out: they live in routing.json and
    /// settings.json, which the caller saves.
    pub fn document_changed(&self) -> bool {
        [self.profiles, self.spaces, self.tabs, self.groups, self.favorites]
            .iter()
            .any(|kind| kind.added > 0)
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Serialize)]
pub struct ImportSummary {
    pub profiles: usize,
    pub spaces: usize,
    pub tabs: usize,
    pub favorites: usize,
    pub routes: usize,
    pub engines: usize,
    pub notes: usize,
}

pub fn summary(plan: &ImportPlan) -> ImportSummary {
    ImportSummary {
        profiles: plan.profiles.len(),
        spaces: plan.spaces.len(),
        tabs: plan.spaces.iter().map(|s| s.pinned.len() + s.tabs.len()).sum(),
        favorites: plan.favorites.len(),
        routes: plan.routes.len(),
        engines: distinct_engines(&plan.engines),
        notes: plan.notes.len(),
    }
}

/// What the applier would add to an empty list: a keyword or URL seen before
/// is a duplicate there too.
fn distinct_engines(engines: &[PlannedEngine]) -> usize {
    let mut keywords = HashSet::new();
    let mut urls = HashSet::new();
    engines
        .iter()
        .filter(|e| {
            let seen = find_engine_match(
                &e.keyword,
                &e.url,
                |k| keywords.contains(k).then_some(()),
                |u| urls.contains(u).then_some(()),
            );
            let new = seen.is_none();
            if new {
                keywords.insert(e.keyword.trim().to_lowercase());
                urls.insert(e.url.as_str());
            }
            new
        })
        .count()
}

/// Looks a planned engine up by its normalized keyword, then by URL. The one
/// rule the preview count and the applier both judge a duplicate on, so they
/// can't drift apart.
pub(crate) fn find_engine_match<T>(
    keyword: &str,
    url: &str,
    by_keyword: impl FnOnce(&str) -> Option<T>,
    by_url: impl FnOnce(&str) -> Option<T>,
) -> Option<T> {
    by_keyword(&keyword.trim().to_lowercase()).or_else(|| by_url(url))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tabs(urls: &[&str]) -> Vec<PlannedTab> {
        urls.iter().map(|u| PlannedTab::new(u, u)).collect()
    }

    #[test]
    fn the_summary_counts_the_tabs_in_both_lists() {
        let plan = ImportPlan {
            source: "arc".into(),
            profiles: vec![PlannedProfile {
                key: "default".into(),
                name: "Personal".into(),
                is_default: true,
            }],
            spaces: vec![PlannedSpace {
                key: "S".into(),
                name: "Work".into(),
                profile_key: "default".into(),
                pinned: tabs(&["https://a.example.test/"]),
                tabs: tabs(&["https://b.example.test/", "https://c.example.test/"]),
                ..Default::default()
            }],
            favorites: vec![PlannedFavorite {
                url: "https://f.example.test/".into(),
                title: "F".into(),
                profile_key: "default".into(),
            }],
            routes: Vec::new(),
            engines: Vec::new(),
            notes: vec!["one note".into()],
        };

        assert_eq!(
            summary(&plan),
            ImportSummary { profiles: 1, spaces: 1, tabs: 3, favorites: 1, routes: 0, engines: 0, notes: 1 }
        );
    }

    #[test]
    fn the_summary_counts_the_engines_the_applier_would_add() {
        let engine = |keyword: &str, url: &str| PlannedEngine {
            name: keyword.into(),
            keyword: keyword.into(),
            url: url.into(),
            ..Default::default()
        };
        let plan = ImportPlan {
            engines: vec![
                engine("docs", "https://a.example.test/?q=%s"),
                engine("DOCS ", "https://b.example.test/?q=%s"),
                engine("other", "https://a.example.test/?q=%s"),
                engine("wiki", "https://w.example.test/?q=%s"),
            ],
            ..Default::default()
        };
        assert_eq!(summary(&plan).engines, 2, "a keyword or URL seen before is a duplicate");
    }

    #[test]
    fn the_report_writes_camel_case_with_three_counts_per_kind() {
        let report = ImportReport { source: "arc".into(), ..Default::default() };
        let json = serde_json::to_string(&report).unwrap();
        assert!(json.contains(r#""tabs":{"added":0,"duplicate":0,"dropped":0}"#), "{json}");
        assert!(json.contains(r#""source":"arc""#), "{json}");
    }

    #[test]
    fn the_report_writes_the_profile_map_as_profile_ids() {
        let mut report = ImportReport::default();
        report.profile_ids.insert("default".into(), "Default".into());
        let json = serde_json::to_string(&report).unwrap();
        assert!(json.contains(r#""profileIds":{"default":"Default"}"#), "{json}");
    }

    #[test]
    fn only_an_added_document_item_changes_the_document() {
        let mut report = ImportReport::default();
        report.routes.added = 2;
        report.tabs.duplicate = 5;
        assert!(!report.document_changed(), "routes live in routing.json");
        report.groups.added = 1;
        assert!(report.document_changed());
    }

    #[test]
    fn only_web_and_file_urls_are_importable() {
        for url in ["https://a.example.test/", "HTTP://a.example.test/", "file:///tmp/a.html"] {
            assert!(importable_url(url), "{url}");
        }
        for url in ["javascript:alert(1)", "about:preferences", "chrome://settings", "moz-extension://x/", "data:text/html,x", ""] {
            assert!(!importable_url(url), "{url}");
        }
    }

    #[test]
    fn a_url_past_chromiums_own_cap_is_not_importable() {
        let base = "https://a.example.test/";
        let at_cap = format!("{base}{}", "a".repeat(2 * 1024 * 1024 - base.len()));
        assert!(importable_url(&at_cap));
        assert!(!importable_url(&format!("{at_cap}a")));
    }
}
