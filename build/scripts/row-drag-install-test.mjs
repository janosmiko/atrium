#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// what installRowDrag WIRES, as opposed to the index maths it calls.
//
//   make webui-test
//
// The maths is covered by row-drag-test.mjs and drop-decision-test.mjs. What is
// left is the wiring, where every bug is a silent one: which element a listener
// reads the section from, which rows it counts, whether the line the user sees
// is the row the browser is told about, and whether a finished drag leaves
// anything behind.
//
// BOUNDARY: this proves routing and decisions. It cannot prove a drag survives
// the browser's own cancellation rules - a reflow at dragstart can
// kill the drag before any dragover fires, and no shim can see that. That
// stays the VM probe's job.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event, makeDataTransfer} from './dom-shim.mjs';

const h = installDOM();

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {installRowDrag} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/sidebar-drag.js'));
const {EDGE_DWELL_MS} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/sidebar-paging.js'));

// The sidebar is drawn at x=1000 in every fixture, because the edge zones are
// local to it and a bug that forgets to subtract the root's own left offset
// only shows up when that offset is not zero.
const ROOT_LEFT = 1000;
const ROOT_WIDTH = 260;
// Clear of both edge zones, so an ordinary drag never arms a page.
const MID_X = ROOT_LEFT + 130;
const ROW_H = 30;

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

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

function addRow(parent, top) {
  const row = element('div', 'row tabrow',
                      {top, left: ROOT_LEFT, width: ROOT_WIDTH, height: ROW_H});
  row.dataset.tabId = 'tab-' + top;
  parent.appendChild(row);
  return row;
}

function addZone(parent, section) {
  const zone = element('div', 'rows');
  zone.dataset.dropzone = section;
  parent.appendChild(zone);
  return zone;
}

function sidebarRoot() {
  const root = element('div', 'sbroot',
                       {top: 0, left: ROOT_LEFT, width: ROOT_WIDTH, height: 600});
  h.body.appendChild(root);
  return root;
}

const fire = (node, type, init = {}) => {
  const event = new Event(type, {bubbles: true, clientX: MID_X, ...init});
  node.dispatchEvent(event);
  return event;
};

/**
 * Two panes of one space each, the shape buildSidebar renders: a favourites
 * grid, a pinned strip and a tab list in space 0, a second tab list in space 1.
 * Rows are 30px, stacked with no gaps, so a midpoint is a round number.
 */
function fixture(t, options = {}) {
  h.reset();
  const drops = [];
  const pages = [];
  const releases = [];

  const root = sidebarRoot();

  // The grid is a sibling of the rail, and the rail names the space showing.
  // A tile therefore has no pane to say which space a drag off it means.
  const favzone = element('div', 'favgrid',
                          {top: 0, left: ROOT_LEFT, width: ROOT_WIDTH, height: 40});
  favzone.dataset.dropzone = 'favorites';
  const tile = element('div', 'fav',
                       {top: 4, left: ROOT_LEFT + 4, width: 40, height: 32});
  // A favourite is identified by its url and has no id of its own, so a tile
  // without one names nothing to remove - which dropDecision refuses.
  tile.dataset.favId = 'https://fav.test/';
  favzone.appendChild(tile);
  root.appendChild(favzone);

  const rail = element('div', 'rail');
  rail.dataset.activeSpace = '0';
  root.appendChild(rail);

  const pane = element('div', 'railpane');
  pane.dataset.spaceIndex = '0';
  rail.appendChild(pane);

  const pinzone = addZone(pane, 'pinned');
  const pinned = [40, 70].map((top) => addRow(pinzone, top));

  const tabzone = addZone(pane, 'tabs');
  const rows = [100, 130, 160, 190].map((top) => addRow(tabzone, top));

  const other = element('div', 'railpane');
  other.dataset.spaceIndex = '1';
  rail.appendChild(other);
  const otherzone = addZone(other, 'tabs');
  const otherRows = [300, 330].map((top) => addRow(otherzone, top));

  // A row with no dropzone above it: the sidebar has rows outside every zone.
  const stray = addRow(root, 400);

  const drag = installRowDrag(root, {
    onDrop: (decision) => drops.push(decision),
    onEdgePage: (direction) => pages.push(direction),
    onRelease: (tabId, screenX, screenY) => releases.push({tabId, screenX, screenY}),
    ...options,
  });

  // The edge poll is a real interval, so a drag left in flight would keep the
  // test runner alive after the file is done.
  t.after(() => fire(root, 'dragend'));

  return {root, favzone, tile, pinzone, pinned, tabzone, rows, otherzone,
          otherRows, stray, drops, pages, releases, drag};
}

const line = (root) => root.querySelector('.dropline');
// A list that never needs the line never builds its node.
const lineShown = (root) => line(root) !== null && !line(root).hidden;
// A zone's children in DOM order: tab id, or 'ghost' for the preview row.
const order = (zone) => [...zone.children].map(
    (el) => el.classList.contains('rowghost') ? 'ghost' : el.dataset.tabId);
// The shim has no layout engine: this stacks the zone's children in DOM order,
// as the browser lays them out after a preview moved them.
function layOut(zone, top) {
  for (const el of zone.children) {
    el.setLayoutBox({top, height: ROW_H});
    top += ROW_H;
  }
}

// --- dragstart: what starts a drag, and what it records ----------------------

test('a drag starts from anywhere inside the row', (t) => {
  const f = fixture(t);
  // The pointer is over the title span, never the row element itself.
  const title = element('span', 'tl');
  f.rows[1].appendChild(title);

  fire(title, 'dragstart');

  assert.equal(f.rows[1].classList.contains('dragging'), true);
  assert.equal(f.root.classList.contains('dragactive'), true,
               'the root marks a live drag, which is what the CSS keys off');
});

test('the favourites hint sits where .dragactive on the root can reach it, ' +
         'and drop or dragend clears the class again',
     (t) => {
       const f = fixture(t);
       const hint = element('div', 'favhint');
       f.favzone.appendChild(hint);
       assert.equal(f.root.classList.contains('dragactive'), false);

       fire(f.rows[0], 'dragstart', {dataTransfer: makeDataTransfer()});
       assert.equal(f.root.classList.contains('dragactive'), true);
       assert.equal(f.root.contains(hint), true,
                     'still under the same root the class lands on');

       fire(f.tabzone, 'drop', {clientY: 115});
       assert.equal(f.root.classList.contains('dragactive'), false, 'drop clears it');

       fire(f.rows[0], 'dragstart', {dataTransfer: makeDataTransfer()});
       assert.equal(f.root.classList.contains('dragactive'), true);
       fire(f.root, 'dragend');
       assert.equal(f.root.classList.contains('dragactive'), false, 'dragend clears it too');
     });

test('the payload carries the source index', (t) => {
  const f = fixture(t);
  const dataTransfer = makeDataTransfer();
  // A header among the rows, so the row's position among the zone's children
  // and its index among the DRAGGABLE rows are different numbers - otherwise
  // the payload agrees with the index it is supposed to be by accident.
  f.tabzone.prepend(element('div', 'sechdr',
                            {top: 70, left: ROOT_LEFT, width: ROOT_WIDTH, height: ROW_H}));

  fire(f.rows[2], 'dragstart', {dataTransfer});

  // Chromium refuses to start a drag whose dataTransfer carries nothing, so
  // this is not decoration even though both ends of the drag are this surface.
  assert.equal(dataTransfer.getData('application/x-atrium-tab'), '2');
  assert.equal(dataTransfer.effectAllowed, 'move');
});

// A tile used to be left entirely to
// installFavoriteDrag, which owns reordering INSIDE the grid and refuses
// anything outside it - so a favourite dragged to the tab list resolved to
// nothing and the grid was a one-way door. This module now takes the tile too,
// for the leaving half. The two do not collide: fav-drag previews and commits
// only over its own grid, and answers nothing anywhere else.
test('a tile is claimed for the drags that LEAVE the grid', (t) => {
  const f = fixture(t);

  fire(f.tile, 'dragstart', {dataTransfer: makeDataTransfer()});

  assert.equal(f.root.classList.contains('dragactive'), true);
  assert.equal(fire(f.rows[0], 'dragover', {clientY: 176}).defaultPrevented,
               true, 'a drop into the tab list has to be allowed');
});

test('a tile dropped in the tab list resolves to unfavorite', (t) => {
  const f = fixture(t);

  fire(f.tile, 'dragstart', {dataTransfer: makeDataTransfer()});
  fire(f.rows[0], 'dragover', {clientY: 176});
  fire(f.rows[0], 'drop', {clientY: 176});

  assert.equal(f.drops.length, 1, 'the drop reached the host');
  assert.equal(f.drops[0].kind, 'unfavorite');
  assert.equal(f.drops[0].toSection, 'tabs');
  assert.ok(f.drops[0].url, 'the favourite is named by its url, which is its id');
});

test('a tile dropped in the pinned strip resolves to unfavorite there', (t) => {
  const f = fixture(t);

  fire(f.tile, 'dragstart', {dataTransfer: makeDataTransfer()});
  fire(f.pinned[0], 'dragover', {clientY: 46});
  fire(f.pinned[0], 'drop', {clientY: 46});

  assert.equal(f.drops.length, 1);
  assert.equal(f.drops[0].kind, 'unfavorite');
  assert.equal(f.drops[0].toSection, 'pinned');
});

test('a row outside every dropzone starts nothing', (t) => {
  const f = fixture(t);

  fire(f.stray, 'dragstart');

  assert.equal(f.root.classList.contains('dragactive'), false);
  fire(f.tabzone, 'drop', {clientY: 176});
  assert.deepEqual(f.drops, [], 'a row with no section resolves to no message');
});

test('the index counts draggable rows, not every child', (t) => {
  const f = fixture(t);
  // A section header inside the zone would shift every index by one if the
  // rows were addressed by child position - and the browser side translates
  // that index straight back through the persisted array.
  f.tabzone.prepend(element('div', 'sechdr',
                            {top: 70, left: ROOT_LEFT, width: ROOT_WIDTH, height: ROW_H}));

  fire(f.rows[0], 'dragstart');
  fire(f.tabzone, 'drop', {clientY: 300});

  assert.deepEqual(f.drops, [{kind: 'reorder', from: 0, to: 3}]);
});

// --- folder heads --------------------------------------------------
// A folder head is also its own [data-dropzone] (the 'folder:' group target),
// so hovering one while dragging a folder must redirect to the folders list.

function folderFixture(t) {
  h.reset();
  const drops = [];
  const root = sidebarRoot();
  const folderList = addZone(root, 'folders');
  const heads = [40, 70, 100].map((top) => {
    const head = element('div', 'row frow',
                         {top, left: ROOT_LEFT, width: ROOT_WIDTH, height: ROW_H});
    head.dataset.folderId = 'f-' + top;
    head.dataset.dropzone = 'folder:f-' + top;
    folderList.appendChild(head);
    return head;
  });
  installRowDrag(root, {
    rowSelector: '.tabrow, .frow',
    onDrop: (decision) => drops.push(decision),
  });
  t.after(() => fire(root, 'dragend'));
  return {root, folderList, heads, drops};
}

test('a folder head can be picked up and reorders among folders', (t) => {
  const f = folderFixture(t);

  fire(f.heads[0], 'dragstart');
  fire(f.heads[2], 'drop', {clientY: 96});

  assert.deepEqual(f.drops, [{kind: 'reorderFolder', from: 0, to: 1}]);
});

test('hovering another folder head while dragging a folder resolves to the ' +
         'folders list, not that head\'s own group target',
     (t) => {
       const f = folderFixture(t);

       fire(f.heads[0], 'dragstart');
       const over = fire(f.heads[2], 'dragover', {clientY: 96});

       assert.equal(over.defaultPrevented, true, 'a valid reorder target');
       assert.equal(line(f.root).hidden, false,
                     'a line, not the group highlight the head would show for a tab');
     });

// --- pinned folders: one item space shared by tabs and folder heads ----------
// Members are draggable now (the group-out gesture), and a pinned folder's
// own head is one item among the pinned tabs, not a section of its own.

function pinnedFolderFixture(t) {
  h.reset();
  const drops = [];
  const root = sidebarRoot();
  const pinzone = addZone(root, 'pinned');
  const pin0 = addRow(pinzone, 0);

  const head = element('div', 'row frow',
                       {top: 30, left: ROOT_LEFT, width: ROOT_WIDTH, height: ROW_H});
  head.dataset.folderId = 'G1';
  head.dataset.dropzone = 'folder:G1';
  pinzone.appendChild(head);

  const fold = element('div', 'fold');
  const foldin = element('div', 'foldin');
  foldin.dataset.dropzone = 'folder:G1';
  const member = element('div', 'row tabrow',
                         {top: 60, left: ROOT_LEFT, width: ROOT_WIDTH, height: ROW_H});
  member.dataset.tabId = 'member-0';
  foldin.appendChild(member);
  fold.appendChild(foldin);
  pinzone.appendChild(fold);

  const pin1 = addRow(pinzone, 90);
  const tabzone = addZone(root, 'tabs');
  const tabs = [150, 180].map((top) => addRow(tabzone, top));

  installRowDrag(root, {
    rowSelector: '.tabrow, .frow',
    onDrop: (decision) => drops.push(decision),
  });
  t.after(() => fire(root, 'dragend'));
  return {root, pinzone, pin0, head, fold, foldin, member, pin1, tabzone, tabs,
          drops};
}

test('the pinned zone counts a folder head as one item, not its members or ' +
         'its wrapper',
     (t) => {
       const f = pinnedFolderFixture(t);
       const items = [...f.pinzone.children].filter(
           (el) => el.matches('.tabrow, .frow'));
       assert.deepEqual(items, [f.pin0, f.head, f.pin1]);
     });

test('a row inside a folder can be picked up and leaves the group on drop', (t) => {
  const f = pinnedFolderFixture(t);

  fire(f.member, 'dragstart');
  // Inside the reorder band, below the row's midpoint.
  fire(f.tabs[0], 'drop', {clientY: 153});

  assert.deepEqual(f.drops,
      [{kind: 'ungroup', groupID: 'G1', member: 0, toSection: 'tabs', to: 0}]);
});

test('a pinned tab dropped above the folder\'s only member joins it first', (t) => {
  const f = pinnedFolderFixture(t);

  fire(f.pin1, 'dragstart');
  // Above the member's midpoint (75).
  fire(f.member, 'drop', {clientY: 65});

  assert.deepEqual(f.drops, [{kind: 'group', from: 2, groupID: 'G1',
                              section: 'pinned', memberSlot: 0}]);
});

test('a pinned tab dropped below the folder\'s only member joins it last', (t) => {
  const f = pinnedFolderFixture(t);

  fire(f.pin1, 'dragstart');
  // Below the member's midpoint (75): the append slot, carried as no slot.
  fire(f.member, 'drop', {clientY: 85});

  assert.deepEqual(f.drops,
      [{kind: 'group', from: 2, groupID: 'G1', section: 'pinned'}]);
});

test('a collapsed pinned folder head drop appends, not prepends', (t) => {
  const f = pinnedFolderFixture(t);

  fire(f.pin1, 'dragstart');
  // The head has no body rows to measure - boxes is empty regardless of y.
  fire(f.head, 'drop', {clientY: 35});

  assert.deepEqual(f.drops,
      [{kind: 'group', from: 2, groupID: 'G1', section: 'pinned'}]);
});

test('a pinned folder head reorders among pinned items', (t) => {
  const f = pinnedFolderFixture(t);

  fire(f.head, 'dragstart');
  // Past pin1's midpoint (105): the head moves from item 1 to after pin1.
  fire(f.pin1, 'drop', {clientY: 110});

  assert.deepEqual(f.drops, [{kind: 'reorderPinned', from: 1, to: 2}]);
});

test('a pinned folder head moves with its body, not as a copy', (t) => {
  const f = pinnedFolderFixture(t);
  fire(f.head, 'dragstart');

  fire(f.pin1, 'dragover', {clientY: 117});

  assert.deepEqual([...f.pinzone.children], [f.pin0, f.pin1, f.head, f.fold]);
  assert.equal(f.pinzone.querySelector('.rowghost'), null);

  fire(f.root, 'dragend');
  assert.deepEqual([...f.pinzone.children], [f.pin0, f.head, f.fold, f.pin1]);
});

test('an external drag reads its source from the list at rest', (t) => {
  const f = fixture(t);
  const {drag} = f;
  fire(f.rows[0], 'dragstart');
  fire(f.rows[2], 'dragover', {clientY: 187});
  fire(f.rows[2], 'drop', {clientY: 187});

  // The held preview still has tab-100 third. A drag from the devbar
  // before the rebuild must not take that as tab-100's slot.
  drag.beginExternal('tab-100');

  assert.deepEqual(order(f.tabzone), ['tab-100', 'tab-130', 'tab-160', 'tab-190']);
  assert.equal(drag.dragSource().index, 0);
  drag.endExternal();
});

// --- dragover: what it allows, and what it paints ----------------------------

test('dragover allows the drop', (t) => {
  const f = fixture(t);
  const dataTransfer = makeDataTransfer();
  fire(f.rows[0], 'dragstart', {dataTransfer});

  const over = fire(f.rows[2], 'dragover', {clientY: 176, dataTransfer});

  // Without preventDefault the browser never fires drop at all, so the whole
  // gesture dies here silently.
  assert.equal(over.defaultPrevented, true);
  assert.equal(dataTransfer.dropEffect, 'move');
});

test('dragover with no drag in flight allows nothing', (t) => {
  const f = fixture(t);

  const over = fire(f.rows[2], 'dragover', {clientY: 176});

  assert.equal(over.defaultPrevented, false);
});

test('the dragged row moves to the slot the pointer resolves to', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');

  // Rows are 100..220 in 30px steps, 187 is inside the reorder band, past
  // the third row's midpoint.
  fire(f.rows[2], 'dragover', {clientY: 187});
  assert.deepEqual(order(f.tabzone), ['tab-130', 'tab-160', 'tab-100', 'tab-190']);
  assert.equal(lineShown(f.root), false, 'a list shows the row, not the line');

  // Measured at rest: the row the move put under 157 does not count.
  fire(f.rows[1], 'dragover', {clientY: 157});
  assert.deepEqual(order(f.tabzone), ['tab-130', 'tab-100', 'tab-160', 'tab-190']);
});

test('a row from another list shows as a ghost copy', (t) => {
  const f = fixture(t);
  fire(f.pinned[0], 'dragstart');

  fire(f.rows[0], 'dragover', {clientY: 103});

  assert.deepEqual(order(f.tabzone),
                   ['ghost', 'tab-100', 'tab-130', 'tab-160', 'tab-190']);
  assert.deepEqual(order(f.pinzone), ['tab-40', 'tab-70'],
                   'the source row stays in its own list');
  assert.equal(f.tabzone.children[0].hasAttribute('draggable'), false);
});

test('the row goes home where the drop would change nothing', (t) => {
  const f = fixture(t);
  fire(f.rows[1], 'dragstart');
  fire(f.rows[2], 'dragover', {clientY: 187});
  assert.deepEqual(order(f.tabzone), ['tab-100', 'tab-160', 'tab-130', 'tab-190']);
  layOut(f.tabzone, 100);

  // The top of the row that moved up into the dragged row's old place.
  fire(f.rows[2], 'dragover', {clientY: 133});

  assert.deepEqual(order(f.tabzone), ['tab-100', 'tab-130', 'tab-160', 'tab-190']);
});

test('a pointer inside the moved row keeps it there', (t) => {
  const f = fixture(t);
  fire(f.rows[1], 'dragstart');
  fire(f.rows[0], 'dragover', {clientY: 103});
  assert.deepEqual(order(f.tabzone), ['tab-130', 'tab-100', 'tab-160', 'tab-190']);
  layOut(f.tabzone, 100);

  // At rest this was the middle of the first row, a split band. On screen it
  // is the dragged row, so the row must not jump home.
  fire(f.rows[1], 'dragover', {clientY: 115});

  assert.deepEqual(order(f.tabzone), ['tab-130', 'tab-100', 'tab-160', 'tab-190']);
  assert.equal(f.rows[0].classList.contains('dropsplit'), false);
});

test('a split band on a row the preview moved keeps the preview', (t) => {
  const f = fixture(t);
  fire(f.rows[1], 'dragstart');
  fire(f.rows[0], 'dragover', {clientY: 103});
  layOut(f.tabzone, 100);

  // The first row now lies at 130..160, so 145 is its middle.
  fire(f.rows[0], 'dragover', {clientY: 145});

  assert.equal(f.rows[0].dataset.splitSide, 'bottom');
  assert.deepEqual(order(f.tabzone), ['tab-130', 'tab-100', 'tab-160', 'tab-190']);
  assert.equal(f.root.querySelector('.dragfollow').hidden, true,
               'the drawn row would cover the band');

  fire(f.rows[0], 'drop', {clientY: 145});
  assert.deepEqual(f.drops, [{kind: 'split', side: 'bottom',
                              draggedTabId: 'tab-130', targetTabId: 'tab-100'}]);
  assert.deepEqual(order(f.tabzone), ['tab-100', 'tab-130', 'tab-160', 'tab-190'],
                   'a split moves no row, so no preview waits for the rebuild');
});

test('a split band in another list hides the ghost row but keeps its place', (t) => {
  const f = fixture(t);
  fire(f.pinned[0], 'dragstart');
  fire(f.rows[1], 'dragover', {clientY: 133});
  layOut(f.tabzone, 100);
  const ghost = f.tabzone.children[1];

  // The second row now lies at 160..190.
  fire(f.rows[1], 'dragover', {clientY: 175});

  assert.equal(f.rows[1].dataset.splitSide, 'bottom');
  assert.deepEqual(order(f.tabzone),
                   ['tab-100', 'ghost', 'tab-130', 'tab-160', 'tab-190']);
  assert.equal(ghost.style.visibility, 'hidden');

  fire(f.rows[1], 'dragover', {clientY: 145});
  assert.equal(ghost.style.visibility, '', 'back on the ghost, it shows again');
  assert.equal(f.rows[1].classList.contains('dropsplit'), false);
});

test('the drawn row comes back when the pointer leaves the split band', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');
  fire(f.rows[2], 'dragover', {clientY: 175});
  assert.equal(f.root.querySelector('.dragfollow').hidden, true);

  fire(f.rows[2], 'dragover', {clientY: 187});

  assert.equal(f.root.querySelector('.dragfollow').hidden, false);
});

test('dragend puts the list back', (t) => {
  const f = fixture(t);
  fire(f.pinned[0], 'dragstart');
  fire(f.rows[2], 'dragover', {clientY: 187});
  assert.equal(order(f.tabzone).includes('ghost'), true);

  fire(f.root, 'dragend');

  assert.deepEqual(order(f.tabzone), ['tab-100', 'tab-130', 'tab-160', 'tab-190']);
});

test('leaving the sidebar puts the list back', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');
  fire(f.rows[2], 'dragover', {clientY: 187});
  // Inside the sidebar: the pointer only moved to another element.
  fire(f.rows[2], 'dragleave', {relatedTarget: f.rows[1]});
  assert.deepEqual(order(f.tabzone), ['tab-130', 'tab-160', 'tab-100', 'tab-190']);

  // Out onto the page: a tear-off from here lands nowhere in this list.
  fire(f.rows[2], 'dragleave', {relatedTarget: null});

  assert.deepEqual(order(f.tabzone), ['tab-100', 'tab-130', 'tab-160', 'tab-190']);
});

test('a committed drop holds the preview for the rebuild', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');
  fire(f.rows[2], 'dragover', {clientY: 187});
  fire(f.rows[2], 'drop', {clientY: 187});

  // The rebuild replaces the list. Until then the row stays where it went,
  // so the list does not jump back and forward again.
  assert.deepEqual(order(f.tabzone), ['tab-130', 'tab-160', 'tab-100', 'tab-190']);

  // The next drag starts from the list as it is at rest.
  fire(f.rows[3], 'dragstart');
  assert.deepEqual(order(f.tabzone), ['tab-100', 'tab-130', 'tab-160', 'tab-190']);
});

test('a favourite over a list shows a tab row, not a tile', (t) => {
  const f = fixture(t, {rowLook: builtRow});
  fire(f.tile, 'dragstart');

  fire(f.rows[1], 'dragover', {clientY: 133});

  const ghost = f.tabzone.children[1];
  assert.equal(ghost.classList.contains('rowghost'), true);
  assert.equal(ghost.classList.contains('row'), true);
  assert.equal(ghost.classList.contains('fav'), false);

  const follow = f.root.querySelector('.dragfollow');
  assert.equal(follow.dataset.look, 'row', 'the drag image is a row too');
  fire(f.tile, 'dragover', {clientY: 20});
  assert.equal(follow.dataset.look, 'tile', 'and a tile again over the grid');

  fire(f.root, 'dragend');
  assert.equal(f.root.querySelector('.dragfollow'), null);
  assert.deepEqual(order(f.tabzone), ['tab-100', 'tab-130', 'tab-160', 'tab-190']);
});

// What sidebar.js hands in: its own row and tile builders, ids and all.
function builtRow(tile) {
  const row = element('div', 'row tabrow sel');
  row.dataset.tabId = 'built';
  const title = element('span', 'tl');
  title.textContent = 'row for ' + tile.dataset.favId;
  row.appendChild(title);
  return row;
}

function builtTile(row) {
  const tile = element('div', 'fav');
  tile.dataset.favId = 'built';
  tile.dataset.from = row.dataset.tabId;
  tile.draggable = true;
  return tile;
}

test('a favourite over a list draws the tab row the sidebar builds for it', (t) => {
  const f = fixture(t, {rowLook: builtRow});
  fire(f.tile, 'dragstart');
  fire(f.rows[1], 'dragover', {clientY: 133});

  const ghost = f.tabzone.children[1];
  const drawn = f.root.querySelector('.dragfollow .row');
  for (const row of [ghost, drawn]) {
    assert.equal(row.querySelector('.tl').textContent, 'row for https://fav.test/');
    assert.equal(row.classList.contains('tablook'), true, 'painted as a tab row');
    assert.equal(row.classList.contains('tabrow'), false,
                 'every lookup of the real rows goes by .tabrow');
    assert.equal(row.dataset.tabId, undefined);
  }
  assert.deepEqual(order(f.tabzone), ['tab-100', 'ghost', 'tab-130', 'tab-160', 'tab-190']);
});

test('a tab row over the grid draws a tile, and its slot shows that tile faded', (t) => {
  const f = gridFixture(t, {tileLook: builtTile});
  fire(f.rows[0], 'dragstart', {dataTransfer: makeDataTransfer()});
  fire(f.favzone, 'dragover', {clientX: TILE_CENTRES[1] + 2, clientY: 20});

  const follow = f.root.querySelector('.dragfollow');
  assert.equal(follow.dataset.look, 'tile');
  assert.equal(follow.firstChild.dataset.from, 'tab-100');
  const ghost = tilesOf(f.favzone)[ghostAt(f.favzone)];
  assert.equal(ghost.dataset.from, 'tab-100', 'the gap is the tile the row becomes');
  assert.equal(ghost.dataset.favId, undefined);
  assert.equal(ghost.draggable, false);

  fire(f.rows[2], 'dragover', {clientY: 163});
  assert.equal(follow.dataset.look, 'row', 'a row again over the list');
  assert.equal(follow.querySelector('[data-tab-id]'), null);
});

test('a folder head over the grid gets a blank gap, not a tile of its name', (t) => {
  const f = gridFixture(t, {tileLook: builtTile});
  f.rows[0].classList.add('frow');
  fire(f.rows[0], 'dragstart', {dataTransfer: makeDataTransfer()});
  fire(f.favzone, 'dragover', {clientX: TILE_CENTRES[1] + 2, clientY: 20});

  const ghost = tilesOf(f.favzone)[ghostAt(f.favzone)];
  assert.equal(ghost.dataset.from, undefined);
});

test('a tab row hands Blink a blank drag image, a split row keeps its own', (t) => {
  const f = fixture(t);
  const images = [];
  const transfer = () => ({...makeDataTransfer(),
                           setDragImage: (el) => images.push(el.className)});
  fire(f.rows[0], 'dragstart', {dataTransfer: transfer()});
  fire(f.root, 'dragend');
  assert.deepEqual(images, ['dragblank']);

  f.rows[1].classList.add('splitrow');
  fire(f.rows[1], 'dragstart', {dataTransfer: transfer()});
  assert.deepEqual(images, ['dragblank'], 'no drawn copy of a split row');
});

test('a grid shows the ghost tile, with no ring and no line', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');
  fire(f.rows[2], 'dragover', {clientY: 187});

  fire(f.tile, 'dragover', {clientY: 20});

  assert.equal(f.favzone.querySelectorAll('.favghost').length, 1);
  assert.equal(f.favzone.classList.contains('dropinto'), false);
  assert.equal(lineShown(f.root), false,
               'tiles wrap, so there is no meaningful line between two of them');
});

test('a ghost tile in the pinned grid grows every pane room by one slot', (t) => {
  const f = fixture(t);
  const room = element('div', 'favgrid');
  room.setAttribute('aria-hidden', 'true');
  room.appendChild(element('div', 'favslot'));
  f.root.querySelector('.railpane').prepend(room);
  const slots = () => room.querySelectorAll('.favslot').length;

  fire(f.rows[0], 'dragstart');
  fire(f.tile, 'dragover', {clientY: 20});
  assert.equal(slots(), 2, 'the room grows with the grid it holds a place for');

  fire(f.pinned[1], 'dragover', {clientY: 80});
  assert.equal(slots(), 1);

  fire(f.tile, 'dragover', {clientY: 20});
  fire(f.root, 'dragend');
  assert.equal(slots(), 1);
});

test('a drag that leaves the sidebar closes the grid preview', (t) => {
  const f = fixture(t);
  const room = element('div', 'favgrid');
  room.setAttribute('aria-hidden', 'true');
  f.root.querySelector('.railpane').prepend(room);

  fire(f.rows[0], 'dragstart');
  fire(f.tile, 'dragover', {clientY: 20});
  assert.equal(room.querySelectorAll('.favslot').length, 1);

  // dragover stops once the pointer is off this surface, so dragleave is the
  // last chance to put the rows back.
  fire(f.favzone, 'dragleave', {relatedTarget: null});

  assert.equal(room.querySelectorAll('.favslot').length, 0);
  assert.equal(f.favzone.querySelectorAll('.favghost').length, 0);
});

test('a list with no rows takes the ghost row', (t) => {
  // The first pin can only be made by dragging into a section that renders
  // nothing, so an empty list has to show where the drop lands.
  h.reset();
  const root = sidebarRoot();
  const tabzone = addZone(root, 'tabs');
  const rows = [100, 130].map((top) => addRow(tabzone, top));
  const pinzone = addZone(root, 'pinned');
  installRowDrag(root, {onDrop: () => {}});
  t.after(() => fire(root, 'dragend'));

  fire(rows[0], 'dragstart');
  fire(pinzone, 'dragover', {clientY: 60});

  assert.deepEqual(order(pinzone), ['ghost']);
  assert.deepEqual(order(tabzone), ['tab-100', 'tab-130']);
});

test('an empty grid opens for a drag, with its room, and shows no ghost tile', (t) => {
  const f = fixture(t);
  f.tile.remove();
  f.favzone.classList.add('empty');
  const room = element('div', 'favgrid empty');
  room.setAttribute('aria-hidden', 'true');
  f.root.querySelector('.railpane').prepend(room);
  const open = () => [f.favzone, room].map((g) => g.classList.contains('dropopen'));

  fire(f.rows[0], 'dragstart');
  fire(f.favzone, 'dragover', {clientY: 20});
  assert.deepEqual(open(), [true, true]);
  assert.equal(f.favzone.querySelectorAll('.favghost').length, 0,
               'the hint marks the drop, not a lone tile');

  fire(f.pinned[1], 'dragover', {clientY: 80});
  assert.deepEqual(open(), [false, false]);

  fire(f.favzone, 'dragover', {clientY: 20});
  fire(f.favzone, 'drop', {clientY: 20});
  assert.deepEqual(f.drops, [{kind: 'favorite', from: 0, fromSection: 'tabs', to: 0}]);
});

test('a grid drop holds the ghost tile and the room for the rebuild', (t) => {
  const f = fixture(t);
  const room = element('div', 'favgrid');
  room.setAttribute('aria-hidden', 'true');
  f.root.querySelector('.railpane').prepend(room);
  const shown = () => [f.favzone.querySelectorAll('.favghost').length,
                       room.querySelectorAll('.favslot').length];

  fire(f.rows[0], 'dragstart');
  fire(f.tile, 'dragover', {clientY: 20});
  fire(f.tile, 'drop', {clientY: 20});
  fire(f.root, 'dragend');
  assert.deepEqual(shown(), [1, 1], 'the grid does not shrink before the rebuild');

  fire(f.rows[1], 'dragstart');
  assert.deepEqual(shown(), [0, 0], 'the next drag starts from the grid at rest');
});

test('only the zone under the pointer stays highlighted', (t) => {
  const f = fixture(t);
  const head = element('div', 'row frow',
                       {top: 220, left: ROOT_LEFT, width: ROOT_WIDTH, height: ROW_H});
  head.dataset.dropzone = 'folder:G1';
  f.root.querySelector('.railpane').appendChild(head);
  fire(f.rows[0], 'dragstart');
  fire(head, 'dragover', {clientY: 230});
  assert.equal(head.classList.contains('dropinto'), true);

  fire(f.pinned[1], 'dragover', {clientY: 80});

  assert.equal(head.classList.contains('dropinto'), false);
});

// --- drop: what onDrop is told -----------------------------------------------

test('onDrop is called once, with the decision', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');
  fire(f.rows[2], 'dragover', {clientY: 187});

  fire(f.rows[2], 'drop', {clientY: 187});

  assert.deepEqual(f.drops, [{kind: 'reorder', from: 0, to: 2}]);
});

test('a drop that changes nothing does not call onDrop', (t) => {
  const f = fixture(t);
  fire(f.rows[1], 'dragstart');

  fire(f.rows[1], 'drop', {clientY: 150});

  assert.deepEqual(f.drops, []);
});

test('the drop is consumed, not handed back to the browser', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');

  // Chromium's default action for a drop carrying text/plain is to navigate,
  // so a drop this module handles but does not consume reorders the list AND
  // loads '0' as a URL in the tab under the pointer.
  assert.equal(fire(f.rows[2], 'drop', {clientY: 176}).defaultPrevented, true);

  // Including the drops that commit nothing: the gesture was still ours.
  fire(f.rows[1], 'dragstart');
  assert.equal(fire(f.rows[1], 'drop', {clientY: 150}).defaultPrevented, true);
});

test('a drop outside every dropzone commits nothing and still ends the drag', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');

  fire(f.root, 'drop', {clientY: 500});

  assert.deepEqual(f.drops, []);
  assert.equal(f.root.classList.contains('dragactive'), false);
  assert.equal(f.rows[0].classList.contains('dragging'), false);
});

test('the section comes from the zone under the pointer, not the source', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');

  // Pinned rows are 40..100, 73 is inside the reorder band, above the
  // second one's midpoint.
  fire(f.pinned[1], 'drop', {clientY: 73});

  assert.deepEqual(f.drops, [{kind: 'pin', from: 0, to: 1}]);
});

test('a pinned row dropped below the pinned section becomes a regular tab', (t) => {
  const f = fixture(t);
  fire(f.pinned[0], 'dragstart');

  fire(f.rows[0], 'drop', {clientY: 90});

  assert.deepEqual(f.drops, [{kind: 'unpin', from: 0, to: 0}]);
});

test('the space name shows the ghost row under it and pins the tab dropped on it', (t) => {
  const f = fixture(t);
  // A bare zone stands in for the rendered .wsname header - sidebar.js gives
  // it the same data-dropzone, with no rows of its own to lay out.
  const name = element('div', 'wsname');
  name.dataset.dropzone = 'pinned';
  f.root.querySelector('.railpane').insertBefore(name, f.pinzone);

  fire(f.rows[0], 'dragstart');
  fire(name, 'dragover', {clientY: 10});
  assert.equal(name.classList.contains('dropinto'), false, 'no ring');
  assert.equal(name.nextElementSibling.classList.contains('rowghost'), true);

  fire(f.rows[2], 'dragover', {clientY: 167});
  assert.equal(f.root.querySelector('.railpane > .rowghost'), null,
               'the ghost leaves the name with the pointer');

  fire(name, 'dragover', {clientY: 10});
  fire(name, 'drop', {clientY: 10});
  assert.deepEqual(f.drops, [{kind: 'pin', from: 0, to: 0}]);
});

test('the space name puts the ghost row first in an open pinned body', (t) => {
  const f = fixture(t);
  const pane = f.root.querySelector('.railpane');
  const name = element('div', 'wsname frow');
  name.dataset.dropzone = 'pinned';
  const fold = element('div', 'fold');
  const foldin = element('div', 'foldin');
  foldin.dataset.dropzone = 'pinned';
  addRow(foldin, 40);
  fold.appendChild(foldin);
  pane.prepend(name, fold);

  fire(f.rows[0], 'dragstart');
  fire(name, 'dragover', {clientY: 10});
  assert.deepEqual(order(foldin), ['ghost', 'tab-40']);

  fire(name, 'drop', {clientY: 10});
  assert.deepEqual(f.drops, [{kind: 'pin', from: 0, to: 0}]);
});

test('the space name over a folded pinned body puts the ghost under the name', (t) => {
  const f = fixture(t);
  const pane = f.root.querySelector('.railpane');
  const name = element('div', 'wsname frow');
  name.dataset.dropzone = 'pinned';
  const fold = element('div', 'fold closed');
  const foldin = element('div', 'foldin');
  foldin.dataset.dropzone = 'pinned';
  addRow(foldin, 40);
  fold.appendChild(foldin);
  pane.prepend(name, fold);

  fire(f.rows[0], 'dragstart');
  fire(name, 'dragover', {clientY: 10});
  assert.equal(name.nextElementSibling.classList.contains('rowghost'), true);
  assert.deepEqual(order(foldin), ['tab-40']);
});

test('a drop on another pane carries both spaces', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');

  fire(f.otherRows[1], 'drop', {clientY: 333});

  // The source space is the pane the row is DRAWN in, not the rail's active
  // one: paging mid-drag has already changed which pane is on screen.
  assert.deepEqual(f.drops, [{
    kind: 'moveToSpace',
    from: 0,
    fromSection: 'tabs',
    fromSpace: 0,
    toSpace: 1,
    to: 1,
  }]);
});

test("the source space is the row's own pane, not the first one rendered", (t) => {
  const f = fixture(t);
  // The mirror of the drag above, and the only shape that tells the row's own
  // pane apart from the rail's first one: this drag STARTS in the second pane.
  fire(f.otherRows[0], 'dragstart');

  fire(f.rows[1], 'drop', {clientY: 157});

  assert.deepEqual(f.drops, [{
    kind: 'moveToSpace',
    from: 0,
    fromSection: 'tabs',
    fromSpace: 1,
    toSpace: 0,
    to: 2,
  }]);
});

test('a sidebar with no panes still reorders', (t) => {
  // Panes are a rail thing; the surface renders without them (and the pill
  // shares this module), so a missing data-space-index must not read as a
  // cross-space move.
  h.reset();
  const drops = [];
  const root = sidebarRoot();
  const zone = addZone(root, 'tabs');
  const rows = [100, 130, 160].map((top) => addRow(zone, top));
  installRowDrag(root, {onDrop: (decision) => drops.push(decision)});

  fire(rows[0], 'dragstart');
  fire(rows[2], 'drop', {clientY: 187});

  assert.deepEqual(drops, [{kind: 'reorder', from: 0, to: 2}]);
});

// --- geometry ----------------------------------------------------------------

test('the row the user sees is the slot the drop commits to', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');

  fire(f.rows[2], 'dragover', {clientY: 187});
  assert.deepEqual(order(f.tabzone), ['tab-130', 'tab-160', 'tab-100', 'tab-190'],
                   'moved under the third row');

  fire(f.rows[2], 'drop', {clientY: 187});

  // 2 counts the list with the dragged row already gone, which puts it under
  // the third row - where the preview put it. A preview that disagrees with
  // the commit is the bug this pairing exists to catch.
  assert.deepEqual(f.drops, [{kind: 'reorder', from: 0, to: 2}]);
});

test('the slot follows a scroll of the list under a still pointer', (t) => {
  const f = fixture(t);
  f.tabzone.setRect({top: 0, left: ROOT_LEFT, width: ROOT_WIDTH, height: 400});
  fire(f.rows[0], 'dragstart');
  fire(f.rows[2], 'dragover', {clientY: 187});

  // The same pointer position, after a held drag scrolled the list 60px up.
  f.tabzone.setRect({top: -60, left: ROOT_LEFT, width: ROOT_WIDTH, height: 400});
  f.rows.forEach((row, i) => row.setRect({top: 40 + i * ROW_H, height: ROW_H}));
  fire(f.tabzone, 'drop', {clientY: 187});

  // The rows are measured once, from the list's own top, because each preview
  // moves them. The list's top is read live, so a scroll still counts.
  assert.deepEqual(f.drops, [{kind: 'reorder', from: 0, to: 3}]);
});

// --- dragend and edge paging --------------------------------------------------

test('dragend clears the drag classes and every highlight', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');
  fire(f.tile, 'dragover', {clientY: 20});

  fire(f.root, 'dragend');

  assert.equal(f.rows[0].classList.contains('dragging'), false);
  assert.equal(f.root.classList.contains('dragactive'), false);
  assert.equal(f.root.querySelectorAll('[data-dropzone].dropinto').length, 0);
});

// A folder body still draws the line: two members, at y 60 and 90.
function twoMemberFolder(t) {
  const f = pinnedFolderFixture(t);
  const second = element('div', 'row tabrow',
                         {top: 90, left: ROOT_LEFT, width: ROOT_WIDTH, height: ROW_H});
  second.dataset.tabId = 'member-1';
  f.foldin.appendChild(second);
  return {...f, second};
}

test('neither way out of a drag leaves the line painted', (t) => {
  const f = twoMemberFolder(t);
  fire(f.member, 'dragstart');
  fire(f.second, 'dragover', {clientY: 110});
  assert.equal(line(f.root).hidden, false);

  fire(f.second, 'drop', {clientY: 110});

  // One indicator element serves every drag, so a drag that ends without
  // hiding it leaves a line lying across the sidebar until the next dragover -
  // pointing at a slot from a gesture that is already over.
  assert.equal(line(f.root).hidden, true, 'after a committed drop');

  fire(f.member, 'dragstart');
  fire(f.second, 'dragover', {clientY: 110});
  assert.equal(line(f.root).hidden, false);

  fire(f.root, 'dragend');

  assert.equal(line(f.root).hidden, true,
               'and after a drag released over nothing');
});

test('a drop after dragend commits nothing', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');
  fire(f.root, 'dragend');

  fire(f.rows[2], 'drop', {clientY: 176});

  // dragend fires for a drag released over nothing, which is the only signal
  // that the gesture is over - the recorded source has to go with it.
  assert.deepEqual(f.drops, []);
});

// --- dragend: the release point ----------------------------------

test('a release outside the sidebar with no resolved drop reports the screen point', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');

  // Never over the sidebar's own DOM at all - the only way 'drop' never fires
  // and `from` survives to see this dragend. clientX/Y are far outside the
  // root's rect (1000..1260 x 0..600); screenX/Y are the OS-absolute point
  // that the tear-off tracking hit-tests other windows against.
  fire(f.root, 'dragend',
       {clientX: 2000, clientY: 300, screenX: 2500, screenY: 800});

  assert.deepEqual(f.releases, [{tabId: 'tab-100', screenX: 2500, screenY: 800}]);
});

test('a release inside the sidebar but over no dropzone reports nothing', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');

  // The stray row sits inside root's own DOM, so 'drop' fires by bubbling even
  // though it resolves to no section - and that alone must be enough to keep
  // dragend from treating this as a release outside the sidebar.
  fire(f.stray, 'drop', {clientY: 400});
  fire(f.root, 'dragend', {clientX: 2000, clientY: 300});

  assert.deepEqual(f.releases, []);
});

test('a dragend that resolved to a drop kind reports nothing, however far it ends', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');

  fire(f.rows[2], 'drop', {clientY: 187});
  assert.deepEqual(f.drops, [{kind: 'reorder', from: 0, to: 2}]);

  // The pointer can still be miles from the sidebar by the time dragend fires
  // (it fires after 'drop', not at the same coordinates) - what has to matter
  // is that 'drop' already resolved a decision, not where dragend lands.
  fire(f.root, 'dragend', {clientX: 5000, clientY: 5000});

  assert.deepEqual(f.releases, []);
});

test('a release from a row with no tabId warns instead of going quiet', (t) => {
  const f = fixture(t);
  const warnings = [];
  const realWarn = console.warn;
  console.warn = (...args) => warnings.push(args.join(' '));
  t.after(() => {
    console.warn = realWarn;
  });

  // No row type renders without data-tab-id today. This is the guard for the
  // one that does: release routing drives tear-off, cross-window moves and
  // splits, and a silent return loses the whole gesture with nothing to read.
  delete f.rows[0].dataset.tabId;
  fire(f.rows[0], 'dragstart');
  fire(f.root, 'dragend',
       {clientX: 2000, clientY: 300, screenX: 2500, screenY: 800});

  assert.deepEqual(f.releases, [], 'nothing addressable to release');
  assert.equal(warnings.length, 1);
  // The section and index are what name the row: they are the only identity
  // the drag ever recorded, and the pair is enough to find it in the render.
  assert.match(warnings[0], /tabs/);
  assert.match(warnings[0], /\b0\b/);
});

test('an ordinary release still reports, and says nothing', (t) => {
  const f = fixture(t);
  const warnings = [];
  const realWarn = console.warn;
  console.warn = (...args) => warnings.push(args.join(' '));
  t.after(() => {
    console.warn = realWarn;
  });

  fire(f.rows[0], 'dragstart');
  fire(f.root, 'dragend',
       {clientX: 2000, clientY: 300, screenX: 2500, screenY: 800});

  // Every row rendered today carries a tabId, so a guard that warns on the
  // normal path would put a line in the console on every tear-off.
  assert.deepEqual(f.releases, [{tabId: 'tab-100', screenX: 2500, screenY: 800}]);
  assert.deepEqual(warnings, []);
});

test('holding at the leading edge pages back a space', async (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');

  // 5px inside the sidebar's own left edge. Measured against the root box, not
  // the rail: without subtracting the root's offset this x reads as trailing
  // and pages the wrong way.
  fire(f.rows[0], 'dragover', {clientX: ROOT_LEFT + 5, clientY: 120});
  h.setNow(EDGE_DWELL_MS + 50);
  await sleep(160);

  assert.deepEqual(f.pages, ['previous']);
});

test('holding at the trailing edge pages forward', async (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');

  fire(f.rows[0], 'dragover',
       {clientX: ROOT_LEFT + ROOT_WIDTH - 5, clientY: 120});
  h.setNow(EDGE_DWELL_MS + 50);
  await sleep(160);

  assert.deepEqual(f.pages, ['next']);
});

test('a drag away from either edge never pages', async (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');

  // An ordinary reorder holds still over the list for as long as it likes.
  fire(f.rows[0], 'dragover', {clientY: 176});
  h.setNow(EDGE_DWELL_MS * 10);
  await sleep(160);

  assert.deepEqual(f.pages, []);
});

test('a drag that ended stops paging', async (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');
  fire(f.rows[0], 'dragover', {clientX: ROOT_LEFT + 5, clientY: 120});

  fire(f.root, 'dragend');
  h.setNow(EDGE_DWELL_MS * 10);
  await sleep(160);

  assert.deepEqual(f.pages, []);
});

test('a drag that leaves the sidebar surface stops paging', async (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');
  fire(f.rows[0], 'dragover', {clientX: ROOT_LEFT + 5, clientY: 120});

  // relatedTarget null is what a genuine exit off this WebContents looks
  // like - dragover cannot fire again to say so.
  fire(f.root, 'dragleave', {relatedTarget: null});
  h.setNow(EDGE_DWELL_MS + 50);
  await sleep(160);

  assert.deepEqual(f.pages, []);
});

test('dragleave inside the sidebar does not disarm the poll', async (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');
  fire(f.rows[0], 'dragover', {clientX: ROOT_LEFT + 5, clientY: 120});

  // Moving from one row to another still fires dragleave on the row, but the
  // pointer never left root.
  fire(f.rows[0], 'dragleave', {relatedTarget: f.rows[1]});
  h.setNow(EDGE_DWELL_MS + 50);
  await sleep(160);

  assert.deepEqual(f.pages, ['previous']);
});

test('paging resumes when the pointer re-enters after leaving the sidebar', async (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');
  fire(f.rows[0], 'dragover', {clientX: ROOT_LEFT + 5, clientY: 120});

  // A genuine exit stops the poll (relatedTarget null), same as the
  // 'stops paging' test above - the drag then comes back without a drop.
  fire(f.root, 'dragleave', {relatedTarget: null});
  fire(f.rows[0], 'dragover', {clientX: ROOT_LEFT + 5, clientY: 120});
  h.setNow(EDGE_DWELL_MS + 50);
  await sleep(160);

  assert.deepEqual(f.pages, ['previous']);
});

test('a new drag starts with no edge armed', async (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');
  // Armed at an edge, then released before the dwell was ever served.
  fire(f.rows[0], 'dragover', {clientX: ROOT_LEFT + 5, clientY: 120});
  fire(f.root, 'dragend');

  fire(f.rows[0], 'dragstart');
  h.setNow(EDGE_DWELL_MS * 10);
  await sleep(160);

  // The dwell is measured from when the edge was entered, so a stale one is
  // already overdue: the next drag would page the moment it began, before the
  // pointer had gone anywhere near an edge.
  assert.deepEqual(f.pages, []);
});

// --- a member reorders inside its own folder body, never via the head -------

test('a member dropped on a sibling inside its own folder reorders', (t) => {
  const f = pinnedFolderFixture(t);
  const second = element('div', 'row tabrow',
                         {top: 90, left: ROOT_LEFT, width: ROOT_WIDTH, height: ROW_H});
  second.dataset.tabId = 'member-1';
  f.foldin.appendChild(second);

  fire(f.member, 'dragstart');
  fire(second, 'dragover', {clientY: 110});
  assert.equal(line(f.root).hidden, false, 'the line shows between siblings');
  fire(second, 'drop', {clientY: 110});

  assert.deepEqual(f.drops, [{kind: 'reorderMember', groupID: 'G1', from: 0, to: 1}]);
});

test('a member over its own folder head is not a target at all', (t) => {
  const f = pinnedFolderFixture(t);
  fire(f.member, 'dragstart');
  fire(f.head, 'dragover', {clientY: 45});
  assert.equal(line(f.root), null, 'no line');
  assert.equal(f.head.classList.contains('dropinto'), false,
               'no join highlight either - the row is already a member');
  fire(f.head, 'drop', {clientY: 45});

  assert.deepEqual(f.drops, []);
});

// --- the drop line outlives a rail rebuild ------------------------------------

test('the drop line comes back after a rebuild detached it', (t) => {
  const f = twoMemberFolder(t);
  fire(f.member, 'dragstart');
  fire(f.second, 'dragover', {clientY: 110});
  assert.equal(line(f.root).hidden, false, 'the first drag shows a line');
  fire(f.second, 'drop', {clientY: 110});

  // apply() rebuilds the rail with replaceChildren after every push, which
  // takes the line node out of the DOM with everything else.
  line(f.root).remove();
  assert.equal(line(f.root), null);

  fire(f.member, 'dragstart');
  fire(f.second, 'dragover', {clientY: 110});
  assert.notEqual(line(f.root), null, 'the second drag draws a line again');
  assert.equal(line(f.root).hidden, false);
});

// --- split bands of a row: the split affordance ------------------------------

// rows[2] spans y 160..190 and x 1000..1260, so a quarter is 7.5px tall and
// 65px wide.
const LEFT_X = ROOT_LEFT + 20;
const RIGHT_X = ROOT_LEFT + ROOT_WIDTH - 20;

test('each split band marks the row with the side the split takes', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');

  for (const [side, point] of [['left', {clientX: LEFT_X, clientY: 175}],
                               ['right', {clientX: RIGHT_X, clientY: 175}],
                               ['top', {clientY: 170}],
                               ['bottom', {clientY: 180}]]) {
    fire(f.rows[2], 'dragover', point);
    assert.equal(f.rows[2].classList.contains('dropsplit'), true, side);
    assert.equal(f.rows[2].dataset.splitSide, side);
    assert.equal(lineShown(f.root), false,
                 'the reorder line and the side band never show together');
  }
});

test('the row\'s exact midpoint now falls in the bottom split, not a reorder', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');

  // the old no-side middle is gone, down 0.5 is not < 0.5 so it
  // resolves to 'bottom' rather than the reorder line.
  fire(f.rows[2], 'dragover', {clientY: 175});

  assert.equal(f.rows[2].classList.contains('dropsplit'), true);
  assert.equal(f.rows[2].dataset.splitSide, 'bottom');
  assert.equal(lineShown(f.root), false);
});

test('moving off an edge takes the side band with it', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');
  fire(f.rows[2], 'dragover', {clientX: RIGHT_X, clientY: 175});
  assert.equal(f.rows[2].classList.contains('dropsplit'), true);

  fire(f.rows[2], 'dragover', {clientY: 187});

  assert.equal(f.rows[2].classList.contains('dropsplit'), false);
  assert.equal(f.rows[2].dataset.splitSide, undefined);
});

test('a drop in a split band commits a split naming both tabs', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');
  fire(f.rows[2], 'dragover', {clientX: RIGHT_X, clientY: 175});
  fire(f.rows[2], 'drop', {clientX: RIGHT_X, clientY: 175});

  assert.deepEqual(f.drops, [{kind: 'split', side: 'right',
                              draggedTabId: 'tab-100', targetTabId: 'tab-160'}]);
  assert.equal(f.rows[2].classList.contains('dropsplit'), false,
               'the drop clears the affordance with the drag');
});

test('a folder member dropped on a tab row edge splits', (t) => {
  const f = pinnedFolderFixture(t);

  fire(f.member, 'dragstart');
  // The 'top' split band of the first tab row, which spans y 150..180.
  fire(f.tabs[0], 'dragover', {clientY: 160});
  fire(f.tabs[0], 'drop', {clientY: 160});

  assert.deepEqual(f.drops, [{kind: 'split', side: 'top',
                              draggedTabId: 'member-0', targetTabId: 'tab-150'}]);
});

test('an open split takes the split bands away and leaves the reorder', (t) => {
  h.reset();
  const drops = [];
  const root = sidebarRoot();
  const zone = addZone(root, 'tabs');
  const rows = [0, 30].map((top) => addRow(zone, top));
  let split = true;
  installRowDrag(root, {onDrop: (d) => drops.push(d), splitActive: () => split});
  t.after(() => fire(root, 'dragend'));

  fire(rows[0], 'dragstart');
  fire(rows[1], 'dragover', {clientX: ROOT_LEFT + ROOT_WIDTH - 20, clientY: 45});
  assert.equal(rows[1].classList.contains('dropsplit'), false,
               'no side band while a split is already up');
  assert.deepEqual(order(zone), ['tab-30', 'tab-0'],
                   'the reorder preview stands in for it');

  fire(rows[1], 'drop', {clientX: ROOT_LEFT + ROOT_WIDTH - 20, clientY: 45});
  assert.deepEqual(drops, [{kind: 'reorder', from: 0, to: 1}]);

  split = false;
  fire(rows[0], 'dragstart');
  fire(rows[1], 'dragover', {clientX: ROOT_LEFT + ROOT_WIDTH - 20, clientY: 45});
  assert.equal(rows[1].dataset.splitSide, 'right',
               'the split bands come back once the split is gone');
});

test('dragend clears the side band', (t) => {
  const f = fixture(t);
  fire(f.rows[0], 'dragstart');
  fire(f.rows[2], 'dragover', {clientX: LEFT_X, clientY: 175});

  fire(f.root, 'dragend');

  assert.equal(f.rows[2].classList.contains('dropsplit'), false);
  assert.equal(f.rows[2].dataset.splitSide, undefined);
});

// --- a row dragged into the favourites grid -----------------------
//
// The grid used to light up whole and append whatever landed on it, so the tile
// appeared somewhere the pointer had never been.

// Centres at 1024, 1068 and 1112, so a clientX either side of one of them names
// a slot without landing on a boundary. Three tiles, because "which tile is
// nearest" is the question and one tile cannot ask it.
const TILE_CENTRES = [1024, 1068, 1112];

function gridFixture(t, options) {
  const f = fixture(t, options);
  for (const left of [ROOT_LEFT + 48, ROOT_LEFT + 92]) {
    const tile = element('div', 'fav', {top: 4, left, width: 40, height: 32});
    tile.dataset.favId = `https://fav${left}.test/`;
    f.favzone.appendChild(tile);
  }
  return f;
}

const tilesOf = (zone) => [...zone.querySelectorAll('.fav')];
const ghostAt = (zone) =>
    tilesOf(zone).findIndex((t) => t.classList.contains('favghost'));

test('a row dragged over the grid opens a gap at the slot under the pointer', (t) => {
  const f = gridFixture(t);
  fire(f.rows[0], 'dragstart', {dataTransfer: makeDataTransfer()});

  fire(f.favzone, 'dragover', {clientX: TILE_CENTRES[1] + 2, clientY: 20});

  assert.equal(ghostAt(f.favzone), 2,
               'past the middle tile\'s centre means after it');
  fire(f.favzone, 'drop', {clientX: TILE_CENTRES[1] + 2, clientY: 20});
  assert.deepEqual(f.drops,
                   [{kind: 'favorite', from: 0, fromSection: 'tabs', to: 2}]);
});

function addTiles(zone, count) {
  for (let i = 0; i < count; i++) {
    const tile = element('div', 'fav', {top: 40 + i * 36, left: ROOT_LEFT + 4,
                                        width: 40, height: 32});
    tile.dataset.favId = `https://extra${i}.test/`;
    zone.appendChild(tile);
  }
}

test('a full grid opens no gap and takes no drop', (t) => {
  const f = gridFixture(t);
  addTiles(f.favzone, 9);
  fire(f.rows[0], 'dragstart', {dataTransfer: makeDataTransfer()});

  fire(f.favzone, 'dragover', {clientX: TILE_CENTRES[1] + 2, clientY: 20});

  assert.equal(ghostAt(f.favzone), -1);
  assert.equal(f.favzone.classList.contains('dropinto'), false);
  assert.equal(f.root.querySelector('.dragfollow').dataset.look, 'row',
               'the row does not turn into a tile it cannot become');
  fire(f.favzone, 'drop', {clientX: TILE_CENTRES[1] + 2, clientY: 20});
  assert.deepEqual(f.drops, []);
});

test('the gap in the last free slot does not count as a tile', (t) => {
  const f = gridFixture(t);
  addTiles(f.favzone, 8);
  fire(f.rows[0], 'dragstart', {dataTransfer: makeDataTransfer()});

  fire(f.favzone, 'dragover', {clientX: TILE_CENTRES[1] + 2, clientY: 20});
  fire(f.favzone, 'dragover', {clientX: TILE_CENTRES[1] + 3, clientY: 20});

  assert.equal(ghostAt(f.favzone), 2);
});

test('the gap moves with the pointer and the drop commits what it showed', (t) => {
  const f = gridFixture(t);
  fire(f.rows[1], 'dragstart', {dataTransfer: makeDataTransfer()});

  fire(f.favzone, 'dragover', {clientX: TILE_CENTRES[0] - 2, clientY: 20});
  assert.equal(ghostAt(f.favzone), 0, 'left of the first centre is the front');

  fire(f.favzone, 'dragover', {clientX: TILE_CENTRES[2] + 2, clientY: 20});
  assert.equal(ghostAt(f.favzone), 3, 'past the last centre is the end');

  fire(f.favzone, 'drop', {clientX: TILE_CENTRES[2] + 2, clientY: 20});
  assert.deepEqual(f.drops,
                   [{kind: 'favorite', from: 1, fromSection: 'tabs', to: 3}]);
});

test('a pointer that leaves the grid closes the gap again', (t) => {
  const f = gridFixture(t);
  const before = tilesOf(f.favzone);
  fire(f.rows[0], 'dragstart', {dataTransfer: makeDataTransfer()});

  fire(f.favzone, 'dragover', {clientX: TILE_CENTRES[1] + 2, clientY: 20});
  assert.equal(ghostAt(f.favzone), 2);

  fire(f.rows[2], 'dragover', {clientX: LEFT_X, clientY: 175});

  assert.equal(ghostAt(f.favzone), -1, 'the gap goes with the pointer');
  assert.deepEqual(tilesOf(f.favzone), before,
                   'and the tiles are back in the order they started in');
});

// sidebar-fav-drag owns a tile's reorder and moves the tile itself. A ghost
// here put the tile back in its old slot, under an outline at the new one.
test('a favourite dragged over the grid gets no gap', (t) => {
  const f = gridFixture(t);
  const before = tilesOf(f.favzone);
  fire(before[0], 'dragstart', {dataTransfer: makeDataTransfer()});

  fire(f.favzone, 'dragover', {clientX: TILE_CENTRES[2] + 2, clientY: 20});

  assert.equal(ghostAt(f.favzone), -1);
  assert.deepEqual(tilesOf(f.favzone), before);
});

// Blink takes the drop operation from dragenter on the update where the element
// under the pointer changes. The dragged tile reflows under a still pointer, and
// an uncancelled dragenter then turned the release into no drop at all.
test('a drag cancels dragenter so a reflowed tile under the pointer still drops', (t) => {
  const f = gridFixture(t);
  const tile = tilesOf(f.favzone)[0];
  assert.equal(fire(tile, 'dragenter').defaultPrevented, false,
               'no drag, nothing to accept');

  fire(tile, 'dragstart', {dataTransfer: makeDataTransfer()});

  assert.equal(fire(tile, 'dragenter').defaultPrevented, true);
});

// A tile dropped on a folder used to resolve to nothing, so the drag looked
// like it had gone into a hole.
function tileOverFolder(t) {
  const f = pinnedFolderFixture(t);
  const favzone = element('div', 'favgrid',
                          {top: 200, left: ROOT_LEFT, width: ROOT_WIDTH, height: 40});
  favzone.dataset.dropzone = 'favorites';
  const tile = element('div', 'fav',
                       {top: 204, left: ROOT_LEFT + 4, width: 40, height: 32});
  tile.dataset.favId = 'https://fav.test/';
  favzone.appendChild(tile);
  f.root.appendChild(favzone);
  return {...f, favzone, tile};
}

test('a favourite dropped in an open folder joins it at the pointed slot', (t) => {
  const f = tileOverFolder(t);
  fire(f.tile, 'dragstart', {dataTransfer: makeDataTransfer()});

  // The top half of the only member, which is the slot before it.
  fire(f.member, 'drop', {clientX: LEFT_X, clientY: 65});

  assert.deepEqual(f.drops, [{kind: 'favoriteToFolder', url: 'https://fav.test/',
                              groupID: 'G1', memberSlot: 0}]);
});

test('a favourite dropped on a collapsed folder head becomes its last member', (t) => {
  const f = tileOverFolder(t);
  // A collapsed folder draws its head and no body, so there is no member row
  // to measure a slot against.
  f.fold.remove();
  fire(f.tile, 'dragstart', {dataTransfer: makeDataTransfer()});

  fire(f.head, 'drop', {clientX: LEFT_X, clientY: 45});

  assert.deepEqual(f.drops, [{kind: 'favoriteToFolder', url: 'https://fav.test/',
                              groupID: 'G1', memberSlot: -1}]);
});

test('a drag that ends without a drop leaves the grid as it found it', (t) => {
  const f = gridFixture(t);
  const before = tilesOf(f.favzone);
  fire(f.rows[0], 'dragstart', {dataTransfer: makeDataTransfer()});
  fire(f.favzone, 'dragover', {clientX: TILE_CENTRES[0] - 2, clientY: 20});
  assert.equal(ghostAt(f.favzone), 0);

  fire(f.root, 'dragend');

  assert.equal(ghostAt(f.favzone), -1);
  assert.deepEqual(tilesOf(f.favzone), before);
});
