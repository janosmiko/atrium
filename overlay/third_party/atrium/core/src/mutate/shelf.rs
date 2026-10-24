// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The Shelf: things set aside from a page, not tied to a tab.
//!
//! An item carries the space it was saved from so the overview can say where
//! it came from, and `None` is a real state - an item saved with no space
//! belongs to the profile rather than to one rail pane.

use super::truncate_to;
use crate::SwiftDate;
use crate::spaces::{Document, ShelfItem, ShelfOverviewEntry};

/// A generous cap on a shelf item's title, matching the short
/// labels `settings.rs` bounds. Truncated rather than refused - see
/// `notes::MAX_NOTE_BODY_LEN` for why.
const MAX_SHELF_TITLE_LEN: usize = 200;

/// The Shelf overview list, each item's own space resolved to
/// a name, in shelf order (newest-added first, matching `unshift` in the lab).
pub fn shelf_overview(doc: &Document) -> Vec<ShelfOverviewEntry> {
    doc.shelf
        .iter()
        .map(|item| {
            let space_name = item
                .space_id
                .as_deref()
                .and_then(|id| doc.spaces.iter().find(|s| s.id == id))
                .map(|s| s.name.clone());
            ShelfOverviewEntry {
                id: item.id.clone(),
                kind: item.kind.clone(),
                title: item.title.clone(),
                url: item.url.clone(),
                space_id: item.space_id.clone(),
                space_name,
                added_at: item.added_at,
                highlights: item
                    .highlights
                    .iter()
                    .take(2)
                    .map(|h| h.text.clone())
                    .collect(),
            }
        })
        .collect()
}

/// Adds a ShelfItem. `space_id` is the shelved page's own space, not the
/// window's active one - the tab it was shelved from stays open there.
pub fn add_shelf_item(
    doc: &mut Document,
    kind: &str,
    title: &str,
    url: &str,
    space_id: Option<&str>,
    now: SwiftDate,
) -> ShelfItem {
    let item = ShelfItem {
        id: format!("shelf-{}", doc.allocate_token()),
        kind: kind.to_string(),
        title: truncate_to(title, MAX_SHELF_TITLE_LEN).into_owned(),
        url: url.to_string(),
        space_id: space_id.map(str::to_string),
        added_at: now,
        highlights: Vec::new(),
    };
    doc.shelf.insert(0, item.clone());
    // Newest first here, so the OLDEST is at the end - truncate rather than
    // drain from the front.
    doc.shelf.truncate(MAX_SHELF_ITEMS);
    item
}

/// Removes a ShelfItem by id. The tab it points at is never touched -
/// shelving never closes it, so removing the entry can't either.
pub fn remove_shelf_item(doc: &mut Document, id: &str) -> bool {
    let Some(pos) = doc.shelf.iter().position(|i| i.id == id) else {
        return false;
    };
    doc.shelf.remove(pos);
    true
}

const MAX_SHELF_ITEMS: usize = 1000;

#[cfg(test)]
mod tests {
    use super::*;
    #[allow(unused_imports)]
    use crate::mutate::test_support::*;

    #[test]
    fn an_overlong_shelf_title_is_truncated_to_the_cap() {
        let mut doc = document(vec![]);
        let overlong = "t".repeat(MAX_SHELF_TITLE_LEN + 50);
        let item = add_shelf_item(&mut doc, "page", &overlong, "https://a/", None, 1.0);
        assert_eq!(item.title.len(), MAX_SHELF_TITLE_LEN);
        assert_eq!(doc.shelf[0].title.len(), MAX_SHELF_TITLE_LEN);
    }

    #[test]
    fn a_normal_length_shelf_title_is_untouched() {
        let mut doc = document(vec![]);
        let item = add_shelf_item(
            &mut doc,
            "page",
            "An Ordinary Title",
            "https://a/",
            None,
            1.0,
        );
        assert_eq!(item.title, "An Ordinary Title");
    }
}
