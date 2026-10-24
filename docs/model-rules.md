# Model rules

The rules the Swift model layer holds, read out of the tests that assert them.

Those tests are deleted at the cut. This is what the Rust core is built from,
so every rule here cites the file it came from. If a rule looks wrong, open the
citation - the test is the authority, not this page.

Three companion records carry the parts that are data rather than prose:

| Record | Holds |
|---|---|
| `fixtures/stores/` | Real store files and the state each loads to |
| `fixtures/sidebar-model/` | The sidebar model JSON for five profiles |
| `build/generated/README.md` | Where every design token came from |

**Not every rule below lives in Swift.** Some are already in C++ or in the
sidebar's JavaScript and survive the cut untouched. The Owner column says
which, because porting a rule that did not move is wasted work, and assuming a
rule moved when it did not is a regression.

---

## 1. Schema acceptance

`SchemaAcceptance.swift`, `SchemaAcceptanceTests.swift`,
`SchemaAcceptanceStoreTests.swift`.

**R1.1 A file loads when its version is at least 1 and at most the build's.**
Older is accepted and migrated forward. Newer is refused. Below 1 is refused -
a version of 0 or -1 is a hand edit or a truncated write, not an older format.
`SchemaAcceptanceTests.testCurrentVersionLoads`, `.testOlderVersionStillLoads`,
`.testNewerVersionIsRejected`, `.testNonPositiveVersionIsRejected`.

**R1.2 A refused file is never overwritten.** The store falls back to defaults
in memory AND turns persistence off, so the newer file survives a
downgrade-then-upgrade untouched. No `.bak` is written either.
`SpacesStoreTests.testNewerSchemaVersionIsNotClobbered` asserts the file is
byte-identical after a `saveNow()`. `SchemaAcceptanceStoreTests` asserts the
same for the settings, theme and routing stores.

This is the rule most likely to be lost in a port. Falling back to defaults is
the easy half; refusing to write is the half that saves the user's data.

**R1.2b `save_document` re-reads before it writes, the same guard
`save_settings` already had.** `Core::open`'s own refusal only
answers whether this build could read `spaces.json` when the session started.
The crate has no clock (R7.2b) - a session holds its document in memory for as
long as the caller keeps it open, and `save_document` used to write straight
over whatever was on disk with no second look. A file swapped in after open -
a newer build run in turn against the same profile, a sync client landing a
file mid-session - was overwritten with no check at all. It now re-reads
`path` and refuses exactly as `Core::open` would, through `spaces::load`
itself rather than a narrower check, so nothing about the guard can be looser
than the reader it stands in for.

**R1.3 Accepting an older file obliges the decoder to tolerate missing
fields.** Every field added after v1 is optional or has a default. A required
field added in v2 turns a v1 file into a decode failure, which lands in the
same silently-replaced-by-defaults hole R1.2 exists to close.
`SchemaAcceptanceStoreTests.testSettingsStoreLoadsAFileCarryingOnlyTheOriginalFields`,
and `fixtures/stores/settings/v1-original-keys.json`, where eleven later fields
decode to their defaults.

**R1.3a Settings adds fields without bumping, and only its writer makes that
safe.** The two facts depend on each other, so they are stated together. R1.1's
gate cannot see a file whose version did not move, so a NEWER settings.json
passes it, and a writer that spelled only its own fields would drop everything
it had no field for. `settings::FileAsFound` carries the rest of the file past
the load and `persist::save_settings` writes it back as the bytes the file
carried. The day that catch-all goes, R1.3 stops being safe for settings.
Settings is the only store that survives a newer build's additions this way;
spaces does not, and R2.3 accepts that for spaces because spaces bumps.

**R1.3b A settings file that LOADS can always be SAVED.** `FileShape`
skips a key it has no field for with serde's `IgnoredAny`, which walks the
value's tokens and looks at nothing else - so the reader accepts a number
outside f64's range, a nesting past serde_json's recursion limit, and a lone
surrogate. The writer asked for the whole file as a `Map<String, Value>`, which
refuses all three, so a file that loaded was declined on every save with the
file left byte-identical, for good. R1.2 forbids overwriting a store this build
cannot READ; refusing one it CAN is a different thing, and it costs the user
every later edit to every other setting. So `settings::load_as_found` reports
rather than refuses, and a value its parser will not take is carried through as
the bytes the file spelled it with - the same answer a key with no field gets,
for the same reason: nothing looked at it. The `.bak` check answers to the
reader too, or the one file most likely to need a recovery copy is the one that
never gets one.

**R1.3c A settings migration beats preservation, and only the loader can tell
the two apart.** R1.3a's catch-all puts a key's own text back whenever
the load moved the value - that is what makes a later build's spelling survive a
downgrade. A migration moves the value too, so a key retired without a version
bump comes back verbatim on the next save and the migration undoes itself, every
time. `settings::load_as_found` reports which keys it MIGRATED and
`settings::merge` writes those from the value instead of the span. Nothing
downstream can work it out: a migrated key and a spelling `one_of` did not
recognise both reach the writer as this build's default sitting beside a span
that says something else. First use is `memorySaverThreshold`'s retired
`"never"`, split into `memorySaverEnabled` plus a duration -
`fixtures/stores/settings/v1-memory-saver-never.json`, and
`persist::tests::the_retired_memory_saver_spelling_does_not_come_back_on_the_next_save`
for the two saves it takes to see it.

**R1.4 Current versions, as shipped.** spaces 6, hotkeys 2, settings 1,
themes 1, routing 1. The command-bar selection memory has never carried a
version at all. `SchemaAcceptanceTests.testEveryStoreAcceptsItsOwnCurrentVersion`.

---

## 2. The spaces migration chain

`SpacesStore.applyIfAcceptable`, and `fixtures/stores/spaces/*`.

**R2.1 One load runs the whole chain.** A v1 file runs v1 to v2 and then v2 to
v3 in the same load. The gate is shared by the main-file and `.bak` paths, so a
crash right after a bump can leave `.bak` at either older version and both still
recover. `SpacesStoreTests.testCorruptMainFileWithMigratableV1BakRecoversAndMigrates`.

**R2.2 v1 to v2 stamps `archiveEpoch` and touches nothing else.** It is the
origin for auto-archive eligibility. It must NOT rewrite any tab's
`lastActiveAt`: flattening those to the migration instant makes
`Space.startupTab` - which is `mostRecentTab ?? tabs.first` - pick arbitrarily.
Visible in `fixtures/stores/spaces/v1-original-keys.expected.json`, where the
tab's absent `lastActiveAt` is still absent afterwards.

**R2.3 v2 to v3 is a version bump only.** `windowSpaces` is additive and
optional, so there is nothing to backfill. The bump still matters: without it an
older build reads the file as current and can persist over the `windowSpaces`
key it does not understand.

**R2.4 A current-schema file with no `archiveEpoch` heals itself.** A normal
write always stamps it, so its absence is corruption or a hand edit. Left alone
the file is permanently inert - `TabArchiver.sweep` returns early, and
archiving, retention pruning and expiry stop forever with no way to recover.
The store backfills and persists instead.

**R2.5 The sidebar-width backfill runs once, ever.** An old file at 280
is corrected to 220 and marked migrated. Keying on the VALUE instead of a
persisted one-shot bit reverted a user who deliberately chose 280, on every
launch, forever. `SpacesStoreTests.testSidebarWidthMigrationRunsOnlyOnce` and
`.testDeliberatelyChosenTwoEightyRoundTripsAfterMigration`.

**R2.6 Hotkeys v1 to v2 unions in the defaults and keeps explicit unbinds.** An
action written as an empty array is a deliberate unbind and stays empty; an
action absent from the file falls back to its default. An unknown action name
or an unparseable combo is dropped per entry, never fatally - a typo in a
hand-edited file costs one binding, not the whole config. `HotkeySettings.load`,
`HotkeySettingsTests.testLoadIgnoresUnknownActionsAndBadCombosPerEntry`, and
`fixtures/stores/hotkeys/v1-legacy.expected.json`.

**R2.7 v5 to v6 moves the search engine and the hibernate policy onto the
profile.** The two space fields are CLEARED rather than left as a second copy,
so the next save stops writing them and `lifecycle::hibernate_policy` has one
place to read. Two spaces sharing a profile cannot both set a value: the FIRST
space wins, and taking a later one would silently prefer whichever was created
last. `spaces::migrate_v5_to_v6`,
`spaces::tests::the_v5_step_moves_the_engine_and_the_hibernate_policy_to_the_profile`,
`spaces::tests::the_first_space_on_a_profile_wins_the_engine`.

---

## 3. Favourites

`SpacesStoreTests.swift` lines 1445 to 1660, and `SpacesStore.migrateFavorites`.

**R3.1 The key is (profileID, url).** Nothing else. Two spaces sharing a
profile share ONE favourite for a URL; the same URL under two profiles is two
entries. `.testAddFavoriteDedupsByProfileAndURL`,
`.testAddFavoriteDoesNotDuplicateAcrossSpacesOfTheSameProfile`,
`.testAddFavoriteAllowsSameURLAcrossDifferentProfiles`.

**R3.2 A duplicate add is a no-op, including its title.** Re-adding an existing
(profile, url) must not overwrite the stored title. `.testAddFavoriteDedupsByProfileAndURL`
asserts the title specifically.

**R3.3 Twelve per profile, and the cap rejects rather than evicts.** The 13th
add is refused; nothing existing is dropped. The cap is per profile, so one
profile at its limit does not affect another. `.testAddFavoriteRejectsThe13thEntryForAProfile`,
`.testAddFavoriteCapIsPerProfileNotGlobal`.

**R3.4 `spaceID` is nil on load, always.** `migrateFavorites` collapses the old
per-space expansion to one entry per (profile, url) in first-seen order and
clears `spaceID`. Lossless - a URL favourited in three spaces of a profile
becomes that profile's single favourite rather than being dropped - and
idempotent, since the key is already unique. A favourite whose profile has no
spaces is kept: nothing in this path reads `spaces`.

**R3.5 Order is per profile, and a reorder addresses profile-list indices.**
`profileFavoriteIndices` filters to the profile FIRST and then enumerates.
Enumerating the whole array first hands back global indices and misaddresses
every reorder the moment a second profile has favourites. Out-of-range indices
clamp; an out-of-range source is a no-op. `.testMoveFavoriteOnlyReordersTheGivenProfilesSubset`,
`.testMoveFavoriteToIndexPastTheEndClampsToTheLastPosition`,
`.testMoveFavoriteWithNegativeToIndexClampsToZero`,
`.testMoveFavoriteOutOfRangeFromIndexIsANoOp`.

**R3.6 A favourite mutation touches nothing else.**
`.testFavoriteMutationsDoNotTouchSpacesOrTabs`.

---

## 4. Per-space profile binding

`SpacesViewModelTests.swift` lines 128 to 460.

**R4.1 A space belongs to exactly one profile, and `store.spaces` is never
filtered by it.** Every space is in the list, whichever profile is active. This
is why space cycling can reach a space on another profile at all.

**R4.2 A same-profile switch activates a tab. A cross-profile switch activates
a WINDOW.** Same-profile must not activate a window;
cross-profile must not activate a tab.
`.testSameProfileSwitchSwapsActiveSpaceAndSetsTint`,
`.testCrossProfileSwitchActivatesTargetWindowInsteadOfATab`.

**R4.3 A switch picks the target's most recently active tab, not its first.**
With no last-active record, nothing is activated and nothing is opened.
`.testSameProfileSwitchActivatesLastActiveLiveTabWhenPresent`,
`.testSameProfileSwitchWithNoLastActiveTabDoesNotActivateAnyTab`.

**R4.4 A cold space materialises its most recent tab, once.** A space whose
tabs have never gone live in this window slot - never opened here, restored
from disk, or dropped by the memory saver - gets a new-tab request rather than
an activation. Switching away and back must not materialise it a second time.
`.testSameProfileSwitchToColdSpaceMaterializesItsMostRecentTab`,
`.testSameProfileSwitchAwayAndBackToStillColdSpaceMaterializesOnlyOnce`.

**R4.5 A split closes before the switch, on both paths.**
`.testCrossSpaceSameProfileSwitchClosesSplitBeforeActivatingTab`,
`.testCrossProfileSwitchClosesSplitBeforeActivatingWindow`.

**R4.6 An unregistered profile still swaps the active space.** The tint, the
window activation and the split close are all skipped, but the model moves. A
switch must not be silently dropped because no window happens to be open for
that profile. `.testSwitchToUnregisteredProfileStillSwapsFilterButSkipsOps`.

---

## 5. Renderer isolation

The four rules in this section. **Owner: C++ and the sidebar JavaScript.** None
of these is Swift, so none moves at the cut - but a Rust core that starts
serving the sidebar can break every one of them.

**R5.1 The sidebar renderer is pinned to one fixed BrowserContext**, whatever
profiles its rows belong to. Following the active space's profile tears the
renderer down and rebuilds it on every space switch.

**R5.2 The renderer is stateless.** No localStorage, no IndexedDB.
Cross-profile data arrives over the bridge and is persisted by the browser
process only. Anything landing in the renderer's own storage is a cross-profile
leak.

**R5.3 Favicons are pushed, never fetched by the renderer.** A fetch issues
requests on the sidebar's own profile and contaminates its cache and cookies.

**R5.4 One renderer per window, not per space.** A space switch is a state
push, not a reload.

---

## 6. Cross-window and cross-surface

Six bugs were filed against these. Each rule below is the one that was missing.

**R6.1 Rows and actions must resolve the same target.** Rows come
from `CopySidebarModelJSON(controller->host())`, which resolves host to SLOT to
that slot's active space. Every action has to re-resolve the same way. An action
that acts on the invoking window's own `browser_` diverges the moment a
cross-profile activation keeps the presenter's window on screen and swaps
another controller's WebContents into its pane - and with five spaces on five
profiles, every switch is cross-profile, so presenter and visible stay split for
the rest of the session. In that state rows carry real ids, selection matches
none of them, and clicks do nothing at all.
Owner: C++, `atrium_browser_window.mm`.

**R6.2 The snapshot owns the active space; the surface does not remember it.**
The swipe controller used to keep its own committed index across
rebuilds, which was right when nothing produced an authoritative active space
and wrong once the model produced one. When the rail and the model disagree, a
drag reads its source from the model and its destination from the rail, so an
ordinary reorder becomes a cross-space move.
Owner: `swipe.js`, `sidebar-drag.js`.

**R6.3 Closing the active tab activates a survivor.** An Atrium
window's Chromium tab strip holds only ONE live tab - other sidebar rows are
cold until clicked - so closing the active tab leaves the strip empty and the
window blank, with a good row one click away. The strip being empty is also
what disables the whole reserved-command family.
Owner: C++, `atrium_browser_window.mm`, `atrium_browser_controller.h`.

**R6.4 A space-cycle chord must reach the model.**
Cycling went through a SwiftUI shortcut button, and the chord reached nothing at
all - not a wrong branch, no call. It is a native intercept now: the C++ hotkey
map steps the slot one space forward or back, wrapping at both ends, in the
sidebar's own order. `SpaceSwitchRouting.cycleTarget` holds the arithmetic. The
base for a cycle is the in-flight target if there is one, else the active space
- so a fast second press steps from where the first one is going, not from
where the screen still is.
`SpaceSwitchRoutingTests.testCycleTargetWrapsAtBothEnds`,
`.testCycleStepsFromTheInFlightTargetWhenOneIsArmed`,
`.testCycleTargetCrossesAProfileBoundary`,
`.testCycleTargetIsNilWhenThereIsNowhereToGo`.
Owner: C++ hotkey map plus `SpaceSwitchRouting.swift`. **The arithmetic moves;
the intercept does not.**

**R6.5 A model push must not destroy folds.** Pushing a model through
`atriumSetSidebar` removed the folder from the sidebar completely - header and
container both gone, not merely unfolded. The push has to reconcile against
what is rendered. The active-space field is always sent,
and a missing one falls back to the first space rather than to nothing.
Owner: `sidebar.js`.

**R6.6 The selected row and the space tint are resolved by the core, not the
surface.** Both were sent unusable: `selected` was hardcoded false in
every row, and `spaceTint` carried the stored ref (`oceanBlue`), which
`sidebar.js` assigns straight to `--selbase`. So the active row drew no pill
and the tint never arrived.

- **Selection is per WINDOW and rides the live strip.** The document records
  an active space and nothing finer, and two windows on one space show
  different tabs. The strip's entries carry `"active": true` on one of them,
  and the selected row is the one that consuming join handed that id to -
  never a URL match, which would mark every row sharing the URL. Only the
  active space gets live ids, so only it can hold the pill. A strip with no
  flag selects nothing.
- **A tint ref becomes a colour in `atrium-core::tint`.** The palette also
  feeds the host's native backdrop ARGB, so putting it in CSS would need a
  second copy, and two copies of a palette drift. The core already produces
  every other colour the sidebar draws. `spaceTint` and `spaces[].color` are
  always `#RRGGBB`; an unknown ref falls back to `atriumSolarized`.

Owner: `rust/atrium-core/src/sidebar.rs`, `src/tint.rs`; the `active` flag is
written by `LiveTabsJSON` in C++.

---

## 7. Persistence mechanics

`SpacesStore`, and `SpacesStoreTests` throughout.

**R7.1 Writes are debounced and coalesced; `saveNow()` is synchronous.** The
shutdown hook and the tests both need the write to have completed by the time
it returns. `.testScheduleSaveCoalescesRapidMutations`.

**R7.2 Both writers share one serial queue.** A `saveNow()` landing while a
debounced write is in flight is ordered after it, never racing it.

**R7.2b The Rust core splits the debounce into two calls.** A mutation export
(`atrium_core_reorder_tab` and the ten beside it) changes the in-memory
document only and returns whether anything moved; `atrium_core_save` is the one
call that blocks. The timer is gone - the crate has no clock and no runloop -
so the caller decides when to write, and a save with nothing dirty writes
nothing. The serial queue survives as a second lock, held across the file IO
and not across the snapshot, so an fsync never stalls the UI thread.

**R7.3 A `.bak` is written before the main file, and read back to recover.**
The recovery path accepts a `.bak` at any point in the migration chain (R2.1).

**R7.3c A backup that cannot be written does not refuse the save it rides
with, and does not vanish either.** Found while fixing an earlier backup bug:
`save_document` and `save_settings` both called `backup_if_valid` and threw
its result away with `let _`, so a save returned `Ok` with no `.bak` behind it
and nothing said. The bug was never the choice to proceed - a comment beside
the `let _` already said "reported rather than fatal" - it is that nothing
ever reported it.

REFUSE was rejected. `spaces.json` is written on every sidebar gesture
(R7.2b), and a backup can fail for reasons that have nothing to do with
whether the document itself is fine to write - one bug showed a single
leftover temp file at the `.bak`'s own counter was enough, and a disk running
low hits the recovery copy exactly as often as the main one. Refusing the
save over that would turn a transient, invisible problem into every later
edit lost until the user notices persistence has stopped, for a reason no
surface shows them - the same cost R1.3b already rejected refusal for, sharper
here because a `.bak` failure is more likely to be environmental than a
`.bak` failure is likely to mean the main write is unsafe. The two are
independent: the main file's own safety (atomic rename, 0600, sorted keys)
does not depend on the recovery copy landing.

So: proceed, and record. `save_document` and `save_settings` return
`persist::BackupOutcome` - `Ok` for a backup that landed or was correctly
declined (`backup_if_valid` on a file not worth keeping, R1.2's `.bak` half),
`Failed(io::Error)` for one that tried and could not. Both do the main write
regardless. `persist::tests::a_document_save_reports_a_backup_that_could_not_be_written`
and `.a_settings_save_reports_a_backup_that_could_not_be_written` force the
failure with a directory sitting where `.bak` should be, so the rename inside
`write_secure` cannot land, and assert the main save still ran.

Deliberately not carried further in this task: `cache::Core::save` and the
`atrium_core_save` / `atrium_core_save_settings` ABI exports keep their
existing `Result<bool, String>` and `i32` contracts, asserted on throughout
the test suite as plain success/failure. Extending them to say WHICH kind of
problem a save hit is a real, separate change - `atrium_core_save_settings`
already differentiates `-1` from `-2` for exactly that reason - and does not
belong to the fix for a value being silently thrown away.

**R7.4 Credentials are redacted on WRITE, and the in-memory model keeps the
real URL.** `redactingCredentials()` runs inside `persist()` immediately before
encoding, on a copy. Live navigation, autofill and every mutation still see the
unredacted URL; only what lands in `spaces.json` is stripped. It covers tab
urls, pinned tab urls, `pinnedURL`, favourites, archived tabs, shelf items and
snoozed tabs - every collection in the document.

**This is the one to watch in the port.** Redaction is on the write path only.
`SidebarModelJSON` does not redact, and everything downstream of it is clean
only because the state it reads has already been through the store. A producer
fed in-memory state that was never persisted leaks the password. Measured, not
inferred: `SidebarModelSnapshotTests.testTheSnapshotCarriesNoCredentials` builds
the same configuration unsaved and finds the credentials still there.

**R7.6b Only `spaces.json` is written deterministically.** Its encoder sets
`.sortedKeys`; the other five stores write in dictionary order, which Swift
randomises per process. Measured, not read: seeding `settings.json` twice
produces two different files while `spaces.json` produces the same bytes.

Two consequences. A byte comparison against those five means nothing, so the
golden corpus compares their DOCUMENTS. And every save of them rewrites the
file even when nothing changed, which is noise in any drift guard or `git diff`
over a user's stores.

The Rust core writes all six with sorted keys, so it is deterministic where
Swift is not. That is a deliberate improvement and it means those five files
differ byte-wise from what Swift wrote - the same document, a stable spelling.

**R7.5 A tab's runtime token is stable across unrelated mutations.**
`SpaceTab` is spec-frozen without an id, so the store keeps a parallel token
list per space and moves the token with the tab. Two windows can each hold a
live tab on the same URL in the same space, and a token is what tells them
apart. `.testTabTokenIsStableAcrossUnrelatedMutations`,
`.testTabTokenTracksItsTabAcrossARemovalThatShiftsIndices`,
`.testTokenForARemovedTabNoLongerResolves`.

**R7.6 A live tab id is handed out by CONSUMING it, not by lookup.** URL is the
only join between a persisted tab and a live one. A first-match-wins map hands
the same id to every row sharing a URL - two blank tabs, two tabs on one site -
and every one of those rows then activates the same tab. `SidebarModelJSON.LiveTabIDs`
is a queue per URL; once a URL's tabs are used up, later rows get an empty id,
which is the existing contract for a cold row.

**R7.7 A space id that looks like a path is rejected.** `addSpace` falls back to
the default profile id rather than accepting a traversal-shaped one.
`.testAddSpaceRejectsTraversalShapedIDsAndFallsBackToDefault`.

---

## What this page does not cover

`SpacesViewModel` is 6,231 lines of behaviour rather than data, and
the design leaves it where it is for now. Its rules are not here. When it moves, this page
needs a section on tab adoption, the memory saver, and routing.
