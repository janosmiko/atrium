#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');

const h = installDOM({pathname: '/settings'});
const {installSettings} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/settings.js'));

const CURSOR = 'atrium-vim-cursor';

// The settings the browser pushes carry the toggle, so this is also how the
// window learns vim is on.
function settingsWith(vim) {
  h.reset();
  const root = document.createElement('div');
  h.body.appendChild(root);
  installSettings(root);
  window.atriumSetSettings({vimModeEnabled: vim});
  return root;
}

const press = (key) =>
    document.dispatchEvent(new Event('keydown', {bubbles: true, key}));

const cursorAt = (root) =>
    [...root.querySelectorAll('.settings-row')].findIndex(
        (r) => r.classList.contains(CURSOR));

test('vim off leaves the rows alone', () => {
  const root = settingsWith(false);
  press('j');
  assert.equal(cursorAt(root), -1, 'no row may carry a cursor with the mode off');
});

test('vim on moves a cursor down the rows', () => {
  const root = settingsWith(true);
  assert.ok(root.querySelectorAll('.settings-row').length > 2, 'setup');

  press('j');
  assert.equal(cursorAt(root), 1, 'j moves down from the first row');
  press('j');
  assert.equal(cursorAt(root), 2);
  press('k');
  assert.equal(cursorAt(root), 1);
});

test('the toggle takes effect without a reload', () => {
  const root = settingsWith(false);
  press('j');
  assert.equal(cursorAt(root), -1, 'setup: off means off');

  window.atriumSetSettings({vimModeEnabled: true});
  press('j');
  assert.ok(cursorAt(root) >= 0, 'the push has to reach the surface live');
});

test('a focused field types rather than moving the cursor', () => {
  const root = settingsWith(true);
  press('j');
  const before = cursorAt(root);
  const field = document.createElement('input');
  root.appendChild(field);
  document.activeElement = field;

  press('j');
  assert.equal(cursorAt(root), before,
               'a letter typed into a settings field must not move the cursor');
});
