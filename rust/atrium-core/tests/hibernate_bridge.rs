// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! `lifecycle::space_for_token` joins a `tabs::Bindings` lookup
//! back to the owning `Space` - proved here without the C ABI.

use atrium_core::lifecycle;
use atrium_core::spaces::{self, Document};
use atrium_core::tabs::Bindings;

const EPOCH: f64 = 1_000_000.0;
const HOUR: f64 = 3600.0;

/// Two spaces, one "never", one Follow General, each on its own profile since
/// the override now lives there. Each has a single tab, so a lookup failure
/// reads as "found the wrong space" rather than a number.
fn doc_with_never_and_follow_spaces() -> Document {
    let json = format!(
        r##"{{"spaces":[
{{"id":"s1","name":"Quiet","emoji":"Q","tint":"#000000","profileID":"p1","order":0,
  "tabs":[{{"url":"https://never.example","title":"never"}}],"hibernate":"never"}},
{{"id":"s2","name":"Loud","emoji":"L","tint":"#FFFFFF","profileID":"p2","order":1,
  "tabs":[{{"url":"https://follow.example","title":"follow"}}]}}
],"appState":{{"sidebarWidth":220,"schemaVersion":3,"sidebarWidthMigrated":true}}}}"##
    );
    spaces::load(&json, EPOCH).expect("fixture must load")
}

fn token_for_url(doc: &Document, url: &str) -> u64 {
    doc.spaces
        .iter()
        .flat_map(|s| s.tabs.iter())
        .find(|t| t.url == url)
        .map(|t| t.token)
        .unwrap_or_else(|| panic!("no tab for {url}"))
}

#[test]
fn a_live_tab_resolves_to_the_space_that_owns_its_row() {
    let doc = doc_with_never_and_follow_spaces();
    let mut bindings = Bindings::new();
    bindings.bind(1, token_for_url(&doc, "https://never.example"));
    bindings.bind(2, token_for_url(&doc, "https://follow.example"));

    let token1 = bindings.token_of(1).expect("tab 1 is bound");
    let space1 = lifecycle::space_for_token(&doc, token1).expect("token1 resolves");
    assert_eq!(space1.id, "s1");

    let token2 = bindings.token_of(2).expect("tab 2 is bound");
    let space2 = lifecycle::space_for_token(&doc, token2).expect("token2 resolves");
    assert_eq!(space2.id, "s2");
}

/// Mirrors `atrium_core_hibernate_policy_for_tab`'s lookup chain.
#[test]
fn hibernate_policy_differs_between_a_never_tab_and_a_follow_tab() {
    let doc = doc_with_never_and_follow_spaces();
    let mut bindings = Bindings::new();
    bindings.bind(1, token_for_url(&doc, "https://never.example"));
    bindings.bind(2, token_for_url(&doc, "https://follow.example"));

    let policy_for = |tab_id: u64| -> Option<f64> {
        let token = bindings.token_of(tab_id)?;
        let space = lifecycle::space_for_token(&doc, token)?;
        lifecycle::hibernate_policy(lifecycle::profile_for_space(&doc, space), true, "twoHours")
    };

    assert_eq!(policy_for(1), None, "the Never space must stay awake");
    assert_eq!(
        policy_for(2),
        Some(2.0 * HOUR),
        "the Follow space uses the general timeout with the toggle on"
    );
}

#[test]
fn an_unbound_tab_id_resolves_to_no_space() {
    let doc = doc_with_never_and_follow_spaces();
    let bindings = Bindings::new();
    assert_eq!(bindings.token_of(99), None);
    let _ = doc; // no token to look up - nothing to resolve
}
