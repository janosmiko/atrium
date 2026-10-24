// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The sidebar's model, encoded for the HTML arm.
//!
//! Ported from `SidebarModelJSON.swift`. The sidebar is the one surface that
//! survives the cut, so this document has to come out identical before Swift
//! can go - `fixtures/sidebar-model/five-profiles.json` is what "identical"
//! points at.
//!
//! Pure, and deliberately so. It takes model values and returns a string; it
//! touches no ABI, no C++ and no view.

use std::collections::{BTreeMap, HashSet};

use serde::Serialize;

use crate::small_stores::{self, ThemeSpec};
use crate::spaces::{Favorite, RowToken, Space, SpaceTab, UNTOKENED};
use crate::tabs::Bindings;
use crate::tint;

/// A tab as the sidebar renders it. Ids are STRINGS, matching what the C++
/// side emits - it keys rows by live tab id and the two must agree.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct Row {
    pub id: String,
    pub title: String,
    pub color: String,
    /// The row holding the tab this window is SHOWING - the active-row pill.
    ///
    /// A per-window fact, not a stored one: the document records an active
    /// space and nothing finer, and two windows on one space show different
    /// tabs. It arrives with the live strip, which is already per window.
    pub selected: bool,
    /// Carried for EVERY row, not just cold ones. A row with no live tab has
    /// an empty id, and an empty id is not something the browser can act on -
    /// the URL is what lets the surface ask for it to be reopened instead.
    pub url: String,
    /// Omitted when false, so an unmarked document still encodes the bytes
    /// `fixtures/sidebar-model/five-profiles.json` pins.
    #[serde(default, skip_serializing_if = "is_false")]
    pub marked: bool,
    /// The live tab a FAVOURITE tile activates. Separate from `id` because a
    /// favourite's `id` is its URL, which the favicon source is keyed on.
    #[serde(default, skip_serializing_if = "String::is_empty", rename = "tabId")]
    pub tab_id: String,
    /// The tab this row's token is bound to, when that tab is in ANOTHER
    /// window's strip. Never `id`, which means live in this window.
    #[serde(
        default,
        skip_serializing_if = "String::is_empty",
        rename = "elsewhereTabId"
    )]
    pub elsewhere_tab_id: String,
    /// Set with `elsewhere_tab_id`: the tab is open next door, so a click
    /// takes it here rather than opening the page again.
    #[serde(default, skip_serializing_if = "is_false")]
    pub elsewhere: bool,
    /// The card's x offset inside its Courtyard lane. Omitted when absent, so
    /// the pinned snapshot fixtures still encode the same bytes for a document
    /// that has never been laid out.
    ///
    /// Renamed by hand: `Row` carries no `rename_all = "camelCase"`, and
    /// `tabId` above is spelled out the same way. Without the rename the JS
    /// would read `canvas_x` and every card would sit at x 0.
    #[serde(default, skip_serializing_if = "Option::is_none", rename = "canvasX")]
    pub canvas_x: Option<f64>,
}

fn is_false(b: &bool) -> bool {
    !*b
}

/// A tab group, which the sidebar draws as a foldable folder.
#[derive(Debug, Clone, PartialEq, Serialize)]
pub struct Folder {
    pub id: String,
    pub name: String,
    pub closed: bool,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub icon: Option<String>,
    pub tabs: Vec<Row>,
}

/// One entry of the pinned section: a pin, or a folder drawn at its first
/// member's place. `kind` tells the surface which of the two it is.
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(tag = "kind", rename_all = "camelCase")]
pub enum PinnedEntry {
    Tab(Row),
    Folder(Folder),
}

impl PinnedEntry {
    pub fn rows(&self) -> &[Row] {
        match self {
            Self::Tab(row) => std::slice::from_ref(row),
            Self::Folder(folder) => &folder.tabs,
        }
    }

    fn rows_mut(&mut self) -> &mut [Row] {
        match self {
            Self::Tab(row) => std::slice::from_mut(row),
            Self::Folder(folder) => &mut folder.tabs,
        }
    }
}

/// One entry in the space switcher and one pane of the swipe rail.
///
/// Carries its own contents. Every pane used to render the ACTIVE space's rows
/// retinted, so a swipe showed the outgoing space's icons until a rebuild
/// landed, and a cross-space drop had no list to resolve a destination against.
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct SpaceChip {
    pub name: String,
    /// Already a colour, never the stored ref - see `crate::tint`.
    pub color: String,
    pub icon: String,
    /// Which profile this space's tabs run under.
    ///
    /// The surface cannot derive it: favourites are already filtered to the
    /// space's profile before they get here, so a chip that dropped the id
    /// left "which profile am I in, and which others exist" unanswerable -
    /// which is exactly what Mirror View's choice list asks.
    pub profile_id: String,
    /// Omitted when empty so the surface falls back to the id.
    #[serde(skip_serializing_if = "String::is_empty")]
    pub profile_name: String,
    pub tabs: Vec<Row>,
    pub pinned: Vec<PinnedEntry>,
    pub folders: Vec<Folder>,
    pub pinned_collapsed: bool,
    /// The grid THIS space shows. Favourites are per-profile and each space
    /// carries its own profile, so a rail whose panes shared one grid drew the
    /// active space's tiles everywhere - and the tiles are the most visible
    /// thing in the sidebar, so a swipe still read as "the content lags the
    /// animation" after the rows were fixed.
    pub favorites: Vec<Row>,
    /// This room's rectangle on the Courtyard plane. Absent means unplaced,
    /// and the view puts the room right of the rightmost placed one.
    #[serde(default, skip_serializing_if = "Option::is_none")]
    pub canvas: Option<crate::spaces::CanvasRect>,
    /// This space's theme, from the themes store keyed by space id. `null`
    /// rather than omitted when there is none, so the studio can tell "no
    /// theme yet" apart from a key it forgot to read.
    pub theme: Option<ThemeSpec>,
    /// Carried so `mark_themes` can look a chip up in the themes map without
    /// re-deriving sidebar order. Not part of the wire shape.
    #[serde(skip_serializing)]
    pub space_id: String,
}

#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Snapshot {
    pub spaces: Vec<SpaceChip>,
    pub active_space: usize,
    pub space_name: String,
    /// Already a colour, never the stored ref - see `crate::tint`.
    pub space_tint: String,
    pub favorites: Vec<Row>,
    pub pinned: Vec<PinnedEntry>,
    pub folders: Vec<Folder>,
    pub tabs: Vec<Row>,
    /// Whether the pinned SECTION is folded - the same stored per-space field
    /// the other arm honours, so the two agree after a restart.
    pub pinned_collapsed: bool,
    /// The ACTIVE space's theme - see `SpaceChip::theme` for why `null` is
    /// explicit rather than omitted.
    pub space_theme: Option<ThemeSpec>,
}

/// A stable per-host tint, standing in for a favicon that has not resolved.
///
/// Byte-identical to `ColorForHost` in `atrium_sidebar_tabs.mm` and to the
/// Swift one - same FNV-1a seed, same prime, same `% 360` - so a row keeps its
/// colour whichever producer built the snapshot. Two hashes that merely look
/// alike would make rows change colour when the producer changes, which reads
/// as a bug and is very hard to attribute.
pub fn color_for_host(host: &str) -> String {
    let mut hash: u32 = 2166136261;
    for byte in host.bytes() {
        hash = (hash ^ u32::from(byte)).wrapping_mul(16777619);
    }
    format!("hsl({},65%,55%)", hash % 360)
}

/// One window's live strip: hands out the live tab id for a persisted tab,
/// CONSUMING each one so two rows never share a tab, and knows which of them
/// the window is showing.
///
/// `SpaceTab` has no id, so URL is the only join there is. What matters is
/// that this is a queue rather than a lookup: a first-match-wins map hands the
/// same id to every row sharing a URL - two blank tabs, two tabs on one site -
/// and every one of those rows then activates the same tab.
///
/// Returns "" once a URL's tabs are used up, which is the existing contract
/// for a COLD row: no favicon, and inert rather than wired to somebody else's
/// tab.
///
/// The active tab rides along here rather than beside it because selection is
/// decided by the SAME consuming join: the selected row is whichever one the
/// active id was handed to. Answering it any other way - by URL, say - marks
/// every row sharing that URL, which is the bug the queue exists to prevent.
#[derive(Debug, Default)]
pub struct LiveTabIds {
    queues: Vec<(String, Vec<u64>)>,
    /// The row each live tab is bound to, for the tabs in THIS window's strip.
    ///
    /// the url join answers with the document's last news of the
    /// page, and a tab can move without the document hearing it - a
    /// same-document navigation fires no PrimaryPageChanged, so note_page_url
    /// never runs and the row keeps the url it had. The binding is what the
    /// core already knows and what no navigation moves.
    by_token: Vec<(RowToken, u64)>,
    /// Rows bound to a tab that is NOT in this window's strip. Such a row is
    /// not cold and must not fall back to the url queue: its tab is known, it
    /// is just next door.
    elsewhere: Vec<(RowToken, u64)>,
    /// Favourites claimed by a tab in another window, by favourite url.
    elsewhere_claims: Vec<(String, u64)>,
    /// The strip's active tab, stringified once so `is_active` compares the
    /// values `take` actually hands out. `None` when the caller does not say,
    /// and then no row is selected - which is what the sidebar drew before
    /// anything answered this at all.
    active: Option<String>,
}

impl LiveTabIds {
    /// `elsewhere_claims` names the favourites whose tab is in another window
    /// of this profile. `bound`, when the caller has a bindings store to join
    /// against, is the store and the ids of tabs living in other windows -
    /// a binding to neither list is stale or foreign, and its row stays cold.
    pub fn new(
        live: &[(String, u64)],
        active: Option<u64>,
        elsewhere_claims: &[(String, u64)],
        bound: Option<(&Bindings, &[u64])>,
    ) -> Self {
        let mut queues: Vec<(String, Vec<u64>)> = Vec::new();
        for (url, id) in live {
            match queues.iter_mut().find(|(u, _)| u == url) {
                Some((_, ids)) => ids.push(*id),
                None => queues.push((url.clone(), vec![*id])),
            }
        }
        let mut by_token = Vec::new();
        let mut elsewhere = Vec::new();
        if let Some((bindings, elsewhere_ids)) = bound {
            for (_, ids) in &queues {
                for id in ids {
                    if let Some(token) = bindings.token_of(*id) {
                        by_token.push((token, *id));
                    }
                }
            }
            // Never by url: matching other windows' tabs by url let a cold
            // row steal a tab bound to a row in another space.
            let here = |id: u64| queues.iter().any(|(_, ids)| ids.contains(&id));
            elsewhere = bindings
                .iter()
                .filter(|(id, _)| !here(*id) && elsewhere_ids.contains(id))
                .map(|(id, token)| (token, id))
                .collect();
            // The same holds inside this strip: it carries every space of the
            // profile, so a bound tab's row may not be in this space.
            for (_, ids) in &mut queues {
                ids.retain(|id| bindings.token_of(*id).is_none());
            }
        }
        Self {
            queues,
            by_token,
            elsewhere,
            elsewhere_claims: elsewhere_claims.to_vec(),
            active: active.map(|id| id.to_string()),
        }
    }

    pub fn elsewhere_claims(&self) -> &[(String, u64)] {
        &self.elsewhere_claims
    }

    /// The live tab for a row, by its binding when there is one and by its url
    /// otherwise. Consuming either way, for the reason on `take`.
    fn take_for(&mut self, token: RowToken, url: &str) -> String {
        if token != UNTOKENED {
            if let Some(position) = self.by_token.iter().position(|(held, _)| *held == token) {
                let (_, id) = self.by_token.remove(position);
                self.drop_id(id);
                return id.to_string();
            }
            if self.elsewhere_for(token).is_some() {
                return String::new();
            }
        }
        self.take(url)
    }

    /// The tab in another window that `token`'s row is bound to.
    fn elsewhere_for(&self, token: RowToken) -> Option<u64> {
        self.elsewhere
            .iter()
            .find(|(held, _)| *held == token)
            .map(|(_, id)| *id)
    }

    /// A read-only copy of the elsewhere table, for the panes that do not
    /// consume live ids.
    fn elsewhere_table(&self) -> Vec<(RowToken, u64)> {
        self.elsewhere.clone()
    }

    /// Removes `id` from its url queue, so a row matching that url cannot take
    /// the tab a token already took.
    fn drop_id(&mut self, id: u64) {
        for (_, ids) in &mut self.queues {
            if let Some(position) = ids.iter().position(|held| *held == id) {
                ids.remove(position);
                return;
            }
        }
    }

    pub fn take(&mut self, url: &str) -> String {
        let Some((_, ids)) = self.queues.iter_mut().find(|(u, _)| u == url) else {
            return String::new();
        };
        if ids.is_empty() {
            return String::new();
        }
        ids.remove(0).to_string()
    }

    /// Whether `id` names the tab this window is showing.
    ///
    /// An empty id is never active. A COLD row has no tab to be showing, and
    /// without the guard a window that named no active tab would match every
    /// cold row at once.
    fn is_active(&self, id: &str) -> bool {
        !id.is_empty() && self.active.as_deref() == Some(id)
    }

    /// The strip's active tab, for a caller that holds no mutable borrow.
    fn active_id(&self) -> Option<&str> {
        self.active.as_deref()
    }
}

fn row(tab: &SpaceTab, live: Option<&mut LiveTabIds>, elsewhere: &[(RowToken, u64)]) -> Row {
    let elsewhere_tab_id = elsewhere
        .iter()
        .find(|(held, _)| *held == tab.token && tab.token != UNTOKENED)
        .map(|(_, id)| id.to_string())
        .unwrap_or_default();
    let (id, selected) = match live {
        Some(ids) => {
            let id = ids.take_for(tab.token, &tab.url);
            // A frozen window's strip names the tab that left as active, so
            // the row it left stays the selected one.
            let selected = ids.is_active(&id) || ids.is_active(&elsewhere_tab_id);
            (id, selected)
        }
        None => (String::new(), false),
    };
    Row {
        id,
        // The user's rename wins over the page title, matching the other arm.
        // A snapshot that ignored it would silently undo tab renaming here.
        title: tab
            .custom_title
            .clone()
            .unwrap_or_else(|| tab.title.clone()),
        color: color_for_host(small_stores::host_of(&tab.url).unwrap_or(&tab.url)),
        selected,
        url: tab.url.clone(),
        marked: false,
        tab_id: String::new(),
        elsewhere: !elsewhere_tab_id.is_empty(),
        elsewhere_tab_id,
        canvas_x: tab.canvas_x,
    }
}

struct Contents {
    pinned: Vec<PinnedEntry>,
    folders: Vec<Folder>,
    tabs: Vec<Row>,
}

/// One space's own rows. Built for EVERY space, so each rail pane renders its
/// own contents instead of the active space's retinted.
fn contents_of(
    space: &Space,
    mut live: Option<&mut LiveTabIds>,
    elsewhere: &[(RowToken, u64)],
    hidden: &[RowToken],
) -> Contents {
    // A favourite's page is not a tab row, in the flat list or in a folder,
    // and neither is a row the window draws as a tile. Pinned
    // tabs are left alone: pinning is a per-space act the user took.
    //
    // Provenance only, never URL: an ordinary tab that merely navigated to a
    // favourite's address is not the tile's tab, and matching on the url
    // swallowed that tab's own row.
    let is_favorite = |t: &&SpaceTab| hidden.contains(&t.token);

    let pinned: Vec<PinnedEntry> = crate::spaces::pinned_items(space)
        .into_iter()
        .map(|item| match item {
            crate::spaces::PinnedItem::Tab(index) => PinnedEntry::Tab(row(
                &space.pinned_tabs[index],
                live.as_deref_mut(),
                elsewhere,
            )),
            crate::spaces::PinnedItem::Folder(at) => {
                let group = &space.groups[at];
                PinnedEntry::Folder(Folder {
                    id: group.id.clone(),
                    name: group.name.clone(),
                    closed: group.collapsed,
                    icon: group.icon.clone(),
                    tabs: space
                        .pinned_tabs
                        .iter()
                        .filter(|t| t.group_id.as_deref() == Some(group.id.as_str()))
                        .map(|t| row(t, live.as_deref_mut(), elsewhere))
                        .collect(),
                })
            }
        })
        .collect();

    // Grouped tabs belong to their folder, not to the flat list. A folder
    // named by a tab that no longer exists renders empty, and one whose
    // members are pinned is drawn in the pinned list instead.
    let folders: Vec<Folder> = space
        .groups
        .iter()
        .filter(|group| {
            !space
                .pinned_tabs
                .iter()
                .any(|t| t.group_id.as_deref() == Some(group.id.as_str()))
        })
        .map(|group| Folder {
            id: group.id.clone(),
            name: group.name.clone(),
            closed: group.collapsed,
            icon: group.icon.clone(),
            tabs: space
                .tabs
                .iter()
                .filter(|t| t.group_id.as_deref() == Some(group.id.as_str()))
                .filter(|t| !is_favorite(t))
                .map(|t| row(t, live.as_deref_mut(), elsewhere))
                .collect(),
        })
        .collect();

    let tabs: Vec<Row> = space
        .tabs
        .iter()
        .filter(|t| t.group_id.is_none())
        .filter(|t| !is_favorite(t))
        .map(|t| row(t, live.as_deref_mut(), elsewhere))
        .collect();

    Contents {
        pinned,
        folders,
        tabs,
    }
}

/// A space shows its PROFILE's favourites - all of them, never a subset. That
/// is the whole difference between a favourite and a pinned tab: pinned tabs
/// belong to a space, favourites follow the profile and appear on every space
/// of it.
///
/// `Favorite.space_id` is legacy and must not be matched on. The store nils it
/// on load precisely so an old per-space document collapses to this rule.
fn favorite_rows(
    space: &Space,
    favorites: &[Favorite],
    claims: &[(String, u64)],
    elsewhere_claims: &[(String, u64)],
    active_tab: Option<&str>,
) -> Vec<Row> {
    let claim_in = |claims: &[(String, u64)], url: &str| {
        claims
            .iter()
            .find(|(key, _)| key == url)
            .map(|(_, id)| id.to_string())
            .unwrap_or_default()
    };
    favorites
        .iter()
        .filter(|f| f.profile_id == space.profile_id)
        .map(|f| {
            // The tab it CLAIMED, never one that merely shares its url - an
            // ordinary tab that navigated there is a stranger.
            // Read off the strip, since the tab is a row of no space.
            let tab_id = claim_in(claims, &f.url);
            // Only when no tab here claims it: a tile means one tab, and the
            // one in this window wins over the one next door.
            let elsewhere_tab_id = if tab_id.is_empty() {
                claim_in(elsewhere_claims, &f.url)
            } else {
                String::new()
            };
            let is_active = |id: &str| !id.is_empty() && active_tab == Some(id);
            let selected = is_active(&tab_id) || is_active(&elsewhere_tab_id);
            Row {
                // The URL is the id: a favourite has no id of its own, and
                // (profileID, url) is what identifies one - the profile half is
                // implied by which space this is.
                id: f.url.clone(),
                title: f.title.clone(),
                color: color_for_host(small_stores::host_of(&f.url).unwrap_or(&f.url)),
                selected,
                url: f.url.clone(),
                marked: false,
                tab_id,
                elsewhere: !elsewhere_tab_id.is_empty(),
                elsewhere_tab_id,
                canvas_x: None,
            }
        })
        .collect()
}

/// Derives the snapshot for `active_index` from the ordered spaces.
///
/// `live` maps a persisted tab to the live Chromium tab id the C++ side knows
/// it by, and carries the one this window is showing. Rows whose tab is not
/// live get an empty id, which sidebar.js already tolerates, and at most one
/// row across the whole snapshot comes back selected.
/// Which space the document itself says is active, as an index into the
/// spaces in stored order.
///
/// A caller that knows the window's slot passes its own index. A caller that
/// does NOT - C++ with no Swift to ask - uses this, because `activeSpaceID` is
/// the only answer the file carries. Falls back to the first space when the id
/// names nothing, which is what a fresh profile shows.
pub fn active_index_from_document(spaces: &[Space], active_space_id: Option<&str>) -> usize {
    let Some(id) = active_space_id else {
        return 0;
    };
    let mut ordered: Vec<&Space> = spaces.iter().collect();
    ordered.sort_by_key(|s| s.order);
    ordered.iter().position(|s| s.id == id).unwrap_or(0)
}

pub fn snapshot(
    active_index: usize,
    spaces: &[Space],
    favorites: &[Favorite],
    live: &mut LiveTabIds,
    hidden: &[RowToken],
    claims: &[(String, u64)],
) -> Snapshot {
    let mut ordered: Vec<&Space> = spaces.iter().collect();
    ordered.sort_by_key(|s| s.order);

    if ordered.is_empty() {
        return Snapshot {
            spaces: Vec::new(),
            active_space: 0,
            space_name: String::new(),
            space_tint: String::new(),
            favorites: Vec::new(),
            pinned: Vec::new(),
            folders: Vec::new(),
            tabs: Vec::new(),
            pinned_collapsed: false,
            space_theme: None,
        };
    }
    let index = if active_index < ordered.len() {
        active_index
    } else {
        0
    };

    // Live ids go to the ACTIVE space alone. A live tab belongs to the
    // window's active space, and the join CONSUMES by URL - so handing the
    // same pool to every space would let a tab live in one lend its id to an
    // identical URL in another, and clicking that row would activate somebody
    // else's tab.
    // The tiles do NOT consume from that pool: a favourite's tab is named by
    // the strip's claim, so every space of the profile answers the same tab
    // and none of them takes it away from another.
    let active_tab = live.active_id().map(str::to_string);
    let active_favorites = favorite_rows(
        ordered[index],
        favorites,
        claims,
        live.elsewhere_claims(),
        active_tab.as_deref(),
    );
    // Rows bound to another window's tab are named on EVERY space, so a click
    // on one moves that tab here instead of opening a copy.
    let elsewhere = live.elsewhere_table();

    let per_space: Vec<Contents> = ordered
        .iter()
        .enumerate()
        .map(|(position, space)| {
            if position == index {
                contents_of(space, Some(live), &elsewhere, hidden)
            } else {
                contents_of(space, None, &elsewhere, hidden)
            }
        })
        .collect();

    let chips: Vec<SpaceChip> = ordered
        .iter()
        .enumerate()
        .zip(per_space.iter())
        .map(|((position, space), content)| SpaceChip {
            name: space.name.clone(),
            color: tint::resolve_hex(&space.tint),
            icon: space.emoji.clone(),
            profile_id: space.profile_id.clone(),
            profile_name: String::new(),
            tabs: content.tabs.clone(),
            pinned: content.pinned.clone(),
            folders: content.folders.clone(),
            pinned_collapsed: space.pinned_collapsed,
            favorites: if position == index {
                active_favorites.clone()
            } else {
                favorite_rows(
                    space,
                    favorites,
                    claims,
                    live.elsewhere_claims(),
                    active_tab.as_deref(),
                )
            },
            canvas: space.canvas,
            theme: None,
            space_id: space.id.clone(),
        })
        .collect();

    let active = ordered[index];
    let active_content = &per_space[index];

    Snapshot {
        spaces: chips,
        active_space: index,
        space_name: active.name.clone(),
        space_tint: tint::resolve_hex(&active.tint),
        // Favourites are the profile-global grid; pinned tabs are the section
        // below it. They are different things and were briefly collapsed into
        // one, which drew a user's pins as tiles.
        favorites: active_favorites,
        // Reused, not rebuilt: the live-id join consumes, so a second pass over
        // the same tabs would hand every row an empty id.
        pinned: active_content.pinned.clone(),
        folders: active_content.folders.clone(),
        tabs: active_content.tabs.clone(),
        pinned_collapsed: active.pinned_collapsed,
        space_theme: None,
    }
}

/// A pass over the finished snapshot rather than a parameter on `snapshot`,
/// which takes model values and not the whole document.
pub fn mark_marginalia(snapshot: &mut Snapshot, marked_urls: &HashSet<String>) {
    if marked_urls.is_empty() {
        return;
    }
    let mark = |rows: &mut [Row]| {
        for row in rows {
            row.marked = marked_urls.contains(&row.url);
        }
    };
    mark(&mut snapshot.tabs);
    for entry in &mut snapshot.pinned {
        mark(entry.rows_mut());
    }
    for folder in &mut snapshot.folders {
        mark(&mut folder.tabs);
    }
    let active = snapshot.active_space;
    if let Some(chip) = snapshot.spaces.get_mut(active) {
        mark(&mut chip.tabs);
        for entry in &mut chip.pinned {
            mark(entry.rows_mut());
        }
        for folder in &mut chip.folders {
            mark(&mut folder.tabs);
        }
    }
}

/// A pass over the finished snapshot rather than a parameter on `snapshot`,
/// same reason as `mark_marginalia`. One pass over the chips `snapshot`
/// already built in sidebar order - no second sort of `spaces`.
pub fn mark_themes(snapshot: &mut Snapshot, themes: &BTreeMap<String, ThemeSpec>) {
    if themes.is_empty() {
        return;
    }
    for chip in &mut snapshot.spaces {
        chip.theme = themes.get(&chip.space_id).cloned();
    }
    snapshot.space_theme = snapshot
        .spaces
        .get(snapshot.active_space)
        .and_then(|chip| themes.get(&chip.space_id))
        .cloned();
}

/// Labels each chip with its profile's display name. A profile the list does
/// not name keeps an empty name, which `encode` omits.
pub fn mark_profile_names(snapshot: &mut Snapshot, profiles: &[crate::spaces::Profile]) {
    for chip in &mut snapshot.spaces {
        if let Some(profile) = profiles.iter().find(|p| p.id == chip.profile_id) {
            chip.profile_name = profile.name.clone();
        }
    }
}

/// The JSON the /sidebar route consumes. An empty object on failure, because
/// that still parses on the far side and the sidebar renders nothing rather
/// than throwing inside the setter.
pub fn encode(snapshot: &Snapshot) -> String {
    serde_json::to_string(snapshot).unwrap_or_else(|_| "{}".to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tab(url: &str, title: &str) -> SpaceTab {
        SpaceTab::new(url, title)
    }

    fn space(name: &str, profile: &str, order: i64) -> Space {
        Space {
            id: format!("id-{name}"),
            name: name.into(),
            emoji: "X".into(),
            tint: "#000000".into(),
            profile_id: profile.into(),
            pinned_tabs: Vec::new(),
            tabs: Vec::new(),
            order,
            groups: Vec::new(),
            split: None,
            pinned_collapsed: false,
            tint_strength: 1.0,
            archive_ttl: None,
            legacy: Default::default(),
            canvas: None,
        }
    }

    /// The colour has to match `ColorForHost` in the C++ tab strip and the
    /// Swift producer exactly, or a row changes colour when the producer does.
    #[test]
    fn the_host_colour_matches_the_other_producers() {
        assert_eq!(color_for_host("example.com"), "hsl(278,65%,55%)");
        assert_eq!(color_for_host("example.org"), "hsl(345,65%,55%)");
        assert_eq!(color_for_host("example.net"), "hsl(64,65%,55%)");
        assert_eq!(color_for_host("example.edu"), "hsl(331,65%,55%)");
    }

    /// The url join is only as good as the document's last news of
    /// the page, and a tab can move without the document hearing it. The
    /// binding does not care where the page went.
    #[test]
    fn a_bound_row_keeps_its_tab_when_the_page_moved_without_telling_the_document() {
        let mut bindings = Bindings::new();
        let mut row = tab("https://fav.test/stale", "Stale");
        row.token = 41;
        assert!(bindings.bind(7, row.token));

        let mut ids = LiveTabIds::new(
            &[("https://fav.test/moved-on".into(), 7)],
            Some(7),
            &[],
            Some((&bindings, &[])),
        );
        assert_eq!(
            ids.take_for(row.token, &row.url),
            "7",
            "the row's own tab, found without comparing urls"
        );
    }

    /// The binding must consume too. Without it the same tab is handed out
    /// twice - once by token, once by url - and both rows activate it.
    #[test]
    fn a_row_taken_by_token_is_not_offered_again_by_url() {
        let mut bindings = Bindings::new();
        assert!(bindings.bind(7, 41));
        let mut ids = LiveTabIds::new(
            &[("https://one.test/".into(), 7)],
            None,
            &[],
            Some((&bindings, &[])),
        );

        assert_eq!(ids.take_for(41, "https://elsewhere.test/"), "7");
        assert_eq!(
            ids.take("https://one.test/"),
            "",
            "the url queue still held the tab the token already took"
        );
    }

    /// `id` means live in THIS window, which the selection, discarded and
    /// elsewhere merges all read that way. A binding is process-wide.
    #[test]
    fn a_binding_to_a_tab_in_another_window_gives_this_row_no_id() {
        let mut bindings = Bindings::new();
        assert!(bindings.bind(9, 41));
        let mut ids = LiveTabIds::new(
            &[("https://here.test/".into(), 7)],
            None,
            &[],
            Some((&bindings, &[9])),
        );

        assert_eq!(ids.take_for(41, "https://nothing.test/"), "");
    }

    /// The url queue must not answer for a row whose tab is elsewhere: that
    /// is how a cold row on a shared url stole another window's tab.
    #[test]
    fn a_row_bound_elsewhere_does_not_fall_back_to_the_url_queue() {
        let mut bindings = Bindings::new();
        assert!(bindings.bind(9, 41));
        let mut ids = LiveTabIds::new(
            &[("https://same.test/".into(), 7)],
            None,
            &[],
            Some((&bindings, &[9])),
        );

        assert_eq!(ids.take_for(41, "https://same.test/"), "");
        assert_eq!(ids.elsewhere_for(41), Some(9));
        assert_eq!(
            ids.take("https://same.test/"),
            "7",
            "the local tab stays for its own row"
        );
    }

    /// A binding to a tab in this window's strip is not elsewhere.
    #[test]
    fn a_binding_to_a_local_tab_is_not_elsewhere() {
        let mut bindings = Bindings::new();
        assert!(bindings.bind(7, 41));
        let ids = LiveTabIds::new(
            &[("https://here.test/".into(), 7)],
            None,
            &[],
            Some((&bindings, &[7])),
        );
        assert_eq!(ids.elsewhere_for(41), None);
    }

    /// A binding to a tab no window reports is stale, or names another
    /// profile's tab. The row is cold, and the url queue may answer for it.
    #[test]
    fn a_binding_no_window_confirms_leaves_the_row_cold() {
        let mut bindings = Bindings::new();
        assert!(bindings.bind(9, 41));
        let mut ids = LiveTabIds::new(
            &[("https://same.test/".into(), 7)],
            None,
            &[],
            Some((&bindings, &[])),
        );
        assert_eq!(ids.elsewhere_for(41), None);
        assert_eq!(ids.take_for(41, "https://same.test/"), "7");
    }

    /// One strip holds every space of its profile, so a url match can be
    /// another space's tab. A close on the cold row then closed that tab.
    #[test]
    fn a_tab_bound_to_another_row_is_not_offered_by_url() {
        let mut a = space("A", "p", 0);
        let mut cold = tab("https://same.test/", "Cold here");
        cold.token = 21;
        a.tabs = vec![cold];
        let mut b = space("B", "p", 1);
        let mut owner = tab("https://same.test/", "Owner");
        owner.token = 22;
        b.tabs = vec![owner];

        let mut bindings = Bindings::new();
        assert!(bindings.bind(7, 22));
        let mut ids = LiveTabIds::new(
            &[("https://same.test/".into(), 7)],
            Some(7),
            &[],
            Some((&bindings, &[])),
        );
        let snap = snapshot(0, &[a, b], &[], &mut ids, &[], &[]);

        assert_eq!(snap.tabs[0].id, "", "took the tab bound to space B's row");
        assert!(!snap.tabs[0].selected);
        assert_eq!(ids.take_for(22, "https://same.test/"), "7", "the owner lost its tab");
    }

    /// Every space names the tab, not only the active one: the row that owns a
    /// moved tab is usually in a space that is NOT active in this window.
    #[test]
    fn a_row_bound_elsewhere_names_its_tab_on_every_space() {
        let mut a = space("A", "p", 0);
        let mut local = tab("https://example.com/", "A tab");
        local.token = 21;
        a.tabs = vec![local];
        let mut b = space("B", "p", 1);
        let mut away = tab("https://example.com/", "B tab");
        away.token = 22;
        b.tabs = vec![away];

        let mut bindings = Bindings::new();
        assert!(bindings.bind(42, 21));
        assert!(bindings.bind(99, 22));
        let mut ids = LiveTabIds::new(
            &[("https://example.com/".into(), 42)],
            None,
            &[],
            Some((&bindings, &[99])),
        );
        let snap = snapshot(0, &[a, b], &[], &mut ids, &[], &[]);

        assert_eq!(snap.spaces[0].tabs[0].id, "42");
        assert_eq!(snap.spaces[0].tabs[0].elsewhere_tab_id, "");
        assert!(!snap.spaces[0].tabs[0].elsewhere);
        assert_eq!(snap.spaces[1].tabs[0].id, "");
        assert_eq!(snap.spaces[1].tabs[0].elsewhere_tab_id, "99");
        assert!(snap.spaces[1].tabs[0].elsewhere);
    }

    /// The window that lost its tab keeps that row selected: the strip names
    /// the frozen tab as active, and it is an elsewhere tab here.
    #[test]
    fn a_frozen_window_keeps_the_row_of_the_tab_that_left_selected() {
        let mut s = space("A", "p", 0);
        let mut left = tab("https://left.test/", "Left");
        left.token = 21;
        let mut stayed = tab("https://stayed.test/", "Stayed");
        stayed.token = 22;
        s.tabs = vec![left, stayed];

        let mut bindings = Bindings::new();
        assert!(bindings.bind(99, 21));
        assert!(bindings.bind(42, 22));
        let mut ids = LiveTabIds::new(
            &[("https://stayed.test/".into(), 42)],
            Some(99),
            &[],
            Some((&bindings, &[99])),
        );
        let snap = snapshot(0, &[s], &[], &mut ids, &[], &[]);

        assert!(snap.tabs[0].selected, "the row of the tab that left");
        assert!(snap.tabs[0].elsewhere);
        assert!(
            !snap.tabs[1].selected,
            "the strip's own tab is not the shown one"
        );
    }

    /// A favourite whose tab is next door draws as elsewhere and, in a frozen
    /// window, stays selected. A tab HERE that claims it wins over that.
    #[test]
    fn a_favourite_claimed_next_door_is_elsewhere_and_stays_selected_when_frozen() {
        let s = space("A", "p", 0);
        let favorite = |url: &str| Favorite {
            url: url.into(),
            title: "Fav".into(),
            profile_id: "p".into(),
            space_id: None,
        };
        let favs = vec![
            favorite("https://fav.test/"),
            favorite("https://other.test/"),
        ];
        let mut ids = LiveTabIds::new(&[], Some(99), &[("https://fav.test/".into(), 99)], None);
        let snap = snapshot(0, &[s.clone()], &favs, &mut ids, &[], &[]);
        assert_eq!(snap.favorites[0].tab_id, "");
        assert_eq!(snap.favorites[0].elsewhere_tab_id, "99");
        assert!(snap.favorites[0].elsewhere);
        assert!(snap.favorites[0].selected, "frozen on the tab that left");
        assert!(!snap.favorites[1].selected);

        let mut ids = LiveTabIds::new(
            &[("https://fav.test/".into(), 7)],
            Some(7),
            &[("https://fav.test/".into(), 99)],
            None,
        );
        let claims = vec![("https://fav.test/".to_string(), 7)];
        let snap = snapshot(0, &[s], &favs, &mut ids, &[], &claims);
        assert_eq!(snap.favorites[0].tab_id, "7");
        assert_eq!(snap.favorites[0].elsewhere_tab_id, "");
        assert!(!snap.favorites[0].elsewhere);
        assert!(snap.favorites[0].selected);
    }

    /// An unbound row is the ordinary cold case, and the url queue still
    /// answers for it.
    #[test]
    fn an_unbound_row_still_falls_back_to_the_url_queue() {
        let mut ids = LiveTabIds::new(&[("https://here.test/".into(), 7)], None, &[], None);
        assert_eq!(ids.take_for(UNTOKENED, "https://here.test/"), "7");
        assert_eq!(ids.take_for(UNTOKENED, "https://here.test/"), "");
    }

    /// A first-match-wins map gives every row sharing a URL the same tab, and
    /// each of those rows then activates the same one.
    #[test]
    fn a_live_id_is_consumed_rather_than_looked_up() {
        let mut ids = LiveTabIds::new(&[("u".into(), 7), ("u".into(), 8)], None, &[], None);
        assert_eq!(ids.take("u"), "7");
        assert_eq!(ids.take("u"), "8");
        assert_eq!(ids.take("u"), "", "the queue is empty, so the row is cold");
        assert_eq!(ids.take("other"), "");
    }

    /// one dot on the marked row, none on its neighbour, and the flag is
    /// omitted entirely from an unmarked snapshot's JSON.
    #[test]
    fn a_marked_url_dots_its_row_in_the_list_and_in_the_rail_pane() {
        let mut a = space("A", "p", 0);
        a.tabs = vec![
            tab("https://marked.example/", "Marked"),
            tab("https://plain.example/", "Plain"),
        ];
        let mut snap = snapshot(0, &[a], &[], &mut LiveTabIds::default(), &[], &[]);
        assert!(
            !encode(&snap).contains("\"marked\""),
            "an unmarked snapshot must not grow a key"
        );

        let marked: HashSet<String> = ["https://marked.example/".to_string()].into();
        mark_marginalia(&mut snap, &marked);
        assert!(snap.tabs[0].marked);
        assert!(!snap.tabs[1].marked, "the unmarked neighbour stays plain");
        assert!(
            snap.spaces[0].tabs[0].marked,
            "the rail pane for the active space is dotted too"
        );
        assert!(encode(&snap).contains("\"marked\":true"));
    }

    fn theme(name: &str) -> ThemeSpec {
        ThemeSpec {
            name: name.into(),
            stops: Vec::new(),
            opacity: 1.0,
            effects: crate::small_stores::ThemeEffects {
                grain: 0.0,
                vignette: 0.0,
                frost: 0.0,
                warmth: 0.0,
            },
            dark_adaptation: 0.0,
            night_shift_enabled: false,
            ambient_enabled: false,
            version: 1,
        }
    }

    /// The active space's theme reaches both the top-level field and its own
    /// chip. An untouched sibling chip stays `None`, which encodes as `null`.
    #[test]
    fn mark_themes_fills_the_active_space_and_its_own_chip() {
        let a = space("A", "p", 0);
        let b = space("B", "p", 1);
        let mut snap = snapshot(
            0,
            &[a.clone(), b],
            &[],
            &mut LiveTabIds::default(),
            &[],
            &[],
        );

        let themes: BTreeMap<String, ThemeSpec> = [(a.id.clone(), theme("Sunset"))].into();
        mark_themes(&mut snap, &themes);

        assert_eq!(snap.space_theme, Some(theme("Sunset")));
        assert_eq!(snap.spaces[0].theme, Some(theme("Sunset")));
        assert_eq!(snap.spaces[1].theme, None);
        assert!(encode(&snap).contains("\"spaceTheme\":{"));
        assert!(encode(&snap).contains("\"theme\":null"));
    }

    #[test]
    fn a_rename_wins_over_the_page_title() {
        let mut t = tab("https://example.com/", "Page Title");
        t.custom_title = Some("My Name".into());
        assert_eq!(row(&t, None, &[]).title, "My Name");
    }

    #[test]
    fn only_the_active_space_gets_live_ids() {
        let mut a = space("A", "p", 0);
        a.tabs = vec![tab("https://example.com/", "A tab")];
        let mut b = space("B", "p", 1);
        b.tabs = vec![tab("https://example.com/", "B tab")];

        let mut ids = LiveTabIds::new(&[("https://example.com/".into(), 42)], None, &[], None);
        let snap = snapshot(0, &[a, b], &[], &mut ids, &[], &[]);

        assert_eq!(snap.spaces[0].tabs[0].id, "42");
        assert_eq!(
            snap.spaces[1].tabs[0].id, "",
            "a live tab in one space must not lend its id to another"
        );
    }

    #[test]
    fn a_grouped_tab_leaves_the_flat_list_and_joins_its_folder() {
        let mut s = space("A", "p", 0);
        let mut grouped = tab("https://example.com/g", "Grouped");
        grouped.group_id = Some("G".into());
        s.tabs = vec![tab("https://example.com/", "Loose"), grouped];
        s.groups = vec![crate::spaces::TabGroup {
            id: "G".into(),
            name: "Reading".into(),
            collapsed: false,
            icon: None,
        }];

        let mut ids = LiveTabIds::default();
        let snap = snapshot(0, &[s], &[], &mut ids, &[], &[]);
        assert_eq!(snap.tabs.len(), 1);
        assert_eq!(snap.folders[0].tabs.len(), 1);
    }

    /// A favourite follows the PROFILE. Filtering by space is the rule the app
    /// never had, and the store nils spaceID on load to make sure of it.
    #[test]
    fn a_space_shows_its_whole_profiles_favourites() {
        let a = space("A", "work", 0);
        let b = space("B", "home", 1);
        let favs = vec![
            Favorite {
                url: "https://one.test/".into(),
                title: "One".into(),
                profile_id: "work".into(),
                space_id: None,
            },
            Favorite {
                url: "https://two.test/".into(),
                title: "Two".into(),
                profile_id: "home".into(),
                space_id: None,
            },
        ];
        let mut ids = LiveTabIds::default();
        let snap = snapshot(0, &[a, b], &favs, &mut ids, &[], &[]);
        assert_eq!(snap.spaces[0].favorites.len(), 1);
        assert_eq!(snap.spaces[1].favorites.len(), 1);
        assert_eq!(
            snap.favorites[0].id, "https://one.test/",
            "a favourite's id is its url"
        );
    }

    /// The lab never appends a favourite to paneTabs: it is a tab in its own
    /// right, so its page is not also a row in the list.
    #[test]
    fn a_favourites_page_is_not_also_a_tab_row() {
        let mut s = space("A", "work", 0);
        s.tabs = vec![
            tab("https://fav.test/", "The Favourite"),
            tab("https://other.test/", "An Ordinary Tab"),
        ];
        s.tabs[0].token = 5;
        let favs = vec![Favorite {
            url: "https://fav.test/".into(),
            title: "The Favourite".into(),
            profile_id: "work".into(),
            space_id: None,
        }];
        let mut ids = LiveTabIds::new(&[("https://fav.test/".into(), 7)], Some(7), &[], None);
        let claims = vec![("https://fav.test/".to_string(), 7u64)];
        let snap = snapshot(0, &[s], &favs, &mut ids, &[5], &claims);

        let titles: Vec<&str> = snap.tabs.iter().map(|r| r.title.as_str()).collect();
        assert_eq!(
            titles,
            vec!["An Ordinary Tab"],
            "the favourite's page must not appear in the tab list"
        );
        assert_eq!(
            snap.favorites[0].tab_id, "7",
            "the favourite carries the live tab, so clicking it can activate one"
        );
        assert_eq!(
            snap.favorites[0].id, "https://fav.test/",
            "its identity stays the URL - the URL-keyed favicon source is keyed on it"
        );
        assert!(
            snap.favorites[0].selected,
            "the tile shows the selection the tab row used to"
        );
    }

    /// The join CONSUMES, so building the same favourite twice drained the queue
    /// and left the second copy with an empty id.
    #[test]
    fn a_favourites_live_id_survives_being_read_twice() {
        let mut s = space("A", "work", 0);
        s.tabs = vec![tab("https://fav.test/", "The Favourite")];
        s.tabs[0].token = 3;
        let favs = vec![Favorite {
            url: "https://fav.test/".into(),
            title: "The Favourite".into(),
            profile_id: "work".into(),
            space_id: None,
        }];
        let mut ids = LiveTabIds::new(&[("https://fav.test/".into(), 9)], Some(9), &[], None);
        let claims = vec![("https://fav.test/".to_string(), 9u64)];
        let snap = snapshot(0, &[s], &favs, &mut ids, &[3], &claims);
        assert_eq!(snap.favorites[0].tab_id, "9");
        assert_eq!(
            snap.spaces[0].favorites[0].tab_id, "9",
            "the active space's chip and the top level are the same rows"
        );
    }

    /// A claim names one tab's token, never a url or a profile - so it hides
    /// only the row it was handed to, not every identical url elsewhere.
    #[test]
    fn a_claim_hides_only_the_row_it_names_not_every_matching_url() {
        let mut mine = space("A", "work", 0);
        mine.tabs = vec![tab("https://fav.test/", "Mine")];
        mine.tabs[0].token = 5;
        let mut other = space("B", "home", 1);
        other.tabs = vec![tab("https://fav.test/", "Theirs")];
        let favs = vec![Favorite {
            url: "https://fav.test/".into(),
            title: "F".into(),
            profile_id: "work".into(),
            space_id: None,
        }];
        let snap = snapshot(
            0,
            &[mine, other],
            &favs,
            &mut LiveTabIds::default(),
            &[5],
            &[],
        );
        assert!(snap.spaces[0].tabs.is_empty(), "the claimed row is hidden");
        assert_eq!(
            snap.spaces[1].tabs.len(),
            1,
            "an unclaimed row on the same url is an ordinary tab"
        );
    }

    #[test]
    fn a_favourite_with_no_live_tab_carries_no_id() {
        let s = space("A", "work", 0);
        let favs = vec![Favorite {
            url: "https://fav.test/".into(),
            title: "F".into(),
            profile_id: "work".into(),
            space_id: None,
        }];
        let snap = snapshot(0, &[s], &favs, &mut LiveTabIds::default(), &[], &[]);
        assert_eq!(snap.favorites[0].tab_id, "");
        assert_eq!(snap.favorites[0].id, "https://fav.test/");
        assert!(!snap.favorites[0].selected);
    }

    /// Grouped tabs take a separate path from the flat list, so without this the
    /// favourite reappears inside a folder.
    #[test]
    fn a_favourites_page_is_not_a_row_inside_a_folder() {
        let mut s = space("A", "work", 0);
        let mut inside = tab("https://fav.test/", "In A Folder");
        inside.group_id = Some("g1".into());
        inside.token = 9;
        s.tabs = vec![inside];
        s.groups = vec![crate::spaces::TabGroup {
            id: "g1".into(),
            name: "Folder".into(),
            collapsed: false,
            icon: None,
        }];
        let favs = vec![Favorite {
            url: "https://fav.test/".into(),
            title: "F".into(),
            profile_id: "work".into(),
            space_id: None,
        }];
        let snap = snapshot(0, &[s], &favs, &mut LiveTabIds::default(), &[9], &[]);
        assert!(
            snap.folders[0].tabs.is_empty(),
            "a favourite inside a folder is still not a tab row"
        );
    }

    #[test]
    fn spaces_come_out_in_their_stored_order() {
        let snap = snapshot(
            0,
            &[space("second", "p", 1), space("first", "p", 0)],
            &[],
            &mut LiveTabIds::default(),
            &[],
            &[],
        );
        assert_eq!(snap.spaces[0].name, "first");
        assert_eq!(snap.space_name, "first");
    }

    #[test]
    fn an_out_of_range_active_index_falls_back_to_the_first_space() {
        let snap = snapshot(
            9,
            &[space("only", "p", 0)],
            &[],
            &mut LiveTabIds::default(),
            &[],
            &[],
        );
        assert_eq!(snap.active_space, 0);
        assert_eq!(snap.space_name, "only");
    }

    /// Every row that is drawn, in the order the surface draws them, paired
    /// with its selected flag - so a test can say which ONE has the pill.
    fn selected_titles(snap: &Snapshot) -> Vec<&str> {
        snap.pinned
            .iter()
            .flat_map(|e| e.rows().iter())
            .chain(snap.folders.iter().flat_map(|f| f.tabs.iter()))
            .chain(snap.tabs.iter())
            .chain(snap.favorites.iter())
            .filter(|r| r.selected)
            .map(|r| r.title.as_str())
            .collect()
    }

    /// The whole of nothing ever set this, so the active row drew
    /// no pill at all.
    #[test]
    fn the_row_holding_the_windows_active_tab_is_the_selected_one() {
        let mut s = space("A", "p", 0);
        s.tabs = vec![
            tab("https://one.test/", "One"),
            tab("https://two.test/", "Two"),
        ];

        let mut ids = LiveTabIds::new(
            &[
                ("https://one.test/".into(), 11),
                ("https://two.test/".into(), 12),
            ],
            Some(12),
            &[],
            None,
        );
        let snap = snapshot(0, &[s], &[], &mut ids, &[], &[]);

        assert_eq!(selected_titles(&snap), vec!["Two"]);
    }

    /// Being live is not being active. A strip full of tabs still shows one.
    #[test]
    fn a_live_row_that_is_not_the_active_tab_stays_unselected() {
        let mut s = space("A", "p", 0);
        s.tabs = vec![tab("https://one.test/", "One")];

        let mut ids = LiveTabIds::new(&[("https://one.test/".into(), 11)], Some(99), &[], None);
        let snap = snapshot(0, &[s], &[], &mut ids, &[], &[]);

        assert_eq!(snap.tabs[0].id, "11", "the row is live");
        assert!(
            selected_titles(&snap).is_empty(),
            "but it is not the one shown"
        );
    }

    /// The whole point, at snapshot level. Clockify's login bounce moved the
    /// page after the document's last news of it, and the row went cold - inert
    /// to a click, and reopening the url on the next one.
    #[test]
    fn a_row_whose_page_moved_on_is_still_live_and_still_selectable() {
        let mut moved = tab("https://clockify.test/token-exchange", "Log In");
        moved.token = 41;
        let mut s = space("A", "p", 0);
        s.tabs = vec![moved];

        let mut bindings = Bindings::new();
        assert!(bindings.bind(7, 41));
        let mut ids = LiveTabIds::new(
            &[("https://clockify.test/login".into(), 7)],
            Some(7),
            &[],
            Some((&bindings, &[])),
        );
        let snap = snapshot(0, &[s], &[], &mut ids, &[], &[]);

        assert_eq!(
            snap.tabs[0].id, "7",
            "the row lost its tab because the page moved, so a click reopens it"
        );
        assert!(snap.tabs[0].selected, "and it is the tab the window shows");
    }

    /// The pinned array holds both kinds now, so the surface reads `kind` to
    /// tell a pin from a folder rather than which array the entry came in.
    #[test]
    fn a_pinned_entry_names_its_kind_in_the_encoded_model() {
        let mut s = space("A", "p", 0);
        s.groups = vec![crate::spaces::TabGroup {
            id: "G".into(),
            name: "Reading".into(),
            collapsed: false,
            icon: None,
        }];
        let mut member = tab("https://member.test/", "Member");
        member.group_id = Some("G".into());
        s.pinned_tabs = vec![tab("https://pin.test/", "Pinned"), member];

        let snap = snapshot(0, &[s], &[], &mut LiveTabIds::default(), &[], &[]);
        let model: serde_json::Value = serde_json::from_str(&encode(&snap)).unwrap();
        let pinned = model["pinned"].as_array().unwrap();

        assert_eq!(pinned[0]["kind"], "tab");
        assert_eq!(pinned[0]["title"], "Pinned");
        assert_eq!(pinned[1]["kind"], "folder");
        assert_eq!(pinned[1]["id"], "G");
        assert_eq!(pinned[1]["name"], "Reading");
        assert_eq!(pinned[1]["closed"], false);
        assert_eq!(pinned[1]["tabs"][0]["title"], "Member");
        assert!(
            model["folders"].as_array().unwrap().is_empty(),
            "a folder with a pinned member is drawn in the pinned list and nowhere else"
        );
    }

    /// A folder renders at its FIRST member's place, so the two lists cannot
    /// drift: array order is the only order there is.
    #[test]
    fn a_pinned_folder_renders_where_its_first_member_sits() {
        let mut s = space("A", "p", 0);
        s.groups = vec![crate::spaces::TabGroup {
            id: "G".into(),
            name: "Reading".into(),
            collapsed: false,
            icon: None,
        }];
        let member = |url: &str, title: &str| {
            let mut t = tab(url, title);
            t.group_id = Some("G".into());
            t
        };
        s.pinned_tabs = vec![
            member("https://m0.test/", "M0"),
            member("https://m1.test/", "M1"),
            tab("https://pin.test/", "Pinned"),
        ];

        let snap = snapshot(0, &[s], &[], &mut LiveTabIds::default(), &[], &[]);
        assert_eq!(snap.pinned.len(), 2);
        assert!(matches!(snap.pinned[0], PinnedEntry::Folder(_)));
        assert_eq!(snap.pinned[0].rows().len(), 2);
        assert_eq!(snap.pinned[1].rows()[0].title, "Pinned");
    }

    /// A group with no member at all still has to render, or a folder the
    /// user just made disappears until they put something in it.
    #[test]
    fn an_empty_group_stays_in_the_folders_list() {
        let mut s = space("A", "p", 0);
        s.groups = vec![crate::spaces::TabGroup {
            id: "G".into(),
            name: "Reading".into(),
            collapsed: false,
            icon: None,
        }];

        let snap = snapshot(0, &[s], &[], &mut LiveTabIds::default(), &[], &[]);
        assert!(snap.pinned.is_empty());
        assert_eq!(snap.folders.len(), 1);
        assert!(snap.folders[0].tabs.is_empty());
    }

    /// A window that names no active tab is the old contract, and it
    /// must not select every cold row by matching empty ids.
    #[test]
    fn no_active_tab_selects_nothing_rather_than_every_cold_row() {
        let mut s = space("A", "p", 0);
        s.tabs = vec![
            tab("https://one.test/", "One"),
            tab("https://two.test/", "Two"),
        ];

        let mut ids = LiveTabIds::default();
        let snap = snapshot(0, &[s], &[], &mut ids, &[], &[]);

        assert!(selected_titles(&snap).is_empty());
    }

    /// Two rows on one URL is exactly the case the consuming join exists for.
    /// Matching by URL instead would put a pill on both.
    #[test]
    fn only_the_row_that_was_handed_the_active_id_is_selected() {
        let mut s = space("A", "p", 0);
        s.tabs = vec![
            tab("https://same.test/", "First"),
            tab("https://same.test/", "Second"),
        ];

        let mut ids = LiveTabIds::new(
            &[
                ("https://same.test/".into(), 11),
                ("https://same.test/".into(), 12),
            ],
            Some(12),
            &[],
            None,
        );
        let snap = snapshot(0, &[s], &[], &mut ids, &[], &[]);

        assert_eq!(selected_titles(&snap), vec!["Second"]);
    }

    /// Pinned rows and folder rows take the pill too - the active tab does
    /// not stop being active because its row sits in a folder.
    #[test]
    fn a_pinned_or_grouped_row_can_hold_the_pill() {
        let mut s = space("A", "p", 0);
        s.pinned_tabs = vec![tab("https://pin.test/", "Pinned")];
        let mut grouped = tab("https://group.test/", "Grouped");
        grouped.group_id = Some("G".into());
        s.tabs = vec![grouped, tab("https://loose.test/", "Loose")];
        s.groups = vec![crate::spaces::TabGroup {
            id: "G".into(),
            name: "Reading".into(),
            collapsed: false,
            icon: None,
        }];

        let live = [
            ("https://pin.test/".to_string(), 1u64),
            ("https://group.test/".to_string(), 2),
            ("https://loose.test/".to_string(), 3),
        ];

        let mut pinned = LiveTabIds::new(&live, Some(1), &[], None);
        assert_eq!(
            selected_titles(&snapshot(
                0,
                std::slice::from_ref(&s),
                &[],
                &mut pinned,
                &[],
                &[]
            )),
            vec!["Pinned"]
        );

        let mut grouped = LiveTabIds::new(&live, Some(2), &[], None);
        assert_eq!(
            selected_titles(&snapshot(
                0,
                std::slice::from_ref(&s),
                &[],
                &mut grouped,
                &[],
                &[]
            )),
            vec!["Grouped"]
        );
    }

    /// A live tab belongs to the window's active space, so the same URL in a
    /// background space must not light up as well - it has no live id at all.
    #[test]
    fn the_pill_cannot_land_in_a_background_space() {
        let mut a = space("A", "p", 0);
        a.tabs = vec![tab("https://example.com/", "A tab")];
        let mut b = space("B", "p", 1);
        b.tabs = vec![tab("https://example.com/", "B tab")];

        let mut ids = LiveTabIds::new(&[("https://example.com/".into(), 42)], Some(42), &[], None);
        let snap = snapshot(0, &[a, b], &[], &mut ids, &[], &[]);

        assert!(snap.spaces[0].tabs[0].selected);
        assert!(
            !snap.spaces[1].tabs[0].selected,
            "a background space's row shows no pill"
        );
    }

    /// The tint reaches the surface as a colour. sidebar.js sets `--selbase`
    /// straight from this and paints the space dot with it, and a product
    /// name is not something CSS can paint.
    #[test]
    fn a_named_tint_comes_out_as_a_colour() {
        let mut s = space("A", "p", 0);
        s.tint = "oceanBlue".into();

        let snap = snapshot(0, &[s], &[], &mut LiveTabIds::default(), &[], &[]);

        assert_eq!(snap.space_tint, "#0F2B46");
        assert_eq!(snap.spaces[0].color, "#0F2B46", "the space dot too");
    }

    /// A custom hex tint is already a colour and survives the trip.
    #[test]
    fn a_hex_tint_is_left_alone() {
        let mut s = space("A", "p", 0);
        s.tint = "#5B8DEF".into();

        let snap = snapshot(0, &[s], &[], &mut LiveTabIds::default(), &[], &[]);

        assert_eq!(snap.space_tint, "#5B8DEF");
    }

    #[test]
    fn no_spaces_gives_an_empty_snapshot_rather_than_a_panic() {
        let snap = snapshot(0, &[], &[], &mut LiveTabIds::default(), &[], &[]);
        assert!(snap.spaces.is_empty());
        assert_eq!(snap.space_name, "");
    }

    /// a row the window has bound to a favourite tile is hidden,
    /// and the live id it consumed is not offered to any other row.
    #[test]
    fn a_claimed_row_is_hidden_and_keeps_its_live_id() {
        let mut s = space("s", "p", 0);
        s.tabs = vec![tab("https://a/", "A"), tab("https://b/", "B")];
        s.tabs[0].token = 7;
        s.tabs[1].token = 8;
        let mut bindings = Bindings::new();
        bindings.bind(41, 7);
        let mut ids = LiveTabIds::new(
            &[("https://landing/".into(), 41), ("https://b/".into(), 42)],
            None,
            &[],
            Some((&bindings, &[])),
        );
        let snap = snapshot(0, &[s], &[], &mut ids, &[7], &[]);
        let rows = &snap.spaces[0].tabs;
        assert_eq!(rows.len(), 1, "the claimed row was drawn");
        assert_eq!(rows[0].url, "https://b/");
        assert_eq!(rows[0].id, "42");
    }

    /// the bug itself. A tab reached the favourite's url on its
    /// own - no tile ever opened it, so no claim names its token - and it
    /// must keep its own row rather than being swallowed by the tile.
    #[test]
    fn an_unclaimed_tab_on_a_favourites_url_keeps_its_row_and_the_tile_stays_cold() {
        let mut s = space("A", "work", 0);
        s.tabs = vec![tab("https://fav.test/", "Ordinary Tab")];
        let favs = vec![Favorite {
            url: "https://fav.test/".into(),
            title: "F".into(),
            profile_id: "work".into(),
            space_id: None,
        }];
        let mut ids = LiveTabIds::new(&[("https://fav.test/".into(), 9)], Some(9), &[], None);
        let snap = snapshot(0, &[s], &favs, &mut ids, &[], &[]);

        assert_eq!(
            snap.tabs.len(),
            1,
            "the tab keeps its row - nothing claimed it"
        );
        assert_eq!(snap.tabs[0].id, "9");
        assert_eq!(
            snap.favorites[0].tab_id, "",
            "the tile is not bound to a tab it never claimed"
        );
        assert!(!snap.favorites[0].selected);
    }

    /// the claimed counterpart of the test above - a claim still
    /// hides the row and selects the tile, which is the behaviour a click
    /// through the tile relies on.
    #[test]
    fn a_claimed_favourite_hides_its_row_and_selects_its_tile() {
        let mut s = space("A", "work", 0);
        let mut claimed = tab("https://fav.test/", "The Favourite");
        claimed.token = 4;
        s.tabs = vec![claimed];
        let favs = vec![Favorite {
            url: "https://fav.test/".into(),
            title: "F".into(),
            profile_id: "work".into(),
            space_id: None,
        }];
        let mut ids = LiveTabIds::new(&[("https://fav.test/".into(), 7)], Some(7), &[], None);
        let claims = vec![("https://fav.test/".to_string(), 7u64)];
        let snap = snapshot(0, &[s], &favs, &mut ids, &[4], &claims);

        assert!(snap.tabs.is_empty(), "the claimed row is hidden");
        assert_eq!(snap.favorites[0].tab_id, "7");
        assert!(snap.favorites[0].selected);
    }

    /// `selected` is read straight off the claim each snapshot, so a rebuild
    /// with no claim and no live tab clears a tile lit by an earlier one.
    #[test]
    fn closing_a_favourites_tab_drops_the_tiles_highlight() {
        let mut s = space("A", "work", 0);
        let mut claimed = tab("https://fav.test/", "The Favourite");
        claimed.token = 4;
        s.tabs = vec![claimed];
        let favs = vec![Favorite {
            url: "https://fav.test/".into(),
            title: "F".into(),
            profile_id: "work".into(),
            space_id: None,
        }];
        let mut ids = LiveTabIds::new(&[("https://fav.test/".into(), 7)], Some(7), &[], None);
        let claims = vec![("https://fav.test/".to_string(), 7u64)];
        let lit = snapshot(0, &[s.clone()], &favs, &mut ids, &[4], &claims);
        assert!(
            lit.favorites[0].selected,
            "the control: the tile is lit while open"
        );

        s.tabs.clear();
        let mut closed_ids = LiveTabIds::default();
        let closed = snapshot(0, &[s], &favs, &mut closed_ids, &[4], &[]);
        assert_eq!(closed.favorites[0].tab_id, "", "no live tab to bind to");
        assert!(
            !closed.favorites[0].selected,
            "the highlight must not survive the close"
        );
    }

    /// the claimed tab closed, so the strip no longer reports it
    /// and no token survives to bind. A second, unclaimed tab happens to sit
    /// on the same url - a stranger, not the tile's own - and the tile must
    /// stay cold rather than adopting it.
    #[test]
    fn a_claim_whose_tab_closed_leaves_the_tile_cold_rather_than_binding_a_stranger() {
        let s = space("A", "work", 0);
        let favs = vec![Favorite {
            url: "https://fav.test/".into(),
            title: "F".into(),
            profile_id: "work".into(),
            space_id: None,
        }];
        // The strip's only entry on this url (11) is an ordinary tab: it
        // claims nothing, so no claim reaches the tile.
        let mut ids = LiveTabIds::new(&[("https://fav.test/".into(), 11)], Some(11), &[], None);
        let snap = snapshot(0, &[s], &favs, &mut ids, &[4], &[]);

        assert_eq!(
            snap.favorites[0].tab_id, "",
            "the tile does not adopt a tab it never claimed"
        );
        assert!(!snap.favorites[0].selected);
    }

    /// A favourite is one tab, so every space of its profile names it. The
    /// old binding read the ACTIVE space's rows, leaving other tiles cold.
    #[test]
    fn a_tile_binds_the_same_tab_in_every_space_of_its_profile() {
        let favs = vec![Favorite {
            url: "https://fav.test/".into(),
            title: "F".into(),
            profile_id: "work".into(),
            space_id: None,
        }];
        let claims = vec![("https://fav.test/".to_string(), 7u64)];
        let mut ids = LiveTabIds::new(&[("https://fav.test/".into(), 7)], Some(7), &[], None);
        let snap = snapshot(
            0,
            &[space("A", "work", 0), space("B", "work", 1)],
            &favs,
            &mut ids,
            &[],
            &claims,
        );

        assert_eq!(snap.spaces[0].favorites[0].tab_id, "7");
        assert_eq!(
            snap.spaces[1].favorites[0].tab_id, "7",
            "the space the window is NOT showing names the same tab"
        );
        assert!(
            snap.spaces[1].favorites[0].selected,
            "and it is lit there too, because that tab is the one on screen"
        );
    }

    /// A redirect commits a url the tile does not carry, so the claim key is
    /// the only join. Binding by url would leave the tile cold.
    #[test]
    fn a_tile_binds_a_tab_that_redirected_off_its_url() {
        let favs = vec![Favorite {
            url: "https://fav.test/".into(),
            title: "F".into(),
            profile_id: "work".into(),
            space_id: None,
        }];
        let claims = vec![("https://fav.test/".to_string(), 7u64)];
        let mut ids = LiveTabIds::new(&[("https://landed.test/".into(), 7)], Some(7), &[], None);
        let snap = snapshot(0, &[space("A", "work", 0)], &favs, &mut ids, &[], &claims);

        assert_eq!(snap.favorites[0].tab_id, "7");
        assert!(snap.favorites[0].selected);
    }

    /// Favourites follow the profile, so a space on another one shows none of
    /// them - with or without a live claim.
    #[test]
    fn a_space_on_another_profile_gets_no_tile_from_the_claim() {
        let favs = vec![Favorite {
            url: "https://fav.test/".into(),
            title: "F".into(),
            profile_id: "work".into(),
            space_id: None,
        }];
        let claims = vec![("https://fav.test/".to_string(), 7u64)];
        let mut ids = LiveTabIds::new(&[("https://fav.test/".into(), 7)], Some(7), &[], None);
        let snap = snapshot(
            0,
            &[space("A", "work", 0), space("B", "home", 1)],
            &favs,
            &mut ids,
            &[],
            &claims,
        );

        assert_eq!(snap.spaces[0].favorites.len(), 1);
        assert!(
            snap.spaces[1].favorites.is_empty(),
            "the other profile's space has no tile to bind"
        );
    }
}
