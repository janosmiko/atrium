// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Adds an [`ImportPlan`] to the document, the themes and a routing file.
//! It only appends. A matched item keeps everything it had.

use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};

use super::{ImportPlan, ImportReport, KindCount, PlannedSpace, find_engine_match};
use crate::launch::DEFAULT_PROFILE;
use crate::mutate::{self, Section};
use crate::search_engines::{self, SearchEngine};
use crate::small_stores::{CURRENT_SPACE_TARGET, RoutingFile, ThemesFile};
use crate::spaces::{Document, Profile, Space};
use crate::theme_math;

pub fn apply_import(
    doc: &mut Document,
    themes: &mut ThemesFile,
    routing: &mut RoutingFile,
    engines: &mut Vec<SearchEngine>,
    plan: &ImportPlan,
) -> ImportReport {
    let existing_profiles: BTreeSet<String> = doc.profiles.iter().map(|p| p.id.clone()).collect();
    apply_import_with_existing(doc, themes, routing, engines, plan, &existing_profiles)
}

// `cache.rs` passes an empty set for a document it just opened for this
// import: `fresh_loaded` seeds a `Default` profile before this runs, which
// reading `doc.profiles` here would count as pre-existing.
pub(crate) fn apply_import_with_existing(
    doc: &mut Document,
    themes: &mut ThemesFile,
    routing: &mut RoutingFile,
    engines: &mut Vec<SearchEngine>,
    plan: &ImportPlan,
    existing_profiles: &BTreeSet<String>,
) -> ImportReport {
    let mut report = ImportReport {
        source: plan.source.clone(),
        notes: plan.notes.clone(),
        ..Default::default()
    };
    let label = source_label(&plan.source);
    let profiles = map_profiles(doc, plan, &mut report);
    report.profile_ids = profiles.clone();
    let spaces = map_spaces(doc, themes, plan, &profiles, label, &mut report);
    add_favorites(doc, plan, &profiles, &mut report);
    add_routes(routing, plan, &spaces, &mut report);
    add_engines(doc, engines, plan, &profiles, existing_profiles, &mut report);
    report
}

fn source_label(source: &str) -> &str {
    match source {
        "arc" => "Arc",
        "zen" => "Zen",
        "chrome" => "Chrome",
        "firefox" => "Firefox",
        other => other,
    }
}

fn map_profiles(
    doc: &mut Document,
    plan: &ImportPlan,
    report: &mut ImportReport,
) -> BTreeMap<String, String> {
    let mut map = BTreeMap::new();
    let mut capped = false;
    for planned in &plan.profiles {
        if planned.is_default {
            map.insert(planned.key.clone(), DEFAULT_PROFILE.to_string());
            if doc.profiles.iter().any(|p| p.id == DEFAULT_PROFILE) {
                report.profiles.duplicate += 1;
            } else {
                // A first run has no record yet, but the Default directory
                // always exists. It skips MAX_PROFILES for the same reason.
                doc.profiles.push(Profile {
                    id: DEFAULT_PROFILE.to_string(),
                    name: planned.name.clone(),
                    search_engine: None,
                    hibernate: None,
                    hib_threshold: None,
                });
                report.profiles.added += 1;
            }
            continue;
        }
        let found = doc.profiles.iter().find(|p| p.name == planned.name);
        if let Some(found) = found {
            map.insert(planned.key.clone(), found.id.clone());
            report.profiles.duplicate += 1;
            continue;
        }
        match mutate::add_profile(doc) {
            Some(id) => {
                mutate::rename_profile(doc, &id, &planned.name);
                map.insert(planned.key.clone(), id);
                report.profiles.added += 1;
            }
            None => {
                report.profiles.dropped += 1;
                capped = true;
            }
        }
    }
    if capped {
        report.notes.push(format!(
            "Atrium holds {} profiles at most. The rest were not imported.",
            mutate::MAX_PROFILES
        ));
    }
    map
}

fn map_spaces(
    doc: &mut Document,
    themes: &mut ThemesFile,
    plan: &ImportPlan,
    profiles: &BTreeMap<String, String>,
    label: &str,
    report: &mut ImportReport,
) -> BTreeMap<String, String> {
    let mut map = BTreeMap::new();
    let mut capped = false;
    for planned in &plan.spaces {
        let Some(profile_id) = profiles.get(&planned.profile_key) else {
            report.spaces.dropped += 1;
            continue;
        };
        let matched = doc.spaces.iter().position(|s| {
            s.profile_id == *profile_id && is_import_name(&s.name, &planned.name, label)
        });
        let index = match matched {
            Some(index) => {
                report.spaces.duplicate += 1;
                index
            }
            None => match create_space(doc, themes, planned, profile_id, label) {
                Some(index) => {
                    report.spaces.added += 1;
                    index
                }
                None => {
                    report.spaces.dropped += 1;
                    capped = true;
                    continue;
                }
            },
        };
        map.insert(planned.key.clone(), doc.spaces[index].id.clone());
        add_tabs(doc, index, planned, report);
    }
    if capped {
        report.notes.push(format!(
            "Atrium holds {} spaces at most. The rest were not imported.",
            mutate::MAX_SPACES
        ));
    }
    map
}

/// `name`, `name (Arc)` or `name (Arc) N`: the names this import gives.
fn is_import_name(existing: &str, name: &str, label: &str) -> bool {
    if existing == name {
        return true;
    }
    let suffix = format!(" ({label})");
    let Some(rest) = existing
        .strip_prefix(name)
        .and_then(|r| r.strip_prefix(suffix.as_str()))
    else {
        return false;
    };
    rest.is_empty()
        || rest
            .strip_prefix(' ')
            .is_some_and(|n| n.parse::<u32>().is_ok())
}

fn free_name(doc: &Document, name: &str, label: &str) -> String {
    let taken = |candidate: &str| doc.spaces.iter().any(|s| s.name == candidate);
    if !taken(name) {
        return name.to_string();
    }
    let suffixed = format!("{name} ({label})");
    if !taken(&suffixed) {
        return suffixed;
    }
    (2..)
        .map(|n| format!("{name} ({label}) {n}"))
        .find(|candidate| !taken(candidate))
        .expect("an unbounded range always finds a free name")
}

fn create_space(
    doc: &mut Document,
    themes: &mut ThemesFile,
    planned: &PlannedSpace,
    profile_id: &str,
    label: &str,
) -> Option<usize> {
    let name = free_name(doc, &planned.name, label);
    mutate::create_space(doc, &name, profile_id)?;
    let index = doc.spaces.len() - 1;
    if let Some(emoji) = &planned.emoji {
        mutate::set_space_emoji(doc, index, emoji);
    }
    let theme = planned.theme.as_ref().map(theme_math::sanitized);
    let tint = planned
        .tint
        .clone()
        .or_else(|| theme.as_ref().map(theme_math::dominant_tint_hex));
    if let Some(tint) = tint {
        mutate::set_space_tint(doc, index, &tint);
    }
    if let Some(theme) = theme {
        themes.themes.insert(doc.spaces[index].id.clone(), theme);
    }
    Some(index)
}

fn has_url(space: &Space, url: &str) -> bool {
    space
        .pinned_tabs
        .iter()
        .chain(&space.tabs)
        .any(|t| t.url == url || t.pinned_url.as_deref() == Some(url))
}

fn add_tabs(doc: &mut Document, index: usize, planned: &PlannedSpace, report: &mut ImportReport) {
    // Keyed by (planned group, pinned): a group lives in one list only.
    let mut group_ids: BTreeMap<(usize, bool), String> = BTreeMap::new();
    for (section, tabs) in [
        (Section::Pinned, &planned.pinned),
        (Section::Regular, &planned.tabs),
    ] {
        for tab in tabs {
            if has_url(&doc.spaces[index], &tab.url) {
                report.tabs.duplicate += 1;
                continue;
            }
            let group = tab.group.and_then(|g| {
                group_id(
                    doc,
                    index,
                    section,
                    planned,
                    g,
                    &mut group_ids,
                    &mut report.groups,
                )
            });
            let append = |doc: &mut Document, group: Option<&str>| {
                mutate::append_cold_tab(
                    doc,
                    index,
                    section,
                    &tab.url,
                    &tab.title,
                    tab.last_active_at,
                    group,
                )
            };
            // A group refused for its list still lets the tab in, ungrouped.
            let added = append(doc, group.as_deref()) || (group.is_some() && append(doc, None));
            if added {
                report.tabs.added += 1;
            } else {
                report.tabs.dropped += 1;
            }
        }
    }
}

fn group_id(
    doc: &mut Document,
    index: usize,
    section: Section,
    planned: &PlannedSpace,
    planned_group: usize,
    cache: &mut BTreeMap<(usize, bool), String>,
    count: &mut KindCount,
) -> Option<String> {
    let pinned = section == Section::Pinned;
    if let Some(id) = cache.get(&(planned_group, pinned)) {
        return Some(id.clone());
    }
    let name = &planned.groups.get(planned_group)?.name;
    let space = &doc.spaces[index];
    let other = if pinned {
        &space.tabs
    } else {
        &space.pinned_tabs
    };
    let existing = space
        .groups
        .iter()
        .find(|g| {
            g.name == *name
                && !other
                    .iter()
                    .any(|t| t.group_id.as_deref() == Some(g.id.as_str()))
        })
        .map(|g| g.id.clone());
    let id = match existing {
        Some(id) => {
            count.duplicate += 1;
            id
        }
        None => {
            let Some(id) = mutate::create_group(doc, index, name) else {
                count.dropped += 1;
                return None;
            };
            count.added += 1;
            id
        }
    };
    cache.insert((planned_group, pinned), id.clone());
    Some(id)
}

fn add_favorites(
    doc: &mut Document,
    plan: &ImportPlan,
    profiles: &BTreeMap<String, String>,
    report: &mut ImportReport,
) {
    let mut capped = false;
    for fav in &plan.favorites {
        let Some(profile_id) = profiles.get(&fav.profile_key) else {
            report.favorites.dropped += 1;
            continue;
        };
        if doc
            .favorites
            .iter()
            .any(|f| f.profile_id == *profile_id && f.url == fav.url)
        {
            report.favorites.duplicate += 1;
        } else if mutate::add_favorite(doc, &fav.url, &fav.title, profile_id) {
            report.favorites.added += 1;
        } else {
            report.favorites.dropped += 1;
            capped = true;
        }
    }
    if capped {
        report.notes.push(format!(
            "Atrium holds {} favourites per profile. The rest were not imported.",
            mutate::MAX_FAVORITES_PER_PROFILE
        ));
    }
}

fn add_routes(
    routing: &mut RoutingFile,
    plan: &ImportPlan,
    spaces: &BTreeMap<String, String>,
    report: &mut ImportReport,
) {
    let mut unmapped = 0;
    let mut unusable = 0;
    for route in &plan.routes {
        let mapped = route.target_space_key.as_ref().map(|key| spaces.get(key));
        let lost = matches!(mapped, Some(None));
        let target = mapped
            .flatten()
            .cloned()
            .unwrap_or_else(|| CURRENT_SPACE_TARGET.to_string());
        if routing.has_rule(&route.match_type, &route.pattern, &target) {
            report.routes.duplicate += 1;
            continue;
        }
        let id = free_rule_id(routing, &plan.source);
        if routing.append_rule(&id, &route.match_type, &route.pattern, &target) {
            report.routes.added += 1;
            unmapped += usize::from(lost);
        } else {
            report.routes.dropped += 1;
            unusable += 1;
        }
    }
    if unmapped > 0 {
        report.notes.push(format!(
            "Routing rules whose space was not imported open in the current space: {unmapped}."
        ));
    }
    if unusable > 0 {
        report.notes.push(format!(
            "Skipped routing rules with a pattern that Atrium cannot use: {unusable}."
        ));
    }
}

fn free_rule_id(routing: &RoutingFile, source: &str) -> String {
    (1..)
        .map(|n| format!("import-{source}-{n}"))
        .find(|id| !routing.rules.iter().any(|r| r.id == *id))
        .expect("an unbounded range always finds a free id")
}

/// Appends each engine that matches none in the list on keyword or URL. A
/// source profile's default goes only to a profile this import created, so
/// an existing profile keeps the engine the user chose.
fn add_engines(
    doc: &mut Document,
    engines: &mut Vec<SearchEngine>,
    plan: &ImportPlan,
    profiles: &BTreeMap<String, String>,
    existing_profiles: &BTreeSet<String>,
    report: &mut ImportReport,
) {
    let mut by_keyword: HashMap<String, String> =
        engines.iter().map(|e| (e.keyword.clone(), e.id.clone())).collect();
    let mut by_url: HashMap<String, String> =
        engines.iter().map(|e| (e.url.clone(), e.id.clone())).collect();
    let mut ids: HashSet<String> = engines.iter().map(|e| e.id.clone()).collect();
    let mut next = 1;
    let mut unusable = 0;
    for planned in &plan.engines {
        let matched = find_engine_match(
            &planned.keyword,
            &planned.url,
            |k| by_keyword.get(k).cloned(),
            |u| by_url.get(u).cloned(),
        );
        let id = match matched {
            Some(id) => {
                report.engines.duplicate += 1;
                id
            }
            None => {
                let mut engine = SearchEngine {
                    id: free_engine_id(&ids, &plan.source, &mut next),
                    name: planned.name.clone(),
                    keyword: planned.keyword.trim().to_lowercase(),
                    url: planned.url.clone(),
                    enabled: true,
                    builtin: false,
                };
                if !search_engines::valid(&mut engine) {
                    report.engines.dropped += 1;
                    unusable += 1;
                    continue;
                }
                by_keyword.insert(engine.keyword.clone(), engine.id.clone());
                by_url.insert(engine.url.clone(), engine.id.clone());
                ids.insert(engine.id.clone());
                let id = engine.id.clone();
                engines.push(engine);
                report.engines.added += 1;
                id
            }
        };
        if !planned.is_default {
            continue;
        }
        let created = profiles
            .get(&planned.profile_key)
            .filter(|id| !existing_profiles.contains(id.as_str()))
            .cloned();
        if let Some(profile_id) = created {
            mutate::set_profile_search_engine(doc, &profile_id, Some(&id));
        }
    }
    if unusable > 0 {
        report
            .notes
            .push(format!("Skipped search engines that Atrium cannot use: {unusable}."));
    }
}

fn free_engine_id(taken: &HashSet<String>, source: &str, next: &mut usize) -> String {
    loop {
        let id = format!("import-{source}-{next}");
        *next += 1;
        if !taken.contains(&id) {
            return id;
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::import::{
        PlannedEngine, PlannedFavorite, PlannedGroup, PlannedProfile, PlannedRoute, PlannedSpace,
        PlannedTab,
    };
    use crate::mutate::test_support::{document, space, tab, urls};
    use crate::search_engines::SearchEngine;
    use crate::spaces::Profile;

    fn profile(id: &str, name: &str) -> Profile {
        Profile {
            id: id.into(),
            name: name.into(),
            search_engine: None,
            hibernate: None,
            hib_threshold: None,
        }
    }

    /// The Default profile holding one space, "Work", with one tab.
    fn existing() -> Document {
        let mut work = space("Work", 0);
        work.profile_id = "Default".into();
        work.tabs.push(tab("https://old.example.test/"));
        let mut doc = document(vec![work]);
        doc.profiles = vec![profile("Default", "Default")];
        doc
    }

    fn planned(name: &str, key: &str, tab_urls: &[&str]) -> PlannedSpace {
        PlannedSpace {
            key: key.into(),
            name: name.into(),
            profile_key: "default".into(),
            tabs: tab_urls.iter().map(|u| PlannedTab::new(u, u)).collect(),
            ..Default::default()
        }
    }

    fn plan(spaces: Vec<PlannedSpace>) -> ImportPlan {
        ImportPlan {
            source: "arc".into(),
            profiles: vec![PlannedProfile {
                key: "default".into(),
                name: "Personal".into(),
                is_default: true,
            }],
            spaces,
            ..Default::default()
        }
    }

    fn run(doc: &mut Document, plan: &ImportPlan) -> (ImportReport, ThemesFile, RoutingFile) {
        let mut themes = ThemesFile::default();
        let mut routing = RoutingFile::default();
        let report = apply_import(doc, &mut themes, &mut routing, &mut Vec::new(), plan);
        (report, themes, routing)
    }

    fn count(added: usize, duplicate: usize, dropped: usize) -> KindCount {
        KindCount {
            added,
            duplicate,
            dropped,
        }
    }

    fn planned_engine(keyword: &str, url: &str, profile_key: &str, is_default: bool) -> PlannedEngine {
        PlannedEngine {
            name: keyword.trim().into(),
            keyword: keyword.into(),
            url: url.into(),
            profile_key: profile_key.into(),
            is_default,
        }
    }

    fn google() -> SearchEngine {
        SearchEngine {
            id: "google".into(),
            name: "Google".into(),
            keyword: "google.com".into(),
            url: "https://www.google.com/search?q=%s".into(),
            enabled: true,
            builtin: true,
        }
    }

    fn apply_engines(doc: &mut Document, engines: &mut Vec<SearchEngine>, plan: &ImportPlan) -> ImportReport {
        apply_import(doc, &mut ThemesFile::default(), &mut RoutingFile::default(), engines, plan)
    }

    #[test]
    fn an_engine_is_appended_once_and_matched_on_keyword_or_url() {
        let mut doc = existing();
        let mut engines = vec![google()];
        let mut p = plan(Vec::new());
        p.engines = vec![
            planned_engine("Docs", "https://docs.example.test/?q=%s", "default", false),
            planned_engine("GOOGLE.COM ", "https://other.example.test/?q=%s", "default", false),
            planned_engine("g2", "https://www.google.com/search?q=%s", "default", false),
            planned_engine("docs", "https://docs.example.test/?q=%s", "default", false),
        ];

        let report = apply_engines(&mut doc, &mut engines, &p);

        assert_eq!(report.engines, count(1, 3, 0));
        assert_eq!(engines[0], google(), "an existing engine never changes");
        assert_eq!(
            engines[1],
            SearchEngine {
                id: "import-arc-1".into(),
                name: "Docs".into(),
                keyword: "docs".into(),
                url: "https://docs.example.test/?q=%s".into(),
                enabled: true,
                builtin: false,
            }
        );
        assert_eq!(engines.len(), 2);
    }

    #[test]
    fn an_engine_atrium_cannot_use_is_dropped_with_a_note() {
        let mut doc = existing();
        let mut engines = Vec::new();
        let mut p = plan(Vec::new());
        p.engines = vec![planned_engine("plain", "http://plain.example.test/?q=%s", "default", false)];

        let report = apply_engines(&mut doc, &mut engines, &p);

        assert_eq!(report.engines, count(0, 0, 1));
        assert!(engines.is_empty());
        assert!(
            report.notes.contains(&"Skipped search engines that Atrium cannot use: 1.".to_string()),
            "{:?}",
            report.notes
        );
    }

    #[test]
    fn a_second_import_adds_no_engine() {
        let mut doc = existing();
        let mut engines = Vec::new();
        let mut p = plan(Vec::new());
        p.engines = vec![planned_engine("docs", "https://docs.example.test/?q=%s", "default", false)];
        apply_engines(&mut doc, &mut engines, &p);
        let once = engines.clone();

        let again = apply_engines(&mut doc, &mut engines, &p);

        assert_eq!(engines, once);
        assert_eq!(again.engines, count(0, 1, 0));
    }

    #[test]
    fn a_default_engine_lands_only_on_a_profile_this_import_created() {
        let mut doc = existing();
        let mut engines = Vec::new();
        let mut p = plan(Vec::new());
        p.profiles.push(PlannedProfile { key: "work".into(), name: "Work".into(), is_default: false });
        p.engines = vec![
            planned_engine("docs", "https://docs.example.test/?q=%s", "default", true),
            planned_engine("wiki", "https://wiki.example.test/?q=%s", "work", true),
        ];

        apply_engines(&mut doc, &mut engines, &p);

        let default = doc.profiles.iter().find(|p| p.id == "Default").unwrap();
        assert_eq!(default.search_engine, None, "an existing profile keeps its own choice");
        let work = doc.profiles.iter().find(|p| p.name == "Work").unwrap();
        assert_eq!(work.search_engine.as_deref(), Some("import-arc-2"));
    }

    #[test]
    fn a_matched_space_keeps_what_it_had_and_gains_the_new_tabs() {
        let mut doc = existing();
        let before = doc.spaces[0].clone();
        let mut work = planned(
            "Work",
            "A",
            &["https://old.example.test/", "https://new.example.test/"],
        );
        work.emoji = Some("Z".into());
        work.tint = Some("#123456".into());
        let (report, themes, _) = run(&mut doc, &plan(vec![work]));

        let after = &doc.spaces[0];
        assert_eq!(
            (&after.name, &after.emoji, &after.tint),
            (&before.name, &before.emoji, &before.tint)
        );
        assert_eq!(after.tabs[0], before.tabs[0]);
        assert_eq!(
            urls(after),
            ["https://old.example.test/", "https://new.example.test/"]
        );
        assert!(themes.themes.is_empty(), "a matched space gets no theme");
        assert_eq!(report.spaces, count(0, 1, 0));
        assert_eq!(report.tabs, count(1, 1, 0));
    }

    #[test]
    fn a_second_import_adds_nothing() {
        let mut doc = existing();
        let mut themes = ThemesFile::default();
        let mut routing = RoutingFile::default();
        let mut engines = Vec::new();
        let mut p = plan(vec![
            planned("Work", "A", &["https://new.example.test/"]),
            planned("Home", "B", &["https://home.example.test/"]),
        ]);
        p.spaces[1].theme = Some(crate::theme_math::single_stop_theme("#004080"));
        p.favorites = vec![PlannedFavorite {
            url: "https://fav.example.test/".into(),
            title: "Fav".into(),
            profile_key: "default".into(),
        }];
        p.routes = vec![
            PlannedRoute {
                match_type: "contains".into(),
                pattern: "home.example.test".into(),
                target_space_key: Some("B".into()),
            },
            PlannedRoute {
                match_type: "contains".into(),
                pattern: "gone.example.test".into(),
                target_space_key: Some("GONE".into()),
            },
        ];

        apply_import(&mut doc, &mut themes, &mut routing, &mut engines, &p);
        let (doc_once, themes_once, routing_once) = (doc.clone(), themes.clone(), routing.clone());
        let again = apply_import(&mut doc, &mut themes, &mut routing, &mut engines, &p);

        assert_eq!(doc, doc_once);
        assert_eq!(themes, themes_once);
        assert_eq!(routing, routing_once);
        assert!(!again.document_changed());
        assert_eq!(again.routes, count(0, 2, 0));
        assert!(
            !again.notes.iter().any(|n| n.contains("current space")),
            "{:?}",
            again.notes
        );
    }

    #[test]
    fn the_default_profile_maps_to_default_on_a_first_run() {
        let mut doc = document(Vec::new());
        doc.profiles.clear();
        let mut p = plan(vec![planned("Inbox", "S0", &[])]);
        p.favorites = vec![PlannedFavorite {
            url: "https://fav.example.test/".into(),
            title: "Fav".into(),
            profile_key: "default".into(),
        }];

        let (first, _, _) = run(&mut doc, &p);

        assert_eq!(first.profiles, count(1, 0, 0));
        let ids: Vec<&str> = doc.profiles.iter().map(|q| q.id.as_str()).collect();
        assert_eq!(ids, ["Default"]);
        assert_eq!(doc.spaces[0].profile_id, "Default");
        assert_eq!(doc.favorites[0].profile_id, "Default");

        let (second, _, _) = run(&mut doc, &p);
        assert_eq!(second.profiles, count(0, 1, 0));
    }

    #[test]
    fn a_name_that_is_taken_gets_the_source_suffix() {
        let mut a = space("Work", 0);
        a.profile_id = "p-other".into();
        let mut b = space("Work (Arc)", 1);
        b.profile_id = "p-other".into();
        let mut doc = document(vec![a, b]);
        doc.profiles = vec![profile("Default", "Default"), profile("p-other", "Other")];

        let (first, _, _) = run(&mut doc, &plan(vec![planned("Work", "A", &[])]));
        assert_eq!(first.spaces, count(1, 0, 0));
        assert_eq!(doc.spaces[2].name, "Work (Arc) 2");
        assert_eq!(doc.spaces[2].profile_id, "Default");

        let (second, _, _) = run(&mut doc, &plan(vec![planned("Work", "A", &[])]));
        assert_eq!(
            second.spaces,
            count(0, 1, 0),
            "the suffixed name matches on the next import"
        );
    }

    #[test]
    fn a_zen_import_gets_the_zen_suffix() {
        let mut a = space("Work", 0);
        a.profile_id = "p-other".into();
        let mut b = space("Work (Arc)", 1);
        b.profile_id = "p-other".into();
        let mut doc = document(vec![a, b]);
        doc.profiles = vec![profile("Default", "Default"), profile("p-other", "Other")];

        let mut p = plan(vec![planned("Work", "A", &[])]);
        p.source = "zen".into();
        run(&mut doc, &p);

        assert_eq!(doc.spaces[2].name, "Work (Zen)");
    }

    #[test]
    fn profiles_match_the_default_and_by_name_or_are_created() {
        let mut doc = existing();
        doc.profiles.push(profile("profile-9", "Side Project"));
        let mut p = plan(vec![
            planned("Inbox", "S0", &[]),
            planned("Notes", "S1", &[]),
            planned("Trips", "S2", &[]),
        ]);
        p.profiles.push(PlannedProfile {
            key: "Profile 1".into(),
            name: "Side Project".into(),
            is_default: false,
        });
        p.profiles.push(PlannedProfile {
            key: "Profile 2".into(),
            name: "Travel".into(),
            is_default: false,
        });
        p.spaces[1].profile_key = "Profile 1".into();
        p.spaces[2].profile_key = "Profile 2".into();

        let (report, _, _) = run(&mut doc, &p);

        assert_eq!(report.profiles, count(1, 2, 0));
        let travel = doc
            .profiles
            .iter()
            .find(|q| q.name == "Travel")
            .expect("Travel was created")
            .id
            .clone();
        let owner = |name: &str| {
            doc.spaces
                .iter()
                .find(|s| s.name == name)
                .unwrap()
                .profile_id
                .clone()
        };
        assert_eq!(owner("Inbox"), "Default");
        assert_eq!(owner("Notes"), "profile-9");
        assert_eq!(owner("Trips"), travel);
    }

    #[test]
    fn the_report_maps_each_source_profile_to_its_atrium_profile() {
        let mut doc = existing();
        doc.profiles.push(profile("profile-9", "Side Project"));
        let mut p = plan(vec![planned("Inbox", "S0", &[]), planned("Notes", "S1", &[])]);
        p.profiles.push(PlannedProfile {
            key: "Profile 1".into(),
            name: "Side Project".into(),
            is_default: false,
        });
        p.spaces[1].profile_key = "Profile 1".into();

        let (report, _, _) = run(&mut doc, &p);

        assert_eq!(report.profile_ids.get("default").map(String::as_str), Some("Default"));
        assert_eq!(report.profile_ids.get("Profile 1").map(String::as_str), Some("profile-9"));
        assert_eq!(report.profile_ids.len(), 2, "{:?}", report.profile_ids);
    }

    #[test]
    fn favourites_past_the_cap_are_dropped_with_a_note() {
        let mut doc = existing();
        for n in 0..mutate::MAX_FAVORITES_PER_PROFILE - 1 {
            assert!(mutate::add_favorite(
                &mut doc,
                &format!("https://f{n}.example.test/"),
                "F",
                "Default"
            ));
        }
        let mut p = plan(Vec::new());
        p.favorites = (0..3)
            .map(|n| PlannedFavorite {
                url: format!("https://new{n}.example.test/"),
                title: "N".into(),
                profile_key: "default".into(),
            })
            .collect();

        let (report, _, _) = run(&mut doc, &p);

        assert_eq!(report.favorites, count(1, 0, 2));
        assert!(
            report
                .notes
                .iter()
                .any(|n| n.contains("favourites per profile")),
            "{:?}",
            report.notes
        );
    }

    #[test]
    fn a_route_targets_the_space_it_was_mapped_to_or_the_current_one() {
        let mut doc = existing();
        let mut p = plan(vec![planned("Home", "B", &[])]);
        p.routes = vec![
            PlannedRoute {
                match_type: "contains".into(),
                pattern: "home.example.test".into(),
                target_space_key: Some("B".into()),
            },
            PlannedRoute {
                match_type: "contains".into(),
                pattern: "gone.example.test".into(),
                target_space_key: Some("GONE".into()),
            },
            PlannedRoute {
                match_type: "regex".into(),
                pattern: r"^https://now\.example\.test/".into(),
                target_space_key: None,
            },
        ];

        let (report, _, routing) = run(&mut doc, &p);

        let home = doc
            .spaces
            .iter()
            .find(|s| s.name == "Home")
            .unwrap()
            .id
            .clone();
        let targets: Vec<&str> = routing
            .rules
            .iter()
            .map(|r| r.target_space_id.as_str())
            .collect();
        assert_eq!(targets, [home.as_str(), "current", "current"]);
        assert!(routing.rules.iter().all(|r| r.enabled));
        assert_eq!(report.routes, count(3, 0, 0));
        assert!(
            report.notes.iter().any(|n| n.contains("current space")),
            "{:?}",
            report.notes
        );
    }

    #[test]
    fn a_regex_that_does_not_compile_is_skipped_with_a_note() {
        let mut doc = existing();
        let mut p = plan(Vec::new());
        p.routes = vec![PlannedRoute {
            match_type: "regex".into(),
            pattern: "([".into(),
            target_space_key: None,
        }];

        let (report, _, routing) = run(&mut doc, &p);

        assert_eq!(report.routes, count(0, 0, 1));
        assert!(routing.rules.is_empty());
        assert!(
            report.notes.iter().any(|n| n.contains("pattern")),
            "{:?}",
            report.notes
        );
    }

    #[test]
    fn a_created_space_gets_the_emoji_tint_and_theme() {
        let mut doc = existing();
        let mut home = planned("Home", "B", &[]);
        home.emoji = Some("H".into());
        home.tint = Some("#FF8000".into());
        home.theme = Some(crate::theme_math::single_stop_theme("#004080"));
        let mut side = planned("Side", "C", &[]);
        side.theme = Some(crate::theme_math::single_stop_theme("#004080"));

        let (_, themes, _) = run(&mut doc, &plan(vec![home, side]));

        let home = doc.spaces.iter().find(|s| s.name == "Home").unwrap();
        assert_eq!((home.emoji.as_str(), home.tint.as_str()), ("H", "#FF8000"));
        assert!(themes.themes.contains_key(&home.id));
        let side = doc.spaces.iter().find(|s| s.name == "Side").unwrap();
        assert_eq!(
            side.tint,
            crate::theme_math::dominant_tint_hex(&themes.themes[&side.id])
        );
    }

    #[test]
    fn a_folder_arrives_as_one_group_holding_its_tabs() {
        let mut doc = existing();
        let mut work = planned("Work", "A", &[]);
        work.groups = vec![PlannedGroup {
            name: "Reference".into(),
        }];
        work.pinned = vec![
            PlannedTab {
                group: Some(0),
                ..PlannedTab::new("https://spec.example.test/", "Spec")
            },
            PlannedTab {
                group: Some(0),
                ..PlannedTab::new("https://deep.example.test/", "Deep")
            },
        ];

        let (report, _, _) = run(&mut doc, &plan(vec![work]));

        let space = &doc.spaces[0];
        assert_eq!(space.groups.len(), 1);
        assert_eq!(space.groups[0].name, "Reference");
        assert!(
            space
                .pinned_tabs
                .iter()
                .all(|t| t.group_id.as_deref() == Some(space.groups[0].id.as_str()))
        );
        assert_eq!(report.groups, count(1, 0, 0));
        assert_eq!(report.tabs, count(2, 0, 0));
    }

    #[test]
    fn two_spaces_with_one_title_in_one_profile_merge() {
        let mut doc = document(Vec::new());
        doc.profiles = vec![profile("Default", "Default")];

        let (report, _, _) = run(
            &mut doc,
            &plan(vec![
                planned("Work", "A", &["https://a.example.test/"]),
                planned("Work", "B", &["https://b.example.test/"]),
            ]),
        );

        assert_eq!(doc.spaces.len(), 1);
        assert_eq!(
            urls(&doc.spaces[0]),
            ["https://a.example.test/", "https://b.example.test/"]
        );
        assert_eq!(report.spaces, count(1, 1, 0));
    }

    fn grouped(urls: &[&str]) -> PlannedSpace {
        let mut work = planned("Work", "A", &[]);
        work.groups = vec![PlannedGroup {
            name: "Reference".into(),
        }];
        work.pinned = urls
            .iter()
            .map(|u| PlannedTab {
                group: Some(0),
                ..PlannedTab::new(u, u)
            })
            .collect();
        work
    }

    #[test]
    fn a_re_import_puts_a_new_tab_in_the_group_it_made_before() {
        let mut doc = existing();
        run(
            &mut doc,
            &plan(vec![grouped(&["https://spec.example.test/"])]),
        );
        let (report, _, _) = run(
            &mut doc,
            &plan(vec![grouped(&[
                "https://spec.example.test/",
                "https://more.example.test/",
            ])]),
        );

        let space = &doc.spaces[0];
        assert_eq!(space.groups.len(), 1);
        let id = space.groups[0].id.as_str();
        assert!(
            space
                .pinned_tabs
                .iter()
                .all(|t| t.group_id.as_deref() == Some(id))
        );
        assert_eq!(report.groups, count(0, 1, 0));
        assert_eq!(report.tabs, count(1, 1, 0));
    }

    #[test]
    fn a_tab_whose_group_is_refused_arrives_ungrouped() {
        let mut doc = existing();
        while mutate::create_group(&mut doc, 0, "Filler").is_some() {}

        let (report, _, _) = run(
            &mut doc,
            &plan(vec![grouped(&["https://spec.example.test/"])]),
        );

        let tab = &doc.spaces[0].pinned_tabs[0];
        assert_eq!(tab.url, "https://spec.example.test/");
        assert_eq!(tab.group_id, None);
        assert_eq!(report.groups, count(0, 0, 1));
        assert_eq!(report.tabs, count(1, 0, 0));
    }

    #[test]
    fn the_space_cap_stops_the_import_with_a_note() {
        let mut doc = existing();
        while mutate::create_space(&mut doc, "Filler", "Default").is_some() {}

        let (report, _, _) = run(
            &mut doc,
            &plan(vec![planned(
                "Brand new",
                "N",
                &["https://n.example.test/"],
            )]),
        );

        assert_eq!(report.spaces, count(0, 0, 1));
        assert_eq!(report.tabs, count(0, 0, 0));
        assert!(
            report.notes.iter().any(|n| n.contains("200 spaces")),
            "{:?}",
            report.notes
        );
    }
}
