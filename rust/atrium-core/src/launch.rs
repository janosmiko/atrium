// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! What a launch needs from the spaces document before it can show a window.
//!
//! Ported from `RequiredProfilesJSON.swift`, `StartupProfilesJSON.swift` and
//! `StartupTabJSON.swift`.
//!
//! ONE payload rather than three exports, because `ProfileBootstrap::Bootstrap`
//! wants all three at once and they have to agree: the profile the startup tab
//! names must be one of the profiles the launch loads, or the window it belongs
//! to is never created. Deriving them together is what makes that true by
//! construction rather than by a check on the far side.
//!
//! Pure - it reads a document and returns a string.

use serde::Serialize;

use crate::settings::StartupBehavior;
use crate::spaces::{Document, Space, SpaceTab};

/// The profile a launch falls back to when the document names none it can use.
pub(crate) const DEFAULT_PROFILE: &str = "Default";

/// What a window with no tab to restore opens on. The bootstrap reads this
/// exact string as "nothing to restore" and opens the palette instead.
const BLANK_TAB: &str = "about:blank";

/// Chromium's profile directory is `user_data_dir/<id>`, so an id is a single
/// path component and nothing else.
///
/// Mirrors `ProfileBootstrap::IsSafeProfileId` and the
/// `ImportApplier.isValidProfileID` allowlist it was copied from. This is the
/// emission boundary: `Space.profile_id` can arrive from a hand-edited
/// spaces.json without passing any other guard, and a traversal-shaped id must
/// be dropped here rather than escaping `user_data_dir` on the far side.
fn is_safe_profile_id(id: &str) -> bool {
    const MAX_LENGTH: usize = 64;
    if id.is_empty() || id.len() > MAX_LENGTH || id.starts_with('.') || id.contains("..") {
        return false;
    }
    id.bytes()
        .all(|b| b.is_ascii_alphanumeric() || b == b' ' || b == b'_' || b == b'.' || b == b'-')
}

/// The one tab the shown window materialises at launch.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub(crate) struct StartupTab {
    #[serde(rename = "profileID")]
    pub profile_id: String,
    pub url: String,
}

/// Everything `ProfileBootstrap` reads before it creates a profile.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Plan {
    /// Every profile the user's spaces reference, sorted. Never empty - a
    /// launch with no profile to load has no window.
    pub required_profiles: Vec<String>,
    /// The subset that must be resident BEFORE first paint: the active space's
    /// profile, plus the profiles of the spaces the persisted window slots
    /// restore into. Everything else is loaded shortly after the first window
    /// shows.
    ///
    /// EMPTY means no deferral - load everything eagerly. That is the
    /// behaviour from before deferral existed, and it is the safe reading of a degenerate
    /// document: loading too much costs startup time, loading too little costs
    /// the user a window.
    pub startup_profiles: Vec<String>,
    pub(crate) startup_tab: StartupTab,
}

/// The tab lazy restore materialises for `space`: its most recently active
/// tab, falling back to its first.
///
/// PINNED TABS DO NOT COUNT. They are cold tiles until the user clicks one,
/// and restoring into one would leave the tile looking live while the space's
/// real last tab stayed cold.
///
/// The most recent is the first of equal timestamps, not the last - a v1 file
/// that was migrated has none of them stamped at all, and picking arbitrarily
/// among equals makes two launches of one file open different tabs.
fn startup_tab_of(space: &Space) -> Option<&SpaceTab> {
    let most_recent = space
        .tabs
        .iter()
        .filter(|t| t.last_active_at.is_some())
        .fold(None::<&SpaceTab>, |best, tab| match best {
            Some(best) if best.last_active_at >= tab.last_active_at => Some(best),
            _ => Some(tab),
        });
    most_recent.or_else(|| space.tabs.first())
}

/// The spaces in the order the sidebar renders them.
fn ordered(spaces: &[Space]) -> Vec<&Space> {
    let mut out: Vec<&Space> = spaces.iter().collect();
    out.sort_by_key(|s| s.order);
    out
}

fn sorted_unique_safe(ids: impl Iterator<Item = String>) -> Vec<String> {
    let mut out: Vec<String> = ids.filter(|id| is_safe_profile_id(id)).collect();
    out.sort();
    out.dedup();
    out
}

/// Derives the launch plan from a loaded document.
///
/// `startup` is settings.json's Startup row. It picks the shown tab and
/// nothing else: which profiles load is a property of the document, and a
/// blank tab in a profile the launch never loaded is still no window.
pub fn plan(doc: &Document, startup: StartupBehavior) -> Plan {
    let ordered = ordered(&doc.spaces);

    let mut required_profiles = sorted_unique_safe(ordered.iter().map(|s| s.profile_id.clone()));
    if required_profiles.is_empty() {
        required_profiles.push(DEFAULT_PROFILE.to_string());
    }

    let active = crate::sidebar::active_index_from_document(
        &doc.spaces,
        doc.app_state.active_space_id.as_deref(),
    );
    let active_space = ordered.get(active).copied();

    // The spaces a window slot restores into are startup-needed for the same
    // reason the active one is: their window is on screen at first paint.
    let window_space_ids = doc.app_state.window_spaces.clone().unwrap_or_default();
    let startup_profiles = sorted_unique_safe(
        active_space
            .map(|s| s.profile_id.clone())
            .into_iter()
            .chain(
                ordered
                    .iter()
                    .filter(|s| window_space_ids.contains(&s.id))
                    .map(|s| s.profile_id.clone()),
            ),
    );

    // The startup tab's profile has to be one the launch actually loads, or
    // OnProfileAdded never fires for it and no window is ever shown. An
    // unsafe id was filtered out of `required_profiles` above, so this is
    // where that case lands.
    let profile_id = active_space
        .map(|s| s.profile_id.clone())
        .filter(|id| required_profiles.iter().any(|known| known == id))
        .unwrap_or_else(|| required_profiles[0].clone());

    // The Startup row is asked BEFORE the document, not after: on `BlankTab`
    // there is no tab to look for. Swift gated it the same way
    // (StartupTabJSON.swift), and the port dropped the gate while keeping the
    // computation.
    let url = match startup {
        StartupBehavior::BlankTab => BLANK_TAB.to_string(),
        StartupBehavior::RestoreLastActive => active_space
            .and_then(startup_tab_of)
            .map(|t| t.url.clone())
            .filter(|url| !url.is_empty())
            .unwrap_or_else(|| BLANK_TAB.to_string()),
    };

    Plan {
        required_profiles,
        startup_profiles,
        startup_tab: StartupTab { profile_id, url },
    }
}

/// The JSON `ProfileBootstrap` parses. A plan that cannot be encoded would
/// leave the launch with no answer at all, so the fallback is the same one the
/// C++ side would otherwise invent for itself.
pub fn encode(plan: &Plan) -> String {
    serde_json::to_string(plan).unwrap_or_else(|_| {
        format!(
            r#"{{"requiredProfiles":["{DEFAULT_PROFILE}"],"startupProfiles":[],"startupTab":{{"profileID":"{DEFAULT_PROFILE}","url":"{BLANK_TAB}"}}}}"#
        )
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::spaces;

    const INSTANT: f64 = 788_918_400.0;

    /// What every test that is not about the Startup row asks for.
    const RESTORE: StartupBehavior = StartupBehavior::RestoreLastActive;

    fn load(json: &str) -> Document {
        spaces::load(json, INSTANT).expect("the document must load")
    }

    /// Two profiles, two spaces, and the second space is the active one.
    fn two_profiles() -> Document {
        load(
            r##"{"spaces":[
            {"id":"A","name":"Work","emoji":"W","tint":"#111111","profileID":"Work",
             "pinnedTabs":[],"tabs":[{"url":"https://work.test/","title":"W"}],
             "order":0,"groups":[]},
            {"id":"B","name":"Home","emoji":"H","tint":"#222222","profileID":"Home",
             "pinnedTabs":[{"url":"https://pin.test/","title":"P"}],
             "tabs":[{"url":"https://first.test/","title":"F"},
                     {"url":"https://recent.test/","title":"R","lastActiveAt":900}],
             "order":1,"groups":[]}],
            "appState":{"activeSpaceID":"B","sidebarWidth":220,"schemaVersion":3,
            "archiveEpoch":1,"sidebarWidthMigrated":true}}"##,
        )
    }

    /// The whole point of the task: a launch loads every profile the document
    /// names, not only Default.
    #[test]
    fn every_profile_the_spaces_name_is_required() {
        let plan = plan(&two_profiles(), RESTORE);
        assert_eq!(plan.required_profiles, vec!["Home", "Work"]);
    }

    /// Only the active space's profile is needed before first paint here; the
    /// other one is what the deferral machinery exists for.
    #[test]
    fn only_the_visible_profile_is_needed_before_first_paint() {
        let plan = plan(&two_profiles(), RESTORE);
        assert_eq!(plan.startup_profiles, vec!["Home"]);
    }

    /// A persisted window slot's space is on screen at first paint too, so its
    /// profile cannot wait behind the deferral.
    #[test]
    fn a_restored_windows_space_is_startup_needed() {
        let doc = load(
            r##"{"spaces":[
            {"id":"A","name":"Work","emoji":"W","tint":"#111111","profileID":"Work",
             "pinnedTabs":[],"tabs":[],"order":0,"groups":[]},
            {"id":"B","name":"Home","emoji":"H","tint":"#222222","profileID":"Home",
             "pinnedTabs":[],"tabs":[],"order":1,"groups":[]},
            {"id":"C","name":"Side","emoji":"S","tint":"#333333","profileID":"Side",
             "pinnedTabs":[],"tabs":[],"order":2,"groups":[]}],
            "appState":{"activeSpaceID":"A","windowSpaces":["C"],"sidebarWidth":220,
            "schemaVersion":3,"archiveEpoch":1,"sidebarWidthMigrated":true}}"##,
        );
        let plan = plan(&doc, RESTORE);
        assert_eq!(plan.required_profiles, vec!["Home", "Side", "Work"]);
        assert_eq!(plan.startup_profiles, vec!["Side", "Work"]);
    }

    /// The most recently active tab wins over the first one - that is what
    /// "restore my last tab" means.
    #[test]
    fn the_startup_tab_is_the_active_spaces_most_recent() {
        let plan = plan(&two_profiles(), RESTORE);
        assert_eq!(
            plan.startup_tab,
            StartupTab {
                profile_id: "Home".into(),
                url: "https://recent.test/".into(),
            }
        );
    }

    /// The Startup row's whole job. `two_profiles` has a most
    /// recently active tab to restore and the row is what refuses it - a space
    /// with nothing to restore would answer `about:blank` either way and prove
    /// nothing.
    #[test]
    fn blank_tab_refuses_the_tab_the_document_would_restore() {
        let doc = two_profiles();
        assert_eq!(
            plan(&doc, RESTORE).startup_tab.url,
            "https://recent.test/",
            "the control: this document does have a tab to restore"
        );
        assert_eq!(
            plan(&doc, StartupBehavior::BlankTab).startup_tab.url,
            BLANK_TAB
        );
    }

    /// The row picks the URL and nothing else. Every profile the document
    /// names still loads, and the window still belongs to the active space -
    /// a blank tab in the wrong profile is a different profile's window.
    #[test]
    fn blank_tab_changes_the_url_and_leaves_the_profiles_alone() {
        let restored = plan(&two_profiles(), RESTORE);
        let blank = plan(&two_profiles(), StartupBehavior::BlankTab);
        assert_eq!(blank.required_profiles, restored.required_profiles);
        assert_eq!(blank.startup_profiles, restored.startup_profiles);
        assert_eq!(
            blank.startup_tab.profile_id,
            restored.startup_tab.profile_id
        );
    }

    /// A space nobody has visited has no timestamps at all, so its first tab
    /// is what restores.
    #[test]
    fn a_space_with_no_timestamps_restores_its_first_tab() {
        let doc = load(
            r##"{"spaces":[{"id":"A","name":"Work","emoji":"W","tint":"#111111",
            "profileID":"Work","pinnedTabs":[],
            "tabs":[{"url":"https://first.test/","title":"F"},
                    {"url":"https://second.test/","title":"S"}],
            "order":0,"groups":[]}],
            "appState":{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":3,
            "archiveEpoch":1,"sidebarWidthMigrated":true}}"##,
        );
        assert_eq!(plan(&doc, RESTORE).startup_tab.url, "https://first.test/");
    }

    /// A pinned tile is not a restore candidate. Restoring into one would
    /// leave it looking live while the space's own last tab stayed cold.
    #[test]
    fn a_space_with_only_pinned_tabs_restores_nothing() {
        let doc = load(
            r##"{"spaces":[{"id":"A","name":"Work","emoji":"W","tint":"#111111",
            "profileID":"Work","pinnedTabs":[{"url":"https://pin.test/","title":"P"}],
            "tabs":[],"order":0,"groups":[]}],
            "appState":{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":3,
            "archiveEpoch":1,"sidebarWidthMigrated":true}}"##,
        );
        let plan = plan(&doc, RESTORE);
        assert_eq!(plan.startup_tab.url, BLANK_TAB);
        assert_eq!(plan.startup_tab.profile_id, "Work");
    }

    /// The active id naming a space that is gone falls back to the first
    /// space, which is what the sidebar shows for the same document.
    #[test]
    fn a_dangling_active_id_falls_back_to_the_first_space() {
        let doc = load(
            r##"{"spaces":[{"id":"A","name":"Work","emoji":"W","tint":"#111111",
            "profileID":"Work","pinnedTabs":[],
            "tabs":[{"url":"https://work.test/","title":"W"}],"order":0,"groups":[]}],
            "appState":{"activeSpaceID":"deleted","sidebarWidth":220,
            "schemaVersion":3,"archiveEpoch":1,"sidebarWidthMigrated":true}}"##,
        );
        assert_eq!(plan(&doc, RESTORE).startup_tab.profile_id, "Work");
    }

    /// A traversal-shaped id is dropped here rather than reaching
    /// AppendASCII on the far side. The launch still gets a window.
    #[test]
    fn a_traversal_shaped_profile_id_never_leaves_the_crate() {
        let doc = load(
            r##"{"spaces":[{"id":"A","name":"Work","emoji":"W","tint":"#111111",
            "profileID":"../../etc","pinnedTabs":[],"tabs":[],"order":0,"groups":[]}],
            "appState":{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":3,
            "archiveEpoch":1,"sidebarWidthMigrated":true}}"##,
        );
        let plan = plan(&doc, RESTORE);
        assert_eq!(plan.required_profiles, vec![DEFAULT_PROFILE]);
        assert_eq!(plan.startup_tab.profile_id, DEFAULT_PROFILE);
        assert!(plan.startup_profiles.is_empty());
    }

    #[test]
    fn an_unsafe_id_is_refused_and_an_ordinary_one_is_not() {
        for id in ["Default", "Profile 1", "work-2", "a.b"] {
            assert!(is_safe_profile_id(id), "{id}");
        }
        for id in ["", "..", ".hidden", "a/b", "a\\b", "a:b", &"x".repeat(65)] {
            assert!(!is_safe_profile_id(id), "{id}");
        }
    }

    /// A document with no spaces at all still has to name a profile, or the
    /// launch shows nothing.
    #[test]
    fn an_empty_document_still_names_a_profile() {
        let doc = load(
            r##"{"spaces":[],"appState":{"sidebarWidth":220,"schemaVersion":3,
            "archiveEpoch":1,"sidebarWidthMigrated":true}}"##,
        );
        let plan = plan(&doc, RESTORE);
        assert_eq!(plan.required_profiles, vec![DEFAULT_PROFILE]);
        assert_eq!(
            plan.startup_tab,
            StartupTab {
                profile_id: DEFAULT_PROFILE.into(),
                url: BLANK_TAB.into(),
            }
        );
    }

    /// The names C++ reads. A rename here is silently a launch with one
    /// Default profile on the far side.
    #[test]
    fn the_payload_carries_the_keys_the_bootstrap_reads() {
        let json = encode(&plan(&two_profiles(), RESTORE));
        assert_eq!(
            json,
            r##"{"requiredProfiles":["Home","Work"],"startupProfiles":["Home"],"startupTab":{"profileID":"Home","url":"https://recent.test/"}}"##
        );
    }
}
