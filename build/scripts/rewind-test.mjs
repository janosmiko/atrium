#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Rewind's snapshot ring, scrubber summary and scoped restore.
//
//   make rewind-test
//
// Ported from the lab's Rewind (docs/lab/swipe-lab.html:1602-1677). The lab
// keeps its own stores, so it can snapshot on every structural gesture. Atrium
// gets the sidebar snapshot instead, so the ring dedupes on a structural
// digest: an update that changes nothing structural is not a moment.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const MODULE = path.join(SRC, 'chrome/browser/resources/atrium/rewind.js');
const {
  MAX_MOMENTS,
  structuralDigest,
  captureMoment,
  defaultScope,
  scopeChoices,
  momentSummary,
  restoreSnapshot,
} = await import(MODULE);

const row = (id, title, extra = {}) =>
    ({id, title, color: '#888', selected: false, url: 'https://x/' + id, ...extra});

const space = (name, tabs, extra = {}) => ({
  name,
  color: '#3355ff',
  icon: name[0],
  tabs,
  pinned: [],
  folders: [],
  favorites: [],
  ...extra,
});

const base = () => ({
  activeSpace: 1,
  spaces: [
    space('Work', [row('1', 'a'), row('2', 'b')]),
    space('Home', [row('7', 'g', {selected: true})]),
  ],
});

// ---- structural changes produce moments in a bounded ring ----

test('an update that changes nothing structural is not a new moment', () => {
  const first = captureMoment([], base(), 1000);
  const again = captureMoment(first, base(), 2000);
  assert.equal(first.length, 1);
  assert.equal(again.length, 1);
  assert.equal(again[0].at, 1000);
});

test('cosmetic churn is not structure', () => {
  const tinted = base();
  tinted.spaceTint = '#ff0000';
  tinted.spaces[0].color = '#000000';
  assert.equal(structuralDigest(tinted), structuralDigest(base()));
});

test('open, close, move, pin, fold and rename each make a moment', () => {
  const changes = {
    open: s => s.spaces[0].tabs.push(row('9', 'new')),
    close: s => s.spaces[0].tabs.pop(),
    move: s => s.spaces[0].tabs.reverse(),
    pin: s => s.spaces[0].pinned.push(s.spaces[0].tabs.shift()),
    fold: s => s.spaces[0].folders.push({id: 'f', name: 'F', closed: true, tabs: []}),
    rename: s => (s.spaces[0].tabs[0].title = 'renamed'),
    select: s => (s.spaces[0].tabs[0].selected = true),
    switchSpace: s => (s.activeSpace = 0),
  };
  for (const [what, mutate] of Object.entries(changes)) {
    const next = base();
    mutate(next);
    const log = captureMoment(captureMoment([], base(), 1), next, 2);
    assert.equal(log.length, 2, what + ' should have made a moment');
  }
});

test('rename, reorder or navigate inside a pinned folder each make a moment', () => {
  const withFolder = () => {
    const s = base();
    s.spaces[0].pinned = [{
      id: 'f1',
      kind: 'folder',
      name: 'Reading',
      closed: false,
      tabs: [row('p1', 'first'), row('p2', 'second')],
    }];
    return s;
  };
  const changes = {
    rename: s => (s.spaces[0].pinned[0].tabs[0].title = 'renamed'),
    reorder: s => s.spaces[0].pinned[0].tabs.reverse(),
    navigate: s => (s.spaces[0].pinned[0].tabs[0].url = 'https://x/moved'),
  };
  for (const [what, mutate] of Object.entries(changes)) {
    const next = withFolder();
    mutate(next);
    const log = captureMoment(captureMoment([], withFolder(), 1), next, 2);
    assert.equal(
        log.length, 2, what + ' inside a pinned folder should have made a moment');
  }
});

test('the ring is bounded and keeps the newest moments', () => {
  let log = [];
  for (let i = 0; i < MAX_MOMENTS + 25; i++) {
    const snapshot = base();
    snapshot.spaces[0].tabs.push(row('t' + i, 'tab ' + i));
    log = captureMoment(log, snapshot, 1000 + i);
  }
  assert.equal(log.length, MAX_MOMENTS);
  assert.equal(log[log.length - 1].at, 1000 + MAX_MOMENTS + 24);
  assert.equal(log[0].at, 1025);
});

test('capture returns a new array and never mutates the one it was given', () => {
  const log = captureMoment([], base(), 1000);
  const next = base();
  next.activeSpace = 0;
  const grown = captureMoment(log, next, 2000);
  assert.equal(log.length, 1);
  assert.notEqual(grown, log);
});

// ---- the scrubber previews time, tab counts and scope ----

test('scope defaults to the space you are in, and to the window in Overview', () => {
  assert.equal(defaultScope(base()), '1');
  const overview = base();
  overview.spaces[overview.activeSpace].overview = true;
  assert.equal(defaultScope(overview), 'all');
});

test('the scope picker offers the whole window and every real space', () => {
  const withOverview = base();
  withOverview.spaces.push(space('Overview', [], {overview: true}));
  assert.deepEqual(scopeChoices(withOverview), [
    {value: 'all', label: 'Whole window'},
    {value: '0', label: 'Work'},
    {value: '1', label: 'Home'},
  ]);
});

test('a whole-window summary counts every space and names where you would land', () => {
  const [moment] = captureMoment([], base(), Date.UTC(2026, 0, 2, 3, 4, 5));
  const summary = momentSummary(moment, 'all');
  assert.equal(summary.tabs, 3);
  assert.equal(summary.scopeLabel, 'Home');
  assert.match(summary.text, /3 open tabs/);
  assert.match(summary.text, /in Home$/);
  assert.equal(typeof summary.time, 'string');
  assert.ok(summary.time.length > 0);
});

test('a per-space summary counts only that space and names it', () => {
  const [moment] = captureMoment([], base(), 1000);
  const summary = momentSummary(moment, '0');
  assert.equal(summary.tabs, 2);
  assert.equal(summary.scopeLabel, 'Work');
  assert.match(summary.text, /2 tabs in Work$/);
  assert.match(momentSummary(moment, '1').text, /1 tab in Home$/);
});

test('a summary counts pinned rows and folder tabs, not just loose ones', () => {
  const rich = base();
  rich.spaces[0].pinned.push(row('p1', 'mail'));
  rich.spaces[0].folders.push({id: 'f', name: 'F', closed: false, tabs: [row('d1', 'doc')]});
  const [moment] = captureMoment([], rich, 1000);
  assert.equal(momentSummary(moment, '0').tabs, 4);
  assert.equal(momentSummary(moment, 'all').tabs, 5);
});

// ---- whole-window vs per-space restore ----

test('a whole-window restore reinstates tabs, selection and the active space', () => {
  const [moment] = captureMoment([], base(), 1000);
  const now = base();
  now.activeSpace = 0;
  now.spaces[0].tabs = [];
  now.spaces[1].tabs = [row('99', 'later'), row('98', 'later too')];
  const restored = restoreSnapshot(moment, now, 'all');
  assert.equal(restored.activeSpace, 1);
  assert.deepEqual(restored.spaces[0].tabs.map(t => t.id), ['1', '2']);
  assert.deepEqual(restored.spaces[1].tabs.map(t => t.id), ['7']);
  assert.equal(restored.spaces[1].tabs[0].selected, true);
});

test('a per-space restore touches only that space and lands you in it', () => {
  const [moment] = captureMoment([], base(), 1000);
  const now = base();
  now.activeSpace = 1;
  now.spaces[0].tabs = [];
  now.spaces[1].tabs = [row('99', 'later')];
  const restored = restoreSnapshot(moment, now, '0');
  assert.deepEqual(restored.spaces[0].tabs.map(t => t.id), ['1', '2']);
  // Home is left exactly as the user has it now, not as the moment saw it.
  assert.deepEqual(restored.spaces[1].tabs.map(t => t.id), ['99']);
  assert.equal(restored.activeSpace, 0);
});

test('a restore refreshes the top-level rows the active pane actually reads', () => {
  // sidebar-render-test.mjs: the ACTIVE pane draws the snapshot's top-level
  // fields, so a restore that only rewrote spaces[] would leave it stale.
  const [moment] = captureMoment([], base(), 1000);
  const now = base();
  now.activeSpace = 0;
  now.spaces[0].tabs = [];
  now.spaceName = 'Work';
  now.tabs = [];
  const restored = restoreSnapshot(moment, now, 'all');
  assert.equal(restored.spaceName, 'Home');
  assert.deepEqual(restored.tabs.map(t => t.id), ['7']);
  assert.equal(restored.tabs, restored.spaces[1].tabs);
});

test('a per-space restore of a space the moment never saw is refused, not a wipe', () => {
  const [moment] = captureMoment([], base(), 1000);
  const now = base();
  now.spaces.push(space('New', [row('50', 'fresh')]));
  const restored = restoreSnapshot(moment, now, '2');
  assert.equal(restored, null);
});

test('restore never mutates the current snapshot or the stored moment', () => {
  const [moment] = captureMoment([], base(), 1000);
  const frozen = JSON.stringify(moment);
  const now = base();
  now.spaces[0].tabs = [];
  const before = JSON.stringify(now);
  restoreSnapshot(moment, now, 'all');
  assert.equal(JSON.stringify(now), before);
  assert.equal(JSON.stringify(moment), frozen);
});

test('an out-of-range or missing moment restores nothing', () => {
  assert.equal(restoreSnapshot(null, base(), 'all'), null);
  assert.equal(restoreSnapshot(undefined, base(), '0'), null);
});

// ---- local-only ----

test('the module cannot send a moment anywhere', () => {
  const source = fs.readFileSync(MODULE, 'utf8');
  for (const escape of
           ['fetch(', 'XMLHttpRequest', 'sendBeacon', 'WebSocket', 'chrome.send',
            'import(', 'localStorage', 'indexedDB']) {
    assert.equal(source.includes(escape), false, 'rewind.js must not use ' + escape);
  }
});

test('the ring lives in the caller, not in module state', () => {
  const a = captureMoment([], base(), 1000);
  const other = base();
  other.activeSpace = 0;
  const b = captureMoment([], other, 2000);
  assert.equal(a.length, 1);
  assert.equal(b.length, 1);
  assert.notEqual(a[0].digest, b[0].digest);
});
