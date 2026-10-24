// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! What Zen and Firefox share: profiles.ini, containers.json and the shape of
//! a session tab.

use std::collections::BTreeMap;

use serde_json::Value;

#[derive(Debug, Clone, PartialEq, Default)]
pub struct IniProfile {
    /// "Profiles/<folder>". The files map keys for this profile start with it.
    pub path: String,
    pub name: String,
    pub is_default: bool,
}

#[derive(Debug, Clone, PartialEq, Default)]
pub struct ProfilesIni {
    pub profiles: Vec<IniProfile>,
    /// Profiles outside the browser's Profiles folder. C++ never reads them.
    pub outside: usize,
}

/// From the Unix epoch, which Zen and Firefox count from, to the Cocoa epoch.
pub(crate) const UNIX_TO_COCOA_SECONDS: f64 = 978_307_200.0;

fn is_profile_section(name: &str) -> bool {
    name.strip_prefix("Profile").is_some_and(|n| n.parse::<u32>().is_ok())
}

/// The importer's C++ side joins this path onto the Zen root, so a `..`, an
/// absolute component, or a backslash here would let the read escape that root.
fn is_safe_profile_path(path: &str) -> bool {
    let Some(rest) = path.strip_prefix("Profiles/") else { return false };
    if rest.is_empty() || rest.contains('\\') || rest.contains('\0') {
        return false;
    }
    std::path::Path::new(rest).components().all(|c| matches!(c, std::path::Component::Normal(_)))
}

pub fn profiles_ini(text: &str) -> ProfilesIni {
    // A BOM before the first "[" is not part of any section header, so a
    // naive match on the first line drops that whole section silently.
    let text = text.strip_prefix('\u{feff}').unwrap_or(text);
    let mut sections: Vec<(&str, BTreeMap<&str, &str>)> = Vec::new();
    for line in text.lines().map(str::trim) {
        if let Some(name) = line.strip_prefix('[').and_then(|l| l.strip_suffix(']')) {
            sections.push((name, BTreeMap::new()));
        } else if let (Some((key, value)), Some((_, keys))) = (line.split_once('='), sections.last_mut()) {
            keys.insert(key.trim(), value.trim());
        }
    }
    let mut ini = ProfilesIni::default();
    for (_, keys) in sections.iter().filter(|(name, _)| is_profile_section(name)) {
        let Some(&path) = keys.get("Path") else { continue };
        if keys.get("IsRelative") != Some(&"1") || !is_safe_profile_path(path) {
            ini.outside += 1;
            continue;
        }
        let name = keys.get("Name").copied().unwrap_or(path);
        ini.profiles.push(IniProfile {
            path: path.into(),
            name: super::clamped(name),
            is_default: false,
        });
    }
    // Firefox's own order: the install's default, then the legacy flag.
    let installs = sections.iter().filter(|(n, _)| n.starts_with("Install")).filter_map(|(_, k)| k.get("Default"));
    let legacy = sections
        .iter()
        .filter(|(n, k)| is_profile_section(n) && k.get("Default") == Some(&"1"))
        .filter_map(|(_, k)| k.get("Path"));
    let chosen = installs.chain(legacy).find(|path| ini.profiles.iter().any(|p| p.path == **path));
    match chosen {
        Some(path) => ini.profiles.iter_mut().filter(|p| p.path == **path).for_each(|p| p.is_default = true),
        None => {
            if let [only] = ini.profiles.as_mut_slice() {
                only.is_default = true;
            }
        }
    }
    ini
}

/// "Profiles/abcd1234.default-release" is named "default-release".
fn folder_name(path: &str) -> &str {
    let folder = path.rsplit('/').next().unwrap_or(path);
    folder.split_once('.').map_or(folder, |(_, name)| name)
}

/// The ini entry for a profile folder, or one named after the folder. With no
/// ini, a lone folder is the default.
pub fn folder_profile(ini: &ProfilesIni, path: &str, folder_count: usize) -> IniProfile {
    ini.profiles.iter().find(|p| p.path == path).cloned().unwrap_or_else(|| IniProfile {
        path: path.into(),
        name: folder_name(path).into(),
        is_default: ini.profiles.is_empty() && folder_count == 1,
    })
}

/// The page a session tab shows. `index` counts from 1.
pub(crate) fn session_entry(tab: &Value) -> Option<&Value> {
    let entries = tab.get("entries")?.as_array()?;
    let index = tab.get("index").and_then(Value::as_u64).unwrap_or(entries.len() as u64) as usize;
    entries.get(index.saturating_sub(1)).or_else(|| entries.last())
}

/// Built-in containers carry an l10nId and no name. Firefox has spelled the
/// id two ways, "user-context-personal" and "userContextPersonal.label".
fn built_in(l10n_id: &str) -> Option<&'static str> {
    let id = l10n_id.to_ascii_lowercase();
    [("personal", "Personal"), ("work", "Work"), ("banking", "Banking"), ("shopping", "Shopping")]
        .into_iter()
        .find(|(key, _)| id.contains(key))
        .map(|(_, label)| label)
}

/// Public containers by userContextId. A private one is Firefox's own.
pub fn container_names(text: &str) -> Result<BTreeMap<u64, String>, String> {
    let root: Value =
        serde_json::from_str(text).map_err(|e| format!("containers.json could not be read: {e}"))?;
    Ok(root
        .get("identities")
        .and_then(Value::as_array)
        .into_iter()
        .flatten()
        .filter(|i| i.get("public").and_then(Value::as_bool).unwrap_or(false))
        .filter_map(|i| {
            let id = i.get("userContextId")?.as_u64().filter(|id| *id != 0)?;
            let name = super::text(i, "name")
                .map(super::clamped)
                .or_else(|| built_in(super::text(i, "l10nId")?).map(String::from))
                .unwrap_or_else(|| format!("Container {id}"));
            Some((id, name))
        })
        .collect())
}

#[cfg(test)]
mod tests {
    use super::*;

    const INI: &str = "\
[Install0000000000000000]
Default=Profiles/b.second

[Profile0]
Name=First
IsRelative=1
Path=Profiles/a.first
Default=1

[Profile1]
Name=Second
IsRelative=1
Path=Profiles/b.second

[Profile2]
Name=Away
IsRelative=0
Path=/Volumes/Other/away

[General]
Version=2
";

    fn profile(path: &str, name: &str, is_default: bool) -> IniProfile {
        IniProfile { path: path.into(), name: name.into(), is_default }
    }

    #[test]
    fn the_install_default_wins_and_absolute_paths_are_counted() {
        assert_eq!(
            profiles_ini(INI),
            ProfilesIni {
                profiles: vec![
                    profile("Profiles/a.first", "First", false),
                    profile("Profiles/b.second", "Second", true),
                ],
                outside: 1,
            }
        );
    }

    #[test]
    fn without_an_install_section_default_1_wins_then_a_lone_profile() {
        let legacy = INI.replace("[Install0000000000000000]\nDefault=Profiles/b.second\n", "");
        assert!(profiles_ini(&legacy).profiles[0].is_default);
        let lone = "[Profile0]\nName=Only\nIsRelative=1\nPath=Profiles/x.only\n";
        assert_eq!(profiles_ini(lone).profiles, [profile("Profiles/x.only", "Only", true)]);
    }

    #[test]
    fn a_leading_bom_does_not_drop_the_first_section() {
        let bommed = "\u{feff}[Profile0]\nName=Only\nIsRelative=1\nPath=Profiles/x.only\n";
        assert_eq!(profiles_ini(bommed).profiles, [profile("Profiles/x.only", "Only", true)]);
    }

    #[test]
    fn a_profile_section_with_no_path_is_skipped_and_not_counted() {
        let text = "[Profile0]\nName=Only\nIsRelative=1\nPath=Profiles/x.only\n\n\
                     [Profile1]\nName=NoPath\nIsRelative=1\n";
        let ini = profiles_ini(text);
        assert_eq!(ini.profiles, [profile("Profiles/x.only", "Only", true)]);
        assert_eq!(ini.outside, 0);
    }

    #[test]
    fn a_path_that_escapes_profiles_is_counted_as_outside() {
        for path in ["Profiles/../x", "Profiles//abs", "Profiles/a/../../x", "Profiles/a\\b", "Profiles/"] {
            let text = format!("[Profile0]\nName=X\nIsRelative=1\nPath={path}\n");
            let ini = profiles_ini(&text);
            assert_eq!(ini.profiles, [], "{path}");
            assert_eq!(ini.outside, 1, "{path}");
        }
    }

    #[test]
    fn a_nested_relative_path_is_still_accepted() {
        let text = "[Profile0]\nName=X\nIsRelative=1\nPath=Profiles/a/b\n";
        let ini = profiles_ini(text);
        assert_eq!(ini.profiles, [profile("Profiles/a/b", "X", true)]);
        assert_eq!(ini.outside, 0);
    }

    #[test]
    fn public_containers_keep_their_name_or_the_built_in_label() {
        let json = r#"{"version":5,"identities":[
            {"userContextId":1,"public":true,"l10nId":"user-context-personal"},
            {"userContextId":2,"public":true,"name":"Client"},
            {"userContextId":3,"public":true,"l10nId":"userContextShopping.label"},
            {"userContextId":7,"public":true},
            {"userContextId":4294967295,"public":false,"name":"userContextIdInternal.thumbnail"}
        ]}"#;
        let names = container_names(json).unwrap();
        assert_eq!(
            names.into_iter().collect::<Vec<_>>(),
            [
                (1, "Personal".to_string()),
                (2, "Client".to_string()),
                (3, "Shopping".to_string()),
                (7, "Container 7".to_string()),
            ]
        );
        assert!(container_names("{").is_err());
    }

    #[test]
    fn a_folder_missing_from_the_ini_is_named_after_its_folder() {
        let ini = profiles_ini(INI);
        assert_eq!(folder_profile(&ini, "Profiles/b.second", 3), profile("Profiles/b.second", "Second", true));
        assert_eq!(folder_profile(&ini, "Profiles/c.third", 3), profile("Profiles/c.third", "third", false));
        let none = ProfilesIni::default();
        assert_eq!(folder_profile(&none, "Profiles/x.only", 1), profile("Profiles/x.only", "only", true));
        assert_eq!(folder_profile(&none, "Profiles/x.only", 2), profile("Profiles/x.only", "only", false));
    }

    #[test]
    fn a_session_tab_shows_the_entry_at_its_index_or_the_last_one() {
        let tab = serde_json::json!({"entries": [{"url": "a"}, {"url": "b"}], "index": 1});
        assert_eq!(session_entry(&tab).unwrap()["url"], "a");
        let past = serde_json::json!({"entries": [{"url": "a"}, {"url": "b"}], "index": 9});
        assert_eq!(session_entry(&past).unwrap()["url"], "b");
        let no_index = serde_json::json!({"entries": [{"url": "a"}, {"url": "b"}]});
        assert_eq!(session_entry(&no_index).unwrap()["url"], "b");
        assert!(session_entry(&serde_json::json!({})).is_none());
    }
}
