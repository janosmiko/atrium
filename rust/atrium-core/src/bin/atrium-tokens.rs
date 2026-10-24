// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Emits the design tokens as CSS, for `make tokens`.
//!
//! The Swift executable of the same name goes with the cut; this
//! replaces it and needs no Swift and no Chromium checkout.
//!
//! Writes to stdout, like the one it replaces, so `build/generate-tokens.sh`
//! redirects it into build/generated/tokens.css.

fn main() {
    print!("{}", atrium_core::tokens::css());
}
