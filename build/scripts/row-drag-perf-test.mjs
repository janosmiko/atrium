#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// dragover must not re-measure every row's layout on every tick.
//
//   make webui-test
//
// row-drag-install-test.mjs proves the wiring is right; this proves it is
// CHEAP - a drag across many rows costs one measurement pass per real frame,
// not one pass per dragover event.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const h = installDOM();

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {installRowDrag} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/sidebar-drag.js'));

const ROOT_LEFT = 1000;
const ROOT_WIDTH = 260;
// MID_X keeps every point clear of the left and right split bands. A test
// that needs a reorder LINE also has to keep clientY in the outer fifth of a
// row, because the middle three fifths are the split now.
const MID_X = ROOT_LEFT + 130;
const ROW_H = 30;
const ROWS = 40;

function element(tag, className, rect) {
  const el = document.createElement(tag);
  if (className) {
    el.className = className;
  }
  if (rect) {
    el.setRect(rect);
  }
  return el;
}

const fire = (node, type, init = {}) => {
  const event = new Event(type, {bubbles: true, clientX: MID_X, ...init});
  node.dispatchEvent(event);
  return event;
};

// Counts getBoundingClientRect calls on a set of elements without touching
// their answers, so a test can tell how many LAYOUT reads a gesture cost
// without caring what the boxes said.
function countReads(elements) {
  const counters = elements.map((el) => {
    const real = el.getBoundingClientRect.bind(el);
    let count = 0;
    el.getBoundingClientRect = (...args) => {
      count++;
      return real(...args);
    };
    return () => count;
  });
  return () => counters.reduce((sum, get) => sum + get(), 0);
}

function fixture(t) {
  h.reset();
  const root = element('div', 'sbroot',
                       {top: 0, left: ROOT_LEFT, width: ROOT_WIDTH, height: 2000});
  h.body.appendChild(root);
  const zone = element('div', 'rows');
  zone.dataset.dropzone = 'tabs';
  root.appendChild(zone);
  const rows = Array.from({length: ROWS}, (_, i) => {
    const row = element('div', 'row tabrow',
                        {top: i * ROW_H, left: ROOT_LEFT, width: ROOT_WIDTH, height: ROW_H});
    row.dataset.tabId = 'tab-' + i;
    zone.appendChild(row);
    return row;
  });
  installRowDrag(root, {onDrop: () => {}});
  t.after(() => fire(root, 'dragend'));
  return {root, zone, rows};
}

test('many dragover events in one frame cost one measurement pass, not one per event', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');

  const readCount = countReads([f.root, ...f.rows]);
  const EVENTS = 10;
  // Most events in the top reorder band, the last few in the split band.
  for (let i = 0; i < EVENTS; i++) {
    fire(f.rows[20], 'dragover', {clientY: 20 * ROW_H + 1 + i});
  }

  // O(rows) per event would be EVENTS * (ROWS + 1) root+row reads. One pass
  // measures the root once and every row once, and the one preview move reads
  // every row before and after it for the glide.
  assert.equal(readCount(), 3 * ROWS + 1,
               `expected one measurement pass across ${EVENTS} same-frame ` +
               `dragover events, got ${readCount()} getBoundingClientRect calls`);
});

test('a new frame re-measures, so a scroll mid-drag is still seen', (t) => {
  const f = fixture(t);
  f.zone.setRect({top: 0, left: ROOT_LEFT, width: ROOT_WIDTH, height: 1200});
  fire(f.rows[0], 'dragstart');
  fire(f.rows[20], 'dragover', {clientY: 20 * ROW_H + 3});

  const at = () => [...f.zone.children].indexOf(f.rows[0]);
  const before = at();

  // No structural change - same elements, same order - only their boxes
  // move. The shift is 145 rather than a round number so the pointer lands in
  // a reorder band again, and against a row edge `before` cannot repeat.
  f.zone.setRect({top: -145, left: ROOT_LEFT, width: ROOT_WIDTH, height: 1200});
  // A scroll moves the painted box, never the layout box offsetTop reads.
  f.rows.forEach((row, i) => row.setLayoutBox({top: i * ROW_H, height: ROW_H})
                                .setRect({top: i * ROW_H - 145, height: ROW_H}));
  h.tick();
  fire(f.rows[20], 'dragover', {clientY: 20 * ROW_H + 3});

  assert.notEqual(at(), before,
                  'a held drag must keep tracking the list under a scroll');
});

test('dropping into a folded-then-reopened row set measures fresh, not cached', (t) => {
  // Simulates a fold collapsing a run of rows out of the zone mid-drag,
  // without any frame boundary in between - the risk
  // here, since a fold mutates the live DOM rather than waiting for a push.
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');
  fire(f.rows[20], 'dragover', {clientY: 20 * ROW_H + 12});

  const folded = f.rows.slice(10, 20);
  for (const row of folded) {
    row.remove();
  }

  const readCount = countReads([f.zone, ...f.rows.filter((r) => r.isConnected)]);
  fire(f.rows[5], 'dragover', {clientY: 5 * ROW_H + 5});

  assert.ok(readCount() > 0,
             'a row set that changed mid-frame must not serve stale boxes');
});
