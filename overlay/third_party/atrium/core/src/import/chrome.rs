// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Reads Chrome's profiles, bookmarks and search engines into an
//! [`ImportPlan`].
//!
//! C++ reads only the folders that Local State names, reduces Preferences to
//! the default engine, and runs the keywords query on a private copy of each
//! Web Data file. The files come from another browser's profile, so every
//! lookup tolerates a missing or odd value.

use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};

use serde_json::Value;

use super::{
    clamped, importable_url, info_cache_names, objects, text, ImportPlan, PlannedEngine,
    PlannedGroup, PlannedProfile, PlannedSpace, PlannedTab,
};
use crate::search_engines::usable_url;
use crate::spaces::SwiftDate;

pub const LOCAL_STATE_FILE: &str = "Local State";
pub const BOOKMARKS_FILES: [&str; 2] = ["Bookmarks", "AccountBookmarks"];
pub const PREFERENCES_FILE: &str = "Preferences";
pub const KEYWORDS_ROWS: &str = "Web Data#keywords";

const DEFAULT_FOLDER: &str = "Default";
/// From Chrome's 1601 epoch to the Cocoa epoch.
const WINDOWS_TO_COCOA_SECONDS: f64 = 12_622_780_800.0;
const OTHER_GROUP: &str = "Other bookmarks";
const MOBILE_GROUP: &str = "Mobile bookmarks";
const UNNAMED_FOLDER: &str = "Folder";

#[derive(Default)]
struct Skipped {
    not_web: usize,
    nested: usize,
    own_engines: usize,
    unusable_engines: usize,
    unread_engines: bool,
    unread_preferences: bool,
}

impl Skipped {
    fn write_notes(&self, notes: &mut Vec<String>) {
        for (count, label) in [
            (
                self.not_web,
                "Skipped Chrome bookmarks that are not web pages",
            ),
            (
                self.nested,
                "Chrome bookmarks moved from a nested folder to its top-level folder",
            ),
            (
                self.own_engines,
                "Skipped Chrome's own and unused search engines",
            ),
            (
                self.unusable_engines,
                "Skipped Chrome search engines with a URL that Atrium cannot use",
            ),
        ] {
            if count > 0 {
                notes.push(format!("{label}: {count}."));
            }
        }
        if self.unread_engines {
            notes.push("Chrome's search engines could not be read.".into());
        }
        if self.unread_preferences {
            notes.push("Chrome's default search engine could not be read.".into());
        }
        notes.push("Chrome's open tabs are not imported, only its bookmarks.".into());
    }
}

pub fn read(files: &BTreeMap<String, String>) -> Result<ImportPlan, String> {
    let mut plan = ImportPlan {
        source: "chrome".into(),
        ..Default::default()
    };
    let names = match files.get(LOCAL_STATE_FILE) {
        // C++ sends "" for a file it could not read or that failed the size
        // budget. One message covers both.
        Some(text) if text.is_empty() => {
            return Err("Chrome's Local State file is damaged or too large.".into());
        }
        Some(text) => info_cache_names(&parse(LOCAL_STATE_FILE, text)?),
        None => {
            plan.notes.push(
                "Chrome's Local State file is missing, so profiles use their folder names.".into(),
            );
            BTreeMap::new()
        }
    };
    let folders: BTreeSet<&str> = files
        .keys()
        .filter_map(|key| {
            let (folder, file) = key.rsplit_once('/')?;
            BOOKMARKS_FILES.contains(&file).then_some(folder)
        })
        .collect();
    let mut skipped = Skipped::default();
    for folder in folders {
        read_profile(files, folder, &names, &mut plan, &mut skipped)?;
    }
    skipped.write_notes(&mut plan.notes);
    Ok(plan)
}

fn parse(name: &str, text: &str) -> Result<Value, String> {
    serde_json::from_str(text).map_err(|e| format!("Chrome's {name} could not be read: {e}"))
}

fn read_profile(
    files: &BTreeMap<String, String>,
    folder: &str,
    names: &BTreeMap<String, String>,
    plan: &mut ImportPlan,
    skipped: &mut Skipped,
) -> Result<(), String> {
    let name = names
        .get(folder)
        .cloned()
        .unwrap_or_else(|| clamped(folder));
    plan.profiles.push(PlannedProfile {
        key: folder.into(),
        name: name.clone(),
        is_default: folder == DEFAULT_FOLDER,
    });
    let mut bookmarks = Bookmarks {
        space: PlannedSpace {
            key: folder.into(),
            name,
            profile_key: folder.into(),
            ..Default::default()
        },
        seen: HashSet::new(),
        groups: HashMap::new(),
    };
    for file in BOOKMARKS_FILES {
        let Some(text) = files.get(&format!("{folder}/{file}")) else {
            continue;
        };
        if text.is_empty() {
            return Err("Chrome's bookmarks file is damaged or too large.".into());
        }
        bookmarks.read_file(&parse(file, text)?, skipped);
    }
    plan.spaces.push(bookmarks.space);
    // A damaged Preferences file only costs the default engine and gives a note.
    let default_guid = match files.get(&format!("{folder}/{PREFERENCES_FILE}")) {
        Some(text) => match serde_json::from_str::<Value>(text) {
            Ok(prefs) => prefs
                .pointer("/default_search_provider/guid")
                .and_then(Value::as_str)
                .map(str::to_string),
            Err(_) => {
                skipped.unread_preferences = true;
                None
            }
        },
        None => None,
    };
    if let Some(rows) = files.get(&format!("{folder}/{KEYWORDS_ROWS}")) {
        match serde_json::from_str::<Value>(rows) {
            Ok(rows) => read_engines(&rows, folder, default_guid.as_deref(), plan, skipped),
            // C++ sends "" when the copy or the query failed.
            Err(_) => skipped.unread_engines = true,
        }
    }
    Ok(())
}

struct Bookmarks {
    space: PlannedSpace,
    /// Bookmarks and AccountBookmarks often hold the same links.
    seen: HashSet<String>,
    groups: HashMap<String, usize>,
}

impl Bookmarks {
    fn read_file(&mut self, file: &Value, skipped: &mut Skipped) {
        if let Some(bar) = file.pointer("/roots/bookmark_bar") {
            for node in objects(bar.get("children")) {
                if is_folder(node) {
                    let name = clamped(text(node, "name").unwrap_or(UNNAMED_FOLDER));
                    self.walk(node, &name, true, 0, skipped);
                } else {
                    self.add(node, None, skipped);
                }
            }
        }
        for (root, group) in [("other", OTHER_GROUP), ("synced", MOBILE_GROUP)] {
            if let Some(folder) = file.pointer(&format!("/roots/{root}")) {
                self.walk(folder, group, false, 0, skipped);
            }
        }
    }

    /// Every link under `folder` goes into `group`. serde_json's nesting
    /// limit of 128 already bounds the recursion.
    fn walk(
        &mut self,
        folder: &Value,
        group: &str,
        count_nested: bool,
        depth: usize,
        skipped: &mut Skipped,
    ) {
        for node in objects(folder.get("children")) {
            if is_folder(node) {
                self.walk(node, group, count_nested, depth + 1, skipped);
            } else if self.add(node, Some(group), skipped) && count_nested && depth > 0 {
                skipped.nested += 1;
            }
        }
    }

    fn add(&mut self, node: &Value, group: Option<&str>, skipped: &mut Skipped) -> bool {
        let Some(url) = text(node, "url") else {
            return false;
        };
        if !importable_url(url) {
            skipped.not_web += 1;
            return false;
        }
        if !self.seen.insert(url.to_string()) {
            return false;
        }
        let group = group.map(|name| self.group(name));
        self.space.pinned.push(PlannedTab {
            url: url.into(),
            title: clamped(text(node, "name").unwrap_or(url)),
            last_active_at: chrome_time(node.get("date_last_used")),
            group,
        });
        true
    }

    /// Made on the first link, so an empty folder makes no group.
    fn group(&mut self, name: &str) -> usize {
        if let Some(&index) = self.groups.get(name) {
            return index;
        }
        let index = self.space.groups.len();
        self.space.groups.push(PlannedGroup { name: name.into() });
        self.groups.insert(name.into(), index);
        index
    }
}

fn is_folder(node: &Value) -> bool {
    node.get("type").and_then(Value::as_str) == Some("folder")
}

/// Chrome writes microseconds since 1601 as a string. 0 means never.
fn chrome_time(value: Option<&Value>) -> Option<SwiftDate> {
    let micros = match value? {
        Value::String(s) => s.parse::<f64>().ok()?,
        other => other.as_f64()?,
    };
    (micros.is_finite() && micros > 0.0).then(|| micros / 1e6 - WINDOWS_TO_COCOA_SECONDS)
}

fn read_engines(
    rows: &Value,
    folder: &str,
    default_guid: Option<&str>,
    plan: &mut ImportPlan,
    skipped: &mut Skipped,
) {
    for row in objects(Some(rows)) {
        let number = |key: &str| row.get(key).and_then(Value::as_i64).unwrap_or(0);
        // Chrome's own rule: an unspecified row counts as the user's once an
        // edit cleared safe_for_autoreplace. 2 is a row the user turned off.
        let active = match number("is_active") {
            1 => true,
            0 => number("safe_for_autoreplace") == 0,
            _ => false,
        };
        if number("prepopulate_id") != 0
            || number("starter_pack_id") != 0
            || number("created_by_policy") != 0
            || !active
        {
            skipped.own_engines += 1;
            continue;
        }
        let url = text(row, "url").map(|u| u.replace("{searchTerms}", "%s"));
        let keyword = text(row, "keyword")
            .map(str::trim)
            .filter(|k| !k.is_empty());
        let (Some(name), Some(keyword), Some(url)) = (text(row, "short_name"), keyword, url) else {
            skipped.unusable_engines += 1;
            continue;
        };
        // Any other {placeholder} is one that Chrome fills in and Atrium cannot.
        if url.contains('{') || !usable_url(&url) {
            skipped.unusable_engines += 1;
            continue;
        }
        plan.engines.push(PlannedEngine {
            name: clamped(name),
            keyword: clamped(keyword),
            url,
            profile_key: folder.into(),
            is_default: default_guid.is_some_and(|guid| text(row, "sync_guid") == Some(guid)),
        });
    }
}
