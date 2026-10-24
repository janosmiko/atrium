#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// wires Rewind's restore action to sidebarRestoreSlice.
//   make webui-test
// The send is exercised only through the real route and a real Restore
// button click, rewind-bar-test.mjs's own pattern - not the sender directly.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');
const MODULE = path.join(ATRIUM_DIR, 'rewind.js');
const {captureMoment, restorePayload} = await import(MODULE);

const row = (id, title, extra = {}) =>
    ({id, title, url: 'https://x/' + id, selected: false, ...extra});

const space = (name, tabs, extra = {}) => ({
  name,
  tabs,
  pinned: [],
  folders: [],
  favorites: [],
  pinnedCollapsed: false,
  ...extra,
});

const twoSpaces = () => ({
  activeSpace: 0,
  spaces: [
    space('Work', [row('1', 'a'), row('2', 'b')]),
    space('Home', [row('7', 'g')]),
  ],
});

// ---- restorePayload: shape, isolation, refusal (unit level) ----

test('a whole-window payload carries every space the moment held', () => {
  const [moment] = captureMoment([], twoSpaces(), 1000);
  const now = twoSpaces();
  const payload = restorePayload(moment, now, 'all');
  assert.equal(payload.scope, 'all');
  assert.equal(payload.activeSpace, 0);
  assert.deepEqual(payload.spaces.map((s) => s.index), [0, 1]);
  assert.deepEqual(payload.spaces[0].tabs.map((t) => t.id), ['1', '2']);
  assert.deepEqual(payload.spaces[1].tabs.map((t) => t.id), ['7']);
});

test('a per-space payload carries exactly one slice, at the chosen index, ' +
         'and no other space\'s data',
     () => {
       const [moment] = captureMoment([], twoSpaces(), 1000);
       const now = twoSpaces();
       // Home has a tab the moment never saw - if it leaked into the payload,
       // its id would show up in the JSON below.
       now.spaces[1].tabs.push(row('99', 'leaked-if-broken'));

       const payload = restorePayload(moment, now, '0');
       assert.equal(payload.scope, '0');
       assert.equal(payload.spaces.length, 1);
       assert.equal(payload.spaces[0].index, 0);
       assert.equal(JSON.stringify(payload).includes('99'), false,
           'the untouched space\'s tab id must not appear in the sent payload');
     });

// MUTATION PROOF: forcing restorePayload's `indices` to always take the
// whole-window branch (dropping the `perSpace ?` split) makes the assertion
// above fail, since twoSpaces()'s saved state holds Home too.

test('restoring a space the moment never held is refused: no payload, so ' +
         'nothing can be sent',
     () => {
       const [moment] = captureMoment([], twoSpaces(), 1000);
       const now = twoSpaces();
       now.spaces.push(space('New', [row('50', 'fresh')]));
       assert.equal(restorePayload(moment, now, '2'), null);
     });

test('a pinned folder flattens to member rows the payload can carry', () => {
  const withFolder = twoSpaces();
  withFolder.spaces[0].pinned = [{
    id: 'f1',
    kind: 'folder',
    name: 'Reading',
    closed: false,
    tabs: [row('p1', 'first'), row('p2', 'second')],
  }];
  const [moment] = captureMoment([], withFolder, 1000);
  const payload = restorePayload(moment, withFolder, 'all');
  const pinned = payload.spaces[0].pinned;
  assert.equal(pinned.length, 2);
  assert.ok(
      pinned.every((p) => typeof p.url === 'string' && p.url.length > 0),
      'every pinned row must carry a url the core can parse');
  assert.deepEqual(pinned.map((p) => p.id), ['p1', 'p2']);
});

test('no moment, no current: refused', () => {
  assert.equal(restorePayload(null, twoSpaces(), 'all'), null);
  assert.equal(restorePayload({state: twoSpaces()}, null, '0'), null);
});

// ---- End-to-end: real route, real right-click, real Restore button ----

let instance = 0;
async function route(pathname) {
  const handle = installDOM({pathname});
  const root = document.createElement('div');
  root.setAttribute('id', 'atrium-root');
  handle.body.appendChild(root);
  await import(
      path.join(ATRIUM_DIR, 'atrium.js') + `?rewind-restore-route=${instance++}`);
  clearInterval(root.querySelector('.bglayer')?._skyClockHandle);
  return {handle, root};
}

function fullSnapshot(activeSpace, spaces) {
  const active = spaces[activeSpace];
  return {
    activeSpace,
    spaces,
    spaceName: active.name,
    tabs: active.tabs,
    pinned: active.pinned,
    folders: active.folders,
    favorites: active.favorites,
    pinnedCollapsed: !!active.pinnedCollapsed,
  };
}

const fire = (node, type, init = {}) => {
  const event = new Event(type, {bubbles: true, ...init});
  node.dispatchEvent(event);
  return event;
};
const rightClick = (node) => fire(node, 'contextmenu', {clientX: 10, clientY: 10});
const clickCtxItem = (root, label) =>
    [...root.querySelectorAll('.ctxitem')].find((el) => el.textContent === label).click();

test('clicking Restore sends sidebarRestoreSlice with the payload the ' +
         'chosen moment and scope compute',
     async () => {
       const {handle, root} = await route('/sidebar');
       const spaces = [
         space('Work', [{id: 't1', title: 'First', url: 'https://one.example/'}]),
       ];
       handle.window.atriumSetSidebar(fullSnapshot(0, spaces));

       rightClick(root);
       clickCtxItem(root, 'Rewind');
       handle.sent.length = 0;
       root.querySelector('.rwgo').click();

       const sent = handle.sent.filter((m) => m[0] === 'sidebarRestoreSlice');
       assert.equal(sent.length, 1);
       const payload = JSON.parse(sent[0][1][0]);
       assert.equal(payload.scope, '0');
       assert.equal(payload.spaces.length, 1);
       assert.equal(payload.spaces[0].index, 0);
       assert.deepEqual(payload.spaces[0].tabs.map((t) => t.id), ['t1']);
     });

// a restore said nothing, so it was indistinguishable from a
// click that missed. The toast waits for the browser to confirm, because a
// restore the core refuses must show nothing at all.
test('the toast appears only once the browser confirms the restore', async () => {
  const {handle, root} = await route('/sidebar');
  handle.window.atriumSetSidebar(fullSnapshot(
      0, [space('Work', [{id: 't1', title: 'First', url: 'https://one.example/'}])]));

  rightClick(root);
  clickCtxItem(root, 'Rewind');
  root.querySelector('.rwgo').click();
  assert.equal(root.querySelector('.toast'), null,
               'the click alone must not claim success');

  handle.window.atriumRewindRestored();
  const toast = root.querySelector('.toast');
  assert.ok(toast, 'no toast after the browser confirmed');
  assert.match(toast.textContent, /Rewound/);
});

test('the toast names the scope that was restored', async () => {
  // 'Whole window' and a space name are the two the picker offers, and the
  // sentence is the only thing telling the user which one it took.
  const {handle, root} = await route('/sidebar');
  handle.window.atriumSetSidebar(fullSnapshot(
      0, [space('Work', [{id: 't1', title: 'First', url: 'https://one.example/'}])]));

  rightClick(root);
  clickCtxItem(root, 'Rewind');
  root.querySelector('.rwgo').click();
  handle.window.atriumRewindRestored();
  assert.match(root.querySelector('.toast').textContent, /Work/);
});

test('a confirmation with no restore behind it shows nothing', async () => {
  // The refusal path: RestoreSidebarSlice returns before confirming, so this
  // entrypoint must be inert when no Restore was pressed - otherwise a stray
  // call would toast a rewind that never happened.
  const {handle, root} = await route('/sidebar');
  handle.window.atriumSetSidebar(fullSnapshot(
      0, [space('Work', [{id: 't1', title: 'First', url: 'https://one.example/'}])]));

  handle.window.atriumRewindRestored();
  assert.equal(root.querySelector('.toast'), null);
});

test('clicking Restore closes the bar', async () => {
  const {handle, root} = await route('/sidebar');
  handle.window.atriumSetSidebar(fullSnapshot(
      0, [space('Work', [{id: 't1', title: 'First', url: 'https://one.example/'}])]));

  rightClick(root);
  clickCtxItem(root, 'Rewind');
  root.querySelector('.rwgo').click();

  assert.equal(root.querySelector('.rewindwrap'), null);
});

test('restoring an older moment under a scope it never held sends nothing ' +
         'and leaves the bar open',
     async () => {
       const {handle, root} = await route('/sidebar');
       const workOnly = [space('Work', [{id: 't1', title: 'First', url: 'https://one.example/'}])];
       // moment 0: Work only.
       handle.window.atriumSetSidebar(fullSnapshot(0, workOnly));
       // moment 1: Work + Home - the scope select (built off the latest
       // moment) now offers Home, but moment 0 never saw it.
       handle.window.atriumSetSidebar(fullSnapshot(0, [
         ...workOnly,
         space('Home', [{id: 't2', title: 'Second', url: 'https://two.example/'}]),
       ]));
       rightClick(root);
       clickCtxItem(root, 'Rewind');
       assert.equal(root.querySelector('.rwslider').max, '1',
           'two recorded moments, slider tops out at index 1');
       const select = root.querySelector('.rwscope');
       select.value = '1';
       fire(select, 'change');
       const slider = root.querySelector('.rwslider');
       slider.value = '0';
       fire(slider, 'input');

       handle.sent.length = 0;
       root.querySelector('.rwgo').click();

       assert.deepEqual(
           handle.sent.filter((m) => m[0] === 'sidebarRestoreSlice'), []);
       assert.notEqual(root.querySelector('.rewindwrap'), null,
           'a refusal must not close the bar as if it had restored');
     });

test('rewind-bar.js sends sidebarRestoreSlice and it is registered', () => {
  const src = fs.readFileSync(path.join(ATRIUM_DIR, 'rewind-bar.js'), 'utf8');
  assert.match(src, /chrome\.send\(\s*'sidebarRestoreSlice'/);
  const host = fs.readFileSync(
      path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'), 'utf8');
  assert.match(host, /"sidebarRestoreSlice"/);
});
