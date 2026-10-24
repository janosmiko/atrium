# Vim mode V3: browser verbs

Implementation plan for V3. V1 built the injection, mode and scroll
verbs. V2 added hints, find and `yy`. V3 adds the verbs that act on the
browser: `gt` and `gT`, `x`, `o` and `t`, and space navigation.

Read `docs/vim-mode-design.md` first.

## The finding this plan is built on

**None of these verbs need new behaviour. Every one already exists as a native
Atrium operation, and V3 is a routing table.**

`atrium_native_hotkey_defaults.h` declares `NativeHotkeyAction`, 40 actions
covering tab cycling, space navigation, the command bar and more.
`atrium_native_hotkey_map.h` exposes the single dispatch point:

```cpp
bool ExecuteNativeHotkeyAction(BrowserController* controller,
                               NativeHotkeyAction action);
```

Its own comment calls it "the ONE implementation both dispatch levels share".
It returns false when the action's connectivity gate fails, which is exactly
the answer a vim verb needs when the dylib is degraded.

That settles four of the five acceptance criteria by construction rather than
by care. A verb routed through `ExecuteNativeHotkeyAction` moves tabs in the
spaces model's order because that is what `kNextTab` does, and switches spaces
through the native operation because that is what `kNextSpace` does. Writing
tab cycling by hand against the raw `TabStripModel` is exactly what to avoid, and
it is also the only way to get it wrong.

| Verb | Action |
|---|---|
| `gt` | `NativeHotkeyAction::kNextTab` |
| `gT` | `NativeHotkeyAction::kPreviousTab` |
| `o`, `t` | `NativeHotkeyAction::kOpenCommandBar` |
| `]]` | `NativeHotkeyAction::kNextSpace` |
| `[[` | `NativeHotkeyAction::kPreviousSpace` |

`x` is the exception and Task 3 handles it separately.

## What `x` needs instead

There is no `kCloseTab` action, because Atrium's own close is not a hotkey.
The sidebar's close button sends the WebUI message `sidebarTabClosed` with the
row's tab id, handled by `AtriumBrowserWindow::OnSurface_SidebarTabClosed`
(`atrium_browser_window_surface_routes.cc:225`). The goal is "the same native
path as Atrium's own close action", so that handler is the path, not
`BrowserController::RemoveTab`, which is bookkeeping the handler calls.

`x` therefore resolves the active tab's id and runs the same handler the
close button reaches. `atrium_favorite_keep_alive_browsertest.cc:113` shows a
test driving it through `OnSurfaceMessage` directly.

## The one real design question

`gt` and `gT` collide with `gg`. V1 already owns `g` as a prefix, and V2 left
that shape in place. So the prefix machine has to grow from "was the last key
`g`" to "what follows a pending `g`":

| After `g` | Means |
|---|---|
| `g` | scroll to top, V1 |
| `t` | next tab |
| `T` | previous tab |
| anything else | drop the prefix, run that key's own verb |

The same applies to `y`, which V2 made a prefix for `yy`. The two prefixes
already share one `pending` slot, and V2 has a test pinning that a `g` does
not arm `y`. That test is what stops this change breaking `yy`.

`]]` and `[[` are new prefixes with the same shape.

## Task 1: The prefix machine grows a table

**Files:**
- Modify: `overlay/chrome/browser/resources/atrium/vim-page.js`
- Modify: `build/scripts/vim-page-test.mjs`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_vim_key_decision.{h,cc}`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_vim_key_decision_unittest.cc`

- [ ] **Step 1: Write the failing tests**

```js
test('gt and gT ask the browser to cycle tabs', () => {
  const {vim} = loadVim();
  assert.equal(vim.key('g'), 'handled');
  assert.equal(vim.key('t'), 'next-tab');
  assert.equal(vim.key('g'), 'handled');
  assert.equal(vim.key('T'), 'previous-tab');
});

// gg must still scroll. This is the case the table could break.
test('gg still reaches the top after gt exists', () => {
  const {vim, scrolled} = loadVim();
  vim.key('g');
  vim.key('g');
  assert.deepEqual(scrolled, [['to', 0]]);
});

// A g followed by a key with no g-verb drops the prefix and runs that key.
test('gj drops the prefix and scrolls', () => { ... });
```

Plus the grammar cases in the C++ unit suite for `t`, `T`, `x`, `o`, `]`, `[`.

- [ ] **Step 2: Run to verify they fail**

- [ ] **Step 3: Replace the two-key special cases with a table**

V2's `key()` handles `g` and `y` with a hand-written branch each. Three more
prefixes makes that five branches doing one thing, so it becomes data:

```js
  // Second key -> what the pair means. A prefix with no entry for the key
  // that followed drops and lets that key run its own verb.
  var PAIRS = {
    g: {g: 'top', t: 'next-tab', T: 'previous-tab'},
    y: {y: 'copy-url'},
    ']': {']': 'next-space'},
    '[': {'[': 'previous-space'},
  };
```

- [ ] **Step 4: Run, then mutate**

Delete the `g` row's `g` entry and confirm the `gg` test fails by name. That
is the mutation that matters: the risk in this task is a table that serves the
new verbs and quietly drops the old one.

- [ ] **Step 5: Commit**

## Task 2: The browser answers the new verbs

**Files:**
- Modify: `overlay/chrome/browser/ui/atrium/atrium_browser_window.{h,cc}`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_vim_mode_browsertest.cc`

- [ ] **Step 1: Write the failing browsertests**

Each verb asserts the native path ran, not that the tab count
changed. Two tabs and a count is satisfied by any close at all, including one
that bypasses the spaces model, so the assertions are about order and
identity:

```cpp
// Spaces order, not strip order. Set those two apart first, or the test
// passes on a window where they happen to agree.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, GtFollowsTheSpacesOrder)

// The command bar is up afterwards, rather than a tab having navigated.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, OOpensTheCommandBar)

// The space actually changed, read from the spaces model.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, BracketsSwitchSpaces)
```

`GtFollowsTheSpacesOrder` needs a window where the two orders differ. If
arranging that is not possible in a browsertest, say so in the task notes and
assert the weaker property, rather than writing a test whose name claims more
than it checks.

- [ ] **Step 2: Run to verify they fail**

- [ ] **Step 3: Extend `RunVimVerb`**

V2 left `RunVimVerb` as the single place the browser acts on a verb, so the
new answers join `OnVimKeyRan`'s existing switch:

```cpp
  if (answer == "next-tab" || answer == "previous-tab" ||
      answer == "next-space" || answer == "previous-space") {
    RunNativeVimAction(answer);
    return;
  }
```

`RunNativeVimAction` maps the string to a `NativeHotkeyAction` and calls
`ExecuteNativeHotkeyAction` with this window's controller. It must honour the
false return: a degraded dylib means the verb does nothing, and it must not
fall through to some hand-rolled fallback that would work when the real path
does not.

- [ ] **Step 4: Run, mutate one action mapping, commit**

Point `next-tab` at `kPreviousTab` and confirm the cycle test fails by name.

## Task 3: `x` closes the way the close button does

**Files:**
- Modify: `overlay/chrome/browser/ui/atrium/atrium_browser_window.{h,cc}`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_vim_mode_browsertest.cc`

- [ ] **Step 1: Write the failing browsertest**

```cpp
// Not "a tab closed" - the tab the sidebar would have closed, through
// the handler its own button reaches.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, XClosesThroughTheSidebarPath)
```

- [ ] **Step 2: Run to verify it fails**

- [ ] **Step 3: Route `x`**

Resolve the active tab's id and run `OnSurface_SidebarTabClosed`'s path with
it. Do not call `BrowserController::RemoveTab`: that is the bookkeeping the
handler performs, and calling it directly skips whatever else the handler
does, which is the defect to avoid.

A tab with no id is a cold row. `x` on one should do
nothing rather than guess, and the test should say so.

- [ ] **Step 4: Run, mutate, commit**

Mutate the handler call to `RemoveTab` and confirm the test fails. If it does
not, the test is asserting "a tab closed" after all and needs rewriting.

## Task 4: Documentation and the whole gate

- [ ] **Step 1: Update `docs/vim-mode-design.md`**

Add the verb table. State plainly that these verbs dispatch through
`ExecuteNativeHotkeyAction` and that `x` goes through the sidebar close
handler, so the next reader does not look for tab code in `vim-page.js`.

- [ ] **Step 2: Run everything**

```bash
node build/scripts/vim-page-test.mjs
make webui-test && make overlay-verify && make rust-verify
./build/scripts/build-shell.sh unit_tests && \
  ~/chromium-atrium/src/out/atrium/unit_tests "--gtest_filter=AtriumVim*"
./build/scripts/build-shell.sh browser_tests
make vm-run-gtest BIN=browser_tests FILTER="AtriumVimModeBrowserTest.*"
```

Judge every build by its log text, never its exit code: a failed build leaves
the previous binary in place and the VM will happily test it and report green.
Reconcile `RUN - OK - FAILED` on every gtest run.

- [ ] **Step 3: Commit**

## Notes carried from V1 and V2

- `atrium_browser_window.cc` is shared with other lanes. Commit by hunk, and
  say so before committing while another session is staging.
- A mutation must still compile. `if (false && x)` trips
  `-Werror,-Wunreachable-code` and an unused variable trips
  `-Wunused-but-set-variable`, and neither proves anything.
- When a behaviour is pinned in more than one suite, changing it means running
  every suite, not the ones whose names you can guess. A wire format can be
  pinned by its shape rather than by any word you could grep for.
- The hint alphabet is 9 characters, so more than 81 targets on a page runs
  out of two-character labels. Still true, still untested, still not V3's job.
