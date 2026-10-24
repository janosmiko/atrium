# Vim mode: the page core and what rides on it

Design written for V1 and extended as each stage lands. V1
delivered the injection plumbing, the mode machine, scroll motions, the mode
indicator and the settings toggle. V2 added link hints and in-page find, and
V3 the browser verbs. Atrium's own surfaces and media control follow in V4
and V5, planned in `docs/vim-mode-v4-plan.md` and `docs/vim-mode-v5-plan.md`.

## Why this design differs from the task's recorded plan

The earlier plan chose a bundled content-script extension and listed
"content-script loading unproven" as risk R1, with an S0 GO/NO-GO stage to
retire it. Two facts in the current checkout change that.

`AtriumBrowserWindow::PreHandleKeyboardEvent` (`atrium_browser_window.cc`)
returns `NOT_HANDLED` for every event. It steals nothing. The plan's reason
for rejecting native key capture does not hold.

Marginalia already ships the seam the plan calls unproven. `atrium_marginalia_script.cc`
builds a JS library, `AtriumBrowserWindow::ReanchorMarginaliaOnActiveTab`
injects it with `ExecuteJavaScriptInIsolatedWorld` into
`ISOLATED_WORLD_ID_CHROME_INTERNAL` on `DocumentOnLoadCompletedInPrimaryMainFrame`,
and `DrainMarginaliaMarkClicks` collects queued page events on
`DidGetUserInteraction`. Vim mode reuses that seam, so it needs no extension.
Extension command dispatch is dead on chromeless windows. That does not
affect this design, because no extension is involved.

## Architecture

Two halves, one channel between them.

The **page half** is `vim-page.js`, injected into the isolated world of every
frame. It owns mode detection, verb execution, and the overlays. It knows
whether focus sits in an editable element, so it knows the mode.

The **browser half** is `PreHandleKeyboardEvent`. It consumes a key when vim
mode is on and the last mode pushed from the page is NORMAL, then forwards
the key to the page half for execution.

The browser half exists for one reason. A capture-phase listener registered by
the page half cannot beat a listener that a site registered earlier, and the
sites people want vim on - Gmail, GitHub, Notion - all register early.
`PreHandleKeyboardEvent` runs before the renderer sees the event, so no page
listener can outrun it.

The page half decides, the browser half enforces. Neither guesses what the
other knows.

## Components

| Unit | Lives in | Owns |
|---|---|---|
| `vim-page.js` | `overlay/chrome/browser/resources/atrium/` | mode detection, key grammar, scroll verbs, link hints, indicator DOM |
| `atrium_vim_script.{h,cc}` | `overlay/chrome/browser/ui/atrium/` | loads the JS resource, builds the injection and forward scripts |
| `atrium_vim_mode.{h,cc}` | `overlay/chrome/browser/ui/atrium/` | the browser-side decision: given vim-on, mode, and a key, steal or pass |
| settings entry | `rust/atrium-core/src/settings.rs` | the persisted `vim_mode` flag |

`vim-page.js` is a plain script, not an ES module. Injection evaluates a
string, so it cannot carry `import`. It assigns `window.__atriumVim` under a
guard, the way the marginalia library does.

The JS lives in a resource file rather than a C++ raw string literal. The
repo carries over a hundred node suites and `build/scripts/dom-shim.mjs`, so
a real file gets real unit tests. The marginalia library is small enough to
live inside `atrium_marginalia_script.cc`. A vim grammar is not.

`atrium_vim_mode.cc` holds no AppKit and no `views::`. It takes values and
returns an answer, matching the posture of `atrium_surface_key_route.h` and
the core's `commands.rs`. That is what makes the steal decision unit-testable
without a browser.

## Data flow

Injection, per frame, on navigation:

1. The window injects `vim-page.js` into the isolated world of every frame
   through `ForEachRenderFrameHost`, not `GetPrimaryMainFrame`. Marginalia
   only reaches the main frame today, so cross-origin iframes are invisible
   to it. Vim mode must scroll and later hint inside them.
2. The script installs its state, reads the current focus, and pushes its
   mode.

Mode push, page to browser:

3. `vim-page.js` listens for `focusin` and `focusout` and recomputes NORMAL
   against INSERT from the focused element.
4. It queues the mode, and the browser drains it. V1 reuses the marginalia
   drain trigger: the window already observes input through
   `DidGetUserInteraction` and `OnInputEvent`. That trigger covers a click
   into a field and a Tab into one. It does not cover programmatic focus, and
   a site that focuses its own search box on load is the common case. The
   page-side veto below is what makes that case correct, so V1 builds the veto
   rather than treating it as a fallback.

Key handling, browser to page:

5. `PreHandleKeyboardEvent` asks `atrium_vim_mode.cc` whether to steal.
6. On a steal it returns `HANDLED` and runs the verb.
7. On a pass it returns `NOT_HANDLED` and the renderer gets the event
   unchanged, exactly as today.

Most verbs go to `vim-page.js`, which answers with what the browser should do
next rather than merely whether it ran:

| `key()` returns | The browser then |
|---|---|
| `veto` | Puts the character back in the focused field. The cached mode was stale. |
| `handled` | Nothing. The page ran the verb. |
| `copy-url` | Copies the page URL. A clipboard write from an isolated world needs a gesture and fails silently without one. |
| `capture` | Steals every key, not only the grammar, until the page releases. |
| `release` | Returns to normal routing. |
| `next-tab`, `previous-tab` | Cycles the strip. |
| `next-space`, `previous-space` | Cycles the space. |
| `open-command-bar` | Opens the command bar. |
| `close-tab` | Closes the active tab, or parks it if it is a favourite. |

The V3 answers in the lower half of that table are names, not instructions.
The page knows `gt` means "next tab" and knows nothing about tab strips, so
`vim-page.js` has no browser code in it to go stale.

`NativeActionForVimAnswer` maps each name onto the `NativeHotkeyAction` its
cmd-chord already uses, so rebinding the chord moves the vim pair with it and
there is one implementation rather than two. It is a pure function in
`atrium_vim_key_decision.cc` so `unit_tests` can pin all five without a
`Browser`.

`close-tab` is deliberately not in that map. A favourite parks instead of
closing, and only `CloseSidebarTab` knows that, so `x` goes through the same
route the sidebar's own close does. `chrome::CloseTab` would look identical in
a test with no favourite and destroy one in real use.

A verb the browser recognises counts as handled even when the action declines.
A space that will not cycle is still not a character to type into the page.

Three verbs never reach the page at all. `/`, `n` and `N` drive Atrium's own
find bar through `OpenFindBar` and `FindNextMatch`, so there is no search code
in `vim-page.js` to look for. Chrome's FindBar is the one that cannot be used
here: `CreateFindBar` deliberately `CHECK`s, because `FindBarHost`'s
constructor dereferences a null `find_bar_owner()` on a chromeless window.

Hints are allocated by the browser because each frame runs its own instance of
the script and would otherwise start its labels at `a`. `ShowVimHints` asks
every live frame for its target count, then hands each a disjoint slice of one
sequence. A generation counter drops replies from an earlier pass, so a second
`f` cannot interleave with the first and give two frames the same offset. The
label width comes from the cross-frame total, never the local count: one frame
using `a` while another used `aa` would make `a` both a label and a prefix.

## Atrium's own surfaces

V4 gives the same grammar to Atrium's WebUI documents. The two consumers load
code differently and cannot share a module: the injected path evaluates a
string, so it can carry no `import`, while the surfaces load ES modules.

`vim-grammar.js` resolves that by being a plain script that publishes
`globalThis.__atriumVimGrammar`. The injector concatenates it ahead of
`vim-page.js`, and `atrium.html` loads the same file as a classic script
before the module bundle. One definition of the pairs, the browser verbs and
`isEditable`.

Grammar first in that concatenation. `vim-page.js` reads the global, so the
other order leaves every page with no vim and nothing in the log to say why.
`InstallsInTheMainFrame` and `JScrollsARealPage` both fail if it is reversed,
which is why the order is pinned by tests rather than by this paragraph.

`dom-shim.mjs` evaluates the classic scripts too. A shim that loaded only the
module graph was modelling a page that does not exist, and made seven
unrelated suites fail at once.

Each consumer brings its own verb table. The grammar decides what a key means,
the consumer decides how to do it: a page scrolls the window, a list surface
moves a cursor, a reading surface scrolls its own container.

A key the grammar has no verb, pair or name for answers `null` rather than
`handled`. The page can swallow it, because the browser only forwards a
character after deciding to steal it. A surface cannot: it listens to every
key, and the AI panel's open hotkey is a bare printable character.

### Which surfaces have it

| Surface | Vim | Why |
|---|---|---|
| Sidebar | motion and activate | A list of rows, and it binds no printable key of its own |
| AI panel | motion only | One long answer with no rows, so `j`/`k` scroll it. `ai-panel.js` installs and disposes the listener, so the AI kill switch takes it too |
| Settings | motion and activate | Reads `vimModeEnabled` off the settings push it already receives, rather than a second route for the same bit |
| Command bar | no | Its field holds focus whenever the bar is open, so the grammar yields on every key anyway |

The command bar keeps that answer. Its input is focused for the bar's whole
life, `isEditable` is true the entire time, and vim yields on every key. Wiring
it would add a listener that can never fire.

Settings opens with a rail button focused, and a button consumes no printable
key. It passes `yieldsToEditableOnly`, so vim there yields to a text field and
nothing else. The default rule - yield to anything focused - stays for the
sidebar, where a focused control really is a rename in progress.

The sidebar's own keys survive untouched because it has none to lose. Every
listener across `sidebar-menu.js`, `sidebar-nav-row.js`, `row-rename.js` and
`hint-triggers.js` gates on Enter, Escape, or a bare modifier. The one real
conflict is a focused control - a rename field, a menu label - and vim yields
to it through the same `isEditable` the page uses.

## Media verbs

`p` toggles play, `,` and `.` seek five seconds, `<` and `>` halve and double
the rate. They act on the focused player if there is one, otherwise on the
largest visible `video` or `audio` on the page.

"Largest visible" needs saying because an advert and a muted hero loop are
both videos, and neither is the one the reader means. The rule reuses V2's
`visible()`, so a player with no box is never chosen.

Space is deliberately not a media key. It scrolls, on every page, and taking
it would break that everywhere to serve the pages that have a video.

`,` `.` `<` `>` are taken from the page even on sites that bind them. YouTube
uses them for frame stepping and speed, and vim mode overrides that while the
toggle is on. Decided rather than overlooked: `j` and `k` already override
whatever a site does with them, and a grammar that yielded per site would be
unpredictable in a way a consistent one is not. A user who wants the site's
keys back turns vim off.

Hints over a site's own player needed no new code. Custom players draw their
controls as elements with `role="button"`, which the V2 walk already matches.
`HintsReachACustomPlayerButton` pins that, so a later narrowing of the
selector cannot quietly drop them.

## The trust boundary for extension popups

Atrium owns the popup's WebContents through `ExtensionPopupHost`, which is
what makes injection possible. It does not make it obviously right: the
document inside is a third-party extension's own page.

What the injection is limited to:

- Only the popup WebContents `ExtensionPopupHost` itself opened. Never an
  extension's background page, options page, content scripts, or any other
  extension-owned frame.
- Only the isolated world, exactly as page injection works, so the
  extension's own globals are untouched and nothing Atrium defines is visible
  to it.
- Only while vim mode is on. Most users never enable it, and a popup they
  open then has no Atrium script in it at all.
- Nothing is read out. The hints pass reads geometry and dispatches a click.
  Nothing from the popup crosses back to the browser except a target count.

The last point is a promise the code keeps, not something the isolated world
enforces. A future change could read the popup's DOM without any guard
stopping it, so it is the one to check in review.

The popup reads its own keys. Stock `ExtensionViewHost` delegates that
WebContents, so `AtriumBrowserWindow::PreHandleKeyboardEvent` never sees a key
pressed there and has nothing to forward. `vim-popup.js` installs a listener
inside the popup and drives the same `__atriumVim` the page path drives.

It claims `f` and, while labels are up, the label keys and Escape. Nothing
else, because the popup is the extension's own page with its own shortcuts,
and it yields to a focused text field like every other surface. The labels are
allocated from zero rather than as a slice of the page beneath: the reader is
looking at the popup, and two label sequences sharing a keyspace is the bug
the cross-frame total exists to prevent.

## The focus-transition window

Mode travels from page to browser asynchronously. Between the moment focus
enters a text field and the moment the browser learns it, the browser still
believes the mode is NORMAL and will steal a keystroke that belongs in the
field.

For a click or a Tab into a field the window is a single IPC hop, well under
a millisecond, against human key timing two orders of magnitude larger. For a
programmatic focus there is no input event at all, so the browser can stay
stale indefinitely.

The page-side veto closes both. `vim-page.js` re-dispatches a stolen key into
the focused element whenever the browser stole a key the page considers
INSERT-mode, and pushes the corrected mode in the same reply. The user loses
nothing, and the browser is correct from the second keystroke on.

V1 pins both paths: a browsertest that clicks into an input and types
immediately, and one that loads a page which focuses its own field on load
and types without ever clicking. Both assert the characters land in the
field.

## Injection timing

Vim installs from `PrimaryMainDocumentElementAvailable`, which fires when the
main HTML resource finishes parsing. Marginalia's
`DocumentOnLoadCompletedInPrimaryMainFrame` waits for onload and is the hook
this replaced. Chromium's true document-start path belongs to the extensions
user-script layer, which this design does not use.

What was measured, on a page that parses at once but whose onload waits three
seconds on a slow image:

| Hook | `document.readyState` when vim goes live |
|---|---|
| `PrimaryMainDocumentElementAvailable` | short of `complete` |
| `DocumentOnLoadCompletedInPrimaryMainFrame` | `complete` |

So the gap is the whole tail of the page load, not a few milliseconds. On a
page with slow subresources that is seconds during which a verb would have
done nothing.

`VimIsLiveBeforeOnload` pins the choice. Moving the install back to the onload
hook fails it on its own assertion, so a future change cannot quietly give the
delay back. The full suite also ran three times at the parse-time hook with
all ten cases green each time. Thirty executions is a thin sample for a
timing-sensitive hook, so treat that as no evidence of flake rather than as
proof of none.

The choice does not change the architecture. The browser half already wins the
key race, so the hook costs nothing for key capture. It decides how soon after
a navigation vim verbs become live, and it is what V2's hint enumeration
inherits.

Both hooks skip a Back/Forward Cache restore. A page restored from BFCache
keeps the script it was injected with, so vim survives, but anything that
needs a fresh push on every appearance cannot ride this observer.

## Degraded dylib

Scroll verbs, mode detection, and the indicator run entirely in the page.
They do not touch `atrium-core`, so they keep working when the dylib is
degraded. The settings toggle reads through the core like every other
setting, and when the core is unavailable vim mode stays off, which is the
default.

## Testing

Node suites, in `build/scripts/vim-page-test.mjs`, against `dom-shim.mjs`:
the key grammar, editable detection across `input`, `textarea`, and
`contenteditable`, each scroll verb's computed delta, the hint labels, and the
V3 pair tables. `dom-shim.mjs` grew `attachShadow` for V2, and a closed root
reads as `null` there exactly as it does in a real page, so a walk that never
recursed could not pass its own test.

C++ unit tests, in `atrium_vim_key_decision_unittest.cc`: the steal decision
across vim-off, NORMAL, INSERT, modifier chords, a key with no character, hint
capture, and the native action each browser verb names.

Browsertests, in `atrium_vim_mode_browsertest.cc`: injection reaches a
cross-origin subframe, `j` scrolls a real page, typing into a focused input is
not stolen, the toggle turns the whole thing off, hints reach an open shadow
root, find routes to Atrium's own bar, and each V3 verb moves the thing it
names.

Every browsertest waits for load before reading the page. A missing
`WaitForLoadStop` in this repo fails as a JS TypeError and reads as a product
defect.

## Still not built

Per-site rules and the help overlay have no stage and nobody has asked for
them. Atrium's own surfaces, extension popups and media control are V4 and V5,
each with a plan of its own.
