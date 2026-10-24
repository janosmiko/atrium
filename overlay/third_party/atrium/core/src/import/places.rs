// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Reads the bookmarks of Firefox and Zen, the rows of one query on
//! places.sqlite, into pinned tabs and groups.

use std::collections::{HashMap, HashSet};

use serde_json::Value;

use super::mozilla::UNIX_TO_COCOA_SECONDS;
use super::{clamped, importable_url, objects, text, PlannedGroup, PlannedSpace, PlannedTab};

pub const BOOKMARKS_ROWS: &str = "places.sqlite#bookmarks";

const TOOLBAR_ROOT: &str = "toolbar_____";
/// The tags root holds a second copy of every tagged link, so it is not here.
const GROUP_ROOTS: [(&str, &str); 3] = [
    ("menu________", "Bookmarks Menu"),
    ("unfiled_____", "Other Bookmarks"),
    ("mobile______", "Mobile Bookmarks"),
];
const BOOKMARK: i64 = 1;
const FOLDER: i64 = 2;
/// A folder chain past this depth stops, like the Arc folder walk.
const MAX_FOLDER_DEPTH: usize = 64;
const UNNAMED_FOLDER: &str = "Folder";

#[derive(Debug, Default)]
pub struct BookmarkSkips {
    pub not_web: usize,
    pub nested: usize,
    pub too_deep: bool,
}

impl BookmarkSkips {
    pub fn write_notes(&self, browser: &str, notes: &mut Vec<String>) {
        if self.not_web > 0 {
            notes.push(format!(
                "Skipped {browser} bookmarks that are not web pages: {}.",
                self.not_web
            ));
        }
        if self.nested > 0 {
            notes.push(format!(
                "{browser} bookmarks moved from a nested folder to its top-level folder: {}.",
                self.nested
            ));
        }
        if self.too_deep {
            notes.push(format!(
                "{browser} bookmark folders nested more than {MAX_FOLDER_DEPTH} deep were not followed further."
            ));
        }
    }
}

/// Adds the bookmarks in `rows` to `space` as pinned tabs. A URL already in
/// `seen` is dropped, so the open tabs read after this skip a bookmarked page.
pub fn read_bookmarks(
    rows: &Value,
    space: &mut PlannedSpace,
    seen: &mut HashSet<String>,
    skipped: &mut BookmarkSkips,
) {
    let root = |guid: &str| {
        objects(Some(rows))
            .find(|r| text(r, "guid") == Some(guid))
            .and_then(|r| number(r, "id"))
    };
    let mut children: HashMap<i64, Vec<&Value>> = HashMap::new();
    for row in objects(Some(rows)) {
        if let Some(parent) = number(row, "parent") {
            children.entry(parent).or_default().push(row);
        }
    }
    let mut walk = Walk {
        children,
        groups: HashMap::new(),
        space,
        seen,
        skipped,
    };
    if let Some(toolbar) = root(TOOLBAR_ROOT) {
        for node in walk.take(toolbar) {
            match number(node, "type") {
                Some(FOLDER) => {
                    if let Some(id) = number(node, "id") {
                        let name = clamped(text(node, "title").unwrap_or(UNNAMED_FOLDER));
                        walk.folder(id, &name, true, 0);
                    }
                }
                Some(BOOKMARK) => {
                    walk.add(node, None);
                }
                _ => {}
            }
        }
    }
    for (guid, group) in GROUP_ROOTS {
        if let Some(id) = root(guid) {
            walk.folder(id, group, false, 0);
        }
    }
}

/// ReadImportRows sends an integer past the i32 range as a double.
fn number(row: &Value, key: &str) -> Option<i64> {
    let value = row.get(key)?;
    value.as_i64().or_else(|| value.as_f64().map(|f| f as i64))
}

struct Walk<'r, 'o> {
    children: HashMap<i64, Vec<&'r Value>>,
    groups: HashMap<String, usize>,
    space: &'o mut PlannedSpace,
    seen: &'o mut HashSet<String>,
    skipped: &'o mut BookmarkSkips,
}

impl<'r> Walk<'r, '_> {
    /// Taking the list out walks each folder once, so a crafted file whose
    /// parent ids loop, or that repeats an id, cannot keep the walk going.
    fn take(&mut self, id: i64) -> Vec<&'r Value> {
        self.children.remove(&id).unwrap_or_default()
    }

    /// Every link under folder `id` goes into `group`.
    fn folder(&mut self, id: i64, group: &str, count_nested: bool, depth: usize) {
        if depth > MAX_FOLDER_DEPTH {
            self.skipped.too_deep = true;
            return;
        }
        for node in self.take(id) {
            match number(node, "type") {
                Some(FOLDER) => {
                    if let Some(child) = number(node, "id") {
                        self.folder(child, group, count_nested, depth + 1);
                    }
                }
                Some(BOOKMARK) => {
                    if self.add(node, Some(group)) && count_nested && depth > 0 {
                        self.skipped.nested += 1;
                    }
                }
                _ => {}
            }
        }
    }

    fn add(&mut self, node: &Value, group: Option<&str>) -> bool {
        let url = match text(node, "url") {
            // A saved search or a smart folder, not a page.
            Some(url) if url.starts_with("place:") => return false,
            Some(url) if importable_url(url) => url,
            _ => {
                self.skipped.not_web += 1;
                return false;
            }
        };
        if !self.seen.insert(url.to_string()) {
            return false;
        }
        let group = group.map(|name| self.group(name));
        self.space.pinned.push(PlannedTab {
            url: url.into(),
            title: clamped(text(node, "title").unwrap_or(url)),
            // Microseconds since the Unix epoch.
            last_active_at: node
                .get("last_visit_date")
                .and_then(Value::as_f64)
                .filter(|us| *us > 0.0)
                .map(|us| us / 1e6 - UNIX_TO_COCOA_SECONDS),
            group,
        });
        true
    }

    /// Made on the first link, so an empty folder makes no group.
    fn group(&mut self, name: &str) -> usize {
        if let Some(&index) = self.groups.get(name) {
            return index;
        }
        let index = self.space.groups.len();
        self.space.groups.push(PlannedGroup { name: name.into() });
        self.groups.insert(name.into(), index);
        index
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn row(id: i64, parent: i64, kind: i64, title: &str, guid: &str, url: Option<&str>) -> Value {
        json!({"id": id, "parent": parent, "type": kind, "position": 0, "title": title,
               "guid": guid, "url": url, "last_visit_date": null})
    }

    fn roots() -> Vec<Value> {
        vec![
            row(1, 0, FOLDER, "", "root________", None),
            row(2, 1, FOLDER, "menu", "menu________", None),
            row(3, 1, FOLDER, "toolbar", "toolbar_____", None),
            row(4, 1, FOLDER, "tags", "tags________", None),
        ]
    }

    fn read(rows: Vec<Value>) -> (PlannedSpace, BookmarkSkips) {
        let mut space = PlannedSpace::default();
        let mut skipped = BookmarkSkips::default();
        read_bookmarks(
            &Value::Array(rows),
            &mut space,
            &mut HashSet::new(),
            &mut skipped,
        );
        (space, skipped)
    }

    /// `levels` folders below the toolbar, one inside the next, with a link
    /// in the deepest one.
    fn chain(levels: i64) -> Vec<Value> {
        let mut rows = roots();
        let mut parent = 3;
        for id in 100..100 + levels {
            rows.push(row(id, parent, FOLDER, "Level", &format!("F{id}"), None));
            parent = id;
        }
        rows.push(row(
            9,
            parent,
            BOOKMARK,
            "Bottom",
            "B",
            Some("https://bottom.example.test/"),
        ));
        rows
    }

    #[test]
    fn toolbar_links_are_pinned_and_a_toolbar_folder_is_a_group() {
        let mut rows = roots();
        rows.push(row(
            10,
            3,
            BOOKMARK,
            "News",
            "N",
            Some("https://news.example.test/"),
        ));
        rows.push(row(11, 3, FOLDER, "Docs", "D", None));
        rows.push(row(
            12,
            11,
            BOOKMARK,
            "Home",
            "H",
            Some("https://docs.example.test/"),
        ));
        rows.push(row(13, 11, FOLDER, "Deeper", "E", None));
        rows.push(row(
            14,
            13,
            BOOKMARK,
            "Spec",
            "S",
            Some("https://docs.example.test/spec"),
        ));
        rows.push(row(15, 3, FOLDER, "Empty", "M", None));
        rows.push(row(
            20,
            2,
            BOOKMARK,
            "Menu link",
            "L",
            Some("https://menu.example.test/"),
        ));
        let (space, skipped) = read(rows);
        let view: Vec<(&str, Option<usize>)> = space
            .pinned
            .iter()
            .map(|t| (t.url.as_str(), t.group))
            .collect();
        assert_eq!(
            view,
            [
                ("https://news.example.test/", None),
                ("https://docs.example.test/", Some(0)),
                ("https://docs.example.test/spec", Some(0)),
                ("https://menu.example.test/", Some(1)),
            ]
        );
        let groups: Vec<&str> = space.groups.iter().map(|g| g.name.as_str()).collect();
        assert_eq!(
            groups,
            ["Docs", "Bookmarks Menu"],
            "an empty folder makes no group"
        );
        assert_eq!(skipped.nested, 1);
        assert!(
            space.tabs.is_empty(),
            "bookmarks are pinned, never open tabs"
        );
    }

    #[test]
    fn separators_queries_and_the_tags_root_are_skipped_without_a_count() {
        let mut rows = roots();
        rows.push(row(40, 4, FOLDER, "tag", "T", None));
        rows.push(row(
            41,
            40,
            BOOKMARK,
            "",
            "U",
            Some("https://tagged.example.test/"),
        ));
        rows.push(row(10, 3, 3, "", "S", None));
        rows.push(row(11, 3, BOOKMARK, "Recent", "Q", Some("place:sort=8")));
        rows.push(row(
            12,
            2,
            BOOKMARK,
            "Script",
            "J",
            Some("javascript:void(0)"),
        ));
        rows.push(row(13, 2, BOOKMARK, "No page", "P", None));
        let (space, skipped) = read(rows);
        assert!(space.pinned.is_empty());
        assert_eq!(
            skipped.not_web, 2,
            "the bookmarklet and the link with no page count"
        );
    }

    #[test]
    fn a_parent_loop_or_a_repeated_id_is_walked_once() {
        let mut rows = roots();
        rows.push(row(7, 3, FOLDER, "Loop", "L", None));
        // Folder 7 names itself as its parent twice. Walked naively, the
        // work doubles at every level.
        rows.push(row(7, 7, FOLDER, "Loop", "L", None));
        rows.push(row(7, 7, FOLDER, "Loop", "L", None));
        rows.push(row(
            8,
            7,
            BOOKMARK,
            "Deep",
            "D",
            Some("https://deep.example.test/"),
        ));
        let (space, _) = read(rows);
        assert_eq!(space.pinned.len(), 1);
    }

    #[test]
    fn a_chain_past_64_folders_stops_with_a_note() {
        let (space, skipped) = read(chain(MAX_FOLDER_DEPTH as i64 + 1));
        assert_eq!(
            space.pinned.len(),
            1,
            "64 levels below the top folder still arrive"
        );
        assert!(!skipped.too_deep);
        let (space, skipped) = read(chain(MAX_FOLDER_DEPTH as i64 + 2));
        assert!(space.pinned.is_empty());
        assert!(skipped.too_deep);
    }

    #[test]
    fn a_url_already_seen_is_dropped_without_a_count() {
        let mut rows = roots();
        rows.push(row(
            10,
            3,
            BOOKMARK,
            "News",
            "N",
            Some("https://news.example.test/"),
        ));
        rows.push(row(
            11,
            2,
            BOOKMARK,
            "Again",
            "A",
            Some("https://news.example.test/"),
        ));
        let (space, skipped) = read(rows);
        assert_eq!(space.pinned.len(), 1);
        assert_eq!(skipped.not_web, 0);
        assert!(
            space.groups.is_empty(),
            "the menu group is made on its first new link"
        );
    }

    #[test]
    fn numbers_that_arrive_as_doubles_still_link() {
        let mut rows = roots();
        rows.push(
            json!({"id": 10.0, "parent": 3.0, "type": 1, "position": 0, "title": "Big",
                         "guid": "B", "url": "https://big.example.test/",
                         "last_visit_date": 1_704_067_200_000_000.0}),
        );
        let (space, _) = read(rows);
        assert_eq!(space.pinned[0].last_active_at, Some(725_760_000.0));
    }

    #[test]
    fn notes_name_the_browser() {
        let skipped = BookmarkSkips {
            not_web: 2,
            nested: 1,
            too_deep: true,
        };
        let mut notes = Vec::new();
        skipped.write_notes("Firefox", &mut notes);
        assert_eq!(
            notes,
            [
                "Skipped Firefox bookmarks that are not web pages: 2.",
                "Firefox bookmarks moved from a nested folder to its top-level folder: 1.",
                "Firefox bookmark folders nested more than 64 deep were not followed further.",
            ]
        );
    }
}
