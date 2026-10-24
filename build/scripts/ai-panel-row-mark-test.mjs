#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// which tab is carrying a conversation, without opening each one.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');

const h = installDOM({pathname: '/sidebar'});
const {installSidebar} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/sidebar.js'));

function fresh() {
  h.reset();
  const root = document.createElement('div');
  h.body.appendChild(root);
  installSidebar(root);
  return root;
}

const SNAPSHOT = {
  spaces: [{name: 'Work', emoji: 'W', tint: '#333'}],
  activeSpace: 0,
  tabs: [{id: '11', title: 'One', url: 'https://one.test/'},
         {id: '22', title: 'Two', url: 'https://two.test/'}],
  pinned: [],
  folders: [],
  favorites: [],
};

const marked = (root) =>
    [...root.querySelectorAll('.tabrow.aimark')].map((r) => r.dataset.tabId);

test('the receiver lands on window under the name C++ calls', () => {
  fresh();
  // PushAiPanelRow executes the literal "window.atriumSetAiPanelRow(...)", so
  // a rename here breaks the push with no error on either side.
  assert.equal(typeof h.window.atriumSetAiPanelRow, 'function');
});

test('the tab the panel is open for carries the marker', () => {
  const root = fresh();
  h.window.atriumSetSidebar(SNAPSHOT);
  assert.deepEqual(marked(root), [], 'nothing is marked before the panel opens');

  h.window.atriumSetAiPanelRow('22');
  assert.deepEqual(marked(root), ['22']);
});

test('closing the panel clears the marker', () => {
  const root = fresh();
  h.window.atriumSetSidebar(SNAPSHOT);
  h.window.atriumSetAiPanelRow('22');

  h.window.atriumSetAiPanelRow('');
  assert.deepEqual(marked(root), []);
});

test('re-targeting moves the marker rather than adding one', () => {
  const root = fresh();
  h.window.atriumSetSidebar(SNAPSHOT);
  h.window.atriumSetAiPanelRow('22');

  h.window.atriumSetAiPanelRow('11');
  assert.deepEqual(marked(root), ['11'],
                   'the tab the panel left is still marked');
});

test('the marker survives a rebuild', () => {
  const root = fresh();
  h.window.atriumSetSidebar(SNAPSHOT);
  h.window.atriumSetAiPanelRow('22');

  // apply() replaces every row, which is what a favicon push or a reorder
  // does. A marker applied only to the live nodes is gone by here.
  h.window.atriumSetSidebar(SNAPSHOT);
  assert.deepEqual(marked(root), ['22']);
});

test('a marker for a tab this window does not have marks nothing', () => {
  const root = fresh();
  h.window.atriumSetSidebar(SNAPSHOT);

  h.window.atriumSetAiPanelRow('999');
  assert.deepEqual(marked(root), []);
});

test('the marker takes no room and follows the theme', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  const rule = /\.row\.tabrow\.aimark::after\s*{([^}]*)}/.exec(css);
  assert.ok(rule, 'no rule paints the marker');
  assert.match(rule[1], /position:\s*absolute/,
               'a marker in the flow moves the title when it appears');
  // --ringb is redefined per theme, so a literal colour here would be
  // invisible on one of them.
  assert.match(rule[1], /var\(--ringb/);
});
