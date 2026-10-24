#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// shelving gave no confirmation and no way to see the result.
//
//   node --test build/scripts/shelf-toast-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

const REGISTERED = new Set(
    fs.readFileSync(
          path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
          'utf8')
        .split('kSurfaceMessages[] = {')[1]
        .split('};')[0]
        .match(/"[^"]+"/g)
        .map((quoted) => quoted.slice(1, -1)));

let instance = 0;
async function route() {
  const handle = installDOM({pathname: '/sidebar'});
  const root = document.createElement('div');
  root.setAttribute('id', 'atrium-root');
  handle.body.appendChild(root);
  await import(
      path.join(ATRIUM_DIR, 'atrium.js') + `?shelf-toast-route=${instance++}`);
  clearInterval(root.querySelector('.bglayer')?._skyClockHandle);
  return {handle, root};
}

const toastIn = (root) => root.querySelector('.toast');

test('shelving shows a toast saying so', async () => {
  const {handle, root} = await route();
  handle.window.atriumShelfToast('A Page');
  const toast = toastIn(root);
  assert.ok(toast, 'no toast rendered');
  assert.match(toast.textContent, /Added to Shelf/);
});

test('the toast names what was shelved', async () => {
  // The label is the only thing distinguishing two shelves in a row.
  const {handle, root} = await route();
  handle.window.atriumShelfToast('A Page');
  assert.match(toastIn(root).textContent, /A Page/);
});

test('clicking the toast sends sidebarShelfToastClicked', async () => {
  // Exercises the REAL path, not an injected one. A version of this test that
  // only checked an injected callback let a mutant swapping this verb for
  // sidebarOverviewToggled survive - the injection hid the thing under test.
  const {handle, root} = await route();
  handle.sent.length = 0;
  handle.window.atriumShelfToast('A Page');
  toastIn(root).dispatchEvent(new Event('click', {bubbles: true}));
  assert.deepEqual(handle.sent, [['sidebarShelfToastClicked', []]]);
});

test('and that name is registered with the browser', () => {
  // An unregistered name is FATAL in the browser process.
  assert.ok(REGISTERED.has('sidebarShelfToastClicked'));
});

test('a retired off step still raises the toast, migrated to oneSecond', async () => {
  const {handle, root} = await route();
  handle.window.atriumSetNotificationTimeout('off');
  handle.window.atriumShelfToast('A Page');
  assert.ok(toastIn(root));
});
