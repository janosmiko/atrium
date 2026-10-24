// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Favourites and doorways: the tiles above the rail, and the pinned tabs
//! promoted to their own Dock windows.
//!
//! Both are PER PROFILE rather than per space, which is why the move helpers
//! translate a visible tile position through the profile's own subset before
//! addressing the array - the same two-index-spaces problem the parent module
//! documents, on a different axis.
//!
//! Doorways sit here rather than in a module of their own: one function and one
//! cap, sharing the profile-scoping rule that makes this file's shape.

use super::trim_to_cap;
use crate::spaces::{Document, Doorway, Favorite, Space};

/// Arc's own cap, and the one the Swift store enforced: at most 12 favourites
/// per profile. The 13th add is REFUSED rather than evicting an existing one.
pub const MAX_FAVORITES_PER_PROFILE: usize = 12;

/// The positions a space's favourite grid renders, as indices into the
/// PROFILE's own list.
///
/// The identity since favourites stopped being per-space: every space of a
/// profile renders that profile's whole list in its own order, so a grid
/// position already is a profile-list index. Kept as a seam rather than
/// inlined, because both ends of a reorder go through it and the day any
/// filtering applies again there is one place to put it.
fn profile_favorite_positions(space: &Space, favorites: &[Favorite]) -> Vec<usize> {
    (0..favorites
        .iter()
        .filter(|f| f.profile_id == space.profile_id)
        .count())
        .collect()
}

/// The `(from, to)` pair `move_favorite` wants, from the two GRID positions the
/// surface sends.
///
/// Two translations, and both have been a bug somewhere already: grid to
/// profile list, and pre- to post-removal, because the destination counts the
/// list with the dragged tile already taken out. A destination past the end
/// resolves to the append position. `None` when the source does not resolve.
pub fn profile_favorite_move(
    from_visible: usize,
    to_visible: usize,
    space: &Space,
    favorites: &[Favorite],
) -> Option<(usize, usize)> {
    let mut visible = profile_favorite_positions(space, favorites);
    if from_visible >= visible.len() {
        return None;
    }
    let profile_count = visible.len();
    let from = visible.remove(from_visible);
    let shifted: Vec<usize> = visible
        .iter()
        .map(|&i| if i > from { i - 1 } else { i })
        .collect();
    let to = shifted
        .get(to_visible)
        .copied()
        .unwrap_or_else(|| profile_count.saturating_sub(1));
    Some((from, to))
}

/// Adds `(profile_id, url)` to the favourites.
///
/// Purely additive: it never touches a space, a tab or a pinned tab, so it can
/// never lose one. A duplicate and a profile already at the cap are both
/// refused rather than replacing anything.
pub fn add_favorite(doc: &mut Document, url: &str, title: &str, profile_id: &str) -> bool {
    if url.is_empty() {
        return false;
    }
    if doc
        .favorites
        .iter()
        .any(|f| f.profile_id == profile_id && f.url == url)
    {
        return false;
    }
    if doc
        .favorites
        .iter()
        .filter(|f| f.profile_id == profile_id)
        .count()
        >= MAX_FAVORITES_PER_PROFILE
    {
        return false;
    }
    doc.favorites.push(Favorite {
        url: url.to_string(),
        title: title.to_string(),
        profile_id: profile_id.to_string(),
        space_id: None,
    });
    true
}

/// Keyed the same way `add_favorite` is, by `(profile_id, url)`, so the pair
/// cannot disagree about what identifies a tile. A favourite has no id of its
/// own - its URL is the handle the surface carries.
pub fn remove_favorite(doc: &mut Document, profile_id: &str, url: &str) -> bool {
    let before = doc.favorites.len();
    doc.favorites
        .retain(|f| !(f.profile_id == profile_id && f.url == url));
    doc.favorites.len() != before
}

/// Keyed by `(space_id, url)` rather than `(profile_id, url)` like
/// `add_favorite` - a Doorway promotes out of one space, so the same site
/// pinned in two spaces gets two records.
pub fn add_doorway(doc: &mut Document, url: &str, space_id: &str, profile_id: &str) -> bool {
    if url.is_empty() {
        return false;
    }
    if doc
        .doorways
        .iter()
        .any(|d| d.space_id == space_id && d.url == url)
    {
        return false;
    }
    doc.doorways.push(Doorway {
        url: url.to_string(),
        space_id: space_id.to_string(),
        profile_id: profile_id.to_string(),
    });
    trim_to_cap(&mut doc.doorways, MAX_DOORWAYS);
    true
}

pub const MAX_DOORWAYS: usize = 200;

/// Reorders one profile's favourites.
///
/// Both indices address THAT profile's own subset, not the backing array,
/// which interleaves every profile's entries in append order. The move is
/// translated back to absolute positions so another profile's entries and
/// their relative order are left alone.
pub fn move_favorite(
    doc: &mut Document,
    profile_id: &str,
    from_index: usize,
    to_index: usize,
) -> bool {
    let positions = |favorites: &[Favorite]| -> Vec<usize> {
        favorites
            .iter()
            .enumerate()
            .filter(|(_, f)| f.profile_id == profile_id)
            .map(|(i, _)| i)
            .collect()
    };
    let owned = positions(&doc.favorites);
    let Some(&from) = owned.get(from_index) else {
        return false;
    };
    let clamped = to_index.min(owned.len() - 1);
    if clamped == from_index {
        return false;
    }
    let favorite = doc.favorites.remove(from);
    let remaining = positions(&doc.favorites);
    let at = remaining
        .get(clamped)
        .copied()
        .unwrap_or(doc.favorites.len());
    doc.favorites.insert(at, favorite);
    true
}

#[cfg(test)]
mod tests {
    use super::*;
    #[allow(unused_imports)]
    use crate::mutate::test_support::*;

    #[test]
    fn a_favourite_is_removed_from_its_own_profile_only() {
        let mut doc = document(vec![space("A", 0)]);
        assert!(add_favorite(&mut doc, "u", "t", "p"));
        assert!(add_favorite(&mut doc, "u", "t", "other"));

        assert!(remove_favorite(&mut doc, "p", "u"));
        // The same URL under another profile is a different favourite, and the
        // keying has to agree with add_favorite's or the pair would disagree
        // about what a tile is.
        let left: Vec<&str> = doc
            .favorites
            .iter()
            .map(|f| f.profile_id.as_str())
            .collect();
        assert_eq!(left, vec!["other"]);

        assert!(
            !remove_favorite(&mut doc, "p", "u"),
            "removing what is already gone does not dirty the file"
        );
        assert!(!remove_favorite(&mut doc, "p", "never-added"));
    }

    #[test]
    fn a_favourite_is_added_once_and_capped_at_twelve() {
        let mut doc = document(vec![space("A", 0)]);
        assert!(add_favorite(&mut doc, "u", "t", "p"));
        assert!(
            !add_favorite(&mut doc, "u", "t", "p"),
            "the same (profile, url) twice is one favourite"
        );
        for i in 1..MAX_FAVORITES_PER_PROFILE {
            assert!(add_favorite(&mut doc, &format!("u{i}"), "t", "p"));
        }
        assert!(
            !add_favorite(&mut doc, "one-too-many", "t", "p"),
            "the 13th is refused, never evicting an existing one"
        );
        assert_eq!(doc.favorites.len(), MAX_FAVORITES_PER_PROFILE);
        assert!(
            add_favorite(&mut doc, "u", "t", "other"),
            "the cap is per profile"
        );
    }

    #[test]
    fn a_doorway_is_added_carrying_site_space_and_profile() {
        let mut doc = document(vec![space("A", 0)]);
        assert!(add_doorway(&mut doc, "https://mail.example/", "id-A", "p"));
        assert_eq!(
            doc.doorways,
            vec![Doorway {
                url: "https://mail.example/".into(),
                space_id: "id-A".into(),
                profile_id: "p".into(),
            }]
        );
    }

    #[test]
    fn a_second_add_doorway_for_the_same_space_and_site_is_a_noop() {
        let mut doc = document(vec![space("A", 0)]);
        assert!(add_doorway(&mut doc, "https://mail.example/", "id-A", "p"));
        assert!(
            !add_doorway(&mut doc, "https://mail.example/", "id-A", "p"),
            "the same (space, url) twice is one Doorway"
        );
        assert_eq!(doc.doorways.len(), 1);
    }

    #[test]
    fn the_same_site_in_two_spaces_gets_two_doorways() {
        let mut doc = document(vec![space("A", 0)]);
        assert!(add_doorway(&mut doc, "https://mail.example/", "id-A", "p"));
        assert!(
            add_doorway(&mut doc, "https://mail.example/", "id-B", "p"),
            "bound to its space - a different space is a different Doorway"
        );
        assert_eq!(doc.doorways.len(), 2);
    }

    /// The reorder addresses the profile's own subset. A second profile's
    /// entries sit between them in the backing array and must not move.
    #[test]
    fn reordering_a_favourite_leaves_another_profiles_alone() {
        let mut doc = document(vec![space("A", 0)]);
        let fav = |url: &str, profile: &str| Favorite {
            url: url.into(),
            title: "t".into(),
            profile_id: profile.into(),
            space_id: None,
        };
        doc.favorites = vec![
            fav("a", "p"),
            fav("other", "q"),
            fav("b", "p"),
            fav("c", "p"),
        ];

        assert!(move_favorite(&mut doc, "p", 0, 2));
        let names: Vec<&str> = doc.favorites.iter().map(|f| f.url.as_str()).collect();
        assert_eq!(names, vec!["other", "b", "c", "a"]);
        assert_eq!(doc.favorites[0].profile_id, "q");
    }

    #[test]
    fn a_grid_move_translates_past_the_removal() {
        let mut doc = document(vec![space("A", 0)]);
        for url in ["a", "b", "c"] {
            add_favorite(&mut doc, url, "t", "p");
        }
        let space = &doc.spaces[0];
        assert_eq!(
            profile_favorite_move(0, 1, space, &doc.favorites),
            Some((0, 1))
        );
        assert_eq!(
            profile_favorite_move(0, 9, space, &doc.favorites),
            Some((0, 2)),
            "past the end is the append position"
        );
        assert_eq!(profile_favorite_move(9, 0, space, &doc.favorites), None);
    }

    /// The OLDEST goes, matching MAX_ARCHIVED_TABS. Dropping the newest would make
    /// the verb stop working on exactly the documents it gets used in.
    #[test]
    fn reaching_a_cap_drops_the_oldest_entry() {
        let mut doc = document(vec![space("One", 0)]);
        for n in 0..MAX_DOORWAYS + 1 {
            add_doorway(&mut doc, &format!("https://d.test/{n}"), "id-One", "p");
        }
        assert_eq!(doc.doorways.len(), MAX_DOORWAYS);
        // The first one added is gone; the last one added is still there.
        assert!(
            !doc.doorways.iter().any(|d| d.url == "https://d.test/0"),
            "the oldest doorway survived the cap"
        );
        assert!(
            doc.doorways
                .iter()
                .any(|d| d.url == format!("https://d.test/{}", MAX_DOORWAYS)),
            "the newest doorway was dropped instead of the oldest"
        );
    }
}
