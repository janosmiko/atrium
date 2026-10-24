// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Which persisted-file schema versions this build will load.
//!
//! Ported from `SchemaAcceptance.swift`. See `docs/model-rules.md` R1.

/// True when a file at `file_version` can be read by a build whose schema is
/// `current_version`.
///
/// Older is accepted: the caller migrates forward. Newer is refused, because a
/// future build's shape is unknown, and refusing WITHOUT overwriting is what
/// makes downgrade-then-upgrade lossless. Versions start at 1, so anything
/// below that is a hand edit or a truncated write rather than an older format.
pub fn can_load(file_version: i64, current_version: i64) -> bool {
    file_version >= 1 && file_version <= current_version
}

/// Why a load produced nothing usable.
///
/// Shared by the stores rather than owned by one of them, so all six refuse in
/// the same words and a writer can be written once. It lives here
/// rather than in `spaces`, which still re-exports it under its old name.
///
/// Both variants mean the same thing to a writer: what is on disk is not this
/// build's to replace (`docs/model-rules.md` R1.2).
#[derive(Debug, PartialEq)]
pub enum LoadError {
    /// The bytes are not the document at all.
    Unreadable(String),
    /// A version this build does not know. The caller must NOT write over the
    /// file - see `docs/model-rules.md` R1.2.
    Refused { file_version: i64 },
}

impl std::fmt::Display for LoadError {
    /// Starts mid-sentence ON PURPOSE. The error does not know which file it
    /// came from, so the caller prefixes the name: "spaces.json " plus this
    /// gives the sentence the three call sites used to spell out by hand, and
    /// the wording can no longer part between them.
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Refused { file_version } => write!(
                f,
                "is at schema {file_version}, which this build cannot read"
            ),
            Self::Unreadable(why) => write!(f, "did not parse: {why}"),
        }
    }
}

impl std::error::Error for LoadError {}

/// Everything that can stop a store reaching or leaving the disk.
///
/// One type for the read path and the write path, because the interesting
/// half is shared: `Schema` is the R1.2 refusal, and a caller matching on it
/// learns "this file is not mine to replace" WITHOUT reading a message. That
/// was a past defect - the discriminant was flattened to a
/// String at every public boundary, so no caller could tell a refusal from a
/// corrupt file, and each new one invented a fifth wording.
///
/// `Io` carries no path. The caller passed the path in and still has it, so
/// putting it back into the message here would only let the two spellings
/// disagree again.
#[derive(Debug)]
pub enum StoreError {
    /// The bytes on disk are refused or unparseable. Do not write over them.
    Schema(LoadError),
    /// The file could not be read or written at all.
    Io(std::io::Error),
    /// This build could not serialise its OWN output. Not a data condition -
    /// if this is ever seen, it is a bug in the model, not in the file.
    Encode(String),
}

impl std::fmt::Display for StoreError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Schema(e) => write!(f, "{e}"),
            Self::Io(e) => write!(f, "{e}"),
            Self::Encode(why) => write!(f, "could not serialise: {why}"),
        }
    }
}

impl std::error::Error for StoreError {}

impl From<LoadError> for StoreError {
    fn from(e: LoadError) -> Self {
        Self::Schema(e)
    }
}

impl From<std::io::Error> for StoreError {
    fn from(e: std::io::Error) -> Self {
        Self::Io(e)
    }
}

/// The schema versions that have shipped. A store missing from here has never
/// carried a version at all.
pub mod current {
    pub const SPACES: i64 = 6;
    pub const HOTKEYS: i64 = 2;
    pub const SETTINGS: i64 = 1;
    pub const THEMES: i64 = 1;
    pub const ROUTING: i64 = 2;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_current_version_loads() {
        assert!(can_load(3, 3));
    }

    #[test]
    fn an_older_version_still_loads() {
        assert!(can_load(1, 3));
        assert!(can_load(2, 3));
    }

    #[test]
    fn a_newer_version_is_refused() {
        assert!(!can_load(4, 3));
        assert!(!can_load(999, 3));
    }

    /// A version of 0 or below is a hand edit or a truncated write, not an
    /// older format, so it is refused rather than migrated.
    #[test]
    fn a_non_positive_version_is_refused() {
        assert!(!can_load(0, 3));
        assert!(!can_load(-1, 3));
    }
}
