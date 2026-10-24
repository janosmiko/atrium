// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Drives the golden store corpus through the Rust core.
//!
//! `build/corpus-check.py` calls this once per case as
//! `corpus-runner <store> <file>`. It loads the file through the named store
//! and writes the loaded state back to the same path, which is the contract
//! any implementation has to meet - see `fixtures/stores/README.md`.
//!
//!     make corpus-check RUNNER="$(cargo run -q --bin corpus-runner --)"
//!
//! It reads and writes a temporary copy the checker made, never a fixture.

use std::process::ExitCode;

/// The instant a v1 spaces.json stamps `archiveEpoch` from. The same fixed
/// value the Swift harness uses, so the two produce the same bytes:
/// 2026-01-01T00:00:00Z, in seconds since Swift's reference date.
const MIGRATION_INSTANT: f64 = 788_918_400.0;

fn main() -> ExitCode {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 3 {
        eprintln!("usage: corpus-runner <store> <file>");
        return ExitCode::FAILURE;
    }
    let (name, path) = (&args[1], &args[2]);

    let Some(store) = atrium_core::Store::from_name(name) else {
        eprintln!("unknown store: {name}");
        return ExitCode::FAILURE;
    };
    let input = match std::fs::read_to_string(path) {
        Ok(s) => s,
        Err(e) => {
            eprintln!("could not read {path}: {e}");
            return ExitCode::FAILURE;
        }
    };

    match atrium_core::query(store, &input, MIGRATION_INSTANT) {
        Ok(out) => match std::fs::write(path, out) {
            Ok(()) => ExitCode::SUCCESS,
            Err(e) => {
                eprintln!("could not write {path}: {e}");
                ExitCode::FAILURE
            }
        },
        Err(why) => {
            eprintln!("{name}: {why}");
            ExitCode::FAILURE
        }
    }
}
