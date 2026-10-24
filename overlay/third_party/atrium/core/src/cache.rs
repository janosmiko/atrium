// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//! The loaded spaces document, held in memory.
//!
//! `AtriumBrowserWindow::PushSidebarSnapshot` runs on the UI thread, and
//! `base::ReadFileToString` there is a fatal DCHECK - it killed the app three
//! seconds after every launch on 2026-08-09. So the document has to be in
//! memory before the push runs, and the only question was whose memory.
//! `docs/sidebar-model-source-design.md` answers it: this one.
//!
//! Two calls, and the split between them is the whole design:
//!
//! - [`Core::open`] reads the file. It blocks, so it belongs on a thread that
//!   is allowed to block - startup, alongside profile bootstrap.
//! - [`Core::sidebar_model`] builds from what is already loaded and never
//!   touches the filesystem, which is what lets the push stay synchronous and
//!   leaves all seven call sites unchanged.
//!
//! The write half is the same split again: [`Core::apply`] changes the document
//! in memory and is cheap enough for the UI thread, and [`Core::save`] is the
//! one call that blocks.
//!
//! The lock lives here rather than in C++. Snapshots arrive on the UI thread
//! and opens arrive on another, and a lock costs nothing next to building the
//! JSON.

use std::collections::BTreeSet;
use std::path::{Path, PathBuf};
use std::sync::Mutex;

use serde::Serialize;

use crate::small_stores::{ThemeSpec, ThemesFile};
use crate::{SwiftDate, mutate, schema, sidebar, small_stores, spaces, tabs, theme_math};

/// The pad's whole payload. The highlights ride on the note rather than on an
/// export of their own so the read side needed no ABI slot.
#[derive(Serialize)]
struct SpacePad<'a> {
    #[serde(flatten)]
    note: &'a spaces::SpaceNote,
    marginalia: Vec<&'a spaces::Marginal>,
}

/// What is loaded, and where it came from.
struct Loaded {
    path: PathBuf,
    doc: spaces::Document,
    /// Which live tab is showing which persisted row. Runtime
    /// state, and it lives here rather than beside the core so it is dropped
    /// with the document whose rows it names.
    bindings: tabs::Bindings,
    /// Bumped by every mutation that changed something.
    version: u64,
    /// The version the file on disk holds. Equal to `version` means there is
    /// nothing to write, so a caller can ask for a save after every gesture
    /// without rewriting the file for the ones that moved nothing.
    saved: u64,
    /// The space themes, keyed by space id. A separate file from `doc`
    /// (`themes.json` beside `spaces.json`), so it gets its own dirty flag.
    themes: ThemesFile,
    themes_path: PathBuf,
    themes_version: u64,
    themes_saved: u64,
}

/// Which stores a failed [`Core::save_by_store`] left unwritten.
#[derive(Debug, Default)]
pub struct SaveFailure {
    pub spaces: bool,
    pub themes: bool,
    pub message: String,
}

/// One profile's model, in memory.
///
/// Shut until [`Core::open`] succeeds. A shut core answers snapshots with an
/// error rather than an empty document: an empty sidebar is indistinguishable
/// from lost data, and `ChooseSidebarModelSource` already routes this case to
/// the flat tab list.
#[derive(Default)]
pub struct Core {
    loaded: Mutex<Option<Loaded>>,
    /// One writer at a time (R7.2), and what each writer last put on disk -
    /// separate from `loaded` so an fsync never holds up a UI-thread snapshot.
    writing: Mutex<WriteProgress>,
}

/// Last version [`Core::save`] wrote per file - a save no newer than this
/// skips its write. Reset by [`Core::open`], whose own counters restart at 0.
#[derive(Default)]
struct WriteProgress {
    spaces: u64,
    themes: u64,
}

impl Core {
    pub fn new() -> Self {
        Self::default()
    }

    /// Reads `path` and keeps the parsed document.
    ///
    /// Blocking. Call it where blocking is allowed.
    ///
    /// Opening the same path twice is a no-op - it does NOT re-read. Once
    /// mutations land in this document, a re-read would silently discard them.
    /// A different path is a different profile, so that does reload.
    ///
    /// Returns the reason on failure, and a failed open leaves any previously
    /// loaded document alone. A store this build cannot read must not take the
    /// working one down with it.
    pub fn open(
        &self,
        path: &Path,
        migration_instant: SwiftDate,
    ) -> Result<(), schema::StoreError> {
        let mut slot = self.locked();
        if slot.as_ref().is_some_and(|l| l.path == path) {
            return Ok(());
        }

        let json = std::fs::read_to_string(path)?;
        let doc = spaces::load(&json, migration_instant)?;

        // Best-effort: an absent or unreadable themes.json is an unthemed
        // profile, not a reason to refuse the open the spaces document needs.
        let themes_path = path.with_file_name("themes.json");
        let themes = std::fs::read_to_string(&themes_path)
            .map(|json| small_stores::load_themes(&json))
            .unwrap_or_default();

        *slot = Some(Loaded {
            path: path.to_path_buf(),
            doc,
            bindings: tabs::Bindings::new(),
            version: 0,
            saved: 0,
            themes,
            themes_path,
            themes_version: 0,
            themes_saved: 0,
        });
        // A version counter that starts back at 0 must not read as older
        // than what a save wrote for the document this one is replacing.
        *self.writing.lock().unwrap_or_else(|e| e.into_inner()) = WriteProgress::default();
        Ok(())
    }

    /// Forgets the loaded document. Used when a profile goes away.
    ///
    /// Does NOT write. An unsaved edit is dropped, so call [`Core::save`] first
    /// on any path that can still be carrying one - shutdown above all.
    pub fn close(&self) {
        *self.locked() = None;
    }

    /// True once a document is loaded.
    pub fn is_open(&self) -> bool {
        self.locked().is_some()
    }

    /// Builds this window's sidebar model from memory.
    ///
    /// No file IO, which is the point - see the module note. `live_tabs_json`
    /// is the window's live strip; `active_space_index` is its slot, or `None`
    /// to let the document's own `activeSpaceID` decide.
    ///
    /// Returns an error when nothing is loaded.
    pub fn sidebar_model(
        &self,
        active_space_index: Option<usize>,
        live_tabs_json: &str,
    ) -> Result<String, String> {
        let mut slot = self.locked();
        let loaded = slot
            .as_mut()
            .ok_or_else(|| "no spaces document is open".to_string())?;
        // Merged rather than replaced: this push only speaks for ITS OWN
        // window's tabs, so it must not clear the hidden rows another window
        // claimed.
        loaded.doc.hidden_rows =
            crate::hidden_rows_for(live_tabs_json, &loaded.bindings, &loaded.doc);

        Ok(crate::sidebar_model_from_document(
            &loaded.doc,
            active_space_index,
            live_tabs_json,
            Some(&loaded.bindings),
            &loaded.themes.themes,
        ))
    }

    /// The loaded slot, locked.
    ///
    /// A poisoned mutex is RECOVERED rather than propagated. A panic while a
    /// mutation held this lock leaves the document mid-edit, and refusing every
    /// later read would take the whole sidebar down over one bad gesture - so
    /// the guard is taken anyway and the next write overwrites whatever the
    /// panic left. Twenty-five sites spelled that decision out; now one does
    ///.
    fn locked(&self) -> std::sync::MutexGuard<'_, Option<Loaded>> {
        self.loaded.lock().unwrap_or_else(|e| e.into_inner())
    }

    /// Reads the loaded document, or `None` when nothing is open.
    ///
    /// `None` from `read` and `None` from "nothing is open" are the same answer
    /// to every caller here - a reader that cannot look reports nothing rather
    /// than an empty list, which is the rule the overview readers already
    /// followed by hand.
    fn read_loaded<F, T>(&self, read: F) -> Option<T>
    where
        F: FnOnce(&Loaded) -> Option<T>,
    {
        let slot = self.locked();
        read(slot.as_ref()?)
    }

    /// Runs `change` against the loaded document and records that it moved.
    ///
    /// No file IO, so this is cheap enough for the UI thread - the same split
    /// the read side already has. `change` returns whether it changed anything;
    /// false leaves the document exactly as it was and the file unwritten.
    ///
    /// Returns whether the document changed, which is also the answer to "does
    /// the sidebar need re-rendering".
    pub fn apply<F>(&self, change: F) -> bool
    where
        F: FnOnce(&mut spaces::Document) -> bool,
    {
        let mut slot = self.locked();
        let Some(loaded) = slot.as_mut() else {
            return false;
        };
        if !change(&mut loaded.doc) {
            return false;
        }
        loaded.version += 1;
        true
    }

    /// [`Core::apply`] for the mutations that also touch the live-tab
    /// bindings - tab open, close and replace.
    ///
    /// Separate because those three have to record a binding even when the
    /// document did NOT move: adopting a cold row writes no file and is
    /// exactly the case where the next close depends on the binding existing.
    /// `change` still returns whether the DOCUMENT changed, which is what
    /// decides whether a save is needed.
    pub fn apply_bound<F>(&self, change: F) -> bool
    where
        F: FnOnce(&mut spaces::Document, &mut tabs::Bindings) -> bool,
    {
        let mut slot = self.locked();
        let Some(loaded) = slot.as_mut() else {
            return false;
        };
        if !change(&mut loaded.doc, &mut loaded.bindings) {
            return false;
        }
        loaded.version += 1;
        true
    }

    /// [`Core::apply`] for a mutation that also PRODUCES something.
    ///
    /// `Some(v)` means the document moved and here is the value; `None` means
    /// nothing moved. The dirty flag is the result, so the two cannot part -
    /// which they once did nine times, each export writing its
    /// answer into an `Option` declared above the call and returning a bool
    /// that no longer had to agree with it.
    ///
    /// The value is a domain value, not JSON. Serialising inside the closure
    /// is what let a `serde_json` failure decide whether the document was
    /// dirty, and those are unrelated facts: the document moved either way.
    pub fn apply_value<F, T>(&self, change: F) -> Option<T>
    where
        F: FnOnce(&mut spaces::Document) -> Option<T>,
    {
        let mut slot = self.locked();
        let loaded = slot.as_mut()?;
        let out = change(&mut loaded.doc)?;
        loaded.version += 1;
        Some(out)
    }

    /// [`Core::apply_value`] for the mutations that also touch the bindings.
    pub fn apply_bound_value<F, T>(&self, change: F) -> Option<T>
    where
        F: FnOnce(&mut spaces::Document, &mut tabs::Bindings) -> Option<T>,
    {
        let mut slot = self.locked();
        let loaded = slot.as_mut()?;
        let out = change(&mut loaded.doc, &mut loaded.bindings)?;
        loaded.version += 1;
        Some(out)
    }

    /// Changes the BINDINGS and nothing else, so the version never moves.
    ///
    /// `forget_tab` and `note_tab_replaced` used `apply_bound` and returned
    /// false on purpose, to get the binding write without the version bump,
    /// while carrying their real answer out through a captured local. Nothing
    /// in that signature said which of the two things `false` meant, and
    /// getting it backwards either drops the gesture or rewrites the file on
    /// every tab replacement. Here it cannot be got backwards: there is no
    /// document to dirty.
    ///
    /// `None` when nothing is open, which is not the same as `Some(false)`.
    /// The document is readable too, for a binding that has to find its row
    /// first - a caller with no use for it just ignores the argument.
    pub fn rebind_against<F, T>(&self, f: F) -> Option<T>
    where
        F: FnOnce(&spaces::Document, &mut tabs::Bindings) -> T,
    {
        let mut slot = self.locked();
        let loaded = slot.as_mut()?;
        Some(f(&loaded.doc, &mut loaded.bindings))
    }

    /// Reads the document and the bindings together without mutating either.
    ///
    /// `None` when nothing is open.
    pub fn peek_bound<F, T>(&self, f: F) -> Option<T>
    where
        F: FnOnce(&spaces::Document, &tabs::Bindings) -> T,
    {
        self.read_loaded(|loaded| Some(f(&loaded.doc, &loaded.bindings)))
    }

    /// The launch answers `ProfileBootstrap` needs, as JSON.
    ///
    /// `None` when nothing is open, which is not the same as an empty plan:
    /// the caller falls back to a single Default profile rather than starting
    /// with no window at all.
    ///
    /// `startup` is settings.json's Startup row, which is not part of this
    /// document and so is passed in per call.
    pub fn launch_plan(&self, startup: crate::settings::StartupBehavior) -> Option<String> {
        self.read_loaded(|loaded| {
            Some(crate::launch::encode(&crate::launch::plan(
                &loaded.doc,
                startup,
            )))
        })
    }

    /// The stored sidebar width, or None when nothing is open.
    ///
    /// A narrow reader rather than a general one: the document is
    /// behind a lock and handing a caller a reference to it would let the lock
    /// escape, which is why `sidebar_model` and `launch_plan` each read what they
    /// need and return an owned value instead.
    pub fn sidebar_width(&self) -> Option<f64> {
        let slot = self.locked();
        Some(slot.as_ref()?.doc.app_state.sidebar_width)
    }

    /// Writes the document to the file it was opened from, if it has moved
    /// since the last write.
    ///
    /// BLOCKING - it fsyncs. Call it where blocking is allowed.
    ///
    /// `Ok(false)` means there was nothing to write: nothing is open, or no
    /// mutation has landed since the last save. A failed write leaves the
    /// document marked unsaved, so the next save tries again rather than
    /// silently dropping the user's edit.
    pub fn save(&self) -> Result<bool, String> {
        self.save_by_store().map_err(|failure| failure.message)
    }

    /// [`Core::save`], with the failed stores named. An import needs this:
    /// routing.json may follow saved spaces even when themes.json failed.
    pub fn save_by_store(&self) -> Result<bool, SaveFailure> {
        self.save_inner(|| {})
    }

    /// [`Core::save`]'s body, with a hook run between the snapshot and the
    /// writing lock so a test can force a slower save's write to happen
    /// after a faster one's without sleeping.
    fn save_inner(&self, between_snapshot_and_write: impl FnOnce()) -> Result<bool, SaveFailure> {
        // Copied out under the lock, written outside it: an fsync while the UI
        // thread wants a snapshot is exactly the stall this whole split exists
        // to avoid.
        let (doc, path, version, spaces_dirty, themes, themes_path, themes_version, themes_dirty) = {
            let slot = self.locked();
            let Some(loaded) = slot.as_ref() else {
                return Ok(false);
            };
            let spaces_dirty = loaded.version != loaded.saved;
            let themes_dirty = loaded.themes_version != loaded.themes_saved;
            if !spaces_dirty && !themes_dirty {
                return Ok(false);
            }
            (
                loaded.doc.clone(),
                loaded.path.clone(),
                loaded.version,
                spaces_dirty,
                loaded.themes.clone(),
                loaded.themes_path.clone(),
                loaded.themes_version,
                themes_dirty,
            )
        };

        between_snapshot_and_write();

        let mut spaces_written = false;
        let mut themes_written = false;
        let mut first_error: Option<String> = None;
        let mut failure = SaveFailure::default();
        {
            let mut progress = self.writing.lock().unwrap_or_else(|e| e.into_inner());
            // A save that raced here behind a newer one must not put its
            // stale snapshot back on disk over the newer bytes.
            let spaces_is_newer = version > progress.spaces;
            // `save_document` now reports whether its `.bak` failed
            // (`persist::BackupOutcome`), but `Core::save`'s own contract is
            // "did the document reach disk", and it did either way - a failed
            // backup does not fail this call. Not propagated further: this
            // `Result<bool, String>` predates the distinction and is asserted
            // on throughout the test suite as a plain bool.
            if spaces_dirty && spaces_is_newer {
                match crate::persist::save_document(&doc, &path) {
                    Ok(_) => {
                        spaces_written = true;
                        progress.spaces = version;
                    }
                    Err(e) => {
                        let message = format!("{}: {e}", path.display());
                        eprintln!("{message}");
                        first_error.get_or_insert(message);
                        failure.spaces = true;
                    }
                }
            }
            let themes_is_newer = themes_version > progress.themes;
            // Attempted even if the spaces write above failed - a stalled
            // theme edit must not also cost the user their tab layout, and a
            // failed spaces write must not cost them their theme either.
            if themes_dirty && themes_is_newer {
                match crate::persist::save_themes(&themes, &themes_path) {
                    Ok(_) => {
                        themes_written = true;
                        progress.themes = themes_version;
                    }
                    Err(e) => {
                        let message = format!("{}: {e}", themes_path.display());
                        eprintln!("{message}");
                        first_error.get_or_insert(message);
                        failure.themes = true;
                    }
                }
            }
        }

        let mut slot = self.locked();
        if let Some(loaded) = slot.as_mut() {
            // Only if it is still the same file. A profile switch between the
            // copy and here means this write belongs to a document nobody is
            // holding any more.
            if loaded.path == path {
                // `.max`, not a plain assignment: this call's own version is
                // stale the moment a newer save's bump has already landed
                // between the write above and this lock re-acquire.
                if spaces_written {
                    loaded.saved = loaded.saved.max(version);
                }
                if themes_written {
                    loaded.themes_saved = loaded.themes_saved.max(themes_version);
                }
            }
        }
        match first_error {
            Some(message) => Err(SaveFailure { message, ..failure }),
            None => Ok(true),
        }
    }

    /// Whether a mutation is waiting to be written.
    pub fn is_dirty(&self) -> bool {
        let slot = self.locked();
        slot.as_ref()
            .is_some_and(|l| l.version != l.saved || l.themes_version != l.themes_saved)
    }

    /// The loaded document's active space index, or `None` when shut.
    ///
    /// C++ needs this to seed a new window's slot when it has no Swift to ask.
    pub fn active_space_index(&self) -> Option<usize> {
        self.read_loaded(|loaded| {
            Some(sidebar::active_index_from_document(
                &loaded.doc.spaces,
                loaded.doc.app_state.active_space_id.as_deref(),
            ))
        })
    }

    /// Where the space with `id` renders, and which profile owns it.
    ///
    /// Both facts in one lock, because the routing seam needs them together:
    /// asking twice could straddle a mutation and pair an index with the
    /// profile of a different space.
    ///
    /// `None` when nothing is open or no space carries that id - a rule naming
    /// a space the user has since deleted, which routes nothing rather than
    /// routing somewhere arbitrary.
    pub fn space_slot_for_id(&self, id: &str) -> Option<(usize, String)> {
        self.read_loaded(|loaded| mutate::space_slot_for_id(&loaded.doc, id))
    }

    /// The id of the space at sidebar `position`. `None` when nothing is
    /// open or the position is past the rail's end.
    pub fn space_id_at(&self, position: usize) -> Option<String> {
        self.read_loaded(|loaded| mutate::space_id_at(&loaded.doc, position))
    }

    /// Whether `url` is pinned in the window's space, or favourited by
    /// `profile_id`. False when nothing is open.
    ///
    /// auto-peek's one question, which used to go to Swift.
    /// Resolved against the window's space because the source tab is the one
    /// the user is looking at - see `spaces::is_peek_source_url` for why pinned is
    /// per space and favourites are per profile.
    pub fn is_peek_source_url(
        &self,
        active_space_index: Option<usize>,
        profile_id: &str,
        url: &str,
    ) -> bool {
        self.read_loaded(|loaded| {
            let space = mutate::active_space(&loaded.doc, active_space_index)?;
            let space = &loaded.doc.spaces[space];
            Some(spaces::is_peek_source_url(&loaded.doc, space, profile_id, url))
        })
        .unwrap_or(false)
    }

    pub fn is_favorite_url(&self, profile_id: &str, url: &str) -> bool {
        self.read_loaded(|loaded| Some(spaces::is_favorite_url(&loaded.doc, profile_id, url)))
            .unwrap_or(false)
    }

    /// The active space's own tint as `#RRGGBB` - the base the page tint
    /// shifts away from.
    pub fn space_tint(&self, active_space_index: Option<usize>) -> Option<String> {
        self.read_loaded(|loaded| {
            let space = mutate::active_space(&loaded.doc, active_space_index)?;
            Some(crate::tint::resolve_hex(&loaded.doc.spaces[space].tint))
        })
    }

    /// A space's stored theme, or `None` when it has none, the id is
    /// unknown, or nothing is open.
    pub fn space_theme(&self, space_id: &str) -> Option<ThemeSpec> {
        self.read_loaded(|loaded| loaded.themes.themes.get(space_id).cloned())
    }

    /// Sanitizes and stores a space's theme (`None` removes it) and, when
    /// setting one, derives its tint from the theme's dominant colour. False
    /// for an unknown space.
    pub fn set_space_theme(&self, space_id: &str, theme: Option<ThemeSpec>) -> bool {
        let mut slot = self.locked();
        let Some(loaded) = slot.as_mut() else {
            return false;
        };
        let Some(backing) = loaded.doc.spaces.iter().position(|s| s.id == space_id) else {
            return false;
        };
        match theme {
            Some(theme) => {
                let theme = theme_math::sanitized(&theme);
                let tint = theme_math::dominant_tint_hex(&theme);
                loaded.themes.themes.insert(space_id.to_string(), theme);
                loaded.themes_version += 1;
                if mutate::set_space_tint(&mut loaded.doc, backing, &tint) {
                    loaded.version += 1;
                }
            }
            None => {
                loaded.themes.themes.remove(space_id);
                loaded.themes_version += 1;
            }
        }
        true
    }

    /// The active space's own profile id. A caller that only
    /// needs this one field should not have to build and parse the whole
    /// sidebar snapshot to get it.
    pub fn space_for_profile(
        &self,
        active_space_index: Option<usize>,
        profile_id: &str,
    ) -> Option<usize> {
        self.read_loaded(|loaded| {
            mutate::space_for_profile(&loaded.doc, active_space_index, profile_id)
        })
    }

    pub fn active_space_profile_id(&self, active_space_index: Option<usize>) -> Option<String> {
        self.read_loaded(|loaded| {
            let space = mutate::active_space(&loaded.doc, active_space_index)?;
            Some(loaded.doc.spaces[space].profile_id.clone())
        })
    }

    /// A profile's stored search engine choice, or `None` when nothing is
    /// open, the profile is unknown, or it has never picked one.
    pub fn profile_search_engine(&self, profile_id: &str) -> Option<String> {
        self.read_loaded(|loaded| {
            loaded
                .doc
                .profiles
                .iter()
                .find(|p| p.id == profile_id)
                .and_then(|p| p.search_engine.clone())
        })
    }

    /// A space's saved note as JSON, or an unsaved empty placeholder if it has
    /// never been written - the surface still needs a title before the first
    /// save.
    pub fn space_note(&self, active_space_index: Option<usize>) -> Option<String> {
        self.read_loaded(|loaded| {
            let space = mutate::active_space(&loaded.doc, active_space_index)?;
            let note = mutate::note_for_space(&loaded.doc, space)
                .cloned()
                .unwrap_or_else(|| spaces::SpaceNote {
                    id: String::new(),
                    space_id: Some(loaded.doc.spaces[space].id.clone()),
                    title: "Notes".to_string(),
                    body: String::new(),
                    updated_at: 0.0,
                });
            let marginalia = mutate::marginalia_for_space(&loaded.doc, space);
            serde_json::to_string(&SpacePad {
                note: &note,
                marginalia,
            })
            .ok()
        })
    }

    /// This space's highlights on `url`, as a JSON array. `[]` rather than
    /// `None` when the page has none, so the caller has one shape to parse.
    pub fn marginalia_for_url(
        &self,
        active_space_index: Option<usize>,
        url: &str,
    ) -> Option<String> {
        self.read_loaded(|loaded| {
            let space = mutate::active_space(&loaded.doc, active_space_index)?;
            serde_json::to_string(&mutate::marginalia_for_url(&loaded.doc, space, url)).ok()
        })
    }

    /// The Notes overview list, as JSON. `None` when nothing
    /// is open.
    pub fn notes_overview(&self) -> Option<String> {
        self.read_loaded(|loaded| serde_json::to_string(&mutate::notes_overview(&loaded.doc)).ok())
    }

    /// A single note by id, space pad or freestanding, as JSON. `None` when
    /// nothing is open or the id does not resolve.
    pub fn note_get(&self, id: &str) -> Option<String> {
        self.read_loaded(|loaded| {
            let note = mutate::note_by_id(&loaded.doc, id)?;
            serde_json::to_string(note).ok()
        })
    }

    /// The Shelf overview list, as JSON. `None` when nothing
    /// is open.
    pub fn shelf_overview(&self) -> Option<String> {
        self.read_loaded(|loaded| serde_json::to_string(&mutate::shelf_overview(&loaded.doc)).ok())
    }

    /// The Archived overview list, as JSON. `None` when
    /// nothing is open.
    pub fn archived_overview(&self) -> Option<String> {
        self.read_loaded(|loaded| {
            serde_json::to_string(&mutate::archived_overview(&loaded.doc)).ok()
        })
    }

    /// The Snoozed overview list, as JSON. `None` when
    /// nothing is open.
    pub fn snoozed_overview(&self) -> Option<String> {
        self.read_loaded(|loaded| {
            serde_json::to_string(&mutate::snoozed_overview(&loaded.doc)).ok()
        })
    }

    /// Runs the import applier under the one lock. `routing` is the caller's:
    /// routing.json is not part of the loaded state. With nothing open,
    /// `fresh` names where an empty document opens first. Opening and
    /// applying share the lock, so no other caller sees the empty document.
    pub fn apply_import(
        &self,
        plan: &crate::import::ImportPlan,
        routing: &mut small_stores::RoutingFile,
        engines: &mut Vec<crate::search_engines::SearchEngine>,
        fresh: Option<&Path>,
    ) -> Result<crate::import::ImportReport, String> {
        let mut slot = self.locked();
        let opened_fresh = slot.is_none();
        if opened_fresh {
            let path = fresh.ok_or("no spaces document is open")?;
            *slot = Some(Self::fresh_loaded(path)?);
            *self.writing.lock().unwrap_or_else(|e| e.into_inner()) = WriteProgress::default();
        }
        let Some(loaded) = slot.as_mut() else {
            return Err("no spaces document is open".into());
        };
        let themes_before = loaded.themes.themes.len();
        // A document opened for this call has no existing profiles, even
        // though fresh_loaded seeds a Default record: the plan created it.
        let report = if opened_fresh {
            crate::import::apply_import_with_existing(
                &mut loaded.doc,
                &mut loaded.themes,
                routing,
                engines,
                plan,
                &BTreeSet::new(),
            )
        } else {
            crate::import::apply_import(&mut loaded.doc, &mut loaded.themes, routing, engines, plan)
        };
        if report.document_changed() {
            loaded.version += 1;
        }
        if loaded.themes.themes.len() != themes_before {
            loaded.themes_version += 1;
        }
        Ok(report)
    }

    /// An empty document for an import to land in. Never replaces a store:
    /// an existing file refuses.
    fn fresh_loaded(path: &Path) -> Result<Loaded, String> {
        if path.exists() {
            return Err("spaces.json exists, so it must be opened, not replaced".into());
        }
        let doc = spaces::load(FRESH_DOCUMENT, 0.0).map_err(|e| format!("{e:?}"))?;
        let themes_path = path.with_file_name("themes.json");
        let themes = std::fs::read_to_string(&themes_path)
            .map(|json| small_stores::load_themes(&json))
            .unwrap_or_default();
        Ok(Loaded {
            path: path.to_path_buf(),
            doc,
            bindings: tabs::Bindings::new(),
            // Dirty from the start, so the first save creates the file.
            version: 1,
            saved: 0,
            themes,
            themes_path,
            themes_version: 0,
            themes_saved: 0,
        })
    }
}

/// Matches the Chromium profile directory a fresh install runs in.
const FRESH_DOCUMENT: &str = r#"{"spaces":[],"appState":{"sidebarWidth":220,"schemaVersion":6},"favorites":[],"profiles":[{"id":"Default","name":"Default"}]}"#;

/// The process-wide core.
///
/// One profile is loaded at a time, which is what the browser shows. A second
/// profile replaces it rather than sitting beside it, because the sidebar
/// renders one profile's spaces and asking for two would be a different shape.
pub fn shared() -> &'static Core {
    static SHARED: std::sync::OnceLock<Core> = std::sync::OnceLock::new();
    SHARED.get_or_init(Core::new)
}

#[cfg(test)]
mod tests {
    use std::path::PathBuf;

    use super::*;
    use crate::small_stores::ThemeEffects;
    use crate::test_temp_dir::TempDir;

    const SPACES_JSON: &str = r##"{"spaces":[{"id":"s1","name":"Work","emoji":"W",
        "tint":"#111111","profileID":"p","pinnedTabs":[],"tabs":[],"order":0,
        "groups":[]}],"appState":{"sidebarWidth":220,"schemaVersion":3,
        "archiveEpoch":1,"sidebarWidthMigrated":true}}"##;

    fn theme(name: &str) -> ThemeSpec {
        ThemeSpec {
            name: name.into(),
            stops: vec![crate::small_stores::ThemeStop {
                x: 0.5,
                y: 0.5,
                color: "#FF0000".into(),
                alpha: 1.0,
            }],
            opacity: 1.0,
            effects: ThemeEffects {
                grain: 0.0,
                vignette: 0.0,
                frost: 0.0,
                warmth: 0.0,
            },
            dark_adaptation: 0.0,
            night_shift_enabled: false,
            ambient_enabled: false,
            version: 1,
        }
    }

    #[test]
    fn fresh_document_matches_the_current_spaces_schema() {
        assert_eq!(
            spaces::load(FRESH_DOCUMENT, 0.0).unwrap().app_state.schema_version,
            schema::current::SPACES
        );
    }

    #[test]
    fn opening_a_directory_with_a_themes_file_loads_it() {
        let dir = TempDir::new("open-loads-themes");
        let spaces_path = dir.join("spaces.json");
        std::fs::write(&spaces_path, SPACES_JSON).unwrap();
        let file = ThemesFile {
            schema_version: schema::current::THEMES,
            themes: [("s1".to_string(), theme("Sunset"))].into(),
        };
        std::fs::write(
            dir.join("themes.json"),
            crate::persist::to_sorted_json(&file).unwrap(),
        )
        .unwrap();

        let core = Core::new();
        assert!(core.open(&spaces_path, 0.0).is_ok());
        assert_eq!(core.space_theme("s1"), Some(theme("Sunset")));
    }

    #[test]
    fn set_space_theme_then_save_writes_themes_json_sorted_and_reopens_equal() {
        let dir = TempDir::new("set-then-save");
        let spaces_path = dir.join("spaces.json");
        std::fs::write(&spaces_path, SPACES_JSON).unwrap();

        let core = Core::new();
        core.open(&spaces_path, 0.0).unwrap();
        assert!(core.set_space_theme("s1", Some(theme("Sunset"))));
        assert!(core.save().unwrap());

        let written = std::fs::read_to_string(dir.join("themes.json")).unwrap();
        let reread = small_stores::load_themes(&written);
        assert_eq!(reread.themes.get("s1"), Some(&theme("Sunset")));
        assert_eq!(
            written,
            crate::persist::to_sorted_json(&reread).unwrap(),
            "the file was not written in sorted form"
        );
    }

    #[test]
    fn removing_the_last_theme_leaves_an_empty_map_but_still_writes() {
        let dir = TempDir::new("remove-last");
        let spaces_path = dir.join("spaces.json");
        std::fs::write(&spaces_path, SPACES_JSON).unwrap();

        let core = Core::new();
        core.open(&spaces_path, 0.0).unwrap();
        assert!(core.set_space_theme("s1", Some(theme("Sunset"))));
        assert!(core.save().unwrap());

        assert!(core.set_space_theme("s1", None));
        assert!(core.save().unwrap(), "the removal must still be written");

        let written = std::fs::read_to_string(dir.join("themes.json")).unwrap();
        assert!(small_stores::load_themes(&written).themes.is_empty());
    }

    #[test]
    fn set_space_theme_changes_the_tint_to_the_dominant_hex() {
        let dir = TempDir::new("set-tint");
        let spaces_path = dir.join("spaces.json");
        std::fs::write(&spaces_path, SPACES_JSON).unwrap();

        let core = Core::new();
        core.open(&spaces_path, 0.0).unwrap();
        assert!(core.set_space_theme("s1", Some(theme("Sunset"))));

        let slot = core.locked();
        let tint = slot.as_ref().unwrap().doc.spaces[0].tint.clone();
        drop(slot);
        // The literal, not `dominant_tint_hex`'s own output, so a bug in it
        // cannot grade its own homework: full red's luminance (0.2126)
        // clears the 0.1791 ceiling after one 0.9x darkening, to #E60000.
        assert_eq!(tint, "#E60000");
    }

    #[test]
    fn set_space_theme_sanitizes_an_out_of_range_opacity() {
        let dir = TempDir::new("set-sanitizes");
        let spaces_path = dir.join("spaces.json");
        std::fs::write(&spaces_path, SPACES_JSON).unwrap();

        let mut wild = theme("Sunset");
        wild.opacity = 7.0;

        let core = Core::new();
        core.open(&spaces_path, 0.0).unwrap();
        assert!(core.set_space_theme("s1", Some(wild)));

        assert_eq!(core.space_theme("s1").unwrap().opacity, 1.0);
    }

    #[test]
    fn removing_a_theme_leaves_the_tint_at_its_last_derived_value() {
        let dir = TempDir::new("remove-keeps-tint");
        let spaces_path = dir.join("spaces.json");
        std::fs::write(&spaces_path, SPACES_JSON).unwrap();

        let core = Core::new();
        core.open(&spaces_path, 0.0).unwrap();
        assert!(core.set_space_theme("s1", Some(theme("Sunset"))));
        assert!(core.set_space_theme("s1", None));

        let slot = core.locked();
        let tint = slot.as_ref().unwrap().doc.spaces[0].tint.clone();
        drop(slot);
        assert_eq!(
            tint, "#E60000",
            "removal must not touch the tint it left behind"
        );
    }

    #[test]
    fn set_space_theme_is_false_for_an_unknown_space() {
        let dir = TempDir::new("unknown-id");
        let spaces_path = dir.join("spaces.json");
        std::fs::write(&spaces_path, SPACES_JSON).unwrap();

        let core = Core::new();
        core.open(&spaces_path, 0.0).unwrap();
        assert!(!core.set_space_theme("nope", Some(theme("Sunset"))));
        assert!(!core.set_space_theme("nope", None));
    }

    #[test]
    fn removing_a_theme_alone_makes_the_core_dirty_and_save_clears_it() {
        let dir = TempDir::new("theme-dirty");
        let spaces_path = dir.join("spaces.json");
        std::fs::write(&spaces_path, SPACES_JSON).unwrap();

        let core = Core::new();
        core.open(&spaces_path, 0.0).unwrap();
        assert!(core.set_space_theme("s1", Some(theme("Sunset"))));
        assert!(core.save().unwrap());
        assert!(!core.is_dirty(), "a fresh save must leave nothing dirty");

        assert!(core.set_space_theme("s1", None));
        assert!(core.is_dirty(), "a themes-only change must count as dirty");

        assert!(core.save().unwrap());
        assert!(!core.is_dirty());
    }

    #[test]
    fn a_spaces_write_failure_does_not_skip_the_themes_write() {
        let dir = TempDir::new("spaces-fails-themes-writes");
        let spaces_path = dir.join("spaces.json");
        std::fs::write(&spaces_path, SPACES_JSON).unwrap();

        let core = Core::new();
        core.open(&spaces_path, 0.0).unwrap();
        assert!(core.set_space_theme("s1", Some(theme("Sunset"))));

        // Broken AFTER open, so the in-memory document is unaffected - only
        // the next save, which re-reads and rewrites this same path, fails.
        std::fs::remove_file(&spaces_path).unwrap();
        std::fs::create_dir(&spaces_path).unwrap();

        assert!(core.save().is_err(), "the spaces write must fail");

        let written = std::fs::read_to_string(dir.join("themes.json")).unwrap();
        assert_eq!(
            small_stores::load_themes(&written).themes.get("s1"),
            Some(&theme("Sunset")),
            "the themes write must still land"
        );
    }

    #[test]
    fn a_failed_save_names_the_spaces_store_alone() {
        let dir = TempDir::new("save-names-spaces");
        let spaces_path = dir.join("spaces.json");
        std::fs::write(&spaces_path, SPACES_JSON).unwrap();
        let core = Core::new();
        core.open(&spaces_path, 0.0).unwrap();
        assert!(core.set_space_theme("s1", Some(theme("Sunset"))));
        std::fs::remove_file(&spaces_path).unwrap();
        std::fs::create_dir(&spaces_path).unwrap();

        let failure = core.save_by_store().unwrap_err();
        assert!(failure.spaces);
        assert!(!failure.themes, "themes.json was written");
    }

    #[test]
    fn a_failed_save_names_the_themes_store_alone() {
        let dir = TempDir::new("save-names-themes");
        let spaces_path = dir.join("spaces.json");
        std::fs::write(&spaces_path, SPACES_JSON).unwrap();
        let core = Core::new();
        core.open(&spaces_path, 0.0).unwrap();
        assert!(core.set_space_theme("s1", Some(theme("Sunset"))));
        std::fs::create_dir(dir.join("themes.json")).unwrap();

        let failure = core.save_by_store().unwrap_err();
        assert!(failure.themes);
        assert!(!failure.spaces, "spaces.json was written");
    }

    #[test]
    fn a_slower_save_never_overwrites_a_newer_version_already_on_disk() {
        let dir = TempDir::new("slower-save-race");
        let spaces_path = dir.join("spaces.json");
        std::fs::write(&spaces_path, SPACES_JSON).unwrap();

        let core = std::sync::Arc::new(Core::new());
        core.open(&spaces_path, 0.0).unwrap();
        assert!(core.apply(|doc| {
            doc.app_state.sidebar_width = 111.0;
            true
        }));

        // The older save snapshots version 1 here and then blocks, so the
        // newer one below is free to reach disk first.
        let (snapshotted, wait_for_newer) =
            (std::sync::mpsc::channel(), std::sync::mpsc::channel());
        let (snapshotted_tx, snapshotted_rx) = snapshotted;
        let (resume_tx, resume_rx) = wait_for_newer;
        let older = std::thread::spawn({
            let core = core.clone();
            move || {
                core.save_inner(|| {
                    snapshotted_tx.send(()).unwrap();
                    resume_rx.recv().unwrap();
                })
            }
        });
        snapshotted_rx.recv().unwrap();

        assert!(core.apply(|doc| {
            doc.app_state.sidebar_width = 222.0;
            true
        }));
        assert!(core.save().unwrap());

        resume_tx.send(()).unwrap();
        assert!(older.join().unwrap().is_ok());

        let written = std::fs::read_to_string(&spaces_path).unwrap();
        assert!(
            written.contains("222"),
            "the newer save's bytes must survive the older save that finished after it"
        );
        assert!(
            !core.is_dirty(),
            "the newer save's own saved bump must not be undone by the stale one"
        );
    }

    #[test]
    fn a_push_from_another_window_cannot_change_the_translation_for_this_one() {
        let dir = TempDir::new("hidden-rows-per-window");
        let spaces_path = dir.join("spaces.json");
        std::fs::write(&spaces_path, SPACES_JSON).unwrap();

        let core = Core::new();
        core.open(&spaces_path, 0.0).unwrap();

        // Window 1 owns tabs 1 and 2: tab 1 is claimed by a favourite, so its
        // row hides and window 1's rendered position 0 resolves to tab 2's row.
        // B opens first so that A, the claimed one, lands above it - a new row
        // goes to the top, and the translation is only interesting when the
        // hidden row sits before the rendered one.
        core.apply_bound_value(|doc, bindings| {
            let b = match mutate::open_tab(doc, 0, "https://b.example", "B", None, bindings)? {
                mutate::Opened::Added(t) | mutate::Opened::Adopted(t) => t,
            };
            bindings.bind(2, b);
            let a = match mutate::open_tab(doc, 0, "https://a.example", "A", None, bindings)? {
                mutate::Opened::Added(t) | mutate::Opened::Adopted(t) => t,
            };
            bindings.bind(1, a);
            Some(())
        })
        .unwrap();

        core.sidebar_model(
            Some(0),
            r#"[{"url":"https://a.example","id":1,"active":true,"claimed":"fav-a"}]"#,
        )
        .unwrap();

        let visible_before = core
            .peek_bound(|doc, _| {
                mutate::persisted_index_of_visible(
                    0,
                    &doc.spaces[0],
                    &doc.hidden_rows,
                )
            })
            .flatten();
        assert_eq!(
            visible_before,
            Some(1),
            "window 1's row 0 must resolve to tab B, past the hidden claimed row"
        );

        // Window 2 owns an unrelated tab and knows nothing about window 1's
        // claim - its push must not erase window 1's hidden row.
        core.apply_bound_value(|doc, bindings| {
            let c = match mutate::open_tab(doc, 0, "https://c.example", "C", None, bindings)? {
                mutate::Opened::Added(t) | mutate::Opened::Adopted(t) => t,
            };
            bindings.bind(3, c);
            Some(())
        })
        .unwrap();
        core.sidebar_model(
            Some(0),
            r#"[{"url":"https://c.example","id":3,"active":true}]"#,
        )
        .unwrap();

        // C went in at the top, so window 1's second rendered row is B at index
        // 2. Were the hidden row dropped, A would render between them and the
        // same query would answer 1.
        let visible_after = core
            .peek_bound(|doc, _| {
                mutate::persisted_index_of_visible(
                    1,
                    &doc.spaces[0],
                    &doc.hidden_rows,
                )
            })
            .flatten();
        assert_eq!(
            visible_after,
            Some(2),
            "window 2's push must not erase window 1's hidden row"
        );
    }

    #[test]
    fn a_closed_tabs_token_is_pruned_from_hidden_rows_on_the_next_push() {
        let dir = TempDir::new("hidden-rows-prune-closed");
        let spaces_path = dir.join("spaces.json");
        std::fs::write(&spaces_path, SPACES_JSON).unwrap();

        let core = Core::new();
        core.open(&spaces_path, 0.0).unwrap();

        let token_a = core
            .apply_bound_value(|doc, bindings| {
                let a = match mutate::open_tab(doc, 0, "https://a.example", "A", None, bindings)? {
                    mutate::Opened::Added(t) | mutate::Opened::Adopted(t) => t,
                };
                bindings.bind(1, a);
                Some(a)
            })
            .unwrap();
        core.sidebar_model(
            Some(0),
            r#"[{"url":"https://a.example","id":1,"active":true,"claimed":"fav-a"}]"#,
        )
        .unwrap();
        assert!(
            core.peek_bound(|doc, _| doc.hidden_rows.clone())
                .unwrap()
                .contains(&token_a)
        );

        // The tab closes - its row leaves `doc` entirely, so no window will
        // ever again list it as local to clear the stale token itself.
        core.apply_bound(|doc, bindings| {
            mutate::close_tab(doc, token_a, 0.0).is_some() && bindings.release(1).is_some()
        });

        core.sidebar_model(
            Some(0),
            r#"[{"url":"https://c.example","id":3,"active":true}]"#,
        )
        .unwrap();

        let hidden = core.peek_bound(|doc, _| doc.hidden_rows.clone()).unwrap();
        assert!(
            !hidden.contains(&token_a),
            "a closed tab's token must not linger in hidden_rows forever"
        );
    }
}
