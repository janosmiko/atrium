# Headless pool and cross-profile analysis, Item A / Item B (2026-07-19)

Both items were investigated per the re-scoped audit. Neither needed a code
change: Item A's premise doesn't hold up under the widget's actual
construction-time dependencies, and Item B's core ask (the reentrancy fix +
flag flip) turned out to already be shipped on `main` before this session
started. Only Item B's cross-profile extension remains genuinely open, and it
is a new feature, not a follow-up patch - see its verdict below.

## Item A - headless hidden-pool widget: DEFER

Goal was to avoid constructing a real `views::Widget`/`NSWindow` for a
pool `BrowserController` (`show=false`) until it's actually needed.

Read `BrowserController::BrowserController` in
`chrome/browser/ui/atrium/atrium_browser_controller.mm` (both ctors,
~:697-936) against `AtriumBrowserWindow::AtriumBrowserWindow` in
`chrome/browser/ui/atrium/atrium_browser_window.mm` (:1614-1703).

Findings:

- `AtriumBrowserWindow`'s constructor unconditionally builds `widget_`
  (`views::Widget::Init(params)`, :1666-1679) - this is what actually creates
  the backing `NSWindow`. There is no `show` parameter to this constructor;
  it doesn't know or care whether the controller is a pool member.
- Before `BrowserController`'s ctor ever checks its own `show` argument
  (the check is at :812, `if (!show) { SetExcludedFromWindowCycling(true); }`),
  it has already called, unconditionally, on every controller:
  `window()->AttachSidebar(...)` (:751), `window()->EnsurePeekWidget()`
  (:759), `window()->AttachPeekChrome(...)` (:769, gated on
  `peek_connected()` not on `show`), `window()->AttachSidebarOverlay(...)`
  (:780, gated on `spaces_connected()` not on `show`), and
  `window()->AttachCommandBar(...)` (:804, gated on `command_bar_connected()`
  not on `show`). Every one of these calls dereferences `window()` and
  attaches into the widget's live view tree (`root_view_`,
  `AtriumRootView`/`AtriumPaneContainerView`) - none of them tolerate a null
  or not-yet-built widget.
- Those attach calls are themselves load-bearing at construction time, not
  cosmetic: `AttachCommandBar` hands the widget a `CommandBarWidget` child
  that the destructor explicitly requires be destroyed before `widget_`
  (:1710-1724 comment chain), and `AttachSidebar`/`AttachPeekChrome` wire the
  sidebar/peek NSViews the Swift side owns for this host's whole lifetime.
- Independent of Atrium's own code: `Browser::Create()` (upstream Chromium)
  synchronously invokes `CreateBrowserWindow()`, and a large amount of
  `//chrome` code (extensions, session code, devtools, `//chrome/browser` UI
  plumbing) calls `browser->window()` unconditionally expecting a live,
  non-null `BrowserWindow` the instant `Browser::Create()` returns. There is
  no upstream contract for a `Browser` whose window is deferred.

Conclusion: making the widget lazy would require (a) restructuring
`BrowserController`'s ctor so sidebar/peek/command-bar/overlay attachment is
itself deferred until first Show - a change to five call sites and the
child-widget lifetime/teardown ordering, not one - and (b) either breaking or
working around the upstream `Browser::window()` non-null invariant relied on
throughout `//chrome`. Both are real, cross-cutting redesigns, not a
single-controller optimization. Per the task's own escape hatch, this is
DEFERRED rather than attempted as a destabilizing rewrite; no chrome/ patch
change was made.

If resource cost of the hidden windows is revisited later: `views::Widget::Init`
on macOS does construct a real `NSWindow` object synchronously (via
`CreateNativeWidget()`), but ordering/compositing surface cost is dominated by
`orderFront`/visibility, not object construction - worth profiling before
assuming the current cost is significant, rather than assuming a rewrite is
warranted.

## Item B - cross-profile split save/restore

### Reentrancy fix + flag: ALREADY SHIPPED (no action needed)

The audit's premise - `defaultSplitLaunchRestoreEnabled = false`, blocked by
a `tab_strip_model.cc:140 !*guard_flag` reentrancy crash, fix direction
documented but not applied - is stale. It was fixed and shipped in
`c03aee5` ("fix(split): defer the bootstrap re-split off the TabStripModel
stack"), which is already an ancestor of this branch's HEAD (`git merge-base
--is-ancestor c03aee5 HEAD` confirms it, and it is on `main`).

Current state, verified by reading
`swift/AtriumUI/Sources/AtriumUI/SpacesViewModel.swift`:

- `defaultSplitLaunchRestoreEnabled = true` (:666).
- `armStartupAdoptionIfNeeded` (:3024) and `armSplitRestorePartner` (:3068)
  both take the same `runLater` continuation-deferral `enterSplitFromColdRow`
  already used, posting the partner materialization and the re-`enterSplit`
  off the bootstrap TabStripModel notification stack - the exact "runLater
  off the confirm callback" pattern the re-scoped task asked for.
- Regression coverage already exists in
  `swift/AtriumUI/Tests/AtriumUITests/SplitTests.swift`:
  `testLaunchRestoreDefersPartnerMaterializationOffTheStartupAdoptionCallback`,
  `testLaunchRestoreDefersEnterSplitOffThePartnerAdoptionCallback`, and
  `testLaunchRestorePartnerReActivatesStartupTabBeforeEnteringSplit` all
  inject a `runLater` that records instead of runs, so a synchronous
  re-entry would fail these tests rather than merely working by luck.

No code change was made for this part; it was already correct on disk.

### Cross-profile extension: DEFER (new feature, not a follow-up)

Investigated whether "cross-profile split" already exists as a live
(non-persisted) capability that restore could simply be pointed at. It does
not:

- `SplitArrangement` (`swift/AtriumUI/Sources/AtriumUI/SpacesModel.swift:111`)
  is persisted per-`Space`, and a `Space` belongs to exactly one profile - the
  persisted model has no room for a partner from a different profile.
- The live entry point, `SpacesViewModel.enterSplit(secondaryTabID:...)`
  (`SpacesViewModel.swift:1979`), and its C++ counterpart
  `BrowserController::SetSplitOp` (`atrium_browser_controller.mm:1522`),
  resolve `secondary_tab_id` via `ResolveTab`, which searches only
  `self->browser_->tab_strip_model()` - i.e. only the calling host's own
  (single-profile) tab strip (`atrium_browser_controller.mm:996-1012`). There
  is no ABI op or C++ path that can attach a WebContents owned by a different
  profile's `Browser`/`TabStripModel` into another window's secondary pane.
- `open_window_slot_for_profile` (the dependency named in the re-scoped
  audit) solves a different problem - opening a *new window*
  for a persisted space's own profile - not attaching a foreign profile's
  content into an existing split.

Conclusion: "cross-profile split" isn't a gap in the restore plumbing, it's a
feature that doesn't exist yet anywhere in Atrium (interactively or
otherwise). Building it means a new cross-host tab-resolution ABI op, C++
changes to reparent a foreign profile's WebContents into a window's secondary
pane (with whatever process/isolation implications that carries), a new
persisted arrangement shape carrying two profile IDs, and new Swift restore
logic on top of that. That is a milestone of its own - the same "stop and
size it honestly" call the task asked for on Item A - not an extension
bolted onto the existing single-profile restore path. No change was made.

## Verification

- Working tree: clean before and after this investigation (no edits to
  `chrome/`, `components/`, or `swift/` were needed).
- `swift test` (full suite, `swift/AtriumUI`): **2070 tests, 0 failures**,
  matching the stated baseline exactly.
- No Chromium patch regeneration needed - the C++ investigation was
  read-only.

## Verification

No behavior changed. To confirm the state described above
independently: `swift test` in `swift/AtriumUI` should show 2070 passing
tests, and `git log --oneline c03aee5 -1` should show the reentrancy-fix
commit as reachable from this branch's `main` ancestor.
