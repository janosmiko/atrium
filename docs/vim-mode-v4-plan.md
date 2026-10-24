# Vim mode V4: Atrium's own surfaces

Implementation plan for V4. V1 to V3 gave vim mode to web pages. V4
brings it to Atrium's own WebUI documents: the sidebar first, then the others.

Read `docs/vim-mode-design.md` first.

## The problem this task actually has

The grammar must be shared with `vim-page.js` rather than reimplemented.
That is the whole task, and the two consumers cannot load code the same way:

| Consumer | How it loads |
|---|---|
| Page frames | C++ evaluates `vim-page.js` as a **string**, so it can carry no `import` |
| WebUI surfaces | `atrium.html` loads ES **modules** that `import` each other |

A file cannot be both an ES module and a script evaluated as text: `export`
is a syntax error outside a module. So one of three things has to give.

**The plan takes option C.**

| Option | Why not |
|---|---|
| A. Duplicate the grammar in a module | Duplicates the grammar, and the two copies drift the first time a verb is added |
| B. Make the injector strip `export` before evaluating | A regex over source at runtime. Fragile, and it fails silently into a page that has no vim |
| C. One plain script that publishes a global, loaded both ways | Slightly unusual for a WebUI module, and needs the load order to be right |

Under C, `vim-grammar.js` is a plain script assigning
`globalThis.__atriumVimGrammar`. The injected path concatenates it ahead of
`vim-page.js`. The WebUI path lists it in `atrium.html` as a classic script
before the module bundle, and the surface module reads the global.

The load-order risk is real and gets a test rather than a comment: a surface
module that runs before the classic script sees `undefined` and silently has
no grammar, which looks exactly like vim mode being off.

## What moves into the shared file

Only the parts that are pure. Anything touching the DOM stays where it is:

| Moves | Stays |
|---|---|
| `PAIRS`, `BROWSER_VERBS` | `VERBS` (they call `window.scrollBy`) |
| `isEditable` | the indicator, hints, `paintHints` |
| the `key()` prefix machine | `clickableTargets`, `visible`, `walk` |

`isEditable` moves because a sidebar row and a page input have the same
question to answer, and the answer must not be allowed to differ.

The scroll verbs do not move. A surface scrolls its own container, not
`window`, so V4 supplies its own `VERBS` table to the shared machine. That
is the seam: the grammar decides *what* a key means, each consumer decides
*how* to do it.

## Task 1: Extract the grammar

**Files:**
- Create: `overlay/chrome/browser/resources/atrium/vim-grammar.js`
- Modify: `overlay/chrome/browser/resources/atrium/vim-page.js`
- Modify: `overlay/chrome/browser/resources/atrium/atrium_resources.grd`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_vim_script.cc`
- Create: `build/scripts/vim-grammar-test.mjs`
- Modify: `build/scripts/vim-page-test.mjs`

- [ ] **Step 1: Write the failing tests**

`vim-grammar-test.mjs` covers the machine with an injected verb table, so it
tests the grammar and not any one consumer:

```js
test('a prefix pair resolves, and an unknown second key falls through', () => {
  const ran = [];
  const g = makeGrammar({verbs: {j: () => ran.push('j')}});
  assert.equal(g.key('g'), 'handled');
  assert.equal(g.key('t'), 'next-tab');
  assert.equal(g.key('g'), 'handled');
  assert.equal(g.key('j'), 'handled');
  assert.deepEqual(ran, ['j']);
});

// The whole point of sharing: one definition, two consumers.
test('the page and a surface get the same answer for the same key', () => {
  ...
});
```

`vim-page-test.mjs` keeps every existing case unchanged. That suite passing
untouched is what proves the extraction changed no behaviour, so do not edit
it to fit the new shape - if a case needs editing, the extraction is wrong.

- [ ] **Step 2: Run to verify they fail**

- [ ] **Step 3: Write `vim-grammar.js`**

```js
// A plain script, not a module: the injected path evaluates this file as a
// string and cannot carry an import. WebUI surfaces load it from atrium.html
// ahead of their modules and read the global.
globalThis.__atriumVimGrammar = globalThis.__atriumVimGrammar || (function() {
  ...
  // `verbs` is the consumer's own table, so a page scrolls the window and a
  // surface scrolls its list.
  function makeGrammar(options) { ... }
  return {makeGrammar: makeGrammar, isEditable: isEditable};
})();
```

- [ ] **Step 4: Make `vim-page.js` use it**

Delete the moved code and call `makeGrammar` with the page's own verbs. The
file keeps its DOM half.

- [ ] **Step 5: Concatenate on the injected path**

`BuildVimInstallScript` loads both resources and joins them, grammar first:

```cpp
  return base::StrCat({LoadString(IDR_ATRIUM_VIM_GRAMMAR_JS), u"\n",
                       LoadString(IDR_ATRIUM_VIM_PAGE_JS)});
```

- [ ] **Step 6: Run, then mutate**

Reverse the concatenation order so the grammar loads second, and confirm a
page test fails by name. That mutation is the load-order risk made visible,
and it is the one this task is most likely to ship broken.

- [ ] **Step 7: Commit**

## Task 2: The sidebar gets the grammar

The sidebar goes first because it is the surface that gets motion and hint
verbs, and because doing one surface properly is what makes the other three
cheap. Task 3 then does the command bar, settings and the AI panel, which the
task description names alongside it.

**Files:**
- Modify: `overlay/chrome/browser/resources/atrium/sidebar.js`
- Modify: `overlay/chrome/browser/resources/atrium/atrium.html`
- Modify: `build/scripts/sidebar-test.mjs`

- [ ] **Step 1: Write the failing tests**

```js
test('j and k move the sidebar selection with vim mode on', () => { ... });

// With the toggle off the sidebar behaves exactly as it does today.
test('with vim mode off every sidebar key keeps its meaning', () => { ... });

// The conflict. The sidebar already binds some of these.
test('a sidebar key the grammar also claims is resolved explicitly', () => {
  ...
});

// The load-order trap: a surface whose grammar never loaded looks identical
// to vim mode being off, so assert the grammar is there rather than assuming.
test('the sidebar module finds the grammar global', () => {
  assert.ok(globalThis.__atriumVimGrammar);
});
```

- [ ] **Step 2: Survey the conflicts before writing any code**

List every single unmodified key the sidebar already handles. For each, decide
and write down whether vim takes it, the sidebar keeps it, or the two are the
same action anyway. Conflicts must be "resolved explicitly", so a conflict discovered
later by a user is a failure of this step, not of the code.

Put the table in `docs/vim-mode-design.md`, not only in the task.

- [ ] **Step 3: Wire it**

The surface asks the browser whether vim mode is on. The flag already lives in
the browser process as `AtriumBrowserWindow::VimModeEnabled`, so push it the
way other settings reach surfaces rather than reading a second copy.

- [ ] **Step 4: Run, mutate a conflict decision, commit**

## Task 3: The command bar, settings and the AI panel

Same wiring as Task 2, three more times, so this is a task only because each
surface has its own existing keys to reconcile. The conflict survey is the
work. The AI panel and the command bar both hold a text field that owns nearly
every key while it has focus, so `isEditable` carries them: with the field
focused the grammar must not apply at all.

**Files:**
- Modify: `overlay/chrome/browser/resources/atrium/commandbar.js`
- Modify: `overlay/chrome/browser/resources/atrium/settings.js`
- Modify: `overlay/chrome/browser/resources/atrium/ai-panel.js`
- Modify: the matching `build/scripts/*-test.mjs` suites

- [ ] **Step 1: Survey each surface's keys, as in Task 2 Step 2**

Three tables, one per surface, in `docs/vim-mode-design.md`. Do all three
before writing code: a key the command bar claims and settings does not is the
case that produces an inconsistent feel.

- [ ] **Step 2: Write the failing tests, one suite at a time**

Node coverage is required on each surface that gains the grammar, so each
suite gets the vim-on, vim-off and conflict cases Task 2 established.

- [ ] **Step 3: Wire each surface, running its suite before moving on**

- [ ] **Step 4: Run, mutate one conflict decision per surface, commit**

`atrium-eb` owns `settings.js` today. Check before editing it, and take the
surfaces in whatever order leaves that one last.

## Task 4: Hints on the sidebar

The sidebar gets motion **and hint** verbs. The element walk from V2 is already
consumer-agnostic - it takes a root and returns clickable targets - so this is
mostly wiring, but the label allocation is not: a surface has no frames, so it
allocates its own labels with offset 0 and its own total.

- [ ] **Step 1: Write the failing test**
- [ ] **Step 2: Run to verify it fails**
- [ ] **Step 3: Wire the walk to the sidebar's root**
- [ ] **Step 4: Run, mutate, commit**

## Task 5: Documentation and the whole gate

- [ ] **Step 1: Update `docs/vim-mode-design.md`**

Record the grammar-sharing decision and why a plain script publishing a global
beat a module. Record the four conflict tables from Task 2 and Task 3. Both are
things the next reader would otherwise re-litigate.

- [ ] **Step 2: Run everything, including the suites this touches indirectly**

The sidebar is pinned by more than its own suite. Run every node suite, not
the ones whose names look related - a behaviour can be pinned by the shape of
a message rather than by any word worth grepping for.

- [ ] **Step 3: Commit**

## What V4 deliberately leaves

Hints on the three surfaces Task 3 wires. The sidebar gets motion and hint verbs
and motion alone elsewhere, so Task 4 hints the sidebar only. A
surface whose every control is one of a dozen buttons gains little from labels
over them, so leave it until someone asks.

## Notes carried from V1 to V3

- `atrium_browser_window.cc` is shared. Commit by hunk and announce before
  committing while another session is staging.
- A new JS file must be in `atrium_resources.grd` AND reachable, or
  `verify-unwired-paths.py` fails it. A file reached only from C++ counts
  through its IDR name.
- Injected JS must survive a document with no root element.
  `verify-injected-js.py` scans a named list, so add `vim-grammar.js` to
  `INJECTED_JS` in Task 1.
- A mutation has to compile to prove anything.
