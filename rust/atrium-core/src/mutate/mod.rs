// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Row and space POSITION: the two index spaces, and the moves that cross them.
//!
//! Ported from `SpacesStore.swift`'s mutators and the index translation in
//! `SidebarModelJSON.swift`.
//!
//! TWO INDEX SPACES, and confusing them is the bug this module exists to
//! avoid. The surface counts what it RENDERS - the ungrouped rows of one
//! space, in sidebar order - and the file stores something else: `tabs` holds
//! grouped and ungrouped entries in one array, and `spaces` is in whatever
//! order it was written. So every index from a surface is translated before it
//! addresses anything, and a position that does not resolve is IGNORED rather
//! than clamped. Moving an arbitrary row is worse than doing nothing, and the
//! divergence only appears for users who have tab groups - which is why an
//! untranslated index survives testing and then reorders the wrong tab.
//!
//! Every function here is pure: it takes the document, changes it, and says
//! whether anything moved. Nothing reads a clock and nothing touches the disk.
//! `false` means the document is untouched, so the caller neither writes the
//! file nor re-renders. That holds for the submodules too.
//!
//! WHAT THIS FILE KEEPS, after the split: `Section`, the translation
//! functions, the row moves and pins that use them, space create/rename/
//! reorder/active, and `locate_token`. Everything here needs the index rule
//! above, which is why the module doc still applies to it.
//!
//! WHAT MOVED, each with a doc of its own describing what it holds:
//!
//! | module | holds |
//! |---|---|
//! | `favorites` | favourite tiles and doorways, both per profile |
//! | `notes` | space pads, freestanding notes, marginalia |
//! | `shelf` | items set aside from a page |
//! | `archive` | archived and snoozed rows, and the sweep |
//! | `groups` | tab groups, and the row rename that shares their shape |
//! | `live_tabs` | the binding between a Chromium tab id and a persisted row |
//! | `profiles` | the profile list, and the space-to-profile move |
//!
//! The sidebar's own width and a space's own settings overrides are here
//! directly rather than in modules of their own: each held one `pub fn`.
//!
//! They are re-exported here, so `mutate::add_favorite` still resolves. Moving
//! the code was a smaller change to review than moving the code AND sixty
//! callers, and the re-export list is this file's index.

use crate::choices;
use crate::sidebar;
use crate::spaces::{
    Document, PinnedItem, RowToken, Space, SpaceTab, UNTOKENED, pinned_items,
};

// Submodules, split along the boundaries this file's own sections already
// marked. Re-exported rather than re-pathed: `mutate::` is the
// name sixty call sites use, and moving code is a smaller change to review
// than moving code AND every caller.
mod shelf;
pub use shelf::*;

mod notes;
pub use notes::*;

mod favorites;
pub use favorites::*;

mod archive;
pub use archive::*;

mod groups;
pub use groups::*;

mod live_tabs;
pub use live_tabs::*;

mod profiles;
pub use profiles::*;

#[cfg(any(test, feature = "test-support"))]
pub mod test_support;

/// Which of a space's two lists a row index addresses.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Section {
    Regular,
    Pinned,
}

impl Section {
    /// 1 is pinned, anything else regular - the encoding the surface already
    /// sends, where the section arrives as a string and crosses the ABI as an
    /// int.
    pub fn from_code(code: i32) -> Self {
        if code == 1 {
            Self::Pinned
        } else {
            Self::Regular
        }
    }

    /// The inverse. Without it the wire values were re-spelled as bare 1 and 0
    /// in `restore`, where a grep for `Section` finds nothing - so the encoding
    /// lived in two places and only one of them was searchable.
    pub fn to_code(self) -> i32 {
        match self {
            Self::Pinned => 1,
            Self::Regular => 0,
        }
    }
}

/// The backing-array positions of the spaces, in the order the sidebar renders
/// them.
///
/// The same sort `sidebar::snapshot` uses, and it has to stay the same one: an
/// index from the surface means "the space I was looking at", so a write that
/// ordered them differently would address a different space than the row the
/// user dragged.
pub(crate) fn ordered_positions(spaces: &[Space]) -> Vec<usize> {
    let mut positions: Vec<usize> = (0..spaces.len()).collect();
    positions.sort_by_key(|&i| spaces[i].order);
    positions
}

/// Moves a space from one SIDEBAR position to another, renumbering `order`.
///
/// Positions, not backing indices: the surface only ever knows where a card sat
/// on screen, and the backing array is in insertion order, not sidebar order.
/// Renumbering every space from zero afterwards is deliberate - the file can
/// arrive with duplicate or sparse `order` values from older builds, and a move
/// that only rewrote two of them would leave the rest ambiguous.
///
/// Returns false when either position does not resolve, or when the move lands
/// where it started, so nothing dirties the file for a gesture that did
/// nothing.
pub fn reorder_space(doc: &mut Document, from: usize, to: usize) -> bool {
    let mut positions = ordered_positions(&doc.spaces);
    if from >= positions.len() || to >= positions.len() || from == to {
        return false;
    }
    let moved = positions.remove(from);
    positions.insert(to, moved);
    for (rank, index) in positions.into_iter().enumerate() {
        doc.spaces[index].order = rank as i64;
    }
    true
}

/// The backing-array index of the space at `position` in sidebar order.
pub fn space_at(doc: &Document, position: usize) -> Option<usize> {
    ordered_positions(&doc.spaces).get(position).copied()
}

/// The backing-array index of the space a per-window mutation addresses.
///
/// `active_space_index` is the window's own slot when the caller knows it, and
/// `None` when it does not - C++ with no Swift to ask - in which case the
/// document's own `activeSpaceID` decides. Same convention as the read side,
/// where a negative index means the same thing.
pub fn active_space(doc: &Document, active_space_index: Option<usize>) -> Option<usize> {
    let position = active_space_index.unwrap_or_else(|| {
        sidebar::active_index_from_document(&doc.spaces, doc.app_state.active_space_id.as_deref())
    });
    space_at(doc, position)
}

/// The index in `space.tabs` of the row the surface renders at `position`.
///
/// The rendered list is the UNGROUPED tabs; the persisted array holds grouped
/// ones too. `None` when no row lives there.
pub fn persisted_index_of_visible(
    position: usize,
    space: &Space,
    hidden: &[RowToken],
) -> Option<usize> {
    space
        .tabs
        .iter()
        .enumerate()
        .filter(|(_, tab)| is_drawn_row(tab, hidden))
        .map(|(index, _)| index)
        .nth(position)
}

/// Mirrors `sidebar::contents_of`: hidden by token only, not by favourite URL.
fn is_drawn_row(tab: &SpaceTab, hidden: &[RowToken]) -> bool {
    tab.group_id.is_none() && !hidden.contains(&tab.token)
}

/// The index in `space.tabs` an insertion at visible SLOT `slot` lands on.
///
/// A slot is a gap, not a row: slot `s` means "before the row at visible
/// position `s`", and the slot past the last visible row means append - a real
/// destination for an unpinned tab, and one `persisted_index_of_visible`
/// refuses precisely because no row occupies it.
///
/// `None` for a slot outside `0..=visible_count`, so the caller does nothing
/// rather than inserting somewhere arbitrary.
pub fn persisted_insertion_index_of_visible_slot(
    slot: usize,
    space: &Space,
    hidden: &[RowToken],
) -> Option<usize> {
    let mut seen = 0;
    for (index, tab) in space.tabs.iter().enumerate() {
        if !is_drawn_row(tab, hidden) {
            continue;
        }
        if seen == slot {
            return Some(index);
        }
        seen += 1;
    }
    // Past the last visible row: append. This also covers a space whose tabs
    // are all grouped, where nothing is visible and slot 0 is the append slot.
    if slot == seen {
        Some(space.tabs.len())
    } else {
        None
    }
}

/// Where the item at `item` starts in `pinned_tabs`, and how many entries it
/// spans - one for a tab, the whole run for a folder.
fn pinned_run_of_item(space: &Space, item: usize) -> Option<(usize, usize)> {
    match pinned_items(space).get(item)? {
        PinnedItem::Tab(index) => Some((*index, 1)),
        PinnedItem::Folder(group) => {
            let id = space.groups.get(*group)?.id.as_str();
            let start = space
                .pinned_tabs
                .iter()
                .position(|t| t.group_id.as_deref() == Some(id))?;
            let len = space.pinned_tabs[start..]
                .iter()
                .take_while(|t| t.group_id.as_deref() == Some(id))
                .count();
            Some((start, len))
        }
    }
}

/// The index in `pinned_tabs` of the first entry of the pinned ITEM at `item`.
fn pinned_index_of_item(space: &Space, item: usize) -> Option<usize> {
    pinned_run_of_item(space, item).map(|(start, _)| start)
}

/// The index in `pinned_tabs` of the pinned ITEM at `item`, refusing a folder -
/// a folder's run has no single `pinned_tabs` row to hand back.
pub fn pinned_tab_index_of_item(space: &Space, item: usize) -> Option<usize> {
    match pinned_items(space).get(item)? {
        PinnedItem::Tab(index) => Some(*index),
        PinnedItem::Folder(_) => None,
    }
}

/// The pinned ITEM index of the `pinned_tabs` row at `raw`. The inverse of
/// `pinned_tab_index_of_item`, for a caller holding a tab rather than a row
/// position - a tab just pinned by its id has no item index yet.
pub fn item_index_of_pinned_tab(space: &Space, raw: usize) -> Option<usize> {
    pinned_items(space)
        .iter()
        .position(|item| matches!(item, PinnedItem::Tab(at) if *at == raw))
}

/// The index in `pinned_tabs` an insertion at item SLOT `slot` lands on. A slot
/// past the last item appends, and no slot ever lands inside a folder's run.
pub fn pinned_insertion_index_of_item_slot(space: &Space, slot: usize) -> usize {
    pinned_index_of_item(space, slot).unwrap_or(space.pinned_tabs.len())
}

/// Moves a pinned ITEM - a tab, or a folder with every member it holds - to
/// `to_item`, which is a POST-REMOVAL position clamped into range the way
/// `move_tab` clamps its own.
pub fn reorder_pinned_item(
    doc: &mut Document,
    space: usize,
    from_item: usize,
    to_item: usize,
) -> bool {
    let Some(s) = doc.spaces.get(space) else {
        return false;
    };
    let count = pinned_items(s).len();
    if from_item >= count {
        return false;
    }
    let clamped = to_item.min(count - 1);
    if clamped == from_item {
        return false;
    }
    let Some((start, len)) = pinned_run_of_item(s, from_item) else {
        return false;
    };
    let s = &mut doc.spaces[space];
    let moved: Vec<SpaceTab> = s.pinned_tabs.drain(start..start + len).collect();
    let at = pinned_insertion_index_of_item_slot(s, clamped);
    s.pinned_tabs.splice(at..at, moved);
    true
}

/// Moves a tab within one of a space's lists.
///
/// `to` is a POST-REMOVAL position, clamped into range: an index from a surface
/// can be stale by the time it arrives, and landing at the end beats dropping
/// the move. A clamp that lands back on `from` is not a move, and returns false
/// rather than dirtying the file for nothing.
pub fn move_tab(
    doc: &mut Document,
    space: usize,
    from: usize,
    to: usize,
    section: Section,
) -> bool {
    let Some(space) = doc.spaces.get_mut(space) else {
        return false;
    };
    let list = match section {
        Section::Regular => &mut space.tabs,
        Section::Pinned => &mut space.pinned_tabs,
    };
    if from >= list.len() {
        return false;
    }
    let clamped = to.min(list.len() - 1);
    if clamped == from {
        return false;
    }
    let tab = list.remove(from);
    list.insert(clamped, tab);
    true
}

/// Pins the tab at `index` at pinned item SLOT `to_slot`, or appends on `None`.
/// Inserting at the slot keeps a positional pin a SINGLE mutation - a two-step
/// version briefly persists the tab in the wrong slot.
pub fn pin_tab(doc: &mut Document, space: usize, index: usize, to_slot: Option<usize>) -> bool {
    let Some(space) = doc.spaces.get_mut(space) else {
        return false;
    };
    if index >= space.tabs.len() {
        return false;
    }
    let at = to_slot.map_or(space.pinned_tabs.len(), |slot| {
        pinned_insertion_index_of_item_slot(space, slot)
    });
    let mut tab = space.tabs.remove(index);
    // A lone pin leaves its group: a member left behind would render nowhere.
    // A whole folder pins through `pin_folder`.
    tab.group_id = None;
    // The URL pinned AT, so a later Reset Tab can snap back to it even after
    // the tab has navigated away.
    tab.pinned_url = Some(tab.url.clone());
    space.pinned_tabs.insert(at, tab);
    true
}

/// Unpins the pinned ITEM at `item` into the regular list at `to_index`, the
/// reverse of `pin_tab`. `None` appends. A folder item is refused: it leaves
/// the pinned section through `ungroup_member`, member by member.
pub fn unpin_tab(doc: &mut Document, space: usize, item: usize, to_index: Option<usize>) -> bool {
    let Some(space) = doc.spaces.get_mut(space) else {
        return false;
    };
    let Some(PinnedItem::Tab(index)) = pinned_items(space).get(item).copied() else {
        return false;
    };
    let mut tab = space.pinned_tabs.remove(index);
    tab.group_id = None;
    // The reverse of pin_tab's capture: a regular tab has no pinned URL to
    // reset to.
    tab.pinned_url = None;
    let at = to_index.map_or(space.tabs.len(), |i| i.min(space.tabs.len()));
    space.tabs.insert(at, tab);
    true
}

/// Deletes a pinned row outright, rather than moving it to the regular list
/// the way `unpin_tab` does. This is the second stage of the lab's pinned
/// close: the dash closes the tab and leaves the tile, and the x on a tile with
/// nothing loaded takes the tile too.
pub fn remove_pinned_tab(doc: &mut Document, space: usize, index: usize) -> bool {
    let Some(space) = doc.spaces.get_mut(space) else {
        return false;
    };
    if index >= space.pinned_tabs.len() {
        return false;
    }
    space.pinned_tabs.remove(index);
    true
}

/// Moves a row from one space to another. It always lands in the target's
/// REGULAR list whatever section it came from - the drag target is a space, not
/// a section of one, and the user can re-pin there afterwards.
///
/// `to_index` counts the target's persisted tabs, clamped; `None` appends.
pub fn move_tab_across_spaces(
    doc: &mut Document,
    from_space: usize,
    from_index: usize,
    from_section: Section,
    to_space: usize,
    to_index: Option<usize>,
) -> bool {
    if from_space >= doc.spaces.len() || to_space >= doc.spaces.len() {
        return false;
    }
    let source = &mut doc.spaces[from_space];
    let list = match from_section {
        Section::Regular => &mut source.tabs,
        Section::Pinned => &mut source.pinned_tabs,
    };
    if from_index >= list.len() {
        return false;
    }
    let mut tab = list.remove(from_index);
    // Groups are scoped to one space's own `groups`, so a source-space id would
    // dangle in the target. The landing is a demotion out of pinned, so the
    // snap-back URL clears the same way unpinning clears it.
    tab.group_id = None;
    tab.pinned_url = None;

    let target = &mut doc.spaces[to_space];
    let at = to_index.map_or(target.tabs.len(), |i| i.min(target.tabs.len()));
    target.tabs.insert(at, tab);
    true
}

/// Moves a split's regular tabs, adjacent or not, as one run in list order.
/// `to_index` counts the target list before the members leave it.
pub fn move_run(
    doc: &mut Document,
    from_space: usize,
    members: &[usize],
    to_space: usize,
    to_index: Option<usize>,
) -> bool {
    if from_space >= doc.spaces.len() || to_space >= doc.spaces.len() || members.is_empty() {
        return false;
    }
    let len = doc.spaces[from_space].tabs.len();
    let mut sorted = members.to_vec();
    sorted.sort_unstable();
    sorted.dedup();
    if sorted.len() != members.len() || sorted[sorted.len() - 1] >= len {
        return false;
    }
    let target_len = doc.spaces[to_space].tabs.len();
    let mut at = to_index.map_or(target_len, |i| i.min(target_len));
    let same = from_space == to_space;
    if same {
        at -= sorted.iter().filter(|&&m| m < at).count();
        if sorted.iter().enumerate().all(|(k, &m)| m == at + k) {
            return false;
        }
    }
    let source = &mut doc.spaces[from_space].tabs;
    let mut moved: Vec<SpaceTab> = sorted.iter().rev().map(|&m| source.remove(m)).collect();
    moved.reverse();
    if !same {
        // The same demotion move_tab_across_spaces applies to one row.
        for tab in &mut moved {
            tab.group_id = None;
            tab.pinned_url = None;
        }
    }
    doc.spaces[to_space].tabs.splice(at..at, moved);
    true
}

/// The sidebar position of the space `space_id` names, or `None` if no space
/// carries that id any more - the row's "removed space" case.
pub(crate) fn sidebar_position_of(doc: &Document, space_id: &str) -> Option<i32> {
    let index = doc.spaces.iter().position(|s| s.id == space_id)?;
    ordered_positions(&doc.spaces)
        .iter()
        .position(|&i| i == index)
        .map(|p| p as i32)
}

/// Where the space with `id` renders, and which profile owns it.
///
/// A SIDEBAR position, because the one caller feeds it to
/// `atrium_core_set_active_space_index`, which counts the same way. Both facts
/// come back together so a routing decision cannot pair one space's slot with
/// another space's profile across a mutation.
pub fn space_slot_for_id(doc: &Document, id: &str) -> Option<(usize, String)> {
    let backing = doc.spaces.iter().position(|s| s.id == id)?;
    let position = sidebar_position_of(doc, id)?;
    Some((position as usize, doc.spaces[backing].profile_id.clone()))
}

/// The id of the space rendered at SIDEBAR `position`, the inverse of
/// `space_slot_for_id`. `None` past the rail's end.
pub fn space_id_at(doc: &Document, position: usize) -> Option<String> {
    ordered_positions(&doc.spaces)
        .get(position)
        .map(|&backing| doc.spaces[backing].id.clone())
}

/// Drops the oldest entries until `len` is within `cap`.
pub(crate) fn trim_to_cap<T>(items: &mut Vec<T>, cap: usize) {
    if items.len() > cap {
        let over = items.len() - cap;
        items.drain(0..over);
    }
}

/// Clips `s` to `max_chars` characters, on a char boundary rather than a byte
/// one. Borrows when `s` is already within the cap, so an ordinary
/// write never allocates just to check its own length.
pub(crate) fn truncate_to(s: &str, max_chars: usize) -> std::borrow::Cow<'_, str> {
    match s.char_indices().nth(max_chars) {
        Some((byte_at, _)) => std::borrow::Cow::Borrowed(&s[..byte_at]),
        None => std::borrow::Cow::Borrowed(s),
    }
}

/// The document's own space cap. Refused rather than dropping the oldest
/// space, since a space holds tabs, groups and settings a drop would erase.
pub const MAX_SPACES: usize = 200;

/// The id comes from the document's own token counter - the crate has no RNG.
/// `None` past `MAX_SPACES`.
pub fn create_space(doc: &mut Document, name: &str, profile_id: &str) -> Option<String> {
    if doc.spaces.len() >= MAX_SPACES {
        return None;
    }
    let id = format!("space-{}", doc.allocate_token());
    let order = doc
        .spaces
        .iter()
        .map(|s| s.order)
        .max()
        .map_or(0, |m| m + 1);
    doc.spaces.push(Space {
        id: id.clone(),
        name: name.to_string(),
        emoji: "\u{1F5C2}\u{FE0F}".to_string(),
        tint: "oceanBlue".to_string(),
        profile_id: profile_id.to_string(),
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
    });
    Some(id)
}

/// Renames a space. An empty name does nothing - a space's dot always needs
/// one to show.
pub fn rename_space(doc: &mut Document, space: usize, name: &str) -> bool {
    if name.is_empty() {
        return false;
    }
    let Some(space) = doc.spaces.get_mut(space) else {
        return false;
    };
    if space.name == name {
        return false;
    }
    space.name = name.to_string();
    true
}

/// Sets a space's icon glyph. An empty string clears it, and the sidebar then
/// draws the name's first letter.
pub fn set_space_icon(doc: &mut Document, space: usize, icon: &str) -> bool {
    let Some(space) = doc.spaces.get_mut(space) else {
        return false;
    };
    if space.emoji == icon {
        return false;
    }
    space.emoji = icon.to_string();
    true
}

/// Sets a space's stored tint - a `#RRGGBB` hex or a name from `tint.rs`. A
/// no-op write does nothing, matching `rename_space`.
pub fn set_space_tint(doc: &mut Document, space_index: usize, hex: &str) -> bool {
    let Some(space) = doc.spaces.get_mut(space_index) else {
        return false;
    };
    if space.tint == hex {
        return false;
    }
    space.tint = hex.to_string();
    true
}

/// Sets a space's emoji. Same no-op rule as `set_space_tint`.
pub fn set_space_emoji(doc: &mut Document, space_index: usize, emoji: &str) -> bool {
    !emoji.is_empty() && set_space_icon(doc, space_index, emoji)
}

/// Where a row lives now: its space's backing index, its list, and its
/// position in that list.
///
/// `None` for [`UNTOKENED`] and for a row that has left the document.
pub fn locate_token(doc: &Document, token: RowToken) -> Option<(usize, Section, usize)> {
    if token == UNTOKENED {
        return None;
    }
    for (space, s) in doc.spaces.iter().enumerate() {
        if let Some(index) = s.pinned_tabs.iter().position(|t| t.token == token) {
            return Some((space, Section::Pinned, index));
        }
        if let Some(index) = s.tabs.iter().position(|t| t.token == token) {
            return Some((space, Section::Regular, index));
        }
    }
    None
}

impl Opened {
    pub fn token(self) -> RowToken {
        match self {
            Self::Adopted(token) | Self::Added(token) => token,
        }
    }

    pub fn changed_the_document(self) -> bool {
        matches!(self, Self::Added(_))
    }
}

/// Records which space is active, by its position in sidebar order.
///
/// Out of range is IGNORED, not clamped: a stale index means the surface is
/// describing a model that no longer exists, and switching to a neighbouring
/// space would be worse than doing nothing.
pub fn set_active_space(doc: &mut Document, position: usize) -> bool {
    let Some(backing) = space_at(doc, position) else {
        return false;
    };
    let id = doc.spaces[backing].id.clone();
    if doc.app_state.active_space_id.as_deref() == Some(id.as_str()) {
        return false;
    }
    doc.app_state.active_space_id = Some(id);
    true
}

/// Records the sidebar's settled width.
///
/// A width outside the range is REFUSED, not clamped: a caller sending 0
/// means collapsed, which is session state and must not be stored as a width
/// - the collapse is what a re-expand returns to. `false` when the drag ends
/// where it started, so the caller does not rewrite the file for nothing.
pub fn set_sidebar_width(doc: &mut Document, width: f64) -> bool {
    if !width.is_finite() || width < MIN_SIDEBAR_WIDTH {
        return false;
    }
    let settled = width.min(MAX_SIDEBAR_WIDTH);
    if (doc.app_state.sidebar_width - settled).abs() < f64::EPSILON {
        return false;
    }
    doc.app_state.sidebar_width = settled;
    true
}

/// The host's own clamps (kMinSidebarWidth/kMaxSidebarWidth in
/// atrium_browser_window.cc), restated here because the core must not store a
/// width the host would refuse to render.
pub const MIN_SIDEBAR_WIDTH: f64 = 160.0;

pub const MAX_SIDEBAR_WIDTH: f64 = 500.0;

/// Sets a space's own override field, or clears it.
///
/// `field` hands out the one field this call owns. `parse` decides what is
/// legal. `Err(())` REFUSES the write rather than clearing it.
fn set_space_choice<F, P>(
    doc: &mut Document,
    space: usize,
    value: Option<&str>,
    field: F,
    parse: P,
) -> bool
where
    F: FnOnce(&mut Space) -> &mut Option<String>,
    P: FnOnce(&str) -> Result<String, ()>,
{
    let Some(space) = doc.spaces.get_mut(space) else {
        return false;
    };
    let wanted = match value {
        // Absent and empty are the same request: clear it.
        None => None,
        Some("") => None,
        Some(v) => match parse(v) {
            Ok(canonical) => Some(canonical),
            Err(()) => return false,
        },
    };
    let slot = field(space);
    if *slot == wanted {
        return false;
    }
    *slot = wanted;
    true
}

/// Sets a space's own auto-archive TTL rung. `None` or `""` clears it to
/// Follow General. An unrecognised rung is rejected rather than guessed.
pub fn set_space_archive_ttl(doc: &mut Document, space: usize, ttl: Option<&str>) -> bool {
    set_space_choice(
        doc,
        space,
        ttl,
        |s| &mut s.archive_ttl,
        |v| choices::parse_choice(choices::ARCHIVE_THRESHOLDS, v),
    )
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::mutate::test_support::*;
    // The cross-subsystem cap test below drives all of these.
    use crate::mutate::favorites::{MAX_DOORWAYS, add_doorway};
    use crate::mutate::notes::{MAX_MARGINALIA, MAX_NOTES, add_marginal};

    /// a renderer can call these verbs in a loop. Each section had a cap
    /// or it did not, and four did not.
    #[test]
    fn the_growable_sections_stop_at_their_caps() {
        let mut doc = document(vec![space("One", 0)]);
        // One past each cap, so the trim is exercised rather than merely reached.
        for n in 0..MAX_MARGINALIA + 1 {
            add_marginal(
                &mut doc,
                0,
                &format!("https://e.test/{n}"),
                "a quote long enough to count as a highlight",
                1.0,
            );
        }
        assert_eq!(doc.marginalia.len(), MAX_MARGINALIA);
        for _ in 0..MAX_NOTES + 1 {
            create_freestanding_note(&mut doc, 1.0);
        }
        assert_eq!(doc.notes.len(), MAX_NOTES);
        for n in 0..MAX_DOORWAYS + 1 {
            add_doorway(&mut doc, &format!("https://d.test/{n}"), "id-One", "p");
        }
        assert_eq!(doc.doorways.len(), MAX_DOORWAYS);
    }

    // These span subsystems on purpose: they assert several caps in one
    // pass, so they live where all of them are in scope rather than in the
    // module that owns any one of them.

    use crate::spaces::{TabGroup, assign_tokens};

    #[test]
    fn a_visible_position_skips_the_grouped_rows() {
        let mut s = space("A", 0);
        s.tabs = vec![tab("a"), grouped("g", "G"), tab("b")];
        assert_eq!(persisted_index_of_visible(0, &s, &[]), Some(0));
        assert_eq!(persisted_index_of_visible(1, &s, &[]), Some(2));
        assert_eq!(persisted_index_of_visible(2, &s, &[]), None);
    }

    /// contents_of hides a row by its token as well as a grouped row,
    /// and this mapping only knew about the grouped one. Every gesture that
    /// carries a row position then landed on a tab the user never pointed at.
    #[test]
    fn a_visible_position_skips_a_row_hidden_by_token() {
        let mut s = space("A", 0);
        s.tabs = vec![tab("https://fav.test/"), tab("https://plain.test/")];
        s.tabs[0].token = 7;
        assert_eq!(
            persisted_index_of_visible(0, &s, &[7]),
            Some(1),
            "the only drawn row is the plain one"
        );
        assert_eq!(persisted_index_of_visible(1, &s, &[7]), None);
    }

    #[test]
    fn an_insertion_slot_skips_a_row_hidden_by_token() {
        let mut s = space("A", 0);
        s.tabs = vec![tab("https://fav.test/"), tab("https://plain.test/")];
        s.tabs[0].token = 7;
        assert_eq!(
            persisted_insertion_index_of_visible_slot(0, &s, &[7]),
            Some(1)
        );
        assert_eq!(
            persisted_insertion_index_of_visible_slot(1, &s, &[7]),
            Some(2),
            "past the only drawn row is the append slot"
        );
    }

    /// A slot is a gap, so the one past the last row is a real destination -
    /// which is exactly what resolving it as a ROW refuses.
    #[test]
    fn the_slot_past_the_last_row_is_the_append_slot() {
        let mut s = space("A", 0);
        s.tabs = vec![tab("a"), grouped("g", "G"), tab("b")];
        assert_eq!(
            persisted_insertion_index_of_visible_slot(0, &s, &[]),
            Some(0)
        );
        assert_eq!(
            persisted_insertion_index_of_visible_slot(1, &s, &[]),
            Some(2)
        );
        assert_eq!(
            persisted_insertion_index_of_visible_slot(2, &s, &[]),
            Some(3)
        );
        assert_eq!(
            persisted_insertion_index_of_visible_slot(3, &s, &[]),
            None
        );
    }

    /// A space whose tabs are all grouped renders nothing, and slot 0 is still
    /// where an incoming tab belongs.
    #[test]
    fn a_fully_grouped_space_still_has_an_append_slot() {
        let mut s = space("A", 0);
        s.tabs = vec![grouped("g", "G")];
        assert_eq!(
            persisted_insertion_index_of_visible_slot(0, &s, &[]),
            Some(1)
        );
        assert_eq!(persisted_index_of_visible(0, &s, &[]), None);
    }

    /// Sidebar order is `order`, not array order - and a write has to resolve
    /// the same space the surface rendered at that position.
    #[test]
    fn a_space_index_is_a_position_in_sidebar_order() {
        let doc = document(vec![space("second", 1), space("first", 0)]);
        assert_eq!(space_at(&doc, 0), Some(1));
        assert_eq!(space_at(&doc, 1), Some(0));
        assert_eq!(space_at(&doc, 2), None);
    }

    /// `set_active_space_index` counts in sidebar order, not array order.
    #[test]
    fn a_space_slot_is_a_position_in_sidebar_order() {
        let mut doc = document(vec![
            space("last", 2),
            space("first", 0),
            space("middle", 1),
        ]);
        doc.spaces[0].profile_id = "work".into();
        assert_eq!(
            space_slot_for_id(&doc, "id-last"),
            Some((2, "work".to_string())),
            "id-last is stored first but renders last"
        );
        assert_eq!(
            space_slot_for_id(&doc, "id-first"),
            Some((0, "p".to_string()))
        );
        assert_eq!(
            space_slot_for_id(&doc, "id-middle"),
            Some((1, "p".to_string()))
        );
        assert_eq!(space_slot_for_id(&doc, "id-gone"), None);
    }

    #[test]
    fn an_unknown_active_space_index_moves_nothing() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("a"), tab("b")];
        assert_eq!(active_space(&doc, Some(7)), None);
        assert_eq!(urls(&doc.spaces[0]), vec!["a", "b"]);
    }

    /// No index from the window means the document's own activeSpaceID answers.
    #[test]
    fn without_a_window_index_the_document_decides() {
        let mut doc = document(vec![space("A", 0), space("B", 1)]);
        doc.app_state.active_space_id = Some("id-B".into());
        assert_eq!(active_space(&doc, None), Some(1));
    }

    #[test]
    fn a_tab_moves_within_the_regular_list() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("a"), tab("b"), tab("c")];
        assert!(move_tab(&mut doc, 0, 0, 2, Section::Regular));
        assert_eq!(urls(&doc.spaces[0]), vec!["b", "c", "a"]);
    }

    /// A move that resolves back to where it started is not a move, and must
    /// not dirty the file.
    #[test]
    fn a_move_that_goes_nowhere_reports_no_change() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("a"), tab("b")];
        assert!(!move_tab(&mut doc, 0, 1, 9, Section::Regular));
        assert!(!move_tab(&mut doc, 0, 5, 0, Section::Regular));
        assert_eq!(urls(&doc.spaces[0]), vec!["a", "b"]);
    }

    #[test]
    fn pinning_lands_at_the_slot_and_captures_the_url() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].pinned_tabs = vec![tab("p")];
        doc.spaces[0].tabs = vec![tab("a"), grouped("g", "G"), grouped("h", "G")];
        doc.spaces[0].groups = vec![TabGroup {
            id: "G".into(),
            name: "Reading".into(),
            collapsed: false,
            icon: None,
        }];

        assert!(pin_tab(&mut doc, 0, 1, Some(0)));
        let s = &doc.spaces[0];
        assert_eq!(s.pinned_tabs[0].url, "g");
        assert_eq!(s.pinned_tabs[0].pinned_url.as_deref(), Some("g"));
        assert_eq!(
            s.pinned_tabs[0].group_id, None,
            "a lone pin leaves its group"
        );
        assert_eq!(
            s.tabs[1].group_id.as_deref(),
            Some("G"),
            "the sibling stays"
        );
        assert!(matches!(pinned_items(s)[0], PinnedItem::Tab(0)));
    }

    /// A pinned slot counts ITEMS, so the slot after a two-member folder is
    /// slot 1 and lands past both of its entries.
    #[test]
    fn a_pin_slot_counts_a_folder_as_one_item() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        doc.spaces[0].pinned_tabs = vec![grouped("g0", "G"), grouped("g1", "G"), tab("p")];
        doc.spaces[0].tabs = vec![tab("a")];

        assert!(pin_tab(&mut doc, 0, 0, Some(1)));
        assert_eq!(
            pinned_urls(&doc.spaces[0]),
            vec!["g0", "g1", "a", "p"],
            "slot 1 is after the folder, not inside it"
        );
    }

    #[test]
    fn the_pinned_item_list_collapses_a_group_run() {
        let mut s = space("A", 0);
        s.groups = vec![group("G")];
        s.pinned_tabs = vec![tab("p0"), grouped("g0", "G"), grouped("g1", "G"), tab("p1")];
        assert_eq!(
            crate::spaces::pinned_items(&s),
            vec![
                PinnedItem::Tab(0),
                PinnedItem::Folder(0),
                PinnedItem::Tab(3)
            ]
        );
        assert_eq!(pinned_index_of_item(&s, 1), Some(1));
        assert_eq!(pinned_index_of_item(&s, 2), Some(3));
        assert_eq!(pinned_index_of_item(&s, 3), None);
        assert_eq!(pinned_tab_index_of_item(&s, 0), Some(0));
        assert_eq!(
            pinned_tab_index_of_item(&s, 1),
            None,
            "item 1 is the folder"
        );
        assert_eq!(pinned_tab_index_of_item(&s, 2), Some(3));
        assert_eq!(pinned_tab_index_of_item(&s, 3), None, "past the last item");
        assert_eq!(pinned_insertion_index_of_item_slot(&s, 2), 3);
        assert_eq!(
            pinned_insertion_index_of_item_slot(&s, 3),
            4,
            "a slot past the last item appends"
        );
    }

    #[test]
    fn a_pinned_folder_moves_with_every_member_it_holds() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        doc.spaces[0].pinned_tabs =
            vec![tab("p0"), grouped("g0", "G"), grouped("g1", "G"), tab("p1")];

        assert!(reorder_pinned_item(&mut doc, 0, 1, 2));
        assert_eq!(pinned_urls(&doc.spaces[0]), vec!["p0", "p1", "g0", "g1"]);

        assert!(reorder_pinned_item(&mut doc, 0, 2, 0));
        assert_eq!(pinned_urls(&doc.spaces[0]), vec!["g0", "g1", "p0", "p1"]);
    }

    #[test]
    fn a_pinned_item_move_that_goes_nowhere_reports_no_change() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        doc.spaces[0].pinned_tabs = vec![grouped("g0", "G"), grouped("g1", "G"), tab("p")];

        assert!(
            !reorder_pinned_item(&mut doc, 0, 1, 9),
            "clamped destination is the source"
        );
        assert!(!reorder_pinned_item(&mut doc, 0, 2, 0), "no such item");
        assert_eq!(pinned_urls(&doc.spaces[0]), vec!["g0", "g1", "p"]);
    }

    /// A folder is not a row, so the gesture that demotes one row refuses it
    /// rather than taking the folder's first member with it.
    #[test]
    fn unpinning_addresses_an_item_and_refuses_a_folder() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        doc.spaces[0].pinned_tabs = vec![grouped("g0", "G"), tab("p")];

        assert!(!unpin_tab(&mut doc, 0, 0, Some(0)), "item 0 is the folder");
        assert!(unpin_tab(&mut doc, 0, 1, Some(0)));
        assert_eq!(urls(&doc.spaces[0]), vec!["p"]);
        assert_eq!(pinned_urls(&doc.spaces[0]), vec!["g0"]);
    }

    #[test]
    fn unpinning_clears_the_snap_back_url() {
        let mut doc = document(vec![space("A", 0)]);
        let mut p = tab("p");
        p.pinned_url = Some("p".into());
        doc.spaces[0].pinned_tabs = vec![p];
        doc.spaces[0].tabs = vec![tab("a")];

        assert!(unpin_tab(&mut doc, 0, 0, Some(0)));
        assert_eq!(urls(&doc.spaces[0]), vec!["p", "a"]);
        assert_eq!(doc.spaces[0].tabs[0].pinned_url, None);
        assert!(doc.spaces[0].pinned_tabs.is_empty());
    }

    /// A tab crossing to another space lands in its REGULAR list whatever
    /// section it left, and carries no group id with it.
    #[test]
    fn a_cross_space_move_lands_regular_and_ungrouped() {
        let mut doc = document(vec![space("A", 0), space("B", 1)]);
        doc.spaces[0].pinned_tabs = vec![tab("p")];
        doc.spaces[1].tabs = vec![tab("x"), tab("y")];

        assert!(move_tab_across_spaces(
            &mut doc,
            0,
            0,
            Section::Pinned,
            1,
            Some(1)
        ));
        assert!(doc.spaces[0].pinned_tabs.is_empty());
        assert_eq!(urls(&doc.spaces[1]), vec!["x", "p", "y"]);
        assert_eq!(doc.spaces[1].tabs[1].pinned_url, None);
        assert_eq!(doc.spaces[1].tabs[1].group_id, None);
    }

    /// A stale destination is clamped rather than dropped: landing at the end
    /// beats losing the move.
    #[test]
    fn a_stale_destination_appends_rather_than_dropping_the_move() {
        let mut doc = document(vec![space("A", 0), space("B", 1)]);
        doc.spaces[0].tabs = vec![tab("a")];
        doc.spaces[1].tabs = vec![tab("x")];
        assert!(move_tab_across_spaces(
            &mut doc,
            0,
            0,
            Section::Regular,
            1,
            Some(99)
        ));
        assert_eq!(urls(&doc.spaces[1]), vec!["x", "a"]);
    }

    fn five_tabs() -> Document {
        let mut doc = document(vec![space("A", 0), space("B", 1)]);
        doc.spaces[0].tabs = vec![tab("a"), tab("b"), tab("c"), tab("d"), tab("e")];
        doc.spaces[1].tabs = vec![tab("x"), tab("y")];
        doc
    }

    #[test]
    fn a_run_moves_down_the_list() {
        let mut doc = five_tabs();
        assert!(move_run(&mut doc, 0, &[0, 1], 0, Some(4)));
        assert_eq!(urls(&doc.spaces[0]), vec!["c", "d", "a", "b", "e"]);
    }

    #[test]
    fn a_run_moves_up_the_list() {
        let mut doc = five_tabs();
        assert!(move_run(&mut doc, 0, &[3, 4], 0, Some(1)));
        assert_eq!(urls(&doc.spaces[0]), vec!["a", "d", "e", "b", "c"]);
    }

    /// The members land in list order, whatever order the surface named them.
    #[test]
    fn a_run_keeps_its_members_in_list_order() {
        let mut doc = five_tabs();
        assert!(move_run(&mut doc, 0, &[1, 0], 0, None));
        assert_eq!(urls(&doc.spaces[0]), vec!["c", "d", "e", "a", "b"]);
    }

    /// A split's panes need not sit next to each other, and the move gathers
    /// them at the drop.
    #[test]
    fn a_run_of_non_adjacent_members_lands_together() {
        let mut doc = five_tabs();
        assert!(move_run(&mut doc, 0, &[0, 2], 0, Some(4)));
        assert_eq!(urls(&doc.spaces[0]), vec!["b", "d", "a", "c", "e"]);
    }

    #[test]
    fn a_run_crosses_to_another_space_regular_and_ungrouped() {
        let mut doc = five_tabs();
        doc.spaces[0].tabs[3].group_id = Some("G".into());
        assert!(move_run(&mut doc, 0, &[1, 3], 1, Some(1)));
        assert_eq!(urls(&doc.spaces[0]), vec!["a", "c", "e"]);
        assert_eq!(urls(&doc.spaces[1]), vec!["x", "b", "d", "y"]);
        assert_eq!(doc.spaces[1].tabs[2].group_id, None);
    }

    #[test]
    fn a_run_dropped_on_itself_reports_no_change() {
        let mut doc = five_tabs();
        assert!(!move_run(&mut doc, 0, &[1, 2], 0, Some(1)));
        assert!(!move_run(&mut doc, 0, &[1, 2], 0, Some(3)));
        assert_eq!(urls(&doc.spaces[0]), vec!["a", "b", "c", "d", "e"]);
    }

    /// Every index resolves before anything moves, so one stale slot leaves the
    /// whole document as it was.
    #[test]
    fn a_bad_member_leaves_the_state_unchanged() {
        let mut doc = five_tabs();
        assert!(!move_run(&mut doc, 0, &[0, 9], 1, None));
        assert!(!move_run(&mut doc, 0, &[1, 1], 1, None));
        assert!(!move_run(&mut doc, 0, &[], 1, None));
        assert!(!move_run(&mut doc, 0, &[0], 7, None));
        assert_eq!(urls(&doc.spaces[0]), vec!["a", "b", "c", "d", "e"]);
        assert_eq!(urls(&doc.spaces[1]), vec!["x", "y"]);
    }

    #[test]
    fn a_space_moves_to_a_new_sidebar_position_and_the_rest_renumber() {
        // Deliberately sparse and out of insertion order, which is what an
        // older file looks like - the backing array is not sidebar order.
        let mut doc = document(vec![space("B", 30), space("A", 10), space("C", 20)]);
        let names = |d: &Document| -> Vec<String> {
            ordered_positions(&d.spaces)
                .into_iter()
                .map(|i| d.spaces[i].name.clone())
                .collect()
        };
        assert_eq!(names(&doc), vec!["A", "C", "B"]);

        // Move A to the end.
        assert!(reorder_space(&mut doc, 0, 2));
        assert_eq!(names(&doc), vec!["C", "B", "A"]);

        // Every space is renumbered from zero, so a file with duplicate or
        // sparse orders cannot leave the rest ambiguous after one move.
        let mut orders: Vec<i64> = doc.spaces.iter().map(|s| s.order).collect();
        orders.sort();
        assert_eq!(orders, vec![0, 1, 2]);
    }

    #[test]
    fn a_space_move_that_goes_nowhere_does_not_dirty_the_file() {
        let mut doc = document(vec![space("A", 0), space("B", 1)]);
        assert!(!reorder_space(&mut doc, 1, 1), "same position");
        assert!(!reorder_space(&mut doc, 0, 9), "a position the model lost");
        assert!(!reorder_space(&mut doc, 9, 0));
    }

    #[test]
    fn a_pinned_row_can_be_deleted_rather_than_unpinned() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].pinned_tabs = vec![tab("p0"), tab("p1")];

        assert!(remove_pinned_tab(&mut doc, 0, 0));
        let left: Vec<&str> = doc.spaces[0]
            .pinned_tabs
            .iter()
            .map(|t| t.url.as_str())
            .collect();
        assert_eq!(left, vec!["p1"]);
        // unpin_tab would have moved it here instead. Removing must not.
        assert!(
            doc.spaces[0].tabs.is_empty(),
            "a removed pin is gone, not demoted"
        );

        assert!(
            !remove_pinned_tab(&mut doc, 0, 9),
            "an index the model lost"
        );
        assert!(!remove_pinned_tab(&mut doc, 9, 0), "a space the model lost");
    }

    #[test]
    fn creating_a_space_appends_it_after_the_highest_order() {
        let mut doc = document(vec![space("A", 0), space("B", 3)]);
        let id = create_space(&mut doc, "New Space", "Default").unwrap();
        assert_eq!(doc.spaces.len(), 3);
        let created = &doc.spaces[2];
        assert_eq!(created.id, id);
        assert_eq!(created.name, "New Space");
        assert_eq!(created.profile_id, "Default");
        assert_eq!(created.order, 4);
        assert!(created.tabs.is_empty());
    }

    /// Two spaces created in the same document cannot collide - the crate has
    /// no RNG, so uniqueness has to come from the counter instead.
    #[test]
    fn two_created_spaces_get_different_ids() {
        let mut doc = document(vec![space("A", 0)]);
        let first = create_space(&mut doc, "One", "Default").unwrap();
        let second = create_space(&mut doc, "Two", "Default").unwrap();
        assert_ne!(first, second);
    }

    #[test]
    fn creating_a_space_lands_last_in_sidebar_order() {
        let mut doc = document(vec![space("A", 0)]);
        create_space(&mut doc, "New Space", "Default");
        assert_eq!(space_at(&doc, 1), Some(1));
    }

    #[test]
    fn creating_a_space_past_the_cap_is_refused() {
        let mut doc = document(vec![space("A", 0)]);
        while doc.spaces.len() < MAX_SPACES {
            create_space(&mut doc, "Space", "Default").unwrap();
        }
        assert_eq!(create_space(&mut doc, "One Too Many", "Default"), None);
        assert_eq!(doc.spaces.len(), MAX_SPACES);
    }

    /// A row is found by its identity wherever a gesture has since moved it -
    /// which is the whole reason the binding holds a token and not an index.
    #[test]
    fn a_row_is_located_by_token_after_it_has_moved() {
        let mut doc = tokened(vec![space("A", 0), space("B", 1)]);
        doc.spaces[0].tabs = vec![tab("a"), tab("b")];
        assign_tokens(&mut doc);
        let token = doc.spaces[0].tabs[0].token;

        assert_eq!(locate_token(&doc, token), Some((0, Section::Regular, 0)));
        assert!(move_tab(&mut doc, 0, 0, 1, Section::Regular));
        assert_eq!(locate_token(&doc, token), Some((0, Section::Regular, 1)));
        assert!(pin_tab(&mut doc, 0, 1, None));
        assert_eq!(locate_token(&doc, token), Some((0, Section::Pinned, 0)));
        assert!(move_tab_across_spaces(
            &mut doc,
            0,
            0,
            Section::Pinned,
            1,
            None
        ));
        assert_eq!(locate_token(&doc, token), Some((1, Section::Regular, 0)));
        assert_eq!(locate_token(&doc, UNTOKENED), None, "no identity, no row");
    }

    #[test]
    fn the_active_space_is_recorded_by_sidebar_position() {
        let mut doc = document(vec![space("second", 1), space("first", 0)]);
        assert!(set_active_space(&mut doc, 1));
        assert_eq!(doc.app_state.active_space_id.as_deref(), Some("id-second"));
        assert!(!set_active_space(&mut doc, 1), "already there");
        assert!(
            !set_active_space(&mut doc, 4),
            "out of range is ignored, not clamped to a neighbour"
        );
        assert_eq!(doc.app_state.active_space_id.as_deref(), Some("id-second"));
    }

    #[test]
    fn rename_space_stores_the_new_name() {
        let mut doc = tokened(vec![space("Work", 0)]);

        assert!(rename_space(&mut doc, 0, "Personal"));
        assert_eq!(doc.spaces[0].name, "Personal");
    }

    #[test]
    fn rename_space_rejects_an_empty_name() {
        let mut doc = tokened(vec![space("Work", 0)]);

        assert!(!rename_space(&mut doc, 0, ""));
        assert_eq!(doc.spaces[0].name, "Work");
    }

    #[test]
    fn rename_space_does_nothing_for_an_unknown_space() {
        let mut doc = tokened(vec![space("Work", 0)]);

        assert!(!rename_space(&mut doc, 9, "Personal"));
    }

    #[test]
    fn rename_space_is_a_no_op_when_unchanged() {
        let mut doc = tokened(vec![space("Work", 0)]);

        assert!(!rename_space(&mut doc, 0, "Work"));
    }

    #[test]
    fn setting_a_space_icon_round_trips_and_clears() {
        let mut doc = tokened(vec![space("Work", 0)]);

        assert!(set_space_icon(&mut doc, 0, "⭐"));
        assert_eq!(doc.spaces[0].emoji, "⭐");
        assert!(
            !set_space_icon(&mut doc, 0, "⭐"),
            "the same icon twice is not a change"
        );
        assert!(set_space_icon(&mut doc, 0, ""), "empty clears it");
        assert_eq!(doc.spaces[0].emoji, "");
        assert!(!set_space_icon(&mut doc, 9, "⭐"));
    }

    /// a row the window draws as a favourite tile is not in the
    /// visible list it sends back, so the index space has to skip it here too
    /// or a gesture on the row after it lands one row short.
    #[test]
    fn a_hidden_row_is_skipped_by_the_visible_index() {
        let mut s = space("A", 0);
        s.tabs = vec![tab("https://a/"), tab("https://b/"), tab("https://c/")];
        for (i, tab) in s.tabs.iter_mut().enumerate() {
            tab.token = (i + 1) as RowToken;
        }
        let hidden = [2 as RowToken];
        assert_eq!(persisted_index_of_visible(0, &s, &hidden), Some(0));
        assert_eq!(persisted_index_of_visible(1, &s, &hidden), Some(2));
        assert_eq!(persisted_index_of_visible(2, &s, &hidden), None);
    }

    #[test]
    fn a_settled_width_is_stored() {
        let mut doc = tokened(vec![space("A", 0)]);
        assert!(set_sidebar_width(&mut doc, 300.0));
        assert_eq!(doc.app_state.sidebar_width, 300.0);
    }

    /// Clamped above, and REFUSED below. A caller sending 0 means collapsed,
    /// which is session state - storing it as a width would make a re-expand
    /// return to nothing.
    #[test]
    fn a_width_outside_the_range_is_clamped_or_refused() {
        let mut doc = tokened(vec![space("A", 0)]);

        assert!(set_sidebar_width(&mut doc, 9999.0));
        assert_eq!(doc.app_state.sidebar_width, MAX_SIDEBAR_WIDTH, "clamped");

        assert!(
            !set_sidebar_width(&mut doc, 0.0),
            "a collapse is not a width"
        );
        assert!(
            !set_sidebar_width(&mut doc, 12.0),
            "below the floor is refused"
        );
        assert!(!set_sidebar_width(&mut doc, f64::NAN), "not a number");
        assert_eq!(doc.app_state.sidebar_width, MAX_SIDEBAR_WIDTH);
    }

    /// A drag that ends where it started must not dirty the file.
    #[test]
    fn an_unchanged_width_is_not_a_write() {
        let mut doc = tokened(vec![space("A", 0)]);
        assert!(set_sidebar_width(&mut doc, 260.0));
        assert!(!set_sidebar_width(&mut doc, 260.0));
    }

    #[test]
    fn a_width_at_the_new_maximum_survives_unchanged() {
        let mut doc = tokened(vec![space("A", 0)]);
        assert!(set_sidebar_width(&mut doc, 500.0));
        assert_eq!(doc.app_state.sidebar_width, 500.0);
        assert!(!set_sidebar_width(&mut doc, 500.0));
        assert_eq!(doc.app_state.sidebar_width, 500.0);
    }

    #[test]
    fn a_width_above_the_new_maximum_clamps_to_it() {
        let mut doc = tokened(vec![space("A", 0)]);
        assert!(set_sidebar_width(&mut doc, 640.0));
        assert_eq!(doc.app_state.sidebar_width, 500.0);
    }

    /// The writer accepts EXACTLY what the reader parses, both directions,
    /// driven off `ALL` rather than a copied list.
    #[test]
    fn the_writer_accepts_exactly_the_spellings_the_reader_parses() {
        use crate::choices::ARCHIVE_THRESHOLDS;

        for wire in ARCHIVE_THRESHOLDS {
            let mut doc = tokened(vec![space("Work", 0)]);
            assert!(
                set_space_archive_ttl(&mut doc, 0, Some(wire)),
                "the reader parses {wire} but the writer refused it"
            );
        }

        // The other direction: a spelling the reader cannot parse is refused,
        // and refusing leaves the stored value alone.
        let mut doc = tokened(vec![space("Work", 0)]);
        assert!(set_space_archive_ttl(&mut doc, 0, Some("thirtyDays")));
        assert!(!set_space_archive_ttl(&mut doc, 0, Some("nextWeek")));
        assert_eq!(
            Some("thirtyDays"),
            doc.spaces[0].archive_ttl.as_deref(),
            "a refused write must not disturb what is there"
        );
    }

    #[test]
    fn set_space_archive_ttl_stores_a_valid_rung() {
        let mut doc = tokened(vec![space("Work", 0)]);

        assert!(set_space_archive_ttl(&mut doc, 0, Some("thirtyDays")));
        assert_eq!(doc.spaces[0].archive_ttl, Some("thirtyDays".to_string()));
    }

    #[test]
    fn set_space_archive_ttl_accepts_never() {
        let mut doc = tokened(vec![space("Work", 0)]);

        assert!(set_space_archive_ttl(&mut doc, 0, Some("never")));
        assert_eq!(doc.spaces[0].archive_ttl, Some("never".to_string()));
    }

    #[test]
    fn set_space_archive_ttl_clears_to_follow_general() {
        let mut doc = tokened(vec![space("Work", 0)]);
        doc.spaces[0].archive_ttl = Some("twelveHours".to_string());

        assert!(set_space_archive_ttl(&mut doc, 0, None));
        assert_eq!(doc.spaces[0].archive_ttl, None);

        doc.spaces[0].archive_ttl = Some("twelveHours".to_string());
        assert!(set_space_archive_ttl(&mut doc, 0, Some("")));
        assert_eq!(doc.spaces[0].archive_ttl, None);
    }

    #[test]
    fn set_space_archive_ttl_rejects_an_unrecognised_rung() {
        let mut doc = tokened(vec![space("Work", 0)]);

        assert!(!set_space_archive_ttl(&mut doc, 0, Some("nextWeek")));
        assert_eq!(doc.spaces[0].archive_ttl, None);
    }

    #[test]
    fn set_space_archive_ttl_does_nothing_for_an_unknown_space() {
        let mut doc = tokened(vec![space("Work", 0)]);

        assert!(!set_space_archive_ttl(&mut doc, 9, Some("sevenDays")));
    }

    #[test]
    fn set_space_archive_ttl_is_a_no_op_when_unchanged() {
        let mut doc = tokened(vec![space("Work", 0)]);
        doc.spaces[0].archive_ttl = Some("sevenDays".to_string());

        assert!(!set_space_archive_ttl(&mut doc, 0, Some("sevenDays")));
    }

    #[test]
    fn set_space_emoji_sets_a_new_value_and_refuses_the_rest() {
        let mut doc = test_support::document(vec![test_support::space("A", 0)]);
        assert!(set_space_emoji(&mut doc, 0, "Z"));
        assert_eq!(doc.spaces[0].emoji, "Z");
        assert!(!set_space_emoji(&mut doc, 0, "Z"), "the same value is no change");
        assert!(!set_space_emoji(&mut doc, 0, ""), "an empty emoji is refused");
        assert!(!set_space_emoji(&mut doc, 7, "Q"), "an unknown space is refused");
    }
}
