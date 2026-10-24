// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Putting a whole window or space slice back.
//!
//! Every other sidebar verb is per-gesture: one row moves, one tab closes, one
//! cold tile reopens. Rewind (`chrome/browser/resources/atrium/rewind.js`)
//! computes a state to return to, and no sequence of those verbs reaches it -
//! order, selection and folder membership have no verb at all, and replaying
//! the ones that exist is long, racy and lossy. So this takes the slice WHOLE:
//! the payload describes the space as it should end up, and the document is
//! rewritten to match.
//!
//! VALIDATE, THEN WRITE, and the two phases are separate on purpose. A restore
//! that refused halfway would leave the window in a state that was never a
//! moment - worse than the one the user was trying to leave. [`apply`] resolves
//! every space and inspects every row before it touches anything, so a refusal
//! means the document is byte-identical to what it was.
//!
//! IDENTITY IS PRESERVED WHERE IT EXISTS. A row whose live tab is still around
//! is MOVED - the same [`RowToken`], so the binding a later close resolves
//! through still points at it. A row that is gone comes back as a COLD row,
//! and the returned [`Plan`] tells the browser which ones had a live tab in the
//! moment. Reopening one of those adopts the cold row this wrote rather than
//! adding a second (`mutate::open_tab`'s adoption path), so the browser half
//! cannot duplicate what the core half already put back.
//!
//! Pure, like the rest of `mutate`: no clock, no disk, no ABI.

use serde::{Deserialize, Serialize};

use crate::mutate::{self, Section};
use crate::sidebar;
use crate::spaces::{Document, Favorite, RowToken, Space, SpaceTab};
use crate::tabs::Bindings;

/// How much of the window the payload speaks for.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Scope {
    /// Every space in the payload, and the active space with them.
    Window,
    /// One space, by its position in sidebar order. Every other space is left
    /// exactly as the user has it now.
    Space(usize),
}

/// One row of a slice.
///
/// `id` is the LIVE Chromium tab id as a string, the same value
/// [`sidebar::Row`] carries, and empty for a cold row. It is the moment's id,
/// not today's: whether that tab still exists is what decides moved versus
/// reopened.
#[derive(Debug, Clone, PartialEq, Default, Deserialize)]
pub struct Row {
    #[serde(default)]
    pub id: String,
    pub url: String,
    #[serde(default)]
    pub title: String,
    #[serde(default)]
    pub selected: bool,
}

#[derive(Debug, Clone, PartialEq, Default, Deserialize)]
pub struct Folder {
    pub id: String,
    #[serde(default)]
    pub name: String,
    #[serde(default)]
    pub closed: bool,
    #[serde(default)]
    pub tabs: Vec<Row>,
}

/// One space as it should end up.
///
/// `index` is the space's position in SIDEBAR order, the index space every
/// other verb already speaks, resolved through [`mutate::space_at`].
#[derive(Debug, Clone, PartialEq, Default, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct Slice {
    pub index: usize,
    #[serde(default)]
    pub name: String,
    #[serde(default)]
    pub pinned: Vec<Row>,
    #[serde(default)]
    pub tabs: Vec<Row>,
    #[serde(default)]
    pub folders: Vec<Folder>,
    #[serde(default)]
    pub favorites: Vec<Row>,
    #[serde(default)]
    pub pinned_collapsed: bool,
}

impl Slice {
    /// The slice that would reproduce `chip`, which is what the surface holds
    /// - Rewind's moments are projections of exactly this.
    pub fn from_chip(index: usize, chip: &sidebar::SpaceChip) -> Self {
        Self {
            index,
            name: chip.name.clone(),
            // A pinned folder flattens to its member rows: a moment restores
            // the pins and their order, and rebuilds them ungrouped.
            pinned: chip
                .pinned
                .iter()
                .flat_map(|entry| entry.rows())
                .map(row_of)
                .collect(),
            tabs: chip.tabs.iter().map(row_of).collect(),
            folders: chip
                .folders
                .iter()
                .map(|f| Folder {
                    id: f.id.clone(),
                    name: f.name.clone(),
                    closed: f.closed,
                    tabs: f.tabs.iter().map(row_of).collect(),
                })
                .collect(),
            favorites: chip.favorites.iter().map(row_of).collect(),
            pinned_collapsed: chip.pinned_collapsed,
        }
    }
}

fn row_of(row: &sidebar::Row) -> Row {
    Row {
        id: row.id.clone(),
        url: row.url.clone(),
        title: row.title.clone(),
        selected: row.selected,
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct Payload {
    pub scope: Scope,
    /// Which space to land in, by sidebar position.
    pub active_space: usize,
    pub spaces: Vec<Slice>,
}

/// Why a payload was refused. Every variant means the document was not
/// touched.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Refusal {
    /// A payload that names no space would be an empty restore, and an empty
    /// restore is far more likely to be a bug than an intention.
    NoSlices,
    /// A space position that resolves to nothing. NOT created, and NOT
    /// emptied: the moment is describing a window that no longer exists.
    NoSuchSpace(usize),
    /// Two slices for one space - the payload contradicts itself.
    DuplicateSlice(usize),
    /// A per-space scope carrying anything but that one space's slice.
    ScopeMismatch,
    /// A row with no URL. There is nothing to restore it from and nothing to
    /// reopen it with.
    EmptyUrl,
    /// A folder with no id. Its rows could not be told apart from loose ones.
    EmptyFolderId,
    /// A row or favourite whose URL scheme is not one a restore may reopen.
    ///
    /// Every reopen path already allows only http, https and about. This closes
    /// the same door on the WRITE path: without it, persisting was a way around
    /// the allowlist, because the next launch navigates a startup row with
    /// PAGE_TRANSITION_TYPED and applied no scheme check of its own.
    DisallowedScheme,
}

/// Whether a restore may carry `url`, by scheme.
///
/// The same three the reopen paths allow. Compared case-insensitively on the
/// prefix before ':' rather than parsed, because the core has no URL library and
/// a parser here would be a second, differing opinion about what these strings
/// mean - the browser side is the one that has to agree.
pub(crate) fn scheme_allowed(url: &str) -> bool {
    match url.split_once(':') {
        Some((scheme, _)) => {
            let scheme = scheme.to_ascii_lowercase();
            scheme == "http" || scheme == "https" || scheme == "about"
        }
        // No colon at all is not a URL a reopen could use.
        None => false,
    }
}

/// A row the moment had a live tab on, whose tab is gone now.
///
/// The core has already written the row COLD. This is the other half, which
/// only the browser can do: put a tab back on it. Opening on this URL adopts
/// the cold row rather than adding a second one.
#[derive(Debug, Clone, PartialEq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Reopen {
    /// Sidebar position of the space, as in [`Slice::index`].
    pub space: usize,
    /// 1 for the pinned list, 0 for the regular one - [`mutate::Section`]'s
    /// wire encoding.
    pub section: i32,
    /// Position among the space's RENDERED rows of that section.
    pub slot: usize,
    pub url: String,
    pub title: String,
    /// The folder the row belongs to, when it is inside one.
    #[serde(rename = "groupID")]
    pub group_id: Option<String>,
    /// This is the row the moment was showing. Its tab id died with the tab,
    /// so the selection travels with the row that has to come back rather than
    /// as an id - see [`Plan::select_tab_id`].
    pub selected: bool,
}

/// What the browser still has to do after the document has been rewritten.
#[derive(Debug, Clone, PartialEq, Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Plan {
    pub reopen: Vec<Reopen>,
    /// Live tabs whose row the moment did not hold. Their rows are gone from
    /// the document, so the browser closes them; leaving them open would make
    /// a restore add tabs instead of putting a state back.
    pub close_tab_ids: Vec<u64>,
    /// The tab to show, and it is a tab that exists RIGHT NOW - the moment's
    /// selected row is still live and is being moved, not reopened.
    ///
    /// `None` when the selected row has to be reopened. Chromium never reuses
    /// a tab id, so the moment's id would name a tab that can never come back;
    /// the [`Reopen`] entry carries `selected` instead and the browser shows
    /// whatever tab it opens for that row.
    pub select_tab_id: Option<u64>,
    /// The space to land in, by sidebar position.
    pub active_space: usize,
}

/// The payload from the JSON the surface sends, or `None` when that JSON is
/// not one.
///
/// Refused rather than guessed at: a payload the parser had to repair would
/// rewrite the window into a shape nobody asked for.
pub fn parse(json: &str) -> Option<Payload> {
    #[derive(Deserialize)]
    #[serde(rename_all = "camelCase")]
    struct Wire {
        scope: String,
        active_space: usize,
        #[serde(default)]
        spaces: Vec<Slice>,
    }

    let wire: Wire = serde_json::from_str(json).ok()?;
    let scope = if wire.scope == "all" {
        Scope::Window
    } else {
        Scope::Space(wire.scope.parse::<usize>().ok()?)
    };
    Some(Payload {
        scope,
        active_space: wire.active_space,
        spaces: wire.spaces,
    })
}

fn rows_of(slice: &Slice) -> impl Iterator<Item = &Row> {
    slice
        .pinned
        .iter()
        .chain(slice.tabs.iter())
        .chain(slice.folders.iter().flat_map(|f| f.tabs.iter()))
}

fn validate(doc: &Document, payload: &Payload) -> Result<(), Refusal> {
    if payload.spaces.is_empty() {
        return Err(Refusal::NoSlices);
    }
    for (at, slice) in payload.spaces.iter().enumerate() {
        if payload.spaces[..at].iter().any(|s| s.index == slice.index) {
            return Err(Refusal::DuplicateSlice(slice.index));
        }
    }
    for slice in &payload.spaces {
        if mutate::space_at(doc, slice.index).is_none() {
            return Err(Refusal::NoSuchSpace(slice.index));
        }
    }
    if let Scope::Space(index) = payload.scope {
        if mutate::space_at(doc, index).is_none() {
            return Err(Refusal::NoSuchSpace(index));
        }
        if payload.spaces.len() != 1 || payload.spaces[0].index != index {
            return Err(Refusal::ScopeMismatch);
        }
    }
    if mutate::space_at(doc, payload.active_space).is_none() {
        return Err(Refusal::NoSuchSpace(payload.active_space));
    }
    for slice in &payload.spaces {
        if slice.folders.iter().any(|f| f.id.is_empty()) {
            return Err(Refusal::EmptyFolderId);
        }
        if rows_of(slice).any(|r| r.url.is_empty())
            || slice.favorites.iter().any(|f| f.url.is_empty())
        {
            return Err(Refusal::EmptyUrl);
        }
        // checked here, before anything is written, so a refusal
        // leaves the document exactly as it was.
        if rows_of(slice).any(|r| !scheme_allowed(&r.url))
            || slice.favorites.iter().any(|f| !scheme_allowed(&f.url))
        {
            return Err(Refusal::DisallowedScheme);
        }
    }
    Ok(())
}

/// Whether `id` names a live tab that is still showing a row.
fn live_token(bindings: &Bindings, id: &str) -> Option<RowToken> {
    id.parse::<u64>().ok().and_then(|t| bindings.token_of(t))
}

/// `id` as a tab id, but only when that tab is still open.
fn live_tab_id(bindings: &Bindings, id: &str) -> Option<u64> {
    let tab = id.parse::<u64>().ok()?;
    bindings.token_of(tab).map(|_| tab)
}

/// The rows of `space`, emptied out of it so each can be claimed at most once.
fn harvest(space: &mut Space) -> Vec<SpaceTab> {
    let mut pool = std::mem::take(&mut space.pinned_tabs);
    pool.append(&mut space.tabs);
    pool
}

/// The pool row this payload row is, if it is still there.
///
/// By the live tab first, which is the only unambiguous join. Then by URL, and
/// a row NO live tab is showing is preferred - taking a bound one would move
/// somebody else's tab under this row.
fn claim(pool: &mut Vec<SpaceTab>, bindings: &Bindings, row: &Row) -> Option<SpaceTab> {
    if let Some(token) = live_token(bindings, &row.id) {
        if let Some(at) = pool.iter().position(|t| t.token == token) {
            return Some(pool.remove(at));
        }
    }
    let at = pool
        .iter()
        .position(|t| t.url == row.url && !bindings.is_taken(t.token))
        .or_else(|| pool.iter().position(|t| t.url == row.url))?;
    Some(pool.remove(at))
}

fn rebuilt_row(
    doc_token: &mut dyn FnMut() -> RowToken,
    pool: &mut Vec<SpaceTab>,
    bindings: &Bindings,
    row: &Row,
    group: Option<&str>,
) -> SpaceTab {
    let mut tab = claim(pool, bindings, row).unwrap_or_else(|| {
        let mut fresh = SpaceTab::new(&row.url, &row.title);
        fresh.token = doc_token();
        fresh
    });
    tab.group_id = group.map(str::to_string);
    tab
}

/// The favourite rows one payload writes, and which profiles it has written.
///
/// One value rather than two locals, because they are only ever correct
/// together: a profile listed as written with no rows to go with it CLEARS that
/// profile's grid, which is a real outcome and must not happen by accident.
#[derive(Default)]
struct ProfileFavorites {
    written: Vec<String>,
    rows: Vec<Favorite>,
}

/// Applies one slice to the space it names.
///
/// Five phases, in this order and not interchangeable: harvest the space's
/// existing rows into a pool, rebuild the rows the payload asks for (taking
/// from the pool wherever a live tab still shows the page), note which rows
/// have to be REOPENED, resolve the selection and release the bindings of rows
/// the moment did not keep, then write the space.
///
/// Split out of `apply`. `apply`'s contract is all-or-nothing and
/// that property was being checked by eye across 134 lines nested four deep.
/// It is still checked by eye - nothing here makes it automatic - but over a
/// function whose phases are named.
///
/// The one refusal it returns, `NoSuchSpace`, comes BEFORE it touches the
/// document. That is what lets `apply` stop on a bad slice without having
/// half-written the slices before it.
fn apply_slice(
    doc: &mut Document,
    bindings: &mut Bindings,
    slice: &Slice,
    plan: &mut Plan,
    favorites: &mut ProfileFavorites,
) -> Result<(), Refusal> {
    let backing = mutate::space_at(doc, slice.index).ok_or(Refusal::NoSuchSpace(slice.index))?;
    let profile = doc.spaces[backing].profile_id.clone();

    let mut pool = harvest(&mut doc.spaces[backing]);
    // `allocate_token` needs the document, which is borrowed by the space
    // below, so the counter is advanced up front instead.
    let mut next = doc.next_token.max(1);
    let mut allocate = || {
        let token = next;
        next += 1;
        token
    };

    let pinned: Vec<SpaceTab> = slice
        .pinned
        .iter()
        .map(|r| rebuilt_row(&mut allocate, &mut pool, bindings, r, None))
        .collect();
    let mut tabs: Vec<SpaceTab> = slice
        .tabs
        .iter()
        .map(|r| rebuilt_row(&mut allocate, &mut pool, bindings, r, None))
        .collect();
    for folder in &slice.folders {
        for row in &folder.tabs {
            tabs.push(rebuilt_row(
                &mut allocate,
                &mut pool,
                bindings,
                row,
                Some(&folder.id),
            ));
        }
    }
    doc.next_token = next;

    for (slot, row) in slice.pinned.iter().enumerate() {
        note_reopen(
            plan,
            bindings,
            slice.index,
            Section::Pinned.to_code(),
            slot,
            row,
            None,
        );
    }
    for (slot, row) in slice.tabs.iter().enumerate() {
        note_reopen(
            plan,
            bindings,
            slice.index,
            Section::Regular.to_code(),
            slot,
            row,
            None,
        );
    }
    for folder in &slice.folders {
        for (slot, row) in folder.tabs.iter().enumerate() {
            note_reopen(
                plan,
                bindings,
                slice.index,
                Section::Regular.to_code(),
                slot,
                row,
                Some(folder.id.as_str()),
            );
        }
    }
    // Only a tab that is still live can be named by id. A selected row
    // that has to be reopened was flagged on its own reopen entry above.
    for row in rows_of(slice).filter(|r| r.selected) {
        if let Some(id) = live_tab_id(bindings, &row.id) {
            plan.select_tab_id = Some(id);
        }
    }

    // A row a live tab is still showing but the moment did not hold has
    // left this space, so its binding no longer names anything here. It is
    // released rather than left pointing at a row that is gone, which is
    // what would make the next close delete a stranger's row.
    for dropped in &pool {
        if let Some(tab_id) = bindings.tab_of(dropped.token) {
            bindings.release(tab_id);
            plan.close_tab_ids.push(tab_id);
        }
    }

    let space = &mut doc.spaces[backing];
    space.name = slice.name.clone();
    space.pinned_collapsed = slice.pinned_collapsed;
    space.pinned_tabs = pinned;
    space.tabs = tabs;
    space.groups = slice
        .folders
        .iter()
        .map(|f| crate::spaces::TabGroup {
            id: f.id.clone(),
            name: f.name.clone(),
            collapsed: f.closed,
            icon: None,
        })
        .collect();

    if !favorites.written.contains(&profile) {
        favorites.written.push(profile.clone());
        // the same cap the interactive add enforces. This path
        // wrote slice.favorites straight through, so one restore payload with
        // a large array bypassed MAX_FAVORITES_PER_PROFILE entirely - and it
        // is a renderer that sends the payload.
        for fav in slice
            .favorites
            .iter()
            .take(mutate::MAX_FAVORITES_PER_PROFILE)
        {
            favorites.rows.push(Favorite {
                url: fav.url.clone(),
                title: fav.title.clone(),
                profile_id: profile.clone(),
                // Legacy and nil by contract - a favourite follows the
                // profile, never one space (see `sidebar::favorite_rows`).
                space_id: None,
            });
        }
    }
    Ok(())
}

/// Applies `payload`, or refuses it and leaves `doc` exactly as it was.
///
/// Returns what the browser still has to do - see [`Plan`].
pub fn apply(
    doc: &mut Document,
    bindings: &mut Bindings,
    payload: &Payload,
) -> Result<Plan, Refusal> {
    validate(doc, payload)?;

    let mut plan = Plan {
        active_space: payload.active_space,
        ..Plan::default()
    };
    // First slice wins per profile: favourites follow the PROFILE, and a
    // whole-window payload can carry several spaces of one profile. They all
    // describe the same grid, so the alternative is writing it twice.
    let mut favorites = ProfileFavorites::default();

    for slice in &payload.spaces {
        apply_slice(doc, bindings, slice, &mut plan, &mut favorites)?;
    }

    doc.favorites
        .retain(|f| !favorites.written.contains(&f.profile_id));
    doc.favorites.append(&mut favorites.rows);

    if let Some(backing) = mutate::space_at(doc, payload.active_space) {
        doc.app_state.active_space_id = Some(doc.spaces[backing].id.clone());
    }

    Ok(plan)
}

fn note_reopen(
    plan: &mut Plan,
    bindings: &Bindings,
    space: usize,
    section: i32,
    slot: usize,
    row: &Row,
    group_id: Option<&str>,
) {
    if row.id.is_empty() || live_token(bindings, &row.id).is_some() {
        return;
    }
    plan.reopen.push(Reopen {
        space,
        section,
        slot,
        url: row.url.clone(),
        title: row.title.clone(),
        group_id: group_id.map(str::to_string),
        selected: row.selected,
    });
}
