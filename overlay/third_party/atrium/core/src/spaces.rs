// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! `spaces.json`: the spaces, their tabs, favourites, and everything that
//! rides along in the same file.
//!
//! The shapes mirror `SpacesModel.swift` and `SpacesStore.swift` field for
//! field. Nothing is reshaped on the way across: a reshape makes a mismatch
//! impossible to attribute to either half.
//!
//! Two Swift encoding habits the types have to match:
//!   - A nil optional is OMITTED, not written as null. Swift's synthesised
//!     encoder uses encodeIfPresent.
//!   - A Date is a double, seconds since 2001-01-01 (Swift's reference date),
//!     not the Unix epoch.

use serde::{Deserialize, Serialize};
use std::collections::HashSet;

use crate::schema;

/// Seconds since Swift's reference date. Kept as a plain f64 so a value
/// round-trips to the same bytes rather than through a calendar.
pub type SwiftDate = f64;

/// A persisted row's identity for as long as the document is loaded.
///
/// The file has no tab id and must not grow one, so this is never written and
/// never read - `#[serde(skip)]` on the field. It lives ON the row rather than
/// in a side table so it travels with the value through every `remove` and
/// `insert` the mutations already do; a parallel array would have to be
/// maintained by each of them, which is how Swift's `tabTokens` worked and what
/// made a reorder able to silently re-point a binding.
///
/// [`UNTOKENED`] is "no identity" and never matches, so a hand-built document
/// binds nothing until [`assign_tokens`] has run.
pub type RowToken = u64;

/// The token a row has before [`assign_tokens`] reaches it.
pub const UNTOKENED: RowToken = 0;

/// The furthest a room or a card may sit from the plane origin. A value past
/// this is a corrupt write or a runaway drag, and storing it strands the room
/// where no pan can reach it.
const PLANE_LIMIT: f64 = 100_000.0;

/// A room's rectangle on the Courtyard plane, in plane units.
///
/// Absent means the room has never been placed by hand, and the view puts it
/// right of the rightmost placed room. Absent is NOT a zero rect: a zero would
/// pin every unplaced room on top of the first one.
#[derive(Debug, Clone, Copy, PartialEq, Serialize, Deserialize)]
pub struct CanvasRect {
    pub x: f64,
    pub y: f64,
    pub w: f64,
    pub h: f64,
}

/// True when a rect can be stored. Width and height must be positive, because
/// a lane cannot be hit-tested inside a rect with no area.
pub fn canvas_rect_is_valid(x: f64, y: f64, w: f64, h: f64) -> bool {
    x.is_finite()
        && y.is_finite()
        && w.is_finite()
        && h.is_finite()
        && x.abs() <= PLANE_LIMIT
        && y.abs() <= PLANE_LIMIT
        && w > 0.0
        && h > 0.0
        && w <= PLANE_LIMIT
        && h <= PLANE_LIMIT
}

/// True when a card's lane offset can be stored.
pub fn canvas_x_is_valid(x: f64) -> bool {
    x.is_finite() && x.abs() <= PLANE_LIMIT
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SpaceTab {
    pub url: String,
    pub title: String,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub last_active_at: Option<SwiftDate>,
    #[serde(rename = "groupID", default, skip_serializing_if = "Option::is_none")]
    pub group_id: Option<String>,
    #[serde(rename = "pinnedURL", default, skip_serializing_if = "Option::is_none")]
    pub pinned_url: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub custom_title: Option<String>,
    /// The card's x offset inside its lane, in plane units. Absent means the
    /// card sits at x 0. There is no stored y: the card's vertical place is
    /// its sidebar index, so the order has one source of truth.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub canvas_x: Option<f64>,
    /// See [`RowToken`]. Runtime only - not in the file, either direction.
    #[serde(skip)]
    pub token: RowToken,
}

impl SpaceTab {
    /// A brand new row, as a freshly opened tab persists it.
    ///
    /// The token is [`UNTOKENED`] here; `mutate::open_tab` stamps it from the
    /// document's own counter, which is the only place that can hand out one
    /// that does not collide.
    pub fn new(url: &str, title: &str) -> Self {
        Self {
            url: url.to_string(),
            title: title.to_string(),
            last_active_at: None,
            group_id: None,
            pinned_url: None,
            custom_title: None,
            canvas_x: None,
            token: UNTOKENED,
        }
    }
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct TabGroup {
    pub id: String,
    pub name: String,
    pub collapsed: bool,
    /// A custom glyph, user-supplied. Absent means the default folder icon.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub icon: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SplitArrangement {
    #[serde(rename = "primaryURL")]
    pub primary_url: String,
    #[serde(rename = "secondaryURL")]
    pub secondary_url: String,
    pub orientation: String,
    pub ratio: f64,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Space {
    pub id: String,
    pub name: String,
    pub emoji: String,
    pub tint: String,
    #[serde(rename = "profileID")]
    pub profile_id: String,
    #[serde(default)]
    pub pinned_tabs: Vec<SpaceTab>,
    #[serde(default)]
    pub tabs: Vec<SpaceTab>,
    pub order: i64,
    #[serde(default)]
    pub groups: Vec<TabGroup>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub split: Option<SplitArrangement>,
    /// Added after v1, so a v1 file has no key. Default false.
    #[serde(default)]
    pub pinned_collapsed: bool,
    /// Added after v1. Default 1 (neutral), so an existing space's wash does
    /// not change until the user moves the Tint Strength slider.
    #[serde(default = "one")]
    pub tint_strength: f64,
    /// This space's own auto-archive TTL, a rung of the `archiveThreshold`
    /// ladder. Absent follows the general setting.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub archive_ttl: Option<String>,
    /// A v5 file's own engine and hibernate policy, read once by
    /// `migrate_v5_to_v6` and never written back.
    #[serde(flatten, skip_serializing)]
    pub legacy: RetiredSpaceFields,
    /// This room's rectangle on the Courtyard plane. Absent means unplaced.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub canvas: Option<CanvasRect>,
}

/// The three keys `migrate_v5_to_v6` moves onto the space's [`Profile`].
#[derive(Debug, Clone, Default, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct RetiredSpaceFields {
    #[serde(default)]
    pub search_engine: Option<String>,
    #[serde(default)]
    pub hibernate: Option<String>,
    #[serde(default)]
    pub hib_threshold: Option<String>,
}

fn one() -> f64 {
    1.0
}

/// `id` is the `profileID` a space carries, which is also the directory name
/// under the user data dir, so it must pass the same allowlist the C++
/// bootstrap enforces or a record could name a path that escapes it.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Profile {
    pub id: String,
    pub name: String,
    /// Keys keep the pre-v6 per-space spelling so the move is a copy, not a
    /// rename, and the settings page reads one name for both.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub search_engine: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub hibernate: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub hib_threshold: Option<String>,
}

/// The longest id `ProfileBootstrap::IsSafeProfileId` accepts.
const MAX_PROFILE_ID_LENGTH: usize = 64;

/// Mirrors `ProfileBootstrap::IsSafeProfileId`
/// (`chrome/browser/ui/atrium/atrium_profile_bootstrap.cc`): an id is a path
/// component under the user data dir, so a leading dot, `..`, or a character
/// outside the allowlist could address a directory outside it.
pub fn profile_id_is_safe(id: &str) -> bool {
    if id.is_empty() || id.len() > MAX_PROFILE_ID_LENGTH || id.starts_with('.') || id.contains("..")
    {
        return false;
    }
    id.bytes()
        .all(|b| b.is_ascii_alphanumeric() || b == b' ' || b == b'_' || b == b'.' || b == b'-')
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct AppState {
    #[serde(
        rename = "activeSpaceID",
        default,
        skip_serializing_if = "Option::is_none"
    )]
    pub active_space_id: Option<String>,
    pub sidebar_width: f64,
    pub schema_version: i64,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub archive_epoch: Option<SwiftDate>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub window_spaces: Option<Vec<String>>,
    #[serde(default)]
    pub sidebar_width_migrated: bool,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Favorite {
    pub url: String,
    pub title: String,
    #[serde(rename = "profileID")]
    pub profile_id: String,
    #[serde(rename = "spaceID", default, skip_serializing_if = "Option::is_none")]
    pub space_id: Option<String>,
}

/// Bound to its space: the same site pinned in two spaces is two Doorways,
/// matching the lab's key (`si`, `t`) in `addDoorway`.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Doorway {
    pub url: String,
    #[serde(rename = "spaceID")]
    pub space_id: String,
    #[serde(rename = "profileID")]
    pub profile_id: String,
}

/// Why a row landed in [`Document::archived`]. A file written
/// before this enum existed carries only TTL/explicit-archive rows, never a
/// plain close, so an absent reason defaults to `Archived`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub enum CloseReason {
    UserClosed,
    /// TTL sweep or an explicit Archive/Clear action.
    #[default]
    Archived,
    /// Reserved for keeping a closing window's tabs for restore instead of
    /// deleting them, so nothing produces this yet.
    WindowClosed,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ArchivedTab {
    pub id: String,
    pub url: String,
    pub title: String,
    #[serde(rename = "spaceID")]
    pub space_id: String,
    pub archived_at: SwiftDate,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub last_active_at: Option<SwiftDate>,
    #[serde(rename = "groupID", default, skip_serializing_if = "Option::is_none")]
    pub group_id: Option<String>,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub custom_title: Option<String>,
    #[serde(default)]
    pub reason: CloseReason,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct ShelfHighlight {
    pub id: String,
    pub text: String,
    pub created_at: SwiftDate,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ShelfItem {
    pub id: String,
    pub kind: String,
    pub title: String,
    pub url: String,
    #[serde(rename = "spaceID", default, skip_serializing_if = "Option::is_none")]
    pub space_id: Option<String>,
    pub added_at: SwiftDate,
    #[serde(default)]
    pub(crate) highlights: Vec<ShelfHighlight>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SnoozedTab {
    pub id: String,
    pub url: String,
    pub title: String,
    #[serde(rename = "spaceID")]
    pub space_id: String,
    pub snoozed_at: SwiftDate,
    pub wake_at: SwiftDate,
    #[serde(default)]
    pub alert: bool,
    #[serde(rename = "groupID", default, skip_serializing_if = "Option::is_none")]
    pub group_id: Option<String>,
}

#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct SpaceNote {
    pub id: String,
    #[serde(rename = "spaceID", default, skip_serializing_if = "Option::is_none")]
    pub space_id: Option<String>,
    pub title: String,
    pub body: String,
    pub updated_at: SwiftDate,
}

/// Keyed by URL, not by tab id: a tab id dies with the tab, and a highlight
/// has to come back when the same page is opened again.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Marginal {
    pub id: String,
    pub url: String,
    #[serde(rename = "spaceID")]
    pub space_id: String,
    /// Immutable once stored: it is the substring the re-apply pass searches
    /// for, so editing it would orphan the mark.
    pub quote: String,
    #[serde(default)]
    pub note: String,
    pub created_at: SwiftDate,
}

/// A Notes overview row. `space_id` tells a space pad from a freestanding note.
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct NoteOverviewEntry {
    pub id: String,
    #[serde(rename = "spaceID", skip_serializing_if = "Option::is_none")]
    pub space_id: Option<String>,
    /// The sidebar position `atrium_core_set_active_space_index` takes - not
    /// the backing-array index - so a caller can jump here directly.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub space_index: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub space_name: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub space_icon: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub space_accent: Option<String>,
    pub title: String,
    pub body: String,
    pub updated_at: SwiftDate,
}

/// Capped at two highlight quotes, matching the lab's `marginalia` cap.
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ShelfOverviewEntry {
    pub id: String,
    pub kind: String,
    pub title: String,
    pub url: String,
    #[serde(rename = "spaceID", skip_serializing_if = "Option::is_none")]
    pub space_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub space_name: Option<String>,
    pub added_at: SwiftDate,
    pub highlights: Vec<String>,
}

/// The Archived overview row.
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ArchivedOverviewEntry {
    pub id: String,
    pub url: String,
    pub title: String,
    #[serde(rename = "spaceID", skip_serializing_if = "Option::is_none")]
    pub space_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub space_index: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub space_name: Option<String>,
    pub archived_at: SwiftDate,
    pub reason: CloseReason,
}

/// The Snoozed overview row.
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SnoozedOverviewEntry {
    pub id: String,
    pub url: String,
    pub title: String,
    #[serde(rename = "spaceID", skip_serializing_if = "Option::is_none")]
    pub space_id: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub space_index: Option<i32>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub space_name: Option<String>,
    pub wake_at: SwiftDate,
    pub alert: bool,
}

/// The whole file. `favorites`, `archived`, `shelf`, `snoozed`, `notes` and
/// `doorways` each arrived after v1, so every one defaults to empty rather
/// than failing a file that predates it.
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Document {
    pub spaces: Vec<Space>,
    pub app_state: AppState,
    #[serde(default)]
    pub favorites: Vec<Favorite>,
    /// Rows the window draws as a favourite tile instead of a row, by token.
    /// Set from the live strip on every model build and never written: the
    /// claim is a fact about a live tab, and only the window knows it.
    #[serde(skip)]
    pub hidden_rows: Vec<RowToken>,
    #[serde(default)]
    pub archived: Vec<ArchivedTab>,
    #[serde(default)]
    pub shelf: Vec<ShelfItem>,
    #[serde(default)]
    pub snoozed: Vec<SnoozedTab>,
    #[serde(default)]
    pub notes: Vec<SpaceNote>,
    /// Omitted when empty so an untouched file keeps writing the exact bytes
    /// Swift wrote for it (write_bytes.rs's fixed point).
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub dismissed_notes: Vec<String>,
    /// Omitted when empty for the same reason as `dismissed_notes`: Swift
    /// never wrote this key.
    #[serde(default, skip_serializing_if = "Vec::is_empty")]
    pub marginalia: Vec<Marginal>,
    #[serde(default)]
    pub doorways: Vec<Doorway>,
    /// One per distinct `profileID`, built by `backfill_profiles` for a file
    /// that predates the key. Always written, like `doorways`.
    #[serde(default)]
    pub profiles: Vec<Profile>,
    /// The next unused [`RowToken`]. Runtime only, like the tokens themselves.
    #[serde(skip)]
    pub next_token: RowToken,
}

impl Document {
    /// Hands out a token no row in this document holds.
    pub fn allocate_token(&mut self) -> RowToken {
        self.next_token = self.next_token.max(UNTOKENED + 1);
        let token = self.next_token;
        self.next_token += 1;
        token
    }
}

/// Numbers every persisted row, pinned first then regular, space by space.
///
/// Sequential rather than random so two loads of one file produce equal
/// documents - the crate has no RNG on purpose, and a test that compares two
/// loads would otherwise never agree.
pub fn assign_tokens(doc: &mut Document) {
    let mut next: RowToken = UNTOKENED + 1;
    for space in &mut doc.spaces {
        for tab in space.pinned_tabs.iter_mut().chain(space.tabs.iter_mut()) {
            tab.token = next;
            next += 1;
        }
    }
    // Ids are built from the counter and saved, but row tokens restart at 1 on
    // every load, so the counter has to clear every saved `<prefix>-N` id.
    let saved = doc
        .spaces
        .iter()
        .flat_map(|s| std::iter::once(&s.id).chain(s.groups.iter().map(|g| &g.id)))
        .chain(doc.archived.iter().map(|a| &a.id))
        .chain(doc.snoozed.iter().map(|a| &a.id))
        .chain(doc.profiles.iter().map(|p| &p.id))
        .chain(doc.shelf.iter().map(|i| &i.id))
        .chain(doc.notes.iter().map(|n| &n.id));
    let highest = saved
        .filter_map(|id| id.rsplit_once('-')?.1.parse::<RowToken>().ok())
        .max()
        .unwrap_or(0);
    doc.next_token = next.max(highest.saturating_add(1));
}

/// The dead default every older file carries, because nothing ever set
/// it to anything else.
const LEGACY_DEAD_SIDEBAR_WIDTH: f64 = 280.0;
/// What a sidebar is actually wide.
const DEFAULT_SIDEBAR_WIDTH: f64 = 220.0;

/// Why a load produced nothing usable. Lives in `schema` now, because every
/// store refuses for the same two reasons; re-exported here so the name a
/// caller already writes keeps working.
pub use crate::schema::LoadError;

/// Loads and migrates forward. `migration_instant` is the clock the v1 to v2
/// step stamps `archiveEpoch` from; it is a parameter so a test gets the same
/// bytes twice.
pub fn load(json: &str, migration_instant: SwiftDate) -> Result<Document, LoadError> {
    let mut doc: Document =
        serde_json::from_str(json).map_err(|e| LoadError::Unreadable(e.to_string()))?;

    let version = doc.app_state.schema_version;
    if !schema::can_load(version, schema::current::SPACES) {
        return Err(LoadError::Refused {
            file_version: version,
        });
    }

    // The chain runs oldest first, in one load. A .bak can sit at any point in
    // it, so both entry points share this.
    if version <= 1 {
        migrate_v1_to_v2(&mut doc, migration_instant);
    }
    if version <= 2 {
        migrate_v2_to_v3(&mut doc);
    }
    if version <= 3 {
        migrate_v3_to_v4(&mut doc);
    }
    if version <= 4 {
        migrate_v4_to_v5(&mut doc);
    }
    if version <= 5 {
        migrate_v5_to_v6(&mut doc);
    }

    // A normal write always stamps archiveEpoch, so a current-schema document
    // without one is corruption or a hand edit. Left alone the file is
    // permanently inert: the archive sweep returns early and archiving,
    // pruning and expiry stop forever with no way to self-heal.
    if doc.app_state.archive_epoch.is_none() {
        doc.app_state.archive_epoch = Some(migration_instant);
    }

    doc.favorites = migrate_favorites(&doc.favorites);
    migrate_sidebar_width(&mut doc);
    // A v6 file can still name a profile with no record if it was hand
    // edited, and a space with no profile has no engine and no hibernate
    // policy at all - so this runs unconditionally, after the chain.
    backfill_profiles(&mut doc);
    drop_unreopenable_rows(&mut doc);
    normalize_pinned_groups(&mut doc);
    // Last, so a migration that added or dropped a row cannot leave a hole.
    assign_tokens(&mut doc);

    Ok(doc)
}

/// The cold-row opener (IsOrdinarySidebarUrl) refuses every other scheme, so a
/// chrome:// page open at quit came back as a row no click or x could reach.
/// Pinned tiles keep theirs because a cold pin's x removes it.
fn drop_unreopenable_rows(doc: &mut Document) {
    for space in &mut doc.spaces {
        space
            .tabs
            .retain(|tab| crate::restore::scheme_allowed(&tab.url));
    }
}

/// v1 to v2: stamps `archiveEpoch` and bumps the version. It must NOT touch
/// any tab's `lastActiveAt` - flattening those to the migration instant makes
/// `startupTab`, which is the most recent tab or else the first, pick
/// arbitrarily.
fn migrate_v1_to_v2(doc: &mut Document, instant: SwiftDate) {
    doc.app_state.archive_epoch = Some(instant);
    doc.app_state.schema_version = 2;
}

/// v2 to v3: a version bump only. `windowSpaces` is additive and optional, so
/// there is nothing to backfill. The bump still matters - without it an older
/// build reads the file as current and can persist over a key it does not
/// understand.
fn migrate_v2_to_v3(doc: &mut Document) {
    doc.app_state.schema_version = 3;
}

/// v3 to v4: a version bump only. `archiveTtl` is additive and optional, and
/// absent already means Follow General (`lifecycle::archive_threshold_for`),
/// so no space needs a written value.
fn migrate_v3_to_v4(doc: &mut Document) {
    // The LITERAL 4, like its two siblings, not `schema::current::SPACES`. A
    // step stamps the version IT produces, and the chain runs one step at a
    // time. With `current` here, the day a v5 lands this step stamps 5 and the
    // v4-to-v5 step never runs on a v3 file. That is latent rather than broken
    // today only because `load` captures the version before running any step
    //.
    doc.app_state.schema_version = 4;
}

/// v4 to v5: a version bump only. `canvas` on a space and `canvasX` on a tab
/// are both additive and optional, and absent already means "unplaced", which
/// the view resolves the same way every time. The bump still matters: without
/// it a v4 build reads the file as current and writes over positions it does
/// not understand.
fn migrate_v4_to_v5(doc: &mut Document) {
    // The LITERAL 5, like its three siblings. A step stamps the version IT
    // produces, and the chain runs one step at a time.
    doc.app_state.schema_version = 5;
}

/// v5 to v6: the search engine and the hibernate policy move from the space to
/// its profile.
///
/// The space fields are CLEARED, not left as a second copy - after this step
/// nothing reads the space's own, see `lifecycle::hibernate_policy`. First
/// space wins per profile: two spaces sharing one cannot both set it, and
/// taking the later one would silently prefer whichever was created last.
fn migrate_v5_to_v6(doc: &mut Document) {
    backfill_profiles(doc);
    // Two mutable borrows of `doc` at once, so the moves are collected first
    // and applied to `doc.profiles` in a second pass.
    let moves: Vec<(
        String,
        String,
        Option<String>,
        Option<String>,
        Option<String>,
    )> = doc
        .spaces
        .iter_mut()
        .map(|space| {
            (
                space.id.clone(),
                space.profile_id.clone(),
                space.legacy.search_engine.take(),
                space.legacy.hibernate.take(),
                space.legacy.hib_threshold.take(),
            )
        })
        .collect();
    for (space_id, profile_id, engine, hibernate, threshold) in moves {
        let Some(profile) = doc.profiles.iter_mut().find(|p| p.id == profile_id) else {
            // An unsafe id gets no record from backfill_profiles, so these
            // values have nowhere to land.
            eprintln!(
                "v5 to v6 migration: space {space_id} has no profile record for {profile_id:?}, dropping searchEngine={engine:?} hibernate={hibernate:?} hibThreshold={threshold:?}"
            );
            continue;
        };
        if profile.search_engine.is_none() {
            profile.search_engine = engine;
        }
        if profile.hibernate.is_none() {
            profile.hibernate = hibernate;
        }
        if profile.hib_threshold.is_none() {
            profile.hib_threshold = threshold;
        }
    }
    doc.app_state.schema_version = 6;
}

/// Gives every `profileID` a record, named after the first space that uses it.
///
/// Idempotent: an id that already has one is skipped, so a reload does not
/// grow the list. An id `profile_id_is_safe` refuses gets none at all rather
/// than a record the browser could never load.
fn backfill_profiles(doc: &mut Document) {
    for space in &doc.spaces {
        if !profile_id_is_safe(&space.profile_id)
            || doc.profiles.iter().any(|p| p.id == space.profile_id)
        {
            continue;
        }
        doc.profiles.push(Profile {
            id: space.profile_id.clone(),
            name: space.name.clone(),
            search_engine: None,
            hibernate: None,
            hib_threshold: None,
        });
    }
}

/// Favourites are keyed by (profileID, url) and nothing else. Keeps one entry
/// per pair in first-seen order and clears `spaceID`.
///
/// Lossless: a URL favourited in three spaces of a profile collapses to that
/// profile's single favourite rather than being dropped. Idempotent: the key
/// is already unique on a second pass. A favourite whose profile has no spaces
/// is kept - nothing here reads `spaces` at all.
fn migrate_favorites(favorites: &[Favorite]) -> Vec<Favorite> {
    let mut out: Vec<Favorite> = Vec::with_capacity(favorites.len());
    // a HashSet of borrowed keys, not a Vec scanned per entry. The old
    // form was O(n squared) with a format! allocation per pair, on every load -
    // and a document can arrive carrying a large favourites array, which is the
    // case that made a quadratic loop matter.
    let mut seen: HashSet<(&str, &str)> = HashSet::with_capacity(favorites.len());
    for favorite in favorites {
        // Borrowed rather than formatted: the tuple compares the two fields
        // directly, so no string is built and no separator can collide with a
        // profile id or a url that contains it.
        if !seen.insert((favorite.profile_id.as_str(), favorite.url.as_str())) {
            continue;
        }
        let mut collapsed = favorite.clone();
        collapsed.space_id = None;
        out.push(collapsed);
    }
    out
}

/// One-shot backfill: the first time a file is loaded, a still-legacy 280 is
/// corrected to 220 and the file is marked migrated so it never runs again.
///
/// Keying on the VALUE instead of a persisted one-shot bit reverted a user who
/// deliberately dragged to exactly 280, on every launch, forever.
fn migrate_sidebar_width(doc: &mut Document) {
    if doc.app_state.sidebar_width_migrated {
        return;
    }
    if doc.app_state.sidebar_width == LEGACY_DEAD_SIDEBAR_WIDTH {
        doc.app_state.sidebar_width = DEFAULT_SIDEBAR_WIDTH;
    }
    doc.app_state.sidebar_width_migrated = true;
}

/// Whether `url` is one this profile has committed to: pinned in `space`, or
/// favourited.
///
/// auto-peek's one question.
/// The Keyhole's promise is that a page the user pinned never navigates away
/// under them, so an off-site link from one opens a card instead.
///
/// SCOPE, and why it is drawn here. Pinned is read from the ONE space passed
/// in, not from every space: a pinned row is pinned to its space, and a tab
/// showing the same URL in a different space is a different tab with no such
/// promise. Favourites are read across the profile, because that is how they
/// are keyed - `Favorite` carries a `profile_id` and its `space_id` is cleared
/// on load (see `migrate_favorites`).
///
/// URL comparison is exact. Swift's was too. A prefix or origin match would
/// make every page on a pinned site behave as pinned, which is the opposite of
/// the same-site rule auto-peek already applies.
pub fn is_peek_source_url(doc: &Document, space: &Space, profile_id: &str, url: &str) -> bool {
    if url.is_empty() {
        return false;
    }
    space.pinned_tabs.iter().any(|t| t.url == url) || is_favorite_url(doc, profile_id, url)
}

/// One entry of the pinned ITEM list - what the surface draws in the pinned
/// section, where a run of one group's members is a single folder.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PinnedItem {
    /// An index into `Space::pinned_tabs`.
    Tab(usize),
    /// An index into `Space::groups`.
    Folder(usize),
}

/// `space.pinned_tabs` walked in order, each run of one group's members
/// collapsed into that folder - array order is the only truth, so a folder
/// renders where its first member sits.
pub fn pinned_items(space: &Space) -> Vec<PinnedItem> {
    let mut items = Vec::with_capacity(space.pinned_tabs.len());
    let mut run: Option<&str> = None;
    for (index, tab) in space.pinned_tabs.iter().enumerate() {
        let Some(id) = tab.group_id.as_deref() else {
            run = None;
            items.push(PinnedItem::Tab(index));
            continue;
        };
        if run == Some(id) {
            continue;
        }
        run = Some(id);
        match space.groups.iter().position(|g| g.id == id) {
            Some(group) => items.push(PinnedItem::Folder(group)),
            // A dangling id draws as a plain pin rather than vanishing.
            None => items.push(PinnedItem::Tab(index)),
        }
    }
    items
}

/// Drops a group id from a pinned tab whose group is gone, and from a
/// regular tab whose group has a pinned member - `contents_of` draws that
/// folder from `pinned_tabs` alone. Also gathers pinned members into a run.
fn normalize_pinned_groups(doc: &mut Document) {
    for space in &mut doc.spaces {
        for tab in &mut space.pinned_tabs {
            if let Some(id) = tab.group_id.as_deref()
                && !space.groups.iter().any(|g| g.id == id)
            {
                tab.group_id = None;
            }
        }

        let pinned_group_ids: HashSet<&str> = space
            .pinned_tabs
            .iter()
            .filter_map(|t| t.group_id.as_deref())
            .collect();
        for tab in &mut space.tabs {
            if let Some(id) = tab.group_id.as_deref()
                && pinned_group_ids.contains(id)
            {
                tab.group_id = None;
            }
        }

        let count = space.pinned_tabs.len();
        let mut order: Vec<usize> = Vec::with_capacity(count);
        let mut gathered: Vec<String> = Vec::new();
        for index in 0..count {
            let Some(id) = space.pinned_tabs[index].group_id.clone() else {
                order.push(index);
                continue;
            };
            if gathered.contains(&id) {
                continue;
            }
            gathered.push(id.clone());
            order.extend(
                (index..count)
                    .filter(|&at| space.pinned_tabs[at].group_id.as_deref() == Some(id.as_str())),
            );
        }
        if order.iter().enumerate().any(|(at, &index)| at != index) {
            let mut taken: Vec<Option<SpaceTab>> = space.pinned_tabs.drain(..).map(Some).collect();
            space.pinned_tabs = order
                .into_iter()
                .filter_map(|index| taken[index].take())
                .collect();
        }
    }
}

/// Whether `profile_id` favourites exactly `url`. Pinned rows do not count:
/// a startup tab restored on a pinned row's url is that row's tab, not a tile's.
pub fn is_favorite_url(doc: &Document, profile_id: &str, url: &str) -> bool {
    !url.is_empty()
        && doc
            .favorites
            .iter()
            .any(|f| f.url == url && f.profile_id == profile_id)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::lifecycle;

    const INSTANT: SwiftDate = 788_918_400.0;

    fn v1() -> String {
        r#"{"spaces":[{"id":"A","name":"P","emoji":"H","tint":"t","profileID":"Default",
        "pinnedTabs":[],"tabs":[{"url":"https://u/","title":"T"}],"order":0,"groups":[]}],
        "appState":{"activeSpaceID":"A","sidebarWidth":280,"schemaVersion":1},
        "favorites":[]}"#
            .to_string()
    }

    /// A document with one space holding one pinned row and one regular row,
    /// plus one favourite belonging to `Default`.
    fn peek_fixture() -> Document {
        let json = r#"{"spaces":[
            {"id":"A","name":"P","emoji":"H","tint":"t","profileID":"Default",
             "pinnedTabs":[{"url":"https://pinned.example/","title":"P"}],
             "tabs":[{"url":"https://plain.example/","title":"T"}],
             "order":0,"groups":[]},
            {"id":"B","name":"Q","emoji":"H","tint":"t","profileID":"Default",
             "pinnedTabs":[{"url":"https://elsewhere.example/","title":"E"}],
             "tabs":[],"order":1,"groups":[]}],
            "appState":{"activeSpaceID":"A","sidebarWidth":280,"schemaVersion":1},
            "favorites":[{"url":"https://fav.example/","title":"F","profileID":"Default"}]}"#;
        load(json, INSTANT).unwrap()
    }

    #[test]
    fn a_pinned_url_is_a_peek_source() {
        let doc = peek_fixture();
        assert!(is_peek_source_url(
            &doc,
            &doc.spaces[0],
            "Default",
            "https://pinned.example/"
        ));
    }

    #[test]
    fn a_favourite_is_found_by_exact_url_and_profile_and_a_pinned_row_is_not() {
        let doc = peek_fixture();
        assert!(is_favorite_url(&doc, "Default", "https://fav.example/"));
        assert!(!is_favorite_url(&doc, "Other", "https://fav.example/"));
        assert!(!is_favorite_url(
            &doc,
            "Default",
            "https://fav.example/page"
        ));
        assert!(!is_favorite_url(
            &doc,
            "Default",
            &doc.spaces[0].pinned_tabs[0].url
        ));
        assert!(!is_favorite_url(&doc, "Default", ""));
    }

    #[test]
    fn a_favourited_url_is_a_peek_source() {
        let doc = peek_fixture();
        assert!(is_peek_source_url(
            &doc,
            &doc.spaces[0],
            "Default",
            "https://fav.example/"
        ));
    }

    #[test]
    fn a_plain_tab_is_not_a_peek_source() {
        let doc = peek_fixture();
        assert!(!is_peek_source_url(
            &doc,
            &doc.spaces[0],
            "Default",
            "https://plain.example/"
        ));
    }

    #[test]
    fn a_row_pinned_in_another_space_is_not_a_peek_source_here() {
        let doc = peek_fixture();
        assert!(
            !is_peek_source_url(
                &doc,
                &doc.spaces[0],
                "Default",
                "https://elsewhere.example/"
            ),
            "pinned is per space - the same URL in another space is another tab"
        );
    }

    #[test]
    fn a_favourite_of_another_profile_is_not_a_peek_source() {
        let doc = peek_fixture();
        assert!(!is_peek_source_url(
            &doc,
            &doc.spaces[0],
            "Other",
            "https://fav.example/"
        ));
    }

    #[test]
    fn a_url_that_only_shares_an_origin_is_not_a_peek_source() {
        let doc = peek_fixture();
        assert!(
            !is_peek_source_url(
                &doc,
                &doc.spaces[0],
                "Default",
                "https://pinned.example/deeper"
            ),
            "exact match only - a prefix rule would make a whole site behave as pinned"
        );
    }

    #[test]
    fn an_empty_url_is_never_a_peek_source() {
        let doc = peek_fixture();
        assert!(!is_peek_source_url(&doc, &doc.spaces[0], "Default", ""));
    }

    /// The cold-row opener allows only http, https and about. A pinned tile
    /// stays because its x already has a remove path.
    #[test]
    fn a_row_no_reopen_path_can_carry_is_dropped_on_load() {
        let json = r##"{"spaces":[
            {"id":"A","name":"P","emoji":"H","tint":"t","profileID":"Default",
             "pinnedTabs":[{"url":"chrome://settings/passwords","title":"Pinned"}],
             "tabs":[{"url":"https://one.example/","title":"One"},
                     {"url":"chrome://settings/privacy","title":"Settings"},
                     {"url":"about:blank","title":""},
                     {"url":"CHROME://extensions","title":"Upper"},
                     {"url":"https://two.example/","title":"Two"}],
             "order":0,"groups":[]}],
        "appState":{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":6,
        "archiveEpoch":700000000,"sidebarWidthMigrated":true}}"##;
        let doc = load(json, INSTANT).unwrap();

        let urls: Vec<&str> = doc.spaces[0].tabs.iter().map(|t| t.url.as_str()).collect();
        assert_eq!(
            urls,
            [
                "https://one.example/",
                "about:blank",
                "https://two.example/"
            ]
        );
        assert_eq!(doc.spaces[0].pinned_tabs.len(), 1);
    }

    #[test]
    fn a_v1_file_runs_the_whole_chain_in_one_load() {
        let doc = load(&v1(), INSTANT).unwrap();
        assert_eq!(doc.app_state.schema_version, 6);
        assert_eq!(doc.app_state.archive_epoch, Some(INSTANT));
    }

    /// Hand-written, not generated - a real-looking file with fields already
    /// in play, so the migration is proven against more than an empty shell.
    fn v3_pre_archive_ttl() -> String {
        r##"{"spaces":[
            {"id":"A","name":"Work","emoji":"W","tint":"#112233","profileID":"Default",
             "pinnedTabs":[{"url":"https://mail.example/","title":"Mail"}],
             "tabs":[{"url":"https://a/","title":"A","groupID":"g1"}],
             "order":0,"groups":[{"id":"g1","name":"Reading","collapsed":false}],
             "hibernate":"never"},
            {"id":"B","name":"Personal","emoji":"P","tint":"#445566","profileID":"Default",
             "pinnedTabs":[],"tabs":[],"order":1,"groups":[]}],
        "appState":{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":3,
        "archiveEpoch":700000000,"sidebarWidthMigrated":true}}"##
            .to_string()
    }

    /// `archive_ttl` is absent rather than set to a rung, so a space follows
    /// the general setting rather than a hardcoded default.
    #[test]
    fn a_v3_file_migrates_to_v4_and_existing_spaces_follow_the_general_ttl() {
        let doc = load(&v3_pre_archive_ttl(), INSTANT).unwrap();

        assert_eq!(doc.app_state.schema_version, 6);
        assert_eq!(doc.spaces.len(), 2);

        let work = &doc.spaces[0];
        assert_eq!(work.name, "Work");
        assert_eq!(
            work.legacy.hibernate, None,
            "the v5 to v6 step clears the space's own copy"
        );
        assert_eq!(doc.profiles[0].hibernate.as_deref(), Some("never"));
        assert_eq!(work.pinned_tabs[0].url, "https://mail.example/");
        assert_eq!(work.tabs[0].group_id.as_deref(), Some("g1"));
        assert_eq!(
            work.archive_ttl, None,
            "no rung was ever set - Follow General, not a hardcoded default"
        );

        let personal = &doc.spaces[1];
        assert_eq!(personal.name, "Personal");
        assert_eq!(personal.archive_ttl, None);

        assert_eq!(
            lifecycle::archive_threshold_for(work, "sevenDays"),
            "sevenDays",
            "an absent per-space TTL resolves to whatever the general setting is"
        );
    }

    #[test]
    fn the_v1_migration_leaves_a_tabs_last_active_alone() {
        let doc = load(&v1(), INSTANT).unwrap();
        assert_eq!(
            doc.spaces[0].tabs[0].last_active_at, None,
            "stamping every tab makes startupTab pick arbitrarily"
        );
    }

    #[test]
    fn the_legacy_sidebar_width_is_corrected_once() {
        let doc = load(&v1(), INSTANT).unwrap();
        assert_eq!(doc.app_state.sidebar_width, DEFAULT_SIDEBAR_WIDTH);
        assert!(doc.app_state.sidebar_width_migrated);
    }

    /// The reason the one-shot bit exists: 280 chosen on purpose must survive.
    #[test]
    fn a_deliberate_280_survives_once_the_file_is_marked_migrated() {
        let json = r#"{"spaces":[],"appState":{"sidebarWidth":280,"schemaVersion":3,
        "sidebarWidthMigrated":true,"archiveEpoch":1}}"#;
        let doc = load(json, INSTANT).unwrap();
        assert_eq!(doc.app_state.sidebar_width, 280.0);
    }

    #[test]
    fn a_newer_file_is_refused_rather_than_migrated() {
        let json = r#"{"spaces":[],"appState":{"sidebarWidth":280,"schemaVersion":999}}"#;
        assert_eq!(
            load(json, INSTANT),
            Err(LoadError::Refused { file_version: 999 })
        );
    }

    #[test]
    fn favourites_are_keyed_by_profile_and_url() {
        let f = |p: &str, u: &str, s: Option<&str>| Favorite {
            url: u.into(),
            title: "t".into(),
            profile_id: p.into(),
            space_id: s.map(str::to_string),
        };
        let out = migrate_favorites(&[
            f("A", "u", Some("s1")),
            f("A", "u", Some("s2")),
            f("B", "u", None),
        ]);
        assert_eq!(
            out.len(),
            2,
            "same pair collapses, different profile does not"
        );
        assert_eq!(out[0].space_id, None, "spaceID is cleared on load");
    }

    #[test]
    fn collapsing_favourites_twice_changes_nothing() {
        let once = migrate_favorites(&[Favorite {
            url: "u".into(),
            title: "t".into(),
            profile_id: "A".into(),
            space_id: Some("s".into()),
        }]);
        assert_eq!(migrate_favorites(&once), once);
    }

    #[test]
    fn a_file_without_the_doorways_key_defaults_to_empty() {
        let doc = load(&v1(), INSTANT).unwrap();
        assert!(doc.doorways.is_empty());
    }

    #[test]
    fn a_doorway_round_trips_through_its_camel_case_keys() {
        let json = r#"{"spaces":[],"appState":{"schemaVersion":3,"sidebarWidth":220,
        "sidebarWidthMigrated":true,"archiveEpoch":1},
        "doorways":[{"url":"https://mail.example/","spaceID":"s1","profileID":"Default"}]}"#;
        let doc = load(json, INSTANT).unwrap();
        assert_eq!(
            doc.doorways,
            vec![Doorway {
                url: "https://mail.example/".into(),
                space_id: "s1".into(),
                profile_id: "Default".into(),
            }]
        );
    }

    /// A missing tintStrength is 1, not 0. Zero would wash every existing
    /// space out on the first load after the field appeared.
    #[test]
    fn a_space_without_a_tint_strength_gets_one() {
        let doc = load(&v1(), INSTANT).unwrap();
        assert_eq!(doc.spaces[0].tint_strength, 1.0);
    }

    /// Every row gets an identity, and no two share one - the binding from a
    /// live tab to a persisted row is only as good as this.
    #[test]
    fn every_row_is_numbered_and_no_two_share_a_number() {
        let json = r#"{"spaces":[
        {"id":"A","name":"P","emoji":"H","tint":"t","profileID":"Default",
         "pinnedTabs":[{"url":"p","title":"P"}],
         "tabs":[{"url":"https://u/","title":"T"},{"url":"https://u/","title":"T"}],
         "order":0,"groups":[]},
        {"id":"B","name":"Q","emoji":"H","tint":"t","profileID":"Default",
         "pinnedTabs":[],"tabs":[{"url":"https://v/","title":"V"}],"order":1,"groups":[]}],
        "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
        "sidebarWidthMigrated":true}}"#;
        let doc = load(json, INSTANT).unwrap();
        let mut tokens: Vec<RowToken> = doc
            .spaces
            .iter()
            .flat_map(|s| s.pinned_tabs.iter().chain(s.tabs.iter()))
            .map(|t| t.token)
            .collect();
        assert_eq!(tokens.len(), 4);
        assert!(
            tokens.iter().all(|&t| t != UNTOKENED),
            "an untokened row can never be bound to a live tab"
        );
        tokens.sort_unstable();
        tokens.dedup();
        assert_eq!(tokens.len(), 4, "two rows with one url still differ");
    }

    /// Sequential rather than random: two loads of one file have to produce
    /// equal documents, or nothing downstream can be compared.
    #[test]
    fn two_loads_of_one_file_give_the_same_tokens() {
        assert_eq!(load(&v1(), INSTANT).unwrap(), load(&v1(), INSTANT).unwrap());
    }

    /// The counter starts past every token already handed out, so a row added
    /// at runtime cannot collide with one that came from the file.
    #[test]
    fn a_runtime_token_never_collides_with_a_loaded_one() {
        let mut doc = load(&v1(), INSTANT).unwrap();
        let existing: Vec<RowToken> = doc.spaces[0].tabs.iter().map(|t| t.token).collect();
        let fresh = doc.allocate_token();
        assert!(!existing.contains(&fresh));
        assert_ne!(fresh, doc.allocate_token());
    }

    fn saved_ids_fixture() -> Document {
        let json = r#"{"spaces":[
            {"id":"space-2","name":"P","emoji":"H","tint":"t","profileID":"Default",
             "pinnedTabs":[],"tabs":[{"url":"https://u/","title":"T"}],"order":0,
             "groups":[{"id":"group-2","name":"G","collapsed":false}]}],
            "appState":{"activeSpaceID":"space-2","sidebarWidth":220,"schemaVersion":6,
            "sidebarWidthMigrated":true,"archiveEpoch":1},"favorites":[]}"#;
        load(json, INSTANT).unwrap()
    }

    /// Row tokens restart at 1 on every load, so an id built from the token
    /// repeated one the file already held.
    #[test]
    fn a_created_group_never_reuses_a_saved_group_id() {
        let mut doc = saved_ids_fixture();
        let id = crate::mutate::create_group(&mut doc, 0, "new").unwrap();
        assert_ne!(id, "group-2");
    }

    #[test]
    fn a_created_space_never_reuses_a_saved_space_id() {
        let mut doc = saved_ids_fixture();
        let id = crate::mutate::create_space(&mut doc, "new", "Default").unwrap();
        assert_ne!(id, "space-2");
    }

    #[test]
    fn row_tokens_stay_sequential_when_ids_raise_the_counter() {
        let doc = saved_ids_fixture();
        assert_eq!(doc.spaces[0].tabs[0].token, 1);
    }

    /// The token is runtime state. A file that carried one would change the
    /// bytes, and a file that lost one on save would break the binding.
    #[test]
    fn the_token_is_neither_written_nor_read() {
        let mut doc = load(&v1(), INSTANT).unwrap();
        doc.spaces[0].tabs[0].token = 4242;
        let json = serde_json::to_string(&doc).unwrap();
        assert!(!json.contains("4242"), "{json}");
        assert!(!json.contains("token"), "{json}");

        let back: Document = serde_json::from_str(&json).unwrap();
        assert_eq!(back.spaces[0].tabs[0].token, UNTOKENED);
    }

    /// A v4 document that already carries both canvas fields, so the round
    /// trip is proven against a real shape rather than an empty shell.
    fn v4_with_canvas() -> String {
        r##"{"spaces":[
        {"id":"A","name":"Work","emoji":"W","tint":"#112233","profileID":"Default",
         "canvas":{"x":120.0,"y":-40.0,"w":320.0,"h":260.0},
         "pinnedTabs":[{"url":"https://mail.example/","title":"Mail","canvasX":16.0}],
         "tabs":[{"url":"https://a/","title":"A","canvasX":8.5},
                 {"url":"https://b/","title":"B"}],
         "order":0,"groups":[]}],
        "appState":{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":4,
        "archiveEpoch":700000000,"sidebarWidthMigrated":true}}"##
            .to_string()
    }

    #[test]
    fn a_canvas_rect_and_a_card_offset_round_trip() {
        let doc = load(&v4_with_canvas(), INSTANT).unwrap();
        let space = &doc.spaces[0];
        assert_eq!(
            space.canvas,
            Some(CanvasRect {
                x: 120.0,
                y: -40.0,
                w: 320.0,
                h: 260.0
            })
        );
        assert_eq!(space.pinned_tabs[0].canvas_x, Some(16.0));
        assert_eq!(space.tabs[0].canvas_x, Some(8.5));
        assert_eq!(
            space.tabs[1].canvas_x, None,
            "a card with no offset stays absent, it does not gain a zero"
        );

        let once = crate::persist::to_sorted_json(&doc).unwrap();
        let again = load(&once, INSTANT).unwrap();
        assert_eq!(
            crate::persist::to_sorted_json(&again).unwrap(),
            once,
            "a canvas save must be a fixed point"
        );
    }

    /// The absent case is the one that can break every other store's bytes.
    #[test]
    fn a_document_with_no_positions_writes_no_canvas_keys() {
        let doc = load(&v3_pre_archive_ttl(), INSTANT).unwrap();
        let json = crate::persist::to_sorted_json(&doc).unwrap();
        assert!(
            !json.contains("\"canvas\""),
            "an unplaced room must not write a rect: {json}"
        );
        assert!(
            !json.contains("\"canvasX\""),
            "an unplaced card must not write an offset: {json}"
        );
    }

    /// v4 to v5 is a bump only. Both canvas fields are additive and optional,
    /// so no space and no tab needs a backfilled value.
    #[test]
    fn a_v4_file_migrates_to_v5_and_keeps_every_field() {
        let doc = load(&v4_with_canvas(), INSTANT).unwrap();

        assert_eq!(doc.app_state.schema_version, 6);
        assert_eq!(doc.spaces[0].name, "Work");
        assert_eq!(doc.spaces[0].canvas.map(|c| c.w), Some(320.0));
        assert_eq!(doc.spaces[0].tabs[0].canvas_x, Some(8.5));
    }

    /// A v3 file runs v3 to v4 and then v4 to v5 in one load, and gains no
    /// canvas value on the way.
    #[test]
    fn a_v3_file_reaches_v5_with_no_positions() {
        let doc = load(&v3_pre_archive_ttl(), INSTANT).unwrap();

        assert_eq!(doc.app_state.schema_version, 6);
        assert_eq!(doc.spaces[0].canvas, None);
        assert_eq!(doc.spaces[0].tabs[0].canvas_x, None);
    }

    /// One record per distinct profileID, named after the FIRST space that
    /// uses it - the space list's order, not the sidebar's, because a record
    /// is a fact about the file rather than about what is on screen.
    #[test]
    fn the_loader_builds_one_profile_per_distinct_profile_id() {
        let doc = load(
            r#"{"spaces":[
              {"id":"A","name":"Work","emoji":"W","tint":"t","profileID":"Default",
               "pinnedTabs":[],"tabs":[],"order":0,"groups":[]},
              {"id":"B","name":"Play","emoji":"P","tint":"t","profileID":"Second",
               "pinnedTabs":[],"tabs":[],"order":1,"groups":[]},
              {"id":"C","name":"Side","emoji":"S","tint":"t","profileID":"Default",
               "pinnedTabs":[],"tabs":[],"order":2,"groups":[]}],
              "appState":{"sidebarWidth":220,"schemaVersion":5}}"#,
            0.0,
        )
        .unwrap();

        assert_eq!(doc.profiles.len(), 2);
        assert_eq!(doc.profiles[0].id, "Default");
        assert_eq!(doc.profiles[0].name, "Work");
        assert_eq!(doc.profiles[1].id, "Second");
        assert_eq!(doc.profiles[1].name, "Play");
    }

    /// The v5 to v6 move. The per-space value goes to the profile and the
    /// space field is CLEARED, so the next save stops writing it and nothing
    /// can read two answers to the same question.
    #[test]
    fn the_v5_step_moves_the_engine_and_the_hibernate_policy_to_the_profile() {
        let doc = load(
            r#"{"spaces":[
              {"id":"A","name":"Work","emoji":"W","tint":"t","profileID":"Default",
               "pinnedTabs":[],"tabs":[],"order":0,"groups":[],
               "searchEngine":"kagi","hibernate":"always","hibThreshold":"fourHours"}],
              "appState":{"sidebarWidth":220,"schemaVersion":5}}"#,
            0.0,
        )
        .unwrap();

        assert_eq!(doc.app_state.schema_version, 6);
        assert_eq!(doc.profiles[0].search_engine.as_deref(), Some("kagi"));
        assert_eq!(doc.profiles[0].hibernate.as_deref(), Some("always"));
        assert_eq!(doc.profiles[0].hib_threshold.as_deref(), Some("fourHours"));
        assert_eq!(doc.spaces[0].legacy.search_engine, None);
        assert_eq!(doc.spaces[0].legacy.hibernate, None);
        assert_eq!(doc.spaces[0].legacy.hib_threshold, None);
    }

    /// The v5 file's per-space keys are read once and never written again.
    /// Leaving them would give the same setting two homes, and the loader
    /// would keep re-migrating a value the user had already changed.
    #[test]
    fn the_retired_per_space_keys_do_not_come_back_on_the_next_save() {
        let doc = load(
            r#"{"spaces":[
              {"id":"A","name":"Work","emoji":"W","tint":"t","profileID":"Default",
               "pinnedTabs":[],"tabs":[],"order":0,"groups":[],
               "searchEngine":"kagi","hibernate":"always","hibThreshold":"fourHours"}],
              "appState":{"sidebarWidth":220,"schemaVersion":5}}"#,
            0.0,
        )
        .unwrap();

        let text = crate::persist::to_sorted_json(&doc).unwrap();
        let value: serde_json::Value = serde_json::from_str(&text).unwrap();
        let space = &value["spaces"][0];

        // The moved value legitimately reappears under "profiles" - it is the
        // SPACE's own copy that must never come back.
        assert!(space.get("hibernate").is_none(), "{text}");
        assert!(space.get("hibThreshold").is_none(), "{text}");
        assert!(space.get("searchEngine").is_none(), "{text}");
        assert_eq!(value["profiles"][0]["searchEngine"], "kagi", "{text}");
    }

    /// First space wins. Two spaces on one profile cannot both set it, and
    /// picking the later one would silently prefer whichever was created last.
    #[test]
    fn the_first_space_on_a_profile_wins_the_engine() {
        let doc = load(
            r#"{"spaces":[
              {"id":"A","name":"Work","emoji":"W","tint":"t","profileID":"Default",
               "pinnedTabs":[],"tabs":[],"order":0,"groups":[],"searchEngine":"kagi"},
              {"id":"B","name":"Side","emoji":"S","tint":"t","profileID":"Default",
               "pinnedTabs":[],"tabs":[],"order":1,"groups":[],"searchEngine":"bing"}],
              "appState":{"sidebarWidth":220,"schemaVersion":5}}"#,
            0.0,
        )
        .unwrap();

        assert_eq!(doc.profiles.len(), 1);
        assert_eq!(doc.profiles[0].search_engine.as_deref(), Some("kagi"));
    }

    /// A space whose profileID could escape user_data_dir gets no record.
    /// The id is a directory name, and IsSafeProfileId is the C++ half of
    /// this same rule.
    #[test]
    fn an_unsafe_profile_id_gets_no_record() {
        let doc = load(
            r#"{"spaces":[
              {"id":"A","name":"Work","emoji":"W","tint":"t","profileID":"../etc",
               "pinnedTabs":[],"tabs":[],"order":0,"groups":[]}],
              "appState":{"sidebarWidth":220,"schemaVersion":5}}"#,
            0.0,
        )
        .unwrap();

        assert!(doc.profiles.is_empty());
    }

    /// `legacy` is skip_serializing, so the round trip is what proves these
    /// are truly gone, not just absent from this one in-memory Document.
    #[test]
    fn an_unsafe_profile_id_loses_its_legacy_fields_for_good() {
        let doc = load(
            r#"{"spaces":[
              {"id":"A","name":"Work","emoji":"W","tint":"t","profileID":"../etc",
               "pinnedTabs":[],"tabs":[],"order":0,"groups":[],
               "searchEngine":"kagi","hibernate":"always","hibThreshold":"fourHours"}],
              "appState":{"sidebarWidth":220,"schemaVersion":5}}"#,
            0.0,
        )
        .unwrap();

        assert!(doc.profiles.is_empty());
        assert_eq!(doc.spaces[0].legacy, RetiredSpaceFields::default());

        let text = crate::persist::to_sorted_json(&doc).unwrap();
        let reloaded = load(&text, 0.0).unwrap();
        assert_eq!(reloaded.spaces[0].legacy, doc.spaces[0].legacy);
    }

    /// Loading twice must not add a second record for the same id, or the
    /// list grows by one on every launch.
    #[test]
    fn the_backfill_is_idempotent_across_a_reload() {
        let once = load(
            r#"{"spaces":[
              {"id":"A","name":"Work","emoji":"W","tint":"t","profileID":"Default",
               "pinnedTabs":[],"tabs":[],"order":0,"groups":[]}],
              "appState":{"sidebarWidth":220,"schemaVersion":5}}"#,
            0.0,
        )
        .unwrap();
        let text = crate::persist::to_sorted_json(&once).unwrap();

        let twice = load(&text, 0.0).unwrap();

        assert_eq!(twice.profiles, once.profiles);
    }

    /// A pinned member names a group this space does not have, and the other
    /// group's members arrive split by a loose pin. Both break the item list.
    #[test]
    fn loading_normalises_the_pinned_groups() {
        let doc = load(
            r#"{"spaces":[
              {"id":"A","name":"Work","emoji":"W","tint":"t","profileID":"Default",
               "pinnedTabs":[{"url":"https://g0/","title":"G0","groupID":"G"},
                             {"url":"https://p/","title":"P","groupID":"gone"},
                             {"url":"https://g1/","title":"G1","groupID":"G"}],
               "tabs":[],"order":0,"groups":[{"id":"G","name":"Reading","collapsed":false}]}],
              "appState":{"sidebarWidth":220,"schemaVersion":6}}"#,
            0.0,
        )
        .unwrap();

        let space = &doc.spaces[0];
        let urls: Vec<&str> = space.pinned_tabs.iter().map(|t| t.url.as_str()).collect();
        assert_eq!(urls, vec!["https://g0/", "https://g1/", "https://p/"]);
        assert_eq!(space.pinned_tabs[2].group_id, None, "the group is gone");
        assert_eq!(
            pinned_items(space),
            vec![PinnedItem::Folder(0), PinnedItem::Tab(2)],
            "one folder, drawn where its first member sits"
        );
    }

    /// A regular tab sharing a pinned tab's group id drew nowhere: the
    /// folder excludes it, and so does the flat list.
    #[test]
    fn loading_repairs_a_group_split_across_pinned_and_regular_tabs() {
        let doc = load(
            r#"{"spaces":[
              {"id":"A","name":"Work","emoji":"W","tint":"t","profileID":"Default",
               "pinnedTabs":[{"url":"https://pinned/","title":"Pinned","groupID":"G"}],
               "tabs":[{"url":"https://split/","title":"Split","groupID":"G"},
                       {"url":"https://plain/","title":"Plain"}],
               "order":0,"groups":[{"id":"G","name":"Reading","collapsed":false}]}],
              "appState":{"sidebarWidth":220,"schemaVersion":6}}"#,
            0.0,
        )
        .unwrap();

        let space = &doc.spaces[0];
        assert_eq!(
            space.tabs[0].group_id, None,
            "the split member rejoins the flat list"
        );

        let mut live = crate::sidebar::LiveTabIds::new(&[], None, &[], None);
        let snapshot =
            crate::sidebar::snapshot(0, &doc.spaces, &doc.favorites, &mut live, &[], &[]);
        assert_eq!(snapshot.pinned.len(), 1, "one folder for the pinned member");
        assert!(
            snapshot.folders.is_empty(),
            "the group still has a pinned member"
        );
        let tab_urls: Vec<&str> = snapshot.tabs.iter().map(|r| r.url.as_str()).collect();
        assert_eq!(
            tab_urls,
            vec!["https://split/", "https://plain/"],
            "every row renders somewhere"
        );
    }

    #[test]
    fn a_v7_file_is_still_refused() {
        let err = load(
            r#"{"spaces":[],"appState":{"sidebarWidth":220,"schemaVersion":7}}"#,
            0.0,
        )
        .unwrap_err();
        assert!(matches!(err, LoadError::Refused { file_version: 7 }));
    }
}
