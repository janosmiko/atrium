#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

//   node --test build/scripts/sidebar-flip-test.mjs

// The shim has no layout engine, so every rect here is declared by the test:
// what is asserted is the transform string and its clearing, never a pixel.

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
const {captureRowSlots, crossesPinnedLine, playRowFlip, FLIP_HOLD_CLASS,
       FLIP_RUN_CLASS} = await import(path.join(ATRIUM_DIR, 'sidebar-flip.js'));
const {installSidebar} = await import(path.join(ATRIUM_DIR, 'sidebar.js'));

function fakeClock() {
  let next = 1;
  const timers = new Map();
  return {
    set(ms, fn) {
      timers.set(next, {at: ms, fn});
      return next++;
    },
    clear(handle) {
      timers.delete(handle);
    },
    advance(ms) {
      for (const [handle, timer] of [...timers.entries()]) {
        timer.at -= ms;
        if (timer.at <= 0) {
          timers.delete(handle);
          timer.fn();
        }
      }
    },
    get scheduled() {
      return [...timers.values()].map((timer) => timer.at);
    },
  };
}

// One zone per section, rows at the tops the test declares. Rebuilding the
// list is calling this again - which is what renderSidebar does to the real
// one, and the reason a glide needs the old rects measured first.
function mountList({pinned = [], folder = [], tabs = []} = {}) {
  h.reset();
  const root = document.createElement('div');
  h.body.appendChild(root);
  const zones = [['pinned', pinned], ['folder:G1', folder], ['tabs', tabs]];
  for (const [zoneName, list] of zones) {
    const zone = document.createElement('div');
    zone.className = zoneName === 'tabs' ? 'rows' : 'foldin';
    zone.dataset.dropzone = zoneName;
    root.appendChild(zone);
    for (const spec of list) {
      const row = document.createElement('div');
      row.className = 'row tabrow';
      row.dataset.tabId = spec.id;
      row.setRect({left: spec.left ?? 0, top: spec.top, width: 200, height: 36});
      zone.appendChild(row);
    }
  }
  return root;
}

const rowById = (root, id) => root.querySelector(`[data-tab-id="${id}"]`);

// The two regular rows a fixture starts with, and the tree the first Cmd+D
// leaves behind: t1 in the pinned section, t2 risen into the gap.
const BEFORE_PIN = {tabs: [{id: 't1', top: 100}, {id: 't2', top: 140}]};
const AFTER_PIN = {pinned: [{id: 't1', top: 40}], tabs: [{id: 't2', top: 100}]};

function pinAndPlay(options = {}) {
  const slots = captureRowSlots(mountList(BEFORE_PIN));
  const root = mountList(AFTER_PIN);
  const moved = playRowFlip(root, slots, {reducedMotion: false, ...options});
  return {root, moved};
}

test('captureRowSlots keys the live rows by tab id and names their section', () => {
  const slots = captureRowSlots(mountList(
      {pinned: [{id: 'p1', top: 40}], tabs: [{id: 't1', top: 100}]}));

  assert.deepEqual([...slots.keys()], ['p1', 't1']);
  assert.equal(slots.get('p1').section, 'pinned');
  assert.equal(slots.get('t1').section, 'tabs');
  assert.equal(slots.get('t1').top, 100);
});

test('a pinned row with no live tab has no slot to glide from', () => {
  const slots = captureRowSlots(mountList(
      {pinned: [{id: '', top: 40}], tabs: [{id: 't1', top: 100}]}));

  assert.deepEqual([...slots.keys()], ['t1']);
});

// A duplicate id is a malformed snapshot, not a shape to average: the first
// row is the one the eye tracks, so it is the slot the glide starts from.
test('a duplicated tab id keeps the slot of the row drawn first', () => {
  const slots = captureRowSlots(mountList(
      {tabs: [{id: 't1', top: 100}, {id: 't1', top: 140}]}));

  assert.equal(slots.size, 1);
  assert.equal(slots.get('t1').top, 100);
});

// The gate that decides whether measuring is worth a layout flush at all.
test('a pin and an unpin both ask for a measurement', () => {
  const loose = {pinned: [], tabs: [{id: 't1'}]};
  const pinned = {pinned: [{id: 't1'}], tabs: []};

  assert.equal(crossesPinnedLine(loose, pinned), true);
  assert.equal(crossesPinnedLine(pinned, loose), true);
});

test('a push that only reorders the pinned list asks for no measurement', () => {
  const before = {pinned: [{id: 'a'}, {id: 'b'}], tabs: []};
  const after = {pinned: [{id: 'b'}, {id: 'a'}], tabs: []};

  assert.equal(crossesPinnedLine(before, after), false);
});

test('a push that leaves the sections alone asks for no measurement', () => {
  const before = {pinned: [{id: 'p1'}], tabs: [{id: 't1', title: 'First'}]};
  const after = {pinned: [{id: 'p1'}], tabs: [{id: 't1', title: 'First - loaded'}]};

  assert.equal(crossesPinnedLine(before, after), false);
});

// Cmd+D and the drop both reach a row inside a folder, and a folder's tabs
// live in their own list - so a snapshot gate that reads only pinned and tabs
// never sees that row leave, and nothing glides.
test('a tab pinned out of a folder glides too', () => {
  const before = {pinned: [], tabs: [{id: 't1'}],
                  folders: [{id: 'G1', tabs: [{id: 'g1'}]}]};
  const after = {pinned: [{id: 'g1'}], tabs: [{id: 't1'}],
                 folders: [{id: 'G1', tabs: []}]};
  assert.equal(crossesPinnedLine(before, after), true);

  const slots = captureRowSlots(
      mountList({folder: [{id: 'g1', top: 100}], tabs: [{id: 't1', top: 140}]}));
  const root = mountList({pinned: [{id: 'g1', top: 40}], tabs: [{id: 't1', top: 100}]});

  assert.equal(playRowFlip(root, slots, {reducedMotion: false}), 2);
  assert.equal(rowById(root, 'g1').style.transform, 'translate(0px, 60px)');
});

// A drag between a folder and the list crosses no pinned line, so it must not
// buy a measurement of its own.
test('a tab dragged into a folder asks for no measurement', () => {
  const before = {tabs: [{id: 't1'}], folders: [{id: 'G1', tabs: []}]};
  const after = {tabs: [], folders: [{id: 'G1', tabs: [{id: 't1'}]}]};

  assert.equal(crossesPinnedLine(before, after), false);
});

// An id the previous snapshot never held crossed nothing, and a new tab is
// the most frequent push there is.
test('a newly opened tab asks for no measurement', () => {
  const before = {pinned: [{id: 'p1'}], tabs: [{id: 't1'}]};
  const after = {pinned: [{id: 'p1'}], tabs: [{id: 't1'}, {id: 't2'}]};

  assert.equal(crossesPinnedLine(before, after), false);
});

// The rail's panes carry their own lists, so a switch replaces every row on
// screen - and walking all of them for a move nobody made is the cost this
// gate exists to avoid.
test('a space switch asks for no measurement', () => {
  const work = {activeSpace: 0, tabs: [{id: 't1'}], pinned: [{id: 'p1'}],
                spaces: [{tabs: [{id: 't1'}]}, {tabs: [{id: 't9'}]}]};
  const home = {activeSpace: 1, tabs: [{id: 't9'}], pinned: [],
                spaces: [{tabs: [{id: 't1'}]}, {tabs: [{id: 't9'}]}]};

  assert.equal(crossesPinnedLine(work, home), false);
});

test('a pin inside a rail pane still asks for a measurement', () => {
  const before = {activeSpace: 0, pinned: [], tabs: [{id: 't1'}],
                  spaces: [{pinned: [], tabs: [{id: 't1'}]}]};
  const after = {activeSpace: 0, pinned: [{id: 't1'}], tabs: [],
                 spaces: [{pinned: [{id: 't1'}], tabs: []}]};

  assert.equal(crossesPinnedLine(before, after), true);
});

test('the first push has nothing to compare against', () => {
  assert.equal(crossesPinnedLine(null, {pinned: [{id: 'p1'}]}), false);
});

test('a pin holds the moved row at its old slot', () => {
  const {root, moved} = pinAndPlay();

  assert.equal(moved, 2);
  assert.equal(rowById(root, 't1').style.transform, 'translate(0px, 60px)');
  assert.ok(rowById(root, 't1').classList.contains(FLIP_HOLD_CLASS));
});

test('the row below is held too, so the gap closes as a shift', () => {
  const {root} = pinAndPlay();

  assert.equal(rowById(root, 't2').style.transform, 'translate(0px, 40px)');
});

// The whole animation turns on this read. rAF callbacks run BEFORE the frame's
// style and layout, so a hold written and cleared with nothing in between is
// never computed: the row starts from transform:none and snaps into place.
test('the hold is read back before it is released', () => {
  const slots = captureRowSlots(mountList(BEFORE_PIN));
  const root = mountList(AFTER_PIN);
  const reads = [];
  for (const row of root.querySelectorAll('.row.tabrow')) {
    const real = row.getBoundingClientRect.bind(row);
    row.getBoundingClientRect = () => {
      reads.push(`${row.dataset.tabId}:${row.style.transform || 'none'}`);
      return real();
    };
  }

  playRowFlip(root, slots, {reducedMotion: false});

  assert.deepEqual(reads, [
    't1:none',
    't2:none',
    't1:translate(0px, 60px)',
    't2:translate(0px, 40px)',
  ]);
});

test('the next frame releases both rows toward their new slots', () => {
  const {root} = pinAndPlay();

  h.tick();

  for (const id of ['t1', 't2']) {
    const row = rowById(root, id);
    assert.equal(row.style.transform, '');
    assert.equal(row.classList.contains(FLIP_HOLD_CLASS), false);
    assert.ok(row.classList.contains(FLIP_RUN_CLASS));
  }
});

test('the transition ending drops the run class and its fallback timer', () => {
  const clock = fakeClock();
  const {root} = pinAndPlay({clock});
  h.tick();
  const row = rowById(root, 't1');

  row.dispatchEvent(new Event('transitionend', {propertyName: 'transform'}));

  assert.equal(row.classList.contains(FLIP_RUN_CLASS), false);
  assert.equal(clock.scheduled.length, 1, 't2 is still gliding, t1 is not');
});

test('a cancelled transition drops the run class too', () => {
  const {root} = pinAndPlay();
  h.tick();
  const row = rowById(root, 't1');

  row.dispatchEvent(new Event('transitioncancel', {propertyName: 'transform'}));

  assert.equal(row.classList.contains(FLIP_RUN_CLASS), false);
});

// A transition that never starts fires neither end nor cancel, and a row left
// holding a transform is drawn away from the slot every click is aimed at.
test('a glide that never transitions is released by the fallback', () => {
  const clock = fakeClock();
  const {root} = pinAndPlay({clock});
  h.tick();
  const row = rowById(root, 't1');
  assert.ok(row.classList.contains(FLIP_RUN_CLASS));

  clock.advance(300);

  assert.equal(row.classList.contains(FLIP_RUN_CLASS), false);
  assert.equal(row.style.transform, '');
});

test('a transitionend from a child control leaves the glide running', () => {
  const {root} = pinAndPlay();
  h.tick();
  const row = rowById(root, 't1');
  const close = document.createElement('button');
  row.appendChild(close);

  close.dispatchEvent(
      new Event('transitionend', {bubbles: true, propertyName: 'opacity'}));

  assert.ok(row.classList.contains(FLIP_RUN_CLASS));
});

test('an unpin glides the row back to the regular list', () => {
  const slots = captureRowSlots(mountList(AFTER_PIN));
  const root = mountList(BEFORE_PIN);

  assert.equal(playRowFlip(root, slots, {reducedMotion: false}), 2);
  assert.equal(rowById(root, 't1').style.transform, 'translate(0px, -60px)');
});

test('a rebuild that moves rows without crossing the pinned line is left alone', () => {
  const slots = captureRowSlots(mountList(BEFORE_PIN));
  const root = mountList({tabs: [{id: 't1', top: 160}, {id: 't2', top: 200}]});

  assert.equal(playRowFlip(root, slots, {reducedMotion: false}), 0);
  assert.equal(rowById(root, 't1').style.transform, undefined);
});

test('a push that moves nothing schedules no frame', () => {
  const slots = captureRowSlots(mountList(BEFORE_PIN));
  const root = mountList(BEFORE_PIN);

  assert.equal(playRowFlip(root, slots, {reducedMotion: false}), 0);
  assert.equal(h.pendingFrames, 0);
});

test('the first render has no old slots to glide from', () => {
  const root = mountList(AFTER_PIN);

  assert.equal(playRowFlip(root, new Map(), {reducedMotion: false}), 0);
  assert.equal(playRowFlip(root, null, {reducedMotion: false}), 0);
});

test('Reduce Motion puts the rows straight into their new slots', () => {
  const {root, moved} = pinAndPlay({reducedMotion: true});

  assert.equal(moved, 0);
  assert.equal(rowById(root, 't1').style.transform, undefined);
  assert.equal(rowById(root, 't1').classList.contains(FLIP_RUN_CLASS), false);
});

test('Reduce Motion is read from the media query when it is not passed', () => {
  const slots = captureRowSlots(mountList(BEFORE_PIN));
  const root = mountList(AFTER_PIN);
  h.setMatchMedia((query) => query.includes('prefers-reduced-motion'));

  assert.equal(playRowFlip(root, slots), 0);
  assert.equal(rowById(root, 't1').style.transform, undefined);
});

// The wiring, through the push entrypoint C++ calls. renderSidebar replaces
// the whole tree, so these prove the glide survives a real rebuild rather than
// the hand-built pair above.
function mountSidebar() {
  h.reset();
  const root = document.createElement('div');
  h.body.appendChild(root);
  installSidebar(root);
  return root;
}

const layOutRows = (root) => root.querySelectorAll('.row.tabrow').forEach(
    (row, i) => row.setRect({left: 0, top: 100 + i * 40, width: 200, height: 36}));

const TABS = [
  {id: 't1', title: 'First', url: 'https://one.example/'},
  {id: 't2', title: 'Second', url: 'https://two.example/'},
];

test('a pin push through atriumSetSidebar glides the row it moved', () => {
  const root = mountSidebar();
  h.window.atriumSetSidebar({spaceName: 'Work', tabs: TABS});
  layOutRows(root);

  h.window.atriumSetSidebar(
      {spaceName: 'Work', pinned: [TABS[0]], tabs: [TABS[1]]});

  const row = rowById(root, 't1');
  assert.equal(row.closest('[data-dropzone]').dataset.dropzone, 'pinned');
  assert.equal(row.style.transform, 'translate(0px, 100px)');
  h.tick();
  assert.equal(rowById(root, 't1').style.transform, '');
});

// The shipping path: one space still means the rail, and its panes carry their
// own rows - so a glide proven only on the legacy branch is proven nowhere.
test('a pin push glides inside a rail pane too', () => {
  const root = mountSidebar();
  const spaces = [{id: 's1', name: 'Work', color: '#4488cc'}];
  h.window.atriumSetSidebar(
      {spaceName: 'Work', activeSpace: 0, spaces, tabs: TABS});
  assert.ok(root.querySelectorAll('.railpane').length > 1, 'the rail rendered');
  layOutRows(root);

  h.window.atriumSetSidebar({
    spaceName: 'Work', activeSpace: 0, spaces,
    pinned: [TABS[0]], tabs: [TABS[1]],
  });

  const row = rowById(root, 't1');
  assert.equal(row.closest('[data-dropzone]').dataset.dropzone, 'pinned');
  assert.equal(row.style.transform, 'translate(0px, 100px)');
});

test('a push that pins nothing never measures, so nothing is held', () => {
  const root = mountSidebar();
  h.window.atriumSetSidebar({spaceName: 'Work', tabs: [TABS[0]]});
  layOutRows(root);

  h.window.atriumSetSidebar(
      {spaceName: 'Work', tabs: [{...TABS[0], title: 'First - loaded'}]});

  assert.equal(rowById(root, 't1').style.transform, undefined);
});

test('sidebar-flip.js is registered in atrium_resources.grd', () => {
  const grd = fs.readFileSync(path.join(ATRIUM_DIR, 'atrium_resources.grd'), 'utf8');
  assert.match(grd, /file="sidebar-flip\.js"/);
});

// Reduced motion is decided in JS, which never adds a class - so a rule for it
// here would be dead, and its absence is the thing worth guarding.
test('sidebar.css carries the hold and run rules, and no reduced-motion twin', () => {
  const css = fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar.css'), 'utf8');
  const reduced = css.split('@media (prefers-reduced-motion: reduce)').slice(1);

  assert.match(css, new RegExp(`\\.${FLIP_HOLD_CLASS}\\s*\\{`));
  assert.match(css, new RegExp(`\\.${FLIP_RUN_CLASS}\\s*\\{`));
  for (const block of reduced) {
    assert.equal(block.split('}')[0].includes(FLIP_RUN_CLASS), false);
  }
});

// sidebar.css serves every route. A surround painted on the bare native html
// class covered the whole window from the command bar's overlay document, and
// one scoped to the sidebar root hid the window's page glow on the column.
test('the native surround is not painted by the sidebar document', () => {
  const css = fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar.css'), 'utf8');
  assert.doesNotMatch(
      css, /html\.atrium-native-chrome:has\(#atrium-root\.sbroot\)\s*\{/);
  const bare = css.match(/\nhtml\.atrium-native-chrome\s*\{([^}]*)\}/);
  assert.ok(bare, 'the bare native rule still carries the ink tokens');
  assert.doesNotMatch(bare[1], /background:/);
});
