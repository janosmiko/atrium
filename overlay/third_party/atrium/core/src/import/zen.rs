// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Reads Zen's profiles, containers, workspace sessions and bookmarks into an
//! [`ImportPlan`].
//!
//! C++ decodes each zen-sessions.jsonlz4 before the call, so the files map
//! holds its JSON text under that name. The files come from another
//! browser's profile, so every lookup tolerates a missing or odd value.

use std::collections::{BTreeMap, HashMap, HashSet};

use serde_json::Value;

use super::mozilla::{self, IniProfile, UNIX_TO_COCOA_SECONDS};
use super::places::{self, BookmarkSkips};
use super::{
    clamped, flag, importable_url, objects, text, theme_spec, ImportPlan, PlannedFavorite, PlannedGroup,
    PlannedProfile, PlannedSpace, PlannedTab,
};
use crate::mutate::truncate_to;
use crate::small_stores::{ThemeSpec, ThemeStop};

pub const PROFILES_FILE: &str = "profiles.ini";
pub const SESSIONS_FILE: &str = "zen-sessions.jsonlz4";
pub const CONTAINERS_FILE: &str = "containers.json";

/// A folder chain past this depth stops, like the Arc folder walk.
const MAX_FOLDER_DEPTH: usize = 64;
const MAX_EMOJI_CHARS: usize = 16;
const BOOKMARKS_SPACE: &str = "Bookmarks";

#[derive(Default)]
struct Skipped {
    not_web: usize,
    placeholders: usize,
    orphans: usize,
    other_container: usize,
    nested: usize,
    split_view: bool,
    bookmarks: BookmarkSkips,
    unread_bookmarks: bool,
    bookmarks_space: bool,
}

impl Skipped {
    fn write_notes(&self, notes: &mut Vec<String>) {
        for (count, label) in [
            (self.not_web, "Skipped Zen tabs that are not web pages"),
            (self.placeholders, "Skipped Zen glance and empty tabs"),
            (self.orphans, "Skipped Zen tabs whose workspace is gone"),
            (
                self.other_container,
                "Zen tabs kept in a workspace with another container",
            ),
            (
                self.nested,
                "Zen tabs moved from a nested folder to its top-level folder",
            ),
        ] {
            if count > 0 {
                notes.push(format!("{label}: {count}."));
            }
        }
        if self.split_view {
            notes.push("Zen split views arrive as separate tabs.".into());
        }
        self.bookmarks.write_notes("Zen", notes);
        if self.bookmarks_space {
            notes.push("Zen bookmarks arrive in one Bookmarks space, not in their workspaces.".into());
        }
        if self.unread_bookmarks {
            notes.push(
                "Zen's bookmarks could not be read or are too large. If Zen is open, quit it and import again."
                    .into(),
            );
        }
    }
}

pub fn read(files: &BTreeMap<String, String>) -> Result<ImportPlan, String> {
    let mut plan = ImportPlan {
        source: "zen".into(),
        ..Default::default()
    };
    let ini = match files.get(PROFILES_FILE) {
        Some(text) => mozilla::profiles_ini(text),
        None => {
            plan.notes
                .push("Zen's profiles.ini is missing, so profiles use their folder names.".into());
            mozilla::ProfilesIni::default()
        }
    };
    if ini.outside > 0 {
        plan.notes.push(format!(
            "Skipped Zen profiles outside the Zen folder: {}.",
            ini.outside
        ));
    }
    let suffix = format!("/{SESSIONS_FILE}");
    let folders: Vec<&str> = files
        .keys()
        .filter_map(|k| k.strip_suffix(suffix.as_str()))
        .collect();
    let mut skipped = Skipped::default();
    for &path in &folders {
        let profile = mozilla::folder_profile(&ini, path, folders.len());
        let session_text = &files[&format!("{path}{suffix}")];
        if session_text.is_empty() {
            // C++ sends "" for a file it could not decode, could not read, or
            // that failed the size budget. One message covers all three.
            return Err("Zen's session file is damaged or too large.".into());
        }
        let session: Value = serde_json::from_str(session_text)
            .map_err(|e| format!("Zen's session file could not be read: {e}"))?;
        let containers = match files.get(&format!("{path}/{CONTAINERS_FILE}")) {
            Some(text) => mozilla::container_names(text)?,
            None => BTreeMap::new(),
        };
        let reader = Profile {
            ini: &profile,
            containers: &containers,
        };
        reader.read(&session, &mut plan, &mut skipped);
        if let Some(rows) = files.get(&format!("{path}/{}", places::BOOKMARKS_ROWS)) {
            reader.read_bookmarks(rows, &mut plan, &mut skipped);
        }
    }
    skipped.write_notes(&mut plan.notes);
    Ok(plan)
}

struct Profile<'a> {
    ini: &'a IniProfile,
    containers: &'a BTreeMap<u64, String>,
}

impl Profile<'_> {
    /// The plan key for a container. 0, or a container this profile lacks,
    /// is the folder's own profile.
    fn key(&self, context: u64) -> (String, u64) {
        let context = if self.containers.contains_key(&context) {
            context
        } else {
            0
        };
        (format!("{}#{context}", self.ini.path), context)
    }

    /// Only a container that a workspace or an essential uses becomes a profile.
    fn ensure(&self, plan: &mut ImportPlan, context: u64) -> String {
        let (key, context) = self.key(context);
        if !plan.profiles.iter().any(|p| p.key == key) {
            let (name, is_default) = match self.containers.get(&context) {
                Some(name) => (name.clone(), false),
                None => (self.ini.name.clone(), self.ini.is_default),
            };
            plan.profiles.push(PlannedProfile {
                key: key.clone(),
                name,
                is_default,
            });
        }
        key
    }

    fn read(&self, session: &Value, plan: &mut ImportPlan, skipped: &mut Skipped) {
        let mut spaces: HashMap<&str, usize> = HashMap::new();
        for space in objects(session.get("spaces")) {
            let Some(uuid) = text(space, "uuid") else {
                continue;
            };
            let name = clamped(text(space, "name").unwrap_or("Zen"));
            let theme = space.get("theme").and_then(|t| theme(t, &name));
            let context = space
                .get("containerTabId")
                .and_then(Value::as_u64)
                .unwrap_or(0);
            let profile_key = self.ensure(plan, context);
            spaces.insert(uuid, plan.spaces.len());
            plan.spaces.push(PlannedSpace {
                key: uuid.into(),
                profile_key,
                emoji: text(space, "icon")
                    .filter(|icon| !icon.contains("://") && !icon.chars().any(char::is_control))
                    .map(|icon| truncate_to(icon, MAX_EMOJI_CHARS).into_owned()),
                tint: theme
                    .as_ref()
                    .and_then(|t| t.stops.first())
                    .map(|s| s.color.clone()),
                theme,
                name,
                ..Default::default()
            });
        }
        let folders = Folders::new(session);
        let mut groups: HashMap<(usize, &str), usize> = HashMap::new();
        for tab in objects(session.get("tabs")) {
            if flag(tab, "zenIsGlance") || flag(tab, "zenIsEmpty") {
                skipped.placeholders += 1;
                continue;
            }
            let pinned = flag(tab, "pinned");
            let Some((url, title)) = entry(tab, pinned).and_then(|e| {
                let url = text(e, "url").filter(|u| importable_url(u))?;
                Some((url, clamped(text(e, "title").unwrap_or(url))))
            }) else {
                skipped.not_web += 1;
                continue;
            };
            let context = tab
                .get("userContextId")
                .and_then(Value::as_u64)
                .unwrap_or(0);
            if pinned && flag(tab, "zenEssential") {
                let profile_key = self.ensure(plan, context);
                plan.favorites.push(PlannedFavorite {
                    url: url.into(),
                    title,
                    profile_key,
                });
                continue;
            }
            let Some(&index) = text(tab, "zenWorkspace").and_then(|w| spaces.get(w)) else {
                skipped.orphans += 1;
                continue;
            };
            if self.key(context).0 != plan.spaces[index].profile_key {
                skipped.other_container += 1;
            }
            let mut group = None;
            if let Some((top, name, nested)) = text(tab, "groupId").and_then(|id| folders.top(id)) {
                skipped.nested += usize::from(nested);
                let space = &mut plan.spaces[index];
                group = Some(*groups.entry((index, top)).or_insert_with(|| {
                    space.groups.push(PlannedGroup {
                        name: clamped(name),
                    });
                    space.groups.len() - 1
                }));
            }
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
            let space = &mut plan.spaces[index];
            if pinned {
                space.pinned.push(planned);
            } else {
                space.tabs.push(planned);
            }
        }
        skipped.split_view |= session
            .get("splitViewData")
            .and_then(Value::as_array)
            .is_some_and(|v| !v.is_empty());
    }

    /// Zen's own table that ties a bookmark to a workspace is not read, so a
    /// profile's bookmarks share one space. Its key holds a '/', which a
    /// workspace uuid never does.
    fn read_bookmarks(&self, rows: &str, plan: &mut ImportPlan, skipped: &mut Skipped) {
        // C++ sends "" when the copy or the query failed.
        let Ok(rows) = serde_json::from_str::<Value>(rows) else {
            skipped.unread_bookmarks = true;
            return;
        };
        let mut space = PlannedSpace {
            key: format!("{}#bookmarks", self.ini.path),
            name: BOOKMARKS_SPACE.into(),
            ..Default::default()
        };
        places::read_bookmarks(&rows, &mut space, &mut HashSet::new(), &mut skipped.bookmarks);
        if space.pinned.is_empty() {
            return;
        }
        space.profile_key = self.ensure(plan, 0);
        skipped.bookmarks_space = true;
        plan.spaces.push(space);
    }
}

/// The page a tab shows. A pinned tab keeps the page it was pinned on, which
/// Zen returns to on reset.
fn entry(tab: &Value, pinned: bool) -> Option<&Value> {
    if pinned {
        if let Some(initial) = tab.pointer("/_zenPinnedInitialState/entry") {
            return Some(initial);
        }
    }
    mozilla::session_entry(tab)
}

/// Zen folders and tab groups by id. A tab names one by `groupId`.
struct Folders<'a> {
    by_id: HashMap<&'a str, &'a Value>,
}

impl<'a> Folders<'a> {
    fn new(session: &'a Value) -> Self {
        let by_id = objects(session.get("folders"))
            .chain(objects(session.get("groups")))
            .filter_map(|f| Some((text(f, "id")?, f)))
            .collect();
        Self { by_id }
    }

    /// The top-level folder above `id`: its id, its name, and whether `id`
    /// sat deeper than the top.
    fn top(&self, id: &str) -> Option<(&'a str, &'a str, bool)> {
        let mut folder = *self.by_id.get(id)?;
        let mut depth = 0;
        while let Some(&parent) = text(folder, "parentId").and_then(|p| self.by_id.get(p)) {
            depth += 1;
            if depth > MAX_FOLDER_DEPTH {
                break;
            }
            folder = parent;
        }
        Some((
            text(folder, "id")?,
            text(folder, "name").unwrap_or("Folder"),
            depth > 0,
        ))
    }
}

fn theme(theme: &Value, name: &str) -> Option<ThemeSpec> {
    let colors: Vec<String> = objects(theme.get("gradientColors"))
        .filter_map(|c| rgb_hex(c.get("c")?))
        .collect();
    if colors.is_empty() {
        return None;
    }
    let count = colors.len() as f64;
    let stops = colors
        .into_iter()
        .enumerate()
        .map(|(i, color)| ThemeStop {
            x: (i as f64 + 0.5) / count,
            y: 0.5,
            color,
            alpha: 1.0,
        })
        .collect();
    let opacity = theme
        .get("opacity")
        .and_then(Value::as_f64)
        .unwrap_or(1.0)
        .clamp(0.0, 1.0);
    Some(theme_spec(name, stops, opacity, 0.0))
}

/// Zen stores a colour as `[r, g, b]` in 0..255.
fn rgb_hex(c: &Value) -> Option<String> {
    let channel = |i: usize| {
        c.get(i)?
            .as_f64()
            .map(|v| v.clamp(0.0, 255.0).round() as u8)
    };
    Some(format!(
        "#{:02X}{:02X}{:02X}",
        channel(0)?,
        channel(1)?,
        channel(2)?
    ))
}
