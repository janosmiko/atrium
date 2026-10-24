// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The Atrium model core.
//!
//! C++ reaches this crate only through the hand-written header in
//! `include/atrium_core.h`. Nothing else is exported, so the surface stays
//! small enough to read in one sitting.
//!
//! The crate has two copies: `rust/atrium-core` in the Atrium repo, which is
//! the one you edit, and `third_party/atrium/core` inside the Chromium tree,
//! which GN builds. `make rust-verify` fails when they differ. GN cannot reach
//! outside its checkout, which is why the copy exists.
//!
//! Rust both reads and writes `spaces.json` now. The shapes match
//! what Swift produced byte for byte, deliberately: reshaping during the move
//! would make a mismatch impossible to attribute to either half.
//!
//! The write exports come in two halves, for the same reason the read ones do.
//! An `atrium_core_*` mutation changes the in-memory document and returns
//! whether anything moved - no file IO, safe on the UI thread, and the answer
//! the caller needs to decide whether to re-push the sidebar. The single
//! `atrium_core_save` is the call that blocks, and belongs on a thread that is
//! allowed to.
//!
//! Settings is written by a second blocking call with a different shape, and
//! the difference follows the read rather than being a second convention: no
//! document is kept for `settings.json`, so `atrium_core_save_settings` takes
//! the path and the whole object on every call, exactly as `atrium_core_query`
//! takes the path's contents on every read.
//!
//! Every `const char*` an export takes is borrowed through exactly one of two
//! helpers, and the helper AT THE CALL SITE is the declaration of which
//! contract that argument has - a doc line would drift, a call cannot.
//! `required_str` refuses a NULL. `omittable_str` reads a NULL as `""` on
//! purpose, and is used at eleven pointers, listed in the header next to
//! `atrium_core_abi_version`. The default is `required_str` because the
//! alternative loses data: a NULL body reaching `set_note_for_space` as `""`
//! deletes the note the caller never meant to touch.
//!
//! Two things the crate carries that the file does not. Opening and closing a
//! tab is persisted through a BINDING from a live Chromium tab id to a
//! persisted row - see `tabs` for why a URL alone cannot do that
//! job. And a launch reads which profiles to load and which tab to restore
//! from the same document - see `launch`.

pub mod cache;
pub mod choices;
pub mod commands;
pub mod icon_links;
pub mod import;
pub mod launch;
pub mod lifecycle;
pub mod mutate;
pub mod navigation;
pub mod persist;
pub mod redact;
pub mod restore;
pub mod schema;
pub mod search_engines;
pub mod settings;
pub mod sidebar;
pub mod small_stores;
pub mod spaces;
pub mod tab_recency;
pub mod tabs;
#[cfg(test)]
mod test_temp_dir;
pub mod theme_math;
pub mod tint;
pub mod tokens;
pub mod visits;

use std::collections::BTreeMap;
use std::ffi::{CStr, CString, c_char};
use std::path::Path;

/// The shape of the C ABI. Raise it whenever a signature in
/// `include/atrium_core.h` changes, and change the `ATRIUM_CORE_ABI_VERSION`
/// define in that header to match.
///
/// A C++ unit test compares the two. That test is the whole point of the
/// number: a mismatch means the header and the linked library disagree about
/// what the functions look like, which is silent corruption rather than a
/// build failure.
pub const ABI_VERSION: u32 = 65;

/// Returns the ABI version this library was built with.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_abi_version() -> u32 {
    ABI_VERSION
}

/// Which store a query is about. The names match the directories in
/// `fixtures/stores`, so a corpus case and an ABI call say the same word.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Store {
    Spaces,
    Settings,
    Themes,
    Hotkeys,
    Routing,
    SelectionMemory,
    // Not a file - a computed view over HOTKEY_ACTIONS, so C++ can enumerate
    // it without a hand-copied list.
    HotkeyActionIds,
    // Not a file either - the Inputs pane's edit in, the hotkeys file it
    // should write out, so the reset rule stays on this side.
    HotkeysEdit,
    // Every preset's table, so picking one in the pane is not a round trip.
    HotkeyPresets,
}

impl Store {
    pub fn from_name(name: &str) -> Option<Self> {
        match name {
            "spaces" => Some(Self::Spaces),
            "settings" => Some(Self::Settings),
            "themes" => Some(Self::Themes),
            "hotkeys" => Some(Self::Hotkeys),
            "routing" => Some(Self::Routing),
            "command-bar-selection-memory" => Some(Self::SelectionMemory),
            "hotkey-action-ids" => Some(Self::HotkeyActionIds),
            "hotkeys-edit" => Some(Self::HotkeysEdit),
            "hotkey-presets" => Some(Self::HotkeyPresets),
            _ => None,
        }
    }
}

/// The instant a v1 spaces.json stamps `archiveEpoch` from, when the caller
/// does not supply one. Seconds since Swift's reference date.
///
/// A parameter rather than a clock read: two loads of the same file have to
/// produce the same bytes, or nothing downstream can be compared.
pub type SwiftDate = f64;

/// Loads `json` as `store` and returns the loaded state as JSON.
///
/// This is the whole read path. A store that cannot read its input returns its
/// defaults, matching what Swift does - and nothing here writes those defaults
/// back. Refusing to overwrite a file this build cannot read is the half of the
/// rule that saves the user's data (`docs/model-rules.md` R1.2).
///
/// It used to hold for the five small stores by construction, because none of
/// them had a writer. Settings and routing have one now, each re-reading its
/// file and refusing it itself rather than trusting whoever calls it. The
/// other three still have no write path at all.
pub fn query(
    store: Store,
    json: &str,
    migration_instant: SwiftDate,
) -> Result<String, schema::StoreError> {
    let out = match store {
        Store::Spaces => {
            // A refused file has no loaded state to report. Saying so is not
            // the same as reporting empty defaults, and the caller must not
            // write either one back - which is why the variant travels out of
            // here rather than a sentence about it.
            let doc = spaces::load(json, migration_instant)?;
            serde_json::to_string(&doc)
        }
        Store::Settings => serde_json::to_string(&settings::load(json)),
        Store::Themes => serde_json::to_string(&small_stores::load_themes(json)),
        Store::Hotkeys => serde_json::to_string(&small_stores::load_hotkeys(json)),
        Store::Routing => serde_json::to_string(&small_stores::load_routing(json)),
        Store::SelectionMemory => serde_json::to_string(&small_stores::load_selection_memory(json)),
        Store::HotkeyActionIds => serde_json::to_string(small_stores::HOTKEY_ACTIONS),
        Store::HotkeysEdit => {
            let edit = small_stores::load_hotkeys_edit(json).map_err(schema::StoreError::Schema)?;
            serde_json::to_string(&small_stores::hotkeys_after_edit(&edit))
        }
        Store::HotkeyPresets => serde_json::to_string(&small_stores::hotkey_preset_tables()),
    };
    out.map_err(|e| schema::StoreError::Encode(e.to_string()))
}

/// Loads a store's JSON and returns the loaded state as JSON.
///
/// Returns null on any failure, including an unknown store name, input that is
/// not UTF-8, and a file this build refuses to read. The caller frees a
/// non-null result with `atrium_core_string_free`.
///
/// # Safety
/// `store` and `json` must be non-null, NUL-terminated C strings that stay
/// valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_query(
    store: *const c_char,
    json: *const c_char,
    migration_instant: f64,
) -> *mut c_char {
    if store.is_null() || json.is_null() {
        return std::ptr::null_mut();
    }
    // SAFETY: the caller promises both pointers are NUL-terminated and valid
    // for the duration of this call, which is the contract in the header.
    let (store_name, input) = unsafe { (CStr::from_ptr(store), CStr::from_ptr(json)) };

    let (Ok(store_name), Ok(input)) = (store_name.to_str(), input.to_str()) else {
        return std::ptr::null_mut();
    };
    let Some(store) = Store::from_name(store_name) else {
        return std::ptr::null_mut();
    };
    json_or_null(query(store, input, migration_instant).ok())
}

/// The space a URL should open in, as a target space id, or null for no match.
///
/// Takes the routing file's JSON rather than reading it: the store layer here
/// has never done file IO, and the caller already loads this file once at wiring
/// time. Parsing per call is affordable because navigations happen at human
/// speed and a routing file holds a handful of rules.
///
/// `source_space_id` is the space the navigation started in, or NULL when
/// unknown. A rule that excludes that space is skipped.
///
/// Null means "no rule claims this URL" and also every failure - an unreadable
/// argument, a file that does not parse, a schema this build refuses. They are
/// the same answer on purpose: routing is an enhancement, and a caller that
/// cannot tell them apart still does the right thing, which is to open the URL
/// where it already was going.
///
/// The caller frees a non-null result with `atrium_core_string_free`.
///
/// # Safety
/// `routing_json` and `url` must be non-null, NUL-terminated C strings that stay
/// valid for this call. `source_space_id` may be null, and is otherwise the same.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_route_for_url(
    routing_json: *const c_char,
    url: *const c_char,
    source_space_id: *const c_char,
) -> *mut c_char {
    if routing_json.is_null() || url.is_null() {
        return std::ptr::null_mut();
    }
    // SAFETY: the caller promises the pointers are NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let (json, url) = unsafe { (CStr::from_ptr(routing_json), CStr::from_ptr(url)) };
    let (Ok(json), Ok(url)) = (json.to_str(), url.to_str()) else {
        return std::ptr::null_mut();
    };
    let source = unsafe { optional_str(source_space_id) };
    let file = small_stores::load_routing(json);
    json_or_null(small_stores::route_for_url(&file, url, source.as_deref()).map(str::to_owned))
}

/// The icon links in a page head, as `{"base":...,"icons":[...]}` ranked best
/// first. `html` is `len` bytes and need not be NUL-terminated or UTF-8.
///
/// # Safety
/// `html` must point to `len` readable bytes for the duration of the call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_icon_links(html: *const c_char, len: usize) -> *mut c_char {
    if html.is_null() {
        return std::ptr::null_mut();
    }
    // SAFETY: the caller promises `len` readable bytes at `html`.
    let bytes = unsafe { std::slice::from_raw_parts(html as *const u8, len) };
    // A panic crossing into C aborts the browser process, and this scans bytes a remote host chose.
    match std::panic::catch_unwind(|| icon_links::icon_links_json(bytes)) {
        Ok(json) => json_or_null(json),
        Err(_) => std::ptr::null_mut(),
    }
}

/// Where the space with `space_id` RENDERS, in sidebar order, as
/// {"index":N,"profileId":"..."}.
///
/// Both facts in one call because the routing seam needs them together: asking
/// twice could straddle a mutation and pair an index with another space's
/// profile.
///
/// Null when nothing is open, when the argument is unreadable, or when no space
/// carries that id - a rule naming a space the user has since deleted routes
/// nothing rather than routing somewhere arbitrary.
///
/// The caller frees a non-null result with `atrium_core_string_free`.
///
/// # Safety
/// `space_id` must be a non-null, NUL-terminated C string that stays valid for
/// this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_space_slot_for_id(space_id: *const c_char) -> *mut c_char {
    if space_id.is_null() {
        return std::ptr::null_mut();
    }
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Ok(id) = (unsafe { CStr::from_ptr(space_id) }).to_str() else {
        return std::ptr::null_mut();
    };
    let Some((index, profile_id)) = cache::shared().space_slot_for_id(id) else {
        return std::ptr::null_mut();
    };
    json_or_null(serde_json::json!({"index": index, "profileId": profile_id}).to_string())
}

/// The id of the space at sidebar position `index`. A swipe names the space
/// it heads for by rail position, and the theme store is keyed by id.
/// Null when nothing is open or `index` is past the rail's end. The caller
/// frees a non-null result with `atrium_core_string_free`.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_space_id_at(index: i32) -> *mut c_char {
    let Ok(position) = usize::try_from(index) else {
        return std::ptr::null_mut();
    };
    json_or_null(cache::shared().space_id_at(position))
}

/// The id of the space holding the row this live tab is showing.
///
/// Null when nothing is open, when no row is bound to `tab_id`, or when the
/// binding points at a row that has since gone. A tab with no row is not an
/// error: it has simply not been announced yet.
///
/// The caller frees a non-null result with `atrium_core_string_free`.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_space_id_for_tab(tab_id: u64) -> *mut c_char {
    let found = cache::shared().peek_bound(|doc, bindings| {
        let token = bindings.token_of(tab_id)?;
        let (space, _, _) = mutate::locate_token(doc, token)?;
        Some(doc.spaces[space].id.clone())
    });
    match found.flatten() {
        Some(id) => json_or_null(id),
        None => std::ptr::null_mut(),
    }
}

/// One live Chromium tab, as the caller sees it.
#[derive(Debug, serde::Deserialize)]
struct LiveTab {
    url: String,
    id: u64,
    /// The tab this window is SHOWING, which is what puts the pill on a
    /// sidebar row. Optional so a caller that has not been taught to send it
    /// still gets a model - one with no selected row, which is what the
    /// sidebar drew before this field existed.
    #[serde(default)]
    active: bool,
    /// The favourite this tab IS, and what its tile binds from. A redirect
    /// moves the tab off the tile's url, so no url join can see it.
    #[serde(default)]
    claimed: Option<String>,
    /// A tab in ANOTHER window of this profile, listed so the core can tell
    /// a row or tile whose tab is next door from a cold one. Never joins by
    /// url, and never hands out an id as live here.
    #[serde(default)]
    elsewhere: bool,
}

/// `Document::hidden_rows`, updated for one window's push. Touches only LOCAL
/// tabs (`elsewhere: false`), and drops a token whose row is gone from `doc`.
pub(crate) fn hidden_rows_for(
    live_tabs_json: &str,
    bindings: &tabs::Bindings,
    doc: &spaces::Document,
) -> Vec<spaces::RowToken> {
    let live: Vec<LiveTab> = serde_json::from_str(live_tabs_json).unwrap_or_default();
    let local_tokens: Vec<spaces::RowToken> = live
        .iter()
        .filter(|t| !t.elsewhere)
        .filter_map(|t| bindings.token_of(t.id))
        .collect();
    let newly_hidden = live
        .iter()
        .filter(|t| !t.elsewhere && t.claimed.is_some())
        .filter_map(|t| bindings.token_of(t.id));
    doc.hidden_rows
        .iter()
        .copied()
        .filter(|token| {
            !local_tokens.contains(token) && mutate::locate_token(doc, *token).is_some()
        })
        .chain(newly_hidden)
        .collect()
}

/// Which live tab each favourite is showing, keyed by the favourite's url.
/// Every space of the profile reads this one list, which is what makes a tile
/// mean the same tab in all of them.
pub(crate) fn favorite_claims_for(live: &[LiveTab]) -> Vec<(String, u64)> {
    live.iter()
        .filter_map(|t| t.claimed.as_ref().map(|key| (key.clone(), t.id)))
        .collect()
}

/// Builds the sidebar model for one window.
///
/// `spaces_json` is the persisted document and `live_tabs_json` is that
/// window's live strip as `[{"url": "...", "id": 123, "active": true}, ...]`
/// in strip order. `active` marks the tab the window is showing and may be
/// omitted, in which case no row comes back selected.
///
/// `active_space_index` is which space this window's slot is showing. Pass
/// `None` when the caller does not know - C++ with no Swift to ask - and the
/// document's own `activeSpaceID` decides. That is the only answer the file
/// carries, and it is what a fresh browser shows.
///
/// Returns the model as JSON, or an error. Live tab ids go to the ACTIVE space
/// alone, and they are handed out by consuming a queue - see
/// `sidebar::LiveTabIds` for why a lookup is wrong.
pub fn sidebar_model(
    spaces_json: &str,
    active_space_index: Option<usize>,
    live_tabs_json: &str,
    migration_instant: SwiftDate,
) -> Result<String, schema::LoadError> {
    let doc = spaces::load(spaces_json, migration_instant)?;

    // No bindings and no themes: this entry parses a document off a string
    // and has no store of either to go with it, so the rows fall back to the
    // url join and every space comes back unthemed.
    Ok(sidebar_model_from_document(
        &doc,
        active_space_index,
        live_tabs_json,
        None,
        &BTreeMap::new(),
    ))
}

/// Builds the sidebar model from an already-parsed document.
///
/// The half of `sidebar_model` that does no parsing, so `cache::Core` can
/// build a snapshot from memory without a second copy of these rules.
pub(crate) fn sidebar_model_from_document(
    doc: &spaces::Document,
    active_space_index: Option<usize>,
    live_tabs_json: &str,
    bindings: Option<&tabs::Bindings>,
    themes: &BTreeMap<String, small_stores::ThemeSpec>,
) -> String {
    // An empty or unreadable strip is a window with no live tabs, which is
    // ordinary - every row is cold and the sidebar still renders.
    let live: Vec<LiveTab> = serde_json::from_str(live_tabs_json).unwrap_or_default();
    // The FIRST tab flagged active wins. A strip has exactly one, and a
    // caller that sends several is confused - picking one keeps the "at most
    // one pill" contract instead of letting the last write win silently.
    // A frozen window names the tab that left it as active, and that tab is
    // an elsewhere one - so the search runs over the whole list.
    let active = live.iter().find(|t| t.active).map(|t| t.id);
    let (elsewhere, local): (Vec<LiveTab>, Vec<LiveTab>) =
        live.into_iter().partition(|t| t.elsewhere);
    let claims = favorite_claims_for(&local);
    let elsewhere_claims = favorite_claims_for(&elsewhere);
    let elsewhere_ids: Vec<u64> = elsewhere.iter().map(|t| t.id).collect();
    let pairs: Vec<(String, u64)> = local.into_iter().map(|t| (t.url, t.id)).collect();
    let mut ids = sidebar::LiveTabIds::new(
        &pairs,
        active,
        &elsewhere_claims,
        bindings.map(|b| (b, elsewhere_ids.as_slice())),
    );

    let index = active_space_index.unwrap_or_else(|| {
        sidebar::active_index_from_document(&doc.spaces, doc.app_state.active_space_id.as_deref())
    });

    let mut snapshot = sidebar::snapshot(
        index,
        &doc.spaces,
        &doc.favorites,
        &mut ids,
        &doc.hidden_rows,
        &claims,
    );
    // `index` is a sidebar POSITION; the store is keyed by the backing array.
    if let Some(space) = mutate::active_space(doc, Some(index)) {
        sidebar::mark_marginalia(&mut snapshot, &mutate::urls_with_marginalia(doc, space));
    }
    sidebar::mark_themes(&mut snapshot, themes);
    sidebar::mark_profile_names(&mut snapshot, &doc.profiles);
    sidebar::encode(&snapshot)
}

/// Builds the sidebar model for one window. See `sidebar_model`.
///
/// Returns null on any failure, including a store file this build refuses to
/// read. The caller frees a non-null result with `atrium_core_string_free`.
///
/// # Safety
/// Every pointer must be non-null, NUL-terminated, and valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_sidebar_model(
    spaces_json: *const c_char,
    active_space_index: i32,
    live_tabs_json: *const c_char,
    migration_instant: f64,
) -> *mut c_char {
    if spaces_json.is_null() || live_tabs_json.is_null() {
        return std::ptr::null_mut();
    }
    // Negative means "you decide" - the document's own activeSpaceID.
    let index = usize::try_from(active_space_index).ok();
    // SAFETY: the caller promises both pointers are NUL-terminated and valid
    // for the duration of this call, which is the contract in the header.
    let (spaces_c, live_c) =
        unsafe { (CStr::from_ptr(spaces_json), CStr::from_ptr(live_tabs_json)) };
    let (Ok(spaces), Ok(live)) = (spaces_c.to_str(), live_c.to_str()) else {
        return std::ptr::null_mut();
    };
    json_or_null(sidebar_model(spaces, index, live, migration_instant).ok())
}

/// Reads the spaces store at `spaces_path` and keeps the parsed document.
///
/// Blocking - never call it from the UI thread. See `cache` for why the pair
/// is split this way.
///
/// # Safety
/// `spaces_path` must be a non-null, NUL-terminated C string valid for this
/// call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_open(
    spaces_path: *const c_char,
    migration_instant: f64,
) -> bool {
    if spaces_path.is_null() {
        return false;
    }
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Ok(path) = (unsafe { CStr::from_ptr(spaces_path) }).to_str() else {
        return false;
    };
    cache::shared()
        .open(std::path::Path::new(path), migration_instant)
        .is_ok()
}

/// Forgets the loaded document.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_close() {
    cache::shared().close();
}

/// True once a document is loaded.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_is_open() -> bool {
    cache::shared().is_open()
}

/// Builds the sidebar model from the loaded document. No file IO.
///
/// Returns null when nothing is open - see the header for why that is not an
/// empty model. The caller frees a non-null result with
/// `atrium_core_string_free`.
///
/// # Safety
/// `live_tabs_json` must be a non-null, NUL-terminated C string valid for this
/// call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_sidebar_model_cached(
    active_space_index: i32,
    live_tabs_json: *const c_char,
) -> *mut c_char {
    if live_tabs_json.is_null() {
        return std::ptr::null_mut();
    }
    // Negative means "you decide" - the document's own activeSpaceID.
    let index = usize::try_from(active_space_index).ok();
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Ok(live) = (unsafe { CStr::from_ptr(live_tabs_json) }).to_str() else {
        return std::ptr::null_mut();
    };
    json_or_null(cache::shared().sidebar_model(index, live).ok())
}

/// The loaded document's active space index, or -1 when nothing is open.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_active_space_index() -> i32 {
    cache::shared()
        .active_space_index()
        .and_then(|i| i32::try_from(i).ok())
        .unwrap_or(-1)
}

/// Whether `url` is pinned in the window's space or favourited by `profile_id`,
/// which is auto-peek's one question. False when nothing is open.
///
/// this used to be `UIBridge::IsPeekSourceTab`, and it is the only
/// thing the auto-peek path needed Swift for.
///
/// # Safety
/// `profile_id` and `url` must be non-null, NUL-terminated C strings that stay
/// valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_is_peek_source_url(
    active_space_index: i32,
    profile_id: *const c_char,
    url: *const c_char,
) -> bool {
    if profile_id.is_null() || url.is_null() {
        return false;
    }
    // SAFETY: the caller promises both pointers are NUL-terminated and valid
    // for the duration of this call, which is the contract in the header.
    let (profile_id, url) = unsafe { (CStr::from_ptr(profile_id), CStr::from_ptr(url)) };
    let (Ok(profile_id), Ok(url)) = (profile_id.to_str(), url.to_str()) else {
        return false;
    };
    cache::shared().is_peek_source_url(slot(active_space_index), profile_id, url)
}

/// Whether `profile_id` favourites exactly `url`. A startup tab restored on
/// that url is the tile's tab and is claimed at restore, so a click finds it
/// by claim instead of by url. False when nothing is open.
///
/// # Safety
/// `profile_id` and `url` must be non-null, NUL-terminated C strings that stay
/// valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_is_favorite_url(
    profile_id: *const c_char,
    url: *const c_char,
) -> bool {
    if profile_id.is_null() || url.is_null() {
        return false;
    }
    // SAFETY: the caller promises both pointers are NUL-terminated and valid
    // for the duration of this call, which is the contract in the header.
    let (profile_id, url) = unsafe { (CStr::from_ptr(profile_id), CStr::from_ptr(url)) };
    let (Ok(profile_id), Ok(url)) = (profile_id.to_str(), url.to_str()) else {
        return false;
    };
    cache::shared().is_favorite_url(profile_id, url)
}

// --- Writes -----------------------------------------------------------------
//
// Every one of these mutates the loaded document and returns whether anything
// moved. False covers all the ways a gesture can fail to resolve - nothing
// open, an index the model no longer has, a group id that names nothing - and
// they are deliberately not distinguished: the caller does the same thing for
// all of them, which is nothing.
//
// A SOURCE index that does not resolve is refused; a DESTINATION is clamped
// into range. An index from a surface can be stale by the time it arrives, and
// landing at the end beats dropping the move, while moving an arbitrary row is
// worse than doing nothing.

/// The window's slot as the ABI carries it: negative means "you decide", and
/// the document's own `activeSpaceID` answers. Same convention as the reads.
fn slot(active_space_index: i32) -> Option<usize> {
    usize::try_from(active_space_index).ok()
}

/// A destination index, with a negative clamped to the front.
fn destination(index: i32) -> usize {
    usize::try_from(index).unwrap_or(0)
}

/// Moves a row within the active space's ungrouped list. Both positions count
/// the RENDERED rows.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_reorder_tab(
    active_space_index: i32,
    from_visible: i32,
    to_visible: i32,
) -> bool {
    let (Ok(from_visible), Ok(to_visible)) =
        (usize::try_from(from_visible), usize::try_from(to_visible))
    else {
        return false;
    };
    in_active_space(active_space_index, |doc, space| {
        let Some(from) = mutate::persisted_index_of_visible(
            from_visible,
            &doc.spaces[space],
            &doc.hidden_rows,
        ) else {
            return false;
        };
        let Some(to) = mutate::persisted_index_of_visible(
            to_visible,
            &doc.spaces[space],
            &doc.hidden_rows,
        ) else {
            return false;
        };
        mutate::move_tab(doc, space, from, to, mutate::Section::Regular)
    })
}

/// Moves a pinned ITEM - a pin, or a folder with every member it holds. Both
/// positions count the pinned entries the surface draws, where a folder is one.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_reorder_pinned_item(
    active_space_index: i32,
    from_item: i32,
    to_item: i32,
) -> bool {
    let Ok(from_item) = usize::try_from(from_item) else {
        return false;
    };
    in_active_space(active_space_index, |doc, space| {
        mutate::reorder_pinned_item(doc, space, from_item, destination(to_item))
    })
}

/// Reorders one row of a folder among its siblings.
/// # Safety
/// `group_id` must be a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_reorder_group_member(
    active_space_index: i32,
    group_id: *const c_char,
    from: i32,
    to: i32,
) -> bool {
    let Ok(from) = usize::try_from(from) else {
        return false;
    };
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(id) = (unsafe { required_str(group_id) }) else {
        return false;
    };
    if id.is_empty() {
        return false;
    }
    in_active_space(active_space_index, |doc, space| {
        mutate::reorder_group_member(doc, space, id, from, destination(to))
    })
}

/// Takes one member out of a folder into `to_section` at `to_slot`.
///
/// # Safety
/// `group_id` must be a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_ungroup_member(
    active_space_index: i32,
    group_id: *const c_char,
    member_index: i32,
    to_section: i32,
    to_slot: i32,
) -> bool {
    let Ok(member_index) = usize::try_from(member_index) else {
        return false;
    };
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(id) = (unsafe { required_str(group_id) }) else {
        return false;
    };
    if id.is_empty() {
        return false;
    }
    let to = mutate::Section::from_code(to_section);
    in_active_space(active_space_index, |doc, space| {
        mutate::ungroup_member(doc, space, id, member_index, to, destination(to_slot))
    })
}

/// Pins a folder whose members are regular rows. An already pinned one is
/// refused, because moving it is `atrium_core_reorder_pinned_item`'s job.
///
/// # Safety
/// `group_id` must be a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_pin_folder(
    active_space_index: i32,
    group_id: *const c_char,
    to_item_slot: i32,
) -> bool {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(id) = (unsafe { required_str(group_id) }) else {
        return false;
    };
    if id.is_empty() {
        return false;
    }
    in_active_space(active_space_index, |doc, space| {
        mutate::pin_folder(doc, space, id, destination(to_item_slot))
    })
}

/// Moves a folder within the active space's own list. NOT translated: a
/// folder maps to `sidebar::contents_of`'s `folders` one for one.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_reorder_folder(active_space_index: i32, from: i32, to: i32) -> bool {
    let Ok(from) = usize::try_from(from) else {
        return false;
    };
    in_active_space(active_space_index, |doc, space| {
        mutate::move_group(doc, space, from, destination(to))
    })
}

/// Pins the row at `from_visible` at pinned item SLOT `to_pinned`, where a
/// folder counts as one slot.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_pin_tab(
    active_space_index: i32,
    from_visible: i32,
    to_pinned: i32,
) -> bool {
    let Ok(from_visible) = usize::try_from(from_visible) else {
        return false;
    };
    in_active_space(active_space_index, |doc, space| {
        let Some(from) = mutate::persisted_index_of_visible(
            from_visible,
            &doc.spaces[space],
            &doc.hidden_rows,
        ) else {
            return false;
        };
        mutate::pin_tab(doc, space, from, Some(destination(to_pinned)))
    })
}

/// Moves a space from one SIDEBAR position to another.
///
/// Positions, not backing indices: the surface knows where a card or a dot sat
/// on screen, and the backing array is in insertion order.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_reorder_space(from: i32, to: i32) -> bool {
    let (Ok(from), Ok(to)) = (usize::try_from(from), usize::try_from(to)) else {
        return false;
    };
    cache::shared().apply(|doc| mutate::reorder_space(doc, from, to))
}

/// Archives every ungrouped row in the active space - the Clear control.
///
/// Returns whether anything moved, so a Clear on a space with nothing loose
/// does not dirty the file.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_archive_space_tabs(active_space_index: i32, now: f64) -> bool {
    in_active_space(active_space_index, |doc, space| {
        !mutate::archive_space_tabs(doc, space, now).is_empty()
    })
}

/// Archives the regular row at `index` in the active space - the row menu's
/// Archive Tab.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_archive_tab(active_space_index: i32, index: i32, now: f64) -> bool {
    if index < 0 {
        return false;
    }
    in_active_space(active_space_index, |doc, space| {
        mutate::archive_tab(doc, space, index as usize, now).is_some()
    })
}

/// Archives the regular rows below `index` in the active space - the row
/// menu's Archive Tabs Below.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_archive_tabs_below(
    active_space_index: i32,
    index: i32,
    now: f64,
) -> bool {
    let visible = match index {
        -1 => None,
        i if i >= 0 => Some(i as usize),
        _ => return false,
    };
    in_active_space(active_space_index, |doc, space| {
        !mutate::archive_tabs_below(doc, space, visible, now).is_empty()
    })
}

/// Deletes the pinned row at ITEM `index`, refusing a folder.
/// `atrium_core_unpin_tab` demotes a pin into the regular list, this takes it
/// away, which is what the lab's x on a tile with nothing loaded does.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_remove_pinned_tab(active_space_index: i32, index: i32) -> bool {
    let Ok(index) = usize::try_from(index) else {
        return false;
    };
    in_active_space(active_space_index, |doc, space| {
        let Some(raw) = mutate::pinned_tab_index_of_item(&doc.spaces[space], index) else {
            return false;
        };
        mutate::remove_pinned_tab(doc, space, raw)
    })
}

/// The x on a cold regular row, at RENDERED position `index`. A row with a
/// live tab is refused: its close goes through `atrium_core_close_tab`.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_remove_cold_tab(active_space_index: i32, index: i32) -> bool {
    let Ok(index) = usize::try_from(index) else {
        return false;
    };
    let now = wall_clock_now();
    cache::shared().apply_bound(|doc, bindings| {
        let Some(space) = mutate::active_space(doc, slot(active_space_index)) else {
            return false;
        };
        mutate::remove_cold_tab(doc, bindings, space, index, now)
    })
}

/// Unpins the row at `from_pinned` into the regular list at `to_visible_slot`.
///
/// The destination is a SLOT, not a row: dropping past the last tab is a real
/// destination, and resolving it as a row refuses it because nothing lives
/// there.
/// Pins the row a LIVE TAB is showing, named by that tab's id.
///
/// A favourite dragged into the pinned strip has no visible row
/// position to give: until the favourite is removed its page is drawn as a
/// tile and nothing else, so the caller knows the tab and not the row. The
/// binding already maps one to the other, and it needs no index to do it.
///
/// Refuses a tab bound to a pinned row, which is already where it is going.
///
/// Returns whether the document changed.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_pin_tab_by_tab_id(tab_id: u64, to_pinned: i32) -> bool {
    let to = usize::try_from(to_pinned).ok();
    cache::shared().apply_bound(|doc, bindings| {
        let Some(token) = bindings.token_of(tab_id) else {
            return false;
        };
        let Some((space, mutate::Section::Regular, index)) = mutate::locate_token(doc, token)
        else {
            return false;
        };
        mutate::pin_tab(doc, space, index, to)
    })
}

/// Moves a live tab's regular row to `to_visible_slot`. A favourite dropped in
/// the tab list opens its row at the top, and only the tab id names that row.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_move_tab_by_tab_id(tab_id: u64, to_visible_slot: i32) -> bool {
    let Ok(to_slot) = usize::try_from(to_visible_slot) else {
        return false;
    };
    cache::shared().apply_bound(|doc, bindings| {
        let Some(token) = bindings.token_of(tab_id) else {
            return false;
        };
        let Some((space, mutate::Section::Regular, from)) = mutate::locate_token(doc, token)
        else {
            return false;
        };
        // The slot past the last row has no row, and move_tab clamps to the end.
        let to = mutate::persisted_index_of_visible(
            to_slot,
            &doc.spaces[space],
            &doc.hidden_rows,
        )
        .unwrap_or(usize::MAX);
        mutate::move_tab(doc, space, from, to, mutate::Section::Regular)
    })
}

/// `from_pinned` is a pinned ITEM index and must name a pin, not a folder.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_unpin_tab(
    active_space_index: i32,
    from_pinned: i32,
    to_visible_slot: i32,
) -> bool {
    let (Ok(from_pinned), Ok(to_slot)) = (
        usize::try_from(from_pinned),
        usize::try_from(to_visible_slot),
    ) else {
        return false;
    };
    in_active_space(active_space_index, |doc, space| {
        let Some(to) = mutate::persisted_insertion_index_of_visible_slot(
            to_slot,
            &doc.spaces[space],
            &doc.hidden_rows,
        ) else {
            return false;
        };
        mutate::unpin_tab(doc, space, from_pinned, Some(to))
    })
}

/// Adds the row at `index` to the profile's favourites. `section` is 1 for the
/// pinned list, where `index` is an ITEM index and a folder is refused, and 0
/// for the regular one. A negative `slot` appends.
///
/// An index rather than a URL, because the snapshot's row id is a live tab id,
/// the URL and title are read from the model here, where a favourite's
/// (profile, url) key is already known.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_favorite_row(
    active_space_index: i32,
    index: i32,
    section: i32,
    slot: i32,
) -> bool {
    let Ok(index) = usize::try_from(index) else {
        return false;
    };
    cache::shared().apply_bound(|doc, bindings| {
        let Some(space) = mutate::active_space(doc, crate::slot(active_space_index)) else {
            return false;
        };
        let pinned_raw = if mutate::Section::from_code(section) == mutate::Section::Pinned {
            let Some(raw) = mutate::pinned_tab_index_of_item(&doc.spaces[space], index) else {
                return false;
            };
            Some(raw)
        } else {
            None
        };
        let tab = match pinned_raw {
            Some(raw) => doc.spaces[space].pinned_tabs.get(raw),
            None => match mutate::persisted_index_of_visible(
                index,
                &doc.spaces[space],
                &doc.hidden_rows,
            ) {
                Some(at) => doc.spaces[space].tabs.get(at),
                None => None,
            },
        };
        let Some(tab) = tab else {
            return false;
        };
        // The user's rename wins over the page title, matching what the row
        // shows and what the other arm favourited.
        let url = tab.url.clone();
        let title = tab
            .custom_title
            .clone()
            .unwrap_or_else(|| tab.title.clone());
        let profile = doc.spaces[space].profile_id.clone();
        if !mutate::add_favorite(doc, &url, &title, &profile) {
            return false;
        }
        // the drag is a MOVE, so the source goes. A regular row needs
        // nothing done to it - contents_of stops drawing a tab whose url is a
        // favourite - but a pinned row is deliberately never filtered, because
        // pinning is a per-space act the user took. So the tab stayed pinned AND
        // became a favourite, which is the one case where the gesture visibly
        // copied instead of moving.
        // The tile shows the tab now. A binding left on the deleted row makes
        // a later move-out refuse to open a row for it.
        if let Some(raw) = pinned_raw {
            let token = doc.spaces[space].pinned_tabs[raw].token;
            if let Some(tab_id) = bindings.tab_of(token) {
                bindings.release(tab_id);
            }
            mutate::remove_pinned_tab(doc, space, raw);
        }
        // add_favorite appends, so the tile landed past the slot the
        // pointer was on. Moving it afterwards reuses the same profile-relative
        // index translation the grid's own reorder commits through.
        if let Ok(slot) = usize::try_from(slot) {
            let owned = doc
                .favorites
                .iter()
                .filter(|f| f.profile_id == profile)
                .count();
            mutate::move_favorite(doc, &profile, owned - 1, slot.min(owned - 1));
        }
        true
    })
}

/// Only reads the pinned tab, never mutates it. `pinned_index`
/// is an ITEM index and a folder is refused.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_add_doorway(active_space_index: i32, pinned_index: i32) -> bool {
    let Ok(pinned_index) = usize::try_from(pinned_index) else {
        return false;
    };
    in_active_space(active_space_index, |doc, space| {
        let Some(raw) = mutate::pinned_tab_index_of_item(&doc.spaces[space], pinned_index) else {
            return false;
        };
        let Some(tab) = doc.spaces[space].pinned_tabs.get(raw) else {
            return false;
        };
        let url = tab.url.clone();
        let space_id = doc.spaces[space].id.clone();
        let profile_id = doc.spaces[space].profile_id.clone();
        mutate::add_doorway(doc, &url, &space_id, &profile_id)
    })
}

/// The `#RRGGBB` the chrome paints for the active space showing a page coloured
/// `page_hex`, at opacity `glass_alpha`. Null when there is no active space.
/// Free a non-null result with `atrium_core_string_free`.
///
/// The bound and the `ink_hex` contrast floor grade the COMPOSITED colour, so a
/// thinner glass is answered with a stronger value. See
/// `tint::page_tint_over_glass`.
///
/// The chromePageTint setting is the CALLER's gate: the core has no settings
/// singleton, so nothing here reads it.
///
/// # Safety
/// `page_hex` and `ink_hex` must be null or NUL-terminated C strings valid for
/// this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_page_tint(
    active_space_index: i32,
    page_hex: *const c_char,
    ink_hex: *const c_char,
    glass_alpha: f64,
) -> *mut c_char {
    // SAFETY: the caller promises null or NUL-terminated pointers valid for
    // this call, which is the contract in the header.
    let (page, ink) = unsafe { (omittable_str(page_hex), omittable_str(ink_hex)) };
    let Some(base) = cache::shared().space_tint(slot(active_space_index)) else {
        return std::ptr::null_mut();
    };
    let shifted =
        tint::page_tint_over_glass(&base, page.unwrap_or(""), ink.unwrap_or(""), glass_alpha);
    json_or_null(shifted)
}

/// JSON `{"id","spaceID","title","body","updatedAt"}`, or null. Free a
/// non-null result with `atrium_core_string_free`.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_get_space_note(active_space_index: i32) -> *mut c_char {
    json_or_null(cache::shared().space_note(slot(active_space_index)))
}

/// The space position to pass to `atrium_core_open_tab` for a tab of
/// `profile_id`, or `active_space_index` when the window's own space is that
/// profile's. Creates a space for a profile that has none.
/// # Safety
/// `profile_id` must be null or a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_space_for_profile(
    active_space_index: i32,
    profile_id: *const c_char,
) -> i32 {
    let own = active_space_index.max(-1);
    let Some(id) = (unsafe { required_str(profile_id) }) else {
        return own;
    };
    let core = cache::shared();
    core.apply(|doc| mutate::ensure_space_for_profile(doc, id));
    core.space_for_profile(slot(active_space_index), id)
        .and_then(|p| i32::try_from(p).ok())
        .unwrap_or(own)
}

/// The active space's profile id, or null. Free with `atrium_core_string_free`.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_active_space_profile_id(active_space_index: i32) -> *mut c_char {
    json_or_null(cache::shared().active_space_profile_id(slot(active_space_index)))
}

/// `profile_id`'s stored search engine choice, or null. Free with
/// `atrium_core_string_free`.
/// # Safety
/// `profile_id` must be a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_profile_search_engine(
    profile_id: *const c_char,
) -> *mut c_char {
    let Some(id) = (unsafe { required_str(profile_id) }) else {
        return std::ptr::null_mut();
    };
    json_or_null(cache::shared().profile_search_engine(id))
}

/// `now` follows `atrium_core_open`'s `migration_instant` convention.
/// # Safety
/// `body` must be null or a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_set_space_note(
    active_space_index: i32,
    body: *const c_char,
    now: f64,
) -> bool {
    // SAFETY: the caller promises a null or NUL-terminated pointer valid for
    // this call, which is the contract in the header.
    let Some(body) = (unsafe { required_str(body) }) else {
        return false;
    };
    in_active_space(active_space_index, |doc, space| {
        mutate::set_note_for_space(doc, space, body, now)
    })
}

/// The new highlight's id, or null when refused. Free a non-null result with
/// `atrium_core_string_free`.
/// # Safety
/// `url` and `quote` must be null or NUL-terminated and valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_add_marginal(
    active_space_index: i32,
    url: *const c_char,
    quote: *const c_char,
    now: f64,
) -> *mut c_char {
    // SAFETY: the caller promises null or NUL-terminated pointers valid for
    // this call, which is the contract in the header.
    let (Some(url), Some(quote)) = (unsafe { required_str(url) }, unsafe { required_str(quote) })
    else {
        return std::ptr::null_mut();
    };
    json_or_null(in_active_space_value(active_space_index, |doc, space| {
        mutate::add_marginal(doc, space, url, quote, now)
    }))
}

/// False when the id resolves to nothing or the note already read that way.
/// # Safety
/// `id` and `note` must be null or NUL-terminated and valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_set_marginal_note(
    id: *const c_char,
    note: *const c_char,
) -> bool {
    // SAFETY: as above.
    let (Some(id), Some(note)) = (unsafe { required_str(id) }, unsafe { required_str(note) })
    else {
        return false;
    };
    cache::shared().apply(|doc| mutate::set_marginal_note(doc, id, note))
}

/// This space's highlights on `url`, as a JSON array. Free a non-null result
/// with `atrium_core_string_free`.
/// # Safety
/// `url` must be null or NUL-terminated and valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_marginalia_for_url(
    active_space_index: i32,
    url: *const c_char,
) -> *mut c_char {
    // SAFETY: as above.
    let Some(url) = (unsafe { required_str(url) }) else {
        return std::ptr::null_mut();
    };
    json_or_null(cache::shared().marginalia_for_url(slot(active_space_index), url))
}

/// `Core::apply` with the active space already resolved.
///
/// This preamble was written out 23 times: an `apply` whose first act is to
/// look up the active space and give up if there is none. Nothing
/// is lost by folding it in, because "no active space" and "the mutation moved
/// nothing" are the same answer to the caller - `false`.
fn in_active_space<F>(active_space_index: i32, change: F) -> bool
where
    F: FnOnce(&mut spaces::Document, usize) -> bool,
{
    cache::shared().apply(
        |doc| match mutate::active_space(doc, slot(active_space_index)) {
            Some(space) => change(doc, space),
            None => false,
        },
    )
}

/// [`in_active_space`] for a mutation that also produces a value.
fn in_active_space_value<F, T>(active_space_index: i32, change: F) -> Option<T>
where
    F: FnOnce(&mut spaces::Document, usize) -> Option<T>,
{
    cache::shared().apply_value(|doc| {
        let space = mutate::active_space(doc, slot(active_space_index))?;
        change(doc, space)
    })
}

/// A string to a raw C string, nothing to null - the tail of every export
/// below that returns one.
///
/// Takes a `String` or an `Option<String>`, because it used to exist in both
/// this extracted form and eight inlined copies, and a reader could not tell
/// which was canonical. One helper that accepts both shapes leaves
/// nothing to inline.
fn json_or_null(json: impl Into<Option<String>>) -> *mut c_char {
    match json.into().and_then(|j| CString::new(j).ok()) {
        Some(s) => s.into_raw(),
        None => std::ptr::null_mut(),
    }
}

/// `bytes` to a raw buffer plus its length via `out_len` - the byte
/// counterpart of `json_or_null`, for `atrium_core_theme_raster`.
fn bytes_or_null(bytes: Vec<u8>, out_len: *mut usize) -> *mut u8 {
    // A null out_len leaves the caller no way to learn the length, and so no
    // way to ever free what this would otherwise hand back.
    if out_len.is_null() {
        return std::ptr::null_mut();
    }
    let boxed = bytes.into_boxed_slice();
    let len = boxed.len();
    // SAFETY: just checked non-null. The header requires it point at a
    // writable `size_t` for the duration of the call.
    unsafe { *out_len = len };
    Box::into_raw(boxed) as *mut u8
}

/// `bytes_or_null`'s failure tail: zeroes `out_len` when present and returns
/// null.
fn null_bytes(out_len: *mut usize) -> *mut u8 {
    if !out_len.is_null() {
        // SAFETY: just checked non-null. See `bytes_or_null`.
        unsafe { *out_len = 0 };
    }
    std::ptr::null_mut()
}

/// The Notes overview list, as a JSON array. Free a non-null
/// result with `atrium_core_string_free`.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_notes_overview() -> *mut c_char {
    json_or_null(cache::shared().notes_overview())
}

/// A note by id, space pad or freestanding, as JSON.
/// # Safety
/// `id` must be null or a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_note_get(id: *const c_char) -> *mut c_char {
    // SAFETY: the caller promises a null or NUL-terminated pointer valid for
    // this call, which is the contract in the header.
    let Some(id) = (unsafe { required_str(id) }) else {
        return std::ptr::null_mut();
    };
    json_or_null(cache::shared().note_get(id))
}

/// Creates an empty freestanding note and returns it as JSON.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_note_create(now: f64) -> *mut c_char {
    // The note comes back as a value and is serialised out here, so a serde
    // failure can no longer decide whether the document is dirty. It moved.
    let created =
        cache::shared().apply_value(|doc| Some(mutate::create_freestanding_note(doc, now)));
    json_or_null(created.and_then(|note| serde_json::to_string(&note).ok()))
}

/// # Safety
/// `id` and `body` must each be null or a NUL-terminated C string valid for
/// this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_note_set_body(
    id: *const c_char,
    body: *const c_char,
    now: f64,
) -> bool {
    // SAFETY: the caller promises the pointers are NUL-terminated and valid
    // for the duration of this call, which is the contract in the header.
    let Some(id) = (unsafe { required_str(id) }) else {
        return false;
    };
    let Some(body) = (unsafe { required_str(body) }) else {
        return false;
    };
    cache::shared().apply(|doc| mutate::set_note_body_by_id(doc, id, body, now))
}

/// # Safety
/// `id` must be null or a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_note_dismiss(id: *const c_char) -> bool {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(id) = (unsafe { required_str(id) }) else {
        return false;
    };
    cache::shared().apply(|doc| mutate::dismiss_note(doc, id))
}

/// # Safety
/// `id` must be null or a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_note_delete(id: *const c_char, now: f64) -> bool {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(id) = (unsafe { required_str(id) }) else {
        return false;
    };
    cache::shared().apply(|doc| mutate::delete_note(doc, id, now))
}

/// The Shelf overview list, as a JSON array. Free a non-null
/// result with `atrium_core_string_free`.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_shelf_overview() -> *mut c_char {
    json_or_null(cache::shared().shelf_overview())
}

/// # Safety
/// `kind`, `title` and `url` must be NUL-terminated C strings valid for this
/// call. `space_id` must be null or likewise valid.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_shelf_add(
    kind: *const c_char,
    title: *const c_char,
    url: *const c_char,
    space_id: *const c_char,
    now: f64,
) -> *mut c_char {
    // SAFETY: the caller promises the pointers are NUL-terminated and valid
    // for the duration of this call, which is the contract in the header.
    let Some(kind) = (unsafe { required_str(kind) }) else {
        return std::ptr::null_mut();
    };
    let Some(title) = (unsafe { required_str(title) }) else {
        return std::ptr::null_mut();
    };
    let Some(url) = (unsafe { required_str(url) }) else {
        return std::ptr::null_mut();
    };
    let space_id = unsafe { omittable_str(space_id) };
    let created = cache::shared()
        .apply_value(|doc| Some(mutate::add_shelf_item(doc, kind, title, url, space_id, now)));
    json_or_null(created.and_then(|item| serde_json::to_string(&item).ok()))
}

/// # Safety
/// `id` must be null or a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_shelf_remove(id: *const c_char) -> bool {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(id) = (unsafe { required_str(id) }) else {
        return false;
    };
    cache::shared().apply(|doc| mutate::remove_shelf_item(doc, id))
}

/// The Archived overview list, as a JSON array. Free a
/// non-null result with `atrium_core_string_free`.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_archived_overview() -> *mut c_char {
    json_or_null(cache::shared().archived_overview())
}

/// # Safety
/// `id` must be null or a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_archived_remove(id: *const c_char) -> bool {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(id) = (unsafe { required_str(id) }) else {
        return false;
    };
    cache::shared().apply(|doc| mutate::remove_archived_tab(doc, id))
}

/// The Snoozed overview list, as a JSON array. Free a
/// non-null result with `atrium_core_string_free`.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_snoozed_overview() -> *mut c_char {
    json_or_null(cache::shared().snoozed_overview())
}

/// # Safety
/// `id` must be null or a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_snoozed_remove(id: *const c_char) -> bool {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(id) = (unsafe { required_str(id) }) else {
        return false;
    };
    cache::shared().apply(|doc| mutate::remove_snoozed_tab(doc, id))
}

/// # Safety
/// `threshold` must be null or a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_sweep_archive(threshold: *const c_char, now: f64) -> bool {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(threshold) = (unsafe { required_str(threshold) }) else {
        return false;
    };
    cache::shared().apply_bound(|doc, bindings| {
        !mutate::sweep_archive(doc, bindings, threshold, now).is_empty()
    })
}

/// `section` is 1 for pinned, 0 for regular, matching `atrium_core_favorite_row`.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_snooze_tab(
    active_space_index: i32,
    section: i32,
    index: i32,
    wake_at: f64,
    alert: bool,
    now: f64,
) -> bool {
    let Ok(index) = usize::try_from(index) else {
        return false;
    };
    in_active_space(active_space_index, |doc, space| {
        let token = match mutate::Section::from_code(section) {
            mutate::Section::Pinned => mutate::pinned_tab_index_of_item(&doc.spaces[space], index)
                .and_then(|at| doc.spaces[space].pinned_tabs.get(at))
                .map(|t| t.token),
            mutate::Section::Regular => mutate::persisted_index_of_visible(
                index,
                &doc.spaces[space],
                &doc.hidden_rows,
            )
            .and_then(|at| doc.spaces[space].tabs.get(at))
            .map(|t| t.token),
        };
        let Some(token) = token else {
            return false;
        };
        mutate::snooze_tab(doc, token, wake_at, alert, now).is_some()
    })
}

/// Removes a favourite from the active space's profile. A URL rather than a
/// grid index: a favourite has no id of its own, and this keys the removal the
/// same way `atrium_core_favorite_row` keys the addition.
///
/// # Safety
/// `url` must be NUL-terminated and valid for the duration of this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_remove_favorite(
    active_space_index: i32,
    url: *const c_char,
) -> bool {
    if url.is_null() {
        return false;
    }
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Ok(url) = (unsafe { CStr::from_ptr(url) }).to_str() else {
        return false;
    };
    let url = url.to_string();
    in_active_space(active_space_index, |doc, space| {
        let profile = doc.spaces[space].profile_id.clone();
        mutate::remove_favorite(doc, &profile, &url)
    })
}

/// Reorders a favourite within the grid. Both are GRID positions, and
/// `to_visible` is already adjusted for the tile leaving its old slot - the
/// surface applies that adjustment because it is the same one its live preview
/// used, and the preview and the commit have to resolve the same order.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_reorder_favorite(
    active_space_index: i32,
    from_visible: i32,
    to_visible: i32,
) -> bool {
    let (Ok(from_visible), Ok(to_visible)) =
        (usize::try_from(from_visible), usize::try_from(to_visible))
    else {
        return false;
    };
    in_active_space(active_space_index, |doc, space| {
        let Some((from, to)) = mutate::profile_favorite_move(
            from_visible,
            to_visible,
            &doc.spaces[space],
            &doc.favorites,
        ) else {
            return false;
        };
        let profile = doc.spaces[space].profile_id.clone();
        mutate::move_favorite(doc, &profile, from, to)
    })
}

/// Moves a row from one space to another. `from_section` is 1 for the pinned
/// list and 0 for the regular one; a negative `to_visible_slot` appends.
///
/// Both spaces are positions in sidebar order and NEITHER is the window's
/// active space: paging to the destination during the drag has already changed
/// which space is active by the time the drop lands, so resolving the source
/// against "active" would address the wrong space's tabs.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_move_tab_to_space(
    from_space: i32,
    from_index: i32,
    from_section: i32,
    to_space: i32,
    to_visible_slot: i32,
) -> bool {
    let (Ok(from_space), Ok(from_index), Ok(to_space)) = (
        usize::try_from(from_space),
        usize::try_from(from_index),
        usize::try_from(to_space),
    ) else {
        return false;
    };
    if from_space == to_space {
        return false;
    }
    let section = mutate::Section::from_code(from_section);
    cache::shared().apply(|doc| {
        let (Some(source), Some(target)) = (
            mutate::space_at(doc, from_space),
            mutate::space_at(doc, to_space),
        ) else {
            return false;
        };
        let from = match section {
            mutate::Section::Pinned => {
                mutate::pinned_tab_index_of_item(&doc.spaces[source], from_index)
            }
            mutate::Section::Regular => mutate::persisted_index_of_visible(
                from_index,
                &doc.spaces[source],
                &doc.hidden_rows,
            ),
        };
        let Some(from) = from else {
            return false;
        };
        // The destination slot counts the TARGET space's rendered rows, so it
        // is resolved against that space rather than the source.
        let to = match usize::try_from(to_visible_slot) {
            Ok(slot) => {
                match mutate::persisted_insertion_index_of_visible_slot(
                    slot,
                    &doc.spaces[target],
                    &doc.hidden_rows,
                ) {
                    Some(at) => Some(at),
                    None => return false,
                }
            }
            // Negative is what an older surface sends by omitting the argument,
            // and it means append.
            Err(_) => None,
        };
        mutate::move_tab_across_spaces(doc, source, from, section, target, to)
    })
}

/// Moves a split row's tabs as one run. `slots_json` lists its rendered slots.
///
/// # Safety
/// `slots_json` must be null or a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_move_tab_run(
    from_space: i32,
    slots_json: *const c_char,
    to_space: i32,
    to_visible_slot: i32,
) -> bool {
    let (Ok(from_space), Ok(to_space)) = (usize::try_from(from_space), usize::try_from(to_space))
    else {
        return false;
    };
    // SAFETY: the header's contract, above.
    let Some(json) = (unsafe { required_str(slots_json) }) else {
        return false;
    };
    let Ok(slots) = serde_json::from_str::<Vec<usize>>(json) else {
        return false;
    };
    cache::shared().apply(|doc| {
        let (Some(source), Some(target)) = (
            mutate::space_at(doc, from_space),
            mutate::space_at(doc, to_space),
        ) else {
            return false;
        };
        let Some(members) = slots
            .iter()
            .map(|&slot| {
                mutate::persisted_index_of_visible(
                    slot,
                    &doc.spaces[source],
                    &doc.hidden_rows,
                )
            })
            .collect::<Option<Vec<usize>>>()
        else {
            return false;
        };
        let to = match usize::try_from(to_visible_slot) {
            Ok(slot) => match mutate::persisted_insertion_index_of_visible_slot(
                slot,
                &doc.spaces[target],
                &doc.hidden_rows,
            ) {
                Some(at) => Some(at),
                None => return false,
            },
            Err(_) => None,
        };
        mutate::move_run(doc, source, &members, target, to)
    })
}

/// Moves the row a live tab is showing into the space named by `space_id`,
/// appended to that space's regular list.
///
/// By ID rather than by slot, because the routing seam knows the space a rule
/// names and the tab that asked for it, and nothing about either position.
/// False when the tab has no row, when no space carries that id, or when the
/// row is already there - all three mean the document must not change.
///
/// # Safety
/// `space_id` must be a non-null, NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_move_tab_id_to_space(
    tab_id: u64,
    space_id: *const c_char,
) -> bool {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(id) = (unsafe { required_str(space_id) }) else {
        return false;
    };
    cache::shared().apply_bound(|doc, bindings| {
        let Some(token) = bindings.token_of(tab_id) else {
            return false;
        };
        let Some((from_space, section, from_index)) = mutate::locate_token(doc, token) else {
            return false;
        };
        let Some(to_space) = doc.spaces.iter().position(|s| s.id == id) else {
            return false;
        };
        if from_space == to_space {
            return false;
        }
        mutate::move_tab_across_spaces(doc, from_space, from_index, section, to_space, None)
    })
}

/// Places a room on the Courtyard plane. `space` is a position in sidebar
/// order, never the window's active space: the Courtyard shows every room at
/// once, so "active" names the wrong one for any room the user did not open
/// from.
///
/// Returns false and stores nothing for a NaN or out-of-plane value, so the
/// view keeps its last good rect instead of losing the room off the plane.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_set_space_canvas_rect(
    space: i32,
    x: f64,
    y: f64,
    w: f64,
    h: f64,
) -> bool {
    let Ok(space) = usize::try_from(space) else {
        return false;
    };
    if !spaces::canvas_rect_is_valid(x, y, w, h) {
        return false;
    }
    cache::shared().apply(|doc| {
        let Some(index) = mutate::space_at(doc, space) else {
            return false;
        };
        doc.spaces[index].canvas = Some(spaces::CanvasRect { x, y, w, h });
        true
    })
}

/// Sets a card's x offset inside its lane. `section` is 1 for the pinned list
/// and 0 for the regular one, the same code `atrium_core_move_tab_to_space`
/// takes. There is no favourites code: a favourite is keyed by (profile, url)
/// and is not a `SpaceTab`, so it has nowhere to keep an offset.
///
/// For section 0, `index` counts the RENDERED rows. The Courtyard draws
/// `SpaceChip.tabs`, which drops grouped tabs and favourited urls, so section
/// 0 needs the same translation `atrium_core_reorder_tab` uses. For section 1,
/// `index` is a pinned ITEM index and a folder is refused.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_set_tab_canvas_x(
    space: i32,
    section: i32,
    index: i32,
    x: f64,
) -> bool {
    let (Ok(space), Ok(index)) = (usize::try_from(space), usize::try_from(index)) else {
        return false;
    };
    if !spaces::canvas_x_is_valid(x) {
        return false;
    }
    if section != 0 && section != 1 {
        return false;
    }
    cache::shared().apply(|doc| {
        let Some(slot) = mutate::space_at(doc, space) else {
            return false;
        };
        let at = if section == 1 {
            match mutate::pinned_tab_index_of_item(&doc.spaces[slot], index) {
                Some(raw) => raw,
                None => return false,
            }
        } else {
            match mutate::persisted_index_of_visible(
                index,
                &doc.spaces[slot],
                &doc.hidden_rows,
            ) {
                Some(at) => at,
                None => return false,
            }
        };
        let list = if section == 1 {
            &mut doc.spaces[slot].pinned_tabs
        } else {
            &mut doc.spaces[slot].tabs
        };
        let Some(tab) = list.get_mut(at) else {
            return false;
        };
        tab.canvas_x = Some(x);
        true
    })
}

/// Puts the row at `index` into `group_id`. An empty or null id ungroups, and
/// an id that names no group in this space does nothing. `section` is 1 for the
/// pinned list, where `index` counts ITEMS, and 0 for the rendered rows.
/// `member_slot` places a JOINING row among the members - negative for last.
///
/// # Safety
/// `group_id` must be null or a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_assign_tab_to_group(
    active_space_index: i32,
    section: i32,
    index: i32,
    group_id: *const c_char,
    member_slot: i32,
) -> bool {
    let Ok(index) = usize::try_from(index) else {
        return false;
    };
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(id) = (unsafe { required_str(group_id) }) else {
        return false;
    };
    let section = mutate::Section::from_code(section);
    let member_slot = usize::try_from(member_slot).ok();
    in_active_space(active_space_index, |doc, space| {
        let at = match section {
            mutate::Section::Pinned => index,
            mutate::Section::Regular => match mutate::persisted_index_of_visible(
                index,
                &doc.spaces[space],
                &doc.hidden_rows,
            ) {
                Some(at) => at,
                None => return false,
            },
        };
        let wanted = if id.is_empty() { None } else { Some(id) };
        mutate::assign_to_group(doc, space, section, at, wanted, member_slot)
    })
}

/// Puts the tab with `tab_id` into `group_id`, pinning it first when it is not
/// pinned already - a folder's members live in the pinned run. Negative
/// `member_slot` is last. By tab id because the caller just opened this tab for
/// a favourite, so it has no settled row yet.
///
/// # Safety
/// `group_id` must be a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_group_tab_by_tab_id(
    tab_id: u64,
    group_id: *const c_char,
    member_slot: i32,
) -> bool {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(id) = (unsafe { required_str(group_id) }) else {
        return false;
    };
    if id.is_empty() {
        return false;
    }
    let member_slot = usize::try_from(member_slot).ok();
    cache::shared().apply_bound(|doc, bindings| {
        let Some(token) = bindings.token_of(tab_id) else {
            return false;
        };
        let Some((space, section, index)) = mutate::locate_token(doc, token) else {
            return false;
        };
        if section == mutate::Section::Regular && !mutate::pin_tab(doc, space, index, None) {
            return false;
        }
        let Some(s) = doc.spaces.get(space) else {
            return false;
        };
        let Some(raw) = s.pinned_tabs.iter().position(|t| t.token == token) else {
            return false;
        };
        let Some(item) = mutate::item_index_of_pinned_tab(s, raw) else {
            return false;
        };
        mutate::assign_to_group(
            doc,
            space,
            mutate::Section::Pinned,
            item,
            Some(id),
            member_slot,
        )
    })
}

/// Sets whether the pinned SECTION is folded, in the window's active space.
/// Explicit state, not a toggle: the surface has already moved its own DOM and
/// is reporting where it ended up.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_set_pinned_collapsed(
    active_space_index: i32,
    collapsed: bool,
) -> bool {
    in_active_space(active_space_index, |doc, space| {
        mutate::set_pinned_collapsed(doc, space, collapsed)
    })
}

/// The shared body of `atrium_core_set_space_archive_ttl`. Its
/// three siblings moved to `set_profile_field`.
///
/// The pointer is OMITTABLE here, unlike most exports: null and empty both mean
/// "clear this back to Follow General", and the setters collapse them to the
/// same state - see `omittable_str`.
///
/// # Safety
/// `value` must be null or a NUL-terminated C string valid for this call.
unsafe fn set_space_field(
    space_index: i32,
    value: *const c_char,
    set: impl FnOnce(&mut spaces::Document, usize, Option<&str>) -> bool,
) -> bool {
    // SAFETY: the caller promises the pointer is null or NUL-terminated and
    // valid for this call, which is the contract in the header.
    let value = unsafe { omittable_str(value) };
    let Ok(space_index) = usize::try_from(space_index) else {
        return false;
    };
    cache::shared().apply(|doc| {
        let Some(backing) = mutate::space_at(doc, space_index) else {
            return false;
        };
        set(doc, backing, value)
    })
}

/// Sets a space's own auto-archive TTL rung by sidebar position.
/// Null clears it, and so does an empty string - `omittable_str`.
///
/// # Safety
/// `ttl` must be null or a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_set_space_archive_ttl(
    space_index: i32,
    ttl: *const c_char,
) -> bool {
    // SAFETY: forwarded unchanged; the contract is the helper's.
    unsafe { set_space_field(space_index, ttl, mutate::set_space_archive_ttl) }
}

/// Sets whether a tab group is folded. Explicit state, for the same reason.
///
/// # Safety
/// `group_id` must be null or a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_set_group_collapsed(
    active_space_index: i32,
    group_id: *const c_char,
    collapsed: bool,
) -> bool {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(id) = (unsafe { required_str(group_id) }) else {
        return false;
    };
    if id.is_empty() {
        return false;
    }
    in_active_space(active_space_index, |doc, space| {
        mutate::set_group_collapsed(doc, space, id, collapsed)
    })
}

/// Creates a new, empty space at the end of sidebar order. Returns its id, or
/// null if `name`/`profile_id` is empty or the space cap is reached - the
/// caller frees a non-null result with `atrium_core_string_free`.
///
/// # Safety
/// `name` and `profile_id` must each be null or a valid NUL-terminated C
/// string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_create_space(
    name: *const c_char,
    profile_id: *const c_char,
) -> *mut c_char {
    // SAFETY: the caller promises both pointers are NUL-terminated and valid
    // for the duration of this call, which is the contract in the header.
    let (Some(name), Some(profile_id)) = (unsafe { required_str(name) }, unsafe {
        required_str(profile_id)
    }) else {
        return std::ptr::null_mut();
    };
    if name.is_empty() || profile_id.is_empty() {
        return std::ptr::null_mut();
    }
    json_or_null(cache::shared().apply_value(|doc| mutate::create_space(doc, name, profile_id)))
}

/// Creates a new, empty tab group in the active space. Returns its id, or
/// null if there is none - the caller frees a non-null result with
/// `atrium_core_string_free`.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_create_group(active_space_index: i32) -> *mut c_char {
    json_or_null(in_active_space_value(active_space_index, |doc, space| {
        mutate::create_group(doc, space, "New Folder")
    }))
}

/// Renames a row by setting its `custom_title`. `section` is 1 for the pinned
/// list and 0 for the regular one, matching every other row verb.
///
/// An EMPTY name clears the rename, so a row falls back to the page's own
/// title - the only way back from one.
///
/// # Safety
/// `name` must be null or a valid NUL-terminated C string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_rename_tab(
    active_space_index: i32,
    index: i32,
    section: i32,
    name: *const c_char,
) -> bool {
    let Ok(index) = usize::try_from(index) else {
        return false;
    };
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(name) = (unsafe { required_str(name) }) else {
        return false;
    };
    let name = name.to_string();
    in_active_space(active_space_index, |doc, space| {
        let section = mutate::Section::from_code(section);
        // The regular list addresses by VISIBLE position, as every other row
        // verb here does - a grouped row sits between visible ones.
        let at = match section {
            mutate::Section::Pinned => mutate::pinned_tab_index_of_item(&doc.spaces[space], index),
            mutate::Section::Regular => mutate::persisted_index_of_visible(
                index,
                &doc.spaces[space],
                &doc.hidden_rows,
            ),
        };
        let Some(at) = at else {
            return false;
        };
        mutate::rename_tab(doc, space, section, at, &name)
    })
}

/// Renames a tab group. An unknown id, or an empty name, does nothing.
///
/// # Safety
/// `group_id` and `name` must each be null or a valid NUL-terminated C string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_rename_group(
    active_space_index: i32,
    group_id: *const c_char,
    name: *const c_char,
) -> bool {
    // SAFETY: the caller promises the pointers are NUL-terminated and valid
    // for the duration of this call, which is the contract in the header.
    let Some(id) = (unsafe { required_str(group_id) }) else {
        return false;
    };
    if id.is_empty() {
        return false;
    }
    let Some(name) = (unsafe { required_str(name) }) else {
        return false;
    };
    in_active_space(active_space_index, |doc, space| {
        mutate::rename_group(doc, space, id, name)
    })
}

/// Renames a space by sidebar position. An empty name does nothing.
///
/// # Safety
/// `name` must be null or a valid NUL-terminated C string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_rename_space(space_index: i32, name: *const c_char) -> bool {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(name) = (unsafe { required_str(name) }) else {
        return false;
    };
    let Ok(space_index) = usize::try_from(space_index) else {
        return false;
    };
    cache::shared().apply(|doc| {
        let Some(backing) = mutate::space_at(doc, space_index) else {
            return false;
        };
        mutate::rename_space(doc, backing, name)
    })
}

/// # Safety
/// `icon` must be null or a valid NUL-terminated C string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_set_space_icon(space_index: i32, icon: *const c_char) -> bool {
    // SAFETY: the header's contract, as for atrium_core_rename_space.
    let Some(icon) = (unsafe { required_str(icon) }) else {
        return false;
    };
    let Ok(space_index) = usize::try_from(space_index) else {
        return false;
    };
    cache::shared().apply(|doc| {
        let Some(backing) = mutate::space_at(doc, space_index) else {
            return false;
        };
        mutate::set_space_icon(doc, backing, icon)
    })
}

/// Creates a profile and returns its id, or null if no document is loaded.
/// Caller frees a non-null result with `atrium_core_string_free`.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_add_profile() -> *mut c_char {
    json_or_null(cache::shared().apply_value(mutate::add_profile))
}

fn import_files(files_json: &str) -> Result<std::collections::BTreeMap<String, String>, String> {
    serde_json::from_str(files_json).map_err(|e| format!("the import files could not be read: {e}"))
}

/// Counts what `source`'s files would import. `files_json` is file name to
/// contents, or NULL when the source has none. Free the result with
/// atrium_core_string_free.
/// # Safety
/// `source` is NUL-terminated. `files_json` is null or one.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_import_preview(
    source: *const c_char,
    files_json: *const c_char,
) -> *mut c_char {
    // SAFETY: the caller promises NUL-terminated strings valid for this call.
    let Some(source) = (unsafe { required_str(source) }) else {
        return std::ptr::null_mut();
    };
    // SAFETY: as above.
    let Some(files) = (unsafe { optional_str(files_json) }) else {
        return json_or_null(serde_json::json!({"source": source, "detected": false}).to_string());
    };
    let answer = match import_files(&files).and_then(|f| import::read(source, &f)) {
        Ok(plan) => serde_json::json!({"source": source, "detected": true, "summary": import::summary(&plan)}),
        Err(error) => serde_json::json!({"source": source, "detected": true, "error": error}),
    };
    json_or_null(answer.to_string())
}

/// Reads `source`'s files into the open document and themes. `routing_json`
/// is routing.json now, or "" for none. `engines_json` is the settings
/// search engine list now, or "" to leave engines out. With nothing open,
/// `spaces_path` names where a fresh document opens once the files parse.
/// "" means the document is already open. Answers the report, the routing
/// object to save (null if no rule added) and the engine list to save (null
/// if no engine added). An error changes nothing and leaves a closed core
/// closed.
/// BLOCKING: the caller posts it to a blocking sequence. Free the result
/// with atrium_core_string_free.
/// # Safety
/// All five arguments are NUL-terminated C strings valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_import_run(
    source: *const c_char,
    files_json: *const c_char,
    routing_json: *const c_char,
    engines_json: *const c_char,
    spaces_path: *const c_char,
) -> *mut c_char {
    // SAFETY: the caller promises NUL-terminated strings valid for this call.
    let (Some(source), Some(files), Some(routing_json), Some(engines_json), Some(spaces_path)) = (unsafe {
        (
            required_str(source),
            required_str(files_json),
            omittable_str(routing_json),
            omittable_str(engines_json),
            omittable_str(spaces_path),
        )
    }) else {
        return std::ptr::null_mut();
    };
    let error = |message: String| json_or_null(serde_json::json!({"error": message}).to_string());
    let mut routing = if routing_json.is_empty() {
        small_stores::RoutingFile::default()
    } else {
        match small_stores::load_routing_checked(routing_json) {
            Ok(file) => file,
            Err(_) => return error("routing.json is in a form this build cannot read".into()),
        }
    };
    let mut engines: Vec<search_engines::SearchEngine> = if engines_json.is_empty() {
        Vec::new()
    } else {
        match serde_json::from_str(engines_json) {
            Ok(list) => list,
            Err(_) => return error("the search engine list could not be read".into()),
        }
    };
    // Parsed before the lock: Arc's sidebar file can be many megabytes.
    let mut plan = match import_files(files).and_then(|f| import::read(source, &f)) {
        Ok(plan) => plan,
        Err(message) => return error(message),
    };
    if engines_json.is_empty() {
        plan.engines.clear();
    }
    let fresh = (!spaces_path.is_empty()).then(|| Path::new(spaces_path));
    let report = match cache::shared().apply_import(&plan, &mut routing, &mut engines, fresh) {
        Ok(report) => report,
        Err(message) => return error(message),
    };
    let routing = (report.routes.added > 0).then_some(&routing);
    let engines = (report.engines.added > 0).then_some(&engines);
    json_or_null(
        serde_json::json!({"report": report, "routing": routing, "searchEngines": engines}).to_string(),
    )
}

/// The text inside a mozLz4 file, or null when the bytes do not decode to
/// UTF-8 text without a NUL. `data` is `len` bytes. Free a non-null result
/// with atrium_core_string_free.
/// # Safety
/// `data` must point to `len` readable bytes for the duration of the call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_mozlz4_decode(data: *const c_char, len: usize) -> *mut c_char {
    if data.is_null() {
        return std::ptr::null_mut();
    }
    // SAFETY: the caller promises `len` readable bytes at `data`.
    let bytes = unsafe { std::slice::from_raw_parts(data as *const u8, len) };
    // A panic crossing into C aborts the browser, and another browser wrote these bytes.
    let text = std::panic::catch_unwind(|| import::mozlz4::decode(bytes))
        .ok()
        .flatten()
        .and_then(|decoded| String::from_utf8(decoded).ok());
    json_or_null(text)
}

/// Renames a profile. An unknown id, or an empty name, does nothing.
/// # Safety
/// `profile_id` and `name` must each be null or a valid NUL-terminated C string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_rename_profile(
    profile_id: *const c_char,
    name: *const c_char,
) -> bool {
    // SAFETY: the caller promises both pointers are NUL-terminated and valid
    // for the duration of this call, which is the contract in the header.
    let (Some(id), Some(name)) = (unsafe { required_str(profile_id) }, unsafe {
        required_str(name)
    }) else {
        return false;
    };
    if id.is_empty() {
        return false;
    }
    let name = name.to_string();
    cache::shared().apply(|doc| mutate::rename_profile(doc, id, &name))
}

/// Deletes a profile record. Refused while any space uses it.
/// # Safety
/// `profile_id` must be null or a valid NUL-terminated C string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_delete_profile(profile_id: *const c_char) -> bool {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(id) = (unsafe { required_str(profile_id) }) else {
        return false;
    };
    if id.is_empty() {
        return false;
    }
    cache::shared().apply(|doc| mutate::delete_profile(doc, id))
}

/// Points a space at another profile, by sidebar position.
/// # Safety
/// `profile_id` must be null or a valid NUL-terminated C string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_set_space_profile(
    space_index: i32,
    profile_id: *const c_char,
) -> bool {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(id) = (unsafe { required_str(profile_id) }) else {
        return false;
    };
    let Ok(space_index) = usize::try_from(space_index) else {
        return false;
    };
    cache::shared().apply(|doc| {
        let Some(backing) = mutate::space_at(doc, space_index) else {
            return false;
        };
        mutate::set_space_profile(doc, backing, id)
    })
}

/// A space's theme as `ThemeSpec` JSON, or null when it has none.
/// Free a non-null result with `atrium_core_string_free`.
/// # Safety
/// `space_id` must be a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_space_theme(space_id: *const c_char) -> *mut c_char {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(id) = (unsafe { required_str(space_id) }) else {
        return std::ptr::null_mut();
    };
    json_or_null(
        cache::shared()
            .space_theme(id)
            .and_then(|theme| serde_json::to_string(&theme).ok()),
    )
}

/// Stores a sanitized theme for a space. `theme_json` null removes it and
/// leaves the space's tint as it is. False for an unknown space or
/// unreadable JSON.
/// # Safety
/// `space_id` must be a NUL-terminated C string. `theme_json` must be null or
/// one, valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_set_space_theme(
    space_id: *const c_char,
    theme_json: *const c_char,
) -> bool {
    // SAFETY: the caller promises `space_id` is NUL-terminated and valid, and
    // `theme_json` is either null or NUL-terminated and valid, for this call.
    let Some(id) = (unsafe { required_str(space_id) }) else {
        return false;
    };
    let theme = if theme_json.is_null() {
        None
    } else {
        let Ok(json) = (unsafe { CStr::from_ptr(theme_json) }).to_str() else {
            return false;
        };
        let Ok(spec) = serde_json::from_str::<small_stores::ThemeSpec>(json) else {
            return false;
        };
        // Sanitized in `Core::set_space_theme`, the one place that stores a
        // theme, so a caller reaching it any other way still gets it.
        Some(spec)
    };
    cache::shared().set_space_theme(id, theme)
}

/// Rasterizes a `ThemeSpec` (saved or not) to straight-alpha RGBA8 at
/// `width` x `height`, writing the byte length to `out_len`. `warmth_offset`
/// is what `atrium_core_night_shift_offset` answered, 0 for none.
/// `ambient_time_seconds` < 0 keeps the authored stop positions. Null on bad
/// input. Free a non-null result with `atrium_core_bytes_free`.
/// # Safety
/// `theme_json` must be a NUL-terminated C string valid for this call.
/// `out_len` must be null or point to a writable `size_t` for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_theme_raster(
    theme_json: *const c_char,
    width: i32,
    height: i32,
    dark: bool,
    warmth_offset: f64,
    ambient_time_seconds: f64,
    out_len: *mut usize,
) -> *mut u8 {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(json) = (unsafe { required_str(theme_json) }) else {
        return null_bytes(out_len);
    };
    let Ok(spec) = serde_json::from_str::<small_stores::ThemeSpec>(json) else {
        return null_bytes(out_len);
    };
    // An unchecked width*height reaches `vec![0u8; ...]` and aborts the
    // process on allocation failure, which catch_unwind below cannot catch.
    if !(1..=4096).contains(&width) || !(1..=4096).contains(&height) {
        return null_bytes(out_len);
    }
    let (width, height) = (width as usize, height as usize);
    let ambient_time = (ambient_time_seconds >= 0.0).then_some(ambient_time_seconds);
    let spec = theme_math::sanitized(&spec);
    // A panic crossing into C aborts the browser process, and this loops over
    // numbers a theme JSON from the studio chose - mirrors atrium_core_icon_links.
    match std::panic::catch_unwind(|| {
        theme_math::rasterize(&spec, width, height, dark, warmth_offset, ambient_time)
    }) {
        Ok(bytes) => bytes_or_null(bytes, out_len),
        Err(_) => null_bytes(out_len),
    }
}

/// Frees a buffer `atrium_core_theme_raster` returned. Null is accepted and
/// ignored.
/// # Safety
/// `bytes` must be null, or the exact pointer and `len` a prior
/// `atrium_core_theme_raster` call returned, handed back exactly once.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_bytes_free(bytes: *mut u8, len: usize) {
    if bytes.is_null() {
        return;
    }
    // SAFETY: the caller promises this is `atrium_core_theme_raster`'s own
    // pointer and length, handed back exactly once.
    drop(unsafe { Box::from_raw(std::ptr::slice_from_raw_parts_mut(bytes, len)) });
}

/// `{"accent","dominant","text","hoverFill","activeFill"}` for the
/// appearance. Free a non-null result with `atrium_core_string_free`.
/// # Safety
/// `theme_json` must be a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_theme_tokens(
    theme_json: *const c_char,
    dark: bool,
) -> *mut c_char {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(json) = (unsafe { required_str(theme_json) }) else {
        return std::ptr::null_mut();
    };
    let Ok(spec) = serde_json::from_str::<small_stores::ThemeSpec>(json) else {
        return std::ptr::null_mut();
    };
    let tokens = theme_math::derived_tokens(&theme_math::sanitized(&spec), dark);
    json_or_null(
        serde_json::json!({
            "accent": tokens.accent,
            "dominant": tokens.dominant,
            "text": tokens.text,
            "hoverFill": tokens.hover_fill,
            "activeFill": tokens.active_fill,
        })
        .to_string(),
    )
}

/// The overlay palette for a `#RRGGBB` accent, or for `accent_index` - the
/// live `AppleAccentColor` - when that names one.
/// # Safety
/// `accent_hex` must be a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_overlay_accent_tokens(
    accent_hex: *const c_char,
    accent_index: i32,
) -> *mut c_char {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(hex) = (unsafe { required_str(accent_hex) }) else {
        return std::ptr::null_mut();
    };
    let tokens =
        theme_math::accent_tokens(theme_math::macos_accent_hex(accent_index).unwrap_or(hex));
    json_or_null(
        serde_json::json!({
            "accent": tokens.accent,
            "ink": tokens.ink,
            "paper": tokens.paper,
            "soft": tokens.soft,
            "accentInk": tokens.accent_ink,
        })
        .to_string(),
    )
}

/// Night shift's warmth offset for `local_hour` (0-24).
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_night_shift_offset(local_hour: f64) -> f64 {
    theme_math::night_shift_offset(local_hour)
}

/// A one-stop theme from a tint - a name from `tint.rs` or a `#RRGGBB` hex -
/// what the studio opens with for an unthemed space. Free a non-null result
/// with `atrium_core_string_free`.
/// # Safety
/// `tint` must be a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_single_stop_theme(tint: *const c_char) -> *mut c_char {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(tint) = (unsafe { required_str(tint) }) else {
        return std::ptr::null_mut();
    };
    json_or_null(serde_json::to_string(&theme_math::single_stop_theme(tint)).ok())
}

/// Shared body of the three `atrium_core_set_profile_*` exports.
/// # Safety
/// `profile_id` must be a NUL-terminated C string; `value` must be null or one.
unsafe fn set_profile_field(
    profile_id: *const c_char,
    value: *const c_char,
    set: impl FnOnce(&mut spaces::Document, &str, Option<&str>) -> bool,
) -> bool {
    // SAFETY: forwarded from the export, whose header states both contracts.
    let Some(id) = (unsafe { required_str(profile_id) }) else {
        return false;
    };
    if id.is_empty() {
        return false;
    }
    // SAFETY: as above.
    let value = unsafe { omittable_str(value) };
    cache::shared().apply(|doc| set(doc, id, value))
}

/// Sets a profile's search engine. Null or empty clears it.
/// # Safety
/// `profile_id` must be a valid NUL-terminated C string, `engine` null or one.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_set_profile_search_engine(
    profile_id: *const c_char,
    engine: *const c_char,
) -> bool {
    // SAFETY: forwarded unchanged; the contract is the helper's.
    unsafe { set_profile_field(profile_id, engine, mutate::set_profile_search_engine) }
}

/// Sets a profile's hibernate mode. Null or empty clears it to Follow General.
/// # Safety
/// `profile_id` must be a valid NUL-terminated C string, `mode` null or one.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_set_profile_hibernate(
    profile_id: *const c_char,
    mode: *const c_char,
) -> bool {
    // SAFETY: forwarded unchanged; the contract is the helper's.
    unsafe { set_profile_field(profile_id, mode, mutate::set_profile_hibernate) }
}

/// Sets a profile's own hibernation timeout rung. Null or empty clears it.
/// # Safety
/// `profile_id` must be a valid NUL-terminated C string, `threshold` null or one.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_set_profile_hib_threshold(
    profile_id: *const c_char,
    threshold: *const c_char,
) -> bool {
    // SAFETY: forwarded unchanged; the contract is the helper's.
    unsafe { set_profile_field(profile_id, threshold, mutate::set_profile_hib_threshold) }
}

/// Deletes a tab group. Its tabs rejoin the flat list rather than closing.
///
/// # Safety
/// `group_id` must be null or a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_delete_group(
    active_space_index: i32,
    group_id: *const c_char,
) -> bool {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(id) = (unsafe { required_str(group_id) }) else {
        return false;
    };
    if id.is_empty() {
        return false;
    }
    in_active_space(active_space_index, |doc, space| {
        mutate::delete_group(doc, space, id)
    })
}

/// Sets a group's icon glyph. An empty string clears it.
///
/// # Safety
/// `group_id` and `icon` must each be null or a valid NUL-terminated C string.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_set_group_icon(
    active_space_index: i32,
    group_id: *const c_char,
    icon: *const c_char,
) -> bool {
    // SAFETY: the caller promises the pointers are NUL-terminated and valid
    // for the duration of this call, which is the contract in the header.
    let Some(id) = (unsafe { required_str(group_id) }) else {
        return false;
    };
    if id.is_empty() {
        return false;
    }
    let Some(icon) = (unsafe { required_str(icon) }) else {
        return false;
    };
    in_active_space(active_space_index, |doc, space| {
        mutate::set_group_icon(doc, space, id, icon)
    })
}

/// Records which space is active, by its position in sidebar order. Out of
/// range is ignored rather than clamped - a stale index describes a model that
/// no longer exists.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_set_active_space_index(index: i32) -> bool {
    let Ok(index) = usize::try_from(index) else {
        return false;
    };
    cache::shared().apply(|doc| mutate::set_active_space(doc, index))
}

// --- The strip -------------------------------------------------------------
//
// Opening and closing a tab, which nothing persisted between the Swift cut and
// The document kept rows for tabs that were gone and missed the ones
// that were opened, and the drift grew with every session.
//
// A live tab is joined to its persisted row by a BINDING the core keeps, not by
// URL: a URL is not unique, and two blank tabs would otherwise share one row and
// then delete each other's.

/// Persists a tab the browser just opened in the window's active space.
///
/// `tab_id` is the live Chromium tab id, and the core remembers which row this
/// tab is showing until `atrium_core_close_tab` says otherwise. `to_visible_slot`
/// counts the space's RENDERED rows, and a negative value - or a slot past the
/// last row - appends.
///
/// A row already in this space with the same URL and no live tab on it is TAKEN
/// OVER rather than duplicated: that is a cold placeholder the user clicked, and
/// appending would leave them looking at their tab twice.
///
/// Returns whether the document changed, which is false for a take-over and for
/// a tab that is already bound - the binding is still recorded either way.
///
/// # Safety
/// `url` and `title` must be null or NUL-terminated C strings valid for this
/// call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_open_tab(
    active_space_index: i32,
    tab_id: u64,
    url: *const c_char,
    title: *const c_char,
    to_visible_slot: i32,
) -> bool {
    // SAFETY: the caller promises both pointers are NUL-terminated and valid
    // for the duration of this call, which is the contract in the header.
    let (Some(url), Some(title)) = (unsafe { required_str(url) }, unsafe { required_str(title) })
    else {
        return false;
    };
    let to_slot = usize::try_from(to_visible_slot).ok();
    cache::shared().apply_bound(|doc, bindings| {
        mutate::open_live_tab(
            doc,
            bindings,
            slot(active_space_index),
            tab_id,
            url,
            title,
            to_slot,
        )
    })
}

/// Binds a live tab to the cold row of `url` in the active space, adding no
/// row. For a favourite's tab restored at startup: it is claimed by its tile
/// and opens no row of its own, and without a binding the row it came from
/// stays drawn beside the tile. False when nothing matched or the tab is
/// bound already.
///
/// # Safety
/// `url` must be null or a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_bind_tab_to_row(
    active_space_index: i32,
    tab_id: u64,
    url: *const c_char,
) -> bool {
    // SAFETY: the caller promises a null or NUL-terminated pointer valid for
    // the duration of this call, which is the contract in the header.
    let Some(url) = (unsafe { required_str(url) }) else {
        return false;
    };
    cache::shared()
        .rebind_against(|doc, bindings| {
            mutate::bind_cold_row(doc, bindings, slot(active_space_index), tab_id, url)
        })
        .unwrap_or(false)
}

#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_touch_tab(tab_id: u64, now: f64) -> bool {
    cache::shared().apply_bound(|doc, bindings| mutate::touch_tab(doc, bindings, tab_id, now))
}

// Swift's reference date, matching every C++ ArchiveSweepInstantNow
// duplicate: seconds-since-2001 rather than the Unix epoch mutate.rs expects.
const SWIFT_EPOCH_OFFSET_S: f64 = 978307200.0;

// mutate.rs takes `now` as a parameter to stay clock-free and testable, so
// this caller reads the wall clock instead - a close has no C++-side "now"
// the way the archive sweep does.
fn wall_clock_now() -> spaces::SwiftDate {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs_f64())
        .unwrap_or(0.0)
        - SWIFT_EPOCH_OFFSET_S
}

/// Removes the row a closing tab was showing.
///
/// A PINNED row is kept and reverts to its cold tile - the pin is the user's,
/// not the tab's. A regular row moves to the graveyard (reason
/// `UserClosed`) rather than a cold placeholder, so Restore can bring it back.
///
/// ONLY FOR A TAB THE USER CLOSED. A window tearing down removes every tab in
/// its strip through the same Chromium notification, and routing those through
/// here would empty the space's persisted tab list on every quit. Use
/// `atrium_core_forget_tab` there.
///
/// False covers a tab that was never persisted and the old half of a discard
/// swap, whose row moved to the new id before this call.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_close_tab(tab_id: u64) -> bool {
    let now = wall_clock_now();
    cache::shared().apply_bound(|doc, bindings| mutate::close_live_tab(doc, bindings, tab_id, now))
}

/// Forgets a live tab without touching its row.
///
/// The window-teardown counterpart of `atrium_core_close_tab`: the tabs of a
/// closing window are not tabs the user closed, so their rows stay and render
/// cold next launch. This is what stops the binding map growing for the rest of
/// the session.
///
/// Returns whether there was a binding to drop. The document never changes.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_forget_tab(tab_id: u64) -> bool {
    cache::shared()
        .rebind_against(|_doc, bindings| bindings.release(tab_id).is_some())
        .unwrap_or(false)
}

/// The sidebar's stored width, or 0 when there is nothing to apply.
///
/// The write existed nowhere and the READ existed
/// nowhere either, so a comment in atrium_browser_controller.cc claiming "a
/// launch seeds the stored width" was describing something no code did. A width
/// that persists and is never applied is the same bug from the other end.
///
/// 0 rather than the default, so the caller decides what "nothing stored" means
/// rather than this guessing at a layout constant it does not own.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_sidebar_width() -> i32 {
    cache::shared()
        .sidebar_width()
        .filter(|w| w.is_finite() && *w >= mutate::MIN_SIDEBAR_WIDTH)
        .map(|w| w.min(mutate::MAX_SIDEBAR_WIDTH) as i32)
        .unwrap_or(0)
}

/// Records the sidebar's settled width.
///
/// Called once, when a width drag is RELEASED - not per pointer
/// move, which would rewrite the file on every frame of the gesture. The live
/// half of the drag goes to the host's own SetSidebarWidthLive and touches no
/// document.
///
/// Nothing wrote this before, so a resize lasted only for the session.
///
/// A width below the floor is REFUSED rather than clamped up: 0 means collapsed,
/// which is session state, and storing it as a width would make the next
/// re-expand return to nothing.
///
/// Returns whether the document changed.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_set_sidebar_width(width: i32) -> bool {
    cache::shared().apply(|doc| mutate::set_sidebar_width(doc, f64::from(width)))
}

/// Catches a tab's persisted row up with the url its page navigated to.
///
/// The sibling of `atrium_core_note_tab_title`, and the one that matters more. A
/// row's live tab id is assigned by matching its persisted url against the live
/// strip's url EXACTLY, so a tab that navigated stopped resolving to its own
/// row - and a row with no live id cannot be shown as selected, cannot be
/// closed by id, and its click falls through to the cold-row path, which opens
/// the page a SECOND time. Call it from PrimaryPageChanged, which fires per committed
/// navigation; TitleWasSet is the wrong hook, because a url and a title do not
/// always change together.
///
/// Leaves both titles alone, and refuses an empty url.
///
/// Returns whether the document changed.
///
/// # Safety
/// `url` must be null or a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_note_tab_url(tab_id: u64, url: *const c_char) -> bool {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(url) = (unsafe { required_str(url) }) else {
        return false;
    };
    cache::shared().apply_bound(|doc, bindings| mutate::note_page_url(doc, bindings, tab_id, url))
}

/// Catches a tab's persisted row up with the title its page now has.
///
/// A tab is added before its page has a title, so the row `atrium_core_open_tab`
/// wrote lands blank and stays blank: the sidebar model reads titles off the
/// ROW, and the live strip it merges carries only urls and ids, so re-pushing
/// the surface redraws the same empty string. Call this from
/// TitleWasSet, which is where a title actually arrives.
///
/// This is NOT a rename. It writes the page's title, and a row the user renamed
/// keeps showing that name - `atrium_core_rename_tab` owns the user's own.
///
/// Returns whether the document changed, so the same title arriving twice does
/// not rewrite the file.
///
/// # Safety
/// `title` must be null or a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_note_tab_title(tab_id: u64, title: *const c_char) -> bool {
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call, which is the contract in the header.
    let Some(title) = (unsafe { required_str(title) }) else {
        return false;
    };
    cache::shared()
        .apply_bound(|doc, bindings| mutate::note_page_title(doc, bindings, tab_id, title))
}

/// Carries a tab's persisted row from `old_tab_id` to `new_tab_id`.
///
/// A discard or a prerender activation replaces a tab's `WebContents`, so the
/// same tab arrives with a new id. Chromium then sends the ordinary close and
/// add pair, which without this reads as "that tab is gone" followed by "here
/// is a new one" - and the row is deleted and re-appended, losing where it sat
/// and everything the document knew about it.
///
/// Call it BEFORE the close and add. Afterwards the old id holds nothing, so
/// both halves are no-ops on their own.
///
/// Returns whether a row was carried. The document does not change, so this
/// never needs a save.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_note_tab_replaced(old_tab_id: u64, new_tab_id: u64) -> bool {
    // `rebind_against` rather than `apply_bound`: nothing about the document
    // moved, and marking it dirty would rewrite the file for a swap the user
    // never made. The helper has no document to dirty, so that cannot regress.
    cache::shared()
        .rebind_against(|_doc, bindings| bindings.rekey(old_tab_id, new_tab_id))
        .unwrap_or(false)
}

/// Records a visit on `tab_id`'s timeline.
///
/// `at_ms` is milliseconds since the epoch, supplied by the caller because the
/// core reads no clock. Returns whether this added a STOP: a late title for the
/// url already at the end lands but is not a new stop, and the caller uses that
/// to decide whether the scrubber needs a re-push.
///
/// The timeline is in memory and per tab. It is NOT the back stack and does not
/// prune on a branch, which is the point - Chromium drops the forward entries
/// when a user navigates away from a mid-history point, and those are exactly
/// the pages the user still remembers.
///
/// # Safety
/// `url` and `title` must be null or NUL-terminated C strings valid for this
/// call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_record_visit(
    tab_id: u64,
    url: *const c_char,
    title: *const c_char,
    at_ms: f64,
) -> bool {
    // SAFETY: the caller promises NUL-terminated pointers valid for this call,
    // which is the contract in the header.
    let Some(url) = (unsafe { required_str(url) }) else {
        return false;
    };
    // An untitled visit is ordinary - a page commits before its title arrives -
    // so the title is omittable while the url is not.
    let Some(title) = (unsafe { omittable_str(title) }) else {
        return false;
    };
    visits::record(tab_id, url, title, at_ms)
}

/// One tab's timeline as a JSON array, oldest first:
/// `[{"url","title","at"}, ...]`. An empty array for a tab that has not
/// navigated. Free a non-null result with `atrium_core_string_free`.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_visits_for_tab(tab_id: u64) -> *mut c_char {
    json_or_null(serde_json::to_string(&visits::visits(tab_id)).ok())
}

/// Drops a tab's timeline. Call it when the tab closes - this is what stops the
/// map growing for the rest of the session.
///
/// Returns whether there was one to drop.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_forget_visits(tab_id: u64) -> bool {
    visits::forget(tab_id)
}

/// Sentinels since `Option<f64>` can't cross the ABI: -2.0 unknown/no document, -1.0 hibernation off, >=0.0 threshold seconds.
///
/// # Safety
/// `memory_saver_threshold` must be null or a NUL-terminated C string valid
/// for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_hibernate_policy_for_tab(
    tab_id: u64,
    memory_saver_enabled: bool,
    memory_saver_threshold: *const c_char,
) -> f64 {
    const UNKNOWN: f64 = -2.0;
    const NEVER: f64 = -1.0;
    // SAFETY: the caller promises a null or NUL-terminated pointer valid for
    // this call, which is the contract in the header.
    let Some(threshold) = (unsafe { required_str(memory_saver_threshold) }) else {
        return UNKNOWN;
    };
    // A pure read, so `peek_bound` rather than a mutating apply. A tab with no
    // binding, or a binding into no space, stays UNKNOWN - which is not NEVER:
    // "we cannot say" must not read as "never hibernate this".
    cache::shared()
        .peek_bound(|doc, bindings| {
            bindings
                .token_of(tab_id)
                .and_then(|token| lifecycle::space_for_token(doc, token))
                .map_or(UNKNOWN, |space| {
                    lifecycle::hibernate_policy(
                        lifecycle::profile_for_space(doc, space),
                        memory_saver_enabled,
                        threshold,
                    )
                    .unwrap_or(NEVER)
                })
        })
        .unwrap_or(UNKNOWN)
}

/// Seconds a closed favourite's page stays loaded, or -1.0 when the keep-alive
/// is off. `settings_json` is a whole settings object. One this build cannot
/// read answers with the defaults, the same as the settings read.
///
/// # Safety
/// `settings_json` must be null or a NUL-terminated C string valid for this
/// call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_favorite_keep_alive_seconds(
    settings_json: *const c_char,
) -> f64 {
    const OFF: f64 = -1.0;
    // SAFETY: the caller promises a null or NUL-terminated pointer valid for
    // this call, which is the contract in the header.
    let Some(json) = (unsafe { required_str(settings_json) }) else {
        return OFF;
    };
    let loaded = settings::load(json);
    if !loaded.favorite_keep_alive_enabled {
        return OFF;
    }
    lifecycle::favorite_keep_alive_threshold_seconds(&loaded.favorite_keep_alive_threshold)
        .unwrap_or(OFF)
}

/// What this launch has to load, as JSON:
/// `{"requiredProfiles":[...],"startupProfiles":[...],
///   "startupTab":{"profileID":"...","url":"..."}}`.
///
/// `startup_behavior` is settings.json's Startup row verbatim -
/// `"restoreLastActive"` or `"blankTab"`. It lives in another file than the
/// document, so it is a parameter rather than something the core remembers.
/// `"blankTab"` shows `about:blank` whatever the document would have restored;
/// null, empty, or any value this build does not know restores, which is what
/// the settings loader normalises an unreadable row to.
///
/// Returns null when no document is open, which is not an empty plan - the
/// caller falls back to a single Default profile rather than starting with no
/// window. The caller frees a non-null result with `atrium_core_string_free`.
///
/// # Safety
/// `startup_behavior` must be null or a NUL-terminated C string valid for this
/// call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_launch_plan(startup_behavior: *const c_char) -> *mut c_char {
    // SAFETY: the caller promises a null or NUL-terminated pointer valid for
    // this call, which is the contract in the header.
    let behavior = unsafe { omittable_str(startup_behavior) }.unwrap_or_default();
    let Some(plan) =
        cache::shared().launch_plan(settings::StartupBehavior::from_settings(behavior))
    else {
        return std::ptr::null_mut();
    };
    json_or_null(plan)
}

/// Puts a whole window - or one space slice - back to a state Rewind saved.
///
/// `payload` is the JSON Rewind's `restoreSnapshot` computes:
/// `{"scope":"all"|"<space index>","activeSpace":n,"spaces":[{"index":n,
///   "name":"...","pinnedCollapsed":bool,"pinned":[row],"tabs":[row],
///   "folders":[{"id","name","closed","tabs":[row]}],"favorites":[row]}]}`,
/// where a row is `{"id":"<live tab id or empty>","url","title","selected"}`.
///
/// The slice is taken WHOLE rather than replayed as gestures. Every other
/// sidebar verb moves one row, and order, selection and folder membership have
/// no verb at all - a replay would be long, racy and lossy.
///
/// VALIDATED FIRST: a payload naming a space that does not exist, contradicting
/// its own scope, or carrying a row with no URL is refused outright and the
/// document is left exactly as it was. A half-applied restore would leave the
/// window in a state that was never a moment.
///
/// Returns the JSON of what the BROWSER still has to do, which the document
/// cannot say:
/// `{"reopen":[{"space","section","slot","url","title","groupID","selected"}],
///   "closeTabIds":[id],"selectTabId":id|null,"activeSpace":n}`.
/// A reopened row was written COLD by this call, so opening it adopts that row
/// rather than adding a second one.
///
/// SELECTION ARRIVES ONE OF TWO WAYS, never both. `selectTabId` is a tab that
/// is open right now; it is null when the selected row has to be reopened,
/// because Chromium never reuses a tab id. Then exactly one reopen entry
/// carries `"selected":true` and the browser activates the tab it opens for it.
///
/// Returns null when the payload does not parse or is refused - which is the
/// signal that nothing changed. The caller frees a non-null result with
/// `atrium_core_string_free`.
///
/// # Safety
/// `payload` must be null or a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_restore_slice(payload: *const c_char) -> *mut c_char {
    // SAFETY: the caller promises a null or NUL-terminated pointer valid for
    // this call, which is the contract in the header.
    let Some(json) = (unsafe { required_str(payload) }) else {
        return std::ptr::null_mut();
    };
    let Some(parsed) = restore::parse(json) else {
        return std::ptr::null_mut();
    };
    let plan = cache::shared()
        .apply_bound_value(|doc, bindings| restore::apply(doc, bindings, &parsed).ok());
    json_or_null(plan.and_then(|plan| serde_json::to_string(&plan).ok()))
}

/// Writes the document to the file it was opened from.
///
/// BLOCKING - it fsyncs. Post it to a thread that is allowed to block, and call
/// it synchronously on shutdown so the last gesture is not lost.
///
/// Returns 1 when a file was written, 0 when there was nothing to write
/// (nothing open, or no mutation since the last save), -1 when the spaces.json
/// write failed and -2 when only the themes.json write failed. Either failure
/// is worth a log line, because the user's last gesture is only in memory.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_save() -> i32 {
    match cache::shared().save_by_store() {
        Ok(true) => 1,
        Ok(false) => 0,
        Err(failure) if failure.spaces => -1,
        Err(_) => -2,
    }
}

/// Writes `settings_json` to the settings store at `settings_path`.
///
/// BLOCKING - it fsyncs. Post it to a thread that is allowed to block.
///
/// The path is a parameter and no document is kept, because that is how the
/// settings READ works: `atrium_core_query` takes the file's contents on every
/// call and the caller owns the path (`SettingsFilePath`, which honours the
/// override a scenario stages). A remembered path here would answer a question
/// the read half never asks.
///
/// `settings_json` is a whole settings object in the shape `query` returns for
/// `Store::Settings`. It goes back through this build's own loader rather than
/// being trusted: an opacity outside the legal range or an enum value that means
/// nothing here is normalised on the way in, so a surface cannot write a value
/// the reader would then quietly replace.
///
/// A PARTIAL object is refused rather than completed. Every field past the first
/// four is optional (`settings::FileShape`), so a missing key and a default are
/// the same bytes, and filling them in would reset every field the caller did
/// not send.
///
/// Returns 1 when the file was written, 0 when the arguments were not usable -
/// a null or empty path, or a payload this build cannot read - -1 when the write
/// failed, and -2 when the write was DECLINED because the file ON DISK is one
/// this build cannot read (`docs/model-rules.md` R1.2). The last two are kept
/// apart because a failed write is worth retrying and a decline is not: the
/// file stays unwritable until it is moved aside.
///
/// # Safety
/// `settings_path` and `settings_json` must be non-null, NUL-terminated C
/// strings that stay valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_save_settings(
    settings_path: *const c_char,
    settings_json: *const c_char,
) -> i32 {
    if settings_path.is_null() || settings_json.is_null() {
        return 0;
    }
    // SAFETY: the caller promises both pointers are NUL-terminated and valid
    // for the duration of this call, which is the contract in the header.
    let (path, json) = unsafe { (CStr::from_ptr(settings_path), CStr::from_ptr(settings_json)) };
    let (Ok(path), Ok(json)) = (path.to_str(), json.to_str()) else {
        return 0;
    };
    if path.is_empty() {
        return 0;
    }
    let Ok(settings) = settings::load_checked(json) else {
        return 0;
    };
    match persist::save_settings(&settings, Path::new(path)) {
        // `save_settings` now says whether its `.bak` failed
        // (`persist::BackupOutcome`), but this ABI predates that and its
        // return codes do not carry the distinction yet - a caller here
        // gets 1 whether the backup landed or not.
        Ok(_) => 1,
        Err(schema::StoreError::Io(_)) => -1,
        // Refused and corrupt are one code here, and that is this ABI's
        // limit rather than a lost distinction: the enum keeps them apart, so
        // a caller that needs to tell them apart calls the Rust side.
        Err(schema::StoreError::Schema(_) | schema::StoreError::Encode(_)) => -2,
    }
}

/// What `atrium_core_save_settings` would answer for the file at
/// `settings_path`, asked without writing: 1, 0, -1 or -2 as there. An import
/// asks first, so a declined file costs its engines rather than leaving a
/// profile that names an engine settings.json never got.
///
/// # Safety
/// `settings_path` must be null or a NUL-terminated C string valid for this
/// call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_settings_writable(settings_path: *const c_char) -> i32 {
    // SAFETY: the caller promises a null or NUL-terminated pointer valid for
    // this call, which is the contract in the header.
    let Some(path) = (unsafe { required_str(settings_path) }) else {
        return 0;
    };
    if path.is_empty() {
        return 0;
    }
    match persist::refuse_unless_this_build_can_read_it(Path::new(path)) {
        Ok(_) => 1,
        Err(schema::StoreError::Io(_)) => -1,
        Err(_) => -2,
    }
}

/// Writes `routing_json` to the routing store at `routing_path`. Same shape as
/// `atrium_core_save_settings`: re-read through the core's own loader before
/// anything is written (R1.2), whole-object-in whole-object-out, same return
/// codes (1 written, 0 unusable arguments, -1 write failed, -2 declined).
///
/// # Safety
/// `routing_path` and `routing_json` must be non-null, NUL-terminated C
/// strings that stay valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_save_routing(
    routing_path: *const c_char,
    routing_json: *const c_char,
) -> i32 {
    if routing_path.is_null() || routing_json.is_null() {
        return 0;
    }
    // SAFETY: the caller promises both pointers are NUL-terminated and valid
    // for the duration of this call, which is the contract in the header.
    let (path, json) = unsafe { (CStr::from_ptr(routing_path), CStr::from_ptr(routing_json)) };
    let (Ok(path), Ok(json)) = (path.to_str(), json.to_str()) else {
        return 0;
    };
    if path.is_empty() {
        return 0;
    }
    let Ok(file) = small_stores::load_routing_checked(json) else {
        return 0;
    };
    match persist::save_routing(&file, Path::new(path)) {
        Ok(_) => 1,
        Err(schema::StoreError::Io(_)) => -1,
        Err(schema::StoreError::Schema(_) | schema::StoreError::Encode(_)) => -2,
    }
}

/// Writes `hotkeys_json` to the hotkeys store at `hotkeys_path`. Same shape
/// as `atrium_core_save_routing`.
///
/// # Safety
/// `hotkeys_path` and `hotkeys_json` must be non-null, NUL-terminated C
/// strings that stay valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_save_hotkeys(
    hotkeys_path: *const c_char,
    hotkeys_json: *const c_char,
) -> i32 {
    if hotkeys_path.is_null() || hotkeys_json.is_null() {
        return 0;
    }
    // SAFETY: the caller promises both pointers are NUL-terminated and valid
    // for the duration of this call, which is the contract in the header.
    let (path, json) = unsafe { (CStr::from_ptr(hotkeys_path), CStr::from_ptr(hotkeys_json)) };
    let (Ok(path), Ok(json)) = (path.to_str(), json.to_str()) else {
        return 0;
    };
    if path.is_empty() {
        return 0;
    }
    let Ok(file) = small_stores::load_hotkeys_checked(json) else {
        return 0;
    };
    match persist::save_hotkeys(&file, Path::new(path)) {
        Ok(_) => 1,
        Err(schema::StoreError::Io(_)) => -1,
        Err(schema::StoreError::Schema(_) | schema::StoreError::Encode(_)) => -2,
    }
}

/// The request-time SSRF gate.
///
/// # Safety
/// `ip` must be null, or a NUL-terminated C string valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_resolved_ip_is_local(ip: *const c_char) -> bool {
    // SAFETY: the caller promises the pointer is either null or valid for the
    // duration of this call, which is the contract in the header.
    let Some(ip) = (unsafe { required_str(ip) }) else {
        return true;
    };
    settings::resolved_ip_is_local(ip)
}

/// Whether Atrium claims this key press before the first responder sees it.
///
/// False means the press carries on to the renderer and the main menu, which is
/// also the answer for a null pointer: letting a key through is recoverable and
/// claiming one Atrium then cannot run is not.
///
/// # Safety
/// `input` must be null, or point to an `AtriumCommandInterceptInputs` that
/// stays valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_will_claim_command(
    input: *const commands::CommandInterceptInputs,
) -> bool {
    // SAFETY: the caller promises the pointer is either null or valid for the
    // duration of this call, which is the contract in the header.
    let Some(input) = (unsafe { input.as_ref() }) else {
        return false;
    };
    commands::will_claim_command(input)
}

/// Which intercept runs for a command Atrium has claimed, as one of the
/// `ATRIUM_COMMAND_INTERCEPT_*` values.
///
/// A null pointer answers `NONE`, which dispatches the command normally.
///
/// # Safety
/// `input` must be null, or point to an `AtriumCommandInterceptInputs` that
/// stays valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_command_intercept(
    input: *const commands::CommandInterceptInputs,
) -> i32 {
    // SAFETY: the caller promises the pointer is either null or valid for the
    // duration of this call, which is the contract in the header.
    let Some(input) = (unsafe { input.as_ref() }) else {
        return commands::CommandIntercept::None as i32;
    };
    commands::intercept_for(input) as i32
}

/// Whether a command acts on the tab the user can SEE rather than on the
/// invoking window's own tab.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_command_acts_on_visible_tab(command: i32) -> bool {
    commands::command_acts_on_visible_tab(command)
}

/// The process-wide Ctrl-Tab recency list. One instance, the same scope
/// `cache::shared()`'s loaded document carries - see `tab_recency`'s module
/// doc for why that is enough for this milestone.
fn recency_list() -> &'static std::sync::Mutex<tab_recency::TabRecencyList> {
    static SHARED: std::sync::OnceLock<std::sync::Mutex<tab_recency::TabRecencyList>> =
        std::sync::OnceLock::new();
    SHARED.get_or_init(|| std::sync::Mutex::new(tab_recency::TabRecencyList::new()))
}

/// Records `tab_id` as the newly active tab for Ctrl-Tab recency purposes.
///
/// Call this from the SAME place that already observes tab activation
/// (`BrowserController::OnTabStripModelChanged`) for every activation that
/// is not this module's own cycle stepping - a no-op while a cycle is held,
/// so the walk does not perturb itself; see `tab_recency` for why.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_tab_recency_record_activation(tab_id: u64) {
    recency_list()
        .lock()
        .unwrap_or_else(|e| e.into_inner())
        .record_activation(tab_id);
}

/// Drops `tab_id` from the recency list and from any cycle in progress.
/// Call this from wherever a tab's closure is already observed, so a closed
/// tab is never walked to.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_tab_recency_record_closed(tab_id: u64) {
    recency_list()
        .lock()
        .unwrap_or_else(|e| e.into_inner())
        .record_closed(tab_id);
}

/// Advances the held Ctrl-Tab cycle one step, opening one first if none is
/// in progress. `direction` is +1 for Ctrl-Tab, -1 for Ctrl-Shift-Tab.
///
/// Returns the tab id to show, or -1 when there is nowhere to go (no other
/// tabs to cycle to).
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_tab_recency_step(direction: i32) -> i64 {
    recency_list()
        .lock()
        .unwrap_or_else(|e| e.into_inner())
        .step_cycle(direction)
        .map(|id| id as i64)
        .unwrap_or(-1)
}

/// Ends the held cycle, committing the tab it currently sits on to the
/// front of the recency list. Call this on the modifier key's release - see
/// `tab_recency` for why that boundary was chosen over a timeout or a
/// second key.
#[unsafe(no_mangle)]
pub extern "C" fn atrium_core_tab_recency_end_cycle() {
    recency_list()
        .lock()
        .unwrap_or_else(|e| e.into_inner())
        .end_cycle();
}

/// Where a link another macOS app handed over opens, as an
/// `ATRIUM_EXTERNAL_LINK_TARGET_*` value.
///
/// A null pointer answers `SPACE_WINDOW`, which is what the browser does
/// today: a caller that filled in nothing must not be handed a window type on
/// the strength of that.
///
/// # Safety
/// `input` must be null, or point to an `AtriumExternalLinkInputs` that stays
/// valid for this call.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_external_link_target(
    input: *const navigation::ExternalLinkInputs,
) -> i32 {
    // SAFETY: the caller promises the pointer is either null or valid for the
    // duration of this call, which is the contract in the header.
    let Some(input) = (unsafe { input.as_ref() }) else {
        return navigation::ExternalLinkTarget::SpaceWindow as i32;
    };
    navigation::decide_external_link_target(input) as i32
}

/// Borrows a C string as UTF-8, refusing a null pointer. `None` means the
/// export must refuse: either the caller left a required argument unset, or the
/// bytes are not UTF-8. This is the one to reach for, and `omittable_str` is
/// the exception - see the module doc for why.
///
/// # Safety
/// `s` must be null or a NUL-terminated C string valid for this call.
unsafe fn required_str<'a>(s: *const c_char) -> Option<&'a str> {
    if s.is_null() {
        return None;
    }
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call.
    unsafe { CStr::from_ptr(s) }.to_str().ok()
}

/// Borrows a C string as UTF-8, reading a null pointer as `""` ON PURPOSE.
/// `None` means the bytes are not UTF-8 and the export must still refuse.
///
/// Only for an argument whose absence is a real state, and only where the
/// mutation already collapses `""` to that state - so null and empty cannot
/// diverge later. Anything else takes `required_str`.
///
/// # Safety
/// `s` must be null or a NUL-terminated C string valid for this call.
unsafe fn omittable_str<'a>(s: *const c_char) -> Option<&'a str> {
    if s.is_null() {
        return Some("");
    }
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call.
    unsafe { CStr::from_ptr(s) }.to_str().ok()
}

/// Borrows a C string as UTF-8, reading a null pointer as absent rather than
/// refusing the call. `None` also covers non-UTF-8 bytes.
///
/// For an argument that is genuinely optional at the call site, unlike
/// `required_str`'s refusal and `omittable_str`'s null-as-empty.
///
/// # Safety
/// `s` must be null or a NUL-terminated C string valid for this call.
unsafe fn optional_str(s: *const c_char) -> Option<String> {
    if s.is_null() {
        return None;
    }
    // SAFETY: the caller promises the pointer is NUL-terminated and valid for
    // the duration of this call.
    unsafe { CStr::from_ptr(s) }
        .to_str()
        .ok()
        .map(str::to_owned)
}

/// Frees a string this library returned. Null is accepted and ignored.
///
/// # Safety
/// `s` must have come from this library and be handed back exactly once.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn atrium_core_string_free(s: *mut c_char) {
    if s.is_null() {
        return;
    }
    // SAFETY: the caller promises this came from into_raw in this library and
    // is being handed back once.
    drop(unsafe { CString::from_raw(s) });
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_temp_dir::TempDir;

    /// Two tests opening/closing the shared process-wide cache at once raced
    /// its document into a segfault - hold this for the duration of any test
    /// that calls atrium_core_open.
    fn abi_serial() -> std::sync::MutexGuard<'static, ()> {
        static LOCK: std::sync::Mutex<()> = std::sync::Mutex::new(());
        LOCK.lock().unwrap_or_else(|e| e.into_inner())
    }

    /// # Safety
    /// `raw` must be null or a string this library returned and has not yet
    /// freed.
    unsafe fn take_string(raw: *mut c_char) -> Option<String> {
        if raw.is_null() {
            return None;
        }
        // SAFETY: the caller promises the pointer came from this library.
        let owned = unsafe { CStr::from_ptr(raw) }
            .to_str()
            .expect("the ABI returns UTF-8")
            .to_string();
        unsafe { atrium_core_string_free(raw) };
        Some(owned)
    }

    #[test]
    fn the_exported_function_returns_the_abi_version() {
        assert_eq!(atrium_core_abi_version(), ABI_VERSION);
    }

    /// The header carries the same number as a `#define`. Read it here so a
    /// bump on one side and not the other fails `cargo test` as well as the
    /// C++ test, and fails it without a Chromium checkout.
    #[test]
    fn the_header_define_matches_the_constant() {
        let header = include_str!("../include/atrium_core.h");
        let line = header
            .lines()
            .find(|l| l.starts_with("#define ATRIUM_CORE_ABI_VERSION"))
            .expect("the header must define ATRIUM_CORE_ABI_VERSION");
        let value: u32 = line
            .split_whitespace()
            .nth(2)
            .expect("the define must have a value")
            .trim_end_matches('u') // the define is unsigned: `1u`
            .parse()
            .expect("the value must be a number");
        assert_eq!(value, ABI_VERSION, "header define drifted from ABI_VERSION");
    }

    #[test]
    fn every_store_name_in_the_corpus_resolves() {
        for name in [
            "spaces",
            "settings",
            "themes",
            "hotkeys",
            "routing",
            "command-bar-selection-memory",
            "hotkey-action-ids",
            "hotkeys-edit",
            "hotkey-presets",
        ] {
            assert!(Store::from_name(name).is_some(), "{name}");
        }
        assert!(Store::from_name("nope").is_none());
    }

    // C++ reads this list over the ABI rather than keeping a second copy, so
    // the round trip itself needs pinning.
    #[test]
    fn hotkey_action_ids_lists_every_core_action() {
        let out = query(Store::HotkeyActionIds, "", 0.0).unwrap();
        let ids: Vec<String> = serde_json::from_str(&out).unwrap();
        assert_eq!(ids, small_stores::HOTKEY_ACTIONS);
    }

    // The Inputs pane's reset reaches this side through the query ABI, so the
    // round trip is what C++ actually calls.
    #[test]
    fn a_hotkeys_edit_query_drops_the_action_it_names() {
        let out = query(
            Store::HotkeysEdit,
            r#"{"schemaVersion":2,"actions":{"reload":["cmd+shift+r"]},"forget":"reload"}"#,
            0.0,
        )
        .unwrap();
        assert_eq!(out, r#"{"schemaVersion":2,"actions":{}}"#);
    }

    /// The pane offers a preset it can then apply from the same push, so the
    /// two names it draws buttons for have to arrive with their tables.
    #[test]
    fn the_preset_query_carries_every_table_the_pane_can_pick() {
        let out = query(Store::HotkeyPresets, "", 0.0).unwrap();
        let tables: serde_json::Value = serde_json::from_str(&out).unwrap();

        assert!(tables["arc"]["toggleSidebar"] == serde_json::json!(["cmd+s"]));
        assert!(tables["arc"]["openCommandBar"] == serde_json::json!([]));
        assert!(tables["atrium"]["openCommandBar"] == serde_json::json!(["cmd+t"]));
    }

    /// A file naming a preset has to come back naming it, or the pane cannot
    /// tell which table its rows are being compared against.
    #[test]
    fn a_hotkeys_query_answers_with_the_preset_the_file_named() {
        let out = query(
            Store::Hotkeys,
            r#"{"schemaVersion":2,"preset":"arc","actions":{}}"#,
            0.0,
        )
        .unwrap();
        let file: serde_json::Value = serde_json::from_str(&out).unwrap();

        assert_eq!(file["preset"], "arc");
        assert_eq!(
            file["actions"]["nextTab"],
            serde_json::json!(["cmd+shift+]"])
        );
    }

    /// A refused payload has to travel as a refusal, or C++ writes an empty
    /// map over a file this build was told not to touch.
    #[test]
    fn a_hotkeys_edit_at_an_unreadable_schema_is_refused() {
        assert!(
            query(
                Store::HotkeysEdit,
                r#"{"schemaVersion":99,"actions":{}}"#,
                0.0
            )
            .is_err()
        );
    }

    /// The version number only guards signatures that both sides already
    /// declare. A function exported here and absent from the header is
    /// invisible to C++; one declared there and absent here is a link error at
    /// best and a call into nothing at worst.
    #[test]
    fn the_header_declares_exactly_the_exported_functions() {
        let source = include_str!("lib.rs");
        let mut exported: Vec<&str> = source
            .lines()
            .filter_map(|l| l.trim().strip_prefix("pub "))
            .filter_map(|l| {
                l.strip_prefix("unsafe extern \"C\" fn ")
                    .or_else(|| l.strip_prefix("extern \"C\" fn "))
            })
            .filter_map(|l| l.split('(').next())
            .collect();
        exported.sort_unstable();

        let header = include_str!("../include/atrium_core.h");
        let mut declared: Vec<&str> = header
            .lines()
            .filter(|l| !l.trim_start().starts_with("//"))
            .filter_map(|l| l.find("atrium_core_").map(|at| &l[at..]))
            .filter(|rest| rest.contains('('))
            .filter_map(|rest| rest.split('(').next())
            .collect();
        declared.sort_unstable();
        declared.dedup();

        // Two empty lists would compare equal and prove nothing.
        assert!(exported.len() >= 8, "parsed too few exports: {exported:?}");
        assert_eq!(
            exported, declared,
            "the header and the exports disagree about which functions exist"
        );
    }

    /// The pair as C++ calls it: open, then a snapshot with no path in sight.
    #[test]
    fn the_cached_pair_works_across_the_c_boundary() {
        let _guard = abi_serial();
        // Written here rather than read from fixtures/: GN builds these unit
        // tests inside the Chromium checkout, which has no fixtures directory
        // and no CARGO_MANIFEST_DIR. tests/cache.rs covers the corpus document.
        let dir = TempDir::new("abi");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            // r## because the tint contains `"#`, which would close an r#" here.
            r##"{"spaces":[{"id":"a","name":"Work","emoji":"W","tint":"#111111",
            "profileID":"p","pinnedTabs":[],"tabs":[{"url":"https://example.com/",
            "title":"One"}],"order":0,"groups":[]}],
            "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
            "sidebarWidthMigrated":true}}"##,
        )
        .unwrap();

        let live = CString::new(r#"[{"url":"https://example.com/","id":7}]"#).unwrap();
        // Nothing open yet: null, so the caller falls back rather than
        // rendering an empty sidebar.
        atrium_core_close();
        assert!(unsafe { atrium_core_sidebar_model_cached(-1, live.as_ptr()) }.is_null());
        assert!(!atrium_core_is_open());

        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });
        assert!(atrium_core_is_open());
        assert!(atrium_core_active_space_index() >= 0);

        let out = unsafe { atrium_core_sidebar_model_cached(-1, live.as_ptr()) };
        let json = unsafe { take_string(out) }.expect("an open core must produce a model");
        assert!(json.starts_with('{'), "{json}");

        atrium_core_close();
        assert!(!atrium_core_is_open());
    }

    /// two spaces' notes never share a record, and a
    /// space with no saved note yet reads back as an empty placeholder.
    #[test]
    fn a_space_note_round_trips_and_stays_scoped_to_its_own_space() {
        let _guard = abi_serial();
        let dir = TempDir::new("note-abi");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[
                {"id":"a","name":"Work","emoji":"W","tint":"#111111",
                 "profileID":"p","pinnedTabs":[],"tabs":[],"order":0,"groups":[]},
                {"id":"b","name":"Home","emoji":"H","tint":"#222222",
                 "profileID":"p","pinnedTabs":[],"tabs":[],"order":1,"groups":[]}
            ],
            "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
            "sidebarWidthMigrated":true}}"##,
        )
        .unwrap();

        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        // Space b has never been written: an empty placeholder, not null.
        let raw = atrium_core_get_space_note(1);
        let empty_note = unsafe { take_string(raw) }.expect("a placeholder note, not null");
        assert!(empty_note.contains(r#""body":"""#), "{empty_note}");

        // Writing space a's note must not touch space b's.
        let body = CString::new("Space A jotting").unwrap();
        assert!(unsafe { atrium_core_set_space_note(0, body.as_ptr(), 100.0) });

        let raw_a = atrium_core_get_space_note(0);
        let json_a = unsafe { take_string(raw_a) }.unwrap();
        assert!(json_a.contains("Space A jotting"), "{json_a}");

        let raw_b = atrium_core_get_space_note(1);
        let json_b = unsafe { take_string(raw_b) }.unwrap();
        assert!(
            !json_b.contains("Space A jotting"),
            "space b's note leaked space a's body: {json_b}"
        );
        assert!(json_b.contains(r#""body":"""#), "{json_b}");

        atrium_core_close();
    }

    /// The two null contracts, one export each, and the point is the
    /// CONTRAST: the same null that is refused by a note is accepted by a TTL.
    #[test]
    fn a_null_is_refused_where_it_would_delete_and_accepted_where_absent_is_a_state() {
        let _guard = abi_serial();
        let dir = TempDir::new("null-abi");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[
                {"id":"a","name":"Work","emoji":"W","tint":"#111111",
                 "profileID":"p","pinnedTabs":[],"tabs":[],"order":0,"groups":[]}
            ],
            "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
            "sidebarWidthMigrated":true}}"##,
        )
        .unwrap();

        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        let body_of_space_a = || {
            let raw = atrium_core_get_space_note(0);
            unsafe { take_string(raw) }.expect("space a has a note")
        };

        // required_str, the data-losing case. A note goes in by hand.
        let body = CString::new("do not lose me").unwrap();
        assert!(unsafe { atrium_core_set_space_note(0, body.as_ptr(), 100.0) });
        assert!(body_of_space_a().contains("do not lose me"));

        // A null is REFUSED, and the note is still there afterwards.
        assert!(
            !unsafe { atrium_core_set_space_note(0, std::ptr::null(), 200.0) },
            "a null body must be refused, not read as an empty one"
        );
        assert!(
            body_of_space_a().contains("do not lose me"),
            "a refused call must leave the note alone: {}",
            body_of_space_a()
        );

        // Clearing is still reachable, by saying so.
        let empty = CString::new("").unwrap();
        assert!(unsafe { atrium_core_set_space_note(0, empty.as_ptr(), 300.0) });
        assert!(
            body_of_space_a().contains(r#""body":"""#),
            "an explicit empty string must clear: {}",
            body_of_space_a()
        );

        // omittable_str, where absent is a real state. Set a TTL, then clear it
        // twice - once with null, once with "". Both must land, and `false` here
        // means "nothing moved", which is how the second clear reports itself.
        let ttl = CString::new("sevenDays").unwrap();
        assert!(unsafe { atrium_core_set_space_archive_ttl(0, ttl.as_ptr()) });
        assert!(
            unsafe { atrium_core_set_space_archive_ttl(0, std::ptr::null()) },
            "a null TTL clears back to Follow General"
        );
        assert!(
            !unsafe { atrium_core_set_space_archive_ttl(0, empty.as_ptr()) },
            "already cleared, so an empty string moves nothing"
        );
        assert!(unsafe { atrium_core_set_space_archive_ttl(0, ttl.as_ptr()) });
        assert!(
            unsafe { atrium_core_set_space_archive_ttl(0, empty.as_ptr()) },
            "an empty TTL clears it too, by the same door as null"
        );

        atrium_core_close();
    }

    #[test]
    fn theme_raster_returns_the_pixel_buffer_or_null() {
        let tint = CString::new("oceanBlue").unwrap();
        let theme_json = unsafe { take_string(atrium_core_single_stop_theme(tint.as_ptr())) }
            .expect("a known tint must produce a theme");
        let c_theme = CString::new(theme_json).unwrap();

        let mut len: usize = 0;
        let raw = unsafe {
            atrium_core_theme_raster(c_theme.as_ptr(), 192, 108, false, 0.0, -1.0, &mut len)
        };
        assert!(!raw.is_null());
        assert_eq!(len, 192 * 108 * 4);
        unsafe { atrium_core_bytes_free(raw, len) };

        let garbage = CString::new("not json").unwrap();
        let mut garbage_len: usize = 1;
        let raw = unsafe {
            atrium_core_theme_raster(
                garbage.as_ptr(),
                192,
                108,
                false,
                0.0,
                -1.0,
                &mut garbage_len,
            )
        };
        assert!(raw.is_null());
        assert_eq!(garbage_len, 0, "the failure tail must zero out_len too");
    }

    #[test]
    fn bytes_free_on_null_is_a_no_op() {
        unsafe { atrium_core_bytes_free(std::ptr::null_mut(), 0) };
    }

    #[test]
    fn theme_raster_rejects_out_of_bounds_dimensions() {
        let tint = CString::new("oceanBlue").unwrap();
        let theme_json =
            unsafe { take_string(atrium_core_single_stop_theme(tint.as_ptr())) }.unwrap();
        let c_theme = CString::new(theme_json).unwrap();

        for (width, height) in [(0, 0), (-1, 108), (5000, 108)] {
            let mut len: usize = 1;
            let raw = unsafe {
                atrium_core_theme_raster(
                    c_theme.as_ptr(),
                    width,
                    height,
                    false,
                    0.0,
                    -1.0,
                    &mut len,
                )
            };
            assert!(raw.is_null(), "{width}x{height} must be refused");
            assert_eq!(len, 0, "{width}x{height} must zero out_len");
        }
    }

    #[test]
    fn bytes_or_null_with_a_null_out_len_frees_the_bytes_and_returns_null() {
        assert!(bytes_or_null(vec![1, 2, 3], std::ptr::null_mut()).is_null());
    }

    #[test]
    fn a_space_theme_round_trips_and_sets_the_tint() {
        let _guard = abi_serial();
        let dir = TempDir::new("theme-abi");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[{"id":"a","name":"Work","emoji":"W","tint":"#111111",
            "profileID":"p","pinnedTabs":[],"tabs":[],"order":0,"groups":[]}],
            "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
            "sidebarWidthMigrated":true}}"##,
        )
        .unwrap();

        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        let space_id = CString::new("a").unwrap();
        assert!(unsafe { atrium_core_space_theme(space_id.as_ptr()) }.is_null());

        let tint = CString::new("oceanBlue").unwrap();
        let theme_json =
            unsafe { take_string(atrium_core_single_stop_theme(tint.as_ptr())) }.unwrap();
        let sent: serde_json::Value = serde_json::from_str(&theme_json).unwrap();
        let c_theme = CString::new(theme_json).unwrap();
        assert!(unsafe { atrium_core_set_space_theme(space_id.as_ptr(), c_theme.as_ptr()) });

        let raw = unsafe { atrium_core_space_theme(space_id.as_ptr()) };
        let got = unsafe { take_string(raw) }.expect("the theme must round-trip");
        let got: serde_json::Value = serde_json::from_str(&got).unwrap();
        assert_eq!(
            got, sent,
            "a theme already sanitized must round-trip unchanged"
        );

        assert!(
            unsafe { atrium_core_set_space_theme(space_id.as_ptr(), std::ptr::null()) },
            "a null theme removes it"
        );
        assert!(unsafe { atrium_core_space_theme(space_id.as_ptr()) }.is_null());

        let unknown = CString::new("nope").unwrap();
        assert!(!unsafe { atrium_core_set_space_theme(unknown.as_ptr(), c_theme.as_ptr()) });

        atrium_core_close();
    }

    /// the space did not exist in any fixture - `create_space` put
    /// it there, then the same switch `BrowserController::SelectSpace` makes.
    #[test]
    fn a_created_space_is_switchable_to_and_scoped_on_its_own() {
        let _guard = abi_serial();
        let dir = TempDir::new("create-space-abi");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[
                {"id":"a","name":"Work","emoji":"W","tint":"#111111",
                 "profileID":"p","pinnedTabs":[],"tabs":[],"order":0,"groups":[]}
            ],
            "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
            "activeSpaceID":"a","sidebarWidthMigrated":true}}"##,
        )
        .unwrap();

        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });
        assert_eq!(atrium_core_active_space_index(), 0);

        let name = CString::new("Play").unwrap();
        let profile = CString::new("p").unwrap();
        let raw_id = unsafe { atrium_core_create_space(name.as_ptr(), profile.as_ptr()) };
        assert!(!raw_id.is_null());
        unsafe { atrium_core_string_free(raw_id) };

        // The new space renders last, so its position is 1 - the same index
        // BrowserController::SelectSpace would pass after a real cycle.
        assert!(atrium_core_set_active_space_index(1));
        assert_eq!(atrium_core_active_space_index(), 1);

        let body = CString::new("Work-only note").unwrap();
        assert!(unsafe { atrium_core_set_space_note(0, body.as_ptr(), 100.0) });

        let raw = atrium_core_get_space_note(atrium_core_active_space_index());
        let json = unsafe { take_string(raw) }.unwrap();
        assert!(
            !json.contains("Work-only note"),
            "switching to the created space still read Work's note: {json}"
        );

        atrium_core_close();
    }

    /// the Marginalia ABI end to end, including
    /// the pad payload the highlights ride on and survival across a reopen.
    #[test]
    fn the_marginalia_abi_stores_a_quote_edits_its_note_and_survives_a_reopen() {
        let _guard = abi_serial();
        let dir = TempDir::new("marginalia-abi");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[
                {"id":"a","name":"Work","emoji":"W","tint":"#111111",
                 "profileID":"p","pinnedTabs":[],
                 "tabs":[{"url":"https://example.com/doc","title":"Doc"}],
                 "order":0,"groups":[]}
            ],
            "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
            "sidebarWidthMigrated":true}}"##,
        )
        .unwrap();

        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        let url = CString::new("https://example.com/doc").unwrap();
        let short = CString::new("ab").unwrap();
        assert!(
            unsafe { atrium_core_add_marginal(0, url.as_ptr(), short.as_ptr(), 10.0) }.is_null(),
            "a two-character selection is refused across the ABI"
        );

        let quote = CString::new("the passage worth marking").unwrap();
        let id_raw = unsafe { atrium_core_add_marginal(0, url.as_ptr(), quote.as_ptr(), 20.0) };
        let id = unsafe { take_string(id_raw) }.expect("a valid selection returns an id");

        let c_id = CString::new(id.clone()).unwrap();
        let note = CString::new("why this matters").unwrap();
        assert!(unsafe { atrium_core_set_marginal_note(c_id.as_ptr(), note.as_ptr()) });
        let missing = CString::new("no-such-mark").unwrap();
        assert!(!unsafe { atrium_core_set_marginal_note(missing.as_ptr(), note.as_ptr()) });

        // The pad payload carries the highlights beside the note body.
        let pad_raw = atrium_core_get_space_note(0);
        let pad_json = unsafe { take_string(pad_raw) }.unwrap();
        let pad: serde_json::Value = serde_json::from_str(&pad_json).unwrap();
        assert_eq!(pad["marginalia"][0]["quote"], "the passage worth marking");
        assert_eq!(pad["marginalia"][0]["note"], "why this matters");

        // the row for that URL comes back dotted.
        let live = CString::new("[]").unwrap();
        let model_raw = unsafe { atrium_core_sidebar_model_cached(0, live.as_ptr()) };
        let model = unsafe { take_string(model_raw) }.unwrap();
        assert!(model.contains(r#""marked":true"#), "{model}");

        // a reopen still answers for that page.
        assert_eq!(atrium_core_save(), 1);
        atrium_core_close();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });
        let marks_raw = unsafe { atrium_core_marginalia_for_url(0, url.as_ptr()) };
        let marks = unsafe { take_string(marks_raw) }.unwrap();
        assert!(marks.contains("the passage worth marking"), "{marks}");
        assert!(marks.contains("why this matters"), "{marks}");

        let other = CString::new("https://example.com/elsewhere").unwrap();
        let none_raw = unsafe { atrium_core_marginalia_for_url(0, other.as_ptr()) };
        let none = unsafe { take_string(none_raw) }.unwrap();
        assert_eq!(none, "[]");

        atrium_core_close();
    }

    /// the Notes overview ABI end to end.
    #[test]
    fn the_notes_overview_abi_round_trips_both_kinds_of_note() {
        let _guard = abi_serial();
        let dir = TempDir::new("notes-overview-abi");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[
                {"id":"a","name":"Work","emoji":"W","tint":"#111111",
                 "profileID":"p","pinnedTabs":[],"tabs":[],"order":0,"groups":[]}
            ],
            "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
            "sidebarWidthMigrated":true}}"##,
        )
        .unwrap();

        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        // An empty core has nothing to show yet.
        let raw = atrium_core_notes_overview();
        let empty = unsafe { take_string(raw) }.expect("an empty overview, not null");
        assert_eq!(empty, "[]");

        // A space pad with content joins the list.
        let body = CString::new("Work jotting").unwrap();
        assert!(unsafe { atrium_core_set_space_note(0, body.as_ptr(), 10.0) });

        // A freestanding note is created empty and joins immediately.
        let created_raw = atrium_core_note_create(20.0);
        let created_json = unsafe { take_string(created_raw) }.expect("note_create must answer");
        let created: serde_json::Value = serde_json::from_str(&created_json).unwrap();
        let note_id = created["id"].as_str().unwrap().to_string();
        let c_id = CString::new(note_id.clone()).unwrap();

        let overview_raw = atrium_core_notes_overview();
        let overview = unsafe { take_string(overview_raw) }.unwrap();
        assert!(overview.contains("Work jotting"), "{overview}");
        assert!(overview.contains(&note_id), "{overview}");

        // Its body round-trips through get/set by id.
        let new_body = CString::new("freestanding jotting").unwrap();
        assert!(unsafe { atrium_core_note_set_body(c_id.as_ptr(), new_body.as_ptr(), 30.0) });
        let get_raw = unsafe { atrium_core_note_get(c_id.as_ptr()) };
        let got = unsafe { take_string(get_raw) }.unwrap();
        assert!(got.contains("freestanding jotting"), "{got}");

        // Dismiss hides it from the list without deleting it.
        assert!(unsafe { atrium_core_note_dismiss(c_id.as_ptr()) });
        let after_dismiss_raw = atrium_core_notes_overview();
        let after_dismiss = unsafe { take_string(after_dismiss_raw) }.unwrap();
        assert!(!after_dismiss.contains(&note_id), "{after_dismiss}");
        let still_there_raw = unsafe { atrium_core_note_get(c_id.as_ptr()) };
        assert!(!still_there_raw.is_null(), "dismiss must not delete");
        unsafe { atrium_core_string_free(still_there_raw) };

        // Delete removes a freestanding note entirely.
        assert!(unsafe { atrium_core_note_delete(c_id.as_ptr(), 40.0) });
        let gone_raw = unsafe { atrium_core_note_get(c_id.as_ptr()) };
        assert!(gone_raw.is_null());

        atrium_core_close();
    }

    /// add and remove a ShelfItem, and it survives a
    /// save-then-reopen the way a real restart would.
    #[test]
    fn the_shelf_abi_adds_removes_and_persists_across_a_reopen() {
        let _guard = abi_serial();
        let dir = TempDir::new("shelf-abi");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[
                {"id":"a","name":"Work","emoji":"W","tint":"#111111",
                 "profileID":"p","pinnedTabs":[],"tabs":[],"order":0,"groups":[]}
            ],
            "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
            "sidebarWidthMigrated":true}}"##,
        )
        .unwrap();

        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        let raw = atrium_core_shelf_overview();
        let empty = unsafe { take_string(raw) }.expect("an empty overview, not null");
        assert_eq!(empty, "[]");

        let kind = CString::new("page").unwrap();
        let title = CString::new("Example").unwrap();
        let url = CString::new("https://example.com/").unwrap();
        let space_id = CString::new("a").unwrap();
        let added_raw = unsafe {
            atrium_core_shelf_add(
                kind.as_ptr(),
                title.as_ptr(),
                url.as_ptr(),
                space_id.as_ptr(),
                10.0,
            )
        };
        let added_json = unsafe { take_string(added_raw) }.expect("shelf_add must answer");
        let added: serde_json::Value = serde_json::from_str(&added_json).unwrap();
        let item_id = added["id"].as_str().unwrap().to_string();
        let c_id = CString::new(item_id.clone()).unwrap();

        let overview_raw = atrium_core_shelf_overview();
        let overview = unsafe { take_string(overview_raw) }.unwrap();
        assert!(overview.contains(&item_id), "{overview}");
        assert!(overview.contains("\"spaceName\":\"Work\""), "{overview}");

        // Persists across a reopen - the same round trip a restart takes.
        assert_eq!(atrium_core_save(), 1);
        atrium_core_close();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });
        let reopened_raw = atrium_core_shelf_overview();
        let reopened = unsafe { take_string(reopened_raw) }.unwrap();
        assert!(reopened.contains(&item_id), "{reopened}");

        // Removing it also persists.
        assert!(unsafe { atrium_core_shelf_remove(c_id.as_ptr()) });
        assert_eq!(atrium_core_save(), 1);
        atrium_core_close();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });
        let after_remove_raw = atrium_core_shelf_overview();
        let after_remove = unsafe { take_string(after_remove_raw) }.unwrap();
        assert_eq!(after_remove, "[]");

        atrium_core_close();
    }

    /// A row persisted directly in the file - the auto-archive sweep
    /// that would otherwise create one does not exist yet.
    #[test]
    fn the_archived_overview_reads_persisted_rows_and_restore_removes_them() {
        let _guard = abi_serial();
        let dir = TempDir::new("archived-abi");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[
                {"id":"a","name":"Work","emoji":"W","tint":"#111111",
                 "profileID":"p","pinnedTabs":[],"tabs":[],"order":0,"groups":[]}
            ],
            "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
            "sidebarWidthMigrated":true},
            "archived":[
                {"id":"arch-1","url":"https://example.com/","title":"Example",
                 "spaceID":"a","archivedAt":10},
                {"id":"arch-2","url":"https://gone.example/","title":"Gone",
                 "spaceID":"no-such-space","archivedAt":20}
            ]}"##,
        )
        .unwrap();

        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        let raw = atrium_core_archived_overview();
        let overview = unsafe { take_string(raw) }.expect("an overview, not null");
        assert!(overview.contains("\"id\":\"arch-1\""), "{overview}");
        assert!(overview.contains("\"spaceName\":\"Work\""), "{overview}");
        assert!(overview.contains("\"spaceIndex\":0"), "{overview}");
        assert!(overview.contains("\"id\":\"arch-2\""), "{overview}");
        let gone: serde_json::Value = serde_json::from_str(&overview).unwrap();
        let gone_row = gone
            .as_array()
            .unwrap()
            .iter()
            .find(|r| r["id"] == "arch-2")
            .unwrap();
        assert!(
            gone_row.get("spaceName").is_none(),
            "a spaceID matching no space must carry no spaceName: {gone_row}"
        );
        assert!(
            gone_row.get("spaceIndex").is_none(),
            "a spaceID matching no space must carry no spaceIndex: {gone_row}"
        );

        let c_id = CString::new("arch-1").unwrap();
        assert!(unsafe { atrium_core_archived_remove(c_id.as_ptr()) });
        assert_eq!(atrium_core_save(), 1);
        atrium_core_close();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });
        let after_raw = atrium_core_archived_overview();
        let after = unsafe { take_string(after_raw) }.unwrap();
        assert!(!after.contains("arch-1"), "{after}");
        assert!(after.contains("arch-2"), "{after}");

        atrium_core_close();
    }

    /// same shape as the archived test above, for a
    /// real SnoozedTab.
    #[test]
    fn the_snoozed_overview_reads_persisted_rows_and_wake_removes_them() {
        let _guard = abi_serial();
        let dir = TempDir::new("snoozed-abi");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[
                {"id":"a","name":"Work","emoji":"W","tint":"#111111",
                 "profileID":"p","pinnedTabs":[],"tabs":[],"order":0,"groups":[]}
            ],
            "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
            "sidebarWidthMigrated":true},
            "snoozed":[
                {"id":"snz-1","url":"https://example.com/","title":"Example",
                 "spaceID":"a","snoozedAt":10,"wakeAt":100,"alert":true}
            ]}"##,
        )
        .unwrap();

        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        let raw = atrium_core_snoozed_overview();
        let overview = unsafe { take_string(raw) }.expect("an overview, not null");
        assert!(overview.contains("\"id\":\"snz-1\""), "{overview}");
        assert!(overview.contains("\"spaceName\":\"Work\""), "{overview}");
        assert!(overview.contains("\"spaceIndex\":0"), "{overview}");
        assert!(overview.contains("\"alert\":true"), "{overview}");

        let c_id = CString::new("snz-1").unwrap();
        assert!(unsafe { atrium_core_snoozed_remove(c_id.as_ptr()) });
        assert_eq!(atrium_core_save(), 1);
        atrium_core_close();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });
        let after_raw = atrium_core_snoozed_overview();
        let after = unsafe { take_string(after_raw) }.unwrap();
        assert_eq!(after, "[]");

        atrium_core_close();
    }

    /// the sweep timer's own call, through the shared cache.
    #[test]
    fn the_sweep_archive_export_moves_a_due_row_across_the_abi() {
        let _guard = abi_serial();
        let dir = TempDir::new("sweep-abi");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[
                {"id":"a","name":"Work","emoji":"W","tint":"#111111",
                 "profileID":"p","pinnedTabs":[],
                 "tabs":[{"url":"https://example.com/","title":"Example"}],
                 "order":0,"groups":[]}
            ],
            "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":0,
            "sidebarWidthMigrated":true}}"##,
        )
        .unwrap();

        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        let threshold = CString::new("twelveHours").unwrap();
        assert!(unsafe { atrium_core_sweep_archive(threshold.as_ptr(), 50_000.0) });
        assert_eq!(atrium_core_save(), 1);

        atrium_core_close();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });
        let raw = atrium_core_archived_overview();
        let overview = unsafe { take_string(raw) }.unwrap();
        assert!(overview.contains("https://example.com/"), "{overview}");

        atrium_core_close();
    }

    /// A bad threshold string must not archive anything - the sweep timer
    /// should not need to validate its own argument.
    #[test]
    fn the_sweep_archive_export_is_a_no_op_for_an_unknown_threshold() {
        let _guard = abi_serial();
        let dir = TempDir::new("sweep-bad-abi");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[
                {"id":"a","name":"Work","emoji":"W","tint":"#111111",
                 "profileID":"p","pinnedTabs":[],
                 "tabs":[{"url":"https://example.com/","title":"Example"}],
                 "order":0,"groups":[]}
            ],
            "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":0,
            "sidebarWidthMigrated":true}}"##,
        )
        .unwrap();

        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        let threshold = CString::new("not-a-real-threshold").unwrap();
        assert!(!unsafe { atrium_core_sweep_archive(threshold.as_ptr(), 50_000.0) });

        atrium_core_close();
    }

    /// the context menu's Snooze entry, addressed by
    /// (space, section, visible index) the way the surface sees a row.
    #[test]
    fn the_snooze_tab_export_moves_a_visible_row_across_the_abi() {
        let _guard = abi_serial();
        let dir = TempDir::new("snooze-abi");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[
                {"id":"a","name":"Work","emoji":"W","tint":"#111111",
                 "profileID":"p","pinnedTabs":[],
                 "tabs":[{"url":"https://example.com/","title":"Example"}],
                 "order":0,"groups":[]}
            ],
            "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":0,
            "sidebarWidthMigrated":true}}"##,
        )
        .unwrap();

        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        assert!(atrium_core_snooze_tab(0, 0, 0, 3_600.0, true, 0.0));
        assert_eq!(atrium_core_save(), 1);

        atrium_core_close();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });
        let raw = atrium_core_snoozed_overview();
        let overview = unsafe { take_string(raw) }.unwrap();
        assert!(overview.contains("https://example.com/"), "{overview}");
        assert!(overview.contains("\"alert\":true"), "{overview}");

        // The row is gone from the live space too - a snooze moves, it does
        // not copy.
        let raw =
            unsafe { atrium_core_sidebar_model_cached(-1, CString::new("[]").unwrap().as_ptr()) };
        let model = unsafe { take_string(raw) }.unwrap();
        assert!(!model.contains("https://example.com/"), "{model}");

        atrium_core_close();
    }

    /// An index past the end of the section must not panic - a stale surface
    /// position is a real input, not a programmer error.
    #[test]
    fn the_snooze_tab_export_is_false_for_an_out_of_range_index() {
        let _guard = abi_serial();
        let dir = TempDir::new("snooze-oob-abi");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[
                {"id":"a","name":"Work","emoji":"W","tint":"#111111",
                 "profileID":"p","pinnedTabs":[],"tabs":[],"order":0,"groups":[]}
            ],
            "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":0,
            "sidebarWidthMigrated":true}}"##,
        )
        .unwrap();

        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        assert!(!atrium_core_snooze_tab(0, 0, 0, 3_600.0, false, 0.0));

        atrium_core_close();
    }

    /// A folder ahead of a loose pin is one ITEM - an untranslated index
    /// snoozes one of the folder's own members instead.
    #[test]
    fn snooze_tab_on_a_pinned_row_behind_a_folder_snoozes_the_right_row() {
        let _guard = abi_serial();
        let dir = TempDir::new("snooze-pinned-folder");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[{"id":"a","name":"Work","emoji":"W","tint":"#111111",
            "profileID":"p",
            "pinnedTabs":[{"url":"https://g.test/","title":"G","groupID":"G"},
                          {"url":"https://pin.test/","title":"Pin"}],
            "tabs":[],"order":0,"groups":[{"id":"G","name":"Folder","collapsed":false}]}],
            "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":0,
            "sidebarWidthMigrated":true}}"##,
        )
        .unwrap();

        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        // Item 0 is the folder, refused - it has no single row to snooze.
        assert!(!atrium_core_snooze_tab(
            0, /*section=*/ 1, 0, 3_600.0, false, 0.0
        ));
        // Item 1 is the loose pin behind it.
        assert!(atrium_core_snooze_tab(
            0, /*section=*/ 1, 1, 3_600.0, false, 0.0
        ));

        let raw = atrium_core_snoozed_overview();
        let overview = unsafe { take_string(raw) }.unwrap();
        assert!(overview.contains("https://pin.test/"), "{overview}");
        assert!(!overview.contains("https://g.test/"), "{overview}");

        atrium_core_close();
    }

    /// The export the openURLs hook calls. The decision itself is covered in
    /// `navigation`; what is checked here is that it survives the boundary -
    /// the struct C++ fills in reaches Rust field for field.
    #[test]
    fn an_external_link_crosses_the_abi_and_answers_a_target() {
        let little = navigation::ExternalLinkInputs {
            external_links_open_in_little: true,
            has_url: true,
            is_http_or_https: true,
        };
        assert_eq!(
            unsafe { atrium_core_external_link_target(&little) },
            navigation::ExternalLinkTarget::LittleWindow as i32
        );

        let off = navigation::ExternalLinkInputs {
            external_links_open_in_little: false,
            ..little
        };
        assert_eq!(
            unsafe { atrium_core_external_link_target(&off) },
            navigation::ExternalLinkTarget::SpaceWindow as i32
        );
    }

    /// A null argument answers SPACE_WINDOW - today's behaviour. A caller that
    /// passed nothing must not be handed a new window type on the strength of
    /// a pointer it failed to fill in.
    #[test]
    fn a_null_external_link_input_keeps_todays_behaviour() {
        assert_eq!(
            unsafe { atrium_core_external_link_target(std::ptr::null()) },
            navigation::ExternalLinkTarget::SpaceWindow as i32
        );
    }

    /// dragging a pinned tab into the favourites grid used to leave
    /// it pinned as well. A regular row disappears on its own, because
    /// contents_of stops drawing a tab whose url is a favourite; a pinned row
    /// is never filtered, so the copy was visible.
    #[test]
    fn favouriting_a_pinned_row_unpins_it() {
        // The core is a process-global, so every ABI test here takes this.
        let _serial = abi_serial();
        let dir = TempDir::new("favourite-unpins");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            // r## because the tint contains `"#`, which would close an r#" here.
            r##"{"spaces":[{"id":"s1","name":"A","emoji":"X","tint":"#111111",
            "profileID":"p","pinnedTabs":[{"url":"https://pin.test/","title":"Pin"}],
            "tabs":[],"order":0,"groups":[]}],
            "appState":{"activeSpaceID":"s1","sidebarWidth":220,"schemaVersion":3,
            "archiveEpoch":1,"sidebarWidthMigrated":true}}"##,
        )
        .unwrap();
        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        assert!(atrium_core_favorite_row(
            -1, 0, /*section=*/ 1, /*slot=*/ -1
        ));

        assert_eq!(atrium_core_save(), 1, "the gesture changed the document");
        let written = std::fs::read_to_string(&path).unwrap();
        let doc: serde_json::Value = serde_json::from_str(&written).unwrap();
        assert_eq!(
            doc["favorites"].as_array().map(Vec::len),
            Some(1),
            "the favourite was added: {written}"
        );
        assert_eq!(
            doc["spaces"][0]["pinnedTabs"].as_array().map(Vec::len),
            Some(0),
            "and the pinned row it came from is gone: {written}"
        );
        atrium_core_close();
    }

    /// the grid used to append whatever was dropped on it, so the
    /// tile appeared somewhere the pointer had never been.
    #[test]
    fn a_favourited_row_lands_on_the_slot_it_was_dropped_on() {
        let _serial = abi_serial();
        let dir = TempDir::new("favourite-slot");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[{"id":"s1","name":"S","emoji":"S","tint":"#111111",
            "profileID":"p","pinnedTabs":[],
            "tabs":[{"url":"https://third.test/","title":"Third"}],
            "order":0,"groups":[]}],
            "favorites":[
              {"url":"https://first.test/","title":"First","profileID":"p"},
              {"url":"https://second.test/","title":"Second","profileID":"p"}],
            "appState":{"activeSpaceID":"s1","sidebarWidth":220,"schemaVersion":3,
            "archiveEpoch":1,"sidebarWidthMigrated":true}}"##,
        )
        .unwrap();
        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        assert!(atrium_core_favorite_row(
            -1, 0, /*section=*/ 0, /*slot=*/ 1
        ));

        assert_eq!(atrium_core_save(), 1, "the gesture changed the document");
        let written = std::fs::read_to_string(&path).unwrap();
        let doc: serde_json::Value = serde_json::from_str(&written).unwrap();
        let urls: Vec<&str> = doc["favorites"]
            .as_array()
            .unwrap()
            .iter()
            .map(|f| f["url"].as_str().unwrap())
            .collect();
        assert_eq!(
            urls,
            vec![
                "https://first.test/",
                "https://third.test/",
                "https://second.test/"
            ],
            "the new tile sits where it was dropped: {written}"
        );
        atrium_core_close();
    }

    /// A negative slot is the older shape, and the gesture that has no grid
    /// position at all - the row menu's own Add to Favourites.
    #[test]
    fn a_favourited_row_with_no_slot_still_appends() {
        let _serial = abi_serial();
        let dir = TempDir::new("favourite-append");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[{"id":"s1","name":"S","emoji":"S","tint":"#111111",
            "profileID":"p","pinnedTabs":[],
            "tabs":[{"url":"https://third.test/","title":"Third"}],
            "order":0,"groups":[]}],
            "favorites":[
              {"url":"https://first.test/","title":"First","profileID":"p"},
              {"url":"https://second.test/","title":"Second","profileID":"p"}],
            "appState":{"activeSpaceID":"s1","sidebarWidth":220,"schemaVersion":3,
            "archiveEpoch":1,"sidebarWidthMigrated":true}}"##,
        )
        .unwrap();
        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        assert!(atrium_core_favorite_row(
            -1, 0, /*section=*/ 0, /*slot=*/ -1
        ));

        assert_eq!(atrium_core_save(), 1, "the gesture changed the document");
        let written = std::fs::read_to_string(&path).unwrap();
        let doc: serde_json::Value = serde_json::from_str(&written).unwrap();
        assert_eq!(
            doc["favorites"][2]["url"].as_str(),
            Some("https://third.test/"),
            "an absent slot appends: {written}"
        );
        atrium_core_close();
    }

    /// a favourite dropped in a folder. The tab it opens is
    /// regular, and a folder's members live in the pinned run, so joining one
    /// has to pin it on the way.
    #[test]
    fn a_tab_joins_a_pinned_folder_by_its_tab_id() {
        let _serial = abi_serial();
        let dir = TempDir::new("group-by-tab-id");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[{"id":"s1","name":"S","emoji":"S","tint":"#111111",
            "profileID":"p",
            "pinnedTabs":[{"url":"https://one.test/","title":"One","groupID":"G"},
                          {"url":"https://two.test/","title":"Two","groupID":"G"}],
            "tabs":[],
            "order":0,"groups":[{"id":"G","name":"Reading","collapsed":false}]}],
            "appState":{"activeSpaceID":"s1","sidebarWidth":220,"schemaVersion":3,
            "archiveEpoch":1,"sidebarWidthMigrated":true}}"##,
        )
        .unwrap();
        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        // The tab the window opens for the dragged favourite, bound to its id.
        let url = CString::new("https://loose.test/").unwrap();
        let title = CString::new("Loose").unwrap();
        assert!(unsafe { atrium_core_open_tab(-1, 7, url.as_ptr(), title.as_ptr(), -1) });

        let group = CString::new("G").unwrap();
        assert!(unsafe {
            atrium_core_group_tab_by_tab_id(7, group.as_ptr(), /*member_slot=*/ 1)
        });

        assert_eq!(atrium_core_save(), 1, "the gesture changed the document");
        let written = std::fs::read_to_string(&path).unwrap();
        let doc: serde_json::Value = serde_json::from_str(&written).unwrap();
        let pinned = doc["spaces"][0]["pinnedTabs"].as_array().unwrap();
        // The folder is ONE item ahead of the pin this makes, so the item index
        // and the `pinned_tabs` index disagree - passing the raw one lands the
        // join on nothing.
        assert_eq!(
            pinned
                .iter()
                .map(|t| t["url"].as_str().unwrap())
                .collect::<Vec<_>>(),
            vec![
                "https://one.test/",
                "https://loose.test/",
                "https://two.test/"
            ],
            "slot 1 puts it between the two members: {written}"
        );
        assert_eq!(
            pinned[1]["groupID"].as_str(),
            Some("G"),
            "and it is in the folder: {written}"
        );
        atrium_core_close();
    }

    /// A folder ahead of a loose pin is one ITEM, not one `pinned_tabs` row -
    /// an untranslated index lands the rename on the folder's own member.
    #[test]
    fn rename_tab_on_a_pinned_row_behind_a_folder_names_the_right_row() {
        let _guard = abi_serial();
        let dir = TempDir::new("rename-pinned-folder");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[{"id":"s1","name":"A","emoji":"X","tint":"#111111",
            "profileID":"p",
            "pinnedTabs":[{"url":"https://g.test/","title":"G","groupID":"G"},
                          {"url":"https://pin.test/","title":"Pin"}],
            "tabs":[],"order":0,"groups":[{"id":"G","name":"Folder","collapsed":false}]}],
            "appState":{"activeSpaceID":"s1","sidebarWidth":220,"schemaVersion":3,
            "archiveEpoch":1,"sidebarWidthMigrated":true}}"##,
        )
        .unwrap();
        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        // Item 0 is the folder (its one member collapses into it), item 1 is
        // the loose pin behind it.
        let name = CString::new("Renamed").unwrap();
        assert!(unsafe {
            atrium_core_rename_tab(-1, 1, /*section=*/ 1, name.as_ptr())
        });

        assert_eq!(atrium_core_save(), 1, "the rename changed the document");
        let written = std::fs::read_to_string(&path).unwrap();
        let doc: serde_json::Value = serde_json::from_str(&written).unwrap();
        assert_eq!(
            doc["spaces"][0]["pinnedTabs"][1]["customTitle"], "Renamed",
            "the loose pin behind the folder: {written}"
        );
        assert_eq!(
            doc["spaces"][0]["pinnedTabs"][0]["customTitle"],
            serde_json::Value::Null,
            "the folder's own member must be untouched: {written}"
        );

        // Item 0 names the folder itself, which has no single `pinned_tabs`
        // row - refused, not silently landed on its first member.
        let refused = CString::new("Nope").unwrap();
        assert!(!unsafe {
            atrium_core_rename_tab(-1, 0, /*section=*/ 1, refused.as_ptr())
        });

        atrium_core_close();
    }

    /// A refused file is an error, not empty defaults. The difference matters:
    /// defaults look writable and are not.
    #[test]
    fn a_refused_spaces_file_is_an_error_rather_than_defaults() {
        let out = query(
            Store::Spaces,
            r#"{"spaces":[],"appState":{"sidebarWidth":280,"schemaVersion":999}}"#,
            0.0,
        );
        assert!(out.is_err());
    }

    /// A binding-only verb must not dirty the document, and a
    /// value-producing mutation must. Both used the same `bool` before, so
    /// nothing stopped one from being written as the other - and getting it
    /// backwards rewrites spaces.json on every tab replacement.
    #[test]
    fn a_binding_only_verb_leaves_the_document_clean_and_a_mutation_does_not() {
        let _guard = abi_serial();
        let dir = TempDir::new("dirty");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[{"id":"a","name":"Work","emoji":"W","tint":"#111111",
            "profileID":"p","pinnedTabs":[],
            "tabs":[{"url":"https://a/","title":"A"}],"order":0,"groups":[]}],
            "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
            "sidebarWidthMigrated":true}}"##,
        )
        .unwrap();
        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });
        assert!(!cache::shared().is_dirty(), "a fresh open is clean");

        // A rekey with no binding to carry, and one with: neither is a document
        // change, so neither may leave a write pending.
        assert!(!atrium_core_note_tab_replaced(41, 42));
        assert!(!cache::shared().is_dirty(), "a failed rekey must not dirty");
        assert!(!atrium_core_forget_tab(41));
        assert!(!cache::shared().is_dirty(), "forget_tab must not dirty");

        // A value-producing mutation is the other half: it DOES dirty, and the
        // value comes back rather than being smuggled through a local.
        let created = atrium_core_note_create(100.0);
        assert!(!created.is_null(), "a note was created");
        unsafe { atrium_core_string_free(created) };
        assert!(
            cache::shared().is_dirty(),
            "creating a note must leave a write pending"
        );

        atrium_core_close();
    }

    /// The three row verbs translate a DIFFERENT number of
    /// their endpoints, and which ones is discoverable only by reading all
    /// three together. The space here has a grouped row sitting between two
    /// ungrouped ones, so a visible position and a persisted index disagree -
    /// without that, every assertion below passes either way.
    #[test]
    fn the_row_verbs_translate_exactly_the_endpoints_that_count_rendered_rows() {
        let _guard = abi_serial();
        let dir = TempDir::new("xlate");
        let path = dir.join("spaces.json");
        // tabs: A(ungrouped) G(grouped) B(ungrouped) C(ungrouped), so visible
        // 0,1,2 are persisted 0,2,3.
        let doc = r##"{"spaces":[{"id":"a","name":"Work","emoji":"W","tint":"#111111",
            "profileID":"p",
            "pinnedTabs":[{"url":"https://p0/","title":"P0"},
                          {"url":"https://p1/","title":"P1"},
                          {"url":"https://p2/","title":"P2"}],
            "tabs":[{"url":"https://a/","title":"A"},
                    {"url":"https://g/","title":"G","groupID":"grp"},
                    {"url":"https://b/","title":"B"},
                    {"url":"https://c/","title":"C"}],
            "order":0,
            "groups":[{"id":"grp","name":"Folder","collapsed":false}]}],
            "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
            "sidebarWidthMigrated":true}}"##;
        std::fs::write(&path, doc).unwrap();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();

        // Each verb starts from the SAME document, so one verb's move cannot
        // stand in for another's translation.
        let reopen = || {
            atrium_core_close();
            std::fs::write(&path, doc).unwrap();
            assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });
        };
        // Read through `peek_bound`, the helper this task added, because no
        // export hands back the persisted arrays and the assertion has to be
        // about PERSISTED indices - the rendered model hides the difference.
        let pinned_urls = || -> Vec<String> {
            cache::shared()
                .peek_bound(|doc, _| {
                    doc.spaces[0]
                        .pinned_tabs
                        .iter()
                        .map(|t| t.url.clone())
                        .collect()
                })
                .unwrap_or_default()
        };
        let tab_urls = || -> Vec<String> {
            cache::shared()
                .peek_bound(|doc, _| doc.spaces[0].tabs.iter().map(|t| t.url.clone()).collect())
                .unwrap_or_default()
        };

        // reorder_pinned_item translates NEITHER endpoint through the regular
        // list. `from_item` counts pinned ITEMS, and with no folder pinned
        // here that is pinnedTabs one for one - a translated 1 reaches P2.
        reopen();
        assert!(atrium_core_reorder_pinned_item(0, 1, 0));
        assert_eq!(
            pinned_urls(),
            vec!["https://p1/", "https://p0/", "https://p2/"],
            "from=1 must mean pinnedTabs[1], P1 - a translated 1 would have moved P2"
        );

        // pin_tab translates the SOURCE only. from_visible=1 is B at persisted
        // 2, never the grouped G at persisted 1.
        reopen();
        assert!(atrium_core_pin_tab(0, 1, 0));
        assert_eq!(
            pinned_urls().first().map(String::as_str),
            Some("https://b/"),
            "an untranslated source would have pinned the grouped row G"
        );

        // reorder_tab translates BOTH. from_visible=0 is A at persisted 0 and
        // to_visible=1 is B at persisted 2, so A lands past the grouped row.
        reopen();
        assert!(atrium_core_reorder_tab(0, 0, 1));
        assert_eq!(
            tab_urls(),
            vec!["https://g/", "https://b/", "https://a/", "https://c/"],
            "an untranslated destination would have stopped at the grouped row"
        );

        atrium_core_close();
    }

    /// `is_err()` above is the whole reason the four wordings drifted
    /// unnoticed: nothing ever looked at WHICH error. These do, and they match
    /// on the variant, never on the sentence.
    #[test]
    fn a_refusal_is_matchable_by_variant_and_is_not_the_same_as_corruption() {
        let refused = query(
            Store::Spaces,
            r#"{"spaces":[],"appState":{"sidebarWidth":280,"schemaVersion":999}}"#,
            0.0,
        )
        .expect_err("a future schema must refuse");
        assert!(
            matches!(
                refused,
                schema::StoreError::Schema(schema::LoadError::Refused { file_version: 999 })
            ),
            "a caller must be able to read the refusal off the variant: {refused:?}"
        );

        // Corruption is the OTHER answer, and the point is that a caller can
        // now tell the two apart. Before this it could not: both arrived as a
        // String, and "do not overwrite" was indistinguishable from "damaged".
        let corrupt = query(Store::Spaces, "{ not json", 0.0)
            .expect_err("garbage must not parse as a document");
        assert!(
            matches!(
                corrupt,
                schema::StoreError::Schema(schema::LoadError::Unreadable(_))
            ),
            "{corrupt:?}"
        );

        // Display still carries the version, so a log line stays useful - but
        // the assertion above does not depend on these words, which is the
        // whole change. The file name is the CALLER's to prefix.
        assert_eq!(
            format!("spaces.json {refused}"),
            "spaces.json is at schema 999, which this build cannot read"
        );
    }

    /// The settings panes list spaces in sidebar order, so the index they send
    /// back counts the same way. These three used to take a backing-array
    /// position and were the only space exports that did.
    #[test]
    fn the_space_exports_take_a_sidebar_position() {
        let _guard = abi_serial();
        let dir = TempDir::new("space-index-abi");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[
                {"id":"b","name":"Second","emoji":"S","tint":"#111111",
                 "profileID":"p","pinnedTabs":[],"tabs":[],"order":1,"groups":[]},
                {"id":"a","name":"First","emoji":"F","tint":"#111111",
                 "profileID":"p","pinnedTabs":[],"tabs":[],"order":0,"groups":[]}
            ],
            "profiles":[{"id":"p","name":"P"},{"id":"q","name":"Q"}],
            "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
            "activeSpaceID":"a","sidebarWidthMigrated":true}}"##,
        )
        .unwrap();

        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        // Slot 0 renders "First", which is stored SECOND.
        let renamed = CString::new("Renamed").unwrap();
        assert!(unsafe { atrium_core_rename_space(0, renamed.as_ptr()) });
        let profile = CString::new("q").unwrap();
        assert!(unsafe { atrium_core_set_space_profile(0, profile.as_ptr()) });
        let ttl = CString::new("thirtyDays").unwrap();
        assert!(unsafe { atrium_core_set_space_archive_ttl(0, ttl.as_ptr()) });
        let icon = CString::new("⭐").unwrap();
        assert!(unsafe { atrium_core_set_space_icon(0, icon.as_ptr()) });

        let raw = unsafe { atrium_core_space_slot_for_id(CString::new("a").unwrap().as_ptr()) };
        let slot = unsafe { take_string(raw) }.unwrap();
        assert!(slot.contains("\"index\":0"), "slot moved: {slot}");
        assert!(
            slot.contains("\"profileId\":\"q\""),
            "the profile set hit the wrong space: {slot}"
        );
        // The inverse. Position 0 is "a", stored second, and past the end is null.
        let id = unsafe { take_string(atrium_core_space_id_at(0)) };
        assert_eq!(id.as_deref(), Some("a"));
        let id = unsafe { take_string(atrium_core_space_id_at(1)) };
        assert_eq!(id.as_deref(), Some("b"));
        assert!(atrium_core_space_id_at(2).is_null());
        assert!(atrium_core_space_id_at(-1).is_null());

        // The slot export only carries index and profile - the rename and the
        // TTL land on the same space too, checked off the saved document.
        assert_eq!(atrium_core_save(), 1);
        let saved = std::fs::read_to_string(&path).unwrap();
        assert!(saved.contains("\"name\":\"Renamed\""), "{saved}");
        assert!(saved.contains("\"archiveTtl\":\"thirtyDays\""), "{saved}");
        assert!(saved.contains("\"emoji\":\"⭐\""), "{saved}");
        assert!(saved.contains("\"emoji\":\"S\""), "the icon hit the wrong space: {saved}");

        atrium_core_close();
    }

    /// The routed tab MOVES. The row follows the tab rather than a copy of it
    /// being left in the space the user typed from.
    #[test]
    fn a_live_tab_moves_its_row_to_a_named_space() {
        let _guard = abi_serial();
        let dir = TempDir::new("move-tab-id-abi");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[
                {"id":"work","name":"Work","emoji":"W","tint":"#111111",
                 "profileID":"p","pinnedTabs":[],"tabs":[],"order":1,"groups":[]},
                {"id":"home","name":"Home","emoji":"H","tint":"#111111",
                 "profileID":"p","pinnedTabs":[],"tabs":[],"order":0,"groups":[]}
            ],
            "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
            "activeSpaceID":"home","sidebarWidthMigrated":true}}"##,
        )
        .unwrap();

        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        // Home renders first, so its slot is 0 though it is stored second.
        let url = CString::new("https://routed.test/").unwrap();
        let title = CString::new("Routed").unwrap();
        assert!(unsafe { atrium_core_open_tab(0, 7, url.as_ptr(), title.as_ptr(), -1) });

        let work = CString::new("work").unwrap();
        assert!(unsafe { atrium_core_move_tab_id_to_space(7, work.as_ptr()) });
        let raw = atrium_core_space_id_for_tab(7);
        assert_eq!(unsafe { take_string(raw) }.as_deref(), Some("work"));

        assert!(
            !unsafe { atrium_core_move_tab_id_to_space(7, work.as_ptr()) },
            "already there, so nothing moves and the document is not dirtied"
        );
        let gone = CString::new("nosuchspace").unwrap();
        assert!(!unsafe { atrium_core_move_tab_id_to_space(7, gone.as_ptr()) });
        assert!(!unsafe { atrium_core_move_tab_id_to_space(999, work.as_ptr()) });

        atrium_core_close();
    }

    /// A folder ahead of a loose pin is one ITEM - an untranslated index moves
    /// one of the folder's own members to the other space instead.
    #[test]
    fn move_tab_to_space_on_a_pinned_row_behind_a_folder_moves_the_right_row() {
        let _guard = abi_serial();
        let dir = TempDir::new("move-pinned-folder");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[
                {"id":"a","name":"Work","emoji":"W","tint":"#111111",
                 "profileID":"p",
                 "pinnedTabs":[{"url":"https://g.test/","title":"G","groupID":"G"},
                               {"url":"https://pin.test/","title":"Pin"}],
                 "tabs":[],"order":0,"groups":[{"id":"G","name":"Folder","collapsed":false}]},
                {"id":"b","name":"Home","emoji":"H","tint":"#111111",
                 "profileID":"p","pinnedTabs":[],"tabs":[],"order":1,"groups":[]}
            ],
            "appState":{"sidebarWidth":220,"schemaVersion":3,"archiveEpoch":1,
            "activeSpaceID":"a","sidebarWidthMigrated":true}}"##,
        )
        .unwrap();

        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        // Item 0 is the folder, refused - it has no single row to move.
        assert!(!atrium_core_move_tab_to_space(
            0, 0, /*from_section=*/ 1, 1, -1
        ));
        // Item 1 is the loose pin behind it.
        assert!(atrium_core_move_tab_to_space(
            0, 1, /*from_section=*/ 1, 1, -1
        ));

        assert_eq!(atrium_core_save(), 1);
        let written = std::fs::read_to_string(&path).unwrap();
        let doc: serde_json::Value = serde_json::from_str(&written).unwrap();
        assert_eq!(
            doc["spaces"][0]["pinnedTabs"].as_array().map(Vec::len),
            Some(1),
            "only the folder's member is left behind: {written}"
        );
        assert_eq!(
            doc["spaces"][1]["tabs"][0]["url"], "https://pin.test/",
            "the loose pin landed in the target's regular list: {written}"
        );

        atrium_core_close();
    }

    /// `mutate::assign_to_group` does its own item-index translation - a second
    /// one in this export would translate twice and land on the wrong row.
    #[test]
    fn assign_tab_to_group_on_a_pinned_row_behind_a_folder_moves_the_right_row() {
        let _guard = abi_serial();
        let dir = TempDir::new("assign-pinned-folder");
        let path = dir.join("spaces.json");
        std::fs::write(
            &path,
            r##"{"spaces":[{"id":"a","name":"Work","emoji":"W","tint":"#111111",
            "profileID":"p",
            "pinnedTabs":[{"url":"https://g.test/","title":"G","groupID":"G"},
                          {"url":"https://pin.test/","title":"Pin"}],
            "tabs":[],"order":0,
            "groups":[{"id":"G","name":"Folder","collapsed":false},
                      {"id":"H","name":"Other","collapsed":false}]}],
            "appState":{"activeSpaceID":"a","sidebarWidth":220,"schemaVersion":3,
            "archiveEpoch":1,"sidebarWidthMigrated":true}}"##,
        )
        .unwrap();

        atrium_core_close();
        let c_path = CString::new(path.to_str().unwrap()).unwrap();
        assert!(unsafe { atrium_core_open(c_path.as_ptr(), 0.0) });

        // Item 0 is the folder itself, refused.
        let group_h = CString::new("H").unwrap();
        assert!(!unsafe {
            atrium_core_assign_tab_to_group(0, /*section=*/ 1, 0, group_h.as_ptr(), -1)
        });
        // Item 1 is the loose pin behind it.
        assert!(unsafe {
            atrium_core_assign_tab_to_group(0, /*section=*/ 1, 1, group_h.as_ptr(), -1)
        });

        assert_eq!(atrium_core_save(), 1);
        let written = std::fs::read_to_string(&path).unwrap();
        let doc: serde_json::Value = serde_json::from_str(&written).unwrap();
        assert_eq!(
            doc["spaces"][0]["pinnedTabs"][1]["groupID"], "H",
            "the loose pin joined H, not the folder's own member: {written}"
        );
        assert_eq!(
            doc["spaces"][0]["pinnedTabs"][0]["groupID"], "G",
            "the folder's member is untouched: {written}"
        );

        atrium_core_close();
    }
}
