#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// renaming a row. The model always carried custom_title and the
// sidebar always read it - nothing writes it now.
//
//   node --test build/scripts/row-rename-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

const h = installDOM({pathname: '/sidebar'});

const {startRowRename} = await import(path.join(ATRIUM_DIR, 'row-rename.js'));

const REGISTERED = new Set(
    fs.readFileSync(
          path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
          'utf8')
        .split('kSurfaceMessages[] = {')[1]
        .split('};')[0]
        .match(/"[^"]+"/g)
        .map((quoted) => quoted.slice(1, -1)));

function row(title = 'Original') {
  const el = h.document.createElement('div');
  el.className = 'row tabrow';
  const tl = h.document.createElement('span');
  tl.className = 'tl';
  tl.textContent = title;
  el.appendChild(tl);
  return el;
}

const key = (input, k) =>
    input.dispatchEvent(new Event('keydown', {bubbles: true, key: k}));

test('it swaps the title for a field carrying the current name', () => {
  const el = row();
  const input = startRowRename(el, () => {});
  assert.ok(input, 'no field opened');
  assert.equal(input.value, 'Original');
});

test('Enter commits the trimmed name', () => {
  const sent = [];
  const el = row();
  const input = startRowRename(el, (name) => sent.push(name));
  input.value = '  New Name  ';
  key(input, 'Enter');
  assert.deepEqual(sent, ['New Name']);
  assert.equal(el.querySelector('.tl').textContent, 'New Name');
});

test('an emptied field commits empty, which is how a rename is cleared', () => {
  // The only way back to the page's own title - the core reads empty as clear.
  const sent = [];
  const el = row();
  const input = startRowRename(el, (name) => sent.push(name));
  input.value = '   ';
  key(input, 'Enter');
  assert.deepEqual(sent, ['']);
  // The row shows what it had, because the page title is what it falls back to
  // and the surface does not know it yet - the next push carries it.
  assert.equal(el.querySelector('.tl').textContent, 'Original');
});

test('Escape abandons without sending anything', () => {
  const sent = [];
  const el = row();
  const input = startRowRename(el, (name) => sent.push(name));
  input.value = 'Discarded';
  key(input, 'Escape');
  assert.deepEqual(sent, []);
  assert.equal(el.querySelector('.tl').textContent, 'Original');
});

test('blur commits once, and Enter before it does not commit twice', () => {
  // Enter blurs in a real browser, so both paths fire for one edit.
  const sent = [];
  const el = row();
  const input = startRowRename(el, (name) => sent.push(name));
  input.value = 'Once';
  key(input, 'Enter');
  input.dispatchEvent(new Event('blur', {bubbles: true}));
  assert.deepEqual(sent, ['Once']);
});

test('a second rename on an open field does nothing', () => {
  const el = row();
  assert.ok(startRowRename(el, () => {}));
  assert.equal(startRowRename(el, () => {}), null,
               'a second field would stack inputs inside the title');
});

test('the verb it sends is registered with the browser', () => {
  assert.ok(REGISTERED.has('sidebarTabRenamed'));
});

test('row-rename.js is registered in atrium_resources.grd', () => {
  const grd = fs.readFileSync(path.join(ATRIUM_DIR, 'atrium_resources.grd'), 'utf8');
  assert.match(grd, /file="row-rename\.js"/);
});
