#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// node --test ./build/scripts/shelf-dropzone-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event as ShimEvent} from './dom-shim.mjs';

const h = installDOM();

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {installShelfDropZone} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/shelf-dropzone.js'));

const fire = (node, type, init = {}) => {
  const event = new ShimEvent(type, init);
  node.dispatchEvent(event);
  return event;
};

function fixture() {
  h.reset();
  const root = h.document.createElement('div');
  root.className = 'sbroot';
  h.body.appendChild(root);
  const row = h.document.createElement('div');
  row.className = 'row tabrow';
  row.dataset.tabId = 't1';
  root.appendChild(row);
  const sent = [];
  const zoneApi =
      installShelfDropZone(root, {send: (tabId) => sent.push(tabId)});
  return {root, row, sent, zoneApi};
}

test('dragging a tab row reveals the zone', () => {
  const {root, row} = fixture();
  fire(row, 'dragstart', {target: row});
  const zone = root.querySelector('.shelfzone');
  assert.notEqual(zone, null);
  assert.equal(zone.hidden, false);
});

test('a cold row (no tab id) does not reveal the zone', () => {
  const {root, row} = fixture();
  row.removeAttribute('data-tab-id');
  fire(row, 'dragstart', {target: row});
  assert.equal(root.querySelector('.shelfzone'), null);
});

test('dragging something other than a tab row leaves the zone alone', () => {
  const {root} = fixture();
  const other = h.document.createElement('div');
  other.className = 'fav';
  root.appendChild(other);
  fire(other, 'dragstart', {target: other});
  assert.equal(root.querySelector('.shelfzone'), null);
});

test('dragover near the zone marks it hot; far away does not', () => {
  const {root, row} = fixture();
  fire(row, 'dragstart', {target: row});
  const zone = root.querySelector('.shelfzone');
  zone.setRect({left: 10, top: 500, width: 10, height: 88});
  fire(root, 'dragover', {target: row, clientX: 12, clientY: 540});
  assert.equal(zone.classList.contains('hot'), true);
  fire(root, 'dragover', {target: row, clientX: 500, clientY: 540});
  assert.equal(zone.classList.contains('hot'), false);
});

test('dropping on the zone sends the dragged tab id, once', () => {
  const {root, row, sent} = fixture();
  fire(row, 'dragstart', {target: row});
  const zone = root.querySelector('.shelfzone');
  const drop = fire(zone, 'drop', {target: zone});
  assert.equal(drop.defaultPrevented, true);
  assert.deepEqual(sent, ['t1']);
  assert.equal(zone.hidden, true);
});

test('a drag that ends without a drop sends nothing and hides the zone', () => {
  const {root, row, sent} = fixture();
  fire(row, 'dragstart', {target: row});
  const zone = root.querySelector('.shelfzone');
  fire(root, 'dragend', {target: row});
  assert.deepEqual(sent, []);
  assert.equal(zone.hidden, true);
});

test('setOnRight mirrors the zone to the sidebar\'s own docked edge', () => {
  const {row, zoneApi} = fixture();
  fire(row, 'dragstart', {target: row});
  const zone = row.parentNode.querySelector('.shelfzone');
  zoneApi.setOnRight(true);
  assert.equal(zone.classList.contains('sbright'), true);
  zoneApi.setOnRight(false);
  assert.equal(zone.classList.contains('sbright'), false);
});
