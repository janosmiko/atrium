#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the sidebar is app chrome, so the pointer stays an arrow and
// nothing is selectable. The defaults used to hang off .sidebar, which the
// Overview rail pane is not inside.

//   node --test build/scripts/chrome-cursor-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

const read = (name) =>
    fs.readFileSync(path.join(ATRIUM_DIR, name), 'utf8');

// Every stylesheet the sidebar and its surfaces load. courtyard.css and
// theme-studio.css are left out: those are their own windows, and their
// rooms and swatches are drag affordances that want their own pointers.
// devbar.css is a developer tool, not shipped chrome.
const CHROME_SHEETS = [
  'ai-panel.css',
  'archive-fly.css',
  'commandbar.css',
  'edge-glow.css',
  'find.css',
  'foyer.css',
  'hint.css',
  'notes.css',
  'overlay.css',
  'overview.css',
  'peek-controls.css',
  'permsheet.css',
  'quickmenu.css',
  'settings.css',
  'sidebar-ext-strip.css',
  'sidebar-nav-row.css',
  'sidebar.css',
  'skylight-ambient.css',
  'theme-window.css',
  'toast-stack.css',
];

test('the document defaults to the arrow and to unselectable text', () => {
  const rule = read('overlay.css').match(/\bbody\s*\{[^}]*\}/)[0];
  assert.match(rule, /cursor:\s*default/);
  assert.match(rule, /user-select:\s*none/);
});

test('buttons and links take that default rather than their own', () => {
  const css = read('overlay.css');
  const rule = css.match(/\nbutton,[^{]*\{[^}]*\}/)[0];
  assert.match(rule, /cursor:\s*inherit/);
  assert.match(rule, /\[role="button"\]/);
});

// A readonly one is excluded: the address row is an input the user can only
// click, so a caret over it offers typing it refuses.
test('a field keeps its caret and its selection', () => {
  const rule = read('overlay.css').match(/\ninput:not[^{]*\{[^}]*\}/)[0];
  assert.match(rule, /cursor:\s*auto/);
  assert.match(rule, /user-select:\s*text/);
  assert.match(rule, /input:not\(\[readonly\]\)/);
});

// The Overview categories, the space cards and the archived rows were the
// three reported sites. Guarding every sheet keeps the fourth from landing.
test('no chrome stylesheet asks for the hand pointer', () => {
  const offenders = CHROME_SHEETS.filter(
      (name) => /cursor:\s*pointer/.test(read(name)));
  assert.deepEqual(offenders, []);
});
