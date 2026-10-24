# Which chrome surfaces survived the Swift cut

Measured 2026-08-09 morning, on the first genuinely dylib-free build. Every
surface below has since come back. The measurement is kept because it records
what the cut actually cost, and because the "what each one was missing" reading
is what made the rebuilds cheap to plan.

## Then and now

| Surface | At the cut | Now |
|---|---|---|
| sidebar | works | works, from the Rust core |
| pill | module survived, nothing created it | works |
| foyer | module survived, nothing created it | works |
| commandbar | no module, no entry point | works |
| peek | no module, no entry point | works, auto-peek on the Rust core |
| settings | no module, no entry point | works, six panes on the Rust core, one row writable |

## What the cut had taken

The morning launch named all six surfaces on the (since removed) HTML-surfaces
switch.

The app stayed up, no surface reported an error, and no `chrome.send` name was
unregistered. It just had almost no chrome.

**One column caught me out and is worth keeping.** The pill and foyer showed
resource loads - `Loading Resource for "chrome://atrium.top-chrome/pill.css"` -
which looked like the surfaces working. Their stylesheet and script are bundled
into the host page and fetched with it. Neither surface was ever created. I
first recorded them as "loads, undriven", which would have made them look nearly
finished.

The distinction that actually predicted cost was **which half was missing**:

- Pill and foyer had their JS and CSS shipping and lacked only a C++ path that
  creates the surface and feeds it. Those were re-wirings.
- Peek, the command bar and settings had no module at all. Those were rewrites.

The command bar was the sharpest case. `AtriumBrowserWindow::OpenCommandBar`
had 47 external call sites - hotkeys, menus, command dispatch - and its whole
body was `if (command_bar_widget_) { command_bar_widget_->Show(...); }` against
a dead Swift host. The wiring survived intact; only the surface was missing.

## What the rebuilds settled

Three things recur across all five, and each was decided once:

**A surface needs a host, not just a route.** `chrome://atrium.top-chrome/...`
will not load as an ordinary tab - it answers `ERR_INVALID_URL`. A route that
serves is not a surface that shows, and a scenario that opens one as a tab
proves nothing.

**Seams that bypass the IDC intercept need their own hook.** Cmd+, reaches
`prePerformKeyEquivalent` and never `postPerform` on an HTML surface, so it is
claimed on the raw NSEvent. The app menu's Settings item carries an explicit
target and action, so it is retargeted at runtime. Neither goes through
`IDC_OPTIONS`, and un-gating that intercept would have brought back neither.

**The values come from the Rust core.** After the cut the running browser read
no store at all - `atrium_core_query` appeared only in a unit test. The settings
surface now reads three through it: settings, hotkeys, and the spaces document.

## The sidebar, still the worked example

It was the only surface with both halves intact, and it was already served by
the Rust core rather than by Swift. `build/scenarios/rust-core-sidebar.json`
passes with Rust-backed pushes and no dylib loaded. It is what "done" looks
like, and the other five now match it.

## What is still missing

Not a surface, but the honest remainder: **one row in settings is editable**.
General's Warn before quitting sends `settingsChanged` and the window writes it
through `SaveSettingsJSON` to `atrium_core_save_settings` (ABI 8). A plain bool
is the smallest round trip there is, so the write path is proved before the rows
whose controls are ranges or segmented pickers. The whole settings object
travels on every edit, because the core refuses a partial one rather than
completing it from defaults.

R1.2 is the rule this must not break - a store this build cannot read must never
be overwritten with defaults - and the writer enforces it by re-reading the file
first and answering -2. That code reaches the USER, not only the log: the
surface shows a warning and puts the row back. It has to, because the settings
reader answers such a file with defaults rather than refusing it, so the window
shows values the user can edit and a silent refusal would leave them believing
an edit the file never took.

The value survives a relaunch: `build/scenarios/settings-confirm-quit-toggle.json`
stages `confirmToQuitEnabled: false`, toggles it, quits, relaunches against the
same file, and reads `true` back off disk. Still open: every other row is
read-only.

**A row that stores a byte is not a row that does something.** That gap was three
separate bugs. Five keyed rows now reach a consumer -
`confirmToQuitEnabled` to `browser.confirm_to_quit`, `sidebarPosition`,
`opaqueSurround` and `glassOpacity` to the live windows, `startupBehavior` to
`SessionStartupPref` - all off ONE startup read (`SeedFromSettings`) plus the
settings window's save, applied only on a successful write.

The rest say so on screen. Ten rows have no store in this build at all - Hide
sidebar, Notification timeout, Pinned extensions, Default browser, Swipe feel
and the five AI rows - and they read "Not yet wired" in a dimmed row rather than
the em dash they shared with a row whose value has not arrived yet. Two rows
came out entirely, Appearance (light/dark/auto) and Native tint strength: no
store carries either field, so there was nothing to fill in later.
`memorySaverThreshold` is the one left standing, and the STORE half of it is
done: the key now carries a duration only, `memorySaverEnabled` carries the
on/off, and a file spelling the retired `"never"` migrates to off plus the
default duration (R1.3c). Two rows still share the old key and nothing applies
either - the surface has to give each key its own row, and each row its own
local-state pref: `kMemorySaverModeState` and
`kMemorySaverModeTimeBeforeDiscardInMinutes`.
