#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Rewind restore can fire on a stale index or fire twice.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

let instance = 0;
async function route(pathname) {
  const handle = installDOM({pathname});
  const root = document.createElement('div');
  root.setAttribute('id', 'atrium-root');
  handle.body.appendChild(root);
  await import(
      path.join(ATRIUM_DIR, 'atrium.js') + `?rewind-restore-guard-route=${instance++}`);
  clearInterval(root.querySelector('.bglayer')?._skyClockHandle);
  return {handle, root};
}

function space(name, tabs, extra = {}) {
  return {
    name,
    tabs,
    pinned: [],
    folders: [],
    favorites: [],
    pinnedCollapsed: false,
    ...extra,
  };
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

test('a scope that is deleted from the live snapshot after the bar ' +
         'opens is refused, proving the feasibility check reads live state',
     async () => {
       const handle = installDOM({pathname: '/sidebar'});
       const root = handle.document.createElement('div');
       handle.body.appendChild(root);
       const REWIND_BAR = path.join(ATRIUM_DIR, 'rewind-bar.js');
       const REWIND = path.join(ATRIUM_DIR, 'rewind.js');
       const {openRewindBar} = await import(REWIND_BAR + '?ac1-unit');
       const {captureMoment} = await import(REWIND + '?ac1-unit');

       const opened = fullSnapshot(0, [
         space('Work', [{id: 't1', title: 'First', url: 'https://one.example/'}]),
         space('Home', [{id: 't2', title: 'Second', url: 'https://two.example/'}]),
       ]);
       const moments = captureMoment([], opened, 1000);

       // `live` stands in for sidebar.js's own currentSnapshot variable -
       // mutated after the bar opens, with no re-render in between.
       let live = opened;
       openRewindBar(root, moments, () => live);
       const select = root.querySelector('.rwscope');
       select.value = '1';
       fire(select, 'change');

       // Home (index 1) is gone from the live snapshot - the space-deleted
       // case the frozen `current` argument used to miss.
       live = fullSnapshot(0, [
         space('Work', [{id: 't1', title: 'First', url: 'https://one.example/'}]),
       ]);

       root.querySelector('.rwgo').click();

       assert.deepEqual(
           handle.sent.filter((m) => m[0] === 'sidebarRestoreSlice'), [],
           'restoring a space the live snapshot no longer holds must be ' +
               'refused, not sent against a frozen index');
     });

test('a double-click on Restore sends sidebarRestoreSlice exactly once',
     async () => {
       const {handle, root} = await route('/sidebar');
       handle.window.atriumSetSidebar(fullSnapshot(
           0, [space('Work', [{id: 't1', title: 'First', url: 'https://one.example/'}])]));

       rightClick(root);
       clickCtxItem(root, 'Rewind');
       handle.sent.length = 0;
       const restoreBtn = root.querySelector('.rwgo');
       restoreBtn.click();
       restoreBtn.click();

       const sent = handle.sent.filter((m) => m[0] === 'sidebarRestoreSlice');
       assert.equal(sent.length, 1, 'a second click must not send a second restore');
     });

test('a re-render while the bar is open closes it, so a stale ' +
         'Restore click cannot fire on an index that has since moved',
     async () => {
       const {handle, root} = await route('/sidebar');
       handle.window.atriumSetSidebar(fullSnapshot(0, [
         space('Work', [{id: 't1', title: 'First', url: 'https://one.example/'}]),
       ]));

       rightClick(root);
       clickCtxItem(root, 'Rewind');
       const staleWrap = root.querySelector('.rewindwrap');
       const staleRestoreBtn = staleWrap.querySelector('.rwgo');
       assert.notEqual(staleWrap, null);

       // A normal push lands while the scrubber is open - Home now sits where
       // Work's index used to be, the deleted-or-reordered-space case.
       handle.window.atriumSetSidebar(fullSnapshot(0, [
         space('Home', [{id: 't9', title: 'Ninth', url: 'https://nine.example/'}]),
       ]));

       assert.equal(root.querySelector('.rewindwrap'), null,
           'the re-render must close the bar rather than leave it orphaned');

       handle.sent.length = 0;
       // The user's click was already in flight when the push landed - the
       // stale reference is still a live JS object even though it is gone
       // from the document.
       staleRestoreBtn.click();

       assert.deepEqual(
           handle.sent.filter((m) => m[0] === 'sidebarRestoreSlice'), [],
           'a closed bar must not resurrect and send a restore for an index ' +
               'that has since moved');
     });
