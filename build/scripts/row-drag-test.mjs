#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the sidebar row drag's index maths.
//
//   make drag-test
//
// Both functions are one line and both have a classic off-by-one in them. The
// slot/index distinction in particular is invisible when dragging UP and wrong
// every time when dragging DOWN, which is exactly the shape of bug that ships.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {insertionIndexFor, destinationFor} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/sidebar-drag.js'));

// Four 30px rows starting at y=0, the sidebar's actual row height.
const boxes = [
  {top: 0, bottom: 30},
  {top: 30, bottom: 60},
  {top: 60, bottom: 90},
  {top: 90, bottom: 120},
];

test('a row is claimed by its own midpoint', () => {
  assert.equal(insertionIndexFor(0, boxes), 0);
  assert.equal(insertionIndexFor(14, boxes), 0, 'top half means before it');
  assert.equal(insertionIndexFor(16, boxes), 1, 'bottom half means after it');
  assert.equal(insertionIndexFor(44, boxes), 1);
  assert.equal(insertionIndexFor(46, boxes), 2);
});

test('exactly on a midpoint falls to the later slot', () => {
  // Not arbitrary: < rather than <= means the boundary belongs to the row
  // below, so the indicator never sits still while the cursor moves.
  assert.equal(insertionIndexFor(15, boxes), 1);
});

test('past the last row lands in the final slot', () => {
  assert.equal(insertionIndexFor(119, boxes), 4);
  assert.equal(insertionIndexFor(9999, boxes), 4);
});

test('above the first row lands in slot zero', () => {
  assert.equal(insertionIndexFor(-50, boxes), 0);
});

test('an empty list has exactly one slot', () => {
  assert.equal(insertionIndexFor(0, []), 0);
  assert.equal(insertionIndexFor(500, []), 0);
});

// THE off-by-one. A slot index counts gaps in the list as it currently
// stands; the destination counts positions after the dragged row is removed.
test('dragging down accounts for the row leaving its old place', () => {
  // Row 0 dropped into the slot after row 2 (slot 3) becomes index 2, not 3.
  assert.equal(destinationFor(0, 3), 2);
  // Row 0 to the very end of a four-row list.
  assert.equal(destinationFor(0, 4), 3);
});

test('dragging up needs no adjustment', () => {
  assert.equal(destinationFor(3, 0), 0);
  assert.equal(destinationFor(3, 1), 1);
});

test('a move that changes nothing returns null', () => {
  // The slot just above the row, and the slot just below it, are both the
  // row's own position - neither should send a message.
  assert.equal(destinationFor(2, 2), null);
  assert.equal(destinationFor(2, 3), null);
});

test('adjacent moves are real moves', () => {
  assert.equal(destinationFor(2, 1), 1, 'up one');
  assert.equal(destinationFor(2, 4), 3, 'down one');
});

test('the maths round-trips a full reorder', () => {
  // Model the list as an array and apply what the pair produces, to check the
  // two functions agree with each other rather than only with my arithmetic.
  const apply = (list, from, slot) => {
    const to = destinationFor(from, slot);
    if (to === null) {
      return list;
    }
    const next = [...list];
    next.splice(to, 0, next.splice(from, 1)[0]);
    return next;
  };
  const list = ['a', 'b', 'c', 'd'];
  assert.deepEqual(apply(list, 0, 4), ['b', 'c', 'd', 'a'], 'first to last');
  assert.deepEqual(apply(list, 3, 0), ['d', 'a', 'b', 'c'], 'last to first');
  assert.deepEqual(apply(list, 1, 3), ['a', 'c', 'b', 'd'], 'swap the middle');
  assert.deepEqual(apply(list, 2, 2), list, 'onto itself');
});

// --- a drag another surface started ----------------------------
//
// The floating address bar's grip starts the drag in its own document, so this
// sidebar never sees a dragstart. beginExternal stands in for one.

const {installRowDrag} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/sidebar-drag.js'));
const {installDOM, Event} = await import('./dom-shim.mjs');
const shim = installDOM();

function externalFixture(options = {}) {
  shim.reset();
  const root = document.createElement('div');
  root.className = 'sbroot';
  root.setRect({top: 0, left: 0, width: 260, height: 600});
  document.body.appendChild(root);
  const pane = document.createElement('div');
  pane.className = 'railpane';
  pane.dataset.spaceIndex = '0';
  root.appendChild(pane);
  const zone = document.createElement('div');
  zone.dataset.dropzone = 'tabs';
  pane.appendChild(zone);
  const addRow = (top, tabId, slot) => {
    const row = document.createElement('div');
    row.className = 'row tabrow';
    row.dataset.tabId = tabId;
    row.dataset.slot = String(slot);
    row.setRect({top, left: 0, width: 260, height: 30});
    zone.appendChild(row);
    return row;
  };
  const rows = [addRow(0, '11', 0), addRow(30, '12', 1)];
  // Slots 2 and 3 are the two panes of one split row.
  const split = addRow(60, '13', 2);
  split.classList.add('splitrow');
  for (const [id, slot] of [['13', 2], ['14', 3]]) {
    const seg = document.createElement('div');
    seg.className = 'sseg';
    seg.dataset.tabId = id;
    seg.dataset.slot = String(slot);
    split.appendChild(seg);
  }
  const drops = [];
  const drag = installRowDrag(root, {onDrop: (d) => drops.push(d), ...options});
  return {root, zone, rows, split, drops, drag};
}

test('beginExternal takes the row a tab id names as the drag source', () => {
  const f = externalFixture();
  assert.equal(f.drag.beginExternal('12'), true);
  const src = f.drag.dragSource();
  assert.equal(src.tabId, '12');
  assert.equal(src.index, 1);
  assert.equal(src.section, 'tabs');
  assert.equal(f.root.classList.contains('dragactive'), true);
  f.drag.endExternal();
});

test('a split pane resolves to its own member slot, not the row\'s first', () => {
  const f = externalFixture();
  assert.equal(f.drag.beginExternal('14'), true);
  assert.equal(f.drag.dragSource().index, 3);
  assert.equal(f.drag.dragSource().tabId, '14');
  f.drag.endExternal();
});

test('an unknown tab id starts nothing', () => {
  const f = externalFixture();
  assert.equal(f.drag.beginExternal('99'), false);
  assert.equal(f.drag.dragSource(), null);
  assert.equal(f.root.classList.contains('dragactive'), false);
});

test('an external drag drops like a row drag and shows the same line', () => {
  const f = externalFixture();
  f.drag.beginExternal('12');
  f.zone.dispatchEvent(new Event('dragover', {bubbles: true, clientX: 100, clientY: 2}));
  const line = f.root.querySelector('.dropline');
  assert.ok(line && !line.hidden, 'the reorder line shows');
  f.zone.dispatchEvent(new Event('drop', {bubbles: true, clientX: 100, clientY: 2}));
  assert.deepEqual(f.drops.map((d) => [d.kind, d.from, d.to]), [['reorder', 1, 0]]);
  assert.equal(f.drag.dragSource(), null, 'the drop cleared the drag');
  assert.equal(f.drag.endExternal(), true, 'the browser learns the row was placed');
});

test('a drop the sidebar refuses reports nothing placed', () => {
  const f = externalFixture();
  f.drag.beginExternal('12');
  f.zone.dispatchEvent(new Event('drop', {bubbles: true, clientX: 100, clientY: 45}));
  assert.deepEqual(f.drops, []);
  assert.equal(f.drag.endExternal(), false);
});

test('endExternal clears a drag that never dropped here', () => {
  const f = externalFixture();
  f.drag.beginExternal('12');
  f.zone.dispatchEvent(new Event('dragover', {bubbles: true, clientX: 100, clientY: 2}));
  assert.equal(f.drag.endExternal(), false);
  assert.equal(f.drag.dragSource(), null);
  assert.equal(f.root.classList.contains('dragactive'), false);
  assert.equal(f.root.querySelector('.dropline').hidden, true);
  assert.equal(f.root.querySelectorAll('.dragging').length, 0);
});

test('a split row drag carries every pane and drops as one run', () => {
  const f = externalFixture();
  f.split.dispatchEvent(new Event('dragstart', {bubbles: true}));
  const src = f.drag.dragSource();
  assert.equal(src.split, true);
  assert.deepEqual(src.slots, [2, 3]);
  assert.equal(f.split.classList.contains('dragging'), true, 'the whole row lifts');
  f.zone.dispatchEvent(new Event('drop', {bubbles: true, clientX: 100, clientY: 2}));
  assert.deepEqual(f.drops,
      [{kind: 'moveRun', slots: [2, 3], fromSpace: 0, toSpace: 0, to: 0}]);
});

test('a drag that starts on a pane moves the whole split row', () => {
  const f = externalFixture();
  f.split.querySelectorAll('.sseg')[1].dispatchEvent(
      new Event('dragstart', {bubbles: true}));
  assert.equal(f.drag.dragSource().split, true);
  assert.deepEqual(f.drag.dragSource().slots, [2, 3]);
  f.drag.endExternal();
});

test('the address bar grip on a split pane still drags that pane alone', () => {
  const f = externalFixture();
  f.drag.beginExternal('14');
  assert.ok(!f.drag.dragSource().split);
  f.drag.endExternal();
});

test('a split row released outside the sidebar tears nothing off', () => {
  const released = [];
  const f = externalFixture({onRelease: (...args) => released.push(args)});
  f.rows[0].dispatchEvent(new Event('dragstart', {bubbles: true}));
  f.rows[0].dispatchEvent(new Event('dragend',
      {bubbles: true, clientX: 900, clientY: 900, screenX: 900, screenY: 900}));
  assert.equal(released.length, 1, 'an ordinary row still releases');
  f.split.dispatchEvent(new Event('dragstart', {bubbles: true}));
  f.split.dispatchEvent(new Event('dragend',
      {bubbles: true, clientX: 900, clientY: 900, screenX: 900, screenY: 900}));
  assert.equal(released.length, 1, 'the split row did not');
});

test('a row drag carries only the tab type, so a page textarea takes nothing', () => {
  const f = externalFixture();
  const data = {};
  const dataTransfer = {setData: (type, value) => { data[type] = value; }};
  f.rows[1].dispatchEvent(new Event('dragstart', {bubbles: true, dataTransfer}));
  assert.deepEqual(Object.keys(data), ['application/x-atrium-tab']);
  f.drag.endExternal();
});
