#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// flyRowToOverview/flyRowsToOverview, ported from the lab's
// flyRow/flySnooze (docs/lab/swipe-lab.html:2247-2273).

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const h = installDOM({pathname: '/sidebar'});
const {flyRowToOverview, flyRowsToOverview} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/archive-fly.js'));

function fakeClock() {
  let next = 1;
  const timers = new Map();
  return {
    set(ms, fn) {
      const id = next++;
      timers.set(id, {at: ms, fn});
      return id;
    },
    clear(id) {
      timers.delete(id);
    },
    advance(ms) {
      for (const [id, t] of [...timers.entries()].sort((a, b) => a[1].at - b[1].at)) {
        t.at -= ms;
        if (t.at <= 0) {
          timers.delete(id);
          t.fn();
        }
      }
    },
    get scheduled() {
      return [...timers.values()].map((t) => t.at);
    },
  };
}

function makeHost() {
  h.reset();
  const host = document.createElement('div');
  document.body.append(host);
  return host;
}

function makeRow(host, id, rect) {
  const row = document.createElement('div');
  row.className = 'row tabrow';
  row.dataset.tabId = id;
  row.setRect(rect);
  const close = document.createElement('button');
  close.className = 'x';
  row.appendChild(close);
  host.appendChild(row);
  return row;
}

function makeOverviewDot(host, rect) {
  const dot = document.createElement('span');
  dot.className = 'sdot ovdot';
  dot.setRect(rect);
  host.appendChild(dot);
  return dot;
}

test('archiving flies a clone from the row toward the Overview dot', () => {
  const host = makeHost();
  const row = makeRow(host, 't1', {left: 0, top: 100, width: 200, height: 30});
  makeOverviewDot(host, {left: 0, top: 0, width: 20, height: 20});

  flyRowToOverview(row, {host, clock: {set: (ms, fn) => fn(), clear: () => {}}});

  const clone = document.body.querySelector('.archivefly');
  assert.ok(clone, 'a clone must be appended for the flight');
  assert.equal(clone.style.left, '0px');
  assert.equal(clone.style.top, '100px');
  // Target center (10,10) minus row center (100,115): dx=-90, dy=-105.
  assert.equal(clone.style['--dx'], '-90px');
  assert.equal(clone.style['--dy'], '-105px');
});

test('the flight never touches the real row', () => {
  const host = makeHost();
  const row = makeRow(host, 't1', {left: 0, top: 100, width: 200, height: 30});
  makeOverviewDot(host, {left: 0, top: 0, width: 20, height: 20});

  flyRowToOverview(row, {host, clock: {set: (ms, fn) => fn(), clear: () => {}}});

  assert.equal(row.className, 'row tabrow', 'no class/style change on the source row');
  assert.equal(row.isConnected, true, 'the row is left in the DOM by this call');
});

test('no Overview dot in the DOM flies the clone toward its own center', () => {
  const host = makeHost();
  const row = makeRow(host, 't1', {left: 40, top: 60, width: 200, height: 30});

  flyRowToOverview(row, {host, clock: {set: (ms, fn) => fn(), clear: () => {}}});

  const clone = document.body.querySelector('.archivefly');
  assert.equal(clone.style['--dx'], '0px');
  assert.equal(clone.style['--dy'], '0px');
});

test('reduced motion skips the flight entirely', () => {
  const host = makeHost();
  const row = makeRow(host, 't1', {left: 0, top: 100, width: 200, height: 30});
  makeOverviewDot(host, {left: 0, top: 0, width: 20, height: 20});
  let scheduled = false;
  const clock = {set: (ms, fn) => scheduled = true, clear: () => {}};

  flyRowToOverview(row, {host, reducedMotion: true, clock});

  assert.equal(scheduled, false, 'reduced motion must return before scheduling anything');
  assert.equal(document.body.querySelector('.archivefly'), null);
  assert.equal(row.isConnected, true, 'the row is unaffected either way');
});

test('omitting reducedMotion falls back to matchMedia and honours it', () => {
  const host = makeHost();
  const row = makeRow(host, 't1', {left: 0, top: 100, width: 200, height: 30});
  h.setMatchMedia(() => true);
  let scheduled = false;
  const clock = {set: (ms, fn) => scheduled = true, clear: () => {}};

  flyRowToOverview(row, {host, clock});

  assert.equal(scheduled, false);
  assert.equal(document.body.querySelector('.archivefly'), null);
});

test('a row already gone from the DOM by flight time is a no-op, not a throw', () => {
  const host = makeHost();
  const row = makeRow(host, 't1', {left: 0, top: 100, width: 200, height: 30});
  const clock = fakeClock();

  flyRowToOverview(row, {host, clock});
  row.remove();
  assert.doesNotThrow(() => clock.advance(1000));
  assert.equal(document.body.querySelector('.archivefly'), null);
});

test('a call on a row that never joined the DOM is a no-op', () => {
  makeHost();
  const row = document.createElement('div');
  row.setRect({left: 0, top: 0, width: 10, height: 10});
  assert.doesNotThrow(() => flyRowToOverview(row, {}));
  assert.equal(document.body.querySelector('.archivefly'), null);
});

test('animationend on the clone removes it', () => {
  const host = makeHost();
  const row = makeRow(host, 't1', {left: 0, top: 100, width: 200, height: 30});

  flyRowToOverview(row, {host, clock: {set: (ms, fn) => fn(), clear: () => {}}});
  const clone = document.body.querySelector('.archivefly');
  clone.dispatchEvent(new Event('animationend'));
  assert.equal(document.body.querySelector('.archivefly'), null);
});

test('a sweep archiving several rows staggers each flight 30ms behind the last', () => {
  const host = makeHost();
  const rows = ['t1', 't2', 't3', 't4', 't5', 't6', 't7'].map(
      (id) => makeRow(host, id, {left: 0, top: 0, width: 200, height: 30}));
  const clock = fakeClock();

  flyRowsToOverview(rows, {host, clock});

  assert.deepEqual(clock.scheduled, [0, 30, 60, 90, 120, 150, 150],
                    'the delay caps at 150ms rather than growing without bound');
});

test('a batch flight still lands every clone once its own delay elapses', () => {
  const host = makeHost();
  const rows = ['t1', 't2', 't3'].map(
      (id) => makeRow(host, id, {left: 0, top: 0, width: 200, height: 30}));
  const clock = fakeClock();

  flyRowsToOverview(rows, {host, clock});
  assert.equal(document.body.querySelectorAll('.archivefly').length, 0);
  clock.advance(60);
  assert.equal(document.body.querySelectorAll('.archivefly').length, 3);
});
