// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Tab groups, and the row rename that shares their shape.
//!
//! A group is a label on rows that stay in the space's one `tabs` array - not a
//! container - which is why deleting a group ungroups its rows rather than
//! closing them, and why every index here is translated before it addresses
//! anything.
//!
//! `rename_tab` lives here because renaming a row and renaming a group are the
//! same operation on different targets: both set a custom title that survives
//! the page's own title changing.

use super::{
    Section, persisted_insertion_index_of_visible_slot, pin_tab,
    pinned_insertion_index_of_item_slot, truncate_to,
};
use crate::spaces::{Document, PinnedItem, TabGroup, pinned_items};

/// A generous cap on a row's custom title, matching the short
/// labels `settings.rs` bounds. Truncated rather than refused - see
/// `notes::MAX_NOTE_BODY_LEN` for why.
const MAX_TAB_NAME_LEN: usize = 200;

/// Puts a row into `group_id`, or ungroups it on `None`. `index` counts
/// `space.tabs`, or pinned ITEMS for `Section::Pinned`. `member_slot` places a
/// joining pin at that member position, clamped - `None` keeps the old last.
pub fn assign_to_group(
    doc: &mut Document,
    space: usize,
    section: Section,
    index: usize,
    group_id: Option<&str>,
    member_slot: Option<usize>,
) -> bool {
    let Some(space) = doc.spaces.get_mut(space) else {
        return false;
    };
    if let Some(id) = group_id {
        if !space.groups.iter().any(|g| g.id == id) {
            return false;
        }
        // A group's members live in one list or the other, never both: a
        // pinned run IS the folder, so a split group would render twice.
        let elsewhere = match section {
            Section::Regular => &space.pinned_tabs,
            Section::Pinned => &space.tabs,
        };
        if elsewhere.iter().any(|t| t.group_id.as_deref() == Some(id)) {
            return false;
        }
    }
    let wanted = group_id.map(str::to_string);
    let at = match section {
        Section::Regular => {
            if index >= space.tabs.len() {
                return false;
            }
            index
        }
        Section::Pinned => match pinned_items(space).get(index).copied() {
            Some(PinnedItem::Tab(at)) => at,
            _ => return false,
        },
    };
    let list = match section {
        Section::Regular => &mut space.tabs,
        Section::Pinned => &mut space.pinned_tabs,
    };
    if list[at].group_id == wanted {
        return false;
    }
    // A joining pin has to land beside another member, because members of one
    // group have to stay contiguous for the folder to be one item. Regular
    // members need not be contiguous, so only Pinned repositions at all.
    let dest = wanted
        .as_deref()
        .filter(|_| section == Section::Pinned)
        .and_then(|id| {
            let members: Vec<usize> = list
                .iter()
                .enumerate()
                .filter(|(_, t)| t.group_id.as_deref() == Some(id))
                .map(|(index, _)| index)
                .collect();
            let last = *members.last()?;
            let wanted_slot = member_slot.unwrap_or(members.len());
            Some(members.get(wanted_slot).copied().unwrap_or(last + 1))
        });
    list[at].group_id = wanted;
    if let Some(dest) = dest {
        let tab = list.remove(at);
        let dest = if at < dest { dest - 1 } else { dest };
        list.insert(dest, tab);
    }
    true
}

/// Moves the member at `from` of `group_id` to `to` among that group's own
/// members, in whichever list holds them. `to` is a POST-REMOVAL position,
/// clamped the way `move_tab` clamps its own.
pub fn reorder_group_member(
    doc: &mut Document,
    space: usize,
    group_id: &str,
    from: usize,
    to: usize,
) -> bool {
    let Some(s) = doc.spaces.get_mut(space) else {
        return false;
    };
    let positions = |list: &[crate::spaces::SpaceTab]| -> Vec<usize> {
        list.iter()
            .enumerate()
            .filter(|(_, t)| t.group_id.as_deref() == Some(group_id))
            .map(|(index, _)| index)
            .collect()
    };
    let list = if positions(&s.pinned_tabs).is_empty() {
        &mut s.tabs
    } else {
        &mut s.pinned_tabs
    };
    let members = positions(list);
    if from >= members.len() {
        return false;
    }
    let clamped = to.min(members.len() - 1);
    if clamped == from {
        return false;
    }
    let tab = list.remove(members[from]);
    // Regular members need not be contiguous, so the landing index is read
    // off the members that remain rather than computed from `from`.
    let rest = positions(list);
    let dest = rest
        .get(clamped)
        .copied()
        .unwrap_or_else(|| rest.last().map_or(list.len(), |last| last + 1));
    list.insert(dest, tab);
    true
}

/// Takes the member at `member_index` out of `group_id` and lands it in `to`
/// at `to_slot` - a pinned ITEM slot for `Section::Pinned`, a visible row slot
/// for `Section::Regular`.
pub fn ungroup_member(
    doc: &mut Document,
    space: usize,
    group_id: &str,
    member_index: usize,
    to: Section,
    to_slot: usize,
) -> bool {
    let Some(s) = doc.spaces.get(space) else {
        return false;
    };
    let member_of = |list: &[crate::spaces::SpaceTab]| {
        list.iter()
            .enumerate()
            .filter(|(_, t)| t.group_id.as_deref() == Some(group_id))
            .map(|(index, _)| index)
            .nth(member_index)
    };
    let pinned = member_of(&s.pinned_tabs);
    let regular = member_of(&s.tabs);

    match (pinned, regular, to) {
        (Some(at), _, Section::Pinned) => {
            // `to_slot` counts the items the surface drew. A last member takes
            // its folder item with it, so every later slot moves up by one.
            let folder_item = pinned_items(s).iter().position(
                |item| matches!(item, PinnedItem::Folder(g) if s.groups[*g].id == group_id),
            );
            let last_member = s
                .pinned_tabs
                .iter()
                .filter(|t| t.group_id.as_deref() == Some(group_id))
                .count()
                == 1;
            let to_slot = match folder_item {
                Some(folder) if last_member && to_slot > folder => to_slot - 1,
                _ => to_slot,
            };
            let s = &mut doc.spaces[space];
            let mut tab = s.pinned_tabs.remove(at);
            tab.group_id = None;
            let dest = pinned_insertion_index_of_item_slot(s, to_slot);
            s.pinned_tabs.insert(dest, tab);
            true
        }
        (Some(at), _, Section::Regular) => {
            let Some(dest) = persisted_insertion_index_of_visible_slot(
                to_slot,
                &doc.spaces[space],
                &doc.hidden_rows,
            ) else {
                return false;
            };
            let s = &mut doc.spaces[space];
            let mut tab = s.pinned_tabs.remove(at);
            tab.group_id = None;
            // The reverse of pin_tab's capture, the same as unpin_tab's.
            tab.pinned_url = None;
            s.tabs.insert(dest, tab);
            true
        }
        (None, Some(at), Section::Regular) => {
            let Some(dest) = persisted_insertion_index_of_visible_slot(
                to_slot,
                &doc.spaces[space],
                &doc.hidden_rows,
            ) else {
                return false;
            };
            let s = &mut doc.spaces[space];
            let mut tab = s.tabs.remove(at);
            tab.group_id = None;
            s.tabs.insert(if at < dest { dest - 1 } else { dest }, tab);
            true
        }
        (None, Some(at), Section::Pinned) => {
            doc.spaces[space].tabs[at].group_id = None;
            pin_tab(doc, space, at, Some(to_slot))
        }
        (None, None, _) => false,
    }
}

/// Pins every member of a regular-tab group, in order, at pinned item slot
/// `to_item_slot`. A group that already has a pinned member is refused - the
/// caller reorders that one instead.
pub fn pin_folder(doc: &mut Document, space: usize, group_id: &str, to_item_slot: usize) -> bool {
    let Some(s) = doc.spaces.get(space) else {
        return false;
    };
    if !s.groups.iter().any(|g| g.id == group_id) {
        return false;
    }
    if s.pinned_tabs
        .iter()
        .any(|t| t.group_id.as_deref() == Some(group_id))
    {
        return false;
    }
    let members: Vec<usize> = s
        .tabs
        .iter()
        .enumerate()
        .filter(|(_, t)| t.group_id.as_deref() == Some(group_id))
        .map(|(index, _)| index)
        .collect();
    if members.is_empty() {
        return false;
    }
    let at = pinned_insertion_index_of_item_slot(s, to_item_slot);

    let s = &mut doc.spaces[space];
    let mut moved: Vec<crate::spaces::SpaceTab> = members
        .iter()
        .rev()
        .map(|&index| s.tabs.remove(index))
        .collect();
    moved.reverse();
    for tab in &mut moved {
        tab.pinned_url = Some(tab.url.clone());
    }
    s.pinned_tabs.splice(at..at, moved);
    true
}

/// Sets whether a tab group is folded. An unknown group id does nothing.
pub fn set_group_collapsed(
    doc: &mut Document,
    space: usize,
    group_id: &str,
    collapsed: bool,
) -> bool {
    let Some(space) = doc.spaces.get_mut(space) else {
        return false;
    };
    let Some(group) = space.groups.iter_mut().find(|g| g.id == group_id) else {
        return false;
    };
    if group.collapsed == collapsed {
        return false;
    }
    group.collapsed = collapsed;
    true
}

/// A space's own folder cap. Refused rather than dropping the oldest folder,
/// since a folder holds tabs a drop would scatter.
const MAX_GROUPS_PER_SPACE: usize = 200;

/// Creates a new, empty tab group in `space`, returning its id.
///
/// The id comes from the document's own token counter - the crate has no RNG.
pub fn create_group(doc: &mut Document, space: usize, name: &str) -> Option<String> {
    if space >= doc.spaces.len() || doc.spaces[space].groups.len() >= MAX_GROUPS_PER_SPACE {
        return None;
    }
    let id = format!("group-{}", doc.allocate_token());
    doc.spaces[space].groups.push(TabGroup {
        id: id.clone(),
        name: name.to_string(),
        collapsed: false,
        icon: None,
    });
    Some(id)
}

/// Renames a tab group. An unknown id, or an empty name, does nothing.
/// Renames a row by setting its `custom_title`, which the model already reads
/// (sidebar.rs).
///
/// An EMPTY name clears the rename rather than storing a blank, so a row falls
/// back to the page's own title - that is the only way back from a rename, and
/// the lab's inline field offers it by submitting empty.
pub fn rename_tab(
    doc: &mut Document,
    space: usize,
    section: Section,
    index: usize,
    name: &str,
) -> bool {
    let Some(space) = doc.spaces.get_mut(space) else {
        return false;
    };
    let list = match section {
        Section::Pinned => &mut space.pinned_tabs,
        Section::Regular => &mut space.tabs,
    };
    let Some(tab) = list.get_mut(index) else {
        return false;
    };
    let next = if name.is_empty() {
        None
    } else {
        Some(truncate_to(name, MAX_TAB_NAME_LEN).into_owned())
    };
    if tab.custom_title == next {
        return false;
    }
    tab.custom_title = next;
    true
}

pub fn rename_group(doc: &mut Document, space: usize, group_id: &str, name: &str) -> bool {
    if name.is_empty() {
        return false;
    }
    let Some(space) = doc.spaces.get_mut(space) else {
        return false;
    };
    let Some(group) = space.groups.iter_mut().find(|g| g.id == group_id) else {
        return false;
    };
    if group.name == name {
        return false;
    }
    group.name = name.to_string();
    true
}

/// Deletes a tab group. Its tabs rejoin the flat list rather than closing, and
/// its pinned members stay pinned where they are.
pub fn delete_group(doc: &mut Document, space: usize, group_id: &str) -> bool {
    let Some(space) = doc.spaces.get_mut(space) else {
        return false;
    };
    let before = space.groups.len();
    space.groups.retain(|g| g.id != group_id);
    if space.groups.len() == before {
        return false;
    }
    for tab in space.tabs.iter_mut().chain(space.pinned_tabs.iter_mut()) {
        if tab.group_id.as_deref() == Some(group_id) {
            tab.group_id = None;
        }
    }
    true
}

/// Sets a group's icon glyph. An empty string clears it. An unknown group id
/// does nothing.
pub fn set_group_icon(doc: &mut Document, space: usize, group_id: &str, icon: &str) -> bool {
    let Some(space) = doc.spaces.get_mut(space) else {
        return false;
    };
    let Some(group) = space.groups.iter_mut().find(|g| g.id == group_id) else {
        return false;
    };
    let wanted = if icon.is_empty() {
        None
    } else {
        Some(icon.to_string())
    };
    if group.icon == wanted {
        return false;
    }
    group.icon = wanted;
    true
}

/// Moves a folder within its space's own list. `to` is a POST-REMOVAL
/// position, clamped into range - the same contract `move_tab` gives its `to`.
pub fn move_group(doc: &mut Document, space: usize, from: usize, to: usize) -> bool {
    let Some(space) = doc.spaces.get_mut(space) else {
        return false;
    };
    if from >= space.groups.len() {
        return false;
    }
    let clamped = to.min(space.groups.len() - 1);
    if clamped == from {
        return false;
    }
    let group = space.groups.remove(from);
    space.groups.insert(clamped, group);
    true
}

/// Sets whether a space's pinned SECTION is folded.
pub fn set_pinned_collapsed(doc: &mut Document, space: usize, collapsed: bool) -> bool {
    let Some(space) = doc.spaces.get_mut(space) else {
        return false;
    };
    if space.pinned_collapsed == collapsed {
        return false;
    }
    space.pinned_collapsed = collapsed;
    true
}

#[cfg(test)]
mod tests {
    use super::*;
    #[allow(unused_imports)]
    use crate::mutate::test_support::*;
    use crate::spaces::assign_tokens;
    // Test-only: production here addresses rows by group, not by position.
    use super::super::persisted_index_of_visible;

    #[test]
    fn a_row_takes_a_custom_title_and_an_empty_name_clears_it() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("one"), tab("two")];
        doc.spaces[0].pinned_tabs = vec![tab("pin")];
        assign_tokens(&mut doc);

        assert!(rename_tab(&mut doc, 0, Section::Regular, 1, "My Name"));
        assert_eq!(
            doc.spaces[0].tabs[1].custom_title.as_deref(),
            Some("My Name")
        );
        assert_eq!(
            doc.spaces[0].tabs[0].custom_title, None,
            "only the row named"
        );

        // Empty clears it, which is the only way back to the page's own title.
        assert!(rename_tab(&mut doc, 0, Section::Regular, 1, ""));
        assert_eq!(doc.spaces[0].tabs[1].custom_title, None);

        // Pinned rows are a separate list and address by their own index.
        assert!(rename_tab(&mut doc, 0, Section::Pinned, 0, "Pinned Name"));
        assert_eq!(
            doc.spaces[0].pinned_tabs[0].custom_title.as_deref(),
            Some("Pinned Name")
        );
    }

    #[test]
    fn an_overlong_rename_is_truncated_to_the_cap() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("one")];
        assign_tokens(&mut doc);

        let overlong = "n".repeat(MAX_TAB_NAME_LEN + 50);
        assert!(rename_tab(&mut doc, 0, Section::Regular, 0, &overlong));
        assert_eq!(
            doc.spaces[0].tabs[0].custom_title.as_ref().map(String::len),
            Some(MAX_TAB_NAME_LEN)
        );
    }

    #[test]
    fn a_rename_that_changes_nothing_does_not_dirty_the_file() {
        let mut doc = tokened(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("one")];
        assign_tokens(&mut doc);

        assert!(rename_tab(&mut doc, 0, Section::Regular, 0, "Same"));
        assert!(
            !rename_tab(&mut doc, 0, Section::Regular, 0, "Same"),
            "twice"
        );
        assert!(
            !rename_tab(&mut doc, 0, Section::Regular, 9, "x"),
            "no such row"
        );
        assert!(
            !rename_tab(&mut doc, 9, Section::Regular, 0, "x"),
            "no such space"
        );
    }

    /// An id that names no group is dropped. Treating it as "ungroup" would let
    /// a typo strip membership silently.
    #[test]
    fn an_unknown_group_id_does_not_ungroup() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![grouped("a", "G")];
        doc.spaces[0].groups = vec![TabGroup {
            id: "G".into(),
            name: "Reading".into(),
            collapsed: false,
            icon: None,
        }];

        assert!(!assign_to_group(
            &mut doc,
            0,
            Section::Regular,
            0,
            Some("typo"),
            None
        ));
        assert_eq!(doc.spaces[0].tabs[0].group_id.as_deref(), Some("G"));
        assert!(
            assign_to_group(&mut doc, 0, Section::Regular, 0, None, None),
            "empty ungroups"
        );
        assert_eq!(doc.spaces[0].tabs[0].group_id, None);
    }

    /// The index counts UNGROUPED rows, so with a group above it the assign
    /// must land on the third persisted tab and not the second.
    #[test]
    fn assigning_uses_the_translated_index() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("a"), grouped("g", "G"), tab("b")];
        doc.spaces[0].groups = vec![TabGroup {
            id: "G".into(),
            name: "Reading".into(),
            collapsed: false,
            icon: None,
        }];
        let index = persisted_index_of_visible(1, &doc.spaces[0], &doc.hidden_rows).unwrap();
        assert!(assign_to_group(
            &mut doc,
            0,
            Section::Regular,
            index,
            Some("G"),
            None
        ));
        assert_eq!(doc.spaces[0].tabs[2].group_id.as_deref(), Some("G"));
        assert_eq!(doc.spaces[0].tabs[0].group_id, None);
    }

    /// A pinned tab joining a folder lands beside its last member, because the
    /// folder is a RUN of the pinned array and a gap would split it in two.
    #[test]
    fn a_joining_pin_lands_beside_the_groups_last_member() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        doc.spaces[0].pinned_tabs = vec![grouped("g0", "G"), tab("p0"), tab("p1")];

        // Item 2 is p1: item 0 is the folder and item 1 is p0.
        assert!(assign_to_group(
            &mut doc,
            0,
            Section::Pinned,
            2,
            Some("G"),
            None
        ));
        assert_eq!(pinned_urls(&doc.spaces[0]), vec!["g0", "p1", "p0"]);
        assert_eq!(doc.spaces[0].pinned_tabs[1].group_id.as_deref(), Some("G"));
    }

    /// An empty group has no member to land beside, so the tab stays put.
    #[test]
    fn joining_an_empty_group_leaves_a_mid_list_tab_where_it_is() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        doc.spaces[0].pinned_tabs = vec![tab("p0"), tab("p1"), tab("p2"), tab("p3"), tab("p4")];

        assert!(assign_to_group(
            &mut doc,
            0,
            Section::Pinned,
            2,
            Some("G"),
            None
        ));
        assert_eq!(
            pinned_urls(&doc.spaces[0]),
            vec!["p0", "p1", "p2", "p3", "p4"]
        );
        assert_eq!(doc.spaces[0].pinned_tabs[2].group_id.as_deref(), Some("G"));
    }

    /// A drop above the only member has to land first, not beside it as the
    /// last (and only) one.
    #[test]
    fn a_member_slot_of_zero_joins_before_the_only_member() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        doc.spaces[0].pinned_tabs = vec![grouped("g0", "G"), tab("p0")];

        assert!(assign_to_group(
            &mut doc,
            0,
            Section::Pinned,
            1,
            Some("G"),
            Some(0)
        ));
        assert_eq!(pinned_urls(&doc.spaces[0]), vec!["p0", "g0"]);
    }

    /// A member slot naming the position right after every current member
    /// behaves exactly like the old "beside the last member" default.
    #[test]
    fn a_member_slot_at_the_member_count_joins_last() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        doc.spaces[0].pinned_tabs = vec![grouped("g0", "G"), tab("p0")];

        assert!(assign_to_group(
            &mut doc,
            0,
            Section::Pinned,
            1,
            Some("G"),
            Some(1)
        ));
        assert_eq!(pinned_urls(&doc.spaces[0]), vec!["g0", "p0"]);
    }

    /// A member slot past the member count clamps to last, the same as an
    /// out-of-range destination clamps everywhere else in this module.
    #[test]
    fn a_member_slot_past_the_end_clamps_to_last() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        doc.spaces[0].pinned_tabs = vec![grouped("g0", "G"), tab("p0")];

        assert!(assign_to_group(
            &mut doc,
            0,
            Section::Pinned,
            1,
            Some("G"),
            Some(99)
        ));
        assert_eq!(pinned_urls(&doc.spaces[0]), vec!["g0", "p0"]);
    }

    /// A group renders in one list or the other. Split across both it would
    /// draw twice, so the second half is refused rather than silently merged.
    #[test]
    fn a_group_refuses_members_in_both_lists() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        doc.spaces[0].pinned_tabs = vec![grouped("g0", "G"), tab("p")];
        doc.spaces[0].tabs = vec![tab("a")];

        assert!(!assign_to_group(
            &mut doc,
            0,
            Section::Regular,
            0,
            Some("G"),
            None
        ));
        assert_eq!(doc.spaces[0].tabs[0].group_id, None);

        doc.spaces[0].pinned_tabs[0].group_id = None;
        doc.spaces[0].tabs[0].group_id = Some("G".into());
        assert!(!assign_to_group(
            &mut doc,
            0,
            Section::Pinned,
            0,
            Some("G"),
            None
        ));
        assert_eq!(doc.spaces[0].pinned_tabs[0].group_id, None);
    }

    #[test]
    fn assigning_a_pinned_folder_item_is_refused() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G"), group("H")];
        doc.spaces[0].pinned_tabs = vec![grouped("g0", "G")];

        assert!(
            !assign_to_group(&mut doc, 0, Section::Pinned, 0, Some("H"), None),
            "item 0 is the folder, not a row"
        );
    }

    #[test]
    fn a_member_ungroups_into_the_pinned_list_at_the_item_slot() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        doc.spaces[0].pinned_tabs =
            vec![tab("p0"), grouped("g0", "G"), grouped("g1", "G"), tab("p1")];

        // The folder is item 1, so slot 0 is before p0.
        assert!(ungroup_member(&mut doc, 0, "G", 1, Section::Pinned, 0));
        assert_eq!(pinned_urls(&doc.spaces[0]), vec!["g1", "p0", "g0", "p1"]);
        assert_eq!(doc.spaces[0].pinned_tabs[0].group_id, None);
    }

    #[test]
    fn a_last_member_lands_at_the_slot_the_surface_drew() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        doc.spaces[0].pinned_tabs = vec![grouped("g0", "G"), tab("p0"), tab("p1")];

        // Slot 2 is before p1 in the drawn list, which still holds the folder.
        assert!(ungroup_member(&mut doc, 0, "G", 0, Section::Pinned, 2));
        assert_eq!(pinned_urls(&doc.spaces[0]), vec!["p0", "g0", "p1"]);
    }

    #[test]
    fn a_pinned_member_ungroups_out_to_the_regular_list() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        let mut member = grouped("g0", "G");
        member.pinned_url = Some("g0".into());
        doc.spaces[0].pinned_tabs = vec![member, grouped("g1", "G")];
        doc.spaces[0].tabs = vec![tab("a"), tab("b")];

        assert!(ungroup_member(&mut doc, 0, "G", 0, Section::Regular, 1));
        assert_eq!(urls(&doc.spaces[0]), vec!["a", "g0", "b"]);
        assert_eq!(doc.spaces[0].tabs[1].group_id, None);
        assert_eq!(
            doc.spaces[0].tabs[1].pinned_url, None,
            "a regular tab has no snap-back url, the same as unpinning"
        );
        assert_eq!(pinned_urls(&doc.spaces[0]), vec!["g1"]);
    }

    /// A folder in the `tabs` list keeps working the way it always did.
    #[test]
    fn a_regular_member_ungroups_to_a_visible_slot() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        doc.spaces[0].tabs = vec![tab("a"), grouped("g0", "G"), tab("b")];

        assert!(ungroup_member(&mut doc, 0, "G", 0, Section::Regular, 2));
        assert_eq!(urls(&doc.spaces[0]), vec!["a", "b", "g0"]);
        assert_eq!(doc.spaces[0].tabs[2].group_id, None);
    }

    #[test]
    fn a_member_reorders_within_its_pinned_folder() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        doc.spaces[0].pinned_tabs = vec![
            tab("p0"),
            grouped("g0", "G"),
            grouped("g1", "G"),
            grouped("g2", "G"),
            tab("p1"),
        ];

        assert!(reorder_group_member(&mut doc, 0, "G", 0, 2));
        assert_eq!(
            pinned_urls(&doc.spaces[0]),
            vec!["p0", "g1", "g2", "g0", "p1"]
        );
    }

    #[test]
    fn a_member_reorders_within_its_regular_folder_across_a_gap() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        doc.spaces[0].tabs = vec![
            grouped("g0", "G"),
            tab("a"),
            grouped("g1", "G"),
            grouped("g2", "G"),
        ];

        assert!(reorder_group_member(&mut doc, 0, "G", 2, 0));
        assert_eq!(urls(&doc.spaces[0]), vec!["g2", "g0", "a", "g1"]);
    }

    #[test]
    fn a_member_reorder_clamps_past_the_end_and_refuses_a_non_move() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        doc.spaces[0].tabs = vec![grouped("g0", "G"), grouped("g1", "G")];

        assert!(!reorder_group_member(&mut doc, 0, "G", 1, 1));
        assert!(!reorder_group_member(&mut doc, 0, "G", 5, 0));
        assert!(!reorder_group_member(&mut doc, 0, "nope", 0, 1));
        assert!(reorder_group_member(&mut doc, 0, "G", 0, 9));
        assert_eq!(urls(&doc.spaces[0]), vec!["g1", "g0"]);
    }

    #[test]
    fn ungrouping_a_member_the_group_does_not_have_does_nothing() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        doc.spaces[0].tabs = vec![grouped("g0", "G")];

        assert!(!ungroup_member(&mut doc, 0, "G", 3, Section::Regular, 0));
        assert!(!ungroup_member(&mut doc, 0, "nope", 0, Section::Regular, 0));
    }

    #[test]
    fn pinning_a_folder_moves_every_member_in_order() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        doc.spaces[0].pinned_tabs = vec![tab("p0")];
        doc.spaces[0].tabs = vec![grouped("g0", "G"), tab("a"), grouped("g1", "G")];

        assert!(pin_folder(&mut doc, 0, "G", 0));
        assert_eq!(pinned_urls(&doc.spaces[0]), vec!["g0", "g1", "p0"]);
        assert_eq!(urls(&doc.spaces[0]), vec!["a"]);
        assert!(
            doc.spaces[0]
                .pinned_tabs
                .iter()
                .take(2)
                .all(|t| t.group_id.as_deref() == Some("G")),
            "the members keep the membership that makes them one folder"
        );
        assert_eq!(
            doc.spaces[0].pinned_tabs[0].pinned_url.as_deref(),
            Some("g0")
        );
    }

    #[test]
    fn pinning_a_folder_twice_is_refused() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        doc.spaces[0].tabs = vec![grouped("g0", "G")];

        assert!(pin_folder(&mut doc, 0, "G", 0));
        assert!(
            !pin_folder(&mut doc, 0, "G", 0),
            "an already pinned folder is reordered, not pinned again"
        );
        assert!(!pin_folder(&mut doc, 0, "nope", 0), "no such group");
    }

    /// An empty folder has nothing to move, so the gesture leaves the file
    /// alone rather than reporting a change the surface would re-render for.
    #[test]
    fn pinning_an_empty_folder_changes_nothing() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];

        assert!(!pin_folder(&mut doc, 0, "G", 0));
    }

    /// Explicit state, not a toggle: the surface has already moved its own DOM
    /// and is reporting where it ended up.
    #[test]
    fn a_collapse_is_the_state_it_is_given() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![TabGroup {
            id: "G".into(),
            name: "Reading".into(),
            collapsed: false,
            icon: None,
        }];

        assert!(set_group_collapsed(&mut doc, 0, "G", true));
        assert!(
            !set_group_collapsed(&mut doc, 0, "G", true),
            "the same state twice is not a change"
        );
        assert!(doc.spaces[0].groups[0].collapsed);
        assert!(!set_group_collapsed(&mut doc, 0, "nope", true));

        assert!(set_pinned_collapsed(&mut doc, 0, true));
        assert!(!set_pinned_collapsed(&mut doc, 0, true));
        assert!(doc.spaces[0].pinned_collapsed);
    }

    #[test]
    fn a_group_moves_within_its_own_list() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![
            TabGroup {
                id: "A".into(),
                name: "A".into(),
                collapsed: false,
                icon: None,
            },
            TabGroup {
                id: "B".into(),
                name: "B".into(),
                collapsed: false,
                icon: None,
            },
            TabGroup {
                id: "C".into(),
                name: "C".into(),
                collapsed: false,
                icon: None,
            },
        ];
        assert!(move_group(&mut doc, 0, 0, 2));
        assert_eq!(
            doc.spaces[0]
                .groups
                .iter()
                .map(|g| g.id.as_str())
                .collect::<Vec<_>>(),
            vec!["B", "C", "A"]
        );
    }

    /// A move that resolves back to where it started is not a move, and must
    /// not dirty the file - the same rule `move_tab` follows.
    #[test]
    fn a_group_move_that_goes_nowhere_reports_no_change() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![
            TabGroup {
                id: "A".into(),
                name: "A".into(),
                collapsed: false,
                icon: None,
            },
            TabGroup {
                id: "B".into(),
                name: "B".into(),
                collapsed: false,
                icon: None,
            },
        ];
        assert!(
            !move_group(&mut doc, 0, 1, 9),
            "clamped destination is the source"
        );
        assert!(!move_group(&mut doc, 0, 5, 0), "no such group");
        assert_eq!(
            doc.spaces[0]
                .groups
                .iter()
                .map(|g| g.id.as_str())
                .collect::<Vec<_>>(),
            vec!["A", "B"]
        );
    }

    #[test]
    fn creating_a_group_adds_it_empty_and_open() {
        let mut doc = document(vec![space("A", 0)]);
        let id = create_group(&mut doc, 0, "New Folder").unwrap();
        assert_eq!(doc.spaces[0].groups.len(), 1);
        let group = &doc.spaces[0].groups[0];
        assert_eq!(group.id, id);
        assert_eq!(group.name, "New Folder");
        assert!(!group.collapsed);
        assert_eq!(group.icon, None);
    }

    /// Two folders in the same space cannot collide - the crate has no RNG,
    /// so uniqueness has to come from the counter instead.
    #[test]
    fn two_created_groups_get_different_ids() {
        let mut doc = document(vec![space("A", 0)]);
        let first = create_group(&mut doc, 0, "One").unwrap();
        let second = create_group(&mut doc, 0, "Two").unwrap();
        assert_ne!(first, second);
    }

    #[test]
    fn creating_a_group_in_an_unknown_space_does_nothing() {
        let mut doc = document(vec![space("A", 0)]);
        assert_eq!(create_group(&mut doc, 5, "New Folder"), None);
    }

    #[test]
    fn creating_a_group_past_the_cap_is_refused() {
        let mut doc = document(vec![space("A", 0)]);
        for _ in 0..MAX_GROUPS_PER_SPACE {
            create_group(&mut doc, 0, "Folder").unwrap();
        }
        assert_eq!(doc.spaces[0].groups.len(), MAX_GROUPS_PER_SPACE);
        assert_eq!(create_group(&mut doc, 0, "One Too Many"), None);
        assert_eq!(doc.spaces[0].groups.len(), MAX_GROUPS_PER_SPACE);
    }

    #[test]
    fn renaming_a_group_changes_its_name() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![TabGroup {
            id: "G".into(),
            name: "Reading".into(),
            collapsed: false,
            icon: None,
        }];
        assert!(rename_group(&mut doc, 0, "G", "Research"));
        assert_eq!(doc.spaces[0].groups[0].name, "Research");
        assert!(
            !rename_group(&mut doc, 0, "G", "Research"),
            "the same name twice is not a change"
        );
        assert!(
            !rename_group(&mut doc, 0, "G", ""),
            "an empty name does nothing"
        );
        assert!(!rename_group(&mut doc, 0, "nope", "Anything"));
    }

    /// A deleted folder's pins stay pinned, only loose - the pinned run is
    /// the folder, so leaving the id behind would draw a folder that is gone.
    #[test]
    fn deleting_a_group_frees_its_pinned_members_in_place() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![group("G")];
        doc.spaces[0].pinned_tabs = vec![tab("p0"), grouped("g0", "G")];

        assert!(delete_group(&mut doc, 0, "G"));
        assert_eq!(pinned_urls(&doc.spaces[0]), vec!["p0", "g0"]);
        assert_eq!(doc.spaces[0].pinned_tabs[1].group_id, None);
    }

    #[test]
    fn deleting_a_group_ungroups_its_tabs_rather_than_closing_them() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].tabs = vec![tab("a"), grouped("g", "G"), tab("b")];
        doc.spaces[0].groups = vec![TabGroup {
            id: "G".into(),
            name: "Reading".into(),
            collapsed: false,
            icon: None,
        }];
        assert!(delete_group(&mut doc, 0, "G"));
        assert!(doc.spaces[0].groups.is_empty());
        assert_eq!(doc.spaces[0].tabs.len(), 3, "the tab stays, only ungrouped");
        assert_eq!(doc.spaces[0].tabs[1].group_id, None);
        assert!(!delete_group(&mut doc, 0, "G"), "already gone");
    }

    #[test]
    fn setting_a_groups_icon_round_trips_and_clears() {
        let mut doc = document(vec![space("A", 0)]);
        doc.spaces[0].groups = vec![TabGroup {
            id: "G".into(),
            name: "Reading".into(),
            collapsed: false,
            icon: None,
        }];
        assert!(set_group_icon(&mut doc, 0, "G", "📚"));
        assert_eq!(doc.spaces[0].groups[0].icon.as_deref(), Some("📚"));
        assert!(
            !set_group_icon(&mut doc, 0, "G", "📚"),
            "the same icon twice is not a change"
        );
        assert!(set_group_icon(&mut doc, 0, "G", ""), "empty clears it");
        assert_eq!(doc.spaces[0].groups[0].icon, None);
        assert!(!set_group_icon(&mut doc, 0, "nope", "📚"));
    }
}
