#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The downloads list is not covered here - no download feed reaches the WebUI yet.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import os from 'node:os';
import {installDOM, Event} from './dom-shim.mjs';
import {waitFor} from './wait-for.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');
const OVERVIEW_JS = path.join(ATRIUM_DIR, 'overview.js');

const {isMediaFile, dayLabel, filterItems, groupByDay} =
    await import(path.join(ATRIUM_DIR, 'downloads-section.js'));

// overview.js keeps per-module state, so each test needs its own instance -
// same cache-busting query as route-test.mjs.
let instance = 0;

// --- pure helpers -----------------------------------------------------------

test('isMediaFile: image/video extensions are media, documents are not', () => {
  assert.equal(isMediaFile('photo.PNG'), true);
  assert.equal(isMediaFile('clip.mp4'), true);
  assert.equal(isMediaFile('report.pdf'), false);
  assert.equal(isMediaFile('notes.txt'), false);
});

test('dayLabel: same calendar day is Today, one day back is Yesterday', () => {
  const now = new Date(2026, 0, 15, 18, 0, 0).getTime();
  const today = new Date(2026, 0, 15, 9, 0, 0).getTime();
  const yesterday = new Date(2026, 0, 14, 22, 0, 0).getTime();
  const older = new Date(2026, 0, 1, 12, 0, 0).getTime();
  assert.equal(dayLabel(today, now), 'Today');
  assert.equal(dayLabel(yesterday, now), 'Yesterday');
  assert.notEqual(dayLabel(older, now), 'Today');
  assert.notEqual(dayLabel(older, now), 'Yesterday');
});

test('filterItems: query matches by name, spaceId narrows to one space', () => {
  const items = [
    {name: 'invoice.pdf', spaceId: 's1'},
    {name: 'photo.png', spaceId: 's2'},
  ];
  assert.deepEqual(filterItems(items, 'invoice', null).map((i) => i.name),
      ['invoice.pdf']);
  assert.deepEqual(filterItems(items, '', 's2').map((i) => i.name),
      ['photo.png']);
});

test('groupByDay: preserves first-seen day order from pre-sorted items', () => {
  const now = new Date(2026, 0, 15).getTime();
  const items = [
    {name: 'a', receivedAt: new Date(2026, 0, 15).getTime()},
    {name: 'b', receivedAt: new Date(2026, 0, 15).getTime()},
    {name: 'c', receivedAt: new Date(2026, 0, 14).getTime()},
  ];
  const groups = groupByDay(items, now);
  assert.deepEqual([...groups.keys()], ['Today', 'Yesterday']);
  assert.equal(groups.get('Today').length, 2);
  assert.equal(groups.get('Yesterday').length, 1);
});

// --- rendered through the real rail pane ------------------------------------

// through renderOverview, the way buildRail reaches it. The
// /overview route these used to import went with the retired widget.
async function freshDom() {
  const dom = installDOM({pathname: '/sidebar'});
  const root = dom.document.createElement('div');
  root.setAttribute('id', 'atrium-root');
  dom.document.body.append(root);
  const {renderOverview} = await import(`${OVERVIEW_JS}?route=${instance++}`);
  renderOverview(root);
  return {dom, root};
}

test('media renders as tiles, documents as a day-grouped list, one ' +
         'search field filters both',
     async () => {
       const {dom, root} = await freshDom();

       const now = Date.now();
       dom.window.atriumSetDownloads([
         {id: 'm1', name: 'photo.png', receivedAt: now, sizeLabel: '2 MB'},
         {id: 'd1', name: 'report.pdf', receivedAt: now, sizeLabel: '1 MB'},
         {
           id: 'd2',
           name: 'old.txt',
           receivedAt: now - 24 * 60 * 60 * 1000,
           sizeLabel: '3 KB',
         },
       ]);

       const pane = root.querySelector('.ovpane');
       const tiles = pane.querySelectorAll('.dltile');
       assert.equal(tiles.length, 1, 'photo.png is the only media file');
       assert.equal(tiles[0].querySelector('.tname').textContent,
           'photo.png');

       const groups = pane.querySelectorAll('.ovgroup');
       assert.deepEqual(groups.map((g) => g.textContent), ['Today', 'Yesterday']);

       const rows = pane.querySelectorAll('.dlrow');
       assert.deepEqual(rows.map((r) => r.querySelector('.dln').textContent),
           ['report.pdf', 'old.txt']);

       const input = pane.querySelector('[data-dl-search]');
       input.value = 'old';
       input.dispatchEvent(new Event('input', {bubbles: true}));
       await waitFor(() => pane.querySelectorAll('.dltile').length === 0);

       assert.equal(pane.querySelectorAll('.dltile').length, 0,
           'the search field also narrows the media tiles');
       const filteredRows = pane.querySelectorAll('.dlrow');
       assert.deepEqual(
           filteredRows.map((r) => r.querySelector('.dln').textContent),
           ['old.txt']);
     });

test('filter chips list every space with a download and narrow the ' +
         'visible set when clicked',
     async () => {
       const {dom, root} = await freshDom();

       const now = Date.now();
       dom.window.atriumSetDownloads([
         {id: 'a', name: 'a.pdf', receivedAt: now, spaceId: 's1',
          spaceName: 'Work', spaceAccent: '#3366CC'},
         {id: 'b', name: 'b.pdf', receivedAt: now, spaceId: 's2',
          spaceName: 'Home'},
       ]);

       const pane = root.querySelector('.ovpane');
       // the bar is collapsed until the toggle opens it, which
       // is what the lab does - this test predates that and used to read the
       // chips straight off the mount.
       pane.querySelector('[data-dl-filter-toggle]').click();
       const chips = pane.querySelectorAll('.dlfchip');
       assert.deepEqual(chips.map((c) => c.textContent), ['All', 'Work', 'Home']);
       assert.ok(chips[0].classList.contains('on'), 'All starts active');
       assert.equal(chips[1].querySelector('.dlfdot').style.background,
           '#3366CC', 'the chip dot carries the space accent');

       chips[1].click();
       const visible = pane.querySelectorAll('.dlrow');
       assert.deepEqual(
           visible.map((r) => r.querySelector('.dln').textContent), ['a.pdf']);
       assert.ok(
           pane.querySelector('[data-dl-filter="s1"]').classList.contains('on'),
           'the clicked chip becomes active');

       pane.querySelector('[data-dl-filter="all"]').click();
       assert.equal(pane.querySelectorAll('.dlrow').length, 2,
           'All restores every space');
     });

test('Option swaps the same 18x18 remove button into delete-from-' +
         'disk behind a confirmation',
     async () => {
       const {dom, root} = await freshDom();

       dom.window.atriumSetDownloads(
           [{id: 'f1', name: 'file.pdf', receivedAt: Date.now()}]);

       const pane = root.querySelector('.ovpane');
       const removeBtn = pane.querySelector('[data-dl-remove="f1"]');
       assert.ok(removeBtn.classList.contains('dlact'));
       assert.ok(removeBtn.classList.contains('dlrmb'));

       const confirmCalls = [];
       dom.window.confirm = (message) => {
         confirmCalls.push(message);
         return true;
       };

       removeBtn.dispatchEvent(new Event('click', {altKey: false}));
       assert.deepEqual(dom.sent.at(-1), ['downloadsRemove', ['f1']]);
       assert.equal(confirmCalls.length, 0,
           'a plain click never asks for confirmation');

       removeBtn.dispatchEvent(new Event('click', {altKey: true}));
       assert.equal(confirmCalls.length, 1,
           'Option-click asks before deleting from disk');
       assert.match(confirmCalls[0], /file\.pdf/);
       assert.deepEqual(dom.sent.at(-1), ['downloadsDeleteFromDisk', ['f1']]);

       const sentBefore = dom.sent.length;
       dom.window.confirm = () => false;
       removeBtn.dispatchEvent(new Event('click', {altKey: true}));
       assert.equal(dom.sent.length, sentBefore,
           'declining the confirmation deletes nothing');

       assert.ok(pane.querySelector('[data-dl-remove="f1"]')
                     .classList.contains('dlrmb'),
           'the same button handles both branches - geometry never changes');
     });

test('an unknown-size download renders an indeterminate bar and a ' +
         'received-only caption',
     async () => {
       const {dom, root} = await freshDom();

       dom.window.atriumSetDownloads([{
         id: 'u1',
         name: 'stream.mp4',
         receivedAt: Date.now(),
         status: 'active',
         indeterminate: true,
         captionActive: '1.2 MB',
       }]);

       const pane = root.querySelector('.ovpane');
       const bar = pane.querySelector('.dlbar');
       assert.ok(bar.classList.contains('indeterminate'),
           'the bar must be marked indeterminate');
       assert.equal(bar.querySelector('span'), null,
           'no width-driven fill for an unknown size');
       assert.equal(pane.querySelector('.dlcap').textContent, '1.2 MB');
     });

// --- the chip bar is collapsed by default -------------------

test('the chip bar is hidden until the filter toggle opens it', async () => {
  // A row of space chips above every download list is a permanent cost for an
  // occasional filter, which is why the lab hides it (swipe-lab.html:3018).
  const {dom, root} = await freshDom();
  dom.window.atriumSetDownloads([
    {id: 'a', name: 'a.pdf', receivedAt: Date.now(), spaceId: 's1',
     spaceName: 'Work'},
  ]);

  const pane = root.querySelector('.ovpane');
  assert.equal(pane.querySelectorAll('.dlfchip').length, 0, 'chips start hidden');

  const toggle = pane.querySelector('[data-dl-filter-toggle]');
  assert.ok(toggle, 'no filter toggle rendered');
  toggle.click();
  assert.ok(pane.querySelectorAll('.dlfchip').length > 0, 'toggle did not open the bar');

  pane.querySelector('[data-dl-filter-toggle]').click();
  assert.equal(pane.querySelectorAll('.dlfchip').length, 0, 'toggle did not close it again');
});

test('a filter set while the bar is open still shows a dot once it is closed', async () => {
  // The dot is the ONLY sign a filter is on once the chips are hidden - without
  // it a collapsed bar and an unfiltered list look identical.
  const {dom, root} = await freshDom();
  const now = Date.now();
  dom.window.atriumSetDownloads([
    {id: 'a', name: 'a.pdf', receivedAt: now, spaceId: 's1', spaceName: 'Work'},
    {id: 'b', name: 'b.pdf', receivedAt: now, spaceId: 's2', spaceName: 'Home'},
  ]);

  const pane = root.querySelector('.ovpane');
  pane.querySelector('[data-dl-filter-toggle]').click();
  assert.equal(pane.querySelector('.factive'), null, 'no dot while the bar is open');

  [...pane.querySelectorAll('.dlfchip')].find((c) => c.textContent === 'Work').click();
  pane.querySelector('[data-dl-filter-toggle]').click();
  assert.ok(pane.querySelector('.factive'), 'a filtered, collapsed bar shows no dot');

  // And the filter really is still applied behind it.
  assert.deepEqual(
      pane.querySelectorAll('.dlrow').map((r) => r.querySelector('.dln').textContent),
      ['a.pdf']);
});
