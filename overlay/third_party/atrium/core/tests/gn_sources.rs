// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! `BUILD.gn` has to list every module in `src/`.
//!
//! GN builds the crate from its own `sources` list, and Chromium's rustc
//! wrapper compares that list against what rustc actually read: a module on
//! disk but not in the list fails the whole Chromium build with "Rust source
//! file or input not in GN sources". Cargo never looks at `BUILD.gn`, so
//! `make rust-test` stays green while nothing else in the tree can compile -
//! which is how a new module landed and broke every other lane.
//!
//! Lives in `tests/` on purpose. GN compiles `src/` and nothing else, so this
//! file is invisible to the build it is checking, and `env!` resolving a cargo
//! variable stays legal.

use std::collections::BTreeSet;
use std::path::PathBuf;

fn crate_dir() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
}

/// The module files GN is told about, as paths relative to the crate root.
fn gn_sources() -> BTreeSet<String> {
    let gn = std::fs::read_to_string(crate_dir().join("BUILD.gn")).expect("BUILD.gn must be here");
    gn.lines()
        .filter_map(|line| {
            let quoted = line.trim().trim_end_matches(',').trim_matches('"');
            quoted.ends_with(".rs").then(|| quoted.to_string())
        })
        .filter(|path| path.starts_with("src/"))
        .collect()
}

/// The crate's own modules. `src/bin/` is deliberately not walked: GN builds
/// the static library, and the binaries are cargo-only tools.
///
/// DIRECTORY modules are walked one level down, because a module that grows
/// into a directory would otherwise leave this guard covering nothing: before
/// `mutate.rs` was split, every file sat at the top level and a flat read
/// was enough. A file added under `src/mutate/` after that would have been
/// missing from BUILD.gn with nothing to say so.
fn module_files() -> BTreeSet<String> {
    let src = crate_dir().join("src");
    let mut out = BTreeSet::new();
    for entry in std::fs::read_dir(&src).expect("src must be here") {
        let Ok(entry) = entry else { continue };
        let Ok(name) = entry.file_name().into_string() else {
            continue;
        };
        if name.ends_with(".rs") {
            out.insert(format!("src/{name}"));
            continue;
        }
        // `bin` holds cargo-only tools GN never compiles.
        if name == "bin" || !entry.path().is_dir() {
            continue;
        }
        for nested in std::fs::read_dir(entry.path()).expect("a module directory") {
            let Ok(nested) = nested else { continue };
            let Ok(leaf) = nested.file_name().into_string() else {
                continue;
            };
            if leaf.ends_with(".rs") {
                out.insert(format!("src/{name}/{leaf}"));
            }
        }
    }
    out
}

#[test]
fn every_module_in_src_is_listed_in_build_gn() {
    let listed = gn_sources();
    let present = module_files();

    let missing: Vec<&String> = present.difference(&listed).collect();
    assert!(
        missing.is_empty(),
        "these modules exist but BUILD.gn does not list them, so the Chromium \
         build will fail with \"Rust source file or input not in GN sources\": \
         {missing:?}"
    );

    let stale: Vec<&String> = listed.difference(&present).collect();
    assert!(
        stale.is_empty(),
        "BUILD.gn lists modules that are gone: {stale:?}"
    );
}
