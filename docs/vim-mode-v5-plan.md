# Vim mode V5: media and extension popups

Implementation plan for V5. Two reaches that both rest on V2's hint
enumerator. They share nothing else, so they are independent and either can
land first.

Read `docs/vim-mode-design.md` first.

## The two halves

| Half | What it needs | Risk |
|---|---|---|
| Media verbs and player hints | A verb table over `<video>`, and the existing element walk | Low. The walk already finds a player's own buttons. |
| Extension popup hints | Injecting `vim-page.js` into a WebContents Atrium owns but does not author | This is the whole task. See below. |

## The trust boundary

The boundary must be stated before it ships, so this is the first
decision, not the last.

Atrium owns the extension popup's WebContents through `ExtensionPopupHost`,
which reuses stock `ExtensionPopup::ShowPopup`. Owning it makes injection
possible. It does not make it obviously right: the document inside is a
third-party extension's own page, and Atrium would be running script in it.

**The boundary this plan accepts:**

- Inject only into the popup WebContents `ExtensionPopupHost` itself opened.
  Never into an extension's background page, its options page, its content
  scripts, or any other extension-owned frame.
- Inject into the isolated world, exactly as page injection does, so the
  extension's own globals are untouched and nothing Atrium defines is visible
  to it.
- Inject only while vim mode is on. Most users never enable it, and a popup
  they open has no Atrium script in it at all.
- Read nothing out. The hints pass reads geometry and dispatches a click. It
  does not read the popup's text, form values, or storage, and nothing from
  the popup crosses back to the browser except a target count.

Write that list into `docs/vim-mode-design.md` as a section of its own, not as
a comment, and say plainly that the last point is a promise the code keeps
rather than something the isolated world enforces.

If that boundary is not acceptable, the honest outcome is to drop the feature rather
than narrow it into something that looks safe. Say so in the task instead of
shipping a half-injection.

## Task 1: Media verbs

**Files:**
- Modify: `overlay/chrome/browser/resources/atrium/vim-page.js`
- Modify: `build/scripts/vim-page-test.mjs`
- Modify: `build/scripts/dom-shim.mjs`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_vim_key_decision.cc`

- [ ] **Step 1: Write the failing tests**

The shim has no media element, so it gains one first - a `<video>` whose
`play`, `pause` and `currentTime` are recorded rather than real:

```js
test('the space verb toggles the primary video', () => { ... });

// The reason "primary" needs a definition at all: an ad iframe and a hero
// loop are both videos, and neither is the one the user means.
test('a hidden or zero-sized video is not the primary one', () => { ... });

// Focused OR primary, so the focused one wins when there is one.
test('a focused video beats the primary one', () => { ... });
```

- [ ] **Step 2: Run to verify they fail**

- [ ] **Step 3: Pick the verbs and the primary rule**

Verbs stay small and vim-shaped. Proposal, to be confirmed against the
existing grammar before coding: `space` toggles play, `,` and `.` seek back
and forward, `<` and `>` change rate. Anything already in the grammar wins and
the media meaning moves.

"Primary" is the largest visible video by painted area, with `visible()` from
V2 doing the filtering. That rule is one line and is why the second test
above exists.

- [ ] **Step 4: Add the keys to `kGrammar`**

A key the browser does not steal never reaches the page, so the grammar list
and the verb table have to move together. This is the step most likely to be
forgotten, and `TheV5GrammarIsStolen` is what catches it.

- [ ] **Step 5: Run, mutate, commit**

Mutation: make the primary rule pick the first video rather than the largest.
It compiles, and `a hidden or zero-sized video is not the primary one` must go
red by name.

## Task 2: Hints over a site's own player

Custom players draw their controls as `div`s with `role="button"`, which
`CLICKABLE` in `vim-page.js` already matches. So this task is mostly proving
that rather than building it - and if it turns out to already work, the
deliverable is the test and a line in the design doc, not new code.

- [ ] **Step 1: Write a browsertest over a custom-player fixture**

A page with a `<video>` and a `div[role="button"]` control bar. Assert `f`
labels the control, not only the video.

- [ ] **Step 2: Run it**

If it passes with no source change, say so in the commit message rather than
inventing a change to justify the task.

- [ ] **Step 3: Fix only what the test actually finds**

The likely finding is `visible()`: a control bar that fades out on idle has
`opacity: 0` but a real box, so it is "visible" by the V2 rule and gets a
label pointing at something the user cannot see.

- [ ] **Step 4: Mutate, commit**

## Task 3: Extension popup hints - SPLIT OUT

Moved to its own task. The plan above assumed the popup could reuse the page's
key path, and it cannot.

`ExtensionPopupHost` has no keyboard handling, and the popup's WebContents is
delegated by stock `ExtensionViewHost` rather than by `AtriumBrowserWindow`.
So `PreHandleKeyboardEvent` never sees a key pressed while the popup has
focus, and the whole page model - the browser steals a key, then forwards the
character to the injected script - has no way in.

Injecting is still easy. Routing a keystroke to what was injected is the task,
and it needs a design decision rather than more wiring. The trust boundary
below was written and is recorded in `docs/vim-mode-design.md`, so the next
person starts from a settled boundary rather than reopening it.

The steps that follow are kept for that task to pick up.

**Files:**
- Modify: `overlay/chrome/browser/ui/atrium/atrium_extension_popup_host.cc`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_browser_window.cc`
- Modify: `docs/vim-mode-design.md`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_vim_mode_browsertest.cc`

- [ ] **Step 1: Write the trust-boundary section in the design doc**

- [ ] **Step 2: Write the failing browsertest**

```cpp
// The popup is a WebContents Atrium owns but did not author, so this is
// also the test that pins how far the injection reaches.
IN_PROC_BROWSER_TEST_F(..., FHintsInsideAnExtensionPopup) { ... }

// The boundary, asserted rather than documented: vim off means no script in
// the popup at all.
IN_PROC_BROWSER_TEST_F(..., AnExtensionPopupHasNoVimScriptWithVimOff) { ... }
```

- [ ] **Step 3: Run to verify they fail**

- [ ] **Step 4: Inject at the point the popup is shown**

`OnPopupShown` already exists as the genuine-show callback and disarms the
hang watchdog, so it is the one place that knows a popup is real and painted.
Install there rather than at open, and only when vim mode is on.

- [ ] **Step 5: Include the popup in the hint allocation**

`ShowVimHints` walks live frames of the active tab. The popup is a separate
WebContents, so it is invisible to that walk and would start its labels at
`a` alongside the page. Either add it to the same pass or give it its own,
but not both - two label sequences sharing a keyspace is the V2 bug that the
cross-frame total was built to avoid.

- [ ] **Step 6: Run, mutate, commit**

Mutation: drop the vim-enabled check at the install point. The
vim-off test must go red by name, which is what makes the boundary a
guarantee rather than a paragraph.

## Task 4: Documentation and the whole gate

- [ ] **Step 1: Update `docs/vim-mode-design.md`**

The media verb table, the primary-video rule, and the trust boundary. The
boundary is the part a reader will come looking for.

- [ ] **Step 2: Run every node suite and the whole vim browsertest file**

Not the suites whose names look related. A behaviour can be pinned by the
shape of a message rather than by any word worth grepping for.

- [ ] **Step 3: Commit**

## Notes carried from V1 to V4

- `atrium_browser_window.cc` is shared with other lanes. Commit by hunk, and
  announce before committing, before syncing, and before building.
- Judge a build by its log text, never its exit code.
- A mutation has to compile to prove anything.
- The vim indicator once painted on every page because install-time paint was
  pinned as correct by its own test. A new install point deserves the same
  suspicion.
