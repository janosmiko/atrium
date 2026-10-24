// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The profile list: the record a space points at, and the settings it owns.
//!
//! Same posture as `space_settings`: the three optional-string setters take
//! `Option<&str>` and collapse `None` and `""` to the unset state, which is the
//! contract `omittable_str` relies on, and an unrecognised value is REFUSED
//! rather than guessed - a value this build does not know is more likely a
//! newer build's than a typo.

use crate::choices;
use crate::search_engines::engine_id_ok;
use crate::spaces::{Document, Profile, Space, profile_id_is_safe};

/// The profile list's cap. Refused rather than dropping the oldest profile,
/// since each one is a distinct identity with its own favourites and settings
/// a drop would erase.
pub const MAX_PROFILES: usize = 200;

/// Creates a profile and returns its id, or `None` when the document is not
/// loaded or already at `MAX_PROFILES`. The id comes from the document's own
/// token counter - the crate has no RNG - spelled so `profile_id_is_safe`
/// accepts it, since it becomes a directory name under the user data dir.
pub fn add_profile(doc: &mut Document) -> Option<String> {
    if doc.profiles.len() >= MAX_PROFILES {
        return None;
    }
    let id = format!("profile-{}", doc.allocate_token());
    let name = format!("Profile {}", doc.profiles.len() + 1);
    doc.profiles.push(Profile {
        id: id.clone(),
        name,
        search_engine: None,
        hibernate: None,
        hib_threshold: None,
    });
    Some(id)
}

/// Renames a profile. An empty name does nothing - a profile's row always needs
/// one to show, the same rule `rename_space` holds for a space's dot.
pub fn rename_profile(doc: &mut Document, id: &str, name: &str) -> bool {
    if name.is_empty() {
        return false;
    }
    let Some(profile) = doc.profiles.iter_mut().find(|p| p.id == id) else {
        return false;
    };
    if profile.name == name {
        return false;
    }
    profile.name = name.to_string();
    true
}

/// Removes a profile record. REFUSED while any space names it: that space would
/// be left with no engine and no hibernate policy, and the record is the only
/// thing that carries them. The directory on disk is left alone either way -
/// deleting a user's cookies and logins is not what a settings row does.
pub fn delete_profile(doc: &mut Document, id: &str) -> bool {
    if spaces_on_profile(doc, id) > 0 {
        return false;
    }
    let Some(at) = doc.profiles.iter().position(|p| p.id == id) else {
        return false;
    };
    doc.profiles.remove(at);
    true
}

/// Points a space at another profile, by backing-array position. An id with no
/// record is refused rather than stored: a space on a profile the document does
/// not know reads back as having no settings at all.
pub fn set_space_profile(doc: &mut Document, space: usize, profile_id: &str) -> bool {
    if !profile_id_is_safe(profile_id) || !doc.profiles.iter().any(|p| p.id == profile_id) {
        return false;
    }
    let Some(space) = doc.spaces.get_mut(space) else {
        return false;
    };
    if space.profile_id == profile_id {
        return false;
    }
    space.profile_id = profile_id.to_string();
    true
}

/// How many spaces sit on `id`. The Profiles pane shows it, and `delete_profile`
/// refuses on it, so both read one function.
fn spaces_on_profile(doc: &Document, id: &str) -> usize {
    doc.spaces.iter().filter(|s| s.profile_id == id).count()
}

/// Sets one of a profile's own choice fields, or clears it. The shape
/// `set_space_choice` has, addressed by id rather than by index.
fn set_profile_choice<F, P>(
    doc: &mut Document,
    id: &str,
    value: Option<&str>,
    field: F,
    parse: P,
) -> bool
where
    F: FnOnce(&mut Profile) -> &mut Option<String>,
    P: FnOnce(&str) -> Result<String, ()>,
{
    let Some(profile) = doc.profiles.iter_mut().find(|p| p.id == id) else {
        return false;
    };
    let wanted = match value {
        None => None,
        Some("") => None,
        Some(v) => match parse(v) {
            Ok(canonical) => Some(canonical),
            Err(()) => return false,
        },
    };
    let slot = field(profile);
    if *slot == wanted {
        return false;
    }
    *slot = wanted;
    true
}

/// Sets a profile's search engine to any id matching the engine id shape. The
/// settings list holds the engines themselves.
pub fn set_profile_search_engine(doc: &mut Document, id: &str, engine: Option<&str>) -> bool {
    set_profile_choice(
        doc,
        id,
        engine,
        |p| &mut p.search_engine,
        |v| engine_id_ok(v).then(|| v.to_string()).ok_or(()),
    )
}

/// Sets a profile's hibernate mode. `None` or `""` clears it to Follow General.
pub fn set_profile_hibernate(doc: &mut Document, id: &str, mode: Option<&str>) -> bool {
    set_profile_choice(
        doc,
        id,
        mode,
        |p| &mut p.hibernate,
        |v| choices::parse_choice(choices::HIBERNATE_MODES, v),
    )
}

/// Sets a profile's own hibernation timeout rung, overriding the general
/// `memorySaverThreshold` ladder. `None` or `""` clears it to Follow General.
pub fn set_profile_hib_threshold(doc: &mut Document, id: &str, threshold: Option<&str>) -> bool {
    set_profile_choice(
        doc,
        id,
        threshold,
        |p| &mut p.hib_threshold,
        |v| choices::parse_choice(choices::MEMORY_SAVER_THRESHOLDS, v),
    )
}

/// The sidebar position of the space where a new tab of `profile_id` gets its
/// row. `None` when the window's space is that profile's, or no space is. Of
/// several spaces, the one the user last worked in with that profile wins.
pub fn space_for_profile(
    doc: &Document,
    active_space_index: Option<usize>,
    profile_id: &str,
) -> Option<usize> {
    if super::active_space(doc, active_space_index)
        .is_some_and(|a| doc.spaces[a].profile_id == profile_id)
    {
        return None;
    }
    let recency = |space: &Space| {
        space
            .tabs
            .iter()
            .chain(&space.pinned_tabs)
            .filter_map(|t| t.last_active_at)
            .fold(f64::MIN, f64::max)
    };
    super::ordered_positions(&doc.spaces)
        .into_iter()
        .enumerate()
        .filter(|&(_, i)| doc.spaces[i].profile_id == profile_id)
        // On a tie the earlier position wins.
        .max_by(|&(pa, a), &(pb, b)| {
            recency(&doc.spaces[a])
                .total_cmp(&recency(&doc.spaces[b]))
                .then(pb.cmp(&pa))
        })
        .map(|(position, _)| position)
}

/// Creates a space named after `profile_id` when no space uses that profile.
/// Without one, the profile's new tab gets a row in the space on screen,
/// which never draws that tab live. False when a space exists or at the cap.
pub fn ensure_space_for_profile(doc: &mut Document, profile_id: &str) -> bool {
    if profile_id.is_empty() || doc.spaces.iter().any(|s| s.profile_id == profile_id) {
        return false;
    }
    let name = doc
        .profiles
        .iter()
        .find(|p| p.id == profile_id)
        .map_or(profile_id, |p| p.name.as_str())
        .to_string();
    super::create_space(doc, &name, profile_id).is_some()
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::mutate::test_support::*;
    use crate::spaces::Profile;

    fn one_profile(id: &str, name: &str) -> Profile {
        Profile {
            id: id.into(),
            name: name.into(),
            search_engine: None,
            hibernate: None,
            hib_threshold: None,
        }
    }

    #[test]
    fn add_profile_hands_out_a_safe_id_and_a_numbered_name() {
        let mut doc = tokened(vec![space("Work", 0)]);
        doc.profiles.push(one_profile("p", "Work"));

        let id = add_profile(&mut doc).expect("a new profile");

        assert!(crate::spaces::profile_id_is_safe(&id), "{id}");
        assert_eq!(doc.profiles.len(), 2);
        assert_eq!(doc.profiles[1].id, id);
        assert_eq!(doc.profiles[1].name, "Profile 2");
    }

    #[test]
    fn a_profile_without_a_space_gets_one_named_after_it() {
        let mut doc = tokened(vec![space("Work", 0)]);
        doc.profiles.push(one_profile("q", "Travel"));

        assert!(ensure_space_for_profile(&mut doc, "q"));
        assert!(!ensure_space_for_profile(&mut doc, "q"), "one space is enough");
        assert!(!ensure_space_for_profile(&mut doc, "p"), "p already has Work");

        let made: Vec<_> = doc.spaces.iter().filter(|s| s.profile_id == "q").collect();
        assert_eq!(made.len(), 1);
        assert_eq!(made[0].name, "Travel");
    }

    #[test]
    fn add_profile_never_collides_with_an_existing_id() {
        let mut doc = tokened(vec![space("Work", 0)]);
        let first = add_profile(&mut doc).unwrap();
        let second = add_profile(&mut doc).unwrap();
        assert_ne!(first, second);
    }

    #[test]
    fn add_profile_past_the_cap_is_refused() {
        let mut doc = tokened(vec![space("Work", 0)]);
        while doc.profiles.len() < MAX_PROFILES {
            add_profile(&mut doc).unwrap();
        }
        assert_eq!(add_profile(&mut doc), None);
        assert_eq!(doc.profiles.len(), MAX_PROFILES);
    }

    #[test]
    fn rename_profile_takes_a_name_and_refuses_an_empty_one() {
        let mut doc = tokened(vec![space("Work", 0)]);
        doc.profiles.push(one_profile("p", "Work"));

        assert!(rename_profile(&mut doc, "p", "Personal"));
        assert_eq!(doc.profiles[0].name, "Personal");

        // A profile's row always needs a name to show, same rule rename_space
        // holds for a space's dot.
        assert!(!rename_profile(&mut doc, "p", ""));
        assert_eq!(doc.profiles[0].name, "Personal");
    }

    #[test]
    fn rename_profile_is_a_no_op_for_an_unknown_id_or_an_unchanged_name() {
        let mut doc = tokened(vec![space("Work", 0)]);
        doc.profiles.push(one_profile("p", "Work"));

        assert!(!rename_profile(&mut doc, "nope", "Personal"));
        assert!(!rename_profile(&mut doc, "p", "Work"));
    }

    /// The refusal the UI disables its button for. Deleting a record a space
    /// still names would leave that space with no engine and no policy, and
    /// nothing would say why.
    #[test]
    fn delete_profile_is_refused_while_a_space_uses_it() {
        let mut doc = tokened(vec![space("Work", 0)]);
        doc.profiles.push(one_profile("p", "Work"));

        assert!(!delete_profile(&mut doc, "p"));
        assert_eq!(doc.profiles.len(), 1);
    }

    #[test]
    fn delete_profile_removes_an_unused_record() {
        let mut doc = tokened(vec![space("Work", 0)]);
        doc.profiles.push(one_profile("p", "Work"));
        doc.profiles.push(one_profile("spare", "Spare"));

        assert!(delete_profile(&mut doc, "spare"));
        assert_eq!(doc.profiles.len(), 1);
        assert_eq!(doc.profiles[0].id, "p");
    }

    #[test]
    fn set_space_profile_moves_the_space() {
        let mut doc = tokened(vec![space("Work", 0)]);
        doc.profiles.push(one_profile("p", "Work"));
        doc.profiles.push(one_profile("other", "Other"));

        assert!(set_space_profile(&mut doc, 0, "other"));
        assert_eq!(doc.spaces[0].profile_id, "other");
    }

    #[test]
    fn set_space_profile_refuses_an_unknown_profile_and_an_unknown_space() {
        let mut doc = tokened(vec![space("Work", 0)]);
        doc.profiles.push(one_profile("p", "Work"));

        assert!(!set_space_profile(&mut doc, 0, "nope"));
        assert_eq!(doc.spaces[0].profile_id, "p");
        assert!(!set_space_profile(&mut doc, 9, "p"));
    }

    #[test]
    fn set_space_profile_is_a_no_op_when_unchanged() {
        let mut doc = tokened(vec![space("Work", 0)]);
        doc.profiles.push(one_profile("p", "Work"));

        assert!(!set_space_profile(&mut doc, 0, "p"));
    }

    #[test]
    fn set_profile_search_engine_stores_clears_and_rejects() {
        let mut doc = tokened(vec![space("Work", 0)]);
        doc.profiles.push(one_profile("p", "Work"));

        assert!(set_profile_search_engine(&mut doc, "p", Some("kagi")));
        assert_eq!(doc.profiles[0].search_engine.as_deref(), Some("kagi"));

        // Null and empty are the same request, the contract omittable_str relies on.
        assert!(set_profile_search_engine(&mut doc, "p", Some("")));
        assert_eq!(doc.profiles[0].search_engine, None);

        assert!(set_profile_search_engine(&mut doc, "p", Some("bing")));
        assert!(!set_profile_search_engine(&mut doc, "p", Some("Google")));
        assert_eq!(
            doc.profiles[0].search_engine.as_deref(),
            Some("bing"),
            "a refused write must not disturb what is there"
        );
    }

    /// Driven off ALL rather than a copied list, the same guard
    /// space_settings.rs keeps: the writer accepts exactly what the reader
    /// parses, so a variant added to one and not the other cannot pass.
    #[test]
    fn the_profile_writer_accepts_exactly_the_spellings_the_reader_parses() {
        use crate::choices::{HIBERNATE_MODES, MEMORY_SAVER_THRESHOLDS};

        for wire in HIBERNATE_MODES {
            let mut doc = tokened(vec![space("Work", 0)]);
            doc.profiles.push(one_profile("p", "Work"));
            assert!(
                set_profile_hibernate(&mut doc, "p", Some(wire)),
                "the reader parses {wire} but the writer refused it"
            );
            assert_eq!(Some(*wire), doc.profiles[0].hibernate.as_deref());
        }
        for wire in MEMORY_SAVER_THRESHOLDS {
            let mut doc = tokened(vec![space("Work", 0)]);
            doc.profiles.push(one_profile("p", "Work"));
            assert!(
                set_profile_hib_threshold(&mut doc, "p", Some(wire)),
                "the reader parses {wire} but the writer refused it"
            );
        }

        let mut doc = tokened(vec![space("Work", 0)]);
        doc.profiles.push(one_profile("p", "Work"));
        // "never" is a legal ARCHIVE rung and must not be a legal duration.
        assert!(!set_profile_hib_threshold(&mut doc, "p", Some("never")));
        assert!(!set_profile_hibernate(&mut doc, "p", Some("sometimes")));
    }

    #[test]
    fn every_profile_setter_ignores_an_unknown_id() {
        let mut doc = tokened(vec![space("Work", 0)]);
        doc.profiles.push(one_profile("p", "Work"));

        assert!(!set_profile_search_engine(&mut doc, "nope", Some("kagi")));
        assert!(!set_profile_hibernate(&mut doc, "nope", Some("always")));
        assert!(!set_profile_hib_threshold(
            &mut doc,
            "nope",
            Some("oneHour")
        ));
    }

    #[test]
    fn spaces_on_profile_counts_only_that_profile() {
        let mut doc = tokened(vec![space("Work", 0), space("Play", 1)]);
        doc.spaces[1].profile_id = "other".into();
        doc.profiles.push(one_profile("p", "Work"));
        doc.profiles.push(one_profile("other", "Play"));
        doc.profiles.push(one_profile("spare", "Spare"));

        assert_eq!(spaces_on_profile(&doc, "p"), 1);
        assert_eq!(spaces_on_profile(&doc, "other"), 1);
        assert_eq!(spaces_on_profile(&doc, "spare"), 0);
    }
}
