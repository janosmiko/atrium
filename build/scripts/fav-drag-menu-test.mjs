#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The favourite-tile drag and the row context menu, as
// INSTALLED on a root rather than as maths.
//
//   node --test ./build/scripts/fav-drag-menu-test.mjs
//
// The maths under both is covered elsewhere (grid-drop-test, reflow-test,
// row-drag-test). What is not covered is the wiring, which is where the
// interesting failures live: a drag that previews a reorder and then does not
// happen has to leave the grid exactly as it found it, and a menu that builds
// itself with innerHTML is a hard runtime throw under Trusted Types rather
// than a lint finding.
//
// TWO tests are marked todo. They name behaviour the modules do not have yet
// and fail today - a cancelled favourite drag keeps its optimistic reflow, and
// a drag treats any pane's grid as its own. The run stays green (fail 0);
// those two are findings, not regressions.
//
// The shim has no layout engine, so every rectangle these tests resolve
// against is declared with setRect. Structure and routing here; the real drag
// semantics stay the VM probe's job (make fav-drag-probe, make menu-probe).

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event, makeDataTransfer} from './dom-shim.mjs';

const h = installDOM();

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {installFavoriteDrag} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/sidebar-fav-drag.js'));
const {installRowMenu, closeMenu, presetWakeAtSwiftSeconds} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/sidebar-menu.js'));

const elementPrototype = Object.getPrototypeOf(h.document.createElement('div'));

// Trusted Types runs with an EMPTY allowlist, so an innerHTML write in the
// surface throws in the browser and takes the whole sidebar with it. The shim
// has no innerHTML at all, which would let one through unnoticed - this makes
// reaching for it fail here the way it fails there.
for (const blocked of ['innerHTML', 'outerHTML']) {
  Object.defineProperty(elementPrototype, blocked, {
    set() {
      throw new TypeError(`${blocked} is blocked by Trusted Types`);
    },
  });
}

// The shim has no focus model at all, so record the calls instead. Focusing an
// item is what puts the keyboard inside the menu: a keydown only reaches the
// root's listener from something focused underneath it.
const focused = [];
elementPrototype.focus = function() {
  focused.push(this);
};

const fire = (node, type, init = {}) => {
  const event = new Event(type, {bubbles: true, ...init});
  node.dispatchEvent(event);
  return event;
};

// --- favourite drag ---------------------------------------------------------

const TILE = 40;
const COLUMNS = 3;

// One pane: a favourites grid of 40px tiles laid out three across, plus a tab
// list to drag out into. Tile i sits at column i % 3, row i / 3.
function buildPane(tileCount, prefix = 'f') {
  const pane = h.document.createElement('div');
  pane.className = 'railpane';

  const grid = h.document.createElement('div');
  grid.className = 'favgrid';
  grid.dataset.dropzone = 'favorites';
  for (let i = 0; i < tileCount; i++) {
    const tile = h.document.createElement('div');
    tile.className = 'fav';
    tile.dataset.favId = prefix + i;
    tile.setRect({
      left: (i % COLUMNS) * TILE,
      top: Math.floor(i / COLUMNS) * TILE,
      width: TILE,
      height: TILE,
    });
    grid.appendChild(tile);
  }

  const rows = h.document.createElement('div');
  rows.className = 'rows';
  rows.dataset.dropzone = 'tabs';
  const row = h.document.createElement('div');
  row.className = 'row tabrow';
  row.dataset.tabId = 't0';
  rows.appendChild(row);

  pane.append(grid, rows);
  return {pane, grid, rows, row};
}

function dragFixture(tileCount = 4) {
  h.reset();
  const root = h.document.createElement('div');
  root.className = 'sbroot';
  const {pane, grid, rows, row} = buildPane(tileCount);
  root.appendChild(pane);
  h.body.appendChild(root);

  const reorders = [];
  installFavoriteDrag(root, {onReorder: (from, to) => reorders.push([from, to])});
  return {root, grid, rows, row, reorders, tiles: () => [...grid.querySelectorAll('.fav')]};
}

const idsOf = (grid) =>
    [...grid.querySelectorAll('.fav')].map((tile) => tile.dataset.favId);

// The shim has no layout engine, so a tile keeps the rectangle it was declared
// with however the grid is reordered around it - the reflow then measures every
// tile as having moved nowhere and its FLIP branch never runs at all. This
// gives one grid a layout: a tile's rectangle follows the slot it currently
// occupies, the way a real grid lays itself out again on every append. Only the
// tests that care what the reflow leaves on the tiles need it.
function layoutGrid(grid) {
  for (const tile of grid.querySelectorAll('.fav')) {
    tile.getBoundingClientRect = function() {
      const slot = [...grid.querySelectorAll('.fav')].indexOf(this);
      const left = (slot % COLUMNS) * TILE;
      const top = Math.floor(slot / COLUMNS) * TILE;
      return {left, top, right: left + TILE, bottom: top + TILE,
              x: left, y: top, width: TILE, height: TILE};
    };
  }
}

const dragstart = (tile, dataTransfer = makeDataTransfer()) =>
    fire(tile, 'dragstart', {dataTransfer});

test('dragstart marks the tile the drag started on', () => {
  const {grid, tiles} = dragFixture();
  dragstart(tiles()[1]);
  assert.deepEqual(
      [...grid.querySelectorAll('.favdragging')].map((t) => t.dataset.favId),
      ['f1']);
});

test('dragstart puts the source index in the drag payload', () => {
  // Not decoration: a dragstart that sets no data at all is refused outright
  // on some platforms and the drag never starts.
  const {tiles} = dragFixture();
  const payload = makeDataTransfer();
  dragstart(tiles()[2], payload);
  assert.equal(payload.getData('text/plain'), '2');
  assert.equal(payload.effectAllowed, 'move');
});

test('a dragstart on a tab row is not a tile drag', () => {
  const {grid, row, reorders} = dragFixture();
  dragstart(row);
  assert.equal(grid.querySelector('.favdragging'), null);
  // And nothing is armed, so the tab list's own drag keeps the drop.
  const over = fire(grid, 'dragover', {clientX: 100, clientY: 20});
  assert.equal(over.defaultPrevented, false);
  assert.deepEqual(idsOf(grid), ['f0', 'f1', 'f2', 'f3']);
});

test('a tile outside a favourites dropzone is inert', () => {
  // A tile rendered somewhere without the dropzone attribute has no grid to
  // reorder, so arming the drag would reflow whatever it found next.
  h.reset();
  const root = h.document.createElement('div');
  const loose = h.document.createElement('div');
  loose.className = 'fav';
  loose.setRect({left: 0, top: 0, width: TILE, height: TILE});
  root.appendChild(loose);
  h.body.appendChild(root);
  installFavoriteDrag(root, {onReorder: () => assert.fail('no grid to reorder')});

  dragstart(loose);
  assert.equal(loose.classList.contains('favdragging'), false);
  assert.equal(fire(loose, 'drop', {clientX: 0, clientY: 0}).defaultPrevented, false);
});

test('dragover previews the reorder in the live grid', () => {
  const {grid, tiles} = dragFixture();
  dragstart(tiles()[0]);
  // Past the centre of the third tile: slot 3, so f0 lands between f2 and f3.
  fire(grid, 'dragover', {clientX: 100, clientY: 20});
  assert.deepEqual(idsOf(grid), ['f1', 'f2', 'f0', 'f3']);
});

test('dragover accepts the drop', () => {
  // A dragover that does not preventDefault produces no drop event at all,
  // which is a reorder that silently never commits.
  const {grid, tiles} = dragFixture();
  dragstart(tiles()[0]);
  const payload = makeDataTransfer();
  const over = fire(grid, 'dragover',
      {clientX: 100, clientY: 20, dataTransfer: payload});
  assert.equal(over.defaultPrevented, true);
  assert.equal(payload.dropEffect, 'move');
});

test('a dragover out in the tab list previews nothing', () => {
  // A favourite dragged onto the rows has nowhere to go, and a preview there
  // would promise a move that cannot happen.
  const {grid, rows, tiles} = dragFixture();
  dragstart(tiles()[0]);
  const over = fire(rows, 'dragover', {clientX: 100, clientY: 20});
  assert.equal(over.defaultPrevented, false);
  assert.deepEqual(idsOf(grid), ['f0', 'f1', 'f2', 'f3']);
});

test('every preview is measured from the order the drag started in', () => {
  // Applying each move to the LAST preview instead of the starting order
  // compounds them and the tiles wander off somewhere neither the user nor
  // the drop ever asked for.
  const {grid, tiles} = dragFixture();
  dragstart(tiles()[2]);
  fire(grid, 'dragover', {clientX: 5, clientY: 20});
  assert.deepEqual(idsOf(grid), ['f2', 'f0', 'f1', 'f3'], 'to the front');
  fire(grid, 'dragover', {clientX: 35, clientY: 60});
  assert.deepEqual(idsOf(grid), ['f0', 'f1', 'f3', 'f2'], 'to the end');
});

test('drop reports the destination adjusted for the tile leaving', () => {
  // Slot 3 counts the gaps in the grid as it stands; index 2 is where the
  // tile ends up once it has vacated slot 0. SpacesStore.moveFavorite expects
  // the adjusted one, so an unadjusted slot lands every forward drag one tile
  // short.
  const {grid, tiles, reorders} = dragFixture();
  dragstart(tiles()[0]);
  fire(grid, 'dragover', {clientX: 100, clientY: 20});
  fire(grid, 'drop', {clientX: 100, clientY: 20});
  assert.deepEqual(reorders, [[0, 2]]);
});

test('the reorder is reported as numbers', () => {
  // sidebarFavoriteReordered is dropped on the floor unless both arguments
  // are ints - a string index is not rejected, it is simply ignored.
  const {grid, tiles, reorders} = dragFixture();
  dragstart(tiles()[3]);
  fire(grid, 'dragover', {clientX: 5, clientY: 20});
  fire(grid, 'drop', {clientX: 5, clientY: 20});
  assert.deepEqual(reorders, [[3, 0]]);
  assert.equal(reorders[0].every(Number.isInteger), true);
});

test('dropping a tile back where it started reports nothing', () => {
  const {grid, tiles, reorders} = dragFixture();
  dragstart(tiles()[1]);
  fire(grid, 'dragover', {clientX: 45, clientY: 20});
  fire(grid, 'drop', {clientX: 45, clientY: 20});
  assert.deepEqual(reorders, []);
  assert.deepEqual(idsOf(grid), ['f0', 'f1', 'f2', 'f3']);
});

test('a drop with no preview behind it still commits', () => {
  // Releasing without ever crossing a tile boundary is a real drag; the drop
  // resolves the slot itself rather than relying on a preview having run.
  const {grid, tiles, reorders} = dragFixture();
  dragstart(tiles()[0]);
  fire(grid, 'drop', {clientX: 100, clientY: 20});
  assert.deepEqual(reorders, [[0, 2]]);
});

test('the drag is released on drop', () => {
  const {grid, tiles, reorders} = dragFixture();
  dragstart(tiles()[0]);
  fire(grid, 'drop', {clientX: 100, clientY: 20});
  assert.equal(grid.querySelector('.favdragging'), null);

  // A dragover after the drop belongs to no drag: previewing there would
  // reorder the grid with the geometry of a drag that has already finished.
  const after = idsOf(grid);
  const over = fire(grid, 'dragover', {clientX: 5, clientY: 20});
  assert.equal(over.defaultPrevented, false);
  assert.deepEqual(idsOf(grid), after);
  assert.deepEqual(reorders, [[0, 2]]);
});

test('dragend releases the drag', () => {
  const {grid, tiles, reorders} = dragFixture();
  dragstart(tiles()[0]);
  fire(grid, 'dragover', {clientX: 100, clientY: 20});
  fire(grid, 'dragend');
  assert.equal(grid.querySelector('.favdragging'), null);
  assert.equal(fire(grid, 'dragover', {clientX: 5, clientY: 20}).defaultPrevented,
      false, 'the drag is over, not merely unmarked');
  assert.deepEqual(reorders, [], 'a cancelled drag commits nothing');
});

test('the drag leaves no animation state on the tiles', () => {
  // The reflow drives the tiles with an inline transform and an inline
  // transition, and the transition is the half that outlives the drag: a tile
  // still carrying `transform 180ms` slides to its new place the next time
  // anything re-renders the rail, so an unrelated snapshot arrives as an
  // animation nobody asked for.
  const {grid, tiles} = dragFixture();
  layoutGrid(grid);
  dragstart(tiles()[0]);
  fire(grid, 'dragover', {clientX: 100, clientY: 20});
  assert.equal(tiles().filter((t) => t.style.transition).length, 3,
      'three tiles changed slot, so three were animated');

  fire(grid, 'dragend');
  assert.deepEqual(
      tiles().map((t) => [t.style.transform ?? '', t.style.transition ?? '']),
      [['', ''], ['', ''], ['', ''], ['', '']]);
});

test('a tile drag stays inside the surface', () => {
  // Not for the surface's own sake - installRowDrag listens on this same root
  // and claims .tabrow rather than .fav, and sidebar.js takes its copy in the
  // capture phase, where a stopPropagation here cannot reach either of them.
  // It is the page underneath: a dragstart or a drop that escapes the root
  // arrives at whatever the embedder has wired and starts a second gesture on
  // the same press.
  const {grid, tiles} = dragFixture();
  const escaped = [];
  for (const type of ['dragstart', 'dragover', 'drop']) {
    h.body.addEventListener(type, (e) => escaped.push(e.type));
  }
  dragstart(tiles()[0]);
  fire(grid, 'dragover', {clientX: 100, clientY: 20});
  fire(grid, 'drop', {clientX: 100, clientY: 20});
  assert.deepEqual(escaped, []);
});

test('a dragstart with no drag payload still starts the drag', () => {
  // dataTransfer is optional-chained throughout. A drag arriving without one
  // must still arm, not throw partway through and leave the tile marked with
  // no listener able to unmark it.
  const {grid, tiles, reorders} = dragFixture();
  fire(tiles()[0], 'dragstart');
  fire(grid, 'dragover', {clientX: 100, clientY: 20});
  fire(grid, 'drop', {clientX: 100, clientY: 20});
  assert.deepEqual(reorders, [[0, 2]]);
});

test('a grid with one tile has nothing to reorder', () => {
  const {grid, tiles, reorders} = dragFixture(1);
  dragstart(tiles()[0]);
  fire(grid, 'dragover', {clientX: 100, clientY: 20});
  fire(grid, 'drop', {clientX: 100, clientY: 20});
  assert.deepEqual(reorders, []);
  assert.deepEqual(idsOf(grid), ['f0']);
});

// The optimistic reflow physically moves the tiles. Nothing puts them back:
// finish() clears the transforms and the marker but never re-applies
// baseOrder, and the rail is only rebuilt on dragend if a push was DEFERRED
// during the drag. Cancel a drag with nothing else going on and the grid keeps
// showing a reorder that never happened, until some unrelated snapshot lands.
test('a cancelled drag puts the tiles back where they were', () => {
  const {grid, tiles} = dragFixture();
  dragstart(tiles()[0]);
  fire(grid, 'dragover', {clientX: 100, clientY: 20});
  fire(grid, 'dragend');
  assert.deepEqual(idsOf(grid), ['f0', 'f1', 'f2', 'f3']);
});

// Every pane renders its OWN space's tiles, so the surface holds
// one favourites grid per space. The dragover only asks whether the pointer is
// over SOME favourites zone, never whether it is over the one the drag started
// in - and edge paging moves another pane under the pointer mid-drag by design
//.
test('a drag ignores another space\'s grid', () => {
  h.reset();
  const root = h.document.createElement('div');
  const here = buildPane(4, 'a');
  const there = buildPane(4, 'b');
  root.append(here.pane, there.pane);
  h.body.appendChild(root);
  const reorders = [];
  installFavoriteDrag(root, {onReorder: (from, to) => reorders.push([from, to])});

  dragstart([...here.grid.querySelectorAll('.fav')][0]);
  fire(there.grid, 'dragover', {clientX: 100, clientY: 20});
  assert.deepEqual(idsOf(here.grid), ['a0', 'a1', 'a2', 'a3'],
      'the source grid must not reflow for a pointer that has left it');
  fire(there.grid, 'drop', {clientX: 100, clientY: 20});
  assert.deepEqual(reorders, [], 'and the drop must not commit into it');
});

// --- row context menu -------------------------------------------------------

function menuFixture(opts = {}) {
  const {tabs = 2, pinned = 1, grouped = 0} = opts;
  h.reset();
  focused.length = 0;
  const root = h.document.createElement('div');
  root.className = 'sbroot';
  root.setRect({left: 0, top: 0, width: 200, height: 300});

  const zone = (dropzone, ids) => {
    const wrap = h.document.createElement('div');
    wrap.className = 'rows';
    wrap.dataset.dropzone = dropzone;
    for (const id of ids) {
      const row = h.document.createElement('div');
      row.className = 'row tabrow';
      row.dataset.tabId = id;
      const title = h.document.createElement('span');
      title.className = 'tl';
      title.textContent = 'Tab ' + id;
      row.appendChild(title);
      wrap.appendChild(row);
    }
    root.appendChild(wrap);
    return wrap;
  };

  const pinnedZone = zone('pinned', [...Array(pinned).keys()].map((i) => 'p' + i));
  if (opts.pinnedFolder) {
    // A folder ahead of the pins in the DOM, the shape a pinned folder head
    // renders as - one item, not a row the menu's index should ever count.
    const folderHead = h.document.createElement('div');
    folderHead.className = 'row frow';
    folderHead.dataset.folderId = 'PF';
    pinnedZone.insertBefore(folderHead, pinnedZone.firstChild);
  }
  const groupedZone = zone('folder:G1', [...Array(grouped).keys()].map((i) => 'g' + i));
  const tabsZone = zone('tabs', [...Array(tabs).keys()].map((i) => 't' + i));

  h.body.appendChild(root);
  const sent = [];
  installRowMenu(root, {
    send: (name, args) => sent.push([name, args]),
    ...(opts.spaces ? {spaces: () => opts.spaces} : {}),
    ...(opts.incognito ? {incognito: () => true} : {}),
  });
  return {root, sent, tabsZone, pinnedZone, groupedZone};
}

// the lab opens every row menu with Copy Link (lab 4443), and
// the favourite menu has it too - a row menu without it is an
// inconsistency inside one surface.
test('a row menu opens with Copy Link when the row has a url', () => {
  const {root, tabsZone} = menuFixture();
  const row = tabsZone.children[0];
  row.dataset.url = 'https://copy.example/page';
  h.clipboard.length = 0;

  rightClick(row);
  assert.equal(labels(root)[0], 'Copy Link');
  [...root.querySelectorAll('.ctxitem')]
      .find((i) => i.textContent === 'Copy Link').dispatchEvent(new Event('click', {bubbles: true}));
  assert.deepEqual(h.clipboard, ['https://copy.example/page']);
});

test('a row with no url gets no Copy Link rather than a dead one', () => {
  // A cold row carries a url and a live one may not; an entry that silently
  // copies an empty string is worse than an absent entry.
  const {root, tabsZone} = menuFixture();
  rightClick(tabsZone.children[0]);
  assert.equal(labels(root).includes('Copy Link'), false);
});

const rightClick = (row, at = {}) =>
    fire(row, 'contextmenu', {clientX: 10, clientY: 10, ...at});

const byLabel = (root, label) =>
    [...root.querySelectorAll('.ctxitem')].find((el) => el.textContent === label);

const labels = (root) =>
    [...root.querySelectorAll('.ctxitem')].map((item) => item.textContent);

// The menu measures itself the instant it is built, and there is no layout
// engine to give it a size, so declare one as it is created.
function withMenuSize(width, height, body) {
  const real = h.document.createElement;
  h.document.createElement = (tag) =>
      real(tag).setRect({left: 0, top: 0, width, height});
  try {
    return body();
  } finally {
    h.document.createElement = real;
  }
}

test('a right-click on a row opens the menu and suppresses the page menu', () => {
  // Chromium's own menu describes the sidebar's WebContents - Back, Forward,
  // Reload, all greyed out - and says nothing about the tab under the cursor.
  const {root, tabsZone} = menuFixture();
  const event = rightClick(tabsZone.children[0]);
  assert.equal(event.defaultPrevented, true);
  assert.notEqual(root.querySelector('.ctxmenu'), null);
});

test('an unpinned row is offered Pin and Snooze', () => {
  const {root, tabsZone} = menuFixture();
  rightClick(tabsZone.children[1]);
  assert.deepEqual(
      // Rename leads, as it does in the lab. These rows
      // carry no url, so Copy Link is absent.
      // The last row, so no Archive Tabs Below - there is nothing below it.
      labels(root), ['Rename', 'Pin Tab', 'Snooze', 'Duplicate', 'Put on Shelf',
                     'Archive Tab', 'Close Tab']);
});

test('a pinned row is offered Unpin and Add to Dock', () => {
  const {root, pinnedZone} = menuFixture();
  rightClick(pinnedZone.children[0]);
  assert.deepEqual(
      labels(root),
      ['Rename', 'Unpin Tab', 'Add to Dock', 'Duplicate', 'Put on Shelf',
       'Close Tab']);
});

// Pin, Snooze, Put on Shelf, Archive, Move to and Rename all file the row
// somewhere that outlives the window. Copy, Duplicate and Close stay inside it.
// A folder ahead of a loose pin is one item in the core's pinned list - an
// index counted off `.tabrow` alone lands every one of these on the folder.
test('Unpin Tab on a pinned row after a folder head names its item index', () => {
  const {root, pinnedZone, sent} = menuFixture({pinnedFolder: true});
  rightClick(pinnedZone.children[1]);
  byLabel(root, 'Unpin Tab').click();
  assert.deepEqual(sent.at(-1), ['sidebarTabUnpinned', [1, 2]]);
});

test('Add to Dock on a pinned row after a folder head names its item index', () => {
  const {root, pinnedZone, sent} = menuFixture({pinnedFolder: true});
  rightClick(pinnedZone.children[1]);
  byLabel(root, 'Add to Dock').click();
  assert.deepEqual(sent.at(-1), ['sidebarTabAddedToDock', [1]]);
});

test('Rename on a pinned row after a folder head sends its item index', () => {
  const {root, pinnedZone, sent} = menuFixture({pinnedFolder: true});
  const row = pinnedZone.children[1];
  rightClick(row);
  byLabel(root, 'Rename').click();
  const input = row.querySelector('input');
  input.value = 'New Name';
  input.dispatchEvent(new Event('keydown', {bubbles: true, key: 'Enter'}));
  assert.deepEqual(sent.at(-1), ['sidebarTabRenamed', [1, 1, 'New Name']]);
});

test('Move to... on a pinned row after a folder head names its item index', () => {
  const {root, pinnedZone, sent} =
      menuFixture({pinnedFolder: true, spaces: [{name: 'A'}, {name: 'B'}]});
  rightClick(pinnedZone.children[1]);
  byLabel(root, 'Move to...').click();
  byLabel(root, 'B').click();
  assert.deepEqual(sent.at(-1), ['sidebarTabMovedToSpace', [0, 1, 'pinned', 1, -1]]);
});

test('an incognito row is offered nothing that files it anywhere', () => {
  const {root, tabsZone} = menuFixture({incognito: true, spaces: [{}, {}]});
  const row = tabsZone.children[0];
  row.dataset.url = 'https://private.example/page';
  rightClick(row);
  assert.deepEqual(labels(root), ['Copy Link', 'Duplicate', 'Close Tab']);
});

test('an incognito pinned row gets the same three, not the pinned menu', () => {
  // Section is read off the dropzone, so the pinned arm is a separate list
  // with its own Unpin and Add to Dock.
  const {root, pinnedZone} = menuFixture({incognito: true});
  rightClick(pinnedZone.children[0]);
  assert.deepEqual(labels(root), ['Duplicate', 'Close Tab']);
});

test('a grouped row keeps the page menu', () => {
  // None of these messages address a row inside a folder, so a menu here
  // would be three no-ops where the browser's own menu used to be.
  const {root, groupedZone} = menuFixture({grouped: 2});
  const event = rightClick(groupedZone.children[0]);
  assert.equal(event.defaultPrevented, false);
  assert.equal(root.querySelector('.ctxmenu'), null);
});

test('a right-click on the surface itself opens nothing', () => {
  const {root} = menuFixture();
  const event = rightClick(root);
  assert.equal(event.defaultPrevented, false);
  assert.equal(root.querySelector('.ctxmenu'), null);
});

test('the menu is built out of elements, never markup', () => {
  // innerHTML throws under Trusted Types with an empty allowlist, so this is
  // a crash in the surface rather than a style question. Each label has to be
  // a text node under a real button.
  const {root, tabsZone} = menuFixture();
  rightClick(tabsZone.children[0]);
  const item = root.querySelector('.ctxitem');
  assert.equal(item.tagName, 'BUTTON');
  assert.equal(item.type, 'button',
      'set explicitly, because a <button> with no type is a submit button');
  assert.equal(item.childNodes.length, 1);
  assert.equal(item.childNodes[0].nodeType, 3, 'the label is a text node');
});

test('the separator is not a menu item', () => {
  // A labelless entry is a rule. Building it as a button would put a blank
  // stop between Pin and Close for anyone arrowing through the menu.
  const {root, tabsZone} = menuFixture();
  rightClick(tabsZone.children[0]);
  assert.deepEqual(
      [...root.querySelector('.ctxmenu').children].map((el) => el.className),
      // Rename leads the list. Every button also wears
      // the shared overlay row class, unlike a separator.
      ['ctxitem ov-row', 'ctxitem ov-row', 'ctxitem ov-row', 'ctxrule',
       'ctxitem ov-row', 'ctxrule', 'ctxitem ov-row', 'ctxitem ov-row',
       'ctxitem ov-row', 'ctxrule', 'ctxitem ov-row']);
});

test('choosing Pin sends the row index and the top pinned slot', () => {
  // Slot 0, matching where a drag to the top of the pinned section lands.
  const {root, tabsZone, sent} = menuFixture();
  rightClick(tabsZone.children[1]);
  // By label, not position: Rename now leads the list, and an index-based pick
  // would silently choose a different entry the next time one is added.
  byLabel(root, 'Pin Tab').click();
  assert.deepEqual(sent, [['sidebarTabPinned', [1, 0]]]);
});

test('choosing Snooze opens the duration/alert menu rather than sending immediately', () => {
  const {root, tabsZone, sent} = menuFixture();
  rightClick(tabsZone.children[1]);
  byLabel(root, 'Snooze').click();
  assert.deepEqual(sent, [], 'nothing is sent until a duration is chosen');
  assert.notEqual(root.querySelector('.snoozemenu'), null);
});

test('picking a duration from the ctx-menu Snooze entry sends the row index, a wake time and the alert flag', () => {
  const {root, tabsZone, sent} = menuFixture();
  rightClick(tabsZone.children[1]);
  byLabel(root, 'Snooze').click();
  const durationButtons = root.querySelectorAll('.snoozemenu button.ctxitem');
  durationButtons[0].click();
  assert.equal(sent.length, 1);
  const [name, args] = sent[0];
  assert.equal(name, 'sidebarTabSnoozed');
  assert.equal(args[0], 1);
  assert.equal(typeof args[1], 'number');
  assert.equal(args[2], false, 'the alert checkbox defaults to off');
});

test('a pinned row has no Snooze', () => {
  const {root, pinnedZone} = menuFixture();
  rightClick(pinnedZone.children[0]);
  assert.equal(labels(root).includes('Snooze'), false);
});

test('choosing Unpin appends after the last regular row', () => {
  // The row's place among the pinned tabs says nothing about where it belongs
  // once it is an ordinary tab, so it goes on the end.
  const {root, pinnedZone, sent} = menuFixture({tabs: 3});
  rightClick(pinnedZone.children[0]);
  byLabel(root, 'Unpin Tab').click();
  assert.deepEqual(sent, [['sidebarTabUnpinned', [0, 3]]]);
});

test('pin and unpin travel as numbers', () => {
  // Both handlers test args[0].is_int() and args[1].is_int() and return
  // without a word if either fails, so a string index is a message that
  // vanishes rather than one that errors.
  const {root, tabsZone, sent} = menuFixture();
  rightClick(tabsZone.children[0]);
  byLabel(root, 'Pin Tab').click();
  assert.equal(sent[0][1].every(Number.isInteger), true);
});

test('choosing Close sends the row\'s tab id', () => {
  const {root, tabsZone, sent} = menuFixture();
  rightClick(tabsZone.children[1]);
  byLabel(root, 'Close Tab').click();
  assert.deepEqual(sent, [['sidebarTabClosed', ['t1']]]);
  assert.equal(typeof sent[0][1][0], 'string',
      'the handler ignores anything that is not a string');
});

test('choosing Close on a pinned row sends the same message', () => {
  // The pinned branch builds its own Close item rather than sharing the
  // unpinned one, so it is a second call site with its own message name and
  // its own argument to get wrong.
  const {root, pinnedZone, sent} = menuFixture();
  rightClick(pinnedZone.children[0]);
  byLabel(root, 'Close Tab').click();
  assert.deepEqual(sent, [['sidebarTabClosed', ['p0']]]);
});

test('choosing Put on Shelf sends the row\'s tab id and leaves the tab alone', () => {
  // the message name says nothing about closing anything,
  // and there is no second message this test would have to see ABSENT.
  const {root, tabsZone, sent} = menuFixture();
  rightClick(tabsZone.children[1]);
  byLabel(root, 'Put on Shelf').click();
  assert.deepEqual(sent, [['sidebarTabShelved', ['t1']]]);
});

test('choosing Put on Shelf on a pinned row sends the same message', () => {
  const {root, pinnedZone, sent} = menuFixture();
  rightClick(pinnedZone.children[0]);
  byLabel(root, 'Put on Shelf').click();
  assert.deepEqual(sent, [['sidebarTabShelved', ['p0']]]);
});

test('choosing Add to Dock sends the pinned row index', () => {
  const {root, pinnedZone, sent} = menuFixture();
  rightClick(pinnedZone.children[0]);
  byLabel(root, 'Add to Dock').click();
  assert.deepEqual(sent, [['sidebarTabAddedToDock', [0]]]);
});

test('an unpinned row has no Add to Dock', () => {
  // the action only exists on a pinned row.
  const {root, tabsZone} = menuFixture();
  rightClick(tabsZone.children[0]);
  assert.equal(labels(root).includes('Add to Dock'), false);
});

test('Close Tab on a cold row removes it by its rendered index', () => {
  // A cold row carries no id, so sidebarTabClosed could never name
  // it and the entry was inert. It routes the way the x goes.
  const {root, tabsZone, sent} = menuFixture();
  const row = tabsZone.children[1];
  row.removeAttribute('data-tab-id');
  rightClick(row);
  byLabel(root, 'Close Tab').click();
  assert.deepEqual(sent, [['sidebarColdTabRemoved', [1]]]);
});

test('Close Tab on a row live elsewhere is not a cold remove', () => {
  const {root, tabsZone, sent} = menuFixture();
  const row = tabsZone.children[1];
  row.removeAttribute('data-tab-id');
  row.dataset.elsewhereTabId = 'w2-7';
  rightClick(row);
  byLabel(root, 'Close Tab').click();
  assert.deepEqual(sent.filter(([n]) => n === 'sidebarColdTabRemoved'), []);
});

test('choosing an item closes the menu', () => {
  const {root, tabsZone} = menuFixture();
  rightClick(tabsZone.children[0]);
  root.querySelector('.ctxitem').click();
  assert.equal(root.querySelector('.ctxmenu'), null);
});

test('a press outside closes the menu', () => {
  const {root, tabsZone} = menuFixture();
  rightClick(tabsZone.children[0]);
  fire(tabsZone.children[1], 'pointerdown');
  assert.equal(root.querySelector('.ctxmenu'), null);
});

test('a press on the menu itself leaves it open', () => {
  // The dismiss listener is on the root in the CAPTURE phase, so it sees the
  // press that is about to choose an item before the item does. Closing on
  // that one would make the menu unusable by mouse.
  const {root, tabsZone, sent} = menuFixture();
  rightClick(tabsZone.children[0]);
  // An item that SENDS, by label - the first entry is Rename,
  // and it opens an inline field rather than sending, so a
  // position-based pick would make this assertion unfalsifiable.
  const item = byLabel(root, 'Pin Tab');
  fire(item, 'pointerdown');
  assert.notEqual(root.querySelector('.ctxmenu'), null);
  item.click();
  assert.equal(sent.length, 1, 'the press did not eat the click behind it');
});

test('the dismiss lands before the surface\'s own press handlers', () => {
  // The capture phase is the point, not an implementation detail: everything
  // else that takes pointerdown under this root - a row arming a drag, the
  // swipe rail - runs on the way back up. Dismissing after them leaves a menu
  // addressing a row that a gesture has already started moving.
  const {root, tabsZone} = menuFixture();
  const row = tabsZone.children[1];
  const openWhenPressed = [];
  row.addEventListener('pointerdown', () => {
    openWhenPressed.push(root.querySelector('.ctxmenu') !== null);
  });

  rightClick(tabsZone.children[0]);
  fire(row, 'pointerdown');
  assert.deepEqual(openWhenPressed, [false], 'the menu was already gone');
});

test('the first item takes focus', () => {
  // Escape is handled on the root, and a keydown only gets there from a
  // focused descendant - so without this the menu cannot be dismissed by
  // keyboard, and cannot be opened by one at all.
  const {root, tabsZone} = menuFixture();
  rightClick(tabsZone.children[0]);
  assert.equal(focused.length, 1);
  assert.equal(focused[0], root.querySelector('.ctxitem'));
});

test('Escape closes the menu', () => {
  const {root, tabsZone} = menuFixture();
  rightClick(tabsZone.children[0]);
  fire(root, 'keydown', {key: 'a'});
  assert.notEqual(root.querySelector('.ctxmenu'), null, 'any other key is not a dismiss');
  fire(root, 'keydown', {key: 'Escape'});
  assert.equal(root.querySelector('.ctxmenu'), null);
});

test('a scroll anywhere closes the menu', () => {
  // Captured, because a scrolling tab list does not bubble one.
  const {root, tabsZone} = menuFixture();
  rightClick(tabsZone.children[0]);
  fire(tabsZone, 'scroll', {bubbles: false});
  assert.equal(root.querySelector('.ctxmenu'), null);
});

test('a second right-click replaces the first menu', () => {
  const {root, tabsZone, pinnedZone} = menuFixture();
  rightClick(tabsZone.children[0]);
  rightClick(pinnedZone.children[0]);
  assert.equal(root.querySelectorAll('.ctxmenu').length, 1);
  assert.deepEqual(
      labels(root),
      ['Rename', 'Unpin Tab', 'Add to Dock', 'Duplicate', 'Put on Shelf',
       'Close Tab']);
});

test('closeMenu is safe when nothing is open', () => {
  const {root} = menuFixture();
  closeMenu(root);
  closeMenu(root);
  assert.equal(root.querySelector('.ctxmenu'), null);
});

test('the menu opens at the pointer, in the surface\'s own coordinates', () => {
  const {root, tabsZone} = menuFixture();
  root.setRect({left: 40, top: 10, width: 200, height: 300});
  withMenuSize(120, 80, () => rightClick(tabsZone.children[0], {clientX: 100, clientY: 60}));
  const menu = root.querySelector('.ctxmenu');
  assert.equal(menu.style.left, '60px');
  assert.equal(menu.style.top, '50px');
});

test('the menu is clamped inside the surface', () => {
  // The sidebar is narrow and short and there is no window to overflow into,
  // so a menu opened near an edge would simply be clipped.
  const {root, tabsZone} = menuFixture();
  withMenuSize(120, 80, () => rightClick(tabsZone.children[0], {clientX: 150, clientY: 260}));
  const menu = root.querySelector('.ctxmenu');
  assert.equal(menu.style.left, '78px', '200 wide, 120 of menu, 2 of margin');
  assert.equal(menu.style.top, '218px');
});

test('the menu is kept off the top-left edge too', () => {
  const {root, tabsZone} = menuFixture();
  withMenuSize(120, 80, () => rightClick(tabsZone.children[0], {clientX: 0, clientY: 0}));
  const menu = root.querySelector('.ctxmenu');
  assert.equal(menu.style.left, '2px');
  assert.equal(menu.style.top, '2px');
});

// --- folder context menu ------------------------------------

function folderMenuFixture(id = 'G1', folderName = 'Reading') {
  h.reset();
  focused.length = 0;
  const root = h.document.createElement('div');
  root.className = 'sbroot';
  root.setRect({left: 0, top: 0, width: 200, height: 300});

  const head = h.document.createElement('div');
  head.className = 'frow';
  head.dataset.folderId = id;
  const label = h.document.createElement('span');
  label.className = 'tl';
  label.textContent = folderName;
  const icon = h.document.createElement('span');
  icon.className = 'fic';
  head.append(icon, label);
  root.appendChild(head);

  h.body.appendChild(root);
  const sent = [];
  installRowMenu(root, {send: (name, args) => sent.push([name, args])});
  return {root, sent, head, label};
}

const clickItem = (root, label) =>
    [...root.querySelectorAll('.ctxitem')].find((el) => el.textContent === label).click();

test('a right-click on a folder header opens Rename, Change Icon and Delete', () => {
  const {root, head} = folderMenuFixture();
  rightClick(head);
  assert.deepEqual(labels(root), ['Rename Folder', 'Change Icon', 'Delete Folder']);
});

test('a right-click on the pinned section keeps the page menu', () => {
  // The pinned fold shares the .frow markup but is not a group - it has no id
  // these messages could act on.
  const {root, head} = folderMenuFixture();
  head.classList.add('sechdrow');
  rightClick(head);
  assert.equal(root.querySelector('.ctxmenu'), null);
});

test('choosing Delete Folder sends the folder id', () => {
  const {root, sent, head} = folderMenuFixture('G1');
  rightClick(head);
  clickItem(root, 'Delete Folder');
  assert.deepEqual(sent, [['sidebarFolderDeleted', ['G1']]]);
});

test('choosing Change Icon sends the folder id and the first preset glyph', () => {
  const {root, sent, head} = folderMenuFixture('G1');
  rightClick(head);
  clickItem(root, 'Change Icon');
  assert.equal(sent.length, 1);
  assert.equal(sent[0][0], 'sidebarFolderIconSet');
  assert.equal(sent[0][1][0], 'G1');
  assert.equal(typeof sent[0][1][1], 'string');
});

test('choosing Rename Folder makes the label editable and focuses it', () => {
  const {root, head, label} = folderMenuFixture();
  rightClick(head);
  clickItem(root, 'Rename Folder');
  assert.equal(label.contentEditable, 'true');
  assert.ok(focused.includes(label));
});

test('committing a rename by blur sends the new name', () => {
  const {root, sent, head, label} = folderMenuFixture('G1', 'Reading');
  rightClick(head);
  clickItem(root, 'Rename Folder');
  label.textContent = 'Research';
  fire(label, 'blur');
  assert.deepEqual(sent, [['sidebarFolderRenamed', ['G1', 'Research']]]);
  assert.equal(label.contentEditable, 'false');
});

test('a rename left unchanged sends nothing', () => {
  const {sent, root, head, label} = folderMenuFixture('G1', 'Reading');
  rightClick(head);
  clickItem(root, 'Rename Folder');
  fire(label, 'blur');
  assert.deepEqual(sent, []);
});

test('a rename emptied out is restored rather than sent', () => {
  const {sent, root, head, label} = folderMenuFixture('G1', 'Reading');
  rightClick(head);
  clickItem(root, 'Rename Folder');
  label.textContent = '   ';
  fire(label, 'blur');
  assert.deepEqual(sent, []);
  assert.equal(label.textContent, 'Reading');
});

test('Enter commits a rename without inserting a newline', () => {
  const {sent, root, head, label} = folderMenuFixture('G1', 'Reading');
  rightClick(head);
  clickItem(root, 'Rename Folder');
  label.textContent = 'Research';
  const event = fire(label, 'keydown', {key: 'Enter'});
  assert.equal(event.defaultPrevented, true);
  assert.deepEqual(sent, [['sidebarFolderRenamed', ['G1', 'Research']]]);
});

test('Escape restores the original name and sends nothing', () => {
  const {sent, root, head, label} = folderMenuFixture('G1', 'Reading');
  rightClick(head);
  clickItem(root, 'Rename Folder');
  label.textContent = 'Research';
  fire(label, 'keydown', {key: 'Escape'});
  assert.deepEqual(sent, []);
  assert.equal(label.textContent, 'Reading');
});

// --- the snooze duration menu's preset math -------------------

const SWIFT_TO_UNIX_EPOCH_OFFSET_S = 978307200;
const swiftSeconds = (date) => date.getTime() / 1000 - SWIFT_TO_UNIX_EPOCH_OFFSET_S;

test('In 1 hour is exactly now plus one hour', () => {
  const now = new Date(2026, 0, 15, 10, 0, 0, 0).getTime();
  assert.equal(presetWakeAtSwiftSeconds('inOneHour', now),
               swiftSeconds(new Date(now + 3600 * 1000)));
});

test('This evening before 6pm lands the same day at 18:00', () => {
  const now = new Date(2026, 0, 15, 10, 0, 0, 0).getTime();
  assert.equal(presetWakeAtSwiftSeconds('thisEvening', now),
               swiftSeconds(new Date(2026, 0, 15, 18, 0, 0, 0)));
});

test('This evening after 6pm rolls to tomorrow at 18:00, staying strictly future', () => {
  const now = new Date(2026, 0, 15, 19, 0, 0, 0).getTime();
  assert.equal(presetWakeAtSwiftSeconds('thisEvening', now),
               swiftSeconds(new Date(2026, 0, 16, 18, 0, 0, 0)));
});

test('Tomorrow morning is always the next day at 08:00, regardless of the current hour', () => {
  const now = new Date(2026, 0, 15, 23, 30, 0, 0).getTime();
  assert.equal(presetWakeAtSwiftSeconds('tomorrowMorning', now),
               swiftSeconds(new Date(2026, 0, 16, 8, 0, 0, 0)));
});

test('Next Monday from a Thursday is this coming Monday at 09:00', () => {
  const now = new Date(2026, 0, 15, 10, 0, 0, 0).getTime();  // Thursday
  assert.equal(presetWakeAtSwiftSeconds('nextMonday', now),
               swiftSeconds(new Date(2026, 0, 19, 9, 0, 0, 0)));
});

test('Next Monday from a Monday is never today - it is 7 days out', () => {
  const now = new Date(2026, 0, 19, 8, 0, 0, 0).getTime();  // Monday
  assert.equal(presetWakeAtSwiftSeconds('nextMonday', now),
               swiftSeconds(new Date(2026, 0, 26, 9, 0, 0, 0)));
});

test('every preset is strictly future, never the instant it was picked', () => {
  const now = new Date(2026, 0, 19, 9, 0, 0, 0).getTime();  // Monday, 09:00 exactly
  const nowSwift = swiftSeconds(new Date(now));
  for (const key of ['inOneHour', 'thisEvening', 'tomorrowMorning', 'nextMonday']) {
    assert.ok(presetWakeAtSwiftSeconds(key, now) > nowSwift, key);
  }
});

// --- the rest of the lab's row menu --------------------------

const SPACES = [{name: 'Work'}, {name: 'Play'}, {name: 'Reading'}];

test('an unpinned row carries the lab entries that have a backend', () => {
  // Share, Summarize, Change Icon, Mute and Notes Beside are in the lab but
  // have nothing behind them here, so they are absent rather than dead.
  const {root, tabsZone} = menuFixture({spaces: SPACES});
  const row = tabsZone.children[0];
  row.dataset.url = 'https://x.example/';
  rightClick(row);
  assert.deepEqual(labels(root), [
    'Copy Link', 'Rename', 'Pin Tab', 'Snooze', 'Duplicate', 'Move to...',
    'Put on Shelf', 'Archive Tab', 'Archive Tabs Below', 'Close Tab',
  ]);
});

test('a pinned row is offered neither Archive entry', () => {
  // Both archive the REGULAR list by index, so on a pinned row the same index
  // would sweep an unrelated tab.
  const {root, pinnedZone} = menuFixture({spaces: SPACES});
  rightClick(pinnedZone.children[0]);
  assert.equal(labels(root).includes('Archive Tab'), false);
  assert.equal(labels(root).includes('Archive Tabs Below'), false);
});

test('Duplicate sends the row tab id, not its index', () => {
  const {root, tabsZone, sent} = menuFixture({spaces: SPACES});
  rightClick(tabsZone.children[1]);
  byLabel(root, 'Duplicate').click();
  assert.deepEqual(sent, [['sidebarTabDuplicated', ['t1']]]);
});

test('Archive Tab sends the row position in the regular list', () => {
  const {root, tabsZone, sent} = menuFixture({spaces: SPACES});
  rightClick(tabsZone.children[1]);
  byLabel(root, 'Archive Tab').click();
  assert.deepEqual(sent, [['sidebarTabArchived', [1]]]);
});

test('Archive Tabs Below sends the same position, and the core takes it from there', () => {
  const {root, tabsZone, sent} = menuFixture({spaces: SPACES});
  rightClick(tabsZone.children[0]);
  byLabel(root, 'Archive Tabs Below').click();
  assert.deepEqual(sent, [['sidebarTabsArchivedBelow', [0]]]);
});

test('the last row is offered no Archive Tabs Below', () => {
  const {root, tabsZone} = menuFixture({spaces: SPACES});
  rightClick(tabsZone.children[1]);
  assert.equal(labels(root).includes('Archive Tabs Below'), false);
});

test('Move to... lists the other spaces and not the one the row is in', () => {
  const {root, tabsZone} = menuFixture({spaces: SPACES});
  rightClick(tabsZone.children[0]);
  byLabel(root, 'Move to...').click();
  assert.deepEqual(labels(root), ['Play', 'Reading']);
});

test('choosing a space from Move to... moves the row there', () => {
  const {root, tabsZone, sent} = menuFixture({spaces: SPACES});
  rightClick(tabsZone.children[1]);
  byLabel(root, 'Move to...').click();
  byLabel(root, 'Reading').click();
  assert.deepEqual(sent, [['sidebarTabMovedToSpace', [0, 1, 'tabs', 2, -1]]]);
});

test('a lone space offers no Move to... rather than an empty submenu', () => {
  const {root, tabsZone} = menuFixture({spaces: [{name: 'Only'}]});
  rightClick(tabsZone.children[0]);
  assert.equal(labels(root).includes('Move to...'), false);
});

test('a surface that supplies no space list offers no Move to...', () => {
  const {root, tabsZone} = menuFixture();
  rightClick(tabsZone.children[0]);
  assert.equal(labels(root).includes('Move to...'), false);
});
