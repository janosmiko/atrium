# Atrium Private Mode - Design

**Date:** 2026-07-25
**Status:** DRAFT - design only. No implementation.
**Scope:** private/incognito browsing under the profile-per-space model; the store-suppression matrix; what drives `CommandBarModel.isPrivateContext`

## 1. What exists today

Verified from the tree, not from general Chromium knowledge.

| Fact | Evidence |
|---|---|
| A space names a profile *directory*; C++ builds the path from it | `Space.profileID` -> `RequiredProfilesJSON.uniqueProfileIDs` -> `user_data_dir.AppendASCII(profile_id)` (`atrium_profile_bootstrap.mm`) |
| A `BrowserController` exists per `(slot, profile)`; `profile_id_` = `profile->GetBaseName()` | patch 0003 `:3771`, `:3985`; class doc `:1892` |
| Cross-profile space switch = *activate another window*, not repaint one | `SpacesViewModel.switchToSpace` `:1985-2010` |
| Controllers and Swift hosts are process-lifetime; nothing ever unregisters | patch 0003 `:1900-1916`; `HostRegistry.swift` Lifetime doc |
| Chromium's own session restore is off for every Atrium profile | `SetSavingEnabled(false)` in `OnProfileAdded`; `omit_from_session_restore = true` |
| Restore is entirely Swift-driven | `AppState.activeSpaceID` + `windowSpaces` + `StartupTabJSON` + `RequiredProfilesJSON` |
| Per-slot teardown hook already exists | `AtriumOnSlotClosing` -> `SpacesViewModel.markSlotClosing` |
| Chromium OTR machinery **is** reachable and partly handled | `browser_window_factory.cc` gives every `is_type_normal()` browser an `AtriumBrowserWindow`; `IsOffTheRecord()` guards exist in close-suppression (0003 `:12065`), all three routing hooks (0007 `:777/:878/:999`), and the favicon fetch op (0003 `:4811`) |
| Nothing ever constructs an OTR profile | no `GetPrimaryOTRProfile` / `GetOffTheRecordProfile` call anywhere in `patches/` |
| A chromeless single-tab window primitive already exists | `BrowserController::CreateLittleWindow` - skips sidebar, command bar, and `RegisterHostInSlot` (0003 `:2049`, `:3829`, `:3886`) |
| The autocomplete client is built from the controller's own profile | `AutocompleteBridge(host(), profile)` (0003 `:3896`, `:4060`) |

**Consequence today:** any stock incognito Browser (extension, "open in incognito" context menu) already gets an `AtriumBrowserWindow` with no `BrowserController` - so no sidebar, no palette, no space integration. That failure mode is confirmed, not hypothetical.

**Nothing is bound to Shift+Cmd+N.** `global_keyboard_shortcuts_mac.mm` only adds `Ctrl+Cmd+N -> IDC_NEW_WINDOW` (patch 0004 `:35`).

## 2. Decisions

### 2.1 The unit of privacy is a private WINDOW that owns one ephemeral private SPACE

Private browsing is a **window**. Each private window is a normal Atrium window-slot whose `BrowserController` is built against a Chromium **off-the-record profile**, rendering exactly one **synthetic, in-memory-only space** that never enters `SpacesStore`.

**Why not a private TAB.** Architecturally blocked, not a preference: a `Browser`'s tabs all share that `Browser`'s `Profile`. `BrowserController` is `(slot, profile)`-keyed and `profile_id_` is set once in each initializer list with no setter. A private tab in a normal window would need one `Browser` spanning two browser contexts.

**Why not a private SPACE alone.** A space is a durable, persisted, profile-*directory*-named object:

- `RequiredProfilesJSON` -> `AppendASCII(profile_id)` -> `CreateProfileAsync` requires an on-disk directory name. An OTR profile has none.
- A space is written to `spaces.json` and rendered in **every** window's sidebar (`SidebarView` reads `store.spaces`), and restored aggressively.
- `ProfileBootstrap` would preload it at launch and pin it with a `kAtriumSpace` keep-alive.

**Why a WINDOW works.** The slot already maps 1:1 to a `Browser` and to a Swift host; `CreateWindowSlotForProfile` already opens a slot for a named profile; `markSlotClosing` is already the teardown hook; per-slot state (`slotActiveSpaceID`, `slotActiveTab`, `tabMRU`) is already ephemeral. An OTR profile's natural lifetime - destroyed with its last `Browser` - lines up, because close-suppression explicitly excludes OTR so a private window genuinely closes.

The window still needs *a* space, because `sidebarRows(forSlot:)` resolves everything from the slot's active space. That space is synthesized in `SpacesViewModel` only.

### 2.2 The private profile is a Chromium OTR profile of the invoking window's profile

`original->GetPrimaryOTRProfile(/*create_if_needed=*/true)`, where `original` is the profile of the slot that invoked the private window.

Real OTR gives no-history, no-cookie-persistence and evaporate-on-last-close for free, plus OTR-correct autocomplete via `ChromeAutocompleteProviderClient(otr_profile)`. Deriving from the invoking profile keeps extensions and the default search engine consistent with what the user was just using.

**Rejected:** a throwaway on-disk profile directory deleted at quit - it writes browsing data to disk, and deletion is not guaranteed on crash.

**Consequence (Chrome parity):** all private windows derived from the same original share one private session, which evaporates when the last closes. See Q2.

### 2.3 Privacy identity is an explicit bit, never inferred from `profile_id_`

`profile_id_` is `profile->GetBaseName().AsUTF8Unsafe()`. Upstream `OffTheRecordProfileImpl::GetPath()` returns the **original** profile's path, so an OTR profile's basename is expected to be identical to its original's. A private host registered naively would land in the *real* profile's namespace - exactly the cross-profile leak the command bar history guard exists to prevent, silently, with no crash. (Unverified locally; see R1. The design does not depend on the answer.)

Three parts, one write site:

1. C++ `RegisterHostInSlot` gains a privacy flag (additive-ABI convention): `RegisterHostInSlot(host(), profile_id_, slot_, /*is_private=*/private_)`. `private_` is a `const bool` member set in the initializer list alongside `little_`.
2. `HostRegistry.registerHost(_:profileID:slot:isPrivate:)` stores the bit **and** derives the namespace in the same statement:
   `hostToProfile[token] = isPrivate ? "private:\(slot)" : profileID`.
   `':'` is outside `ImportApplier.allowedProfileIDCharacters` and outside `ProfileBootstrap::IsSafeProfileId`'s allowlist, so this token can never equal a real profile directory name, become a `Space.profileID`, or survive `RequiredProfilesJSON`'s filter. Same trick as the shipped `unregistered-host:<token>`.
3. Nothing ever pattern-matches the string. The bit is the fact; the string is derived from the bit.

### 2.4 Lifetime, teardown, and the restore hazard

| Event | Guarantee | Mechanism |
|---|---|---|
| Private window closes | OTR profile destroyed with its last `Browser` | Chromium; close-suppression already excludes `IsOffTheRecord()` (0003 `:12065`) |
| Private slot torn down | per-slot ephemerals dropped | `markSlotClosing` already clears `slotActiveSpaceID`, `windowSlotOrder`, `tabMRU`, `slotActiveTab`; add: drop the synthetic space |
| Normal quit | nothing to flush | `AtriumWillTerminate` saves `SpacesStore`/`ThemeStore`; private state was never in either |
| Crash | nothing to clean | no private state ever reaches disk (2.5) |
| Relaunch / restore | private window never returns | three independent barriers below |

Restore barriers (any one suffices; all three hold):

1. `recomputeWindowSpaces()` is `windowSlotOrder.compactMap { slotActiveSpaceID[$0] }`. A private slot keeps `slotActiveSpaceID[slot] == nil`, so it compacts away - fail-safe by construction, no new filter to forget.
2. The synthetic space is never in `store.spaces`, so it can never be `activeSpaceID` and `StartupTabJSON` can never name it.
3. `"private:<slot>"` fails `isValidProfileID`, so `RequiredProfilesJSON` drops it even if barrier 2 were breached.

**Must-fix, found in the constructor:** `AcquireProfileKeepAliveForSlot(profile)` runs *before* the `little_` branch (0003 `:3806`) and `ReleaseProfileKeepAliveForSlot(profile_id_)` is unconditional (`:4463`). For a private controller both key on the **original** profile's basename, so closing a private window would decrement a real profile's `kAtriumSpace` refcount; and `ScopedProfileKeepAlive` on an OTR profile is not a supported shape. Both must be gated on `!private_`. Chromium's own `kBrowserWindow` keep-alive is the correct pin for an OTR `Browser`.

### 2.5 Store-by-store posture

| Store | On disk | Private posture | Where |
|---|---|---|---|
| `SpacesStore` - spaces, tabs, pinned, groups, split | `spaces.json` | Suppress | synthetic space never inserted; `syncTabURL`/`syncTabTitle`/`syncTabClosed`/`recordLastActive` no-op for a private slot |
| `SpacesStore` - `archived` | `spaces.json` | Suppress | auto-archive sweep and `archiveTab` skip private slots |
| `SpacesStore` - `favorites` | `spaces.json` | Suppress | private namespace matches no `Favorite.profileID`, so the strip is empty and add/remove unreachable - already structural |
| `SpacesStore` - `AppState.windowSpaces`/`activeSpaceID` | `spaces.json` | Suppress | 2.4 barrier 1 |
| `ThemeStore` | `themes.json` | Suppress write | fixed built-in private `ThemeSpec`; never `setTheme(_:for:)` |
| `CommandBarSelectionMemory` | `command-bar-selection-memory.json` | Suppress | `isPrivateContext` gate in `recordSelectionIfEligible` (already written, currently inert) |
| `FaviconStore` | `favicons/<host>.png` | Suppress write, allow read | `record(pngData:forURL:)` skipped for private tabs. The leak direction is private browsing *creating* an icon for a host never visited normally. See Q6 |
| `RoutingSettings` | `routing.json` | Read-only, inert | C++ already bails on `IsOffTheRecord()` in all three hooks, so no route fires *out of* a private window; additionally block routing *into* one |
| `RoutingLedger` | in-memory, 3s TTL | Safe | ephemeral; moot given routing is off |
| `SettingsStore`, `HotkeySettings` | `settings.json`, `hotkeys.json` | Already safe | no browsing data |
| Capture output | user-chosen file | Allow - explicit user action | see Q4 |
| history, cookies, cache, autofill, download history | profile dir | Handled by OTR itself | no Atrium work |
| Downloaded files | Downloads dir | Persist by design | matches Chrome incognito |

**Structural bonus.** `OpenTabsSource.rows(spaces:liveTabs:favorites:)` only emits a live tab when `tab.spaceID == space.id` for some space in the passed list. Because the private space is never in `store.spaces`, a private tab's title and URL cannot appear in a normal window's Switch-to-Tab list. "Never enters `SpacesStore`" doubles as the cross-window leak guard.

**The reverse direction needs an explicit fix.** The private window's own palette would be fed `SpacesStore.shared.spaces` and every live tab. In a private window, restrict the open-tabs section to that window's own tabs and drop the `switchToSpace` commands - otherwise the private window becomes a portal that activates normal windows.

### 2.6 Visual identity

Reuses existing mechanisms; no new plumbing.

- Window backdrop: a fixed, non-editable built-in private theme (graphite/near-black) via the existing `applyBackdrop` per-space path. Visible in the whole content pane, not just a badge.
- Sidebar: exactly one space row labelled `Private`, with a mask glyph instead of an emoji. Not renamable, no tint picker, no "Move to Space". The absence of the user's own spaces is the loudest available signal.
- Sidebar header: a small `Private` pill.
- No favorites strip, no archive entry point, no Settings entry point from a private window.

## 3. What `isPrivateContext` is derived from

One source, read once, where the profile namespace is also resolved.

- `HostRegistry` gains `isPrivate(host:) -> Bool`, written by `registerHost(_:profileID:slot:isPrivate:)` in the same statement that derives the namespace (2.3).
- `AtriumCreateCommandBar` already resolves `profileID` there and threads it into `CommandBarModel` as a `let`. Add the privacy read to that same block, threaded the same way.
- `CommandBarModel.isPrivateContext` changes from `public var` to a `public let` init parameter (default `false`).

**Why record and lookup cannot disagree:** both `profileID` and `isPrivateContext` become immutable `let`s, both produced by a single read of a single `HostRegistry` entry in one statement block, and a host's profile can never change afterwards (`hostToProfile` is never rewritten by a space switch - only `visibleHostBySlot` is). This is the command bar history guard's argument extended by one field.

**Fail-closed on the unresolved-host branch.** The existing `else` arm keeps its `assertionFailure` and synthetic `"unregistered-host:<token>"` namespace, and additionally sets `isPrivate = true`. A host we cannot identify must not be recorded against. Assertions compile out of release, so the release-path value must be safe on its own.

**Test change required:** `CommandBarSelectionRecordingTests.swift:204` sets `h.model.isPrivateContext = true`; it becomes a constructor argument.

## 4. Stubs and deferrals pointing at this task

| Location | What it says | Resolved by |
|---|---|---|
| `CommandBarModel.swift:1283-1290` | `isPrivateContext` wired ahead of private mode; "can never be set to true by anything that currently exists" | 3 |
| `CommandBarModel.swift:1862-1873` | `recordSelectionIfEligible` gate: "never in a private/incognito context" | 3 (gate becomes live) |
| `CommandBarSelectionRecordingTests.swift:10, 198-208` | test named for private mode, pins the gate | 3 (init-parameter migration) |
| Command bar history requirement | "Not recorded for incognito/private contexts" - currently unverifiable end-to-end | 3 + Phase 1 |
| Command bar history comment | "whether private/incognito contexts must be excluded" | 2.5 - yes, excluded |
| Arc hotkey row `New Incognito Window / Shift+Cmd+N` | NEEDS-VERIFICATION; verified: no private mode exists, so NEEDS-FEATURE | Phase 1 binds it |
| Arc hotkey row `Blank Window / Ctrl+Cmd+N` | resolved separately - already mapped to `IDC_NEW_WINDOW` (patch 0004 `:35`) | closed here |
| Arc hotkey audit, judgment call 4 | "hinges on whether the profile-per-space model has a private mode" | 2.1 |
| `AtriumBrowserWindow::ShowIncognitoClearBrowsingDataDialog` / `ShowIncognitoHistoryDisclaimerDialog` (0003 `:11934-11940`) | `NOTIMPLEMENTED_LOG_ONCE()` | Phase 3. Both are stock incognito-NTP affordances; Atrium's private window has no NTP |
| `docs/hotkeys/arc-defaults.md:82` | catalogue row, unbound | Phase 1 |

## 5. Phasing and non-goals

| Phase | Content | Ships |
|---|---|---|
| 0 | Thread `isPrivate` through `RegisterHostInSlot` -> `HostRegistry` -> `CommandBarModel` as a `let`; fail-closed nil branch; gate the keep-alive acquire/release on `!private_`; tests | No user-visible change; de-inerts the gate the instant Phase 1 lands |
| 1 | `BrowserController::CreatePrivateWindow` (OTR profile, own slot, full chrome, one synthetic space, private theme, palette scoped to own tabs, no space-switch commands); bind Shift+Cmd+N | First shippable slice |
| 2 | Suppression hardening + regression tests: favicon write guard, auto-archive skip, routing in/out, theme suppression, `windowSpaces` exclusion, quit-and-relaunch leaves no trace | Guarantees, not features |
| 3 | Multiple isolated private sessions; promote a private tab into a real space; "Close All Private Windows"; extensions-in-private opt-in; the two incognito dialogs | Deferred |

**Non-goals.** Private tabs inside a normal window (blocked, 2.1). Persisting anything about a private session. Private spaces in normal windows' sidebars (Q1). Sync interaction - nothing to sync. A private-specific new-tab page. Extensions in private (Chromium requires per-extension `incognito_enabled`; default off, so the pill will look empty - a visible difference, not a bug).

## 6. Risks and unknowns

| # | Risk | Posture |
|---|---|---|
| R1 | Unverified: whether `Profile::GetBaseName()` for an OTR profile equals its original's. The pinned Chromium checkout was not readable during this pass. | The design never uses `profile_id_` as the privacy discriminator, so the answer changes nothing. Verify before Phase 0 merges - if it does collide, the keep-alive gating in 2.4 is a correctness fix, not hygiene |
| R2 | Hosts and controllers are process-lifetime and never unregister. A closed private window's host stays in `hostToProfile`/`hostToSlot`, and its `CommandBarModel` stays in `CommandBarState.modelsByHost` holding the last query until process exit. | In-memory only; no disk. Mitigate by clearing the model's query/results in `markSlotClosing` for private slots. A real host-unregistration path is out of scope (pointer reuse hazard) |
| R3 | Any stock incognito entry point already produces a controller-less `AtriumBrowserWindow`: no sidebar, no palette. | Pre-existing. Phase 1 should route these through `CreatePrivateWindow` or remove the entry points; do not ship the half-window |
| R4 | Confirming "no residual disk footprint" for an OTR profile needs runtime observation, which a design pass cannot assert. | Phase 2 acceptance: launch private, browse, quit, diff the profile dir and `~/Library/Application Support/Atrium/` |
| R5 | Widevine / codecs raised as a possible private-mode difference. No difference is possible today: no CDM ships in source builds (`docs/LICENSING.md`), and codecs are compile-time (`build/args.gn`). | Closed, not escalated |

## 7. Questions for the repo owner

| # | Question | Options | Recommendation |
|---|---|---|---|
| Q1 | Does the private space appear in **normal** windows' sidebars? | (a) No - window-local, invisible elsewhere. (b) Yes - one extra row while it exists. | (a). (b) needs a parallel non-`store.spaces` space list plumbed through `sidebarRows`, plus cross-profile switch handling for an OTR host, and puts private page titles in a UI that reads as persisted |
| Q2 | One shared private session across all private windows, or one per window? | (a) Shared primary OTR - Chrome parity. (b) Unique OTR per window. | (a) for Phase 1. (b) is a genuine feature (parallel identities) that fits the space model - deferred to Phase 3, not rejected |
| Q3 | Which original profile does the OTR derive from? | (a) The invoking window's. (b) Always `Default`. | (a) - keeps extensions and default search engine consistent. Note it makes "which private session am I in" profile-dependent, interacting with Q2 |
| Q4 | Is screenshot capture allowed in a private window? It writes a file. | (a) Allow - explicit user action. (b) Block. | (a). Blocking an explicit save is surprising; downloads already persist by design |
| Q5 | Does Shift+Cmd+N ship in Phase 1? | (a) Yes. (b) Command-bar action first, hotkey in Phase 2. | (a). The binding is trivial once the primitive exists, and the Arc hotkey table waits on this row |
| Q6 | Favicon reads in a private window from the existing on-disk cache? | (a) Read cached, never write. (b) Render no cached favicons at all. | (a). (b) is visibly worse for no disk-state benefit, but it is the stricter reading if the concern is a shoulder-surfer seeing a familiar icon |
| Q7 | How much chrome does a private window get? The one real cost fork. | (a) Full Atrium chrome + synthetic space (this design). (b) Chromeless single-tab reusing `CreateLittleWindow`. | (a). (b) is roughly a fifth of the work but has no tab list and no palette, so it leaves `isPrivateContext` permanently inert - it does not satisfy the reason this task is being done now |
