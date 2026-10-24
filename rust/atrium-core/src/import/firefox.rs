// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Reads Firefox's profiles, bookmarks, open tabs and search engines into an
//! [`ImportPlan`].
//!
//! C++ runs the bookmarks query on a private copy of each places.sqlite, and
//! decodes the newer session file and search.json.mozlz4 before the call.
//! The files come from another browser's profile, so every lookup tolerates
//! a missing or odd value.

use std::collections::{BTreeMap, HashMap, HashSet};

use serde_json::Value;

use super::mozilla::{self, IniProfile, UNIX_TO_COCOA_SECONDS};
use super::places::{self, BookmarkSkips};
use super::{
    clamped, flag, importable_url, objects, text, ImportPlan, PlannedEngine, PlannedGroup,
    PlannedProfile, PlannedSpace, PlannedTab,
};
use crate::search_engines::{scheme_and_host, usable_url};

pub const PROFILES_FILE: &str = "profiles.ini";
/// C++ sends the newer of sessionstore.jsonlz4 and
/// sessionstore-backups/recovery.jsonlz4 under this one name.
pub const SESSION_FILE: &str = "sessionstore.jsonlz4";
pub const SEARCH_FILE: &str = "search.json.mozlz4";

const UNNAMED_GROUP: &str = "Tab group";

#[derive(Default)]
struct Skipped {
    bookmarks: BookmarkSkips,
    not_web_tabs: usize,
    container_tabs: usize,
    own_engines: usize,
    unusable_engines: usize,
    unread_engines: bool,
}

impl Skipped {
    fn write_notes(&self, notes: &mut Vec<String>) {
        self.bookmarks.write_notes("Firefox", notes);
        for (count, label) in [
            (
                self.not_web_tabs,
                "Skipped Firefox tabs that are not web pages",
            ),
            (
                self.container_tabs,
                "Firefox container tabs kept in their profile's space",
            ),
            (self.own_engines, "Skipped Firefox's own search engines"),
            (
                self.unusable_engines,
                "Skipped Firefox search engines with a URL that Atrium cannot use",
            ),
        ] {
            if count > 0 {
                notes.push(format!("{label}: {count}."));
            }
        }
        if self.unread_engines {
            notes.push("Firefox's search engines could not be read.".into());
        }
    }
}

pub fn read(files: &BTreeMap<String, String>) -> Result<ImportPlan, String> {
    let mut plan = ImportPlan {
        source: "firefox".into(),
        ..Default::default()
    };
    let ini = match files.get(PROFILES_FILE) {
        Some(text) => mozilla::profiles_ini(text),
        None => {
            plan.notes.push(
                "Firefox's profiles.ini is missing, so profiles use their folder names.".into(),
            );
            mozilla::ProfilesIni::default()
        }
    };
    if ini.outside > 0 {
        plan.notes.push(format!(
            "Skipped Firefox profiles outside the Firefox folder: {}.",
            ini.outside
        ));
    }
    let suffix = format!("/{}", places::BOOKMARKS_ROWS);
    let folders: Vec<&str> = files
        .keys()
        .filter_map(|k| k.strip_suffix(suffix.as_str()))
        .collect();
    let mut skipped = Skipped::default();
    for &path in &folders {
        let profile = mozilla::folder_profile(&ini, path, folders.len());
        read_profile(files, &profile, &mut plan, &mut skipped)?;
    }
    skipped.write_notes(&mut plan.notes);
    Ok(plan)
}

fn read_profile(
    files: &BTreeMap<String, String>,
    profile: &IniProfile,
    plan: &mut ImportPlan,
    skipped: &mut Skipped,
) -> Result<(), String> {
    let path = profile.path.as_str();
    // C++ sends "" when the copy or the query failed. The copy gives up when
    // a running Firefox keeps rewriting the file.
    let rows: Value = serde_json::from_str(&files[&format!("{path}/{}", places::BOOKMARKS_ROWS)])
        .map_err(|_| {
        "Firefox's bookmarks could not be read or are too large. If Firefox is open, quit it and import again."
            .to_string()
    })?;
    plan.profiles.push(PlannedProfile {
        key: path.into(),
        name: profile.name.clone(),
        is_default: profile.is_default,
    });
    let mut space = PlannedSpace {
        key: path.into(),
        name: profile.name.clone(),
        profile_key: path.into(),
        ..Default::default()
    };
    let mut seen = HashSet::new();
    places::read_bookmarks(&rows, &mut space, &mut seen, &mut skipped.bookmarks);
    if let Some(text) = files.get(&format!("{path}/{SESSION_FILE}")) {
        if text.is_empty() {
            // C++ sends "" for a file it could not read or decode, or that
            // failed the size budget. One message covers all three.
            return Err("Firefox's session file is damaged or too large.".into());
        }
        let session: Value = serde_json::from_str(text)
            .map_err(|e| format!("Firefox's session file could not be read: {e}"))?;
        read_session(&session, &mut space, &mut seen, skipped);
    }
    plan.spaces.push(space);
    // A damaged search file only costs the engines and gives a note.
    if let Some(text) = files.get(&format!("{path}/{SEARCH_FILE}")) {
        match serde_json::from_str::<Value>(text) {
            Ok(search) => read_engines(&search, path, plan, skipped),
            Err(_) => skipped.unread_engines = true,
        }
    }
    Ok(())
}

/// Open windows only. `_closedWindows` is history, not tabs.
fn read_session(
    session: &Value,
    space: &mut PlannedSpace,
    seen: &mut HashSet<String>,
    skipped: &mut Skipped,
) {
    let mut groups: HashMap<&str, usize> = HashMap::new();
    for window in objects(session.get("windows")) {
        let names: HashMap<&str, &str> = objects(window.get("groups"))
            .filter_map(|g| Some((text(g, "id")?, text(g, "name").unwrap_or(UNNAMED_GROUP))))
            .collect();
        for tab in objects(window.get("tabs")) {
            let Some((url, title)) = mozilla::session_entry(tab).and_then(|e| {
                let url = text(e, "url").filter(|u| importable_url(u))?;
                Some((url, clamped(text(e, "title").unwrap_or(url))))
            }) else {
                skipped.not_web_tabs += 1;
                continue;
            };
            // A bookmarked page is already a pinned tab.
            if !seen.insert(url.to_string()) {
                continue;
            }
            if tab
                .get("userContextId")
                .and_then(Value::as_u64)
                .is_some_and(|id| id != 0)
            {
                skipped.container_tabs += 1;
            }
            let pinned = flag(tab, "pinned");
            let group = match text(tab, "groupId").and_then(|id| Some((id, *names.get(id)?))) {
                Some((id, name)) if !pinned => Some(*groups.entry(id).or_insert_with(|| {
                    space.groups.push(PlannedGroup {
                        name: clamped(name),
                    });
                    space.groups.len() - 1
                })),
                _ => None,
            };
            let planned = PlannedTab {
                url: url.into(),
                title,
                last_active_at: tab
                    .get("lastAccessed")
                    .and_then(Value::as_f64)
                    .filter(|ms| *ms > 0.0)
                    .map(|ms| ms / 1000.0 - UNIX_TO_COCOA_SECONDS),
                group,
            };
            if pinned {
                space.pinned.push(planned);
            } else {
                space.tabs.push(planned);
            }
        }
    }
}

fn read_engines(search: &Value, profile_key: &str, plan: &mut ImportPlan, skipped: &mut Skipped) {
    let default_id = search
        .pointer("/metaData/defaultEngineId")
        .and_then(Value::as_str);
    for engine in objects(search.get("engines")) {
        // Firefox ships and updates these itself.
        if flag(engine, "_isConfigEngine")
            || flag(engine, "_isAppProvided")
            || engine.get("_urls").is_none()
        {
            skipped.own_engines += 1;
            continue;
        }
        let (Some(name), Some(url)) = (text(engine, "_name"), html_url(engine)) else {
            skipped.unusable_engines += 1;
            continue;
        };
        // Any other {placeholder} is one that Firefox fills in and Atrium cannot.
        if url.contains('{') || !usable_url(&url) {
            skipped.unusable_engines += 1;
            continue;
        }
        // Firefox needs no keyword and Atrium does, so the host stands in.
        let alias = engine
            .pointer("/_metaData/alias")
            .and_then(Value::as_str)
            .map(str::trim)
            .filter(|a| !a.is_empty());
        let Some(keyword) = alias
            .map(str::to_string)
            .or_else(|| scheme_and_host(&url).map(|(_, host)| host.to_ascii_lowercase()))
            .filter(|k| !k.is_empty())
        else {
            skipped.unusable_engines += 1;
            continue;
        };
        plan.engines.push(PlannedEngine {
            name: clamped(name),
            keyword: clamped(&keyword),
            url,
            profile_key: profile_key.into(),
            is_default: default_id.is_some_and(|id| text(engine, "id") == Some(id)),
        });
    }
}

/// The results page template with its GET params, `{searchTerms}` as `%s`.
/// A POST engine has no URL that a tab can open.
fn html_url(engine: &Value) -> Option<String> {
    let url = objects(engine.get("_urls"))
        .find(|u| matches!(text(u, "type"), None | Some("text/html")))?;
    if text(url, "method").is_some_and(|m| !m.eq_ignore_ascii_case("GET")) {
        return None;
    }
    let mut template = text(url, "template")?.to_string();
    for param in objects(url.get("params")) {
        let (Some(name), Some(value)) = (
            text(param, "name"),
            param.get("value").and_then(Value::as_str),
        ) else {
            continue;
        };
        template.push(if template.contains('?') { '&' } else { '?' });
        template.push_str(name);
        template.push('=');
        template.push_str(value);
    }
    Some(template.replace("{searchTerms}", "%s"))
}
