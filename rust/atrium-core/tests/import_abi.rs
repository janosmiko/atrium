// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The import exports as C++ calls them. They act on the process-wide core,
//! so every test takes abi_serial().

use atrium_core::{
    atrium_core_close, atrium_core_import_preview, atrium_core_import_run, atrium_core_is_open,
    atrium_core_mozlz4_decode, atrium_core_save,
};

mod common;
use common::{TempDir, abi_serial, c, take_string};

fn files(dir: &str) -> String {
    serde_json::to_string(&common::arc_fixture_files(dir)).unwrap()
}

fn json(raw: *mut std::ffi::c_char) -> serde_json::Value {
    serde_json::from_str(&unsafe { take_string(raw) }.expect("a non-null answer")).unwrap()
}

#[test]
fn a_preview_without_files_is_not_detected() {
    let _guard = abi_serial();
    let arc = c("arc");
    let answer = json(unsafe { atrium_core_import_preview(arc.as_ptr(), std::ptr::null()) });
    assert_eq!(answer, serde_json::json!({"source": "arc", "detected": false}));
}

#[test]
fn a_preview_counts_the_fixture_and_reports_a_broken_file() {
    let _guard = abi_serial();
    let arc = c("arc");
    let basic = c(&files("basic"));
    let answer = json(unsafe { atrium_core_import_preview(arc.as_ptr(), basic.as_ptr()) });
    assert_eq!(answer["detected"], true);
    assert_eq!(answer["summary"]["spaces"], 3);
    assert_eq!(answer["summary"]["tabs"], 6);

    let broken = c(&files("broken"));
    let answer = json(unsafe { atrium_core_import_preview(arc.as_ptr(), broken.as_ptr()) });
    assert!(answer["error"].as_str().unwrap().contains("could not be read"), "{answer}");
}

#[test]
fn an_import_on_a_fresh_profile_writes_spaces_and_returns_the_routes() {
    let _guard = abi_serial();
    let dir = TempDir::new("import-fresh");
    let path = c(dir.join("spaces.json").to_str().unwrap());
    atrium_core_close();

    let (arc, basic, routing) = (c("arc"), c(&files("basic")), c(""));
    let no_engines = c("");
    let answer = json(unsafe {
        atrium_core_import_run(
            arc.as_ptr(),
            basic.as_ptr(),
            routing.as_ptr(),
            no_engines.as_ptr(),
            path.as_ptr(),
        )
    });

    assert_eq!(answer["report"]["spaces"]["added"], 3, "{answer}");
    assert_eq!(answer["report"]["routes"]["added"], 3, "the bad regex is dropped");
    assert_eq!(answer["report"]["profileIds"]["default"], "Default", "{answer}");
    assert!(
        answer["report"]["profileIds"]["Profile 1"].as_str().unwrap_or("").starts_with("profile-"),
        "{answer}"
    );
    assert_eq!(answer["routing"]["rules"].as_array().unwrap().len(), 3);
    assert!(answer["searchEngines"].is_null(), "{answer}");
    assert_eq!(atrium_core_save(), 1);
    let written = std::fs::read_to_string(dir.join("spaces.json")).unwrap();
    assert!(written.contains("\"Work\""));
    assert!(dir.join("themes.json").exists(), "Work and Home brought themes");
    atrium_core_close();
}

#[test]
fn an_import_never_replaces_a_store_it_did_not_open() {
    let _guard = abi_serial();
    let dir = TempDir::new("import-existing");
    std::fs::write(dir.join("spaces.json"), "{}").unwrap();
    let path = c(dir.join("spaces.json").to_str().unwrap());
    atrium_core_close();
    let (arc, basic, routing) = (c("arc"), c(&files("basic")), c(""));
    let no_engines = c("");
    let answer = json(unsafe {
        atrium_core_import_run(
            arc.as_ptr(),
            basic.as_ptr(),
            routing.as_ptr(),
            no_engines.as_ptr(),
            path.as_ptr(),
        )
    });
    assert!(answer["error"].is_string(), "the file exists: {answer}");
    assert!(!atrium_core_is_open());
    assert_eq!(std::fs::read_to_string(dir.join("spaces.json")).unwrap(), "{}");
}

#[test]
fn a_broken_file_on_a_fresh_profile_leaves_the_core_closed() {
    let _guard = abi_serial();
    let dir = TempDir::new("import-fresh-broken");
    let path = c(dir.join("spaces.json").to_str().unwrap());
    atrium_core_close();
    let (arc, broken, routing) = (c("arc"), c(&files("broken")), c(""));
    let no_engines = c("");
    let answer = json(unsafe {
        atrium_core_import_run(
            arc.as_ptr(),
            broken.as_ptr(),
            routing.as_ptr(),
            no_engines.as_ptr(),
            path.as_ptr(),
        )
    });
    assert!(answer["error"].is_string(), "{answer}");
    assert!(!atrium_core_is_open(), "no empty document is left open");
    assert_eq!(atrium_core_save(), 0);
    assert!(!dir.join("spaces.json").exists(), "nothing is written");
}

#[test]
fn a_failed_import_changes_nothing() {
    let _guard = abi_serial();
    let dir = TempDir::new("import-broken");
    let path = c(dir.join("spaces.json").to_str().unwrap());
    atrium_core_close();
    let (arc, basic, broken, routing, none) =
        (c("arc"), c(&files("basic")), c(&files("broken")), c(""), c(""));
    let no_engines = c("");
    let answer = json(unsafe {
        atrium_core_import_run(
            arc.as_ptr(),
            basic.as_ptr(),
            routing.as_ptr(),
            no_engines.as_ptr(),
            path.as_ptr(),
        )
    });
    assert!(answer["report"].is_object(), "{answer}");
    assert_eq!(atrium_core_save(), 1, "the first import is written once");

    let answer = json(unsafe {
        atrium_core_import_run(
            arc.as_ptr(),
            broken.as_ptr(),
            routing.as_ptr(),
            no_engines.as_ptr(),
            none.as_ptr(),
        )
    });
    assert!(answer["error"].is_string(), "{answer}");
    assert_eq!(atrium_core_save(), 0, "nothing moved");

    let refused = c(r#"{"schemaVersion":999,"rules":[]}"#);
    let answer = json(unsafe {
        atrium_core_import_run(
            arc.as_ptr(),
            basic.as_ptr(),
            refused.as_ptr(),
            no_engines.as_ptr(),
            none.as_ptr(),
        )
    });
    assert!(answer["error"].is_string(), "a routing file this build cannot read stops the import");
    assert_eq!(atrium_core_save(), 0, "nothing moved");
    atrium_core_close();
}

#[test]
fn an_import_with_nothing_open_and_no_path_is_an_error() {
    let _guard = abi_serial();
    atrium_core_close();
    let (arc, basic, routing, none) = (c("arc"), c(&files("basic")), c(""), c(""));
    let no_engines = c("");
    let answer = json(unsafe {
        atrium_core_import_run(
            arc.as_ptr(),
            basic.as_ptr(),
            routing.as_ptr(),
            no_engines.as_ptr(),
            none.as_ptr(),
        )
    });
    assert!(answer["error"].is_string());
    assert!(!atrium_core_is_open());
}

#[test]
fn an_engine_list_that_does_not_parse_changes_nothing() {
    let _guard = abi_serial();
    atrium_core_close();
    let (arc, basic, routing, engines, none) = (c("arc"), c(&files("basic")), c(""), c("not json"), c(""));
    let answer = json(unsafe {
        atrium_core_import_run(arc.as_ptr(), basic.as_ptr(), routing.as_ptr(), engines.as_ptr(), none.as_ptr())
    });
    assert_eq!(answer["error"], "the search engine list could not be read", "{answer}");
    assert!(!atrium_core_is_open());
}

#[test]
fn a_chrome_import_appends_its_engines_and_sets_the_new_profiles_default() {
    let _guard = abi_serial();
    let dir = TempDir::new("import-chrome");
    let path = c(dir.join("spaces.json").to_str().unwrap());
    atrium_core_close();
    let files = c(&serde_json::to_string(&common::chrome_fixture_files("basic")).unwrap());
    let (chrome, routing) = (c("chrome"), c(""));
    let engines = c(
        r#"[{"id":"google","name":"Google","keyword":"google.com","url":"https://www.google.com/search?q=%s","enabled":true,"builtin":true}]"#,
    );

    let answer = json(unsafe {
        atrium_core_import_run(chrome.as_ptr(), files.as_ptr(), routing.as_ptr(), engines.as_ptr(), path.as_ptr())
    });

    assert_eq!(
        answer["report"]["engines"],
        serde_json::json!({"added": 3, "duplicate": 1, "dropped": 0}),
        "{answer}"
    );
    let ids: Vec<&str> = answer["searchEngines"]
        .as_array()
        .unwrap()
        .iter()
        .map(|e| e["id"].as_str().unwrap())
        .collect();
    assert_eq!(ids, ["google", "import-chrome-1", "import-chrome-2", "import-chrome-3"]);
    assert_eq!(atrium_core_save(), 1);
    let doc: serde_json::Value =
        serde_json::from_str(&std::fs::read_to_string(dir.join("spaces.json")).unwrap()).unwrap();
    let work = doc["profiles"]
        .as_array()
        .unwrap()
        .iter()
        .find(|p| p["name"] == "Work")
        .expect("the Work profile");
    assert_eq!(work["searchEngine"], "import-chrome-1", "Work was created, so its default applies");
    atrium_core_close();
}

#[test]
fn a_fresh_documents_default_profile_gets_its_own_default_engine() {
    // The "basic" fixture's Default folder points at Chrome's prepopulated
    // Google entry, which chrome.rs always skips as Chrome's own engine, so
    // it cannot exercise this path. This fixture's default is a plain one.
    let _guard = abi_serial();
    let dir = TempDir::new("import-chrome-default-engine");
    let path = c(dir.join("spaces.json").to_str().unwrap());
    atrium_core_close();
    let files = c(&serde_json::to_string(&common::chrome_fixture_files("default-engine")).unwrap());
    let (chrome, routing, engines) = (c("chrome"), c(""), c("[]"));

    let answer = json(unsafe {
        atrium_core_import_run(chrome.as_ptr(), files.as_ptr(), routing.as_ptr(), engines.as_ptr(), path.as_ptr())
    });

    assert_eq!(
        answer["report"]["engines"],
        serde_json::json!({"added": 1, "duplicate": 0, "dropped": 0}),
        "{answer}"
    );
    assert_eq!(atrium_core_save(), 1);
    let doc: serde_json::Value =
        serde_json::from_str(&std::fs::read_to_string(dir.join("spaces.json")).unwrap()).unwrap();
    let default = doc["profiles"]
        .as_array()
        .unwrap()
        .iter()
        .find(|p| p["id"] == "Default")
        .expect("the Default profile");
    assert_eq!(default["searchEngine"], "import-chrome-1", "a first import into a fresh document sets Default too");
    atrium_core_close();
}

#[test]
fn a_firefox_preview_counts_the_fixture() {
    let _guard = abi_serial();
    let files = c(&serde_json::to_string(&common::firefox_fixture_files("basic")).unwrap());
    let firefox = c("firefox");
    let answer = json(unsafe { atrium_core_import_preview(firefox.as_ptr(), files.as_ptr()) });
    assert_eq!(answer["detected"], true, "{answer}");
    assert_eq!(answer["summary"]["tabs"], 11, "{answer}");
    assert_eq!(answer["summary"]["engines"], 2, "{answer}");
}

#[test]
fn a_firefox_import_adds_its_engines_and_sets_the_default_profiles_engine() {
    let _guard = abi_serial();
    let dir = TempDir::new("import-firefox");
    let path = c(dir.join("spaces.json").to_str().unwrap());
    atrium_core_close();
    let files = c(&serde_json::to_string(&common::firefox_fixture_files("basic")).unwrap());
    let (firefox, routing) = (c("firefox"), c(""));
    let engines = c(
        r#"[{"id":"google","name":"Google","keyword":"google.com","url":"https://www.google.com/search?q=%s","enabled":true,"builtin":true}]"#,
    );

    let answer = json(unsafe {
        atrium_core_import_run(firefox.as_ptr(), files.as_ptr(), routing.as_ptr(), engines.as_ptr(), path.as_ptr())
    });

    assert_eq!(
        answer["report"]["engines"],
        serde_json::json!({"added": 2, "duplicate": 0, "dropped": 0}),
        "{answer}"
    );
    let ids: Vec<&str> = answer["searchEngines"]
        .as_array()
        .unwrap()
        .iter()
        .map(|e| e["id"].as_str().unwrap())
        .collect();
    assert_eq!(ids, ["google", "import-firefox-1", "import-firefox-2"]);
    assert_eq!(atrium_core_save(), 1);
    let doc: serde_json::Value =
        serde_json::from_str(&std::fs::read_to_string(dir.join("spaces.json")).unwrap()).unwrap();
    let default = doc["profiles"]
        .as_array()
        .unwrap()
        .iter()
        .find(|p| p["id"] == "Default")
        .expect("the Default profile");
    assert_eq!(default["searchEngine"], "import-firefox-1", "the ini default maps to Default");
    atrium_core_close();
}

fn packed(size: u32, block: &[u8]) -> Vec<u8> {
    [b"mozLz40\0".as_slice(), &size.to_le_bytes(), block].concat()
}

#[test]
fn mozlz4_decode_returns_the_text_or_null() {
    let decode = |bytes: &[u8]| unsafe { take_string(atrium_core_mozlz4_decode(bytes.as_ptr().cast(), bytes.len())) };
    assert_eq!(decode(&packed(5, b"\x50hello")).as_deref(), Some("hello"));
    assert_eq!(decode(&packed(1, b"\x10\x00")), None, "a NUL cannot cross as a C string");
    assert_eq!(decode(&packed(1, b"\x10\xff")), None, "not UTF-8");
    assert_eq!(decode(b"mozLz4"), None);
    assert!(unsafe { atrium_core_mozlz4_decode(std::ptr::null(), 0) }.is_null());
}
