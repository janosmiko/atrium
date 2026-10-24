// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! Writing a store to disk.
//!
//! Ported from `SecureFileWriter.swift` and `SpacesStore.performPersist`.
//!
//! Three properties the Swift path has, all of which matter:
//!
//! 0600 FROM CREATION. The temp file is created with owner-only permissions
//! rather than written and then chmod'd, so there is no window where a shared
//! local account can read it. `spaces.json` holds a 30-day browsing archive.
//!
//! ATOMIC. Temp file, then rename. A rename swaps the destination's inode
//! outright, so a pre-existing file with looser permissions is fixed BY this
//! write - no separate migration pass. A crash mid-write leaves either the old
//! file or the new one, never half of either.
//!
//! SORTED KEYS. A deterministic file diffs with plain `diff`, and it makes a
//! save a true fixed point: re-saving unchanged content must not perturb the
//! bytes.
//!
//! Key order is only half of that fixed point. The other half is that a number
//! has to survive being read back, which serde_json's default float parser does
//! not give for free - see `as_the_reader_reads_it`.
//!
//! `settings.json` writes through the same parts, with one step
//! in front of them: see `save_settings` for why a store that will not load has
//! to be refused HERE rather than by whoever calls it. It also spells its object
//! through `emit_object_with_raw`, because the keys it is preserving for a later
//! build are not this build's to round.
//!
//! `save_document` has the same step now - see
//! `refuse_unless_bytes_are_a_readable_spaces_file` for why `Core::open`'s
//! own refusal is not enough on its own.

use std::collections::BTreeMap;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

use crate::redact;
use crate::schema::{LoadError, StoreError};
use crate::settings::{self, FileAsFound, Settings};
use crate::small_stores::{self, HotkeysFile, RoutingFile, ThemesFile};
use crate::spaces::{self, Document};

/// Writes a spaces document to `path`, through every rule the Swift store had.
///
/// The order matters and each step earns its place:
///
/// 0. the re-read refusal (R1.2) - see
///    `refuse_unless_this_build_can_read_the_spaces_file`;
/// 1. the one-shot `.pre-rust` copy, so an older build has something to go back
///    to (it is taken before the FIRST Rust write and never again);
/// 2. the `.bak`, refreshed before every write and only from a file that reads
///    back as JSON (R7.3);
/// 3. redaction, on a COPY - the in-memory document keeps the real URLs
///    (R7.4);
/// 4. sorted JSON, then the atomic 0600 write.
///
/// Returns whether the `.bak` step succeeded. A backup that cannot be written
/// is not a reason to lose the main save (`docs/model-rules.md` R7.3c), so
/// this is reported rather than fatal - but both savers once
/// reported it to nobody, with `let _ = backup_if_valid(...)`. The caller
/// decides whether `BackupOutcome::Failed` is worth a log line, the same way
/// `atrium_core_save`'s own -1 is documented as one.
///
/// Blocking. Call it where blocking is allowed.
pub fn save_document(doc: &Document, path: &Path) -> Result<BackupOutcome, StoreError> {
    let existing = read_existing(path)?;
    if let Some(bytes) = &existing {
        refuse_unless_bytes_are_a_readable_spaces_file(bytes)?;
    }

    back_up_once_before_rust_writes(path, &unique())?;
    // The guard above already parsed these same bytes as a spaces file, so
    // there is nothing left for a second, looser JSON check to decide.
    let backup = BackupOutcome::of(back_up_bytes(existing.as_deref(), path, &unique()));

    let json = to_sorted_json(&redact::redact_document(doc)).map_err(io::Error::other)?;
    write_secure(json.as_bytes(), path, &unique())?;
    Ok(backup)
}

/// Same shape as `save_document`: R1.2 refusal, `.pre-rust`, `.bak`, sorted
/// JSON, atomic 0600 write. No redaction and no settings-style merge - the
/// caller's list replaces the file outright. Shared by `save_routing`,
/// `save_themes`, and `save_hotkeys`, which differ only in `check`.
fn save_small_store<T: serde::Serialize>(
    file: &T,
    path: &Path,
    check: fn(&str) -> Result<(), LoadError>,
) -> Result<BackupOutcome, StoreError> {
    refuse_unless_this_build_can_read_the_file(path, check)?;

    back_up_once_before_rust_writes(path, &unique())?;
    let backup = BackupOutcome::of(backup_if_valid(path, &unique(), |data| {
        std::str::from_utf8(data).is_ok_and(|text| check(text).is_ok())
    }));

    let json = to_sorted_json(file).map_err(io::Error::other)?;
    write_secure(json.as_bytes(), path, &unique())?;
    Ok(backup)
}

/// R1.2 for a small store - absent is the fresh-profile case, everything else
/// this build's own reader declines is a refusal.
fn refuse_unless_this_build_can_read_the_file(
    path: &Path,
    check: fn(&str) -> Result<(), LoadError>,
) -> Result<(), StoreError> {
    let bytes = match fs::read(path) {
        Ok(bytes) => bytes,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(()),
        Err(e) => return Err(StoreError::Io(e)),
    };
    let Ok(text) = String::from_utf8(bytes) else {
        return Err(StoreError::Schema(LoadError::Unreadable(
            "not UTF-8".into(),
        )));
    };
    check(&text).map_err(StoreError::Schema)
}

pub fn save_routing(file: &RoutingFile, path: &Path) -> Result<BackupOutcome, StoreError> {
    save_small_store(file, path, |text| {
        small_stores::load_routing_checked(text).map(|_| ())
    })
}

pub fn save_themes(file: &ThemesFile, path: &Path) -> Result<BackupOutcome, StoreError> {
    save_small_store(file, path, |text| {
        small_stores::load_themes_checked(text).map(|_| ())
    })
}

pub fn save_hotkeys(file: &HotkeysFile, path: &Path) -> Result<BackupOutcome, StoreError> {
    save_small_store(file, path, |text| {
        small_stores::load_hotkeys_checked(text).map(|_| ())
    })
}

/// `docs/model-rules.md` R1.2, re-enforced at save time rather than only at
/// open time.
///
/// `Core::open` already refuses a file this build cannot read, and a refused
/// open leaves nothing loaded, so nothing gets saved from it - but that only
/// answers "could this build read the file when the session started". The
/// crate has no clock (R7.2b): a session holds its document in memory for as
/// long as the caller keeps it open, and `save_document` used to write
/// straight over whatever was on disk with no second look. A file swapped in
/// after open - a newer build run in turn against the same profile, a sync
/// client landing a file mid-session - was overwritten with no check at all.
///
/// Mirrors `refuse_unless_this_build_can_read_it` below: absent is not a
/// refusal (a fresh profile has no spaces.json, and the first save is what
/// creates it), and everything else this build cannot load through the real
/// reader is. `spaces::load` rather than a narrower check, for the same
/// reason the spaces `.bak` check stayed a full parse -
/// anything looser risks being less strict than what `Core::open`
/// itself would decide, which is the exact asymmetry that was closed on the
/// settings side. The migrated `Document` is discarded. Only whether the load
/// succeeds is asked here, so the instant handed to it never surfaces.
fn refuse_unless_bytes_are_a_readable_spaces_file(bytes: &[u8]) -> Result<(), StoreError> {
    let Ok(text) = std::str::from_utf8(bytes) else {
        return Err(StoreError::Schema(LoadError::Unreadable(
            "not UTF-8".into(),
        )));
    };
    match spaces::load(text, 0.0) {
        Ok(_) => Ok(()),
        Err(e) => Err(e.into()),
    }
}

/// Reads `path` once, telling "nothing there yet" apart from every other
/// failure.
fn read_existing(path: &Path) -> Result<Option<Vec<u8>>, StoreError> {
    match read_file(path) {
        Ok(bytes) => Ok(Some(bytes)),
        Err(e) if e.kind() == io::ErrorKind::NotFound => Ok(None),
        Err(e) => Err(StoreError::Io(e)),
    }
}

/// Copies already-read `data` to `<path>.bak`, `None` skipping it. No
/// readability check here - the caller's own guard already did it.
fn back_up_bytes(data: Option<&[u8]>, path: &Path, unique: &str) -> io::Result<bool> {
    let Some(data) = data else {
        return Ok(false);
    };
    write_secure(data, &backup_path(path), unique)?;
    Ok(true)
}

/// Every real disk read of a store's current bytes goes through here, so a
/// test can count them.
fn read_file(path: &Path) -> io::Result<Vec<u8>> {
    let data = fs::read(path)?;
    #[cfg(test)]
    count_read();
    Ok(data)
}

// Per-thread so parallel tests never share a count - the harness gives each
// `#[test]` its own thread.
#[cfg(test)]
thread_local! {
    static IO_READS: std::cell::Cell<u32> = std::cell::Cell::new(0);
    static IO_WRITES: std::cell::Cell<u32> = std::cell::Cell::new(0);
}

#[cfg(test)]
fn count_read() {
    IO_READS.with(|c| c.set(c.get() + 1));
}

#[cfg(test)]
fn count_write() {
    IO_WRITES.with(|c| c.set(c.get() + 1));
}

/// Whether a save's `.bak` copy landed.
///
/// `backup_if_valid` also answers `Ok(false)` for a backup it correctly
/// declined to write - nothing there yet, or what was there was not worth
/// keeping as a recovery copy - and that is not a failure of anything; it is
/// working as designed. Only the `Err` side, the write into `.bak` itself
/// going wrong, is what this crate owes a caller a word about.
#[derive(Debug)]
pub enum BackupOutcome {
    /// `.bak` was written, or correctly skipped.
    Ok,
    /// The write into `.bak` failed. The save that carries it went ahead
    /// anyway - see `save_document` and `save_settings`.
    Failed(io::Error),
}

impl BackupOutcome {
    fn of(result: io::Result<bool>) -> Self {
        match result {
            Ok(_) => Self::Ok,
            Err(e) => Self::Failed(e),
        }
    }
}

/// Writes settings to `path`, through the same rules as the spaces document:
/// the one-shot `.pre-rust` copy, the `.bak`, sorted JSON, the atomic 0600
/// write.
///
/// Two differences from `save_document`, both deliberate.
///
/// There is no redaction step. Redaction strips credentials out of URLs and
/// settings carries none, so there would be nothing for it to do.
///
/// And the file already on disk is read back and offered to this build's own
/// loader BEFORE anything is written. A settings.json this build cannot read is
/// left exactly as it is - see `refuse_unless_this_build_can_read_it`. What
/// that read finds is then written back around the settings it was handed, so
/// a key this build has no field for survives the save
/// (`settings::FileAsFound`) - as the file's own bytes, which is
/// why the settings object is spelled by `emit_object_with_raw` rather than by
/// `to_sorted_json`.
///
/// Returns whether the `.bak` step succeeded, for the same reason
/// `save_document` does - see `BackupOutcome` and R7.3c.
///
/// Blocking. Call it where blocking is allowed.
pub fn save_settings(settings: &Settings, path: &Path) -> Result<BackupOutcome, StoreError> {
    let found = refuse_unless_this_build_can_read_it(path)?;

    back_up_once_before_rust_writes(path, &unique())?;
    let backup = BackupOutcome::of(backup_if_valid(path, &unique(), reads_back_as_settings));

    let json = found.merge(settings).to_json();
    write_secure(json.as_bytes(), path, &unique())?;
    Ok(backup)
}

/// `docs/model-rules.md` R1.2, enforced where it cannot be skipped.
///
/// It sits inside the writer rather than in the caller because a `Settings`
/// value cannot carry the answer: the defaults from a v99 file and the defaults
/// from a fresh profile are the same bytes, so a caller holding defaults does
/// not know which one it has. Reading the file here asks the only question that
/// settles it - can this build load what is about to be replaced?
///
/// ABSENT is not a refusal. A fresh profile has no settings.json and the first
/// save is what creates it, so an absent file hands back an empty
/// `FileAsFound` - nothing to preserve, every key written.
///
/// Everything else that does not load IS a refusal, including bytes that are
/// not JSON at all. From here a newer build's file and a damaged one look the
/// same, and a settings file that stops taking writes until it is moved aside
/// is a smaller loss than a user's settings silently replaced by defaults after
/// a downgrade.
///
/// It hands back what it read because the same read answers a second question,
/// and reading the file twice would let the two answers disagree: the version
/// gate is not the only thing a newer build can carry past this build's struct
/// (`settings::FileAsFound`).
pub(crate) fn refuse_unless_this_build_can_read_it(path: &Path) -> Result<FileAsFound, StoreError> {
    let bytes = match fs::read(path) {
        Ok(bytes) => bytes,
        Err(e) if e.kind() == io::ErrorKind::NotFound => return Ok(FileAsFound::default()),
        // Present but unreadable is not the same as absent, and guessing which
        // one it is would guess in the direction that destroys data.
        Err(e) => return Err(StoreError::Io(e)),
    };
    let Ok(text) = String::from_utf8(bytes) else {
        return Err(StoreError::Schema(LoadError::Unreadable(
            "not UTF-8".into(),
        )));
    };
    match settings::load_as_found(&text) {
        Ok((_, found)) => Ok(found),
        Err(e) => Err(e.into()),
    }
}

/// A distinct temp-file name per write.
///
/// The crate has no clock and no RNG, so the pair is the process and a counter:
/// unique within this process by the counter, and across processes by the pid.
fn unique() -> String {
    static NEXT: AtomicU64 = AtomicU64::new(0);
    format!("{}{}", our_names(), NEXT.fetch_add(1, Ordering::Relaxed))
}

/// What every name `unique()` hands out begins with.
///
/// Split out so `only_this_process_could_have_written` reads the pid back
/// through the same spelling that put it there. Two copies of this format
/// could drift, and the rule would then match nothing and say nothing.
fn our_names() -> String {
    format!("atrium-{}-", std::process::id())
}

/// Whether a temp file called `.{unique}.tmp` is one no OTHER live process
/// could be writing right now.
///
/// The pid in the name is what settles it, and it settles it in one direction
/// only. A name carrying our own pid was written by a process that held this
/// pid, and a pid belongs to one process at a time, so that writer is either us
/// or something already dead. Not us, because `unique()` hands each counter out
/// once and never again, so a name we are only now trying to create is not a
/// name we are still writing. That leaves dead, which is the whole claim.
///
/// It deliberately does NOT ask whether some other pid is running. Pids are
/// reused: "4242 is alive" does not say that the process which left
/// `.atrium-4242-0.tmp` is alive, and by the time anyone asks, 4242 is usually
/// an unrelated program. Answering that question wrong deletes a file a live
/// save is still filling, which turns a blocked save into a lost one - strictly
/// worse than the block. There is no syscall in this crate that could ask
/// anyway.
///
/// So everything else answers NO: a foreign pid, and any name not of this shape
/// at all, which is what a caller passing its own string gets. The write then
/// fails as it did before rather than removing a file this crate cannot account
/// for. Leaving a file alone costs one save; removing the wrong one costs the
/// save that was writing it.
///
/// The one place it can still be wrong is a profile directory shared between
/// two machines, where a pid is not unique. Every store in that directory is
/// already unsound for bigger reasons - two browsers writing one `spaces.json`
/// with no coordination at all - so this does not add a failure mode there.
fn only_this_process_could_have_written(unique: &str) -> bool {
    unique
        .strip_prefix(&our_names())
        .is_some_and(|counter| !counter.is_empty() && counter.bytes().all(|b| b.is_ascii_digit()))
}

/// Writes `data` to `path` through a temp file at 0600, then renames.
///
/// `unique` distinguishes the temp file. It is a parameter rather than a
/// random name because the crate has no clock and no RNG - the caller passes
/// something distinct per write, and two writers racing on one path is
/// already prevented by the serial queue above this.
pub fn write_secure(data: &[u8], path: &Path, unique: &str) -> io::Result<()> {
    write_secure_through(data, path, unique, write_owner_only)
}

/// `write_secure` with the temp-file write handed in, so a failure PART WAY
/// through one can be asked for. That case needs a filesystem that runs out of
/// room, and there is no other way to reach it from a test - which is why the
/// cleanup it needs was missing for as long as it was.
fn write_secure_through(
    data: &[u8],
    path: &Path,
    unique: &str,
    write: fn(&Path, &[u8]) -> io::Result<()>,
) -> io::Result<()> {
    let dir = path.parent().unwrap_or_else(|| Path::new("."));
    fs::create_dir_all(dir)?;

    let temp = dir.join(format!(".{unique}.tmp"));
    fill_temp(&temp, data, unique, write)?;

    match fs::rename(&temp, path) {
        Ok(()) => Ok(()),
        Err(e) => {
            let _ = fs::remove_file(&temp);
            Err(e)
        }
    }
}

/// Puts `data` in the temp file, over a leftover if there is one this process
/// can account for.
///
/// A leftover used to stop the save dead. `create_new` refuses an existing
/// file, so `.atrium-<pid>-<n>.tmp` from a process that died mid-write blocked
/// every later write that landed on that name - and since the counter restarts
/// at zero in a new process, a run of leftovers blocks a run of saves from its
/// first one onwards. That reaches the spaces document, which is written on
/// every sidebar gesture, not only settings.
///
/// Reclaiming it is safe only for a name
/// `only_this_process_could_have_written`, and that is the entire condition:
/// nothing here inspects the leftover. A killed writer leaves anything from
/// zero bytes to the whole finished file, so its contents say nothing about
/// whether it is finished, and nothing at all about whether it is in use.
fn fill_temp(
    temp: &Path,
    data: &[u8],
    unique: &str,
    write: fn(&Path, &[u8]) -> io::Result<()>,
) -> io::Result<()> {
    let blocked = match write_or_clean_up(temp, data, write) {
        Ok(()) => return Ok(()),
        Err(e) => e,
    };
    if blocked.kind() != io::ErrorKind::AlreadyExists
        || !only_this_process_could_have_written(unique)
    {
        return Err(blocked);
    }
    match fs::remove_file(temp) {
        Ok(()) => {}
        // Gone before we reached it is the outcome this wanted anyway.
        Err(e) if e.kind() == io::ErrorKind::NotFound => {}
        Err(e) => return Err(e),
    }
    // One retry. A second collision on a name only this process can produce
    // means something is recreating it faster than it can be cleared, and
    // looping on that would spin instead of saving.
    write_or_clean_up(temp, data, write)
}

/// Writes the temp file, and takes it back down if the write fails part way.
///
/// The rename below already did this; the write did not, and that asymmetry is
/// what a full disk turned into litter. `write_all` failing with no space left
/// returned through `?` and left the file it had just created, so a disk that
/// stayed full left one leftover per attempt - the trail that made "every later
/// save" true rather than "one".
///
/// `AlreadyExists` is the one failure that leaves the file alone, because then
/// the file is not ours: the call that reported it is the call that did not
/// create it.
fn write_or_clean_up(
    temp: &Path,
    data: &[u8],
    write: fn(&Path, &[u8]) -> io::Result<()>,
) -> io::Result<()> {
    match write(temp, data) {
        Ok(()) => Ok(()),
        Err(e) => {
            if e.kind() != io::ErrorKind::AlreadyExists {
                let _ = fs::remove_file(temp);
            }
            Err(e)
        }
    }
}

#[cfg(unix)]
fn write_owner_only(path: &Path, data: &[u8]) -> io::Result<()> {
    use std::io::Write;
    use std::os::unix::fs::OpenOptionsExt;

    let mut file = fs::OpenOptions::new()
        .write(true)
        .create_new(true)
        .mode(0o600)
        .open(path)?;
    file.write_all(data)?;
    file.sync_all()?;
    #[cfg(test)]
    count_write();
    Ok(())
}

#[cfg(not(unix))]
fn write_owner_only(path: &Path, data: &[u8]) -> io::Result<()> {
    // No POSIX mode here. The file still lands through the same
    // create-then-rename, so it is still atomic; the permission half of the
    // contract needs the platform's own equivalent when Windows arrives.
    fs::write(path, data)?;
    #[cfg(test)]
    count_write();
    Ok(())
}

/// Serialises with keys in sorted order, at every depth.
///
/// Swift encodes with `.sortedKeys`, and matching it is not cosmetic: a
/// deterministic file diffs with plain `diff`, and it makes a save a true
/// FIXED POINT - re-saving unchanged content does not perturb the bytes.
/// Without that, the transition period would rewrite every file on every save
/// purely because the two writers disagree about key order.
///
/// The sort is done here rather than left to the map type on purpose. The
/// Chromium tree builds serde_json with `preserve_order`, so its objects keep
/// insertion order; a plain `cargo build` does not, so its objects sort
/// themselves. Inserting in sorted order gives the same bytes under both, and
/// `keys_come_out_sorted_at_every_depth` fails loudly if that ever stops
/// being true.
pub fn to_sorted_json<T: serde::Serialize>(value: &T) -> Result<String, serde_json::Error> {
    let raw = serde_json::to_value(value)?;
    let mut out = String::new();
    emit(&raw, &mut out);
    Ok(out)
}

/// Writes the JSON by hand rather than through `serde_json::to_string`.
///
/// Three things have to match what Swift's `JSONEncoder` emits, and none of
/// them is reachable through serde_json's options:
///
/// SORTED KEYS at every depth. serde_json follows the map type, and the
/// Chromium tree builds it with `preserve_order` while a plain cargo build
/// does not - so leaving it to the map would give the two crate copies
/// different bytes.
///
/// A WHOLE NUMBER HAS NO `.0`. Swift writes `220`; serde_json writes `220.0`
/// for the same f64. Every length, every timestamp and every tint strength in
/// the document is a Double, so this is most of the file.
///
/// A SLASH IS ESCAPED. Swift writes `https:\/\/example.com\/`. That is legal
/// either way and means the same string, but it is a different byte sequence,
/// and every URL in the file carries several.
fn emit(value: &serde_json::Value, out: &mut String) {
    match value {
        serde_json::Value::Null => out.push_str("null"),
        serde_json::Value::Bool(b) => out.push_str(if *b { "true" } else { "false" }),
        serde_json::Value::Number(n) => emit_number(n, out),
        serde_json::Value::String(s) => emit_string(s, out),
        serde_json::Value::Array(items) => {
            out.push('[');
            for (i, item) in items.iter().enumerate() {
                if i > 0 {
                    out.push(',');
                }
                emit(item, out);
            }
            out.push(']');
        }
        serde_json::Value::Object(map) => {
            let mut keys: Vec<&String> = map.keys().collect();
            keys.sort();
            out.push('{');
            for (i, key) in keys.iter().enumerate() {
                if i > 0 {
                    out.push(',');
                }
                emit_string(key, out);
                out.push(':');
                emit(&map[*key], out);
            }
            out.push('}');
        }
    }
}

/// A settings object, in sorted key order, where the keys in `raw` are copied
/// through as the text the file carried instead of being spelled from a value.
///
/// The two halves are spelled by different rules on purpose. `typed` is this
/// build's own value and gets the writer's forms - a whole number without its
/// `.0`, a slash escaped, a float rounded to what the reader will give back.
/// `raw` is a later build's and gets none of them, because every one of those
/// forms is a decision about a value this build does not understand, and the
/// parse they start from is the data-loss channel `settings::spans_of` exists
/// to remove.
///
/// Sorting spans both halves, so which half a key came from cannot change the
/// bytes.
pub(crate) fn emit_object_with_raw(
    typed: &serde_json::Map<String, serde_json::Value>,
    raw: &BTreeMap<String, String>,
    out: &mut String,
) {
    let mut keys: Vec<&String> = typed.keys().chain(raw.keys()).collect();
    keys.sort();
    // `merge` hands over two disjoint halves. Writing a key twice would be a
    // file nothing can read, so it is made impossible here rather than assumed.
    keys.dedup();

    out.push('{');
    for (i, key) in keys.iter().enumerate() {
        if i > 0 {
            out.push(',');
        }
        emit_string(key, out);
        out.push(':');
        match raw.get(*key) {
            Some(text) => out.push_str(text),
            None => emit(&typed[*key], out),
        }
    }
    out.push('}');
}

/// This build's own spelling of `n`: the bytes `emit_object_with_raw` puts on
/// disk when it writes that number from the value.
///
/// `settings::merge` asks for it to decide whether writing a key from the value
/// would change the file, which is the question the value comparison could not
/// answer for a number.
pub(crate) fn spelling_of(n: &serde_json::Number) -> String {
    let mut out = String::new();
    emit_number(n, &mut out);
    out
}

fn emit_number(n: &serde_json::Number, out: &mut String) {
    if let Some(i) = n.as_i64() {
        out.push_str(&i.to_string());
        return;
    }
    if let Some(f) = n.as_f64() {
        // A Double that happens to be whole is written without a fraction,
        // which is what Swift does and what every existing file already has.
        // That branch is exact: the value goes through an i64 and comes back
        // through serde_json's integer path, neither of which loses anything.
        if f.fract() == 0.0 && f.abs() < 9.007_199_254_740_992e15 {
            out.push_str(&(f as i64).to_string());
            return;
        }
        out.push_str(&as_the_reader_reads_it(f).to_string());
        return;
    }
    out.push_str(&n.to_string());
}

/// How many times `as_the_reader_reads_it` will re-ask before giving up.
///
/// One is enough for every value these stores hold - opacities, tint strengths,
/// sidebar widths and Swift reference dates were all measured at one round or
/// none over sweeps of 200,000 values each. The margin is for the far ends of
/// the f64 range, where the same iteration does not converge at all.
const READ_BACK_ROUNDS: usize = 4;

/// The value this build's JSON reader gives back from `f`'s own shortest
/// decimal form, settled.
///
/// serde_json parses floats correctly only under its `float_roundtrip` feature,
/// and NEITHER build turns it on - the Chromium tree's vendored copy lists
/// alloc, default, indexmap, preserve_order and std, and this crate's
/// `Cargo.toml` takes the defaults. Without it the parser computes
/// `significand as f64` times a power of ten, two roundings, so a decimal
/// needing 17 significant digits comes back one ulp out. Over a 1001-step sweep
/// of the legal `glassOpacity` range, 107 values read back as a different f64,
/// and the file then changes on every save forever - the exact opposite of the
/// fixed point this module's own header promises.
///
/// The writer cannot spell its way out of it. The parser's image does not cover
/// f64: over a 100,001-step sweep of the same range, 2,095 values have no
/// decimal at ANY width from 1 to 25 that reads back as themselves. So the
/// writer emits the value the reader WILL produce rather than one it cannot -
/// at most one ulp away over that range, and stable from the first write.
///
/// Enabling `float_roundtrip` in both builds would turn this into a no-op
/// instead of a wrong answer, because the parser would then be correctly
/// rounded and `back == v` on the first round every time. It was weighed again
/// and refused again - `settings::this_build_would_change_it`
/// records the evidence.
///
/// It applies to a number this build OWNS and to nothing else - one the caller
/// just set, or one already spelled the way this writes it. A number the file
/// spells differently never reaches here, whether the key is declared or not:
/// it is copied through as the text the file carried (`settings::merge`),
/// because settling on the reader's value is still a change to a number this
/// build was not asked to change.
fn as_the_reader_reads_it(f: f64) -> f64 {
    let mut v = f;
    for _ in 0..READ_BACK_ROUNDS {
        // Not reached for a value out of a `serde_json::Number`, which cannot
        // hold an infinity or a NaN; writing `f` unchanged is what this did
        // before, so an unparseable form falls back to it rather than looping.
        let Ok(back) = serde_json::from_str::<f64>(&v.to_string()) else {
            return f;
        };
        if back == v {
            return v;
        }
        v = back;
    }
    f
}

fn emit_string(s: &str, out: &mut String) {
    out.push('"');
    for c in s.chars() {
        match c {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '/' => out.push_str("\\/"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            '\u{08}' => out.push_str("\\b"),
            '\u{0C}' => out.push_str("\\f"),
            // Anything else below space has no short form. Non-ASCII goes
            // through as itself - the store files carry emoji raw.
            c if (c as u32) < 0x20 => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push('"');
}

/// `<path>.bak`.
pub fn backup_path(path: &Path) -> PathBuf {
    let mut name = path.as_os_str().to_os_string();
    name.push(".bak");
    PathBuf::from(name)
}

/// Copies the current file to `<path>.bak`, but only when `readable` accepts
/// what is there now.
///
/// A corrupt file is not worth keeping as the recovery copy - and keeping it
/// would overwrite a GOOD `.bak` with rubbish, which is worse than having no
/// backup at all. Routed through the same atomic writer as the main file, so
/// the `.bak` is never briefly at the source's looser mode.
///
/// `readable` is the caller's own reader rather than one fixed check, because
/// the two stores do not agree on what a readable file is. Settings is the
/// looser of them (`settings::load_as_found`), and a `serde_json::Value` parse
/// standing in for it skipped the backup on exactly the three files most likely
/// to need one - a number outside f64's range, a nesting past the recursion
/// limit, a lone surrogate. Those load, so a save writes the main file; without
/// this they were the only files it wrote with no `.bak` beside them.
pub fn backup_if_valid(
    path: &Path,
    unique: &str,
    readable: impl Fn(&[u8]) -> bool,
) -> io::Result<bool> {
    let Ok(data) = fs::read(path) else {
        return Ok(false);
    };
    if !readable(&data) {
        return Ok(false);
    }
    write_secure(&data, &backup_path(path), unique)?;
    Ok(true)
}

/// The settings store's readability, as a `.bak` check: the same loader that
/// decides whether the save may proceed at all.
fn reads_back_as_settings(data: &[u8]) -> bool {
    std::str::from_utf8(data).is_ok_and(|text| settings::load_checked(text).is_ok())
}

/// The one-shot copy taken before Rust writes a store for the FIRST time.
///
/// The cut is not revertible for user data: once Rust has written, an older
/// build reading the file is on its own. This is the copy that makes going
/// back possible. It is `<path>.pre-rust`, distinct from `.bak`, because
/// `.bak` is overwritten on every save and would be gone within a second.
///
/// Guarded by the file's own existence: present means it has already been
/// taken, and it is never taken again. That is the guard, not a flag in
/// memory, because a flag does not survive a relaunch and the second launch
/// would overwrite the copy with data Rust had already written.
pub fn pre_rust_backup_path(path: &Path) -> PathBuf {
    let mut name = path.as_os_str().to_os_string();
    name.push(".pre-rust");
    PathBuf::from(name)
}

/// Takes the one-shot copy if it has not been taken. Returns whether it wrote
/// one just now.
fn back_up_once_before_rust_writes(path: &Path, unique: &str) -> io::Result<bool> {
    let destination = pre_rust_backup_path(path);
    if destination.exists() {
        return Ok(false);
    }
    let Ok(data) = fs::read(path) else {
        // Nothing there to preserve. A fresh profile has no pre-Rust state,
        // and writing an empty marker would make the guard lie about what it
        // holds.
        return Ok(false);
    };
    write_secure(&data, &destination, unique)?;
    Ok(true)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::test_temp_dir::TempDir;

    #[test]
    fn a_write_lands_and_reads_back() {
        let dir = TempDir::new("write-lands");
        let path = dir.join("spaces.json");
        write_secure(b"{\"a\":1}", &path, "u1").unwrap();
        assert_eq!(fs::read_to_string(&path).unwrap(), "{\"a\":1}");
    }

    #[cfg(unix)]
    #[test]
    fn a_written_file_is_owner_only() {
        use std::os::unix::fs::PermissionsExt;
        let dir = TempDir::new("mode");
        let path = dir.join("spaces.json");
        write_secure(b"{}", &path, "u1").unwrap();
        let mode = fs::metadata(&path).unwrap().permissions().mode() & 0o777;
        assert_eq!(
            mode, 0o600,
            "a shared local account must not be able to read it"
        );
    }

    /// A rename swaps the inode, so a file that was already there at a looser
    /// mode is FIXED by this write rather than needing a migration pass.
    #[cfg(unix)]
    #[test]
    fn a_loose_existing_file_is_tightened_by_the_next_write() {
        use std::os::unix::fs::PermissionsExt;
        let dir = TempDir::new("tighten");
        let path = dir.join("spaces.json");
        fs::write(&path, b"{}").unwrap();
        fs::set_permissions(&path, fs::Permissions::from_mode(0o644)).unwrap();

        write_secure(b"{\"b\":2}", &path, "u1").unwrap();

        let mode = fs::metadata(&path).unwrap().permissions().mode() & 0o777;
        assert_eq!(mode, 0o600);
    }

    #[test]
    fn no_temp_file_is_left_behind() {
        let dir = TempDir::new("no-temp");
        let path = dir.join("spaces.json");
        write_secure(b"{}", &path, "u1").unwrap();
        let leftovers: Vec<_> = fs::read_dir(dir.path())
            .unwrap()
            .filter_map(Result::ok)
            .filter(|e| e.file_name().to_string_lossy().ends_with(".tmp"))
            .collect();
        assert!(leftovers.is_empty(), "{leftovers:?}");
    }

    // --- leftover temp files ---

    /// What a full disk does to a write: the file is created, and then the
    /// write into it fails. Reached by nothing else - it needs a filesystem
    /// with no room left, which is why it went unnoticed.
    ///
    /// Measured on a 10 MB ram disk with 1.7 MB free: `write_secure` returned
    /// `StorageFull` and left `.atrium-<pid>-0.tmp` behind, and five saves in a
    /// row left five of them.
    fn creates_then_runs_out_of_room(path: &Path, _data: &[u8]) -> io::Result<()> {
        write_owner_only(path, b"")?;
        Err(io::Error::other("no space left on device"))
    }

    /// A leftover this process would have to be dead to have written cannot be
    /// in use, so the write takes the name back rather than failing.
    ///
    /// The name comes from `unique()` rather than being spelled out, so this
    /// claims the counter for itself and no other test can collide with it -
    /// and so the shape being matched is the real one.
    #[test]
    fn a_leftover_from_a_dead_process_does_not_block_the_next_write() {
        let dir = TempDir::new("leftover-reclaimed");
        let path = dir.join("spaces.json");
        let name = unique();

        // A killed writer leaves anything from nothing to the whole file. This
        // is the "whole file" end, which is the one that looks finished.
        fs::write(
            dir.join(&format!(".{name}.tmp")),
            b"{\"from\":\"the dead\"}",
        )
        .unwrap();

        write_secure(b"{\"new\":1}", &path, &name).unwrap();

        assert_eq!(fs::read_to_string(&path).unwrap(), "{\"new\":1}");
        assert!(no_leftovers(&dir), "and the leftover is gone with it");
    }

    /// The other half of the rule, and the one that matters more: a name this
    /// process could not have produced is NOT removed.
    ///
    /// A foreign pid is the case pid reuse makes unanswerable - the pid may be
    /// alive and be something else entirely, or be alive and be the writer.
    /// Both are indistinguishable from here, so the file stays and the write
    /// fails, which costs one save instead of the save that owns that file.
    #[test]
    fn a_leftover_this_process_cannot_account_for_is_left_alone() {
        for name in [
            // Some other process's, whether it is still running or not.
            format!("atrium-{}-0", u32::MAX),
            // Not this module's shape at all: a caller's own string.
            "u1".to_string(),
        ] {
            let dir = TempDir::new(&format!("leftover-foreign-{name}"));
            let path = dir.join("spaces.json");
            let temp = dir.join(&format!(".{name}.tmp"));
            fs::write(&temp, b"someone else is writing this").unwrap();

            let err = write_secure(b"{\"new\":1}", &path, &name).unwrap_err();

            assert_eq!(err.kind(), io::ErrorKind::AlreadyExists, "{name}");
            assert_eq!(
                fs::read_to_string(&temp).unwrap(),
                "someone else is writing this",
                "{name}: a file this crate cannot account for was removed"
            );
            assert!(!path.exists(), "{name}: and nothing was written over it");
        }
    }

    /// The reclaim takes back the name it was asked for and nothing else. A
    /// sweep of the directory would reach a live sibling's temp file; this
    /// cannot, because it only ever touches the one name it is about to use.
    #[test]
    fn the_reclaim_takes_one_name_and_not_the_directory() {
        let dir = TempDir::new("leftover-not-a-sweep");
        let path = dir.join("spaces.json");
        let name = unique();
        let bystander = dir.join(&format!(".atrium-{}-9999.tmp", u32::MAX));
        fs::write(dir.join(&format!(".{name}.tmp")), b"junk").unwrap();
        fs::write(&bystander, b"not yours").unwrap();

        write_secure(b"{\"new\":1}", &path, &name).unwrap();

        assert_eq!(fs::read_to_string(&bystander).unwrap(), "not yours");
    }

    /// A write that dies part way takes its own temp file down.
    ///
    /// The rename path already did this and the write path did not. Without it
    /// a full disk left one file per attempt, and every one of them was a name
    /// some later process would be blocked on.
    #[test]
    fn a_write_that_fails_part_way_takes_its_temp_file_with_it() {
        let dir = TempDir::new("full-disk");
        let path = dir.join("spaces.json");

        let err = write_secure_through(b"{}", &path, &unique(), creates_then_runs_out_of_room)
            .unwrap_err();

        assert_eq!(err.to_string(), "no space left on device");
        assert!(
            no_leftovers(&dir),
            "a name a later save would be blocked on"
        );
        assert!(!path.exists(), "and a failed write landed nothing");
    }

    /// The shape the measurement found: a disk that stays full is asked again
    /// and again, and used to leave a leftover each time. A run of them is what
    /// turns "one blocked save" into every save from the first one onwards,
    /// because a new process counts from zero.
    #[test]
    fn a_disk_that_stays_full_leaves_no_trail() {
        let dir = TempDir::new("full-disk-trail");
        let path = dir.join("spaces.json");

        for _ in 0..5 {
            assert!(
                write_secure_through(b"{}", &path, &unique(), creates_then_runs_out_of_room)
                    .is_err()
            );
        }

        assert!(no_leftovers(&dir));
    }

    /// The rule itself, without a file: which names this process will take back.
    #[test]
    fn only_names_this_process_makes_are_reclaimable() {
        let pid = std::process::id();

        assert!(only_this_process_could_have_written(&unique()));
        assert!(only_this_process_could_have_written(&format!(
            "atrium-{pid}-0"
        )));

        for foreign in [
            // Another process, alive or not - unanswerable either way.
            format!("atrium-{}-0", u32::MAX),
            // Our pid as a prefix of a longer one, which is not our pid.
            format!("atrium-{pid}9-0"),
            // The shape without a counter, so not something `unique()` made.
            format!("atrium-{pid}-"),
            format!("atrium-{pid}-tmp"),
            // A caller's own string.
            "u1".to_string(),
            String::new(),
        ] {
            assert!(
                !only_this_process_could_have_written(&foreign),
                "{foreign} is not a name this process can account for"
            );
        }
    }

    /// The guard against the two crate copies disagreeing: the Chromium tree
    /// builds serde_json with `preserve_order` and a plain cargo build does
    /// not, so anything relying on the map type would differ between them.
    #[test]
    fn keys_come_out_sorted_at_every_depth() {
        let value = serde_json::json!({
            "zebra": 1,
            "alpha": {"yak": 1, "bee": {"z": 1, "a": 2}},
            "mid": [{"q": 1, "b": 2}]
        });
        let out = to_sorted_json(&value).unwrap();
        assert_eq!(
            out,
            r#"{"alpha":{"bee":{"a":2,"z":1},"yak":1},"mid":[{"b":2,"q":1}],"zebra":1}"#
        );
    }

    /// Re-saving unchanged content must not perturb the file, or every save
    /// during the transition rewrites it for no reason.
    #[test]
    fn writing_the_same_document_twice_gives_the_same_bytes() {
        let value = serde_json::json!({"b": 1, "a": {"d": 2, "c": 3}});
        assert_eq!(
            to_sorted_json(&value).unwrap(),
            to_sorted_json(&value).unwrap()
        );
    }

    // --- a failed backup is reported, not discarded ---

    /// A directory sitting where `.bak` should be makes the rename inside
    /// `write_secure` fail deterministically - reachable without knowing
    /// which name `unique()` will hand out next, unlike the leftover-temp-file
    /// technique that needed a child process.
    fn block_backup_with_a_directory(path: &Path) {
        fs::create_dir(backup_path(path)).unwrap();
    }

    /// Before this task both savers called `backup_if_valid` and discarded
    /// the result with `let _`, so this same setup returned a bare `Ok` with
    /// no way to tell the `.bak` never landed. `BackupOutcome::Failed` is the
    /// fix: the save still goes ahead, but says so.
    #[test]
    fn a_document_save_reports_a_backup_that_could_not_be_written() {
        let dir = TempDir::new("backup-fails-document");
        let path = dir.join("spaces.json");
        let seed = r#"{"spaces":[],"appState":{"sidebarWidth":220,"schemaVersion":3}}"#;
        fs::write(&path, seed).unwrap();
        let doc = crate::spaces::load(seed, 0.0).unwrap();
        block_backup_with_a_directory(&path);

        let outcome = save_document(&doc, &path).unwrap();

        assert!(matches!(outcome, BackupOutcome::Failed(_)), "{outcome:?}");
        assert!(
            fs::read_to_string(&path)
                .unwrap()
                .contains("\"archiveEpoch\""),
            "the main save still ran despite the failed backup"
        );
        assert!(
            backup_path(&path).is_dir(),
            "and nothing overwrote the directory that blocked it"
        );
    }

    /// The settings half of the same defect and the same fix.
    #[test]
    fn a_settings_save_reports_a_backup_that_could_not_be_written() {
        let (_dir, path) = settings_dir("backup-fails-settings");
        fs::write(&path, SETTINGS_V1).unwrap();
        block_backup_with_a_directory(&path);

        let outcome = save_settings(&Settings::default(), &path).unwrap();

        assert!(matches!(outcome, BackupOutcome::Failed(_)), "{outcome:?}");
        assert_eq!(
            settings::load(&fs::read_to_string(&path).unwrap()),
            Settings::default(),
            "the main save still ran despite the failed backup"
        );
        assert!(
            backup_path(&path).is_dir(),
            "and nothing overwrote the directory that blocked it"
        );
    }

    /// The ordinary case, named so a regression that flips it is obvious: no
    /// blocker, so the outcome says `Ok`, not just "not `Failed`".
    #[test]
    fn a_backup_that_succeeds_is_reported_as_ok() {
        let (_dir, path) = settings_dir("backup-succeeds");
        fs::write(&path, SETTINGS_V1).unwrap();

        let outcome = save_settings(&Settings::default(), &path).unwrap();

        assert!(matches!(outcome, BackupOutcome::Ok), "{outcome:?}");
    }

    #[test]
    fn the_pre_rust_copy_is_taken_once_and_never_again() {
        let dir = TempDir::new("pre-rust");
        let path = dir.join("spaces.json");
        fs::write(&path, b"{\"original\":1}").unwrap();

        assert!(back_up_once_before_rust_writes(&path, "u1").unwrap());
        assert_eq!(
            fs::read_to_string(pre_rust_backup_path(&path)).unwrap(),
            "{\"original\":1}"
        );

        // Rust writes, and the guard must not let the copy be replaced by what
        // Rust wrote - that is exactly the state the copy exists to escape.
        write_secure(b"{\"written-by-rust\":1}", &path, "u2").unwrap();
        assert!(!back_up_once_before_rust_writes(&path, "u3").unwrap());
        assert_eq!(
            fs::read_to_string(pre_rust_backup_path(&path)).unwrap(),
            "{\"original\":1}",
            "the pre-Rust state is still what it was"
        );
    }

    /// The guard is the file, not a flag in memory. A flag does not survive a
    /// relaunch, and the second launch would overwrite the copy.
    #[test]
    fn the_guard_survives_a_restart() {
        let dir = TempDir::new("pre-rust-restart");
        let path = dir.join("spaces.json");
        fs::write(&path, b"{\"original\":1}").unwrap();
        back_up_once_before_rust_writes(&path, "u1").unwrap();
        write_secure(b"{\"rust\":1}", &path, "u2").unwrap();

        // Nothing is carried over between these calls but the filesystem.
        assert!(!back_up_once_before_rust_writes(&path, "u3").unwrap());
        assert!(!back_up_once_before_rust_writes(&path, "u4").unwrap());
        assert_eq!(
            fs::read_to_string(pre_rust_backup_path(&path)).unwrap(),
            "{\"original\":1}"
        );
    }

    #[test]
    fn a_fresh_profile_gets_no_empty_marker() {
        let dir = TempDir::new("pre-rust-fresh");
        let path = dir.join("spaces.json");
        assert!(!back_up_once_before_rust_writes(&path, "u1").unwrap());
        assert!(
            !pre_rust_backup_path(&path).exists(),
            "an empty marker would make the guard lie about what it holds"
        );
    }

    // --- save_document re-reads before it writes ---

    /// A real v3 file, so there is something for the reader to accept.
    const SPACES_V3: &str = r#"{"spaces":[],"appState":{"sidebarWidth":220,"schemaVersion":3}}"#;

    /// The same file from a build that does not exist yet.
    const SPACES_V99: &str = r#"{"spaces":[],"appState":{"sidebarWidth":220,"schemaVersion":99}}"#;

    fn spaces_dir(name: &str) -> (TempDir, PathBuf) {
        let dir = TempDir::new(name);
        let path = dir.join("spaces.json");
        (dir, path)
    }

    /// `docs/model-rules.md` R1.2, now enforced at save time too: a file from a
    /// build that came later survives being written over by this one, even
    /// when the in-memory document this call was handed is perfectly good -
    /// the caller cannot know the file changed underneath it. Both halves are
    /// asserted, matching `a_newer_settings_file_is_never_overwritten`: the
    /// file is byte-identical, and no `.bak` is taken from it either.
    #[test]
    fn a_newer_spaces_file_is_never_overwritten() {
        let (dir, path) = spaces_dir("spaces-refused");
        fs::write(&path, SPACES_V3).unwrap();
        // A good earlier save, so there is a .bak worth protecting.
        let doc = spaces::load(SPACES_V3, 0.0).unwrap();
        save_document(&doc, &path).unwrap();
        let good_backup = fs::read_to_string(backup_path(&path)).unwrap();

        // A newer build - or a sync client - lands its own file while this
        // process still holds the older document in memory.
        fs::write(&path, SPACES_V99).unwrap();
        let err = save_document(&doc, &path).unwrap_err();

        assert!(
            matches!(
                err,
                StoreError::Schema(LoadError::Refused { file_version: 99 })
            ),
            "{err:?}"
        );
        assert_eq!(
            fs::read_to_string(&path).unwrap(),
            SPACES_V99,
            "the newer file is byte-identical"
        );
        assert_eq!(
            fs::read_to_string(backup_path(&path)).unwrap(),
            good_backup,
            "R1.2: no .bak is written from a refused file either"
        );
        assert!(no_leftovers(&dir), "and nothing half-written");
    }

    /// A refused save on a fresh profile writes NOTHING at all, including the
    /// one-shot copy - the same guarantee `save_settings` already gives.
    #[test]
    fn a_refused_spaces_file_gets_no_backup_of_any_kind() {
        let (dir, path) = spaces_dir("spaces-refused-fresh");
        fs::write(&path, SPACES_V99).unwrap();
        let doc = spaces::load(SPACES_V3, 0.0).unwrap();

        assert!(save_document(&doc, &path).is_err());

        assert!(!backup_path(&path).exists(), "no .bak");
        assert!(!pre_rust_backup_path(&path).exists(), "no .pre-rust copy");
        assert!(no_leftovers(&dir));
    }

    /// A file this build cannot decode at all is refused the same way a
    /// refused schema version is: from here a newer build's shape and a
    /// damaged file look the same.
    #[test]
    fn a_spaces_file_that_does_not_parse_is_not_overwritten_either() {
        let (_dir, path) = spaces_dir("spaces-corrupt");
        fs::write(&path, b"not json at all").unwrap();
        let doc = spaces::load(SPACES_V3, 0.0).unwrap();

        let err = save_document(&doc, &path).unwrap_err();

        assert!(
            matches!(err, StoreError::Schema(LoadError::Unreadable(_))),
            "{err:?}"
        );
        assert_eq!(fs::read_to_string(&path).unwrap(), "not json at all");
        assert!(!backup_path(&path).exists());
        assert!(!pre_rust_backup_path(&path).exists());
    }

    /// ABSENT is not a refusal - a fresh profile has no spaces.json, and the
    /// first save is what creates it.
    #[test]
    fn a_missing_spaces_file_is_not_a_refusal() {
        let (_dir, path) = spaces_dir("spaces-fresh");
        let doc = spaces::load(SPACES_V3, 0.0).unwrap();

        save_document(&doc, &path).unwrap();

        assert!(path.exists());
    }

    fn reset_io_counts() {
        IO_READS.with(|c| c.set(0));
        IO_WRITES.with(|c| c.set(0));
    }

    fn io_reads() -> u32 {
        IO_READS.with(|c| c.get())
    }

    fn io_writes() -> u32 {
        IO_WRITES.with(|c| c.get())
    }

    /// The guard and the `.bak` check used to each read and parse the file
    /// on their own. One read now answers both.
    #[test]
    fn a_written_save_reads_and_parses_the_spaces_file_once() {
        let (dir, path) = spaces_dir("save-io-counts");
        fs::write(&path, SPACES_V3).unwrap();
        // Seeded first so this save's own one-shot copy adds no read of its own.
        back_up_once_before_rust_writes(&path, "seed").unwrap();
        let doc = spaces::load(SPACES_V3, 0.0).unwrap();

        reset_io_counts();
        save_document(&doc, &path).unwrap();

        assert_eq!(
            io_reads(),
            1,
            "one read must serve the guard and the .bak check"
        );
        assert_eq!(io_writes(), 2, ".bak and the real file, still two files");
        assert!(no_leftovers(&dir));
    }

    // --- settings.json ---

    /// A real v1 file, with two values that are not the defaults, so a save
    /// that quietly replaced it with defaults would show.
    const SETTINGS_V1: &str = r#"{"schemaVersion":1,"appIconVariant":"nord-accent",
        "animationsEnabled":false,"startupBehavior":"blankTab"}"#;

    /// The same file from a build that does not exist yet.
    const SETTINGS_V99: &str = r#"{"schemaVersion":99,"appIconVariant":"nord-accent",
        "animationsEnabled":false,"startupBehavior":"blankTab"}"#;

    fn settings_dir(name: &str) -> (TempDir, PathBuf) {
        let dir = TempDir::new(name);
        let path = dir.join("settings.json");
        (dir, path)
    }

    /// What a caller does with the file: read it back through the loader and
    /// expect its own value.
    #[test]
    fn a_settings_save_reads_back_as_what_was_saved() {
        let (_dir, path) = settings_dir("settings-round-trip");
        let want = Settings {
            confirm_to_quit_enabled: true,
            app_icon_variant: "nord-accent".into(),
            glass_opacity: 0.8,
            ..Settings::default()
        };

        save_settings(&want, &path).unwrap();

        let on_disk = fs::read_to_string(&path).unwrap();
        assert_eq!(settings::load(&on_disk), want);
    }

    /// Set the notification timeout, reload settings from disk, and read the
    /// same value back.
    #[test]
    fn notification_timeout_survives_a_save_and_reload() {
        let (_dir, path) = settings_dir("settings-notification-timeout");
        let want = Settings {
            notification_timeout: "tenSeconds".into(),
            ..Settings::default()
        };

        save_settings(&want, &path).unwrap();

        let on_disk = fs::read_to_string(&path).unwrap();
        assert_eq!(settings::load(&on_disk).notification_timeout, "tenSeconds");
    }

    fn one_pinned_space(url: &str) -> Document {
        let seed = format!(
            r#"{{"spaces":[{{"id":"A","name":"Work","emoji":"W","tint":"t",
            "profileID":"Work","pinnedTabs":[{{"url":"{url}","title":"Mail"}}],
            "tabs":[],"order":0,"groups":[]}}],
            "appState":{{"activeSpaceID":"A","sidebarWidth":220,"schemaVersion":3}}}}"#
        );
        spaces::load(&seed, 0.0).unwrap()
    }

    /// add a Doorway, save, reload from disk, and get
    /// the same record back with the pinned tab it came from untouched.
    #[test]
    fn a_doorway_survives_a_save_and_reload() {
        let dir = TempDir::new("spaces-doorway-round-trip");
        let path = dir.join("spaces.json");
        let mut doc = one_pinned_space("https://mail.example/");
        assert!(crate::mutate::add_doorway(
            &mut doc,
            "https://mail.example/",
            "A",
            "Work"
        ));

        save_document(&doc, &path).unwrap();

        let on_disk = fs::read_to_string(&path).unwrap();
        let reloaded = spaces::load(&on_disk, 0.0).unwrap();
        assert_eq!(reloaded.doorways.len(), 1);
        assert_eq!(reloaded.doorways[0].url, "https://mail.example/");
        assert_eq!(reloaded.doorways[0].space_id, "A");
        assert_eq!(reloaded.doorways[0].profile_id, "Work");
        assert_eq!(
            reloaded.spaces[0].pinned_tabs.len(),
            1,
            "the pinned tab is untouched"
        );
    }

    /// A Doorway carries a URL, same as a favourite or an archived tab, so it
    /// goes through the same funnel before disk.
    #[test]
    fn a_doorways_credential_bearing_url_is_redacted_on_save() {
        let dir = TempDir::new("spaces-doorway-redaction");
        let path = dir.join("spaces.json");
        let mut doc = one_pinned_space("https://mail.example/?token=SECRET");
        assert!(crate::mutate::add_doorway(
            &mut doc,
            "https://mail.example/?token=SECRET",
            "A",
            "Work"
        ));

        save_document(&doc, &path).unwrap();

        let on_disk = fs::read_to_string(&path).unwrap();
        assert!(
            !on_disk.contains("SECRET"),
            "a Doorway's url must be redacted like every other persisted url"
        );
    }

    /// And the bytes are a fixed point through a full load-save cycle. An equal
    /// value is not enough: a second writer that agreed about the value but not
    /// about key order would rewrite the whole file on every toggle.
    #[test]
    fn a_settings_save_is_byte_stable_through_load() {
        let (_dir, path) = settings_dir("settings-fixed-point");
        save_settings(&Settings::default(), &path).unwrap();
        let first = fs::read_to_string(&path).unwrap();

        save_settings(&settings::load(&first), &path).unwrap();

        assert_eq!(fs::read_to_string(&path).unwrap(), first);
    }

    #[test]
    fn saving_the_same_settings_twice_gives_the_same_bytes() {
        let (_dir, path) = settings_dir("settings-twice");
        let s = Settings {
            confirm_to_quit_enabled: true,
            ..Settings::default()
        };

        save_settings(&s, &path).unwrap();
        let first = fs::read(&path).unwrap();
        save_settings(&s, &path).unwrap();

        assert_eq!(fs::read(&path).unwrap(), first);
    }

    /// A settings.json from before the memory-saver split, carrying the one
    /// key that used to answer both questions.
    const SETTINGS_BEFORE_THE_MEMORY_SAVER_SPLIT: &str = r#"{"schemaVersion":1,
        "appIconVariant":"nord-accent","animationsEnabled":false,
        "startupBehavior":"blankTab","memorySaverThreshold":"never"}"#;

    /// A migration has to survive the WRITER, not only the reader.
    ///
    /// `FileAsFound` puts a key's own text back whenever the load moved the
    /// value, which is what preservation is for: a spelling this build did not
    /// recognise belongs to whoever wrote it. A migration moves the value too,
    /// so with nothing telling the two apart the save copies `"never"` back
    /// over the migrated value and the file never leaves the old shape - the
    /// migration undoes itself on the very next save, every time, for good.
    ///
    /// Two saves rather than one because one cannot see it: the first save
    /// writes the file, and it is the SECOND read of what that wrote that says
    /// whether the migration stuck.
    #[test]
    fn the_retired_memory_saver_spelling_does_not_come_back_on_the_next_save() {
        let (_dir, path) = settings_dir("settings-memory-saver-split");
        fs::write(&path, SETTINGS_BEFORE_THE_MEMORY_SAVER_SPLIT).unwrap();

        // The shape every settings save has: the surface is pushed what the
        // file loaded to and hands that same object back.
        let loaded = settings::load(&fs::read_to_string(&path).unwrap());
        save_settings(&loaded, &path).unwrap();
        let once = fs::read_to_string(&path).unwrap();

        save_settings(&settings::load(&once), &path).unwrap();
        let twice = fs::read_to_string(&path).unwrap();

        for (which, text) in [("the first save", &once), ("the second", &twice)] {
            let out: serde_json::Value = serde_json::from_str(text).unwrap();
            assert_ne!(
                out["memorySaverThreshold"],
                serde_json::json!("never"),
                "{which} put the retired spelling back: {text}"
            );
            assert_eq!(
                out["memorySaverEnabled"],
                serde_json::json!(false),
                "{which} lost what never migrated to: {text}"
            );
        }
        assert_eq!(twice, once, "and the save is still a fixed point");
    }

    /// `docs/model-rules.md` R1.2, the rule this writer exists to keep: a file
    /// from a build that came later survives a downgrade untouched. Both halves
    /// are asserted - the file is byte-identical, and no `.bak` was taken from
    /// it either.
    #[test]
    fn a_newer_settings_file_is_never_overwritten() {
        let (dir, path) = settings_dir("settings-refused");
        fs::write(&path, SETTINGS_V1).unwrap();
        // A good earlier save, so there is a .bak worth protecting.
        save_settings(&Settings::default(), &path).unwrap();
        let good_backup = fs::read_to_string(backup_path(&path)).unwrap();

        fs::write(&path, SETTINGS_V99).unwrap();
        let err = save_settings(&Settings::default(), &path).unwrap_err();

        assert!(
            matches!(
                err,
                StoreError::Schema(LoadError::Refused { file_version: 99 })
            ),
            "{err:?}"
        );
        assert_eq!(
            fs::read_to_string(&path).unwrap(),
            SETTINGS_V99,
            "the newer file is byte-identical"
        );
        assert_eq!(
            fs::read_to_string(backup_path(&path)).unwrap(),
            good_backup,
            "R1.2: no .bak is written from a refused file either"
        );
        assert!(no_leftovers(&dir), "and nothing half-written");
    }

    /// A refused save on a fresh profile writes NOTHING at all, including the
    /// one-shot copy. Taking the `.pre-rust` copy of a file this build cannot
    /// read would be the same mistake in a different filename.
    #[test]
    fn a_refused_settings_file_gets_no_backup_of_any_kind() {
        let (dir, path) = settings_dir("settings-refused-fresh");
        fs::write(&path, SETTINGS_V99).unwrap();

        assert!(save_settings(&Settings::default(), &path).is_err());

        assert!(!backup_path(&path).exists(), "no .bak");
        assert!(!pre_rust_backup_path(&path).exists(), "no .pre-rust copy");
        assert!(no_leftovers(&dir));
    }

    /// A file this build cannot decode is refused for the same reason: from
    /// here it is indistinguishable from a newer build's shape, and R1.3 says
    /// a decode failure lands in exactly the hole R1.2 exists to close.
    #[test]
    fn a_settings_file_that_does_not_parse_is_not_overwritten_either() {
        let (_dir, path) = settings_dir("settings-corrupt");
        fs::write(&path, b"not json at all").unwrap();

        let err = save_settings(&Settings::default(), &path).unwrap_err();

        assert!(
            matches!(err, StoreError::Schema(LoadError::Unreadable(_))),
            "{err:?}"
        );
        assert_eq!(fs::read_to_string(&path).unwrap(), "not json at all");
        assert!(!backup_path(&path).exists());
        assert!(!pre_rust_backup_path(&path).exists());
    }

    /// The one-shot copy holds the state from BEFORE Rust's first settings
    /// write, and keeps holding it however many saves follow.
    #[test]
    fn the_one_shot_copy_is_taken_before_the_first_settings_write() {
        let (_dir, path) = settings_dir("settings-pre-rust");
        fs::write(&path, SETTINGS_V1).unwrap();

        save_settings(&Settings::default(), &path).unwrap();
        assert_eq!(
            fs::read_to_string(pre_rust_backup_path(&path)).unwrap(),
            SETTINGS_V1,
            "the pre-Rust state, not what Rust just wrote"
        );

        save_settings(
            &Settings {
                confirm_to_quit_enabled: true,
                ..Settings::default()
            },
            &path,
        )
        .unwrap();
        assert_eq!(
            fs::read_to_string(pre_rust_backup_path(&path)).unwrap(),
            SETTINGS_V1,
            "and the second save did not replace it"
        );
    }

    /// A fresh profile has no settings.json, and that is not a refusal - the
    /// first save is what creates the file.
    #[test]
    fn a_fresh_profile_can_save_settings() {
        let (_dir, path) = settings_dir("settings-fresh");

        save_settings(&Settings::default(), &path).unwrap();

        assert!(path.exists());
        assert!(
            !pre_rust_backup_path(&path).exists(),
            "there was no pre-Rust state to copy"
        );
    }

    #[cfg(unix)]
    #[test]
    fn a_written_settings_file_is_owner_only() {
        use std::os::unix::fs::PermissionsExt;
        let (_dir, path) = settings_dir("settings-mode");
        fs::write(&path, SETTINGS_V1).unwrap();
        fs::set_permissions(&path, fs::Permissions::from_mode(0o644)).unwrap();

        save_settings(&Settings::default(), &path).unwrap();

        let mode = fs::metadata(&path).unwrap().permissions().mode() & 0o777;
        assert_eq!(mode, 0o600);
    }

    /// The failure case: a build that came later added keys
    /// WITHOUT moving `schemaVersion`, because R1.3 tells it to make them
    /// optional instead. R1.2's gate lets that file through, and everything the
    /// later build added has to still be there afterwards.
    #[test]
    fn a_later_builds_settings_survive_this_builds_save() {
        let (_dir, path) = settings_dir("settings-later-build");
        let from_later = r#"{"schemaVersion":1,"appIconVariant":"aurora-accent",
            "animationsEnabled":false,"startupBehavior":"blankTab",
            "surroundColor":"oklch(0.2 0.05 210)","readingModeEnabled":true,
            "sidebarBlur":{"radius":18,"tint":"cool"}}"#;
        fs::write(&path, from_later).unwrap();

        let loaded = settings::load(&fs::read_to_string(&path).unwrap());
        save_settings(
            &Settings {
                confirm_to_quit_enabled: true,
                ..loaded
            },
            &path,
        )
        .unwrap();

        let after: serde_json::Value =
            serde_json::from_str(&fs::read_to_string(&path).unwrap()).unwrap();
        assert_eq!(after["readingModeEnabled"], serde_json::json!(true));
        assert_eq!(
            after["sidebarBlur"],
            serde_json::json!({"radius": 18, "tint": "cool"})
        );
        assert_eq!(after["appIconVariant"], serde_json::json!("aurora-accent"));
        assert_eq!(
            after["surroundColor"],
            serde_json::json!("oklch(0.2 0.05 210)")
        );
        assert_eq!(
            after["confirmToQuitEnabled"],
            serde_json::json!(true),
            "and the edit that caused the save landed"
        );
    }

    /// The float half, swept rather than sampled.
    ///
    /// The suite used to check this with 0.55 and 0.8, which are two of the
    /// values that were never broken. Across the legal range about one in ten
    /// is, and `unreachable` below fails the test if the sweep ever stops
    /// covering them - which it would if the range narrowed or `glassOpacity`
    /// were rounded on the way in.
    ///
    /// It then asked for the value to move "at most one ulp", which CERTIFIED
    /// the drift rather than forbidding it, and only ever for a field this build
    /// declares. Both arms below are byte equality with no tolerance: the file
    /// this build writes for its own field is a fixed point, and the same
    /// decimal written by a LATER build comes back as the same characters.
    ///
    /// Every step is the save-load-save `save_settings` performs, with the disk
    /// left out so the sweep can be this dense; the values that were actually
    /// broken go through the real writer afterwards.
    #[test]
    fn every_opacity_in_the_legal_range_is_a_fixed_point() {
        let (min, max) = (settings::MIN_GLASS_OPACITY, settings::MAX_GLASS_OPACITY);
        const STEPS: u32 = 2000;

        let mut unreachable = Vec::new();
        for step in 0..=STEPS {
            let want = min + (max - min) * f64::from(step) / f64::from(STEPS);
            if serde_json::from_str::<f64>(&want.to_string()).ok() != Some(want) {
                unreachable.push(want);
            }

            let (once, twice) = save_load_save(want);
            assert_eq!(once, twice, "{want} does not survive its own file");

            for text in [want.to_string(), format!("{want:.17}")] {
                // The same decimal under a key this build has no field for and
                // under one it DECLARES. Only the first arm existed, and the
                // second is the whole of a declared key is parsed,
                // and the parse is where the precision goes.
                for key in ["laterOpacity", "glassOpacity"] {
                    let (once, twice) = save_load_save_number(key, &text);
                    assert!(
                        once.contains(&format!(r#""{key}":{text}"#)),
                        "a later build's {key} {text} was rewritten: {once}"
                    );
                    assert_eq!(once, twice, "{key} {text} does not survive its own file");
                }
            }
        }

        assert!(
            unreachable.len() > 100,
            "only {} of {} values exercise the defect - the sweep has stopped testing it",
            unreachable.len(),
            STEPS + 1
        );

        let (_dir, path) = settings_dir("settings-opacity-sweep");
        for want in unreachable.iter().take(20) {
            save_settings(
                &Settings {
                    glass_opacity: *want,
                    ..Settings::default()
                },
                &path,
            )
            .unwrap();
            let once = fs::read_to_string(&path).unwrap();

            save_settings(&settings::load(&once), &path).unwrap();

            assert_eq!(
                fs::read_to_string(&path).unwrap(),
                once,
                "{want} does not survive a real save"
            );
        }
    }

    /// One save-load-save at the bytes, exactly as `save_settings` composes it,
    /// without the disk. Returns the two files.
    fn save_load_save(glass_opacity: f64) -> (String, String) {
        let want = Settings {
            glass_opacity,
            ..Settings::default()
        };
        let once = FileAsFound::default().merge(&want).to_json();

        let (read_back, found) = settings::load_as_found(&once).unwrap();
        let twice = found.merge(&read_back).to_json();

        (once, twice)
    }

    /// The same round trip for a NUMERIC key, which is what nothing in the
    /// suite exercised: `a_later_builds_settings_survive_this_builds_save` uses
    /// bools, strings and the integer 18.
    ///
    /// `key` so the same decimal can be asked under a key this build has no
    /// field for and under one it declares. The two once answered differently and
    /// nothing could see it.
    fn save_load_save_number(key: &str, text: &str) -> (String, String) {
        let file = format!(
            r#"{{"schemaVersion":1,"appIconVariant":"nord-accent",
            "animationsEnabled":true,"startupBehavior":"blankTab",
            "{key}":{text}}}"#
        );

        let (settings, found) = settings::load_as_found(&file).unwrap();
        let once = found.merge(&settings).to_json();

        let (again, found) = settings::load_as_found(&once).unwrap();
        let twice = found.merge(&again).to_json();

        (once, twice)
    }

    /// A later build's numbers, through the real writer and the real disk.
    ///
    /// One of these per leak the parse used to open. Preservation kept the key
    /// as a `serde_json::Value`, and serde_json without `float_roundtrip` misread
    /// every one of them on the way in - after which `emit_number` faithfully
    /// wrote the misreading down for good.
    #[test]
    fn a_later_builds_numbers_survive_a_save_byte_for_byte() {
        for text in [
            // 17 significant digits: the width the parser's two roundings
            // cannot reproduce. About one fractional value in seven moved.
            "0.30000000000000004",
            "0.1234567890123456789",
            // Above i64::MAX, so it fell to the f64 path: 18446744073709552000.
            "18446744073709551615",
            // Under the smallest subnormal, so it was flattened to 0 and the
            // save reported success.
            "1e-400",
            // The worst one: re-emitted as a 309-digit literal serde_json then
            // refuses. See the test below for what that did to the file.
            "1.7976931348623157e308",
        ] {
            let (_dir, path) = settings_dir("settings-later-numbers");
            let file = format!(
                r#"{{"schemaVersion":1,"appIconVariant":"nord-accent",
                "animationsEnabled":false,"startupBehavior":"blankTab",
                "laterNumber":{text}}}"#
            );
            fs::write(&path, &file).unwrap();
            let loaded = settings::load(&fs::read_to_string(&path).unwrap());

            save_settings(
                &Settings {
                    confirm_to_quit_enabled: true,
                    ..loaded
                },
                &path,
            )
            .unwrap();

            let after = fs::read_to_string(&path).unwrap();
            assert!(
                after.contains(&format!(r#""laterNumber":{text}"#)),
                "{text} was not written as itself: {after}"
            );
            assert_eq!(
                serde_json::from_str::<serde_json::Value>(&after).unwrap()["confirmToQuitEnabled"],
                serde_json::json!(true),
                "and the edit that caused the save landed"
            );
        }
    }

    /// The decimal that was measured, through the real writer and the
    /// real disk, under the key this build DECLARES.
    ///
    /// It is inside the legal range, so nothing clamps it and the load looked
    /// like it had reproduced it. `merge` asked `loaded == was` - the lossy
    /// parse against itself - said yes, and wrote `0.0200196` over the top.
    const A_LATER_BUILDS_OPACITY: &str = "0.020019600000000002";

    /// The file this build did not write, one unrelated edit later.
    fn saved_after_an_edit(name: &str, settings: impl FnOnce(Settings) -> Settings) -> String {
        let (_dir, path) = settings_dir(name);
        fs::write(
            &path,
            format!(
                r#"{{"schemaVersion":1,"appIconVariant":"nord-accent",
                "animationsEnabled":false,"startupBehavior":"blankTab",
                "glassOpacity":{A_LATER_BUILDS_OPACITY}}}"#
            ),
        )
        .unwrap();
        let loaded = settings::load(&fs::read_to_string(&path).unwrap());

        save_settings(&settings(loaded), &path).unwrap();

        fs::read_to_string(&path).unwrap()
    }

    /// At the disk. Nothing asked for the opacity to change, so nothing
    /// about it may change - not the value, not the characters.
    #[test]
    fn a_declared_number_from_a_later_build_survives_an_unrelated_save() {
        let after = saved_after_an_edit("settings-declared-number", |loaded| Settings {
            confirm_to_quit_enabled: true,
            ..loaded
        });

        assert!(
            after.contains(&format!(r#""glassOpacity":{A_LATER_BUILDS_OPACITY}"#)),
            "the declared field was rewritten: {after}"
        );
        assert!(
            after.contains(r#""confirmToQuitEnabled":true"#),
            "and the edit that caused the save landed: {after}"
        );
    }

    /// At the disk, and the reason this is not simply "always keep the
    /// file's bytes". Preservation that outlives the user's own edit is a
    /// correctness defect, which is worse than the precision one it replaces.
    #[test]
    fn an_edit_to_a_declared_number_lands_over_a_later_builds_spelling() {
        let after = saved_after_an_edit("settings-declared-edit", |loaded| Settings {
            glass_opacity: 0.4,
            ..loaded
        });

        assert!(
            after.contains(r#""glassOpacity":0.4"#),
            "the edit was swallowed by the file's own spelling: {after}"
        );
        assert!(
            !after.contains(A_LATER_BUILDS_OPACITY),
            "the value being replaced is still on disk: {after}"
        );
        assert_eq!(
            settings::load(&after).glass_opacity,
            0.4,
            "and it reads back as what was asked for"
        );
    }

    /// The measurement, kept where it can be re-run rather than
    /// quoted from a note.
    ///
    /// `cargo test --release -- --ignored the_whole_legal_opacity_range`. It is
    /// ignored by default because 100,001 points is about fifteen seconds of a
    /// suite that otherwise runs in two, and the 2001-point sweep above already
    /// fails on hundreds of these if the fix goes away - density is the proof
    /// here, not the sensitivity.
    ///
    /// `would_be_respelled` is the guard that keeps it honest: it counts the
    /// points this build would spell differently from the file, which is what
    /// the save used to do to them. If that count collapses the sweep has
    /// stopped covering the defect and the zeros above it mean nothing.
    #[test]
    #[ignore = "100,001 points; the proof, run with --ignored"]
    fn the_whole_legal_opacity_range_survives_a_later_builds_spelling() {
        let (min, max) = (settings::MIN_GLASS_OPACITY, settings::MAX_GLASS_OPACITY);
        const STEPS: u32 = 100_000;

        let mut would_be_respelled = 0u32;
        for step in 0..=STEPS {
            let want = min + (max - min) * f64::from(step) / f64::from(STEPS);
            let text = want.to_string();

            for key in ["laterOpacity", "glassOpacity"] {
                let (once, twice) = save_load_save_number(key, &text);
                assert!(
                    once.contains(&format!(r#""{key}":{text}"#)),
                    "a later build's {key} {text} was rewritten: {once}"
                );
                assert_eq!(once, twice, "{key} {text} does not survive its own file");
            }

            let n: serde_json::Number = serde_json::from_str(&text).unwrap();
            if spelling_of(&n) != text {
                would_be_respelled += 1;
            }
        }

        assert!(
            would_be_respelled > 10_000,
            "only {would_be_respelled} of {} points would be respelled - the sweep has \
             stopped covering the defect",
            STEPS + 1
        );
    }

    /// The total-loss case, which is why this one is worth a test of its own.
    ///
    /// A preserved `f64::MAX` used to be re-emitted as a 309-digit literal, and
    /// serde_json REFUSES that as out of range. From that save onwards the file
    /// never loaded again: `save_settings` returned `Unreadable` forever, so
    /// nothing could correct it, and `load` handed back pure defaults. A partial
    /// loss became a total one.
    #[test]
    fn a_preserved_f64_max_leaves_the_file_loadable() {
        let (_dir, path) = settings_dir("settings-f64-max");
        fs::write(
            &path,
            r#"{"schemaVersion":1,"appIconVariant":"nord-accent",
            "animationsEnabled":false,"startupBehavior":"blankTab",
            "laterCeiling":1.7976931348623157e308}"#,
        )
        .unwrap();
        let loaded = settings::load(&fs::read_to_string(&path).unwrap());

        save_settings(&loaded, &path).unwrap();

        let after = fs::read_to_string(&path).unwrap();
        assert!(
            settings::load_checked(&after).is_ok(),
            "the file stopped loading: {after}"
        );
        assert_eq!(
            settings::load(&after),
            loaded,
            "and it still says what it said"
        );

        // The save that used to fail forever, and the bytes it leaves.
        save_settings(&loaded, &path).unwrap();
        assert_eq!(fs::read_to_string(&path).unwrap(), after);
    }

    /// What `as_the_reader_reads_it` is for, said without a file: whatever the
    /// writer puts down, this build's own reader gives back the same f64.
    #[test]
    fn a_written_float_reads_back_as_itself() {
        for f in [
            0.022_940_000_000_000_002_f64,
            0.047_439_999_999_999_996,
            0.55,
            0.8,
            788_918_400.5,
        ] {
            let written = to_sorted_json(&serde_json::json!({ "n": f })).unwrap();
            let back: serde_json::Value = serde_json::from_str(&written).unwrap();
            assert_eq!(
                to_sorted_json(&back).unwrap(),
                written,
                "{f} is not written as something this build can read back"
            );
        }
    }

    fn no_leftovers(dir: &TempDir) -> bool {
        fs::read_dir(dir.path())
            .unwrap()
            .filter_map(Result::ok)
            .all(|e| !e.file_name().to_string_lossy().ends_with(".tmp"))
    }
}
