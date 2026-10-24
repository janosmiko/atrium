// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Document fixtures the mutate submodules' tests share.
//!
//! One copy, because a fixture that drifts between test modules makes two
//! of them agree about a document the code never sees. Test-only
//! and never compiled into the library.

use crate::spaces::{AppState, Document, Space, SpaceTab, assign_tokens};

pub fn tab(url: &str) -> SpaceTab {
    SpaceTab::new(url, url)
}

pub fn grouped(url: &str, group: &str) -> SpaceTab {
    let mut t = tab(url);
    t.group_id = Some(group.into());
    t
}

pub fn space(name: &str, order: i64) -> Space {
    Space {
        id: format!("id-{name}"),
        name: name.into(),
        emoji: "X".into(),
        tint: "#000000".into(),
        profile_id: "p".into(),
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

pub fn document(spaces: Vec<Space>) -> Document {
    Document {
        spaces,
        app_state: AppState {
            active_space_id: None,
            sidebar_width: 220.0,
            schema_version: 3,
            archive_epoch: Some(1.0),
            window_spaces: None,
            sidebar_width_migrated: true,
        },
        favorites: Vec::new(),
        hidden_rows: Vec::new(),
        archived: Vec::new(),
        shelf: Vec::new(),
        snoozed: Vec::new(),
        notes: Vec::new(),
        dismissed_notes: Vec::new(),
        marginalia: Vec::new(),
        doorways: Vec::new(),
        profiles: Vec::new(),
        next_token: 0,
    }
}

/// A document whose rows carry identities, as `spaces::load` leaves one.
/// Only the tab-open and tab-close rules need this; the older mutations
/// never look at a token.
pub fn tokened(spaces: Vec<Space>) -> Document {
    let mut doc = document(spaces);
    assign_tokens(&mut doc);
    doc
}

pub fn urls(space: &Space) -> Vec<&str> {
    space.tabs.iter().map(|t| t.url.as_str()).collect()
}

pub fn pinned_urls(space: &Space) -> Vec<&str> {
    space.pinned_tabs.iter().map(|t| t.url.as_str()).collect()
}

pub fn group(id: &str) -> crate::spaces::TabGroup {
    crate::spaces::TabGroup {
        id: id.into(),
        name: id.into(),
        collapsed: false,
        icon: None,
    }
}
