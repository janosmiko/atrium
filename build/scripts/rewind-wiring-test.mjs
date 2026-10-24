#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// drives the real atrium.js route, route-test.mjs's own
// pattern, rather than a test-built host.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

test('rewind.js is registered in atrium_resources.grd', () => {
  const grd =
      fs.readFileSync(path.join(ATRIUM_DIR, 'atrium_resources.grd'), 'utf8');
  assert.match(grd, /file="rewind\.js"/);
});

test('atrium.js\'s import graph reaches rewind.js', () => {
  const atriumSrc = fs.readFileSync(path.join(ATRIUM_DIR, 'atrium.js'), 'utf8');
  const wiringMatch = atriumSrc.match(/from '\.\/([\w-]+)\.js'/g) || [];
  const wiringFiles = wiringMatch.map((m) => m.match(/'\.\/([\w-]+)\.js'/)[1]);
  const wiring = wiringFiles.find((f) => {
    const src = fs.readFileSync(path.join(ATRIUM_DIR, `${f}.js`), 'utf8');
    return /from '\.\/rewind\.js'/.test(src);
  });
  assert.ok(wiring,
      'no file atrium.js imports (directly or one hop away) imports rewind.js');
});

// main() runs at import, so a route needs a fresh module instance each time
// (route-test.mjs's own pattern).
let instance = 0;
async function route(pathname) {
  const handle = installDOM({pathname});
  const root = document.createElement('div');
  root.setAttribute('id', 'atrium-root');
  handle.body.appendChild(root);
  await import(
      path.join(ATRIUM_DIR, 'atrium.js') + `?rewind-route=${instance++}`);
  clearInterval(root.querySelector('.bglayer')?._skyClockHandle);
  return {handle, root};
}

const SPACE = (tabs, extra = {}) => ({
  name: 'Work',
  color: '#3355ff',
  tabs,
  pinned: [],
  folders: [],
  favorites: [],
  ...extra,
});

// apply() renders a single space straight from the TOP-LEVEL fields, not from
// spaces[activeSpace] (sidebar.js's own mirrorActiveSpace comment) - so a
// snapshot that omits them renders nothing to click, real snapshot or not.
function snapshot(activeSpace, spaces) {
  const active = spaces[activeSpace];
  return {
    activeSpace,
    spaces,
    spaceName: active.name,
    spaceTint: active.color,
    tabs: active.tabs,
    pinned: active.pinned,
    folders: active.folders,
    favorites: active.favorites,
    pinnedCollapsed: !!active.pinnedCollapsed,
  };
}

const BASE_SNAPSHOT =
    snapshot(0, [SPACE([{id: 't1', title: 'First', url: 'https://one.example/'}])]);

// The ring itself is closed over installSidebar's call - openRewindBar reads
// it directly (sidebar.js's own openRewind hook), so the real reaching path
// for its length is the Rewind menu item, not a direct accessor.
const fire = (node, type, init = {}) => {
  const event = new Event(type, {bubbles: true, ...init});
  node.dispatchEvent(event);
  return event;
};
const rightClick = (node) => fire(node, 'contextmenu', {clientX: 10, clientY: 10});
const clickCtxItem = (root, label) =>
    [...root.querySelectorAll('.ctxitem')].find((el) => el.textContent === label).click();

// openRewindBar closes any existing bar before rendering, so calling this
// repeatedly in one test always reflects the current ring.
function momentsCount(root) {
  rightClick(root);
  clickCtxItem(root, 'Rewind');
  const slider = root.querySelector('.rwslider');
  return slider ? Number(slider.max) + 1 : 0;
}

test('the first push through the real route entrypoint seeds one moment',
     async () => {
       const {handle, root} = await route('/sidebar');
       handle.window.atriumSetSidebar(BASE_SNAPSHOT);
       assert.equal(momentsCount(root), 1);
     });

// sidebar-install-test.mjs already covers the close click itself. What this
// adds is the browser's confirming push - the half that actually grows the
// ring for a real gesture, not a synthetic captureMoment call.
test('closing a tab through the real click handler grows the ring once the ' +
         'browser confirms it',
     async () => {
       const {handle, root} = await route('/sidebar');
       handle.window.atriumSetSidebar(BASE_SNAPSHOT);
       assert.equal(momentsCount(root), 1);

       const closeButton = root.querySelector('.tabrow .x');
       closeButton.dispatchEvent(new Event('click', {bubbles: true}));
       assert.deepEqual(
           handle.sent.filter((m) => m[0] === 'sidebarTabClosed'),
           [['sidebarTabClosed', ['t1']]],
           'the real handler must have reached the browser');

       handle.window.atriumSetSidebar(snapshot(0, [SPACE([])]));
       assert.equal(momentsCount(root), 2,
           'a structurally different push must grow the ring');
     });

test('pushing the same structural state twice in a row does not add a ' +
         'second moment',
     async () => {
       const {handle, root} = await route('/sidebar');
       handle.window.atriumSetSidebar(BASE_SNAPSHOT);
       assert.equal(momentsCount(root), 1);

       handle.window.atriumSetSidebar(BASE_SNAPSHOT);
       assert.equal(momentsCount(root), 1,
           'an unchanged structural push must not grow the ring');

       // Colour is dropped from rewind.js's own projection - this is that
       // contract proven through the real route, not just the pure model.
       handle.window.atriumSetSidebar(
           snapshot(0, [SPACE(BASE_SNAPSHOT.tabs, {color: '#ff0000'})]));
       assert.equal(momentsCount(root), 1,
           'a colour-only push must not grow the ring');
     });

test('a tab rename produces a new moment', async () => {
  const {handle, root} = await route('/sidebar');
  handle.window.atriumSetSidebar(BASE_SNAPSHOT);

  handle.window.atriumSetSidebar(snapshot(
      0, [SPACE([{id: 't1', title: 'Renamed', url: 'https://one.example/'}])]));
  assert.equal(momentsCount(root), 2);
});

test('a space rename produces a new moment', async () => {
  const {handle, root} = await route('/sidebar');
  handle.window.atriumSetSidebar(BASE_SNAPSHOT);

  handle.window.atriumSetSidebar(
      snapshot(0, [SPACE(BASE_SNAPSHOT.tabs, {name: 'Renamed Space'})]));
  assert.equal(momentsCount(root), 2);
});

// One push per gesture, so each is shown to reach the ring on its own rather
// than only in combination.
test('pin, move-to-space and fold each grow the ring by one', async () => {
  const {handle, root} = await route('/sidebar');
  handle.window.atriumSetSidebar(BASE_SNAPSHOT);
  assert.equal(momentsCount(root), 1);

  handle.window.atriumSetSidebar(snapshot(0, [SPACE(
      [], {pinned: [{id: 't1', title: 'First', url: 'https://one.example/'}]})]));
  assert.equal(momentsCount(root), 2, 'pin');

  handle.window.atriumSetSidebar(snapshot(0, [SPACE([], {
    pinned: [{id: 't1', title: 'First', url: 'https://one.example/'}],
    folders: [{id: 'g1', name: 'Group', closed: false, tabs: []}],
  })]));
  assert.equal(momentsCount(root), 3, 'new folder');

  handle.window.atriumSetSidebar(snapshot(0, [SPACE([], {
    pinned: [{id: 't1', title: 'First', url: 'https://one.example/'}],
    folders: [{id: 'g1', name: 'Group', closed: true, tabs: []}],
  })]));
  assert.equal(momentsCount(root), 4, 'fold toggled');

  handle.window.atriumSetSidebar(snapshot(1, [
    SPACE([], {
      pinned: [{id: 't1', title: 'First', url: 'https://one.example/'}],
      folders: [{id: 'g1', name: 'Group', closed: true, tabs: []}],
    }),
    SPACE([{id: 't2', title: 'Moved in', url: 'https://two.example/'}], {name: 'Home'}),
  ]));
  assert.equal(momentsCount(root), 5, 'moved to another space');
});

// Eviction content is unit-tested in rewind-test.mjs. This proves the real
// route's ring is capped too.
test('the ring stays at MAX_MOMENTS once it is full, dropping the oldest',
     async () => {
       const {handle, root} = await route('/sidebar');
       for (let i = 0; i < 65; i++) {
         handle.window.atriumSetSidebar(snapshot(
             0, [SPACE([{id: 't' + i, title: 'T' + i, url: 'https://x/' + i}])]));
       }
       assert.equal(momentsCount(root), 60);
     });
