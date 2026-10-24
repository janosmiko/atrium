// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The four smaller stores: themes, hotkeys, routing, and the command bar's
//! selection memory.
//!
//! Each is a straight port of the Swift store's load path. They share one file
//! because none is big enough to be worth its own, and reading them side by
//! side makes the one difference between them obvious: the selection memory
//! has never carried a schema version at all.

use std::collections::BTreeMap;

use serde::{Deserialize, Deserializer, Serialize, Serializer};

use crate::schema;

// ---------------------------------------------------------------- themes

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ThemeStop {
    pub x: f64,
    pub y: f64,
    pub color: String,
    pub alpha: f64,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ThemeEffects {
    pub grain: f64,
    pub vignette: f64,
    pub frost: f64,
    pub warmth: f64,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ThemeSpec {
    pub name: String,
    pub stops: Vec<ThemeStop>,
    pub opacity: f64,
    pub effects: ThemeEffects,
    pub dark_adaptation: f64,
    /// Was a global setting, now per workspace. Default false so a themes.json
    /// written before the move decodes clean.
    #[serde(default)]
    pub night_shift_enabled: bool,
    #[serde(default)]
    pub ambient_enabled: bool,
    pub version: i64,
}

/// A BTreeMap, not a HashMap: the key order is what gets written back, and a
/// random order makes every save look like a change.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ThemesFile {
    pub schema_version: i64,
    /// Absent entirely on a profile that never opened Theme Studio.
    #[serde(default)]
    pub themes: BTreeMap<String, ThemeSpec>,
}

impl Default for ThemesFile {
    fn default() -> Self {
        Self {
            schema_version: schema::current::THEMES,
            themes: BTreeMap::new(),
        }
    }
}

/// Deserialises a small store, falling back to defaults twice over: once when
/// the bytes do not parse, and once when the file's own `schemaVersion` is one
/// this build cannot read.
///
/// The second fallback is the read half of `docs/model-rules.md` R1.2, and it
/// once lived in three copies - so a change to how an unreadable
/// version is handled had to be made three times or would be made once. The
/// small stores had no writer at all, which is why defaults were safe here in
/// a way they are not for `spaces` (see `schema::LoadError`). Routing has one
/// now (`load_routing_checked`), and it does not call this - it needs the
/// refusal, not the default.
fn load_versioned<T>(json: &str, current: i64) -> Option<T>
where
    T: serde::de::DeserializeOwned + HasSchemaVersion,
{
    let file = serde_json::from_str::<T>(json).ok()?;
    schema::can_load(file.schema_version(), current).then_some(file)
}

/// What `load_versioned` needs from a store: where its version is kept.
trait HasSchemaVersion {
    fn schema_version(&self) -> i64;
}

pub fn load_themes(json: &str) -> ThemesFile {
    load_versioned(json, schema::current::THEMES).unwrap_or_default()
}

/// The write half `load_themes` cannot be - see `load_routing_checked`.
pub fn load_themes_checked(json: &str) -> Result<ThemesFile, schema::LoadError> {
    let file: ThemesFile =
        serde_json::from_str(json).map_err(|e| schema::LoadError::Unreadable(e.to_string()))?;
    if !schema::can_load(file.schema_version, schema::current::THEMES) {
        return Err(schema::LoadError::Refused {
            file_version: file.schema_version,
        });
    }
    Ok(file)
}

// ------------------------------------------------- routing: the stored rules

/// A rule target that keeps the tab where the navigation started. A space id
/// is a UUID, so the word cannot collide with one.
pub(crate) const CURRENT_SPACE_TARGET: &str = "current";

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct RoutingRule {
    pub id: String,
    pub enabled: bool,
    pub match_type: String,
    pub pattern: String,
    #[serde(rename = "targetSpaceID")]
    pub target_space_id: String,
    #[serde(rename = "excludedSourceSpaceIDs", default)]
    pub excluded_source_space_ids: Vec<String>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RoutingFile {
    pub schema_version: i64,
    #[serde(rename = "defaultTargetSpaceID", default)]
    pub default_target_space_id: String,
    #[serde(default)]
    pub(crate) rules: Vec<RoutingRule>,
}

impl Default for RoutingFile {
    fn default() -> Self {
        Self {
            schema_version: schema::current::ROUTING,
            default_target_space_id: String::new(),
            rules: Vec::new(),
        }
    }
}

impl RoutingFile {
    pub(crate) fn has_rule(&self, match_type: &str, pattern: &str, target: &str) -> bool {
        self.rules.iter().any(|r| {
            r.match_type == match_type && r.pattern == pattern && r.target_space_id == target
        })
    }

    /// Refuses a rule `route_for_url` would drop anyway, so the file never
    /// stores a rule that routes nothing.
    pub(crate) fn append_rule(
        &mut self,
        id: &str,
        match_type: &str,
        pattern: &str,
        target: &str,
    ) -> bool {
        let usable = match match_type {
            "contains" => !pattern.is_empty(),
            "regex" => !pattern.is_empty() && compile_rule_regex(pattern).is_some(),
            _ => false,
        };
        if !usable || self.has_rule(match_type, pattern, target) {
            return false;
        }
        self.rules.push(RoutingRule {
            id: id.to_string(),
            enabled: true,
            match_type: match_type.to_string(),
            pattern: pattern.to_string(),
            target_space_id: target.to_string(),
            excluded_source_space_ids: Vec::new(),
        });
        true
    }
}

pub fn load_routing(json: &str) -> RoutingFile {
    load_versioned(json, schema::current::ROUTING).unwrap_or_default()
}

/// The write half `load_routing` cannot be: it says WHY a load produced
/// nothing, which a writer needs to tell a fresh profile's empty rules apart
/// from a file this build must not replace (`docs/model-rules.md` R1.2).
pub fn load_routing_checked(json: &str) -> Result<RoutingFile, schema::LoadError> {
    let file: RoutingFile =
        serde_json::from_str(json).map_err(|e| schema::LoadError::Unreadable(e.to_string()))?;
    if !schema::can_load(file.schema_version, schema::current::ROUTING) {
        return Err(schema::LoadError::Refused {
            file_version: file.schema_version,
        });
    }
    Ok(file)
}

// ------------------------------------------------- routing: resolving a url

/// The space a URL should open in, or `None` when no rule claims it.
///
/// Array order is priority and the first ENABLED match wins, which is what the
/// settings list shows a user - so a rule dragged above another has to change
/// the answer.
///
/// `contains` tests the HOST only, case-insensitively, and it matches on a LABEL
/// boundary: the host either equals the pattern or ends with `.` plus the
/// pattern. It is not a bare substring, and a leading `*.` is not a wildcard - it
/// matches the characters `*.` and so matches nothing.
///
/// The boundary is the point. A substring match let a rule for
/// `corp.example` claim `corp.example.attacker.test`, which routes an attacker's
/// page into the corp profile's window - the one thing a routing rule must never
/// do. `notcorp.example` is refused for the same reason.
///
/// `regex` tests the whole URL, because that is the only form that can express a
/// path or query rule, and it is compiled with a size limit. A pattern that does
/// not compile - a typo, or one too large for the budget - drops its own rule and
/// leaves its siblings alone. A rule whose `excludedSourceSpaceIDs` names
/// `source_space_id` is skipped. No hit falls back to `defaultTargetSpaceID` when set.
pub fn route_for_url<'a>(
    file: &'a RoutingFile,
    url: &str,
    source_space_id: Option<&str>,
) -> Option<&'a str> {
    let host = host_of(url).unwrap_or("").to_lowercase();
    let lowered = url.to_lowercase();
    for rule in &file.rules {
        if !rule.enabled || rule.pattern.is_empty() {
            continue;
        }
        if let Some(source) = source_space_id {
            if rule.excluded_source_space_ids.iter().any(|id| id == source) {
                continue;
            }
        }
        let hit = match rule.match_type.as_str() {
            "contains" => host_matches(&host, &rule.pattern.to_lowercase()),
            "regex" => compile_rule_regex(&rule.pattern)
                .map(|re| re.is_match(&lowered))
                .unwrap_or(false),
            _ => false,
        };
        if hit {
            if rule.target_space_id == CURRENT_SPACE_TARGET {
                return None;
            }
            return Some(&rule.target_space_id);
        }
    }
    if file.default_target_space_id.is_empty() {
        None
    } else {
        Some(&file.default_target_space_id)
    }
}

/// What a compiled routing regex may cost, in bytes.
///
/// regex_lite enables a default limit already; this states one so the budget is a
/// decision rather than a version's default, and so a rule set cannot make every
/// navigation pay for a pathological pattern. A rule over the limit fails to
/// build, which the caller already treats as "this rule drops out".
const MAX_COMPILED_REGEX_BYTES: usize = 64 * 1024;

/// A routing rule's regex, or None when it will not compile inside the budget.
///
/// Separate from route_for_url so the budget itself is testable: a test that only
/// went through route_for_url could not tell an over-budget pattern from one that
/// simply did not match, and the first version of that test proved nothing.
pub(crate) fn compile_rule_regex(pattern: &str) -> Option<regex_lite::Regex> {
    regex_lite::RegexBuilder::new(pattern)
        .case_insensitive(true)
        .size_limit(MAX_COMPILED_REGEX_BYTES)
        .build()
        .ok()
}

/// Whether `host` is `pattern` or a subdomain of it, both already lowercased.
///
/// Label boundary, not substring - see route_for_url's doc for what that stops.
fn host_matches(host: &str, pattern: &str) -> bool {
    if host.is_empty() || pattern.is_empty() {
        return false;
    }
    if host == pattern {
        return true;
    }
    // The '.' has to be part of what is stripped, or "notcorp.example" would
    // match "corp.example" on the tail alone.
    host.len() > pattern.len()
        && host.ends_with(pattern)
        && host.as_bytes()[host.len() - pattern.len() - 1] == b'.'
}

/// The host of `url`, or an empty string when there is none to read.
///
/// Hand-rolled rather than pulled from a URL crate: the callers are Chromium,
/// which has already parsed and normalised the URL before it reaches here, so
/// this only has to find the authority between `//` and the next `/?#`.
/// Shared with `sidebar`, which falls back to the whole URL on `None`.
pub(crate) fn host_of(url: &str) -> Option<&str> {
    let after_scheme = &url[url.find("://")? + 3..];
    let end = after_scheme
        .find(['/', '?', '#'])
        .unwrap_or(after_scheme.len());
    let authority = &after_scheme[..end];
    // Credentials, if any, are not part of the host - a rule matching "corp"
    // must not fire on https://corp@evil.example/.
    let host_and_port = match authority.rfind('@') {
        Some(at) => &authority[at + 1..],
        None => authority,
    };
    // A port is not part of the host either - a test server's random port
    // must not defeat a rule typed as a plain hostname.
    let host = if let Some(rest) = host_and_port.strip_prefix('[') {
        // An IPv6 literal's own colons are not the port separator.
        match rest.find(']') {
            Some(at) => &host_and_port[..at + 2],
            None => host_and_port,
        }
    } else {
        match host_and_port.rfind(':') {
            Some(at) => &host_and_port[..at],
            None => host_and_port,
        }
    };
    if host.is_empty() { None } else { Some(host) }
}

// --------------------------------------------------------------- hotkeys

/// Which shipped table the file's own map merges over. The NAME is stored,
/// not the chords, so a later change to a preset still reaches its users.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub(crate) enum HotkeyPreset {
    Arc,
    #[default]
    Atrium,
}

impl HotkeyPreset {
    pub const ALL: &'static [Self] = &[Self::Arc, Self::Atrium];

    pub fn name(self) -> &'static str {
        match self {
            Self::Arc => "arc",
            Self::Atrium => "atrium",
        }
    }

    pub fn from_name(name: &str) -> Option<Self> {
        Self::ALL.iter().copied().find(|p| p.name() == name)
    }

    fn is_shipped(&self) -> bool {
        *self == Self::default()
    }
}

impl Serialize for HotkeyPreset {
    fn serialize<S: Serializer>(&self, serializer: S) -> Result<S::Ok, S::Error> {
        serializer.serialize_str(self.name())
    }
}

impl<'de> Deserialize<'de> for HotkeyPreset {
    /// A name this build does not know costs its own meaning and nothing else,
    /// the leniency an unknown action id already gets.
    fn deserialize<D: Deserializer<'de>>(deserializer: D) -> Result<Self, D::Error> {
        Ok(Self::from_name(&String::deserialize(deserializer)?).unwrap_or_default())
    }
}

/// The combos are strings like `cmd+shift+right`, and the action keys are
/// strings too, so nothing here has to know what an action means. That is
/// deliberate: the leniency rule below is about not understanding a value.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HotkeysFile {
    pub schema_version: i64,
    #[serde(default)]
    pub actions: BTreeMap<String, Vec<String>>,
    /// Absent is how every file written before presets existed spells Atrium,
    /// so writing the shipped name back would rewrite all of them for nothing.
    #[serde(default, skip_serializing_if = "HotkeyPreset::is_shipped")]
    pub(crate) preset: HotkeyPreset,
}

/// Every action name that exists. NOT the same as the set with defaults -
/// `createSpace` has no binding to ship yet and is still perfectly real.
/// Reading "known" off the defaults map instead would drop a user's own
/// binding for it as if it were a typo.
pub const HOTKEY_ACTIONS: &[&str] = &[
    "goToSpace1",
    "goToSpace2",
    "goToSpace3",
    "goToSpace4",
    "goToSpace5",
    "goToSpace6",
    "goToSpace7",
    "goToSpace8",
    "goToSpace9",
    "nextSpace",
    "previousSpace",
    "openCommandBar",
    "toggleSidebar",
    "nextTab",
    "previousTab",
    "editCurrentURL",
    "reload",
    "hardReload",
    "reloadClearingCache",
    "reloadClearingSiteData",
    "littleWindow",
    "openArchive",
    "clearTabs",
    "toggleOverview",
    "createSpace",
    "pinToggleTab",
    "closeSplitPane",
    "toggleSplitOrientation",
    "toggleDeveloperMode",
    "toggleDevBar",
    "goToSidebarItem1",
    "goToSidebarItem2",
    "goToSidebarItem3",
    "goToSidebarItem4",
    "goToSidebarItem5",
    "goToSidebarItem6",
    "goToSidebarItem7",
    "goToSidebarItem8",
    "goToSidebarItemLast",
    "toggleAiPanel",
];

/// The bindings Atrium ships with. An action absent from a file falls back to
/// its entry here; an action present with an empty list is a deliberate
/// unbind and stays empty.
///
/// Never map an action to an EMPTY list. This map is also what seeds the
/// native hotkey table, which leaves an absent action on its compiled-in combo
/// and reads an empty list as an unbind - so an empty default would release a
/// key the build ships bound. Absent is how "no default" is spelled here.
fn default_hotkeys() -> BTreeMap<String, Vec<String>> {
    let mut out: BTreeMap<String, Vec<String>> = BTreeMap::new();
    let mut put = |action: &str, combos: &[&str]| {
        out.insert(
            action.to_string(),
            combos.iter().map(|c| c.to_string()).collect(),
        );
    };
    // The plain digits are the spaces. They held the sidebar items only while
    // goToSpaceN had no native action to run.
    for n in 1..=9 {
        put(&format!("goToSpace{n}"), &[&format!("cmd+{n}")]);
    }
    for n in 1..=8 {
        put(&format!("goToSidebarItem{n}"), &[&format!("cmd+opt+{n}")]);
    }
    // Arc's ninth key is the LAST item, whatever the count, and the core has
    // named it that since the store was written.
    put("goToSidebarItemLast", &["cmd+opt+9"]);
    put(
        "nextSpace",
        &[
            "opt+tab",
            "cmd+opt+right",
            "opt+ctrl+right",
            "cmd+shift+right",
        ],
    );
    put(
        "previousSpace",
        &[
            "shift+opt+tab",
            "cmd+opt+left",
            "opt+ctrl+left",
            "cmd+shift+left",
        ],
    );
    put("nextTab", &["cmd+opt+down", "cmd+j", "cmd+shift+]"]);
    put("previousTab", &["cmd+opt+up", "cmd+k", "cmd+shift+["]);
    // Cmd+Option+R is Safari's Reload Page From Origin, borrowed because no
    // Chromium browser binds a chord to IDC_RELOAD_CLEARING_CACHE at all.
    put("reload", &["cmd+r"]);
    put("hardReload", &["cmd+shift+r"]);
    put("reloadClearingCache", &["cmd+opt+r"]);
    put("reloadClearingSiteData", &["cmd+shift+opt+r"]);
    put("openCommandBar", &["cmd+t"]);
    put("toggleSidebar", &["cmd+s"]);
    put("toggleOverview", &["cmd+shift+o"]);
    put("openArchive", &["cmd+shift+a"]);
    put("clearTabs", &["cmd+shift+k"]);
    put("pinToggleTab", &["cmd+d"]);
    put("editCurrentURL", &["cmd+l"]);
    put("littleWindow", &["cmd+opt+n"]);
    put("closeSplitPane", &["shift+ctrl+-"]);
    put("toggleSplitOrientation", &["shift+ctrl+\\"]);
    put("toggleDeveloperMode", &["opt+ctrl+d"]);
    put("toggleDevBar", &["cmd+shift+d"]);
    put("toggleAiPanel", &["cmd+shift+i"]);
    out
}

/// Arc's chords for Atrium's actions, per `docs/hotkeys/arc-defaults.md`. An
/// unassigned row is an EMPTY list, which releases the compiled-in key.
fn arc_hotkeys() -> BTreeMap<String, Vec<String>> {
    let mut out: BTreeMap<String, Vec<String>> = BTreeMap::new();
    let mut put = |action: &str, combos: &[&str]| {
        out.insert(
            action.to_string(),
            combos.iter().map(|c| c.to_string()).collect(),
        );
    };
    for n in 1..=9 {
        put(&format!("goToSpace{n}"), &[&format!("cmd+{n}")]);
    }
    for n in 1..=8 {
        put(&format!("goToSidebarItem{n}"), &[]);
    }
    put("goToSidebarItemLast", &[]);
    put("nextSpace", &["cmd+l"]);
    put("previousSpace", &["cmd+h"]);
    put("nextTab", &["cmd+shift+]"]);
    put("previousTab", &["cmd+shift+["]);
    put("openCommandBar", &[]);
    put("editCurrentURL", &[]);
    put("toggleSidebar", &["cmd+s"]);
    put("reload", &["cmd+r"]);
    put("hardReload", &["cmd+shift+r"]);
    put("reloadClearingCache", &[]);
    put("reloadClearingSiteData", &[]);
    put("littleWindow", &["cmd+opt+n"]);
    put("openArchive", &[]);
    // Arc's Clear Unpinned Tabs.
    put("clearTabs", &["cmd+shift+k"]);
    put("toggleOverview", &[]);
    put("pinToggleTab", &["cmd+d"]);
    put("closeSplitPane", &["shift+ctrl+-"]);
    put("toggleSplitOrientation", &[]);
    put("toggleDeveloperMode", &["opt+ctrl+d"]);
    put("toggleDevBar", &["cmd+shift+d"]);
    // Arc has no AI panel to match.
    put("toggleAiPanel", &[]);
    out
}

fn preset_hotkeys(preset: HotkeyPreset) -> BTreeMap<String, Vec<String>> {
    match preset {
        HotkeyPreset::Arc => arc_hotkeys(),
        HotkeyPreset::Atrium => default_hotkeys(),
    }
}

/// Every preset by name, which is what the Inputs pane's picker applies. The
/// pane holds all of them so picking one is not a second round trip.
pub fn hotkey_preset_tables() -> BTreeMap<String, BTreeMap<String, Vec<String>>> {
    HotkeyPreset::ALL
        .iter()
        .map(|preset| (preset.name().to_string(), preset_hotkeys(*preset)))
        .collect()
}

impl Default for HotkeysFile {
    fn default() -> Self {
        Self {
            schema_version: schema::current::HOTKEYS,
            actions: default_hotkeys(),
            preset: HotkeyPreset::default(),
        }
    }
}

const NAMED_KEYS: &[&str] = &["tab", "escape", "space", "left", "right", "up", "down"];
const MODIFIERS: &[&str] = &["cmd", "shift", "opt", "ctrl"];

/// `cmd+shift+t` parsed to its canonical form, or None.
///
/// The LAST segment is the key; every segment before it must be a known
/// modifier. Modifier order in the output is fixed - cmd, shift, opt, ctrl -
/// so two spellings of one combo compare equal as strings.
fn parse_combo(raw: &str) -> Option<String> {
    let lowered = raw.to_lowercase();
    let segments: Vec<&str> = lowered.split('+').collect();
    let key = segments.last()?;
    if key.is_empty() {
        return None;
    }
    if key.chars().count() != 1 && !NAMED_KEYS.contains(key) {
        return None;
    }
    let mut present = [false; 4];
    for segment in &segments[..segments.len() - 1] {
        let index = MODIFIERS.iter().position(|m| m == segment)?;
        present[index] = true;
    }
    let mut out: Vec<&str> = Vec::with_capacity(5);
    for (i, m) in MODIFIERS.iter().enumerate() {
        if present[i] {
            out.push(m);
        }
    }
    out.push(key);
    Some(out.join("+"))
}

/// Moves a saved file still on the OLD digit layout onto the new one.
///
/// A current-schema file REPLACES the defaults, so a stale
/// `goToSidebarItemN: ["cmd+N"]` row would hold cmd+N while goToSpaceN
/// defaults to it too. The native map breaks that tie by declaration order
/// and the sidebar item wins, leaving the new binding silently dead.
///
/// Only a row still EQUAL to the old default moves, and only onto a free
/// chord. Runs on the file's own rows, before the v1 union appends the new
/// default and makes a stale row unrecognisable.
fn migrate_digits_to_spaces(stored: &mut BTreeMap<String, Vec<String>>) {
    for n in 1..=9 {
        let action = if n == 9 {
            "goToSidebarItemLast".to_string()
        } else {
            format!("goToSidebarItem{n}")
        };
        let moved_to = format!("cmd+opt+{n}");
        if !stored
            .get(&action)
            .is_some_and(|c| c == &[format!("cmd+{n}")])
        {
            continue;
        }
        // Moving onto a chord the user put somewhere else would rebuild the
        // two-actions-one-combo clash this whole change exists to remove.
        if stored
            .iter()
            .any(|(other, combos)| other != &action && combos.contains(&moved_to))
        {
            continue;
        }
        stored.insert(action, vec![moved_to]);
    }
}

/// A v1 file is UNIONED with the current defaults; a current-schema file
/// REPLACES them.
///
/// v1 predates the Arc-parity pass, which added most of its combos as EXTRA
/// entries on actions that already existed. Replace semantics meant any
/// machine with an older file silently lost every one of them - the
/// reported "a lot of hotkeys are not working".
///
/// The union is only for v1, because v1 cannot express "I removed this default
/// on purpose". Once the file is written at the current schema it is
/// authoritative, so a combo deleted in Settings stays deleted.
pub fn load_hotkeys(json: &str) -> HotkeysFile {
    // Through the shared guard, then the legacy fill below - which is why this
    // one keeps a body where themes and routing are a single call. `None` here
    // means refused or unparseable, and filling legacy rows into defaults would
    // invent bindings for a file this build declined to read.
    let Some(file) = load_versioned::<HotkeysFile>(json, schema::current::HOTKEYS) else {
        return HotkeysFile::default();
    };

    let is_legacy = file.schema_version < schema::current::HOTKEYS;

    // Read the whole file first: the digit migration below has to see every
    // row to tell whether the chord it wants to move onto is already spoken
    // for.
    let mut stored_by_action: BTreeMap<String, Vec<String>> = BTreeMap::new();
    for (action, raw_combos) in &file.actions {
        // An unknown action name is dropped, per entry, never fatally: a typo
        // in a hand-edited file costs that one binding, not the whole config.
        if !HOTKEY_ACTIONS.contains(&action.as_str()) {
            continue;
        }
        // An unparseable combo goes the same way, on its own.
        stored_by_action.insert(
            action.clone(),
            raw_combos.iter().filter_map(|c| parse_combo(c)).collect(),
        );
    }
    // BEFORE the union, which appends the new default to a stale v1 row and
    // leaves ["cmd+1", "cmd+opt+1"] - no longer recognisable as the old one.
    migrate_digits_to_spaces(&mut stored_by_action);

    let mut actions = preset_hotkeys(file.preset);
    for (action, stored) in stored_by_action {
        if !is_legacy {
            actions.insert(action, stored);
            continue;
        }
        // An explicit empty list is a deliberate UNBIND and is honoured even
        // in a legacy file. Unlike a stale non-empty list, `[]` cannot have
        // been written by an older default, so it is unambiguous. Unioning it
        // would switch an action back on that the user had switched off.
        if stored.is_empty() {
            actions.insert(action, Vec::new());
            continue;
        }
        let mut merged = stored;
        for combo in actions.get(&action).into_iter().flatten() {
            if !merged.contains(combo) {
                merged.push(combo.clone());
            }
        }
        actions.insert(action, merged);
    }

    HotkeysFile {
        schema_version: schema::current::HOTKEYS,
        actions,
        preset: file.preset,
    }
}

/// The write half `load_hotkeys` cannot be: it says WHY a load produced
/// nothing, rather than folding a refusal into the same defaults a fresh
/// profile gets. The pane already sends the merged, current-schema map it
/// was pushed, so no migration or leniency runs here - a writer accepting a
/// legacy shape back would let a stale schema keep re-entering the file.
pub fn load_hotkeys_checked(json: &str) -> Result<HotkeysFile, schema::LoadError> {
    let file: HotkeysFile =
        serde_json::from_str(json).map_err(|e| schema::LoadError::Unreadable(e.to_string()))?;
    if !schema::can_load(file.schema_version, schema::current::HOTKEYS) {
        return Err(schema::LoadError::Refused {
            file_version: file.schema_version,
        });
    }
    Ok(file)
}

/// The map the pane holds, plus which user entries to drop before the file is
/// written. Neither `forget` field set is an ordinary rebind.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct HotkeysEdit {
    #[serde(flatten)]
    pub file: HotkeysFile,
    #[serde(default)]
    pub forget: Option<String>,
    #[serde(default)]
    pub forget_all: bool,
}

/// Parses an Inputs-pane edit. `Err` means the payload is not one, which the
/// caller reads as "write what the pane sent, unchanged".
pub fn load_hotkeys_edit(json: &str) -> Result<HotkeysEdit, schema::LoadError> {
    let edit: HotkeysEdit =
        serde_json::from_str(json).map_err(|e| schema::LoadError::Unreadable(e.to_string()))?;
    if !schema::can_load(edit.file.schema_version, schema::current::HOTKEYS) {
        return Err(schema::LoadError::Refused {
            file_version: edit.file.schema_version,
        });
    }
    Ok(edit)
}

/// The file `edit` should write.
pub fn hotkeys_after_edit(edit: &HotkeysEdit) -> HotkeysFile {
    if edit.forget_all {
        return forget_all_hotkey_actions(&edit.file);
    }
    match &edit.forget {
        Some(action) => forget_hotkey_action(&edit.file, action),
        None => edit.file.clone(),
    }
}

/// `file` with the user's entry for `action` DROPPED, so `load_hotkeys` fills
/// the shipped default in again. Writing that default back as a user value
/// would pin the action to today's chord and never see a later one.
fn forget_hotkey_action(file: &HotkeysFile, action: &str) -> HotkeysFile {
    let mut actions = file.actions.clone();
    actions.remove(action);
    HotkeysFile {
        schema_version: file.schema_version,
        actions,
        preset: file.preset,
    }
}

/// `file` with every user entry dropped - the whole pane back on the shipped
/// defaults, by the same rule as `forget_hotkey_action`. Picking a preset is
/// this verb plus a new name, so the name has to outlive the map.
fn forget_all_hotkey_actions(file: &HotkeysFile) -> HotkeysFile {
    HotkeysFile {
        schema_version: file.schema_version,
        actions: BTreeMap::new(),
        preset: file.preset,
    }
}

// ------------------------------------------------- selection memory

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct SelectionEntry {
    pub query_prefix: String,
    pub row_identity: String,
    #[serde(rename = "profileID")]
    pub profile_id: String,
    pub count: i64,
    pub last_selected_at: f64,
}

/// No schema version, ever. Kept that way on purpose - adding one now would
/// make every existing file unreadable by the build that wrote it.
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SelectionMemoryFile {
    #[serde(default)]
    pub(crate) entries: Vec<SelectionEntry>,
}

pub fn load_selection_memory(json: &str) -> SelectionMemoryFile {
    serde_json::from_str(json).unwrap_or_default()
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn a_themes_file_with_no_themes_key_loads_empty() {
        let f = load_themes(r#"{"schemaVersion":1}"#);
        assert!(f.themes.is_empty());
        assert_eq!(f.schema_version, 1);
    }

    #[test]
    fn a_newer_themes_file_gives_defaults() {
        assert_eq!(load_themes(r#"{"schemaVersion":9}"#), ThemesFile::default());
    }

    #[test]
    fn a_routing_file_with_no_rules_loads_empty() {
        assert!(load_routing(r#"{"schemaVersion":1}"#).rules.is_empty());
    }

    fn rule(match_type: &str, pattern: &str, space: &str) -> RoutingRule {
        RoutingRule {
            id: String::new(),
            enabled: true,
            match_type: match_type.into(),
            pattern: pattern.into(),
            target_space_id: space.into(),
            excluded_source_space_ids: Vec::new(),
        }
    }

    fn file(rules: Vec<RoutingRule>, default_target: &str) -> RoutingFile {
        RoutingFile {
            schema_version: schema::current::ROUTING,
            default_target_space_id: default_target.into(),
            rules,
        }
    }

    #[test]
    fn append_rule_adds_an_enabled_rule() {
        let mut file = RoutingFile::default();
        assert!(file.append_rule("import-arc-1", "contains", "docs.example.test", "space-1"));
        assert_eq!(file.rules.len(), 1);
        assert!(file.rules[0].enabled);
        assert!(file.has_rule("contains", "docs.example.test", "space-1"));
        assert!(!file.has_rule("contains", "docs.example.test", CURRENT_SPACE_TARGET));
    }

    #[test]
    fn append_rule_refuses_a_duplicate_a_bad_regex_and_an_unknown_type() {
        let mut file = RoutingFile::default();
        assert!(file.append_rule("r1", "regex", "^https://a\\.example\\.test/", "current"));
        assert!(!file.append_rule("r2", "regex", "^https://a\\.example\\.test/", "current"), "duplicate");
        assert!(!file.append_rule("r3", "regex", "([", "current"), "does not compile");
        assert!(!file.append_rule("r4", "glob", "a", "current"), "unknown match type");
        assert!(!file.append_rule("r5", "contains", "", "current"), "empty pattern");
        assert_eq!(file.rules.len(), 1);
    }

    #[test]
    fn a_version_one_file_loads_with_the_new_fields_defaulted() {
        let loaded = load_routing(
            r#"{"schemaVersion":1,"rules":[{"id":"r","enabled":true,
               "matchType":"contains","pattern":"corp.example","targetSpaceID":"S1"}]}"#,
        );
        assert_eq!(loaded.default_target_space_id, "");
        assert!(loaded.rules[0].excluded_source_space_ids.is_empty());
    }

    #[test]
    fn a_schema_two_file_round_trips_the_fallback_and_exclusions() {
        let loaded = load_routing(
            r#"{"schemaVersion":2,"defaultTargetSpaceID":"HOME","rules":[{"id":"r","enabled":true,
               "matchType":"contains","pattern":"corp.example","targetSpaceID":"S1",
               "excludedSourceSpaceIDs":["WORK"]}]}"#,
        );
        assert_eq!(loaded.default_target_space_id, "HOME");
        assert_eq!(loaded.rules[0].excluded_source_space_ids, vec!["WORK"]);
    }

    #[test]
    fn an_excluded_source_space_skips_the_rule_and_the_next_one_still_matches() {
        let mut first = rule("contains", "corp.example", "S1");
        first.excluded_source_space_ids = vec!["HOME".into()];
        let second = rule("contains", "corp.example", "S2");
        let f = file(vec![first, second], "");
        assert_eq!(
            route_for_url(&f, "https://corp.example/", Some("HOME")),
            Some("S2")
        );
        assert_eq!(
            route_for_url(&f, "https://corp.example/", Some("WORK")),
            Some("S1")
        );
        assert_eq!(route_for_url(&f, "https://corp.example/", None), Some("S1"));
    }

    #[test]
    fn a_current_space_target_stops_the_search_even_with_a_fallback() {
        let f = file(
            vec![rule("contains", "github.com", CURRENT_SPACE_TARGET)],
            "HOME",
        );
        assert_eq!(
            route_for_url(&f, "https://github.com/x", Some("WORK")),
            None
        );
    }

    #[test]
    fn no_hit_returns_the_fallback_and_an_empty_fallback_returns_none() {
        let with = file(vec![rule("contains", "corp.example", "S1")], "HOME");
        assert_eq!(
            route_for_url(&with, "https://other.test/", Some("WORK")),
            Some("HOME")
        );
        let without = file(vec![rule("contains", "corp.example", "S1")], "");
        assert_eq!(
            route_for_url(&without, "https://other.test/", Some("WORK")),
            None
        );
    }

    #[test]
    fn contains_tests_the_host_and_ignores_the_path() {
        let rules = vec![rule("contains", "corp.example", "S1")];
        assert_eq!(
            route_for_url(
                &file(rules.clone(), ""),
                "https://mail.corp.example/inbox",
                None
            ),
            Some("S1")
        );
        // The pattern appears in the PATH here, which must not route: a rule
        // naming a host would otherwise fire on any page linking to it.
        assert_eq!(
            route_for_url(&file(rules, ""), "https://evil.example/corp.example", None),
            None
        );
    }

    /// a bare substring let a rule escape the domain boundary. A rule
    /// for corp.example claimed corp.example.attacker.test, which routes an
    /// attacker's page into the corp profile window.
    #[test]
    fn a_host_that_merely_starts_with_the_pattern_does_not_match() {
        let rules = vec![rule("contains", "corp.example", "S1")];
        assert_eq!(
            route_for_url(
                &file(rules.clone(), ""),
                "https://corp.example.attacker.test/",
                None
            ),
            None,
            "a rule for corp.example claimed a host that only starts with it"
        );
        // Nor a label that merely ends with the pattern's last label.
        assert_eq!(
            route_for_url(&file(rules, ""), "https://notcorp.example/", None),
            None,
            "a rule for corp.example claimed notcorp.example"
        );
    }

    #[test]
    fn the_host_itself_and_its_subdomains_still_match() {
        // The other direction: the boundary rule must not break what routing is
        // for. Exactly the host, and anything under it.
        let f = file(vec![rule("contains", "corp.example", "S1")], "");
        for url in [
            "https://corp.example/",
            "https://mail.corp.example/inbox",
            "https://deep.mail.corp.example/x",
        ] {
            assert_eq!(
                route_for_url(&f, url, None),
                Some("S1"),
                "{url} stopped matching"
            );
        }
    }

    #[test]
    fn an_over_budget_regex_does_not_compile_and_an_ordinary_one_does() {
        // the budget, pinned where it can be seen. Going through
        // route_for_url could not distinguish an over-budget pattern from one that
        // merely did not match, and the first version of this test did exactly
        // that and passed with the limit removed.
        assert!(
            compile_rule_regex("corp\\.example").is_some(),
            "an ordinary pattern stopped compiling"
        );
        let huge = "(?:a|b|c|d|e|f|g|h|i|j){64}".repeat(64);
        assert!(
            compile_rule_regex(&huge).is_none(),
            "a pattern well past the budget still compiled"
        );
    }

    #[test]
    fn a_rule_that_cannot_compile_leaves_its_siblings_alone() {
        let rules = vec![
            rule("regex", "(unclosed", "S1"),
            rule("contains", "corp.example", "S2"),
        ];
        assert_eq!(
            route_for_url(&file(rules, ""), "https://mail.corp.example/inbox", None),
            Some("S2"),
            "a broken regex took its sibling rule down with it"
        );
    }

    #[test]
    fn a_leading_star_is_matched_literally() {
        // Documented behaviour, and worth a test because users read "*.corp"
        // as a wildcard and it is a literal substring.
        let rules = vec![rule("contains", "*.corp.example", "S1")];
        assert_eq!(
            route_for_url(&file(rules, ""), "https://mail.corp.example/", None),
            None
        );
    }

    #[test]
    fn credentials_are_not_part_of_the_host() {
        let rules = vec![rule("contains", "corp.example", "S1")];
        assert_eq!(
            route_for_url(&file(rules, ""), "https://corp.example@evil.test/", None),
            None,
            "a userinfo section must not pass as the host"
        );
    }

    /// A test-server URL always carries a port. A host rule must match it too.
    #[test]
    fn a_port_in_the_url_does_not_defeat_a_host_match() {
        let rules = vec![rule("contains", "corp.example", "S1")];
        assert_eq!(
            route_for_url(&file(rules, ""), "https://corp.example:8443/inbox", None),
            Some("S1"),
            "the port should not be part of the host \"contains\" matches"
        );
    }

    #[test]
    fn host_of_strips_a_port_but_keeps_an_ipv6_literal_intact() {
        assert_eq!(host_of("http://[::1]/"), Some("[::1]"));
        assert_eq!(host_of("http://[::1]:8080/"), Some("[::1]"));
        assert_eq!(
            host_of("https://user:pw@corp.example:8443/x"),
            Some("corp.example")
        );
        assert_eq!(host_of("not a url"), None);
    }

    #[test]
    fn the_first_enabled_match_wins_in_array_order() {
        let mut disabled = rule("contains", "corp.example", "DISABLED");
        disabled.enabled = false;
        let rules = vec![
            disabled,
            rule("contains", "corp.example", "FIRST"),
            rule("contains", "corp.example", "SECOND"),
        ];
        assert_eq!(
            route_for_url(&file(rules, ""), "https://corp.example/", None),
            Some("FIRST")
        );
    }

    #[test]
    fn regex_sees_the_whole_url_and_is_case_insensitive() {
        let rules = vec![rule("regex", r"host\.example:8090/secure/.*", "S1")];
        let f = file(rules, "");
        assert_eq!(
            route_for_url(&f, "https://HOST.example:8090/secure/x?y=1", None),
            Some("S1")
        );
        assert_eq!(
            route_for_url(&f, "https://host.example:8090/open/x", None),
            None
        );
    }

    #[test]
    fn a_pattern_that_does_not_compile_drops_only_its_own_rule() {
        let rules = vec![
            rule("regex", "([unclosed", "BROKEN"),
            rule("contains", "corp.example", "GOOD"),
        ];
        assert_eq!(
            route_for_url(&file(rules, ""), "https://corp.example/", None),
            Some("GOOD")
        );
    }

    #[test]
    fn an_unknown_match_type_routes_nothing() {
        // Leniency has a direction: a rule we cannot evaluate must match
        // nothing rather than everything.
        let rules = vec![rule("glob", "corp.example", "S1")];
        assert_eq!(
            route_for_url(&file(rules, ""), "https://corp.example/", None),
            None
        );
    }

    #[test]
    fn a_url_with_no_host_routes_nothing() {
        let f = file(vec![rule("contains", "corp.example", "S1")], "");
        assert_eq!(route_for_url(&f, "about:blank", None), None);
        assert_eq!(route_for_url(&f, "", None), None);
    }

    #[test]
    fn a_legacy_hotkeys_file_unions_in_the_defaults() {
        let f = load_hotkeys(r#"{"schemaVersion":1,"actions":{"goToSpace1":["cmd+1"]}}"#);
        assert_eq!(f.schema_version, 2);
        assert_eq!(f.actions["goToSpace1"], vec!["cmd+1"]);
        assert!(
            f.actions.contains_key("toggleSidebar"),
            "a default filled in"
        );
    }

    /// An action written as an empty list is the user unbinding it. The union
    /// must not treat that as absent and hand the default back.
    #[test]
    fn an_explicit_unbind_survives_the_union() {
        let f = load_hotkeys(r#"{"schemaVersion":1,"actions":{"openCommandBar":[]}}"#);
        assert_eq!(f.actions["openCommandBar"], Vec::<String>::new());
    }

    #[test]
    fn an_unknown_action_is_dropped_and_the_rest_survives() {
        let f = load_hotkeys(
            r#"{"schemaVersion":2,"actions":{"noSuchAction":["cmd+1"],"toggleSidebar":["cmd+s"]}}"#,
        );
        assert!(!f.actions.contains_key("noSuchAction"));
        assert_eq!(f.actions["toggleSidebar"], vec!["cmd+s"]);
    }

    /// Each digit belongs to one action, so the Inputs pane has no
    /// clash to warn about and a user can rebind any single tile key.
    #[test]
    fn each_digit_chord_belongs_to_one_space() {
        let defaults = default_hotkeys();
        for n in 1..=9 {
            let combo = format!("cmd+{n}");
            let owners: Vec<&String> = defaults
                .iter()
                .filter(|(_, combos)| combos.contains(&combo))
                .map(|(action, _)| action)
                .collect();
            assert_eq!(owners.len(), 1, "{combo} is bound to {owners:?}");
            assert_eq!(*owners[0], format!("goToSpace{n}"));
        }
    }

    /// A file saved by a build that still had the digits on the tiles must not
    /// keep them there: goToSpaceN would default to the same chord and lose
    /// the tie in the native map, so the new binding would never fire.
    #[test]
    fn a_saved_file_on_the_old_digit_layout_is_migrated() {
        let f = load_hotkeys(
            r#"{"schemaVersion":2,"actions":{
                "goToSidebarItem1":["cmd+1"],
                "goToSidebarItem2":["cmd+2"],
                "goToSidebarItemLast":["cmd+9"]}}"#,
        );
        assert_eq!(f.actions["goToSidebarItem1"], vec!["cmd+opt+1"]);
        assert_eq!(f.actions["goToSidebarItem2"], vec!["cmd+opt+2"]);
        assert_eq!(f.actions["goToSidebarItemLast"], vec!["cmd+opt+9"]);
        // The digit is the space's now, and nothing else answers it.
        assert_eq!(f.actions["goToSpace1"], vec!["cmd+1"]);
        for (action, combos) in &f.actions {
            assert!(
                action == "goToSpace1" || !combos.contains(&"cmd+1".to_string()),
                "{action} still answers cmd+1"
            );
        }
    }

    /// The v1 union appends the new default to the stale row, so a migration
    /// running after it sees ["cmd+1","cmd+opt+1"] and skips.
    #[test]
    fn a_legacy_v1_file_lands_the_tiles_on_the_opt_digits() {
        let f = load_hotkeys(
            r#"{"schemaVersion":1,"actions":{
                "goToSidebarItem1":["cmd+1"],
                "goToSidebarItem8":["cmd+8"],
                "goToSidebarItemLast":["cmd+9"]}}"#,
        );
        assert_eq!(f.actions["goToSidebarItem1"], vec!["cmd+opt+1"]);
        assert_eq!(f.actions["goToSidebarItem8"], vec!["cmd+opt+8"]);
        assert_eq!(f.actions["goToSidebarItemLast"], vec!["cmd+opt+9"]);
        assert_eq!(f.actions["goToSpace1"], vec!["cmd+1"]);
        for (action, combos) in &f.actions {
            assert!(
                action == "goToSpace1" || !combos.contains(&"cmd+1".to_string()),
                "{action} still answers cmd+1"
            );
        }
    }

    /// Moving a stale tile onto a chord the user already put somewhere else
    /// would rebuild the very clash the move is meant to remove.
    #[test]
    fn a_tile_is_not_moved_onto_a_chord_someone_else_holds() {
        let f = load_hotkeys(
            r#"{"schemaVersion":2,"actions":{
                "goToSidebarItem1":["cmd+1"],
                "goToSpace1":["cmd+opt+1"]}}"#,
        );
        assert_eq!(f.actions["goToSpace1"], vec!["cmd+opt+1"]);
        assert_eq!(f.actions["goToSidebarItem1"], vec!["cmd+1"]);
    }

    /// A chord the user chose is not a stale default, so the migration above
    /// must leave it exactly as saved.
    #[test]
    fn a_user_chosen_sidebar_chord_survives_the_migration() {
        let f =
            load_hotkeys(r#"{"schemaVersion":2,"actions":{"goToSidebarItem1":["cmd+shift+7"]}}"#);
        assert_eq!(f.actions["goToSidebarItem1"], vec!["cmd+shift+7"]);
    }

    /// The favourite tiles must keep a chord of their own, or moving the plain
    /// digits to the spaces leaves nine actions dark.
    #[test]
    fn each_sidebar_item_keeps_a_chord() {
        let defaults = default_hotkeys();
        for n in 1..=9 {
            let action = if n == 9 {
                "goToSidebarItemLast".to_string()
            } else {
                format!("goToSidebarItem{n}")
            };
            assert_eq!(defaults[&action], vec![format!("cmd+opt+{n}")]);
        }
    }

    /// Every action with a default has to be a real action, or the union
    /// writes a name nothing can ever dispatch.
    #[test]
    fn every_default_names_a_real_action() {
        for action in default_hotkeys().keys() {
            assert!(HOTKEY_ACTIONS.contains(&action.as_str()), "{action}");
        }
    }

    /// Cmd+T opens the command bar, and the C++ table and the Arc
    /// mapping notes both say so - this map was the one place that did not,
    /// which is why the Inputs pane read "Unassigned" for a key that works.
    #[test]
    fn the_command_bar_ships_on_cmd_t() {
        assert_eq!(default_hotkeys()["openCommandBar"], vec!["cmd+t"]);
    }

    /// Distinct from toggleDeveloperMode (extensions dev mode, opt+ctrl+d).
    #[test]
    fn the_dev_bar_ships_on_cmd_shift_d() {
        assert_eq!(default_hotkeys()["toggleDevBar"], vec!["cmd+shift+d"]);
        assert!(HOTKEY_ACTIONS.contains(&"toggleDevBar"));
    }

    /// A duplicate chord would leave the matcher answering whichever rung it
    /// walked first, making the other unreachable.
    #[test]
    fn each_reload_rung_ships_its_own_chord() {
        let defaults = default_hotkeys();
        let ladder = [
            ("reload", "cmd+r"),
            ("hardReload", "cmd+shift+r"),
            ("reloadClearingCache", "cmd+opt+r"),
            ("reloadClearingSiteData", "cmd+shift+opt+r"),
        ];
        for (action, combo) in ladder {
            assert!(HOTKEY_ACTIONS.contains(&action), "{action}");
            assert_eq!(defaults[action], vec![combo], "{action}");
        }
        let mut combos: Vec<&str> = ladder.iter().map(|(_, c)| *c).collect();
        combos.sort_unstable();
        let before = combos.len();
        combos.dedup();
        assert_eq!(combos.len(), before, "two rungs share a chord");
    }

    /// The cross-table guard byte-matches these against the native map's
    /// copy, so an uncanonical spelling fails it on a chord that is the same.
    #[test]
    fn every_reload_chord_is_already_canonical() {
        for action in [
            "reload",
            "hardReload",
            "reloadClearingCache",
            "reloadClearingSiteData",
        ] {
            for combo in &default_hotkeys()[action] {
                assert_eq!(parse_combo(combo).as_deref(), Some(combo.as_str()));
            }
        }
    }

    /// A default spelled as an EMPTY list is not the same as no default at
    /// all. The seeding push hands every key in this map to the native table,
    /// which leaves an ABSENT action on its compiled-in combo and reads an
    /// empty list as a deliberate unbind - so an empty default here would
    /// release a key the build ships bound.
    #[test]
    fn no_default_is_an_empty_list() {
        for (action, combos) in default_hotkeys() {
            assert!(!combos.is_empty(), "{action} ships an empty list");
        }
    }

    /// The row reset. Dropping the entry is the whole point - writing cmd+t
    /// back as a user value would pin the action to today's default.
    #[test]
    fn forgetting_one_action_leaves_the_file_silent_about_it() {
        let edit = load_hotkeys_edit(
            r#"{"schemaVersion":2,"actions":{"openCommandBar":["cmd+shift+k"],
                "toggleSidebar":["cmd+s"]},"forget":"openCommandBar"}"#,
        )
        .expect("the pane's payload parses");
        let written = hotkeys_after_edit(&edit);

        assert!(!written.actions.contains_key("openCommandBar"));
        assert_eq!(written.actions["toggleSidebar"], vec!["cmd+s"]);
        let reloaded = load_hotkeys(&serde_json::to_string(&written).unwrap());
        assert_eq!(reloaded.actions["openCommandBar"], vec!["cmd+t"]);
    }

    /// A deliberate unbind is an entry like any other, so the reset has to
    /// clear it rather than leave the action dark for good.
    #[test]
    fn forgetting_an_unbound_action_brings_its_default_back() {
        let file = HotkeysFile {
            schema_version: schema::current::HOTKEYS,
            actions: BTreeMap::from([("toggleSidebar".to_string(), Vec::new())]),
            preset: HotkeyPreset::Atrium,
        };
        let written = forget_hotkey_action(&file, "toggleSidebar");

        assert!(written.actions.is_empty());
        let reloaded = load_hotkeys(&serde_json::to_string(&written).unwrap());
        assert_eq!(reloaded.actions["toggleSidebar"], vec!["cmd+s"]);
    }

    /// The pane reset. Every default has to come back, not only the rows the
    /// pane happened to be holding.
    #[test]
    fn forgetting_every_action_empties_the_user_map() {
        let edit = load_hotkeys_edit(
            r#"{"schemaVersion":2,"actions":{"openCommandBar":["cmd+shift+k"],
                "toggleSidebar":[]},"forgetAll":true}"#,
        )
        .expect("the pane's payload parses");
        let written = hotkeys_after_edit(&edit);

        assert!(written.actions.is_empty());
        assert_eq!(written.schema_version, schema::current::HOTKEYS);
        let reloaded = load_hotkeys(&serde_json::to_string(&written).unwrap());
        assert_eq!(reloaded.actions, default_hotkeys());
    }

    /// An ordinary rebind travels through the same payload, so a missing
    /// `forget` must not quietly reset anything.
    #[test]
    fn an_edit_that_forgets_nothing_writes_the_map_as_sent() {
        let edit = load_hotkeys_edit(
            r#"{"schemaVersion":2,"actions":{"openCommandBar":["cmd+shift+k"]}}"#,
        )
        .expect("the pane's payload parses");
        assert_eq!(hotkeys_after_edit(&edit), edit.file);
    }

    /// Forgetting an action the file never named is what a second click on
    /// the same row does.
    #[test]
    fn forgetting_an_action_the_file_does_not_name_changes_nothing() {
        let file = HotkeysFile {
            schema_version: schema::current::HOTKEYS,
            actions: BTreeMap::from([("toggleSidebar".to_string(), vec!["cmd+s".to_string()])]),
            preset: HotkeyPreset::Atrium,
        };
        assert_eq!(forget_hotkey_action(&file, "openCommandBar"), file);
    }

    /// A file this build cannot read is refused rather than replaced by an
    /// empty map, reset or not.
    #[test]
    fn an_edit_at_a_schema_this_build_refuses_is_not_an_edit() {
        assert!(
            load_hotkeys_edit(r#"{"schemaVersion":99,"actions":{},"forgetAll":true}"#).is_err()
        );
    }

    /// A preset that stays silent about an action Atrium binds would leave
    /// that action on Atrium's chord - a switch that half happened.
    #[test]
    fn both_presets_speak_about_exactly_the_same_actions() {
        let arc = arc_hotkeys();
        let atrium = default_hotkeys();
        assert_eq!(
            arc.keys().collect::<Vec<_>>(),
            atrium.keys().collect::<Vec<_>>()
        );
    }

    /// An EMPTY list releases a key the build compiles in. An ABSENT action
    /// leaves the native map holding it, Cmd+T included.
    #[test]
    fn arc_spells_its_unassigned_rows_as_an_unbind() {
        let arc = arc_hotkeys();
        for action in [
            "openCommandBar",
            "editCurrentURL",
            "openArchive",
            "toggleOverview",
            "goToSidebarItem1",
            "goToSidebarItemLast",
        ] {
            assert_eq!(arc[action], Vec::<String>::new(), "{action}");
        }
    }

    /// The sidebar's Clear icon names this chord in its hover hint, and the
    /// hint reads the live table, so the default is what a fresh install shows.
    #[test]
    fn clear_tabs_is_cmd_shift_k_in_both_presets() {
        assert_eq!(default_hotkeys()["clearTabs"], ["cmd+shift+k"]);
        assert_eq!(arc_hotkeys()["clearTabs"], ["cmd+shift+k"]);
    }

    /// The differences the task wrote down, each one a chord Atrium moved off
    /// Arc's. A change to either table has to pass through this list.
    #[test]
    fn atrium_differs_from_arc_only_where_it_was_written_down() {
        let arc = arc_hotkeys();
        let atrium = default_hotkeys();
        let mut moved: Vec<&str> = arc
            .iter()
            .filter(|(action, combos)| atrium[*action] != **combos)
            .map(|(action, _)| action.as_str())
            .collect();
        moved.sort_unstable();
        assert_eq!(
            moved,
            [
                "editCurrentURL",
                "goToSidebarItem1",
                "goToSidebarItem2",
                "goToSidebarItem3",
                "goToSidebarItem4",
                "goToSidebarItem5",
                "goToSidebarItem6",
                "goToSidebarItem7",
                "goToSidebarItem8",
                "goToSidebarItemLast",
                "nextSpace",
                "nextTab",
                "openArchive",
                "openCommandBar",
                "previousSpace",
                "previousTab",
                "reloadClearingCache",
                "reloadClearingSiteData",
                "toggleAiPanel",
                "toggleOverview",
                "toggleSplitOrientation",
            ]
        );
    }

    /// A preset's chords are byte-matched against the native map's compiled-in
    /// copy, so an uncanonical spelling fails on a chord that is the same one.
    #[test]
    fn every_preset_chord_is_canonical_and_names_a_real_action() {
        for preset in [HotkeyPreset::Arc, HotkeyPreset::Atrium] {
            for (action, combos) in preset_hotkeys(preset) {
                assert!(HOTKEY_ACTIONS.contains(&action.as_str()), "{action}");
                for combo in &combos {
                    assert_eq!(parse_combo(combo).as_deref(), Some(combo.as_str()));
                }
            }
        }
    }

    /// Two actions on one chord leaves the matcher answering whichever it
    /// walked first, and the other unreachable.
    #[test]
    fn no_preset_gives_one_chord_to_two_actions() {
        for preset in [HotkeyPreset::Arc, HotkeyPreset::Atrium] {
            let mut seen: BTreeMap<String, String> = BTreeMap::new();
            for (action, combos) in preset_hotkeys(preset) {
                for combo in combos {
                    let held = seen.insert(combo.clone(), action.clone());
                    assert_eq!(held, None, "{combo} is on {action} and {held:?}");
                }
            }
        }
    }

    /// An absent file has to keep answering with Atrium's table: it is the one
    /// the native map's compiled-in defaults are guarded against.
    #[test]
    fn a_file_that_names_no_preset_reads_as_atrium() {
        let file = load_hotkeys("");
        assert_eq!(file.preset, HotkeyPreset::Atrium);
        assert_eq!(file.actions, default_hotkeys());
    }

    /// Why the NAME is recorded rather than the chords: a later change to a
    /// preset's table still reaches a profile that picked it.
    #[test]
    fn a_users_map_merges_over_the_preset_it_names() {
        let file = load_hotkeys(
            r#"{"schemaVersion":2,"preset":"arc","actions":{"toggleSidebar":["cmd+shift+s"]}}"#,
        );

        assert_eq!(file.preset, HotkeyPreset::Arc);
        assert_eq!(file.actions["toggleSidebar"], vec!["cmd+shift+s"]);
        assert_eq!(file.actions["nextTab"], arc_hotkeys()["nextTab"]);
        assert_eq!(file.actions["openCommandBar"], Vec::<String>::new());
    }

    /// The reset from the change before this one, read through a preset: the
    /// row goes back to the SELECTED preset's chord, not to Atrium's.
    #[test]
    fn forgetting_a_row_returns_it_to_the_selected_preset() {
        let edit = load_hotkeys_edit(
            r#"{"schemaVersion":2,"preset":"arc","actions":{"nextTab":["cmd+shift+y"]},
                "forget":"nextTab"}"#,
        )
        .expect("the pane's payload parses");
        let written = hotkeys_after_edit(&edit);

        assert_eq!(written.preset, HotkeyPreset::Arc);
        let reloaded = load_hotkeys(&serde_json::to_string(&written).unwrap());
        assert_eq!(reloaded.actions["nextTab"], vec!["cmd+shift+]"]);
    }

    /// Picking a preset is forgetAll plus a name, so the preset has to survive
    /// the verb that empties the map.
    #[test]
    fn switching_preset_keeps_the_name_and_drops_the_user_map() {
        let edit = load_hotkeys_edit(
            r#"{"schemaVersion":2,"preset":"arc","actions":{"toggleSidebar":["cmd+shift+s"]},
                "forgetAll":true}"#,
        )
        .expect("the pane's payload parses");
        let written = hotkeys_after_edit(&edit);

        assert!(written.actions.is_empty());
        assert_eq!(written.preset, HotkeyPreset::Arc);
        let reloaded = load_hotkeys(&serde_json::to_string(&written).unwrap());
        assert_eq!(reloaded.actions, arc_hotkeys());
    }

    /// A preset name this build does not know costs its own meaning and
    /// nothing else, the same rule an unknown action id gets.
    #[test]
    fn an_unknown_preset_name_reads_as_the_shipped_one() {
        let file = load_hotkeys(
            r#"{"schemaVersion":2,"preset":"vivaldi","actions":{"toggleSidebar":["cmd+shift+s"]}}"#,
        );

        assert_eq!(file.preset, HotkeyPreset::Atrium);
        assert_eq!(file.actions["toggleSidebar"], vec!["cmd+shift+s"]);
    }

    /// Atrium is what every file written before presets existed means, so
    /// writing the name back would rewrite every one of them for nothing.
    #[test]
    fn the_shipped_preset_writes_no_name_at_all() {
        let file = HotkeysFile {
            schema_version: schema::current::HOTKEYS,
            preset: HotkeyPreset::Atrium,
            actions: BTreeMap::new(),
        };
        assert_eq!(
            serde_json::to_string(&file).unwrap(),
            r#"{"schemaVersion":2,"actions":{}}"#
        );
        assert_eq!(
            serde_json::to_string(&HotkeysFile {
                preset: HotkeyPreset::Arc,
                ..file
            })
            .unwrap(),
            r#"{"schemaVersion":2,"actions":{},"preset":"arc"}"#
        );
    }

    /// The pane picks a preset's whole table out of this, so a name the pane
    /// can offer and not apply would be a dead button.
    #[test]
    fn the_preset_tables_are_published_under_their_own_names() {
        let tables = hotkey_preset_tables();
        assert_eq!(tables.keys().collect::<Vec<_>>(), ["arc", "atrium"]);
        assert_eq!(tables["arc"], arc_hotkeys());
        assert_eq!(tables["atrium"], default_hotkeys());
    }

    /// v1 added most of its Arc-parity combos as EXTRA entries on actions that
    /// already existed. Replacing instead of unioning is what silently lost
    /// them.
    #[test]
    fn the_legacy_union_adds_combos_to_an_action_the_file_already_names() {
        let f = load_hotkeys(r#"{"schemaVersion":1,"actions":{"nextSpace":["opt+tab"]}}"#);
        assert_eq!(
            f.actions["nextSpace"],
            vec![
                "opt+tab",
                "cmd+opt+right",
                "opt+ctrl+right",
                "cmd+shift+right"
            ],
            "the file's own combo stays first, then the defaults it was missing"
        );
    }

    /// At the current version the file is authoritative, so a combo the user
    /// deleted in Settings must stay deleted.
    #[test]
    fn a_current_file_replaces_rather_than_unions() {
        let f = load_hotkeys(r#"{"schemaVersion":2,"actions":{"nextSpace":["opt+tab"]}}"#);
        assert_eq!(f.actions["nextSpace"], vec!["opt+tab"]);
        assert!(
            f.actions.contains_key("toggleSidebar"),
            "an action the file omits still gets its default"
        );
    }

    #[test]
    fn an_unparseable_combo_is_dropped_on_its_own() {
        let f = load_hotkeys(
            r#"{"schemaVersion":2,"actions":{"toggleSidebar":["cmd+s","meta+q","cmd+notakey"]}}"#,
        );
        assert_eq!(f.actions["toggleSidebar"], vec!["cmd+s"]);
    }

    #[test]
    fn a_combo_comes_back_in_a_fixed_modifier_order() {
        assert_eq!(parse_combo("shift+cmd+T").as_deref(), Some("cmd+shift+t"));
        assert_eq!(
            parse_combo("ctrl+opt+left").as_deref(),
            Some("opt+ctrl+left")
        );
        assert_eq!(parse_combo("escape").as_deref(), Some("escape"));
    }

    #[test]
    fn a_malformed_combo_does_not_parse() {
        for raw in ["", "cmd+", "meta+t", "cmd+notakey", "+t"] {
            assert_eq!(parse_combo(raw), None, "{raw}");
        }
    }

    #[test]
    fn selection_memory_loads_and_empties_cleanly() {
        assert!(
            load_selection_memory(r#"{"entries":[]}"#)
                .entries
                .is_empty()
        );
        assert!(load_selection_memory("not json").entries.is_empty());
    }
}

impl HasSchemaVersion for ThemesFile {
    fn schema_version(&self) -> i64 {
        self.schema_version
    }
}

impl HasSchemaVersion for RoutingFile {
    fn schema_version(&self) -> i64 {
        self.schema_version
    }
}

impl HasSchemaVersion for HotkeysFile {
    fn schema_version(&self) -> i64 {
        self.schema_version
    }
}
