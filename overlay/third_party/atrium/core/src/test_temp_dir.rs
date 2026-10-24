// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// `tests/common/mod.rs` includes this file by path: tests/ is a separate crate.
// It lives in src/ because the GN build has no tests/ directory.

// Each test binary compiles its own copy and may skip a method.
#![allow(dead_code)]

use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU32, Ordering};

/// A temp directory that removes itself, including when the test panics.
pub struct TempDir {
    path: PathBuf,
}

impl TempDir {
    /// Named by `tag`, the process id and a counter, so two tests that pass
    /// the same tag get different directories.
    pub fn new(tag: &str) -> Self {
        static COUNTER: AtomicU32 = AtomicU32::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let path = std::env::temp_dir().join(format!("atrium-{tag}-{}-{n}", std::process::id()));
        // A run that died hard leaves its directory behind, and a test must
        // not read a file it never wrote.
        let _ = std::fs::remove_dir_all(&path);
        std::fs::create_dir_all(&path).expect("temp dir");
        Self { path }
    }

    pub fn path(&self) -> &Path {
        &self.path
    }

    pub fn join(&self, leaf: &str) -> PathBuf {
        self.path.join(leaf)
    }
}

impl Drop for TempDir {
    fn drop(&mut self) {
        // Best effort. A failure must not mask the test's own result.
        let _ = std::fs::remove_dir_all(&self.path);
    }
}
