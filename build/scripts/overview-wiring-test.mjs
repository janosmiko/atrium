#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Overview is a pane of the sidebar's rail, not its own route in
// its own window. Both shipped at once for four weeks, so the retired route
// being gone is pinned here too.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

const dom = installDOM();
const {buildRail} = await import(path.join(ATRIUM_DIR, 'sidebar.js'));

const SNAPSHOT = {
  activeSpace: 0,
  spaceName: 'Home',
  spaces: [{name: 'Home', color: '#0f0', tabs: [], pinned: []}],
  tabs: [],
  pinned: [],
};

test('the retired /overview route is gone from atrium.js', () => {
  const atriumSrc = fs.readFileSync(path.join(ATRIUM_DIR, 'atrium.js'), 'utf8');
  assert.ok(!atriumSrc.includes('installOverview'),
      'a live /overview route means the widget surface can come back');
  assert.ok(!/'\/overview'/.test(atriumSrc),
      'the /overview route must not be registered');
});

test('the sidebar is what imports overview.js now', () => {
  const sidebarSrc =
      fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar.js'), 'utf8');
  assert.match(sidebarSrc, /import \{renderOverview\} from '\.\/overview\.js'/,
      'sidebar.js must import renderOverview directly');
});

test('buildRail puts Overview in the pane before the first space', () => {
  const rail = buildRail(SNAPSHOT);
  const first = rail.children[0];
  assert.ok(first.className.includes('ovrailpane'),
      'Overview must be the leading pane');
  // No spaceIndex, so a drag started here resolves to no space rather than 0.
  assert.equal(first.dataset.spaceIndex, undefined);

  const buttons = first.querySelectorAll('.ovbtn');
  assert.equal(buttons.length, 6, 'the rail has six sections');
  assert.deepEqual(buttons.map((b) => b.textContent),
      ['Downloads', 'Spaces', 'Archived', 'Snoozed', 'Shelf', 'Notes']);
  assert.ok(first.querySelector('.ovpane'), 'a content pane must exist');
});

// The handshake that makes the pane ask for data. Source-matched because the
// send comes from the swipe SETTLE, which no shim drives - the end-to-end
// proof is scenario overview-is-a-rail-pane's vlog assertion.
test('the settle announces the pane so native can push its lists', () => {
  const sidebarSrc =
      fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar.js'), 'utf8');
  assert.match(sidebarSrc,
      /onLeadingPane: \(\) => \{[^}]*chrome\.send\('sidebarOverviewPaneRendered'/,
      'without this the pane renders empty');
});

test('native can reach the pane, which is not a space', () => {
  const sidebarSrc =
      fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar.js'), 'utf8');
  assert.match(sidebarSrc, /window\.atriumSwipeToOverview = /,
      'the hotkeys and the leading dot have no other way in');
});

dom.reset();
