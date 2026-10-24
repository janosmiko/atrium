# Vim mode V2: page targets

Implementation plan for V2. V1 built the injection, the
mode, the steal decision and the scroll verbs. V2 adds the verbs that have to
find things in the page: `f` and `F` link hints, in-page find with `/ n N`, and
`yy` to copy the URL.

Read `docs/vim-mode-design.md` first. This plan assumes V1's architecture and
changes one part of it, the page-to-browser return protocol, in Task 1.

## What V1 leaves in place

- `vim-page.js` is injected into the isolated world of **every frame** on
  `PrimaryMainDocumentElementAvailable`. Each frame gets its own instance.
- The browser enforces the steal in `PreHandleKeyboardEvent`, the page decides
  and can veto. `atrium::RouteVimKey` holds the pure rule.
- `kGrammar` in `atrium_vim_key_decision.cc` is the browser's list of
  characters worth stealing. V2 extends it.
- `AtriumBrowserWindow::SendVimKey` forwards one character and reads the
  result in `OnVimKeyRan`.

## The one architectural change

V1's `key()` returns a bool: `false` means "I am in INSERT, give the key
back", `true` means "handled". V2 needs a third answer, because `yy` is done
by the browser and hint mode changes how later keys route.

`key()` returns a **string** in V2:

| Return | Meaning |
|---|---|
| `"veto"` | INSERT mode. The browser re-dispatches the character into the field. |
| `"handled"` | The page ran a verb. Nothing further. |
| `"copy-url"` | The page recognised `yy`. The browser owns the clipboard. |
| `"capture"` | The page entered a mode that wants every key (hints, find). |
| `"release"` | That mode ended. Normal routing resumes. |

`OnVimKeyRan` keeps accepting a bool for one release so a stale pak cannot
break the veto, and treats `false` and `"veto"` identically.

**Why the browser copies the URL.** `navigator.clipboard.writeText` in an
isolated world needs a focused document and a user gesture, and fails silently
when either is missing. The browser already knows
`GetLastCommittedURL()` and has an unconditional clipboard. Routing `yy` to the
browser removes a permission surface rather than working around one.

**Why `capture` exists.** While hints are up, or while a find query is being
typed, the grammar is not the grammar any more: `j` is a hint label, not a
scroll. Rather than teach `RouteVimKey` about modes, the page says "send me
everything" and the browser widens the steal until it hears `release`.

---

## Task 1: The return protocol

**Files:**
- Modify: `overlay/chrome/browser/resources/atrium/vim-page.js`
- Modify: `build/scripts/vim-page-test.mjs`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_browser_window.cc`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_browser_window.h`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_vim_key_decision.{h,cc}`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_vim_key_decision_unittest.cc`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_vim_mode_browsertest.cc`

- [ ] **Step 1: Write the failing tests**

In `vim-page-test.mjs`, change every `key()` assertion from `true`/`false` to
`'handled'`/`'veto'`, then add:

```js
test('yy asks the browser to copy and a single y does not', () => {
  const {vim} = loadVim();
  assert.equal(vim.key('y'), 'handled');
  assert.equal(vim.key('y'), 'copy-url');
});

test('a y followed by another verb drops the prefix', () => {
  const {vim} = loadVim();
  assert.equal(vim.key('y'), 'handled');
  assert.equal(vim.key('j'), 'handled');
  assert.equal(vim.key('y'), 'handled');
});
```

In `atrium_vim_key_decision_unittest.cc`, add a case per new grammar key and
one for the capture widening:

```cpp
TEST(AtriumVimKeyDecisionTest, CaptureStealsAKeyOutsideTheGrammar) {
  VimKeyInputs in = NormalMode();
  in.character = "q";
  EXPECT_EQ(VimKeyRoute::kPassToPage, RouteVimKey(in));
  in.page_capturing = true;
  EXPECT_EQ(VimKeyRoute::kStealForVim, RouteVimKey(in));
}

// Capture must not survive a modifier: Cmd+W has to still close the tab.
TEST(AtriumVimKeyDecisionTest, CaptureDoesNotStealAChord) {
  VimKeyInputs in = NormalMode();
  in.page_capturing = true;
  in.command = true;
  in.character = "w";
  EXPECT_EQ(VimKeyRoute::kPassToPage, RouteVimKey(in));
}
```

- [ ] **Step 2: Run to verify they fail**

```bash
node build/scripts/vim-page-test.mjs
make overlay-sync && ./build/scripts/build-shell.sh unit_tests
~/chromium-atrium/src/out/atrium/unit_tests "--gtest_filter=AtriumVimKeyDecisionTest.*"
```

- [ ] **Step 3: Widen the decision**

Add `bool page_capturing = false;` to `VimKeyInputs`, `"y"` and `"f"`, `"F"`,
`"/"`, `"n"`, `"N"` to `kGrammar`, and one branch to `RouteVimKey`, after the
modifier check so a chord still escapes:

```cpp
  if (in.page_capturing) {
    return VimKeyRoute::kStealForVim;
  }
```

The placement is load-bearing. It sits below the INSERT check, so capture can
never steal from a focused text field. That is what lets Task 5's find field
and any page's own input keep their typing while hints are the only thing
capture serves.

- [ ] **Step 4: Change the page's answers**

`key()` returns the strings above. Add `pendingY` beside `pendingG`, and reset
both on any non-matching verb - V1's `gg` test already pins that shape.

- [ ] **Step 5: Teach the browser the new answers**

In `OnVimKeyRan`, replace the bool test:

```cpp
  const std::string answer =
      ran.is_string() ? ran.GetString() : (ran.is_bool() && ran.GetBool()
                                               ? "handled" : "veto");
  if (answer == "handled") {
    return;
  }
  if (answer == "copy-url") {
    CopyActiveTabUrl();
    return;
  }
  if (answer == "capture" || answer == "release") {
    CurrentVimPageCapturing() = answer == "capture";
    return;
  }
  // "veto" falls through to V1's re-dispatch.
```

`CopyActiveTabUrl` writes `GetLastCommittedURL().spec()` through
`ui::ScopedClipboardWriter`. Follow whatever the address bar's copy path
already uses rather than adding a second idiom.

- [ ] **Step 6: Run, then mutate**

Both suites green, then delete the `page_capturing` branch and confirm
`CaptureStealsAKeyOutsideTheGrammar` fails by name.

- [ ] **Step 7: Commit**

```bash
git commit -m "feat(vim): let the page ask the browser for a clipboard write"
```

---

## Task 2: The element walk

Hints need every clickable element in a frame, including inside **open** shadow
roots. Closed shadow roots are unreachable from any world, including ours, and
the plan does not pretend otherwise - it tests that fact instead.

**Files:**
- Modify: `overlay/chrome/browser/resources/atrium/vim-page.js`
- Modify: `build/scripts/vim-page-test.mjs`
- Modify: `build/scripts/dom-shim.mjs`

- [ ] **Step 1: Write the failing tests**

The shim needs `attachShadow` and `shadowRoot` before these can run. Add the
smallest version that models a real open root: a node with its own children
that `querySelectorAll` on the host does **not** reach. A shim where the host
search already finds shadow children would make the walk untestable, because
the naive implementation would pass.

```js
test('the walk finds a link inside an open shadow root', () => {
  const {vim} = loadVim();
  const host = globalThis.document.createElement('div');
  globalThis.document.body.appendChild(host);
  const root = host.attachShadow({mode: 'open'});
  root.appendChild(makeAnchor('/deep'));
  assert.equal(vim.clickableTargets().length, 1);
});

test('a closed shadow root is not reachable and does not throw', () => {
  const {vim} = loadVim();
  const host = globalThis.document.createElement('div');
  globalThis.document.body.appendChild(host);
  host.attachShadow({mode: 'closed'});
  assert.deepEqual(vim.clickableTargets(), []);
});

// A hint over an element nobody can see is a label that does nothing.
test('the walk skips hidden and zero-size elements', () => {
  ...
});
```

- [ ] **Step 2: Run to verify they fail**

- [ ] **Step 3: Write the walk**

```js
  var CLICKABLE = 'a[href],button,input,select,textarea,summary,' +
      '[role=button],[role=link],[onclick],[tabindex]:not([tabindex="-1"])';

  function visible(el) {
    if (!el.getBoundingClientRect) return false;
    var r = el.getBoundingClientRect();
    if (r.width <= 0 || r.height <= 0) return false;
    if (r.bottom < 0 || r.top > (window.innerHeight || 0)) return false;
    var style = window.getComputedStyle ? getComputedStyle(el) : null;
    if (style && (style.visibility === 'hidden' ||
                  style.display === 'none')) {
      return false;
    }
    return true;
  }

  // Only open roots have a shadowRoot property. A closed one reads null here,
  // which is the same answer any other world would get.
  function walk(root, out) {
    var all = root.querySelectorAll ? root.querySelectorAll('*') : [];
    for (var i = 0; i < all.length; i++) {
      var el = all[i];
      if (el.matches && el.matches(CLICKABLE) && visible(el)) out.push(el);
      if (el.shadowRoot) walk(el.shadowRoot, out);
    }
    return out;
  }

  function clickableTargets() {
    return walk(document, []);
  }
```

`querySelectorAll('*')` over a large page is the cost here. It runs once per
`f`, not per keystroke, and a page big enough to matter is rare enough to wait
for a measurement. `ponytail: full walk per f, switch to a TreeWalker with a
NodeFilter if a real page measures badly.`

**Extension-drawn DOM needs no code.** A content script writes into the
same DOM tree this walks; isolated worlds share the DOM and differ only in
script state. The browsertest in Task 4 proves it rather than the walk
assuming it.

- [ ] **Step 4: Run to verify they pass, then mutate**

Drop the `if (el.shadowRoot) walk(...)` line and confirm the open-root test
fails by name.

- [ ] **Step 5: Commit**

---

## Task 3: Labels and activation

**Files:**
- Modify: `overlay/chrome/browser/resources/atrium/vim-page.js`
- Modify: `build/scripts/vim-page-test.mjs`

Labels come from a home-row alphabet so they are typed without moving the
hand. Two frames must not both paint `a`, so the label alphabet is not enough
on its own - Task 4 gives each frame a disjoint range.

- [ ] **Step 1: Write the failing tests**

```js
test('labels are unique, and grow a second character only when needed', () => {
  assert.deepEqual(labelsFor(3, 0), ['a', 's', 'd']);
  assert.equal(new Set(labelsFor(200, 0)).size, 200);
  assert.ok(labelsFor(200, 0).every((l) => l.length <= 2));
});

// Frame two must not paint a label frame one already owns.
test('an offset shifts the alphabet without repeating', () => {
  const first = labelsFor(5, 0);
  const second = labelsFor(5, 5);
  assert.equal(first.filter((l) => second.includes(l)).length, 0);
});

test('typing a prefix narrows, and a full match clicks once', () => {
  const {vim, clicks} = loadVimWithLinks(['/a', '/b']);
  vim.key('f');
  assert.equal(vim.key('a'), 'handled');
  assert.deepEqual(clicks, ['/a']);
  assert.equal(vim.key('x'), 'release');
});

// F opens in a new tab, so the click has to carry the modifier.
test('F activates with a meta click', () => { ... });

test('escape leaves hint mode and paints nothing', () => { ... });
```

- [ ] **Step 2-4: Implement, run, mutate**

`f` collects targets, paints an absolutely positioned label per target, and
returns `"capture"`. Each subsequent key narrows. A unique match clicks and
returns `"release"`. Escape or no match returns `"release"`.

Labels are painted into the same container the V1 indicator uses, so one
teardown path clears both. `hide()` must clear hints too - V1's toggle-off
test already proves the indicator goes, and hints need the same guarantee, so
extend that test rather than writing a parallel one.

Mutation bar: make a full match not clear hint state, and confirm the "clicks
once" test fails by name.

- [ ] **Step 5: Commit**

---

## Task 4: Hints across frames

Each frame runs its own instance, so two frames would both start at `a`. The
browser is the only thing that sees all the frames, so it allocates.

**Files:**
- Modify: `overlay/chrome/browser/ui/atrium/atrium_vim_script.{h,cc}`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_browser_window.{h,cc}`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_vim_mode_browsertest.cc`

- [ ] **Step 1: Write the failing browsertests**

```cpp
// Two frames both starting at 'a' is the bug this prevents.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, HintLabelsDoNotRepeatAcrossFrames)

// Open shadow roots, in a real renderer rather than the shim.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, HintsReachAnOpenShadowRoot)

// Content scripts draw into the same tree, and this is the proof.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, HintsReachScriptInjectedDom)
```

For extension-drawn DOM, append the element with `ExecJs` in the **main** world and assert the
isolated-world walk still finds it. That is the property an extension relies
on. Note in the test why a main-world append models a content script.

Reuse V1's `ServeSlowLoadingPage` shape for any markup no fixture provides,
rather than adding files to `content/test/data`.

- [ ] **Step 2: Run to verify they fail**

- [ ] **Step 3: Allocate in the browser**

`ShowVimHints()` runs a two-phase pass over `ForEachRenderFrameHost`:

1. Ask each live frame for its target count.
2. When every reply is in, send each frame its label offset.

Frames reply asynchronously and one can die mid-pass, so the collector holds a
`base::WeakPtr` and a generation counter. A reply from an earlier generation is
dropped rather than merged - otherwise a fast second `f` interleaves with the
first and two frames get the same offset.

- [ ] **Step 4: Run to verify they pass**

- [ ] **Step 5: Prove the tests can fail**

Force every frame's offset to 0 and confirm
`HintLabelsDoNotRepeatAcrossFrames` fails by name. This is the mutation that
matters in this task: a single-frame page passes either way, so without it the
cross-frame test could be green by construction.

- [ ] **Step 6: Commit**

---

## Task 5: Find, by driving the bar Atrium already has

**Atrium has its own find bar and this task does not build a second one.**
`AtriumBrowserWindow` already exposes `OpenFindBar()`,
`FindNextMatch(bool forward)` and `CloseFindBar()`, backed by
`find_bar_widget_`, answering IDC_FIND / IDC_FIND_NEXT / IDC_FIND_PREVIOUS,
and covered by `atrium_find_bar_browsertest.cc`. Those three calls are exactly
what `/`, `n` and `N` mean.

The task description says find must not drive **Chrome's** FindBar, and that
holds: `CreateFindBar` deliberately `CHECK`s, because `FindBarHost`'s
constructor dereferences a null `find_bar_owner()` on a chromeless window.
Atrium's own bar is a different surface and is the one to use.

This is the whole task. It is three lines of routing, not a search engine.

**Files:**
- Modify: `overlay/chrome/browser/ui/atrium/atrium_browser_window.cc`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_vim_mode_browsertest.cc`

- [ ] **Step 1: Write the failing browsertests**

```cpp
// / reaches Atrium's bar, and never Chrome's, which CHECKs here.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, SlashOpensTheAtriumFindBar)

// N and N step the existing session rather than reopening it.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, NStepsTheFindSession)

// A find field is a text field, so vim must not eat what is typed into it.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, TypingInTheFindBarIsNotStolen)
```

The third is the one that earns its place. The bar is a separate WebContents,
so `PreHandleKeyboardEvent` on the page's contents is not what receives those
keystrokes - assert the behaviour rather than reasoning about which delegate
fires, because that is the part most likely to be wrong.

- [ ] **Step 2: Run to verify they fail**

- [ ] **Step 3: Route the three keys**

`/`, `n` and `N` are browser verbs, not page verbs: they never reach
`vim-page.js`. Handle them in `PreHandleKeyboardEvent` after `RouteVimKey`
says steal, before `SendVimKey`:

```cpp
  if (in.character == "/") {
    OpenFindBar();
    return content::KeyboardEventProcessingResult::HANDLED;
  }
  if (in.character == "n" || in.character == "N") {
    FindNextMatch(/*forward=*/in.character == "n");
    return content::KeyboardEventProcessingResult::HANDLED;
  }
```

No `capture` is needed for find. The bar takes focus itself, so its own
keystrokes never arrive here.

- [ ] **Step 4: Run to verify they pass**

- [ ] **Step 5: Prove they can fail**

Point `/` at `CloseFindBar()` and confirm `SlashOpensTheAtriumFindBar` fails by
name. A test that only asserts "no crash" would survive that mutation, which
is the reason it asserts the bar is open instead.

- [ ] **Step 6: Commit**

---

## Task 6: Documentation and the whole gate

- [ ] **Step 1: Update `docs/vim-mode-design.md`**

The grammar table gains `f F / n N yy`. Record that `/ n N` drive Atrium's own
find bar rather than anything in the page, so the next reader does not go
looking for search code in `vim-page.js`. Delete any V1 sentence the protocol
change makes wrong - a doc that still describes `key()` returning a bool is
wrong, not merely out of date.

- [ ] **Step 2: Run everything**

```bash
node build/scripts/vim-page-test.mjs
make webui-test && make overlay-verify && make rust-verify
./build/scripts/build-shell.sh unit_tests && \
  ~/chromium-atrium/src/out/atrium/unit_tests "--gtest_filter=AtriumVim*"
./build/scripts/build-shell.sh browser_tests
make vm-run-gtest BIN=browser_tests FILTER="AtriumVimModeBrowserTest.*"
```

Judge every build by its log text, never its exit code. Reconcile
`RUN - OK - FAILED` on every gtest run: a crash prints neither OK nor FAILED.

- [ ] **Step 3: Commit**

## Notes carried from V1

- `content/test/data` has `scrollable_page.html` but no form fixture. Build
  markup with `ExecJs` or a registered request handler.
- An Atrium test window opens with **no tab**. The first navigation must use
  `chrome::AddAndReturnTabAt`, and `content::NavigateToURL` crashes on the null
  active contents.
- `browser_tests` serves `chrome/test/data`. Anything from `content/test/data`
  needs `ServeFilesFromSourceDirectory`.
- A comment-style hook rejects blocks of four or more lines and semicolons in
  prose.
- `atrium_browser_window.cc` is shared with other lanes. Commit by hunk.
