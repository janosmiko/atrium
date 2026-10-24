// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! A tab that opens in one profile's window while another profile's space shows.

use atrium_core::{
    atrium_core_close, atrium_core_open, atrium_core_open_tab, atrium_core_save,
    atrium_core_space_for_profile, spaces,
};

mod common;
use common::{TempDir, c};

const INSTANT: f64 = 788_918_400.0;
const DOCUMENT: &str = r##"{"spaces":[
{"id":"A","name":"Home","emoji":"H","tint":"#111111","profileID":"Default",
 "pinnedTabs":[],"tabs":[],"order":0,"groups":[]},
{"id":"C","name":"Late","emoji":"L","tint":"#111111","profileID":"Second",
 "pinnedTabs":[],"tabs":[{"url":"https://late.test/","title":"Late","lastActiveAt":900}],
 "order":2,"groups":[]},
{"id":"B","name":"Early","emoji":"E","tint":"#111111","profileID":"Second",
 "pinnedTabs":[],"tabs":[{"url":"https://early.test/","title":"Early","lastActiveAt":100}],
 "order":1,"groups":[]}],
"appState":{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":3,
"archiveEpoch":1,"sidebarWidthMigrated":true}}"##;

fn space_for(profile: &str) -> i32 {
    space_for_window(-1, profile)
}

fn space_for_window(window: i32, profile: &str) -> i32 {
    let profile = c(profile);
    unsafe { atrium_core_space_for_profile(window, profile.as_ptr()) }
}

#[test]
fn a_tab_of_another_profile_joins_that_profiles_last_used_space() {
    let _serial = common::abi_serial();
    let dir = TempDir::new("abi-space-for-profile");
    let path = dir.join("spaces.json");
    std::fs::write(&path, DOCUMENT).unwrap();
    atrium_core_close();
    let c_path = c(path.to_str().unwrap());
    assert!(unsafe { atrium_core_open(c_path.as_ptr(), INSTANT) });

    assert_eq!(
        space_for("Default"),
        -1,
        "the active space is already Default's"
    );
    // Late sits at position 2 in the sidebar, and its row is the newer one.
    assert_eq!(space_for("Second"), 2);

    let (url, title) = (c("https://new.test/"), c("New"));
    assert!(unsafe {
        atrium_core_open_tab(space_for("Second"), 7, url.as_ptr(), title.as_ptr(), -1)
    });
    atrium_core_save();
    let doc = spaces::load(&std::fs::read_to_string(&path).unwrap(), INSTANT).unwrap();
    let urls = |id: &str| -> Vec<String> {
        let space = doc.spaces.iter().find(|s| s.id == id).unwrap();
        space.tabs.iter().map(|t| t.url.clone()).collect()
    };
    assert!(urls("A").is_empty(), "the space on screen gains no row");
    assert!(urls("C").contains(&"https://new.test/".to_string()));

    atrium_core_close();
}

// The space on screen never draws another profile's tab live, so a row there
// stays cold.
#[test]
fn a_tab_of_a_profile_without_a_space_gets_a_new_space() {
    let _serial = common::abi_serial();
    let dir = TempDir::new("abi-space-for-spaceless-profile");
    let path = dir.join("spaces.json");
    std::fs::write(&path, DOCUMENT).unwrap();
    atrium_core_close();
    let c_path = c(path.to_str().unwrap());
    assert!(unsafe { atrium_core_open(c_path.as_ptr(), INSTANT) });

    let position = space_for("Nobody");
    assert_eq!(position, 3, "the new space goes to the end of the sidebar");
    assert_eq!(space_for("Nobody"), 3, "a second tab makes no second space");

    let (url, title) = (c("https://new.test/"), c("New"));
    assert!(unsafe { atrium_core_open_tab(position, 7, url.as_ptr(), title.as_ptr(), -1) });
    atrium_core_save();
    let doc = spaces::load(&std::fs::read_to_string(&path).unwrap(), INSTANT).unwrap();
    let made: Vec<_> = doc.spaces.iter().filter(|s| s.profile_id == "Nobody").collect();
    assert_eq!(made.len(), 1);
    assert_eq!(made[0].name, "Nobody", "a profile with no record is named by its id");
    assert_eq!(made[0].tabs[0].url, "https://new.test/");
    let home = doc.spaces.iter().find(|s| s.id == "A").unwrap();
    assert!(home.tabs.is_empty(), "the space on screen gains no row");

    atrium_core_close();
}

// Each window shows its own space, so the answer follows the window and not
// the document's last-used space.
#[test]
fn a_window_on_another_profiles_space_keeps_the_tab_in_its_own_space() {
    let _serial = common::abi_serial();
    let dir = TempDir::new("abi-space-for-window");
    let path = dir.join("spaces.json");
    std::fs::write(&path, DOCUMENT).unwrap();
    atrium_core_close();
    let c_path = c(path.to_str().unwrap());
    assert!(unsafe { atrium_core_open(c_path.as_ptr(), INSTANT) });

    assert_eq!(space_for_window(1, "Second"), 1, "Early is on screen in this window");
    assert_eq!(space_for_window(1, "Default"), 0, "Default's only space is Home");

    atrium_core_close();
}
