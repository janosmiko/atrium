// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Reads Arc's sidebar, routing and profile files into an [`ImportPlan`].
//!
//! Arc can write these files while the read runs, and the user controls them,
//! so every lookup here tolerates a missing or odd-shaped value.

use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};

use serde_json::Value;

use super::{
    ImportPlan, PlannedFavorite, PlannedGroup, PlannedProfile, PlannedRoute, PlannedSpace,
    PlannedTab, clamped, info_cache_names, objects, text, theme_spec,
};
use crate::mutate::truncate_to;
use crate::small_stores::{ThemeSpec, ThemeStop};

pub const SIDEBAR_FILE: &str = "StorableSidebar.json";
pub const ROUTING_FILE: &str = "StorableLinkRouting.json";
pub const LOCAL_STATE_FILE: &str = "Local State";

const DEFAULT_KEY: &str = "default";
const SYSTEM_PROFILE: &str = "__ARC_SYSTEM_PROFILE";
/// A folder chain past this depth stops, so an untrusted file with a long
/// linear chain of nested folders cannot recurse until the stack overflows.
const MAX_FOLDER_DEPTH: usize = 64;
/// The store has no emoji cap. The longest real emoji sequence is about ten
/// code points, so this only stops a runaway string.
const MAX_EMOJI_CHARS: usize = 16;

pub fn read(files: &BTreeMap<String, String>) -> Result<ImportPlan, String> {
    let mut plan = ImportPlan { source: "arc".into(), ..Default::default() };
    let names = match files.get(LOCAL_STATE_FILE) {
        Some(text) => {
            let mut names = info_cache_names(&parse(LOCAL_STATE_FILE, text)?);
            names.remove(SYSTEM_PROFILE);
            names
        }
        None => {
            plan.notes.push("Arc's Local State file is missing, so profiles use their folder names.".into());
            BTreeMap::new()
        }
    };
    match files.get(SIDEBAR_FILE) {
        Some(text) => read_sidebar(&parse(SIDEBAR_FILE, text)?, &names, &mut plan),
        None => plan.notes.push("Arc's sidebar file is missing, so no spaces were found.".into()),
    }
    match files.get(ROUTING_FILE) {
        Some(text) => read_routes(&parse(ROUTING_FILE, text)?, &mut plan),
        None => plan.notes.push("Arc's routing file is missing, so no routing rules were found.".into()),
    }
    Ok(plan)
}

fn parse(name: &str, text: &str) -> Result<Value, String> {
    serde_json::from_str(text).map_err(|e| format!("Arc's {name} could not be read: {e}"))
}

/// `{"default": true}` or `{"custom": {"_0": {"directoryBasename": ...}}}`.
fn profile_key(profile: &Value) -> Option<String> {
    if profile.get("default").is_some() {
        return Some(DEFAULT_KEY.into());
    }
    profile.pointer("/custom/_0/directoryBasename").and_then(Value::as_str).map(str::to_string)
}

fn ensure_profile(plan: &mut ImportPlan, key: &str, names: &BTreeMap<String, String>) {
    if plan.profiles.iter().any(|p| p.key == key) {
        return;
    }
    let is_default = key == DEFAULT_KEY;
    let name = if is_default {
        names.get("Default").cloned().unwrap_or_else(|| "Default".into())
    } else {
        names.get(key).cloned().unwrap_or_else(|| format!("Arc {key}"))
    };
    plan.profiles.push(PlannedProfile { key: key.into(), name, is_default });
}

fn read_sidebar(root: &Value, names: &BTreeMap<String, String>, plan: &mut ImportPlan) {
    let Some(container) = root
        .pointer("/sidebar/containers")
        .and_then(Value::as_array)
        .and_then(|all| all.iter().find(|c| c.get("spaces").is_some()))
    else {
        plan.notes.push("Arc's sidebar file holds no spaces.".into());
        return;
    };
    let items: HashMap<&str, &Value> = objects(container.get("items"))
        .filter_map(|item| Some((item.get("id")?.as_str()?, item)))
        .collect();
    let mut walk = Walk {
        items: &items,
        visited: HashSet::new(),
        skipped: BTreeSet::new(),
        flattened: false,
        depth_exceeded: false,
    };
    for space in objects(container.get("spaces")) {
        if let Some(planned) = walk.space(space, names, plan) {
            plan.spaces.push(planned);
        }
    }
    walk.favorites(container.get("topAppsContainerIDs"), names, plan);
    if walk.flattened {
        plan.notes.push("Arc folders inside folders were moved into their top folder.".into());
    }
    if walk.depth_exceeded {
        plan.notes.push(format!("Arc folders nested more than {MAX_FOLDER_DEPTH} deep were not followed further."));
    }
    for kind in walk.skipped {
        plan.notes.push(format!("Skipped Arc items of kind {kind}."));
    }
}

struct Walk<'a> {
    items: &'a HashMap<&'a str, &'a Value>,
    /// A cycle in childrenIds would otherwise recurse until the stack ends.
    visited: HashSet<&'a str>,
    skipped: BTreeSet<String>,
    flattened: bool,
    depth_exceeded: bool,
}

impl<'a> Walk<'a> {
    fn space(
        &mut self,
        space: &Value,
        names: &BTreeMap<String, String>,
        plan: &mut ImportPlan,
    ) -> Option<PlannedSpace> {
        let key = text(space, "id")?.to_string();
        let profile_key = profile_key(space.get("profile")?)?;
        ensure_profile(plan, &profile_key, names);
        let ids = space.get("containerIDs").and_then(Value::as_array);
        let container = |label: &str| {
            let ids = ids?;
            let at = ids.iter().position(|v| v.as_str() == Some(label))?;
            ids.get(at + 1)?.as_str()
        };
        let mut planned = PlannedSpace {
            key,
            name: clamped(text(space, "title").unwrap_or("Arc space")),
            profile_key,
            ..Default::default()
        };
        let mut groups = Vec::new();
        if let Some(id) = container("pinned") {
            planned.pinned = self.tree(id, &mut groups);
        }
        if let Some(id) = container("unpinned") {
            planned.tabs = self.tree(id, &mut groups);
        }
        planned.groups = groups;
        if let Some(info) = space.get("customInfo") {
            planned.emoji = emoji(info);
            planned.tint = info.pointer("/windowTheme/primaryColorPalette/midTone").and_then(color_hex);
            planned.theme = info
                .pointer("/windowTheme/background/single/_0/style/color/_0")
                .and_then(|bg| theme(bg, &planned.name));
        }
        Some(planned)
    }

    fn tree(&mut self, container_id: &str, groups: &mut Vec<PlannedGroup>) -> Vec<PlannedTab> {
        let mut out = Vec::new();
        for child in self.children(container_id) {
            self.item(child, None, 0, groups, &mut out);
        }
        out
    }

    fn children(&self, id: &str) -> Vec<&'a Value> {
        self.items
            .get(id)
            .and_then(|item| item.get("childrenIds"))
            .and_then(Value::as_array)
            .into_iter()
            .flatten()
            .filter_map(Value::as_str)
            .filter_map(|child| self.items.get(child).copied())
            .collect()
    }

    fn item(
        &mut self,
        item: &'a Value,
        group: Option<usize>,
        depth: usize,
        groups: &mut Vec<PlannedGroup>,
        out: &mut Vec<PlannedTab>,
    ) {
        if depth > MAX_FOLDER_DEPTH {
            self.depth_exceeded = true;
            return;
        }
        let Some(id) = text(item, "id") else { return };
        if !self.visited.insert(id) {
            return;
        }
        let Some(data) = item.get("data").and_then(Value::as_object) else { return };
        if let Some(tab) = data.get("tab") {
            let Some(url) = text(tab, "savedURL") else { return };
            let title = text(item, "title").or_else(|| text(tab, "savedTitle")).unwrap_or(url);
            out.push(PlannedTab {
                url: url.into(),
                title: clamped(title),
                last_active_at: tab.get("timeLastActiveAt").and_then(Value::as_f64),
                group,
            });
        } else if data.contains_key("list") {
            let group = match group {
                Some(top) => {
                    self.flattened = true;
                    Some(top)
                }
                None => {
                    groups.push(PlannedGroup { name: clamped(text(item, "title").unwrap_or("Folder")) });
                    Some(groups.len() - 1)
                }
            };
            for child in self.children(id) {
                self.item(child, group, depth + 1, groups, out);
            }
        } else if let Some(kind) = data.keys().next() {
            self.skipped.insert(kind.clone());
        }
    }

    fn favorites(
        &mut self,
        list: Option<&'a Value>,
        names: &BTreeMap<String, String>,
        plan: &mut ImportPlan,
    ) {
        let Some(list) = list.and_then(Value::as_array) else { return };
        let mut foreign = false;
        for pair in list.chunks(2) {
            let [descriptor, id] = pair else { continue };
            let (Some(key), Some(id)) = (profile_key(descriptor), id.as_str()) else { continue };
            // A profile with spaces here is on this Mac, named or not.
            let local = key == DEFAULT_KEY
                || names.contains_key(&key)
                || plan.profiles.iter().any(|p| p.key == key);
            if !local {
                foreign = true;
                continue;
            }
            ensure_profile(plan, &key, names);
            let mut ignored_groups = Vec::new();
            for tab in self.tree(id, &mut ignored_groups) {
                plan.favorites.push(PlannedFavorite { url: tab.url, title: tab.title, profile_key: key.clone() });
            }
        }
        if foreign {
            plan.notes.push("Skipped favourites from an Arc profile on another Mac.".into());
        }
    }
}

fn emoji(info: &Value) -> Option<String> {
    let icon = info.get("iconType")?;
    if let Some(e) = text(icon, "emoji_v2") {
        return Some(truncate_to(e, MAX_EMOJI_CHARS).into_owned());
    }
    let code = u32::try_from(icon.get("emoji")?.as_u64()?).ok()?;
    char::from_u32(code).filter(|c| !c.is_control()).map(String::from)
}

/// Arc stores extended sRGB, so a channel can sit outside 0..1.
fn color_hex(color: &Value) -> Option<String> {
    let channel = |name: &str| {
        color.get(name).and_then(Value::as_f64).map(|v| (v.clamp(0.0, 1.0) * 255.0).round() as u8)
    };
    Some(format!("#{:02X}{:02X}{:02X}", channel("red")?, channel("green")?, channel("blue")?))
}

fn theme(background: &Value, name: &str) -> Option<ThemeSpec> {
    let (colors, modifiers): (Vec<&Value>, Option<&Value>) =
        if let Some(gradient) = background.pointer("/blendedGradient/_0") {
            (gradient.get("baseColors")?.as_array()?.iter().collect(), gradient.get("modifiers"))
        } else {
            let single = background.pointer("/blendedSingleColor/_0")?;
            (vec![single.get("color")?], single.get("modifiers"))
        };
    let count = colors.len() as f64;
    let stops: Vec<ThemeStop> = colors
        .iter()
        .enumerate()
        .filter_map(|(i, c)| {
            Some(ThemeStop {
                x: (i as f64 + 0.5) / count,
                y: 0.5,
                color: color_hex(c)?,
                alpha: c.get("alpha").and_then(Value::as_f64).unwrap_or(1.0).clamp(0.0, 1.0),
            })
        })
        .collect();
    if stops.is_empty() {
        return None;
    }
    let grain = modifiers
        .and_then(|m| m.get("noiseFactor"))
        .and_then(Value::as_f64)
        .unwrap_or(0.0)
        .clamp(0.0, 1.0);
    Some(theme_spec(name, stops, 1.0, grain))
}

fn read_routes(root: &Value, plan: &mut ImportPlan) {
    let mut skipped = 0;
    let mut widened = false;
    for rule in root.get("rules").and_then(Value::as_array).into_iter().flatten() {
        match route(rule) {
            Some((planned, from_path)) => {
                widened |= from_path;
                plan.routes.push(planned);
            }
            None => skipped += 1,
        }
    }
    if widened {
        plan.notes.push(
            "Arc rules that match part of a path became regex rules. Atrium's contains rule reads only the host."
                .into(),
        );
    }
    if skipped > 0 {
        plan.notes.push(format!("Skipped Arc routing rules that Atrium cannot read: {skipped}."));
    }
}

/// The rule and whether a `contains` pattern with a path became a regex.
fn route(rule: &Value) -> Option<(PlannedRoute, bool)> {
    let url_match = rule
        .get("sourceComponents")?
        .as_array()?
        .iter()
        .find_map(|c| c.pointer("/componentType/urlMatch"))?;
    let pattern = url_match.get("_1")?.as_str()?.to_string();
    let kind = url_match.get("_0")?;
    let destination = rule.pointer("/destination/space/_0")?;
    let target_space_key = if destination.get("mostRecent").is_some() {
        None
    } else {
        Some(destination.pointer("/space/_0")?.as_str()?.to_string())
    };
    if kind.get("matches").is_some() {
        return Some((PlannedRoute { match_type: "regex".into(), pattern, target_space_key }, false));
    }
    kind.get("contains")?;
    // Arc's contains is a substring of the whole URL. Atrium's reads the host
    // on a label boundary, so a path pattern would match nothing there.
    if pattern.contains('/') {
        let pattern = regex_lite::escape(&pattern);
        return Some((PlannedRoute { match_type: "regex".into(), pattern, target_space_key }, true));
    }
    Some((PlannedRoute { match_type: "contains".into(), pattern, target_space_key }, false))
}
