// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Notes and marginalia.
//!
//! One module rather than two, because a marginal IS a note about a page: it
//! is keyed by the url it was taken on, it is capped and swept by the same
//! rules, and the overview lists both. Splitting them would put the cap in one
//! file and the thing it caps in another.
//!
//! A note has a SPACE or it is freestanding, and that is a real distinction
//! rather than a null: a space's pad follows the rail pane, a freestanding one
//! belongs to the profile.

use std::collections::HashSet;

use super::{ordered_positions, trim_to_cap, truncate_to};
use crate::SwiftDate;
use crate::spaces::{Document, Marginal, NoteOverviewEntry, SpaceNote};

/// The note belonging to `space` (by space id), if one has ever been saved.
pub fn note_for_space(doc: &Document, space: usize) -> Option<&SpaceNote> {
    let space_id = doc.spaces.get(space)?.id.as_str();
    doc.notes
        .iter()
        .find(|n| n.space_id.as_deref() == Some(space_id))
}

/// Keyed by space id rather than position, matching every other per-space
/// record (Doorway, Favorite) - a space reordered later must not lose its pad.
pub fn set_note_for_space(doc: &mut Document, space: usize, body: &str, now: SwiftDate) -> bool {
    if space >= doc.spaces.len() {
        return false;
    }
    let body = truncate_to(body, MAX_NOTE_BODY_LEN);
    let space_id = doc.spaces[space].id.clone();
    if let Some(note) = doc
        .notes
        .iter_mut()
        .find(|n| n.space_id.as_deref() == Some(space_id.as_str()))
    {
        note.body = body.into_owned();
        note.updated_at = now;
        return true;
    }
    let id = format!("note-{}", doc.allocate_token());
    doc.notes.push(SpaceNote {
        id,
        space_id: Some(space_id),
        title: "Notes".to_string(),
        body: body.into_owned(),
        updated_at: now,
    });
    true
}

/// The shortest selection a highlight accepts. The surface enforces the same
/// number, but a renderer can send anything, so the store checks it too.
const MIN_HIGHLIGHT_CHARS: usize = 3;

/// The new highlight's id, or `None` when the selection was too short or the
/// space index resolves to nothing.
pub fn add_marginal(
    doc: &mut Document,
    space: usize,
    url: &str,
    quote: &str,
    now: SwiftDate,
) -> Option<String> {
    if url.is_empty() || quote.chars().count() < MIN_HIGHLIGHT_CHARS {
        return None;
    }
    let space_id = doc.spaces.get(space)?.id.clone();
    // Not allocate_token: a reload renumbers tokens from the tab rows alone,
    // so two sessions handed out the same mark-N and one card's note landed on
    // another card's quote.
    let id = format!("mark-{}", next_marginal_number(doc));
    doc.marginalia.push(Marginal {
        id: id.clone(),
        url: url.to_string(),
        space_id,
        quote: quote.to_string(),
        note: String::new(),
        created_at: now,
    });
    trim_to_cap(&mut doc.marginalia, MAX_MARGINALIA);
    Some(id)
}

fn next_marginal_number(doc: &Document) -> u64 {
    doc.marginalia
        .iter()
        .filter_map(|m| m.id.strip_prefix("mark-")?.parse::<u64>().ok())
        .max()
        .map_or(1, |n| n + 1)
}

pub fn set_marginal_note(doc: &mut Document, id: &str, note: &str) -> bool {
    let note = truncate_to(note, MAX_NOTE_BODY_LEN);
    let Some(mark) = doc.marginalia.iter_mut().find(|m| m.id == id) else {
        return false;
    };
    if mark.note == note {
        return false;
    }
    mark.note = note.into_owned();
    true
}

/// This space's highlights on `url`, oldest first - the set a revisit
/// re-applies and the pad renders as cards.
pub fn marginalia_for_url<'a>(doc: &'a Document, space: usize, url: &str) -> Vec<&'a Marginal> {
    let Some(space_id) = doc.spaces.get(space).map(|s| s.id.as_str()) else {
        return Vec::new();
    };
    doc.marginalia
        .iter()
        .filter(|m| m.space_id == space_id && m.url == url)
        .collect()
}

/// Every highlight in this space, oldest first.
pub fn marginalia_for_space(doc: &Document, space: usize) -> Vec<&Marginal> {
    let Some(space_id) = doc.spaces.get(space).map(|s| s.id.as_str()) else {
        return Vec::new();
    };
    doc.marginalia
        .iter()
        .filter(|m| m.space_id == space_id)
        .collect()
}

/// The URLs this space has marked, for the sidebar's one-dot-per-row rule.
pub fn urls_with_marginalia(doc: &Document, space: usize) -> HashSet<String> {
    marginalia_for_space(doc, space)
        .into_iter()
        .map(|m| m.url.clone())
        .collect()
}

/// Every space, in sidebar order, with a saved and non-empty note that is not
/// dismissed - the space-pad half of the Notes overview list.
fn space_note_entries(doc: &Document) -> Vec<NoteOverviewEntry> {
    ordered_positions(&doc.spaces)
        .into_iter()
        .enumerate()
        .filter_map(|(position, i)| {
            let note = note_for_space(doc, i)?;
            if note.body.is_empty() || doc.dismissed_notes.iter().any(|d| d == &note.id) {
                return None;
            }
            let space = &doc.spaces[i];
            Some(NoteOverviewEntry {
                id: note.id.clone(),
                space_id: Some(space.id.clone()),
                space_index: Some(position as i32),
                space_name: Some(space.name.clone()),
                space_icon: Some(space.emoji.clone()),
                space_accent: Some(space.tint.clone()),
                title: format!("Notes — {}", space.name),
                body: note.body.clone(),
                updated_at: note.updated_at,
            })
        })
        .collect()
}

/// Every freestanding note (belongs to no space) that is not dismissed. An
/// empty body does not drop it here, unlike a space pad - it has no other
/// content to fall back on showing.
fn freestanding_note_entries(doc: &Document) -> Vec<NoteOverviewEntry> {
    doc.notes
        .iter()
        .filter(|n| n.space_id.is_none() && !doc.dismissed_notes.iter().any(|d| d == &n.id))
        .map(|n| NoteOverviewEntry {
            id: n.id.clone(),
            space_id: None,
            space_index: None,
            space_name: None,
            space_icon: None,
            space_accent: None,
            title: n.title.clone(),
            body: n.body.clone(),
            updated_at: n.updated_at,
        })
        .collect()
}

/// The Notes overview list: every space pad with content,
/// then every freestanding note, matching the lab's ordering.
pub fn notes_overview(doc: &Document) -> Vec<NoteOverviewEntry> {
    let mut entries = space_note_entries(doc);
    entries.extend(freestanding_note_entries(doc));
    entries
}

/// Creates an empty freestanding note and returns it (the + New Note action).
pub fn create_freestanding_note(doc: &mut Document, now: SwiftDate) -> SpaceNote {
    let id = format!("note-{}", doc.allocate_token());
    let note = SpaceNote {
        id,
        space_id: None,
        title: "New Note".to_string(),
        body: String::new(),
        updated_at: now,
    };
    doc.notes.push(note.clone());
    trim_to_cap(&mut doc.notes, MAX_NOTES);
    note
}

/// A note by id, space pad or freestanding.
pub fn note_by_id<'a>(doc: &'a Document, id: &str) -> Option<&'a SpaceNote> {
    doc.notes.iter().find(|n| n.id == id)
}

/// Overwrites a note's body by id, whichever kind it is.
pub fn set_note_body_by_id(doc: &mut Document, id: &str, body: &str, now: SwiftDate) -> bool {
    let body = truncate_to(body, MAX_NOTE_BODY_LEN);
    let Some(note) = doc.notes.iter_mut().find(|n| n.id == id) else {
        return false;
    };
    note.body = body.into_owned();
    note.updated_at = now;
    true
}

/// Hides a note from the overview list without touching its content (the
/// plain x).
pub fn dismiss_note(doc: &mut Document, id: &str) -> bool {
    if !doc.notes.iter().any(|n| n.id == id) {
        return false;
    }
    if !doc.dismissed_notes.iter().any(|d| d == id) {
        doc.dismissed_notes.push(id.to_string());
    }
    true
}

/// Option-x's confirmed delete: a space pad is blanked (the space itself
/// can't be removed from the list), a freestanding note is dropped entirely.
pub fn delete_note(doc: &mut Document, id: &str, now: SwiftDate) -> bool {
    let Some(pos) = doc.notes.iter().position(|n| n.id == id) else {
        return false;
    };
    doc.dismissed_notes.retain(|d| d != id);
    if doc.notes[pos].space_id.is_some() {
        doc.notes[pos].body.clear();
        doc.notes[pos].updated_at = now;
    } else {
        doc.notes.remove(pos);
    }
    true
}

/// Per-document caps on the sections a renderer can grow.
///
/// Favourites and archived rows were capped; marginalia, notes, shelf items and
/// doorways were not, and each has a verb a compromised renderer can call in a
/// loop - notesListCreate and sidebarTabShelved among them. Unbounded, that fills
/// the disk and makes every later launch pay for it on load.
///
/// Generous rather than tight: these are things a real user accumulates over
/// months, and the cap exists to stop a loop, not to ration the feature. The
/// OLDEST entry goes when the cap is reached, matching MAX_ARCHIVED_TABS - a cap
/// that dropped the newest would make the verb stop working on exactly the
/// documents it gets used in.
pub const MAX_MARGINALIA: usize = 2000;

pub const MAX_NOTES: usize = 2000;

/// A diary-length cap on note prose, well past any real user.
/// Truncated, not refused: refusing would silently drop the whole edit, while
/// a runaway write only grows past this by repeating itself, so the prefix
/// up to the cap is still exactly what the user typed.
pub const MAX_NOTE_BODY_LEN: usize = 100_000;

#[cfg(test)]
mod tests {
    use super::*;
    #[allow(unused_imports)]
    use crate::mutate::test_support::*;

    /// one entry per space with pad content, plus one per freestanding
    /// note, space pads first (sidebar order) then freestanding notes.
    #[test]
    fn notes_overview_lists_space_pads_then_freestanding_notes() {
        let mut doc = document(vec![space("second", 1), space("first", 0)]);
        set_note_for_space(&mut doc, 1, "first's jotting", 10.0); // sidebar position 0
        set_note_for_space(&mut doc, 0, "second's jotting", 20.0); // sidebar position 1
        create_freestanding_note(&mut doc, 30.0);

        let overview = notes_overview(&doc);
        let bodies: Vec<&str> = overview.iter().map(|e| e.body.as_str()).collect();
        assert_eq!(bodies, vec!["first's jotting", "second's jotting", ""]);
        assert_eq!(overview[0].space_id.as_deref(), Some("id-first"));
        assert_eq!(
            overview[0].space_index,
            Some(0),
            "first sits at sidebar position 0"
        );
        assert_eq!(
            overview[1].space_index,
            Some(1),
            "second sits at sidebar position 1"
        );
        assert_eq!(
            overview[2].space_id, None,
            "the freestanding note has no space"
        );
    }

    /// a blanked space pad drops from the list, a blanked freestanding
    /// note does not - it has no other body to fall back on.
    #[test]
    fn blanking_a_space_pad_drops_it_but_a_freestanding_note_stays() {
        let mut doc = document(vec![space("A", 0)]);
        set_note_for_space(&mut doc, 0, "jotting", 10.0);
        let custom = create_freestanding_note(&mut doc, 20.0);

        assert_eq!(notes_overview(&doc).len(), 2);
        set_note_for_space(&mut doc, 0, "", 30.0);
        let overview = notes_overview(&doc);
        assert_eq!(overview.len(), 1, "the blanked space pad is gone");
        assert_eq!(
            overview[0].id, custom.id,
            "the empty freestanding note stayed"
        );
    }

    /// + New Note is visible in the list immediately, with an empty body.
    #[test]
    fn create_freestanding_note_is_immediately_in_the_overview() {
        let mut doc = document(vec![]);
        let created = create_freestanding_note(&mut doc, 5.0);
        let overview = notes_overview(&doc);
        assert_eq!(overview.len(), 1);
        assert_eq!(overview[0].id, created.id);
        assert_eq!(overview[0].body, "");
    }

    /// The plain x: hidden from the list, but the note itself is untouched.
    #[test]
    fn dismiss_hides_a_note_without_touching_its_body() {
        let mut doc = document(vec![]);
        let created = create_freestanding_note(&mut doc, 5.0);
        set_note_body_by_id(&mut doc, &created.id, "keep me", 6.0);

        assert!(dismiss_note(&mut doc, &created.id));
        assert!(notes_overview(&doc).is_empty(), "dismissed, so hidden");
        assert_eq!(
            note_by_id(&doc, &created.id).map(|n| n.body.as_str()),
            Some("keep me"),
            "dismissing must not delete"
        );
        assert!(!dismiss_note(&mut doc, "no-such-id"));
    }

    /// Option-x: a space pad is blanked, a freestanding note is removed.
    #[test]
    fn delete_blanks_a_space_pad_but_removes_a_freestanding_note() {
        let mut doc = document(vec![space("A", 0)]);
        set_note_for_space(&mut doc, 0, "jotting", 10.0);
        let space_note_id = note_for_space(&doc, 0).unwrap().id.clone();
        let custom = create_freestanding_note(&mut doc, 20.0);

        assert!(delete_note(&mut doc, &space_note_id, 30.0));
        assert_eq!(
            note_by_id(&doc, &space_note_id).map(|n| n.body.as_str()),
            Some("")
        );

        assert!(delete_note(&mut doc, &custom.id, 40.0));
        assert!(note_by_id(&doc, &custom.id).is_none(), "removed entirely");
    }

    /// a 3-character selection stores a highlight carrying the quote
    /// verbatim and an empty note. Anything shorter is refused.
    #[test]
    fn a_highlight_needs_three_characters_and_starts_with_an_empty_note() {
        let mut doc = document(vec![space("A", 0)]);
        assert_eq!(add_marginal(&mut doc, 0, "https://a/", "ab", 1.0), None);
        assert_eq!(add_marginal(&mut doc, 0, "", "abcdef", 1.0), None);
        assert_eq!(add_marginal(&mut doc, 9, "https://a/", "abcdef", 1.0), None);
        assert!(doc.marginalia.is_empty(), "no refusal stored anything");

        let id =
            add_marginal(&mut doc, 0, "https://a/", "abc", 1.0).expect("three chars is enough");
        let mark = doc.marginalia.iter().find(|m| m.id == id).unwrap();
        assert_eq!(mark.quote, "abc");
        assert_eq!(mark.note, "");
        assert_eq!(mark.url, "https://a/");
        assert_eq!(mark.space_id, "id-A");
    }

    /// the card's note field writes back to that highlight's own entry
    /// and never touches the quote.
    #[test]
    fn editing_a_highlights_note_saves_to_that_entry_alone() {
        let mut doc = document(vec![space("A", 0)]);
        let first = add_marginal(&mut doc, 0, "https://a/", "quoted one", 1.0).unwrap();
        let second = add_marginal(&mut doc, 0, "https://a/", "quoted two", 2.0).unwrap();

        assert!(set_marginal_note(&mut doc, &first, "my commentary"));
        assert!(
            !set_marginal_note(&mut doc, &first, "my commentary"),
            "an unchanged note is not a write"
        );
        assert!(!set_marginal_note(&mut doc, "no-such-id", "x"));

        let a = doc.marginalia.iter().find(|m| m.id == first).unwrap();
        let b = doc.marginalia.iter().find(|m| m.id == second).unwrap();
        assert_eq!(a.note, "my commentary");
        assert_eq!(a.quote, "quoted one", "the quote stays immutable");
        assert_eq!(b.note, "", "the sibling card was not written");
    }

    /// a revisit asks by (space, url) and gets only that page's marks,
    /// oldest first.
    #[test]
    fn marginalia_for_url_is_scoped_to_the_space_and_the_page() {
        let mut doc = document(vec![space("A", 0), space("B", 1)]);
        add_marginal(&mut doc, 0, "https://a/", "first", 1.0);
        add_marginal(&mut doc, 0, "https://a/", "second", 2.0);
        add_marginal(&mut doc, 0, "https://other/", "elsewhere", 3.0);
        add_marginal(&mut doc, 1, "https://a/", "other space", 4.0);

        let quotes: Vec<&str> = marginalia_for_url(&doc, 0, "https://a/")
            .iter()
            .map(|m| m.quote.as_str())
            .collect();
        assert_eq!(quotes, vec!["first", "second"]);
        assert!(marginalia_for_url(&doc, 0, "https://nothing/").is_empty());
        assert_eq!(marginalia_for_space(&doc, 0).len(), 3);
    }

    /// A reload renumbers row tokens from the tab rows, so an id taken from
    /// that counter repeated across sessions and set_marginal_note then wrote
    /// the wrong card. Found by mutation testing.
    #[test]
    fn a_reloaded_document_does_not_hand_out_an_id_it_already_uses() {
        let mut doc = document(vec![space("A", 0)]);
        let first = add_marginal(&mut doc, 0, "https://a/", "first", 1.0).unwrap();
        let second = add_marginal(&mut doc, 0, "https://a/", "second", 2.0).unwrap();
        assert_ne!(first, second);

        // What a reload looks like: same marginalia, token counter back to zero.
        doc.next_token = 0;
        let third = add_marginal(&mut doc, 0, "https://a/", "third", 3.0).unwrap();
        assert_ne!(third, first);
        assert_ne!(third, second);

        assert!(set_marginal_note(&mut doc, &third, "on the third"));
        assert_eq!(
            doc.marginalia.iter().find(|m| m.id == first).unwrap().note,
            "",
            "writing the third card's note must not land on the first"
        );
    }

    /// a space pad, a freestanding note and a highlight's note each
    /// clip to the cap instead of storing the whole runaway string.
    #[test]
    fn an_overlong_note_body_is_truncated_to_the_cap() {
        let mut doc = document(vec![space("A", 0)]);
        let overlong = "a".repeat(MAX_NOTE_BODY_LEN + 500);

        set_note_for_space(&mut doc, 0, &overlong, 1.0);
        assert_eq!(
            note_for_space(&doc, 0).unwrap().body.len(),
            MAX_NOTE_BODY_LEN
        );

        let created = create_freestanding_note(&mut doc, 2.0);
        set_note_body_by_id(&mut doc, &created.id, &overlong, 3.0);
        assert_eq!(
            note_by_id(&doc, &created.id).unwrap().body.len(),
            MAX_NOTE_BODY_LEN
        );

        let mark = add_marginal(&mut doc, 0, "https://a/", "quoted text", 4.0).unwrap();
        set_marginal_note(&mut doc, &mark, &overlong);
        let saved = doc.marginalia.iter().find(|m| m.id == mark).unwrap();
        assert_eq!(saved.note.len(), MAX_NOTE_BODY_LEN);
    }

    /// A body within the cap is stored verbatim.
    #[test]
    fn a_normal_length_note_body_is_untouched() {
        let mut doc = document(vec![space("A", 0)]);
        set_note_for_space(&mut doc, 0, "an ordinary jotting", 1.0);
        assert_eq!(note_for_space(&doc, 0).unwrap().body, "an ordinary jotting");
    }

    /// the dot is per URL and shows once however many marks a page has.
    #[test]
    fn urls_with_marginalia_collapses_many_marks_to_one_url() {
        let mut doc = document(vec![space("A", 0), space("B", 1)]);
        add_marginal(&mut doc, 0, "https://a/", "first", 1.0);
        add_marginal(&mut doc, 0, "https://a/", "second", 2.0);
        add_marginal(&mut doc, 1, "https://b/", "other space", 3.0);

        let marked = urls_with_marginalia(&doc, 0);
        assert_eq!(marked.len(), 1, "two marks on one page is still one dot");
        assert!(marked.contains("https://a/"));
        assert!(
            !marked.contains("https://b/"),
            "another space's mark does not dot this space's row"
        );
    }
}
