#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The history scrubber overlay.
//
//   node --test build/scripts/history-scrubber-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

test('history-scrubber.js is registered in atrium_resources.grd', () => {
  const grd =
      fs.readFileSync(path.join(ATRIUM_DIR, 'atrium_resources.grd'), 'utf8');
  assert.match(grd, /file="history-scrubber\.js"/);
});

test('sidebar.js imports history-scrubber.js', () => {
  const src = fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar.js'), 'utf8');
  assert.match(src, /from '\.\/history-scrubber\.js'/);
});

test('sidebarTimelineCommit is registered in the message drift guard', () => {
  const header = fs.readFileSync(
      path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
      'utf8');
  assert.match(header, /"sidebarTimelineCommit"/);
});

const TIMELINE = [
  {url: 'https://a.example/', title: 'A', at: 1},
  {url: 'https://b.example/', title: 'B', at: 2},
  {url: 'https://c.example/', title: 'C', at: 3, thumb: 'data:image/png;base64,cc'},
];

let handle;
let module;
let root;

test.beforeEach(async () => {
  handle = installDOM();
  root = document.createElement('div');
  handle.body.appendChild(root);
  module = await import(
      path.join(ATRIUM_DIR, 'history-scrubber.js') + `?t=${Date.now()}-${Math.random()}`);
});

function stops() {
  return [...root.querySelectorAll('.histstop')];
}

test('opening with nothing recorded opens no overlay', () => {
  const wrap = module.openHistoryScrubber(root, []);
  assert.equal(wrap, null);
  assert.equal(root.querySelector('.' + module.SCRUBBER_CLASS), null);
});

test('a stop with a thumb renders its image; one without renders a placeholder',
     () => {
       module.openHistoryScrubber(root, TIMELINE);
       const rows = stops();
       assert.equal(rows.length, 3);
       assert.equal(rows[0].querySelector('.histthumb').classList.contains('empty'),
                    true);
       assert.equal(rows[0].querySelector('.histthumbimg'), null);
       assert.equal(rows[2].querySelector('.histthumb').classList.contains('empty'),
                    false);
       assert.equal(rows[2].querySelector('.histthumbimg').src, TIMELINE[2].thumb);
       assert.deepEqual(rows.map((r) => r.querySelector('.histtitle').textContent),
                        ['A', 'B', 'C']);
     });

test('the newest stop is selected by default', () => {
  module.openHistoryScrubber(root, TIMELINE);
  const rows = stops();
  assert.deepEqual(rows.map((r) => r.classList.contains('sel')),
                   [false, false, true]);
});

test('dragging selects the nearest stop by center', () => {
  module.openHistoryScrubber(root, TIMELINE);
  const rows = stops();
  rows[0].setRect({left: 0, width: 96});
  rows[1].setRect({left: 106, width: 96});
  rows[2].setRect({left: 212, width: 96});

  window.dispatchEvent(new Event('pointermove', {clientX: 48}));
  assert.deepEqual(stops().map((r) => r.classList.contains('sel')),
                   [true, false, false]);

  window.dispatchEvent(new Event('pointermove', {clientX: 154}));
  assert.deepEqual(stops().map((r) => r.classList.contains('sel')),
                   [false, true, false]);
});

test('release commits the selected stop\'s url and index', () => {
  const sent = [];
  module.openHistoryScrubber(root, TIMELINE, {send: (n, a) => sent.push([n, a])});
  const rows = stops();
  rows[0].setRect({left: 0, width: 96});
  rows[1].setRect({left: 106, width: 96});
  rows[2].setRect({left: 212, width: 96});

  window.dispatchEvent(new Event('pointermove', {clientX: 48}));
  window.dispatchEvent(new Event('pointerup'));

  assert.deepEqual(sent, [['sidebarTimelineCommit', [TIMELINE[0].url, 0]]]);
  assert.equal(root.querySelector('.' + module.SCRUBBER_CLASS), null);
});

test('Escape sends nothing and closes the overlay', () => {
  const sent = [];
  const wrap = module.openHistoryScrubber(
      root, TIMELINE, {send: (n, a) => sent.push([n, a])});
  wrap.dispatchEvent(new Event('keydown', {key: 'Escape'}));

  assert.deepEqual(sent, []);
  assert.equal(root.querySelector('.' + module.SCRUBBER_CLASS), null);
});

test('closing drops every thumbnail\'s data url and the window listeners', () => {
  module.openHistoryScrubber(root, TIMELINE);
  const thumbImg = root.querySelector('.histthumbimg');
  const before = (window._listeners.get('pointermove') || []).length;
  assert.ok(before > 0, 'opening must arm the drag listener');

  window.dispatchEvent(new Event('pointerup'));

  assert.equal(thumbImg.src, '', 'the data: url itself must be dropped, not just the node');
  const after = (window._listeners.get('pointermove') || []).length;
  assert.equal(after, before - 1, 'closing must remove its own listener and no one else\'s');
});

test('an overlay already closed ignores a stray pointer event in flight', () => {
  const sent = [];
  module.openHistoryScrubber(root, TIMELINE, {send: (n, a) => sent.push([n, a])});
  module.closeHistoryScrubber(root);
  sent.length = 0;

  // No throw, and no second send - a listener left over from a race must not
  // act twice.
  window.dispatchEvent(new Event('pointerup'));
  assert.deepEqual(sent, []);
});

test('onClose runs exactly once, on commit as well as on Escape', () => {
  let closed = 0;
  module.openHistoryScrubber(root, TIMELINE, {onClose: () => closed++});
  window.dispatchEvent(new Event('pointerup'));
  assert.equal(closed, 1);

  closed = 0;
  const wrap =
      module.openHistoryScrubber(root, TIMELINE, {onClose: () => closed++});
  wrap.dispatchEvent(new Event('keydown', {key: 'Escape'}));
  assert.equal(closed, 1);
});

// The button's own wiring: a hold's reply opens the real overlay, and a
// snapshot push that lands mid-drag is deferred rather than tearing it out
// from under the gesture.
test('window.atriumSetVisitTimeline opens the overlay, and a push during the drag waits',
     async () => {
       const sidebar = await import(
           path.join(ATRIUM_DIR, 'sidebar.js') + `?t=${Date.now()}-${Math.random()}`);
       sidebar.installSidebar(root);

       window.atriumSetVisitTimeline(TIMELINE);
       assert.notEqual(root.querySelector('.' + module.SCRUBBER_CLASS), null);

       // A push during the drag must not call replaceChildren on root - that
       // would tear out the overlay's own DOM out from under the gesture.
       window.atriumSetSidebar({spaceName: 'Late', tabs: []});
       assert.notEqual(root.querySelector('.' + module.SCRUBBER_CLASS), null,
                       'a push mid-drag must not close the overlay');
       assert.equal(root.querySelector('.wsname'), null,
                    'a deferred push must not render either');

       window.dispatchEvent(new Event('pointerup'));
       assert.equal(root.querySelector('.' + module.SCRUBBER_CLASS), null);
       assert.notEqual(root.querySelector('.wsname'), null,
                       'the deferred push must drain once the overlay closes');
     });
