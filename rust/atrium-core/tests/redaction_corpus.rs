// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! holds the Rust redactor to the answers the Swift one gives.
//!
//! `fixtures/redaction/cases.json` is written by `RedactionCorpusTests` on the
//! Swift side. Reading it here is what makes "produces the same result as the
//! Swift store" a check rather than a claim.
//!
//! A missing fixture fails these tests. It used to skip, and a skip that
//! reports as a pass is a defect.

use std::path::PathBuf;

use serde::Deserialize;

mod common;
use common::{read_fixture, repo_path};

#[derive(Deserialize)]
struct Case {
    input: String,
    expected: Option<String>,
}

#[derive(Deserialize)]
struct Corpus {
    cases: Vec<Case>,
}

fn corpus_path() -> PathBuf {
    repo_path("fixtures/redaction/cases.json")
}

fn load() -> Corpus {
    let text = read_fixture(&corpus_path());
    serde_json::from_str(&text)
        .unwrap_or_else(|err| panic!("{} is not a corpus: {err}", corpus_path().display()))
}

#[test]
fn the_rust_redactor_gives_the_same_answers_as_swift() {
    let corpus = load();
    assert!(!corpus.cases.is_empty(), "the corpus is empty");

    let mut checked = 0;
    for case in &corpus.cases {
        let Some(want) = &case.expected else {
            panic!(
                "case {:?} has no expected answer - run ATRIUM_CORPUS_REGENERATE=1 make test",
                case.input
            );
        };
        assert_eq!(
            &atrium_core::redact::redact(&case.input),
            want,
            "input {:?}",
            case.input
        );
        checked += 1;
    }
    assert!(checked >= 40, "only {checked} cases - the corpus shrank");
}

/// Redacting an already-redacted URL must give the same answer, or the same
/// tab's URL changes on every save.
#[test]
fn every_answer_is_stable_under_a_second_pass() {
    let corpus = load();
    for case in &corpus.cases {
        let once = atrium_core::redact::redact(&case.input);
        assert_eq!(atrium_core::redact::redact(&once), once, "{:?}", case.input);
    }
}
