# Vim mode V1 page core implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Put an Atrium vim script into every page frame so NORMAL-mode scroll motions work, behind a settings toggle that is off by default, without ever stealing a keystroke that belongs in a text field.

**Architecture:** Two halves. `vim-page.js` runs in the isolated world of every frame, owns mode detection and verb execution, and vetoes a wrong steal. `AtriumBrowserWindow::PreHandleKeyboardEvent` consumes a key only while vim mode is on and the page's last reported mode is NORMAL, then forwards that key to the page half. The browser half exists because a page-registered listener cannot beat a listener a site registered earlier, and `PreHandleKeyboardEvent` runs before the renderer sees the event.

**Tech Stack:** Chromium browser C++, Rust (`atrium-core` settings), plain JS resources under `chrome/browser/resources/atrium`, GN, GoogleTest, `node --test` with `build/scripts/dom-shim.mjs`.

**Spec:** `docs/vim-mode-design.md`

## Global Constraints

- Every Atrium-authored source file carries the Apache-2.0 header. `make license-verify` enforces it, `make license-fix` writes it.
- The Rust core is mirrored. Every edit to `rust/atrium-core/src/settings.rs` must be made identically in `overlay/third_party/atrium/core/src/settings.rs`. `make rust-sync` and `make overlay-verify` check the pair.
- Node suites read the JS from the Chromium checkout, not from this repo. Run `make overlay-sync` after editing anything under `overlay/` and before running `make webui-test`.
- `ATRIUM_CHROMIUM_SRC` defaults to `~/chromium-atrium/src`.
- New settings JSON key: `vimModeEnabled`. New Rust field: `vim_mode_enabled`. Default `false`.
- Grit resource id: `IDR_ATRIUM_VIM_PAGE_JS`.
- Never run browsertests or the app on the host. UI verification goes through `make vm-run-gtest BIN=browser_tests FILTER=...`. `browser_tests` is not headless on macOS and puts real windows on the user's desktop.
- Judge a build by the text of its log, not by its exit code. `build-shell.sh` exits 0 after a failed build.
- Commit after every task. Conventional commits, subject line only, 72 characters or fewer.

---

### Task 1: The steal decision

A pure function that answers one question: given vim on or off, the page's last reported mode, and a key, does the browser steal it. No AppKit, no `views::`, no browser process. Same posture as `atrium_surface_key_route.h`.

**Files:**
- Create: `overlay/chrome/browser/ui/atrium/atrium_vim_key_decision.h`
- Create: `overlay/chrome/browser/ui/atrium/atrium_vim_key_decision.cc`
- Create: `overlay/chrome/browser/ui/atrium/atrium_vim_key_decision_unittest.cc`
- Modify: `overlay/chrome/browser/ui/atrium/BUILD.gn` (new `source_set`, plus two entries in `source_set("unit_tests")`)

**Interfaces:**
- Consumes: nothing.
- Produces: `atrium::VimKeyInputs`, `atrium::VimKeyRoute`, `atrium::VimKeyRoute atrium::RouteVimKey(const VimKeyInputs&)`. Task 6 calls `RouteVimKey` from `PreHandleKeyboardEvent`.

- [ ] **Step 1: Write the failing test**

Create `atrium_vim_key_decision_unittest.cc`:

```cpp
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_vim_key_decision_unittest.cc
#include "chrome/browser/ui/atrium/atrium_vim_key_decision.h"

#include "testing/gtest/include/gtest/gtest.h"

namespace atrium {
namespace {

VimKeyInputs NormalModeKey(const char* character) {
  VimKeyInputs in;
  in.vim_enabled = true;
  in.page_in_normal_mode = true;
  in.is_key_down = true;
  in.character = character;
  return in;
}

TEST(AtriumVimKeyDecisionTest, MotionKeyInNormalModeIsStolen) {
  EXPECT_EQ(RouteVimKey(NormalModeKey("j")), VimKeyRoute::kStealForVim);
  EXPECT_EQ(RouteVimKey(NormalModeKey("k")), VimKeyRoute::kStealForVim);
  EXPECT_EQ(RouteVimKey(NormalModeKey("g")), VimKeyRoute::kStealForVim);
}

// The whole point of the toggle. With vim off the window must behave exactly
// as it did before this feature existed.
TEST(AtriumVimKeyDecisionTest, NothingIsStolenWhileVimIsOff) {
  VimKeyInputs in = NormalModeKey("j");
  in.vim_enabled = false;
  EXPECT_EQ(RouteVimKey(in), VimKeyRoute::kPassToPage);
}

// The failure that would make vim mode feel broken: a keystroke meant for a
// text field swallowed as a motion.
TEST(AtriumVimKeyDecisionTest, NothingIsStolenWhileThePageReportsInsert) {
  VimKeyInputs in = NormalModeKey("j");
  in.page_in_normal_mode = false;
  EXPECT_EQ(RouteVimKey(in), VimKeyRoute::kPassToPage);
}

// Cmd+J and Ctrl+D belong to Atrium's own hotkeys and to the system. Vim
// claims unmodified keys only. Shift is not a modifier here: G is shift+g.
TEST(AtriumVimKeyDecisionTest, ModifiedChordsArePassedThrough) {
  VimKeyInputs command = NormalModeKey("j");
  command.command = true;
  EXPECT_EQ(RouteVimKey(command), VimKeyRoute::kPassToPage);

  VimKeyInputs control = NormalModeKey("d");
  control.control = true;
  EXPECT_EQ(RouteVimKey(control), VimKeyRoute::kPassToPage);

  VimKeyInputs option = NormalModeKey("k");
  option.option = true;
  EXPECT_EQ(RouteVimKey(option), VimKeyRoute::kPassToPage);
}

TEST(AtriumVimKeyDecisionTest, ShiftAloneStillReachesVim) {
  VimKeyInputs in = NormalModeKey("G");
  in.shift = true;
  EXPECT_EQ(RouteVimKey(in), VimKeyRoute::kStealForVim);
}

// Key up must not fire a motion twice, and a key with no character (arrows,
// function keys) is not part of the V1 grammar.
TEST(AtriumVimKeyDecisionTest, KeyUpAndCharacterlessKeysArePassedThrough) {
  VimKeyInputs up = NormalModeKey("j");
  up.is_key_down = false;
  EXPECT_EQ(RouteVimKey(up), VimKeyRoute::kPassToPage);

  VimKeyInputs empty = NormalModeKey("");
  EXPECT_EQ(RouteVimKey(empty), VimKeyRoute::kPassToPage);
}

// A key vim has no verb for must reach the page. Stealing every key would
// break every site's own single-key shortcuts.
TEST(AtriumVimKeyDecisionTest, KeysOutsideTheGrammarArePassedThrough) {
  EXPECT_EQ(RouteVimKey(NormalModeKey("q")), VimKeyRoute::kPassToPage);
  EXPECT_EQ(RouteVimKey(NormalModeKey("7")), VimKeyRoute::kPassToPage);
}

}  // namespace
}  // namespace atrium
```

- [ ] **Step 2: Run the test to verify it fails**

```bash
make overlay-sync
autoninja -C ~/chromium-atrium/src/out/atrium unit_tests
```

Expected: the compile fails with `atrium_vim_key_decision.h` not found. Read the log text, not the exit code.

- [ ] **Step 3: Write the header**

Create `atrium_vim_key_decision.h`:

```cpp
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_vim_key_decision.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_VIM_KEY_DECISION_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_VIM_KEY_DECISION_H_

#include <string>

namespace atrium {

// whether PreHandleKeyboardEvent consumes a key for vim mode
// before the renderer sees it.
//
// The browser cannot see which element has focus without asking the renderer,
// so it does not try. `page_in_normal_mode` is the page's own answer, pushed
// from vim-page.js on every focus change. The browser enforces, the page
// decides.
struct VimKeyInputs {
  bool vim_enabled = false;

  // The mode vim-page.js last reported. False means a text field has focus.
  bool page_in_normal_mode = false;

  bool is_key_down = false;

  bool command = false;
  bool control = false;
  bool option = false;

  // Shift is NOT a steal blocker: G is shift+g and belongs to the grammar.
  bool shift = false;

  // The character the key produces, lowercased for unshifted keys. Empty for
  // arrows and function keys.
  std::string character;
};

enum class VimKeyRoute {
  // The renderer gets the event unchanged, exactly as before vim mode existed.
  kPassToPage,
  // The browser consumes the event and forwards the character to vim-page.js.
  kStealForVim,
};

// True for a character V1's grammar has a verb for.
bool IsVimV1GrammarKey(const std::string& character);

VimKeyRoute RouteVimKey(const VimKeyInputs& in);

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_VIM_KEY_DECISION_H_
```

- [ ] **Step 4: Write the implementation**

Create `atrium_vim_key_decision.cc`:

```cpp
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_vim_key_decision.cc
#include "chrome/browser/ui/atrium/atrium_vim_key_decision.h"

#include <array>

namespace atrium {

namespace {

// V1's whole grammar. g is here for the gg prefix. vim-page.js owns the
// pending-prefix state, so the browser only has to know g can start a verb.
constexpr std::array<const char*, 6> kGrammar = {"j", "k", "d", "u", "g", "G"};

}  // namespace

bool IsVimV1GrammarKey(const std::string& character) {
  for (const char* key : kGrammar) {
    if (character == key) {
      return true;
    }
  }
  return false;
}

VimKeyRoute RouteVimKey(const VimKeyInputs& in) {
  if (!in.vim_enabled || !in.page_in_normal_mode || !in.is_key_down) {
    return VimKeyRoute::kPassToPage;
  }
  if (in.command || in.control || in.option) {
    return VimKeyRoute::kPassToPage;
  }
  if (!IsVimV1GrammarKey(in.character)) {
    return VimKeyRoute::kPassToPage;
  }
  return VimKeyRoute::kStealForVim;
}

}  // namespace atrium
```

- [ ] **Step 5: Wire BUILD.gn**

In `overlay/chrome/browser/ui/atrium/BUILD.gn`, add a `source_set` next to `source_set("surface_key_route")` (around line 938):

```gn
# whether PreHandleKeyboardEvent consumes a key for vim mode. Pure,
# so the steal rule is asserted without a browser process.
source_set("vim_key_decision") {
  sources = [
    "atrium_vim_key_decision.cc",
    "atrium_vim_key_decision.h",
  ]
}
```

Add `"atrium_vim_key_decision_unittest.cc"` to the `sources` list of `source_set("unit_tests")`, in alphabetical position. Add `":vim_key_decision"` to that target's `deps`.

- [ ] **Step 6: Run the test to verify it passes**

```bash
make overlay-sync
autoninja -C ~/chromium-atrium/src/out/atrium unit_tests
~/chromium-atrium/src/out/atrium/unit_tests --gtest_filter=AtriumVimKeyDecisionTest.*
```

Expected: 7 tests run, 7 pass. Reconcile the counts: `[ RUN ]` minus `[ OK ]` minus `[ FAILED ]` must be zero. A crash prints neither OK nor FAILED and would otherwise hide in the pass count.

- [ ] **Step 7: Commit**

```bash
git add overlay/chrome/browser/ui/atrium/atrium_vim_key_decision.h \
        overlay/chrome/browser/ui/atrium/atrium_vim_key_decision.cc \
        overlay/chrome/browser/ui/atrium/atrium_vim_key_decision_unittest.cc \
        overlay/chrome/browser/ui/atrium/BUILD.gn
git commit -m "feat(vim): decide when the window steals a key for vim mode"
```

---

### Task 2: The settings flag

A persisted `vimModeEnabled` boolean, off by default, all the way from the Rust core to a switch in Settings. No vim behaviour yet. This mirrors commit `59887646a`, which added `autoSummarize` through the same layers.

**Files:**
- Modify: `rust/atrium-core/src/settings.rs`
- Modify: `overlay/third_party/atrium/core/src/settings.rs` (identical edit)
- Modify: `fixtures/stores/settings/current.expected.json`
- Modify: `fixtures/stores/settings/v1-memory-saver-never.expected.json`
- Modify: `fixtures/stores/settings/v1-original-keys.expected.json`
- Modify: `overlay/chrome/browser/resources/atrium/settings.js`
- Modify: `build/scripts/settings-test.mjs`

**Interfaces:**
- Consumes: nothing.
- Produces: the JSON key `vimModeEnabled` in the settings object. Task 6 reads it with `AtriumBrowserWindow::VimModeEnabledFromSettings`.

- [ ] **Step 1: Write the failing Rust tests**

In `rust/atrium-core/src/settings.rs`, inside `mod tests`, next to the `auto_summarize` block (around line 2336):

```rust
    // --- Vim mode ---

    #[test]
    fn a_file_without_vim_mode_falls_to_off() {
        let s = load(
            r#"{"schemaVersion":1,"appIconVariant":"nord-accent",
            "animationsEnabled":true,"startupBehavior":"blankTab"}"#,
        );
        assert!(!s.vim_mode_enabled);
        assert_eq!(s.vim_mode_enabled, Settings::default().vim_mode_enabled);
    }

    #[test]
    fn vim_mode_true_survives_the_load_and_the_write() {
        let file = r#"{"schemaVersion":1,"appIconVariant":"nord-accent",
            "animationsEnabled":true,"startupBehavior":"blankTab",
            "vimModeEnabled":true}"#;

        let s = load(file);
        assert!(s.vim_mode_enabled);

        let out = written_back(file);
        assert_eq!(out["vimModeEnabled"], serde_json::json!(true));
    }
```

- [ ] **Step 2: Run to verify it fails**

```bash
make rust-test
```

Expected: compile error, no field `vim_mode_enabled` on `Settings`.

- [ ] **Step 3: Add the field**

Three edits in `rust/atrium-core/src/settings.rs`, each next to its `auto_summarize` neighbour:

```rust
// In struct FileShape, after auto_summarize:
    #[serde(default)]
    vim_mode_enabled: Option<bool>,
```

```rust
// In pub struct Settings, after auto_summarize:
    /// whether vim keys act on the page. Defaults off - vim keys
    /// take single unmodified characters away from every site that binds its
    /// own, so this is opt-in.
    pub vim_mode_enabled: bool,
```

```rust
// In impl Default for Settings, after auto_summarize:
            vim_mode_enabled: false,
```

```rust
// In load_and_what_it_migrated, after the auto_summarize line:
        vim_mode_enabled: file.vim_mode_enabled.unwrap_or(false),
```

- [ ] **Step 4: Mirror the edit and run the tests**

```bash
make rust-sync
make rust-test
```

Expected: both new tests pass. `make rust-sync` copies `rust/atrium-core/src/settings.rs` to `overlay/third_party/atrium/core/src/settings.rs`. Confirm with `git status` that the overlay copy changed. If it did not, copy it by hand.

- [ ] **Step 5: Update the store fixtures**

The corpus fixtures record the full written-back object, so a new key with a default makes them stale. Add `"vimModeEnabled": false` after the `"autoSummarize"` line in each of:

- `fixtures/stores/settings/current.expected.json`
- `fixtures/stores/settings/v1-memory-saver-never.expected.json`
- `fixtures/stores/settings/v1-original-keys.expected.json`

Then:

```bash
make corpus-check
```

Expected: PASS. If it reports a diff, the diff names the exact key and file to fix.

- [ ] **Step 6: Write the failing settings UI test**

In `build/scripts/settings-test.mjs`, add `vimModeEnabled: false,` to the `CORE_SETTINGS` object, and add a selector next to `autoSummarizeToggle`:

```js
const vimModeToggle = (root) =>
    root.querySelector(
        '.settings-row[data-key="vimModeEnabled"] .settings-row-toggle');
```

Then add the tests:

```js
// the Vim mode switch.

test('the Vim mode row is a real toggle, off by default', () => {
  const root = inputsRoot();
  const toggle = vimModeToggle(root);
  assert.ok(toggle, 'vimModeEnabled renders a toggle');
  assert.equal(toggle.getAttribute('aria-pressed'), 'false');
});

test('switching Vim mode on sends the whole object with ' +
     'vimModeEnabled true', () => {
  const root = inputsRoot();
  vimModeToggle(root).click();
  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.vimModeEnabled, true);
  assert.deepEqual({...payload, vimModeEnabled: false}, CORE_SETTINGS);
});
```

The row goes in the `inputs` pane, which already has `inputsRoot()` in this
file. Check whether an existing test asserts the `inputs` pane's row count and
raise it by one if so.

- [ ] **Step 7: Run to verify it fails**

```bash
make overlay-sync
node --test build/scripts/settings-test.mjs
```

Expected: both new tests fail, `vimModeEnabled renders a toggle`.

- [ ] **Step 8: Add the settings row**

In `overlay/chrome/browser/resources/atrium/settings.js`, add the row to `PANE_ROWS.inputs`, below the Swipe feel entry:

```js
    {label: 'Vim mode', key: 'vimModeEnabled', toggle: true,
     hint: 'Use j k d u gg G to scroll pages'},
```

`GROUPS` covers only `general`, `appearance`, and `ai`, so the `inputs` pane renders ungrouped and needs no `GROUPS` edit. Do not add one.

- [ ] **Step 9: Run to verify it passes**

```bash
make overlay-sync
node --test build/scripts/settings-test.mjs
```

Expected: every test in the file passes. Read the third number in the summary, not just the absence of FAIL. Two suites in this repo have reported success while their own output said otherwise.

Then run the whole sweep, because one behaviour is pinned in several suites and a settings change goes red in files you did not edit:

```bash
make webui-test
```

- [ ] **Step 10: Commit**

```bash
git add rust/atrium-core/src/settings.rs \
        overlay/third_party/atrium/core/src/settings.rs \
        fixtures/stores/settings \
        overlay/chrome/browser/resources/atrium/settings.js \
        build/scripts/settings-test.mjs
git commit -m "feat(vim): store and show a Vim mode switch, off by default"
```

---

### Task 3: The page grammar

`vim-page.js` on its own: mode detection, the scroll verbs, the mode indicator, and the verb entry point the browser will call. No injection and no C++ yet, so this task is fully covered by a node suite.

The file is a plain script, not an ES module. Injection evaluates a string and a string cannot carry `import`. It installs itself under a guard the way `atrium_marginalia_script.cc`'s library does.

**Files:**
- Create: `overlay/chrome/browser/resources/atrium/vim-page.js`
- Create: `build/scripts/vim-page-test.mjs`

**Interfaces:**
- Consumes: nothing.
- Produces: `window.__atriumVim` with exactly this surface, which Task 5 calls from C++:
  - `key(character)` returns `true` when the verb ran, `false` when the page considers itself in INSERT mode and the browser should not have stolen it.
  - `mode()` returns the string `"NORMAL"` or `"INSERT"`.
  - `drainMode()` returns `"NORMAL"` or `"INSERT"` and is what the browser polls.

- [ ] **Step 1: Write the failing test**

Create `build/scripts/vim-page-test.mjs`:

```js
#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The injected page script's grammar and mode machine. The C++
// half that calls into it is covered by atrium_vim_mode_browsertest.cc.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');
const SOURCE = fs.readFileSync(path.join(ATRIUM_DIR, 'vim-page.js'), 'utf8');

// The script is injected as a string, so it is evaluated the same way here.
function loadVim() {
  installDOM();
  const scrolled = [];
  globalThis.window.scrollBy = (x, y) => scrolled.push([x, y]);
  globalThis.window.innerHeight = 800;
  globalThis.window.scrollTo = (x, y) => scrolled.push(['to', y]);
  new Function(SOURCE)();
  return {vim: globalThis.window.__atriumVim, scrolled};
}

function focusEditable(tag, attrs = {}) {
  const el = globalThis.document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    el.setAttribute(k, v);
  }
  globalThis.document.body.appendChild(el);
  el.focus();
  return el;
}

test('j and k scroll down and up', () => {
  const {vim, scrolled} = loadVim();
  assert.equal(vim.key('j'), true);
  assert.equal(vim.key('k'), true);
  assert.deepEqual(scrolled.map(([, y]) => Math.sign(y)), [1, -1]);
});

test('d and u scroll by half a viewport', () => {
  const {vim, scrolled} = loadVim();
  vim.key('d');
  vim.key('u');
  assert.deepEqual(scrolled, [[0, 400], [0, -400]]);
});

test('gg needs both keystrokes and G needs one', () => {
  const {vim, scrolled} = loadVim();
  assert.equal(vim.key('g'), true);
  assert.deepEqual(scrolled, [], 'a lone g scrolls nothing');
  vim.key('g');
  assert.deepEqual(scrolled, [['to', 0]]);

  vim.key('G');
  assert.equal(scrolled.length, 2);
  assert.equal(scrolled[1][0], 'to');
});

test('a g followed by another verb drops the prefix', () => {
  const {vim, scrolled} = loadVim();
  vim.key('g');
  vim.key('j');
  assert.equal(scrolled.length, 1, 'j ran, gg did not');
  vim.key('g');
  vim.key('g');
  assert.equal(scrolled.length, 2, 'the prefix was not left armed');
});

test('focus in an input, a textarea, or a contenteditable is INSERT',
     () => {
  const {vim} = loadVim();
  assert.equal(vim.mode(), 'NORMAL');

  for (const [tag, attrs] of [['input', {}], ['textarea', {}],
                              ['div', {contenteditable: 'true'}]]) {
    const el = focusEditable(tag, attrs);
    assert.equal(vim.mode(), 'INSERT', `${tag} focus is INSERT`);
    el.blur();
    assert.equal(vim.mode(), 'NORMAL', `${tag} blur returns to NORMAL`);
  }
});

test('a readonly input is not INSERT', () => {
  const {vim} = loadVim();
  focusEditable('input', {readonly: 'readonly'});
  assert.equal(vim.mode(), 'NORMAL');
});

// The veto. The browser can steal a key while its cached mode is stale, and
// the page is the only half that knows the truth.
test('key() refuses the verb and reports false while in INSERT', () => {
  const {vim, scrolled} = loadVim();
  focusEditable('input');
  assert.equal(vim.key('j'), false);
  assert.deepEqual(scrolled, []);
});

test('the indicator shows the current mode and follows focus', () => {
  const {vim} = loadVim();
  const indicator = () =>
      globalThis.document.body.querySelector('.atrium-vim-indicator');
  assert.equal(indicator().textContent, 'NORMAL');
  focusEditable('input');
  vim.mode();
  assert.equal(indicator().textContent, 'INSERT');
});

test('drainMode returns the current mode', () => {
  const {vim} = loadVim();
  assert.equal(vim.drainMode(), 'NORMAL');
  focusEditable('input');
  assert.equal(vim.drainMode(), 'INSERT');
});

// The script is injected into a document that may not have committed yet.
test('installing twice keeps one instance and one indicator', () => {
  const {vim} = loadVim();
  new Function(SOURCE)();
  assert.equal(globalThis.window.__atriumVim, vim);
  assert.equal(
      globalThis.document.body.querySelectorAll('.atrium-vim-indicator').length,
      1);
});
```

- [ ] **Step 2: Run to verify it fails**

```bash
node --test build/scripts/vim-page-test.mjs
```

Expected: the whole file fails to load, `ENOENT` on `vim-page.js`.

- [ ] **Step 3: Write the script**

Create `overlay/chrome/browser/resources/atrium/vim-page.js`:

```js
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Injected into the isolated world of every frame, so this is a
// plain script with no imports - the injector evaluates a string.
//
// This half owns the mode and the verbs. The browser half only enforces the
// steal, and its copy of the mode can be one keystroke stale, which is why
// key() returns false rather than running a verb it should not.
window.__atriumVim = window.__atriumVim || (function() {
  var INDICATOR_CLASS = 'atrium-vim-indicator';
  var pendingG = false;
  var indicator = null;

  function isEditable(el) {
    if (!el) return false;
    if (el.isContentEditable) return true;
    var name = (el.nodeName || '').toUpperCase();
    if (name === 'TEXTAREA') return !el.hasAttribute('readonly');
    if (name === 'INPUT') return !el.hasAttribute('readonly');
    if (el.getAttribute && el.getAttribute('contenteditable') === 'true') {
      return true;
    }
    return false;
  }

  function mode() {
    return isEditable(document.activeElement) ? 'INSERT' : 'NORMAL';
  }

  function ensureIndicator() {
    if (indicator && indicator.parentNode) return indicator;
    var host = document.body || document.documentElement;
    if (!host) return null;
    var existing = host.querySelector && host.querySelector('.' +
                                                            INDICATOR_CLASS);
    if (existing) {
      indicator = existing;
      return indicator;
    }
    indicator = document.createElement('div');
    indicator.className = INDICATOR_CLASS;
    indicator.style.cssText = 'position:fixed;bottom:8px;right:8px;' +
        'z-index:2147483647;font:11px ui-monospace,monospace;' +
        'padding:2px 6px;border-radius:4px;background:rgba(0,0,0,.7);' +
        'color:#fff;pointer-events:none;';
    host.appendChild(indicator);
    return indicator;
  }

  function paint() {
    var el = ensureIndicator();
    if (el) el.textContent = mode();
  }

  function half() {
    return Math.round((window.innerHeight || 0) / 2);
  }

  var VERBS = {
    j: function() { window.scrollBy(0, 60); },
    k: function() { window.scrollBy(0, -60); },
    d: function() { window.scrollBy(0, half()); },
    u: function() { window.scrollBy(0, -half()); },
    G: function() {
      window.scrollTo(0, (document.body || document.documentElement)
          .scrollHeight || 0);
    },
  };

  function key(character) {
    if (mode() === 'INSERT') {
      pendingG = false;
      paint();
      return false;
    }
    if (character === 'g') {
      if (pendingG) {
        pendingG = false;
        window.scrollTo(0, 0);
      } else {
        pendingG = true;
      }
      return true;
    }
    pendingG = false;
    var verb = VERBS[character];
    if (verb) verb();
    return true;
  }

  document.addEventListener('focusin', paint, true);
  document.addEventListener('focusout', function() {
    // focusout fires before the new element takes focus.
    setTimeout(paint, 0);
  }, true);
  paint();

  return {
    key: key,
    mode: mode,
    drainMode: function() {
      paint();
      return mode();
    },
  };
})();
```

- [ ] **Step 4: Run to verify it passes**

```bash
make overlay-sync
node --test build/scripts/vim-page-test.mjs
```

Expected: 10 tests, 10 pass.

- [ ] **Step 5: Prove the tests can fail**

Revert one hunk and confirm a named test goes red, then put it back. Delete the `if (mode() === 'INSERT')` guard in `key`, rerun, and confirm `key() refuses the verb and reports false while in INSERT` fails by name. Restore the guard with an editor, never with `git checkout` - the tree carries uncommitted work from other lanes.

- [ ] **Step 6: Commit**

```bash
git add overlay/chrome/browser/resources/atrium/vim-page.js \
        build/scripts/vim-page-test.mjs
git commit -m "feat(vim): add the injected page script's modes and motions"
```

---

### Task 4: Injection into every frame

Package `vim-page.js` as a resource, load it in C++, and inject it into the isolated world of every frame of the active tab on document load. Marginalia's call site only reaches `GetPrimaryMainFrame`, so cross-origin iframes are invisible to it. Vim needs them.

**Files:**
- Create: `overlay/chrome/browser/ui/atrium/atrium_vim_script.h`
- Create: `overlay/chrome/browser/ui/atrium/atrium_vim_script.cc`
- Create: `overlay/chrome/browser/ui/atrium/atrium_vim_mode_browsertest.cc`
- Modify: `overlay/chrome/browser/resources/atrium/atrium_resources.grd`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_browser_window.h`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_browser_window.cc`
- Modify: `overlay/chrome/browser/ui/atrium/BUILD.gn`

**Interfaces:**
- Consumes: `window.__atriumVim` from Task 3.
- Produces:
  - `std::u16string atrium::BuildVimInstallScript()`
  - `std::u16string atrium::BuildVimKeyScript(const std::string& character)`
  - `std::u16string atrium::BuildVimModeDrainScript()`
  - `void AtriumBrowserWindow::InstallVimOnActiveTab()`
  - `void AtriumBrowserWindow::InstallVimOnActiveTabForTesting()` (public, calls the private one, matching `ReanchorMarginaliaOnActiveTabForTesting`)

- [ ] **Step 1: Write the failing browsertest**

Create `atrium_vim_mode_browsertest.cc` with the injection cases only. Task 6 adds the key cases to the same file.

```cpp
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_vim_mode_browsertest.cc
#include <string>

#include "chrome/browser/ui/atrium/atrium_browser_window.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "content/public/browser/web_contents.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

class AtriumVimModeBrowserTest : public InProcessBrowserTest {
 protected:
  void SetUpOnMainThread() override {
    host_resolver()->AddRule("*", "127.0.0.1");
    InProcessBrowserTest::SetUpOnMainThread();
    ASSERT_TRUE(embedded_test_server()->Start());
  }

  content::WebContents* ActiveContents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  AtriumBrowserWindow* Window() {
    return static_cast<AtriumBrowserWindow*>(browser()->window());
  }

  // Reading the sidebar or a page before it commits fails as a JS TypeError
  // and reads as a product defect. Wait, every time.
  void NavigateAndWait(const std::string& host, const std::string& path) {
    ASSERT_TRUE(content::NavigateToURL(
        ActiveContents(), embedded_test_server()->GetURL(host, path)));
    ASSERT_TRUE(content::WaitForLoadStop(ActiveContents()));
  }
};

IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, InstallsInTheMainFrame) {
  NavigateAndWait("a.test", "/title1.html");
  Window()->InstallVimOnActiveTabForTesting();

  EXPECT_EQ(true, content::EvalJs(ActiveContents()->GetPrimaryMainFrame(),
                                  "!!window.__atriumVim",
                                  content::EXECUTE_SCRIPT_DEFAULT_OPTIONS,
                                  ISOLATED_WORLD_ID_CHROME_INTERNAL));
}

// The marginalia call site this one is modelled on only reaches
// GetPrimaryMainFrame, so a cross-origin iframe would silently have no vim.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest,
                       InstallsInACrossOriginSubframe) {
  NavigateAndWait("a.test", "/iframe_cross_site.html");
  Window()->InstallVimOnActiveTabForTesting();

  content::RenderFrameHost* child =
      content::ChildFrameAt(ActiveContents()->GetPrimaryMainFrame(), 0);
  ASSERT_TRUE(child);
  EXPECT_NE(child->GetLastCommittedOrigin(),
            ActiveContents()->GetPrimaryMainFrame()->GetLastCommittedOrigin());
  EXPECT_EQ(true, content::EvalJs(child, "!!window.__atriumVim",
                                  content::EXECUTE_SCRIPT_DEFAULT_OPTIONS,
                                  ISOLATED_WORLD_ID_CHROME_INTERNAL));
}

// The script is injected once per document load and must not stack.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest,
                       ASecondInstallLeavesOneIndicator) {
  NavigateAndWait("a.test", "/title1.html");
  Window()->InstallVimOnActiveTabForTesting();
  Window()->InstallVimOnActiveTabForTesting();

  EXPECT_EQ(1, content::EvalJs(
                   ActiveContents()->GetPrimaryMainFrame(),
                   "document.querySelectorAll('.atrium-vim-indicator').length",
                   content::EXECUTE_SCRIPT_DEFAULT_OPTIONS,
                   ISOLATED_WORLD_ID_CHROME_INTERNAL));
}

}  // namespace
```

- [ ] **Step 2: Run to verify it fails**

```bash
make overlay-sync
make vm-run-gtest BIN=browser_tests FILTER=AtriumVimModeBrowserTest.*
```

Expected: compile failure, `InstallVimOnActiveTabForTesting` is not a member. Never run `browser_tests` on the host.

- [ ] **Step 3: Register the resource**

Task 3 already added the `include` to
`overlay/chrome/browser/resources/atrium/atrium_resources.grd`:

```xml
      <include name="IDR_ATRIUM_VIM_PAGE_JS" file="vim-page.js" type="BINDATA" />
```

It carries an entry in `build/scripts/unwired-paths-allowlist.json`, because
until Step 4 lands `BuildVimInstallScript` nothing references the resource.
Teach `verify-unwired-paths.py` that a grd JS entry named by an `IDR_` constant
in C++ counts as wired, then delete the entry. Run `make webui-test` and
confirm the guard passes on its own, not through the allowlist.

- [ ] **Step 4: Write the script builder**

Create `atrium_vim_script.h`:

```cpp
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_vim_script.h
#ifndef CHROME_BROWSER_UI_ATRIUM_ATRIUM_VIM_SCRIPT_H_
#define CHROME_BROWSER_UI_ATRIUM_ATRIUM_VIM_SCRIPT_H_

#include <string>

namespace atrium {

// Unlike atrium_marginalia_script.cc, the library is a resource
// rather than a raw string literal: the vim grammar is big enough to deserve
// its own node suite, and a string inside a .cc cannot have one.
std::u16string BuildVimInstallScript();

// Runs one verb. Evaluates to false when the page is in INSERT mode and the
// browser's cached mode was stale, which is the veto the window re-dispatches
// on.
std::u16string BuildVimKeyScript(const std::string& character);

std::u16string BuildVimModeDrainScript();

}  // namespace atrium

#endif  // CHROME_BROWSER_UI_ATRIUM_ATRIUM_VIM_SCRIPT_H_
```

Create `atrium_vim_script.cc`:

```cpp
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// chrome/browser/ui/atrium/atrium_vim_script.cc
#include "chrome/browser/ui/atrium/atrium_vim_script.h"

#include "base/json/string_escape.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "chrome/grit/atrium_resources.h"
#include "ui/base/resource/resource_bundle.h"

namespace atrium {

std::u16string BuildVimInstallScript() {
  // LoadDataResourceString, not GetRawDataResource: entries in
  // atrium_resources.pak are gzipped per entry, and this decompresses.
  return base::UTF8ToUTF16(
      ui::ResourceBundle::GetSharedInstance().LoadDataResourceString(
          IDR_ATRIUM_VIM_PAGE_JS));
}

std::u16string BuildVimKeyScript(const std::string& character) {
  return base::StrCat({u"(window.__atriumVim ? window.__atriumVim.key(",
                       base::UTF8ToUTF16(
                           base::GetQuotedJSONString(character)),
                       u") : false)"});
}

std::u16string BuildVimModeDrainScript() {
  return u"(window.__atriumVim ? window.__atriumVim.drainMode() : 'NORMAL')";
}

}  // namespace atrium
```

- [ ] **Step 5: Add the injection call**

In `atrium_browser_window.h`, next to the marginalia declarations (around line 2694):

```cpp
  // injects vim-page.js into every frame of the active tab. Every
  // frame, not just the primary main frame the marginalia call site uses - a
  // cross-origin iframe needs its own copy.
  void InstallVimOnActiveTab();
```

And in the public testing block next to `ReanchorMarginaliaOnActiveTabForTesting` (around line 1579):

```cpp
  void InstallVimOnActiveTabForTesting() { InstallVimOnActiveTab(); }
```

In `atrium_browser_window.cc`, next to `ReanchorMarginaliaOnActiveTab` (around line 7737):

```cpp
void AtriumBrowserWindow::InstallVimOnActiveTab() {
  content::WebContents* active =
      browser_ ? browser_->tab_strip_model()->GetActiveWebContents() : nullptr;
  if (!active) {
    return;
  }
  const std::u16string script = atrium::BuildVimInstallScript();
  active->ForEachRenderFrameHost(
      [&script](content::RenderFrameHost* frame) {
        if (!frame->IsRenderFrameLive()) {
          return;
        }
        frame->ExecuteJavaScriptInIsolatedWorld(
            script, base::NullCallback(), ISOLATED_WORLD_ID_CHROME_INTERNAL);
      });
}
```

Add `#include "chrome/browser/ui/atrium/atrium_vim_script.h"` to the include block.

In `ActiveTabObserver::DocumentOnLoadCompletedInPrimaryMainFrame` (around line 2951), add the call next to the marginalia one:

```cpp
      window_->ReanchorMarginaliaOnActiveTab();
      window_->InstallVimOnActiveTab();
```

- [ ] **Step 6: Wire BUILD.gn**

Add a `source_set`:

```gn
# loads vim-page.js out of atrium_resources.pak and wraps its entry
# points. The library itself is a JS resource so it can carry a node suite.
source_set("vim_script") {
  sources = [
    "atrium_vim_script.cc",
    "atrium_vim_script.h",
  ]
  deps = [
    "//base",
    "//chrome/browser/resources/atrium:resources",
    "//ui/base",
  ]
}
```

Add `"atrium_vim_mode_browsertest.cc"` to the browsertest `sources` list, in alphabetical position. Add `":vim_script"` to the deps of the target that compiles `atrium_browser_window.cc`.

If `//chrome/browser/resources/atrium:resources` is not the grit target's real label, read the `grit` rule in `overlay/chrome/browser/resources/atrium/BUILD.gn` and use the label it declares.

- [ ] **Step 7: Run to verify it passes**

```bash
make overlay-sync
make vm-run-gtest BIN=browser_tests FILTER=AtriumVimModeBrowserTest.*
```

Expected: 3 tests, 3 pass. Reconcile `[ RUN ]` minus `[ OK ]` minus `[ FAILED ]` against zero. Grep the log for `ERROR:CONSOLE` and `WARNING` even on a pass - a console error there names a real cause that a passing assertion can hide.

- [ ] **Step 8: Confirm the script was actually packaged**

A passing browsertest proves the injection ran. It does not prove the `.pak` carries the file you edited, because a stale build has made a working feature look broken here before.

```bash
python3 - <<'PY'
import gzip, pathlib, sys
pak = pathlib.Path.home() / 'chromium-atrium/src/out/atrium/atrium_resources.pak'
src = pathlib.Path.home() / 'chromium-atrium/src/chrome/browser/resources/atrium/vim-page.js'
blob = pak.read_bytes()
want = src.read_bytes()
hit = want in blob or gzip.compress(want) in blob
print('PACKAGED' if hit else 'STALE OR RECOMPRESSED - decompress entries and compare')
PY
```

If it prints `STALE`, rebuild and re-check before trusting the test. Gzip output is not byte-stable across settings, so a miss is inconclusive on its own - decompress the entries and compare text.

- [ ] **Step 9: Commit**

```bash
git add overlay/chrome/browser/ui/atrium/atrium_vim_script.h \
        overlay/chrome/browser/ui/atrium/atrium_vim_script.cc \
        overlay/chrome/browser/ui/atrium/atrium_vim_mode_browsertest.cc \
        overlay/chrome/browser/resources/atrium/atrium_resources.grd \
        overlay/chrome/browser/ui/atrium/atrium_browser_window.h \
        overlay/chrome/browser/ui/atrium/atrium_browser_window.cc \
        overlay/chrome/browser/ui/atrium/BUILD.gn
git commit -m "feat(vim): inject the page script into every frame on load"
```

---

### Task 5: The settings flag reaches the window

Cache `vimModeEnabled` in the browser process and keep it current from startup and from a Settings save. Still no key handling, so this task is verified by the C++ read path and a browsertest that flips the setting.

This mirrors `SetAiPanelAutoSummarize` in commit `59887646a` exactly.

**Files:**
- Modify: `overlay/chrome/browser/ui/atrium/atrium_browser_window.h`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_browser_window.cc`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_main_extra_parts.cc`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_settings_window.h`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_settings_window.cc`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_settings_apply_changes_unittest.cc`

**Interfaces:**
- Consumes: the `vimModeEnabled` JSON key from Task 2.
- Produces:
  - `static void AtriumBrowserWindow::SetVimModeEnabled(bool)`
  - `static bool AtriumBrowserWindow::VimModeEnabled()`
  - `static std::optional<bool> AtriumBrowserWindow::VimModeEnabledFromSettings(std::string_view)`
  - `SettingsApplyFields::vim_mode_enabled`, `SettingsApplyChanges::vim_mode_enabled`

- [ ] **Step 1: Write the failing test**

In `atrium_settings_apply_changes_unittest.cc`, copy the `auto_summarize` case and add:

```cpp
TEST(AtriumSettingsApplyChangesTest, VimModeChangeIsDetected) {
  SettingsApplyFields prev;
  prev.vim_mode_enabled = false;
  SettingsApplyFields current;
  current.vim_mode_enabled = true;

  const SettingsApplyChanges changes =
      DetermineSettingsApplyChanges(current, prev, /*is_first_save=*/false);
  EXPECT_TRUE(changes.vim_mode_enabled);
}

TEST(AtriumSettingsApplyChangesTest, AnUnchangedVimModeIsNotReapplied) {
  SettingsApplyFields prev;
  prev.vim_mode_enabled = true;
  SettingsApplyFields current;
  current.vim_mode_enabled = true;

  const SettingsApplyChanges changes =
      DetermineSettingsApplyChanges(current, prev, /*is_first_save=*/false);
  EXPECT_FALSE(changes.vim_mode_enabled);
}
```

Read the existing `DetermineSettingsApplyChanges` signature in `atrium_settings_window.cc` before writing these and match it exactly.

- [ ] **Step 2: Run to verify it fails**

```bash
make overlay-sync
autoninja -C ~/chromium-atrium/src/out/atrium unit_tests
```

Expected: no member `vim_mode_enabled`.

- [ ] **Step 3: Add the cache and the accessors**

In `atrium_browser_window.cc`, next to `CurrentAiPanelAutoSummarize` (around line 3037):

```cpp
// Fails closed to off, which is also settings.rs's default. A wrong guess
// here takes single characters away from every site.
bool& CurrentVimModeEnabled() {
  static bool enabled = false;
  return enabled;
}

// The page's last reported mode. NORMAL until a page says otherwise, and
// vim-page.js vetoes a steal made on a stale value.
bool& CurrentVimPageNormalMode() {
  static bool normal = true;
  return normal;
}
```

Next to `SetAiPanelAutoSummarize` (around line 4364):

```cpp
// static
void AtriumBrowserWindow::SetVimModeEnabled(bool enabled) {
  CurrentVimModeEnabled() = enabled;
}

// static
bool AtriumBrowserWindow::VimModeEnabled() {
  return CurrentVimModeEnabled();
}

// static
std::optional<bool> AtriumBrowserWindow::VimModeEnabledFromSettings(
    std::string_view settings_json) {
  std::optional<base::Value> parsed =
      base::JSONReader::Read(settings_json, base::JSON_PARSE_RFC);
  if (!parsed || !parsed->is_dict()) {
    return std::nullopt;
  }
  return parsed->GetDict().FindBool("vimModeEnabled");
}
```

Declare all three in `atrium_browser_window.h` next to the `AutoSummarizeFromSettings` block.

- [ ] **Step 4: Seed it at startup**

In `atrium_main_extra_parts.cc`, inside `SeedFromSettings`, after the `auto_summarize` block:

```cpp
        std::optional<bool> vim_mode_enabled =
            AtriumBrowserWindow::VimModeEnabledFromSettings(
                stores.settings_json);
        if (vim_mode_enabled.has_value()) {
          AtriumBrowserWindow::SetVimModeEnabled(*vim_mode_enabled);
        }
```

- [ ] **Step 5: Apply it on a Settings save**

Five edits in the settings window, each next to its `auto_summarize` neighbour:

1. `atrium_settings_window.h`: add `std::optional<bool> vim_mode_enabled;` to `SettingsApplyFields` and `bool vim_mode_enabled = false;` to `SettingsApplyChanges`.
2. `atrium_settings_window.h`: add the `std::optional<bool> vim_mode_enabled,` parameter to the `OnSettingsSaved` declaration.
3. `atrium_settings_window.cc`, in `SaveSettings`: read it and pass it through the bind.
4. `atrium_settings_window.cc`, in `DetermineSettingsApplyChanges`: both the first-save branch and the diff branch.
5. `atrium_settings_window.cc`, in `OnSettingsSaved`: store it into `current`, and gate the apply:

```cpp
      if (changes.vim_mode_enabled) {
        AtriumBrowserWindow::SetVimModeEnabled(*vim_mode_enabled);
      }
```

- [ ] **Step 6: Run to verify it passes**

```bash
make overlay-sync
autoninja -C ~/chromium-atrium/src/out/atrium unit_tests
~/chromium-atrium/src/out/atrium/unit_tests --gtest_filter=AtriumSettingsApplyChangesTest.*
```

Expected: every case in the suite passes, including the two new ones.

- [ ] **Step 7: Commit**

```bash
git add overlay/chrome/browser/ui/atrium/atrium_browser_window.h \
        overlay/chrome/browser/ui/atrium/atrium_browser_window.cc \
        overlay/chrome/browser/ui/atrium/atrium_main_extra_parts.cc \
        overlay/chrome/browser/ui/atrium/atrium_settings_window.h \
        overlay/chrome/browser/ui/atrium/atrium_settings_window.cc \
        overlay/chrome/browser/ui/atrium/atrium_settings_apply_changes_unittest.cc
git commit -m "feat(vim): carry the Vim mode switch into the browser process"
```

---

### Task 6: Steal the key, run the verb, honour the veto

The last wire. `PreHandleKeyboardEvent` stops being a stub. This is the task that can regress every keystroke in the browser, so its tests are the ones that matter most.

**Files:**
- Modify: `overlay/chrome/browser/ui/atrium/atrium_browser_window.h`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_browser_window.cc`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_vim_mode_browsertest.cc`
- Modify: `overlay/chrome/browser/ui/atrium/BUILD.gn`

**Interfaces:**
- Consumes: `atrium::RouteVimKey` (Task 1), `atrium::BuildVimKeyScript` and `atrium::BuildVimModeDrainScript` (Task 4), `AtriumBrowserWindow::VimModeEnabled` (Task 5).
- Produces: nothing later tasks depend on. V2 and V3 extend `RouteVimKey`'s grammar list and add verbs to `vim-page.js`.

- [ ] **Step 1: Write the failing tests**

Append to `atrium_vim_mode_browsertest.cc`. Add `#include "base/test/run_until.h"` and `#include "chrome/browser/ui/atrium/atrium_browser_controller.h"`. `atrium_favorite_keep_alive_browsertest.cc` shows how to open the core with a settings object rather than the defaults, if a case needs the flag to come from the store instead of `SetVimModeEnabled`.

```cpp
// The verb has to run against a real page, not a stub.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, JScrollsARealPage) {
  AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/long_page.html");
  Window()->InstallVimOnActiveTabForTesting();

  ASSERT_EQ(0, content::EvalJs(ActiveContents(), "window.scrollY"));
  Window()->SendVimKeyForTesting("j");

  EXPECT_TRUE(base::test::RunUntil([&]() {
    return content::EvalJs(ActiveContents(), "window.scrollY")
               .ExtractInt() > 0;
  }));
}

// The clicked case.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest,
                       TypingIntoAClickedFieldIsNotStolen) {
  AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/form.html");
  Window()->InstallVimOnActiveTabForTesting();

  ASSERT_TRUE(content::ExecJs(ActiveContents(),
                              "document.querySelector('input').focus()"));
  Window()->DrainVimModeForTesting();
  Window()->SendVimKeyForTesting("j");

  EXPECT_EQ(false,
            content::EvalJs(ActiveContents()->GetPrimaryMainFrame(),
                            "window.__atriumVim.key('j')",
                            content::EXECUTE_SCRIPT_DEFAULT_OPTIONS,
                            ISOLATED_WORLD_ID_CHROME_INTERNAL));
  EXPECT_EQ(0, content::EvalJs(ActiveContents(), "window.scrollY"));
}

// The case the drain cannot reach. A page that focuses its own field on
// load produces no input event, so the browser's cached mode stays NORMAL and
// only the page-side veto is correct.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest,
                       TypingIntoAProgrammaticallyFocusedFieldIsNotStolen) {
  AtriumBrowserWindow::SetVimModeEnabled(true);
  NavigateAndWait("a.test", "/autofocus_form.html");
  Window()->InstallVimOnActiveTabForTesting();
  // Deliberately no DrainVimModeForTesting: the browser still believes NORMAL.

  Window()->SendVimKeyForTesting("j");
  EXPECT_EQ(0, content::EvalJs(ActiveContents(), "window.scrollY"));
}

// With the toggle off the window must behave exactly as before.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest, TheToggleOffPassesEveryKey) {
  AtriumBrowserWindow::SetVimModeEnabled(false);
  NavigateAndWait("a.test", "/long_page.html");
  Window()->InstallVimOnActiveTabForTesting();

  EXPECT_EQ(content::KeyboardEventProcessingResult::NOT_HANDLED,
            Window()->RouteVimKeyForTesting("j"));
}
```

If `/long_page.html`, `/form.html`, or `/autofocus_form.html` are not in `//content/test/data`, write them into the test's own temp directory and serve them, or use `content::NavigateToURL` with a `data:` URL carrying the markup. Check what the embedded test server already serves before adding files.

- [ ] **Step 2: Run to verify it fails**

```bash
make overlay-sync
make vm-run-gtest BIN=browser_tests FILTER=AtriumVimModeBrowserTest.*
```

Expected: compile failure on `SendVimKeyForTesting`.

- [ ] **Step 3: Add the forward, the drain, and the testing seams**

In `atrium_browser_window.cc`, next to `InstallVimOnActiveTab`:

```cpp
void AtriumBrowserWindow::SendVimKey(const std::string& character) {
  content::WebContents* active =
      browser_ ? browser_->tab_strip_model()->GetActiveWebContents() : nullptr;
  if (!active) {
    return;
  }
  active->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      atrium::BuildVimKeyScript(character),
      base::BindOnce(&AtriumBrowserWindow::OnVimKeyRan,
                     weak_factory_.GetWeakPtr(), character),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

// False means the page was in INSERT mode and the cached mode was stale. The
// keystroke belongs in the focused element, so put it back and correct the
// cache.
void AtriumBrowserWindow::OnVimKeyRan(const std::string& character,
                                      base::Value ran) {
  if (ran.is_bool() && ran.GetBool()) {
    return;
  }
  CurrentVimPageNormalMode() = false;
  content::WebContents* active =
      browser_ ? browser_->tab_strip_model()->GetActiveWebContents() : nullptr;
  if (!active) {
    return;
  }
  const std::u16string redispatch = base::StrCat({
      u"(function(el){ if(!el) return; ",
      u"el.setRangeText ? el.setRangeText(",
      base::UTF8ToUTF16(base::GetQuotedJSONString(character)),
      u", el.selectionStart, el.selectionEnd, 'end') : null; ",
      u"})(document.activeElement)"});
  active->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      redispatch, base::NullCallback(), ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void AtriumBrowserWindow::DrainVimMode() {
  content::WebContents* active =
      browser_ ? browser_->tab_strip_model()->GetActiveWebContents() : nullptr;
  if (!active) {
    return;
  }
  active->GetPrimaryMainFrame()->ExecuteJavaScriptInIsolatedWorld(
      atrium::BuildVimModeDrainScript(),
      base::BindOnce(&AtriumBrowserWindow::OnVimModeDrained,
                     weak_factory_.GetWeakPtr()),
      ISOLATED_WORLD_ID_CHROME_INTERNAL);
}

void AtriumBrowserWindow::OnVimModeDrained(base::Value mode) {
  if (mode.is_string()) {
    CurrentVimPageNormalMode() = mode.GetString() == "NORMAL";
  }
}
```

Declare `SendVimKey`, `OnVimKeyRan`, `DrainVimMode`, and `OnVimModeDrained` in the private section of `atrium_browser_window.h` next to the marginalia declarations, and add the public testing seams next to `InstallVimOnActiveTabForTesting`:

```cpp
  void SendVimKeyForTesting(const std::string& character) {
    SendVimKey(character);
  }
  void DrainVimModeForTesting() { DrainVimMode(); }
  content::KeyboardEventProcessingResult RouteVimKeyForTesting(
      const std::string& character);
```

- [ ] **Step 4: Replace the PreHandleKeyboardEvent stub**

In `atrium_browser_window.cc` at the stub (around line 10913):

```cpp
content::KeyboardEventProcessingResult
AtriumBrowserWindow::PreHandleKeyboardEvent(
    const input::NativeWebKeyboardEvent& event) {
  atrium::VimKeyInputs in;
  in.vim_enabled = CurrentVimModeEnabled();
  in.page_in_normal_mode = CurrentVimPageNormalMode();
  in.is_key_down =
      event.GetType() == input::NativeWebKeyboardEvent::Type::kRawKeyDown ||
      event.GetType() == input::NativeWebKeyboardEvent::Type::kKeyDown;
  in.command = (event.GetModifiers() & blink::WebInputEvent::kMetaKey) != 0;
  in.control = (event.GetModifiers() & blink::WebInputEvent::kControlKey) != 0;
  in.option = (event.GetModifiers() & blink::WebInputEvent::kAltKey) != 0;
  in.shift = (event.GetModifiers() & blink::WebInputEvent::kShiftKey) != 0;
  in.character = VimCharacterOf(event);

  if (atrium::RouteVimKey(in) != atrium::VimKeyRoute::kStealForVim) {
    return content::KeyboardEventProcessingResult::NOT_HANDLED;
  }
  SendVimKey(in.character);
  return content::KeyboardEventProcessingResult::HANDLED;
}

content::KeyboardEventProcessingResult
AtriumBrowserWindow::RouteVimKeyForTesting(const std::string& character) {
  atrium::VimKeyInputs in;
  in.vim_enabled = CurrentVimModeEnabled();
  in.page_in_normal_mode = CurrentVimPageNormalMode();
  in.is_key_down = true;
  in.character = character;
  return atrium::RouteVimKey(in) == atrium::VimKeyRoute::kStealForVim
             ? content::KeyboardEventProcessingResult::HANDLED
             : content::KeyboardEventProcessingResult::NOT_HANDLED;
}
```

Add the `VimCharacterOf` helper in the anonymous namespace near `ShouldHandleKeyboardEvent` (around line 2879):

```cpp
// The character the key produces. event.text carries it already decoded, so
// this stays correct on a non-QWERTY layout the way IsQuitChord does.
std::string VimCharacterOf(const input::NativeWebKeyboardEvent& event) {
  if (event.text[0] == 0) {
    return std::string();
  }
  return base::UTF16ToUTF8(std::u16string(1, event.text[0]));
}
```

Add `#include "chrome/browser/ui/atrium/atrium_vim_key_decision.h"` and `":vim_key_decision"` to the deps of the target compiling `atrium_browser_window.cc`.

- [ ] **Step 5: Drain the mode on input**

In `ActiveTabObserver::DidGetUserInteraction`, next to the marginalia drain:

```cpp
      window_->DrainMarginaliaMarkClicks();
      window_->DrainVimMode();
```

- [ ] **Step 6: Run to verify it passes**

```bash
make overlay-sync
make vm-run-gtest BIN=browser_tests FILTER=AtriumVimModeBrowserTest.*
```

Expected: 7 tests, 7 pass. Reconcile the counts and grep the log for `ERROR:CONSOLE`.

- [ ] **Step 7: Prove the veto test can fail**

Delete the `if (mode() === 'INSERT')` guard from `vim-page.js`, rebuild, rerun, and confirm `TypingIntoAProgrammaticallyFocusedFieldIsNotStolen` fails by name. Restore the guard with an editor. An absence assertion with no matching presence assertion beside it proves nothing.

- [ ] **Step 8: Run the whole gate**

```bash
make webui-test
make rust-test
make lint
~/chromium-atrium/src/out/atrium/unit_tests --gtest_filter=AtriumVim*
```

All four must pass before this task is done.

- [ ] **Step 9: Commit**

```bash
git add overlay/chrome/browser/ui/atrium/atrium_browser_window.h \
        overlay/chrome/browser/ui/atrium/atrium_browser_window.cc \
        overlay/chrome/browser/ui/atrium/atrium_vim_mode_browsertest.cc \
        overlay/chrome/browser/ui/atrium/BUILD.gn
git commit -m "feat(vim): steal normal-mode keys and run their verbs in the page"
```

---

### Task 7: Measure the injection delay and close the guard gap

Two loose ends the spec named. Neither changes behaviour, both change what the next stage can rely on.

**Files:**
- Modify: `docs/vim-mode-design.md`
- Modify: `build/scripts/verify-injected-js.py`
- Modify: `overlay/chrome/browser/ui/atrium/atrium_vim_mode_browsertest.cc`

**Interfaces:**
- Consumes: everything above.
- Produces: a recorded answer for V2, which needs hints live as early as possible.

- [ ] **Step 1: Write the measurement test**

Append to `atrium_vim_mode_browsertest.cc`:

```cpp
// V2's hints need vim live as early as possible, so record which hook
// wins. This asserts the hook we chose actually produces a live document -
// the measured number goes in docs/vim-mode-design.md, not in an assertion.
IN_PROC_BROWSER_TEST_F(AtriumVimModeBrowserTest,
                       TheInstallHookSeesALiveDocument) {
  NavigateAndWait("a.test", "/title1.html");

  EXPECT_EQ("complete", content::EvalJs(ActiveContents(),
                                        "document.readyState"));
  EXPECT_EQ(true, content::EvalJs(ActiveContents()->GetPrimaryMainFrame(),
                                  "!!window.__atriumVim",
                                  content::EXECUTE_SCRIPT_DEFAULT_OPTIONS,
                                  ISOLATED_WORLD_ID_CHROME_INTERNAL));
}
```

- [ ] **Step 2: Compare the two candidate hooks**

Run the suite with `InstallVimOnActiveTab` called from `DocumentOnLoadCompletedInPrimaryMainFrame`, then move the call to `PrimaryMainDocumentElementAvailable` and run again. For each, log `document.readyState` at install time and whether `document.body` exists.

```bash
make vm-run-gtest BIN=browser_tests FILTER=AtriumVimModeBrowserTest.TheInstallHookSeesALiveDocument
```

Keep whichever hook installs earliest while still finding a `document.body` on every run. If `PrimaryMainDocumentElementAvailable` is flaky, keep the load-complete hook.

- [ ] **Step 3: Record the answer**

Replace the "Injection timing" section of `docs/vim-mode-design.md` with what you measured: which hook is used, what `document.readyState` is when it fires, whether `document.body` exists, and what that means for V2's hints. Delete the open question - a spec that still asks a question the code has answered is wrong, not incomplete.

- [ ] **Step 4: Extend the injected-JS guard**

`build/scripts/verify-injected-js.py` only scans `.cc`, `.mm`, and `.h`, so `vim-page.js` escapes the one invariant it must satisfy: injected JS must survive a document with no root element. Add the resources directory and the `.js` suffix.

In `SRC_ROOTS`, add `"chrome/browser/resources/atrium"`. In `offenders`, widen the suffix check to include `.js`, and keep the existing `browsertest`/`unittest` skip.

Only files that are actually injected belong under the guard. Restrict the `.js` scan to a named list so the WebUI modules, which always run against a committed document, are not swept in:

```python
INJECTED_JS = {"vim-page.js"}
```

and skip any `.js` file whose name is not in that set.

- [ ] **Step 5: Prove the guard can fail**

```bash
make injected-js-verify
```

Expected: PASS. Then add `document.documentElement.style.color = 'red';` to `vim-page.js`, sync, and rerun. Expected: FAIL naming the file and line. Remove the line with an editor and confirm PASS again. A guard that has never failed has not been tested.

- [ ] **Step 6: Commit**

```bash
git add docs/vim-mode-design.md \
        build/scripts/verify-injected-js.py \
        overlay/chrome/browser/ui/atrium/atrium_vim_mode_browsertest.cc
git commit -m "test(vim): record the install hook and guard the injected script"
```

---

## Definition of done

Every acceptance criterion has a test that fails when its hunk is reverted:

| AC | Proven by |
|---|---|
| Injection reaches main frame and cross-origin subframes | `InstallsInTheMainFrame`, `InstallsInACrossOriginSubframe` |
| j k d u gg G scroll while NORMAL | `vim-page-test.mjs` motion tests, `JScrollsARealPage` |
| Typing into a focused input is never stolen | `TypingIntoAClickedFieldIsNotStolen`, `TypingIntoAProgrammaticallyFocusedFieldIsNotStolen`, the `key()` veto test |
| Mode indicator tracks focus | `the indicator shows the current mode and follows focus` |
| Toggle persists, defaults off, off is a no-op | Rust load and write-back tests, settings-test.mjs pair, `TheToggleOffPassesEveryKey` |
| Earliest reliable hook measured and recorded | `TheInstallHookSeesALiveDocument` plus the rewritten spec section |
| Node, C++ unit, and browsertest coverage | `make webui-test`, `unit_tests --gtest_filter=AtriumVim*`, `vm-run-gtest` |

Final gate, all four green, read in full rather than by exit code:

```bash
make webui-test
make rust-test
make lint
make vm-run-gtest BIN=browser_tests FILTER=AtriumVimModeBrowserTest.*
```
