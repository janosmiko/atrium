#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// drives the real atrium.js route, rewind-wiring-test.mjs's own
// pattern, rather than a test-built host - the failure mode this task guards
// against is a module registered in the .grd and never reached from a click.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

test('rewind-bar.js is registered in atrium_resources.grd', () => {
  const grd =
      fs.readFileSync(path.join(ATRIUM_DIR, 'atrium_resources.grd'), 'utf8');
  assert.match(grd, /file="rewind-bar\.js"/);
});

test('atrium.js\'s import graph reaches rewind-bar.js', () => {
  const atriumSrc = fs.readFileSync(path.join(ATRIUM_DIR, 'atrium.js'), 'utf8');
  const wiringMatch = atriumSrc.match(/from '\.\/([\w-]+)\.js'/g) || [];
  const wiringFiles = wiringMatch.map((m) => m.match(/'\.\/([\w-]+)\.js'/)[1]);
  const wiring = wiringFiles.find((f) => {
    const src = fs.readFileSync(path.join(ATRIUM_DIR, `${f}.js`), 'utf8');
    return /from '\.\/rewind-bar\.js'/.test(src);
  });
  assert.ok(wiring,
      'no file atrium.js imports (directly or one hop away) imports rewind-bar.js');
});

let instance = 0;
async function route(pathname) {
  const handle = installDOM({pathname});
  const root = document.createElement('div');
  root.setAttribute('id', 'atrium-root');
  handle.body.appendChild(root);
  await import(
      path.join(ATRIUM_DIR, 'atrium.js') + `?rewind-bar-route=${instance++}`);
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

const fire = (node, type, init = {}) => {
  const event = new Event(type, {bubbles: true, ...init});
  node.dispatchEvent(event);
  return event;
};

const rightClick = (node) => fire(node, 'contextmenu', {clientX: 10, clientY: 10});
const ctxLabels = (root) =>
    [...root.querySelectorAll('.ctxitem')].map((item) => item.textContent);
const clickCtxItem = (root, label) =>
    [...root.querySelectorAll('.ctxitem')].find((el) => el.textContent === label).click();

// menu-dismiss.js reports every open and close so the window knows whether to
// push a dismiss. Assertions about what an ITEM did read past those.
const actions = (sent) => sent.filter(([name]) => !name.startsWith('sidebarMenu'));

test('a right-click on the empty sidebar area offers Rewind through the real menu',
     async () => {
       const {handle, root} = await route('/sidebar');
       handle.window.atriumSetSidebar(BASE_SNAPSHOT);
       const event = rightClick(root);
       assert.equal(event.defaultPrevented, true);
       assert.deepEqual(ctxLabels(root), [
         'Change Space Icon', 'Change Space Name', 'Change Theme',
         'Set Profile…', 'New Tab', 'New Folder', 'New Workspace',
         'Downloads', 'Archived Tabs', 'Snoozed Tabs', 'Shelf', 'Notes',
         'Courtyard', 'Rewind', 'Hide Sidebar', 'Settings',
       ]);
     });

test('choosing Courtyard from the empty sidebar menu opens it', async () => {
  const {handle, root} = await route('/sidebar');
  handle.window.atriumSetSidebar(BASE_SNAPSHOT);
  rightClick(root);
  handle.sent.length = 0;
  clickCtxItem(root, 'Courtyard');
  assert.deepEqual(actions(handle.sent), [['sidebarCourtyardOpened', []]]);
});

test('a right-click on a real tab row does not offer Rewind', async () => {
  const {handle, root} = await route('/sidebar');
  handle.window.atriumSetSidebar(BASE_SNAPSHOT);
  rightClick(root.querySelector('.tabrow'));
  assert.equal(ctxLabels(root).includes('Rewind'), false);
});

test('choosing Rewind with nothing recorded opens no bar', async () => {
  const {root} = await route('/sidebar');
  rightClick(root);
  clickCtxItem(root, 'Rewind');
  assert.equal(root.querySelector('.rewindwrap'), null);
});

test('choosing Rewind opens the bar with a scope select, a slider and a summary',
     async () => {
       const {handle, root} = await route('/sidebar');
       handle.window.atriumSetSidebar(BASE_SNAPSHOT);
       rightClick(root);
       clickCtxItem(root, 'Rewind');

       const bar = root.querySelector('.rewindwrap');
       assert.notEqual(bar, null);
       const select = bar.querySelector('.rwscope');
       const slider = bar.querySelector('.rwslider');
       const info = bar.querySelector('.rwinfo');
       assert.notEqual(select, null);
       assert.notEqual(slider, null);
       // One tab in the one recorded moment's space, the summary shape.
       assert.match(info.textContent, /1 tab in Work/);
     });

test('the scope selector defaults to the current space', async () => {
  const {handle, root} = await route('/sidebar');
  handle.window.atriumSetSidebar(BASE_SNAPSHOT);
  rightClick(root);
  clickCtxItem(root, 'Rewind');

  const select = root.querySelector('.rwscope');
  assert.deepEqual(
      [...select.querySelectorAll('option')].map((o) => [o.value, o.textContent]),
      [['all', 'Whole window'], ['0', 'Work']]);
  assert.equal(select.value, '0');
});

test('moving the slider updates the summary to the selected moment and sends nothing',
     async () => {
       const {handle, root} = await route('/sidebar');
       handle.window.atriumSetSidebar(BASE_SNAPSHOT);
       handle.window.atriumSetSidebar(snapshot(
           0, [SPACE([{id: 't1', title: 'First', url: 'https://one.example/'},
                      {id: 't2', title: 'Second', url: 'https://two.example/'}])]));

       handle.sent.length = 0;
       rightClick(root);
       clickCtxItem(root, 'Rewind');
       const info = root.querySelector('.rwinfo');
       assert.match(info.textContent, /2 tabs in Work/);

       const slider = root.querySelector('.rwslider');
       assert.equal(slider.max, '1', 'two recorded moments, slider tops out at index 1');
       slider.value = '0';
       fire(slider, 'input');
       assert.match(info.textContent, /1 tab in Work/);
       assert.deepEqual(actions(handle.sent), [],
                        'a preview must not send anything to the browser');
     });

test('changing scope to Whole window updates the summary without restoring',
     async () => {
       const {handle, root} = await route('/sidebar');
       handle.window.atriumSetSidebar(BASE_SNAPSHOT);
       rightClick(root);
       clickCtxItem(root, 'Rewind');

       handle.sent.length = 0;
       const select = root.querySelector('.rwscope');
       select.value = 'all';
       fire(select, 'change');
       const info = root.querySelector('.rwinfo');
       assert.match(info.textContent, /1 open tabs · in Work/);
       assert.deepEqual(handle.sent, []);
     });

test('a ring of one moment still shows a slider, disabled with nowhere to move',
     async () => {
       const {handle, root} = await route('/sidebar');
       handle.window.atriumSetSidebar(BASE_SNAPSHOT);
       rightClick(root);
       clickCtxItem(root, 'Rewind');

       const slider = root.querySelector('.rwslider');
       assert.equal(slider.max, '0');
       assert.equal(slider.disabled, true);
     });

test('the close button removes the bar', async () => {
  const {handle, root} = await route('/sidebar');
  handle.window.atriumSetSidebar(BASE_SNAPSHOT);
  rightClick(root);
  clickCtxItem(root, 'Rewind');
  assert.notEqual(root.querySelector('.rewindwrap'), null);

  root.querySelector('.rwx').click();
  assert.equal(root.querySelector('.rewindwrap'), null);
});

test('no keyboard shortcut opens Rewind', () => {
  // rewind-bar.js exports no key handler at all - opening only ever happens
  // through installRowMenu's emptyMenuItems callback.
  const src = fs.readFileSync(path.join(ATRIUM_DIR, 'rewind-bar.js'), 'utf8');
  assert.doesNotMatch(src, /keydown|keyCode|addEventListener\('key/);
});
