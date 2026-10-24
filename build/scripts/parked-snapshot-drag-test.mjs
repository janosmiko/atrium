#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// A snapshot parked during a row drag stays parked when a glide ends.
//
//   node --test build/scripts/parked-snapshot-drag-test.mjs
//
// Loads the overlay copy of sidebar.js, so it tests the file being edited.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {installDOM, Event} from './dom-shim.mjs';

const h = installDOM({pathname: '/sidebar'});
const REPO = path.join(path.dirname(fileURLToPath(import.meta.url)), '../..');
const {installSidebar} = await import(path.join(
    REPO, 'overlay/chrome/browser/resources/atrium/sidebar.js'));

const SNAPSHOT = {
  spaceName: 'Work',
  spaceTint: '#4488cc',
  favorites: [],
  pinned: [],
  folders: [],
  tabs: [{id: 't1', title: 'First', url: 'https://one.example/'}],
  activeSpace: 0,
  spaces: [{name: 'Work', color: '#48c'}, {name: 'Play', color: '#c84'}],
};
const NEXT = {
  ...SNAPSHOT,
  tabs: [{id: 't9', title: 'Echoed', url: 'https://echo.example/'}],
};

function runFrames(limit = 400) {
  for (let i = 0; h.pendingFrames && i < limit; i++) {
    h.tick();
  }
}

test('a glide ending mid row drag does not rebuild the tree until the drag ends', () => {
  h.reset();
  const root = document.createElement('div');
  h.body.appendChild(root);
  installSidebar(root);
  h.window.atriumSetSidebar(SNAPSHOT);

  const row = root.querySelector('.tabrow[data-tab-id="t1"]');
  row.dispatchEvent(new Event('dragstart', {bubbles: true}));
  h.window.atriumSwipeTo(1);
  h.window.atriumSetSidebar(NEXT);
  runFrames();

  assert.equal(root.querySelector('.tabrow[data-tab-id="t9"]'), null,
               'the idle callback applied a snapshot under the live drag');
  assert.ok(root.querySelector('.tabrow[data-tab-id="t1"]'),
            'the dragged row was detached');

  row.dispatchEvent(new Event('dragend', {bubbles: true}));
  assert.ok(root.querySelector('.tabrow[data-tab-id="t9"]'),
            'the parked snapshot was lost after the drag');
});
