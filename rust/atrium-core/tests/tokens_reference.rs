// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! the Rust token generator against the committed reference.
//!
//! `build/generated/tokens.css` is what the Swift generator emitted, and the
//! WebUI serves a copy of it. This asserts the Rust generator produces the same
//! bytes - not the same values, the same bytes, because the formatting is the
//! half that goes wrong without anyone noticing.

use std::path::PathBuf;

mod common;
use common::{read_fixture, repo_path};

fn reference() -> PathBuf {
    repo_path("build/generated/tokens.css")
}

#[test]
fn rust_emits_the_committed_tokens_css() {
    let want = read_fixture(&reference());
    assert_eq!(
        atrium_core::tokens::css(),
        want,
        "the Rust token generator drifted from build/generated/tokens.css"
    );
}
