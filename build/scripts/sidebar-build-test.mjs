#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// What the sidebar builders map a C++ snapshot onto.
//
//   node --test build/scripts/sidebar-build-test.mjs
//
// Every bug this file has caught in the wild was a data-mapping bug, not a
// layout one. A row matched by id when the favourite that owns it carries only
// a URL left the whole sidebar unmarked. A cold row - persisted, no
// live WebContents, so an EMPTY id - was silently unclickable because the url
// it could have been reopened by was never put on the element.
// Neither shows up as a broken-looking sidebar; both render perfectly and do
// nothing.
//
// So the assertions here are about the attributes and classes the click
// handler, the drag resolver and the C++ side actually read, plus the two
// places attacker-controlled strings enter the tree: tab titles and space
// names. Geometry is not tested and cannot be - see build/scripts/dom-shim.mjs.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

// The builders call document.createElement at build time, not at import time,
// but installing first keeps the order honest if that ever changes.
installDOM();

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {buildFavGrid, buildRows, buildFolder, buildSpaceDots, buildUrlRow,
       renderSidebar} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/sidebar.js'));

// renderSidebar sets this from the snapshot. The builders are called here
// without one, so a URL-keyed icon path would otherwise be unreachable.
const {setIconScope} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/icon-scope.js'));
const SCOPE = 'testscope';
setIconScope(SCOPE);

// --- buildFavGrid -----------------------------------------------------------

test('an empty grid says so, as text rather than as markup', () => {
  const grid = buildFavGrid([]);
  // 'empty' is the state and 'favgrid' is the identity: the empty grid is only
  // the tall drop target it is meant to be through .favgrid.empty, so losing
  // either half leaves a hint floating in an unstyled box.
  assert.equal(grid.className, 'favgrid empty');
  const hint = grid.querySelector('.favhint');
  assert.notEqual(hint, null, 'the empty grid explains what it is for');
  assert.ok(hint.textContent.length, 'and the explanation is not blank');
  assert.equal(hint.children.length, 0, 'the hint is a text node, not a tree');
});

test('an empty grid is still the drop target', () => {
  // Without this the FIRST favourite could never be made by dragging: there
  // would be nothing on screen to aim at.
  assert.equal(buildFavGrid([]).dataset.dropzone, 'favorites');
});

test('a populated grid emits one tile per favourite', () => {
  const grid = buildFavGrid([{id: 'a'}, {id: 'b'}, {id: 'c'}]);
  assert.equal(grid.querySelectorAll('.fav').length, 3);
  assert.equal(grid.dataset.dropzone, 'favorites', 'still a drop target');
  assert.equal(grid.className, 'favgrid', 'named, and no longer empty');
  assert.equal(grid.querySelector('.favhint'), null, 'and no hint left over');
});

test('no tile carries a host-derived tint', () => {
  // The snapshot still ships a per-host hue, produced when it stood in for a
  // favicon that had not been served yet. Painting it turned every tile whose
  // icon did not resolve into a random coloured square.
  const grid = buildFavGrid([{id: 'a', color: '#f00'}, {id: 'b'}]);
  for (const cell of grid.querySelectorAll('.fav')) {
    assert.equal(cell.style.getPropertyValue('--favc'), '');
    assert.ok(!cell.querySelector('.fvi').style.getPropertyValue('background'),
              'and nothing paints one inline either');
  }
});

test('a favourite with no id maps to an empty string', () => {
  // A favourite is keyed by (profile, url) and its id IS its url, so an entry
  // whose url the encoder could not produce arrives without one. Assigning the
  // raw field would put the literal "undefined" on the element and send that
  // string to the browser on click.
  const cell = buildFavGrid([{}]).querySelector('.fav');
  assert.equal(cell.dataset.favId, '');
  assert.equal(cell.getAttribute('data-fav-id'), '');
});

test('every tile is draggable', () => {
  // the grid reorders by drag, and a tile that is not draggable
  // never emits a dragstart to reorder from.
  assert.equal(buildFavGrid([{id: 'a'}]).querySelector('.fav').draggable, true);
});

test('the selected favourite is the only one marked', () => {
  // when a favourite-bound tab is active it is the TILE that gets
  // marked, because no row carries that tab's id at all.
  const [one, two] =
      buildFavGrid([{id: 'a', selected: true}, {id: 'b'}]).querySelectorAll('.fav');
  assert.equal(one.classList.contains('sel'), true);
  assert.equal(two.classList.contains('sel'), false);
});

test('a favourite title containing markup lands on the attribute, unparsed', () => {
  // Titles are attacker-controlled, so the attribute is the title's one safe
  // destination. It has to arrive there: a tile is otherwise unlabelled.
  const evil = '<img src=x onerror=1>';
  const grid = buildFavGrid([{id: 'a', title: evil}]);
  const cell = grid.querySelector('.fav');
  assert.equal(cell.title, evil);
  assert.equal(cell.textContent, 'A', 'the letter is the host\'s, never the title\'s');
  assert.deepEqual(cell.children.map((c) => c.className), ['fvi', 'brdr']);
});

// --- buildRows --------------------------------------------------------------

test('a live row carries both its id and its url', () => {
  const row = buildRows([{id: '7', url: 'https://example.test/'}]).children[0];
  assert.equal(row.dataset.tabId, '7');
  assert.equal(row.dataset.url, 'https://example.test/');
});

test('a cold row carries its url even though the id is empty', () => {
  // A persisted tab with no live WebContents has no id to
  // activate; the url is the only thing that can be acted on, and without it
  // on the element the row looks normal and does nothing at all.
  const row = buildRows([{id: '', url: 'https://cold.test/'}]).children[0];
  assert.equal(row.dataset.tabId, '');
  assert.equal(row.dataset.url, 'https://cold.test/');
});

test('an empty tab list still offers something to drop onto, invisibly', () => {
  // an empty .rows has no height, so without a zone class there was
  // nothing to aim a drop at. No placeholder text renders any more.
  const rows = buildRows([], {dropzone: 'tabs'});
  assert.equal(rows.dataset.dropzone, 'tabs');
  assert.equal(rows.children.length, 0, 'no placeholder text renders');
  assert.ok(rows.classList.contains('emptyzone'),
            'the zone itself has to be aimable');
});

test('a list that is not a dropzone gets no hint', () => {
  assert.equal(buildRows([]).children.length, 0);
});

test('a row is named .tabrow and lays its parts out in order', () => {
  // .tabrow is what routes: the click handler, installRowDrag's rowSelector and
  // the context menu all reach the row through it, so losing it makes the row
  // unclickable. .row is what the shared row styling keys off, including
  // `.row .fvimg` - the favicon is invisible without it. The child order is the
  // rendered order, so a title ahead of its favicon is a different row.
  //
  // .brdr trails them all and is not part of that order: it is the ring
  // overlay, absolutely positioned and out of flow, so it paints over the row
  // rather than taking a place among its parts.
  const row = buildRows([{id: '1'}]).children[0];
  assert.equal(row.className, 'row tabrow');
  assert.deepEqual(row.children.map((c) => c.className),
                   ['fv', 'tl', 'x', 'brdr']);
});

test('a marked row gets one amber dot, an unmarked row none', () => {
  // The dot sits between the title and the close button so it never covers the
  // hover target the close button owns.
  const [marked, plain, twice] = buildRows([
    {id: '1', marked: true},
    {id: '2'},
    {id: '3', marked: true},
  ]).children;
  assert.deepEqual(marked.children.map((c) => c.className),
                   ['fv', 'tl', 'mdot', 'x', 'brdr']);
  assert.deepEqual(plain.children.map((c) => c.className),
                   ['fv', 'tl', 'x', 'brdr']);
  assert.equal(twice.querySelectorAll('.mdot').length, 1,
      'one dot per row however many highlights the page carries');
});

test('no row carries a host-derived tint either', () => {
  // Rows and tiles read the same field, so dropping it in one place only would
  // leave the coloured square in the tab list.
  for (const row of buildRows([{id: '1', color: '#f00'}, {id: '2'}]).children) {
    assert.equal(row.style.getPropertyValue('--favc'), '');
  }
});

test('a row missing both fields still carries both attributes', () => {
  // Snapshots arrive from C++ with fields absent. Both reads must yield the
  // empty string rather than the string "undefined", which is truthy and would
  // be sent to the browser as a tab id.
  const row = buildRows([{}]).children[0];
  assert.equal(row.getAttribute('data-tab-id'), '');
  assert.equal(row.getAttribute('data-url'), '');
});

test('rows are direct children of the element naming the dropzone', () => {
  // The cold-tab click reads row.parentElement.dataset.dropzone and only fires
  // for 'tabs' - a cold PINNED row would come back unpinned, so it is refused
  //. One level of nesting between the two would break both halves.
  const rows = buildRows([{id: '1'}, {id: '2'}], {dropzone: 'tabs'});
  assert.equal(rows.dataset.dropzone, 'tabs');
  for (const row of rows.children) {
    assert.equal(row.parentNode, rows);
  }
});

test('the selected row is the only one marked', () => {
  const rows = buildRows([{id: '1'}, {id: '2', selected: true}, {id: '3'}]);
  assert.deepEqual(rows.children.map((r) => r.classList.contains('sel')),
                   [false, true, false]);
});

test('the discarded row is the only one dimmed', () => {
  const rows = buildRows([{id: '1'}, {id: '2', discarded: true}, {id: '3'}]);
  assert.deepEqual(rows.children.map((r) => r.classList.contains('hib')),
                   [false, true, false]);
});

test('a row whose tab is live in another window is marked, not dimmed as hibernated', () => {
  const rows = buildRows(
      [{id: '1'}, {id: '', elsewhere: true, elsewhereTabId: '77'}, {id: '3'}]);
  assert.deepEqual(rows.children.map((r) => r.classList.contains('elsewhere')),
                   [false, true, false]);
  assert.deepEqual(rows.children.map((r) => r.classList.contains('hib')),
                   [false, false, false],
                   'a moved tab is not a discarded one - the click reloads for ' +
                   'one and takes the page back for the other');
  assert.equal(rows.children[1].dataset.elsewhereTabId, '77',
               'the id the click sends, since tab.id is empty for this row');
});

test('the sidebar has no New Folder button', () => {
  // It was pure duplication: buildEmptySpaceMenuItems already carries a New
  // Folder entry sending the same sidebarFolderCreated message, and that route
  // has its own test. The button was also actively harmful - a coordinate that
  // measured blank sat inside its empty interior, and two VM runs created
  // folders instead of clicking the row they were aiming at.
  const rows = buildRows([{id: '1'}]);
  const root = rows.parentNode || rows;
  assert.equal(root.querySelectorAll('.newfolder').length, 0);
});

test('a tab title containing markup lands as text', () => {
  const evil = '<img src=x onerror=alert(1)>';
  const label = buildRows([{id: '1', title: evil}]).querySelector('.tl');
  assert.equal(label.textContent, evil);
  assert.equal(label.children.length, 0, 'parsed as markup it would be an img');
});

test('no row builds a link, so a url has no navigation sink here', () => {
  // A persisted url can be any scheme. It is refused on the C++ side rather
  // than here, which only holds while the surface keeps it in a data attribute
  // and never in an href.
  const row = buildRows([{id: '', url: 'javascript:alert(1)'}]).children[0];
  assert.equal(row.dataset.url, 'javascript:alert(1)');
  assert.equal(row.querySelectorAll('a').length, 0);
  assert.equal(row.querySelectorAll('[href]').length, 0);
});

test('a live row asks by tab id, a cold row by url', () => {
  // Asking only for a live tab leaves every restored row an empty
  // square for the session: the tab-keyed cache holds what a live WebContents
  // pushed, and after a cold launch that is nothing.
  assert.equal(buildRows([{id: '7'}]).querySelector('.fvimg').src,
               '/favicon/' + SCOPE + '/7.png');
  assert.equal(buildRows([{id: '', url: 'u'}]).querySelector('.fvimg').src,
               '/urlicon/' + SCOPE + '/u.png');
});

test('a favicon that fails to load takes itself out of the way', () => {
  // A 404 that leaves the img in place renders the browser's own missing-image
  // box in every row it happened to.
  const row = buildRows([{id: '7'}]).children[0];
  row.querySelector('.fvimg').dispatchEvent(new Event('error'));
  assert.equal(row.querySelector('.fvimg'), null);
  assert.equal(row.querySelector('.fv').children.length, 0, 'slot left empty');
});

test('a tab id cannot escape the favicon path', () => {
  const img = buildRows([{id: '7/../evil'}]).querySelector('.fvimg');
  assert.equal(img.src, '/favicon/' + SCOPE + '/7%2F..%2Fevil.png');
});

test('the close glyph is built in the SVG namespace', () => {
  // createElement would produce an unknown HTML element that renders nothing,
  // leaving a close button with no visible glyph.
  const svg = buildRows([{id: '1'}]).querySelector('svg');
  const ns = 'http://www.w3.org/2000/svg';
  assert.equal(svg.namespaceURI, ns);
  assert.equal(svg.querySelector('path').namespaceURI, ns);
});

test('a click on the close glyph resolves to the close button', () => {
  // The glyph is what the pointer actually lands on, and the click handler
  // routes on e.target.closest('.x'). If the glyph sat outside the button the
  // click would fall through to the row and activate the tab it meant to close.
  const row = buildRows([{id: '1'}]).children[0];
  const glyph = row.querySelector('path');
  assert.equal(glyph.closest('.x'), row.querySelector('.x'));
  assert.equal(glyph.closest('.tabrow'), row);
});

test('rows are draggable only when the caller asks', () => {
  // Grouped rows stay undraggable on purpose: a drop out of a folder has no
  // target vocabulary in the ABI yet.
  assert.equal(buildRows([{id: '1'}], {draggable: true}).children[0].draggable,
               true);
  assert.ok(!buildRows([{id: '1'}]).children[0].draggable);
});

// --- buildFolder ------------------------------------------------------------

const folderOf = (folder) => {
  const frag = buildFolder(folder);
  return {head: frag.children[0], fold: frag.children[1], frag};
};

// The glyph stack a folder head draws, in order.
const glyphsIn = (head) =>
    head.querySelector('.fic').children.map((c) => c.getAttribute('class'));

test('a folder renders its head and then its body', () => {
  const {frag} = folderOf({id: 'G1', name: 'Work'});
  assert.deepEqual(frag.children.map((c) => c.className), ['row frow', 'fold']);
});

test('the head and the body are paired by the same folder id', () => {
  // The fold toggle finds the body with .fold[data-folder-body="ID"] read off
  // the head it was clicked on. Diverging names would leave every folder inert.
  const {head, fold} = folderOf({id: 'G1', name: 'Work'});
  assert.equal(head.dataset.folderId, 'G1');
  assert.equal(fold.dataset.folderBody, 'G1');
});

test('a closed folder marks both halves', () => {
  // Both: the chevron rotates off the head and the collapse animates off the
  // body, so marking one alone renders a half-open folder.
  const {head, fold} = folderOf({id: 'G1', closed: true});
  assert.equal(head.classList.contains('closedH'), true);
  assert.equal(fold.classList.contains('closed'), true);
});

test('an open folder marks neither half', () => {
  const {head, fold} = folderOf({id: 'G1'});
  assert.equal(head.classList.contains('closedH'), false);
  assert.equal(fold.classList.contains('closed'), false);
});

test('a folder renders its tabs as rows', () => {
  const {fold} =
      folderOf({id: 'G1', tabs: [{id: '1', url: 'a'}, {id: '', url: 'b'}]});
  const rows = fold.querySelectorAll('.tabrow');
  assert.equal(rows.length, 2);
  assert.deepEqual(rows.map((r) => r.dataset.url), ['a', 'b']);
});

test('the element that parents the folder rows names the dropzone', () => {
  // Same contract the flat list has: the drag resolver reads the section off
  // the row's parent, so an extra wrapper would resolve every drop to nothing.
  const {fold} = folderOf(
      {id: '__pinned__', dropzone: 'pinned', tabs: [{id: '1'}]});
  const inner = fold.querySelector('.foldin');
  assert.equal(inner.dataset.dropzone, 'pinned');
  assert.equal(fold.querySelector('.tabrow').parentNode, inner);
});

test('a folder with no dropzone declares none', () => {
  // 'undefined' is a section dropDecision has no case for, and it would make
  // an unhandled drag resolve to a message instead of staying inert.
  const {fold} = folderOf({id: 'G1', tabs: [{id: '1'}]});
  assert.equal(fold.querySelector('.foldin').hasAttribute('data-dropzone'), false);
});

test('the head names the same dropzone as the body', () => {
  // The body alone collapses to zero height when closed, so a folded section
  // needs its head reachable as a drop target too.
  const {head} = folderOf(
      {id: 'G1', dropzone: 'folder:G1', tabs: [{id: '1'}]});
  assert.equal(head.dataset.dropzone, 'folder:G1');
});

test('a folder head with no dropzone declares none either', () => {
  const {head} = folderOf({id: 'G1', tabs: [{id: '1'}]});
  assert.equal(head.hasAttribute('data-dropzone'), false);
});

test('an empty folder renders no hint text, its head is the drop target', () => {
  const {head, fold} = folderOf({id: 'G1', dropzone: 'folder:G1', tabs: []});
  assert.equal(fold.querySelector('.foldhint'), null);
  assert.equal(fold.querySelector('.foldin').childNodes.length, 0);
  assert.equal(head.dataset.dropzone, 'folder:G1');
});

test('a folder with tabs renders no hint', () => {
  const {fold} = folderOf({id: 'G1', dropzone: 'folder:G1', tabs: [{id: '1'}]});
  assert.equal(fold.querySelector('.foldhint'), null);
});

test('an empty folder with no dropzone renders no hint either', () => {
  // Nothing to drop into it at all - a hint here would promise a target that
  // is not one.
  const {fold} = folderOf({id: 'G1', tabs: []});
  assert.equal(fold.querySelector('.foldhint'), null);
});

test('a section header keeps the chevron and drops the folder glyph', () => {
  // The pinned section folds through the folder machinery but is not a folder
  //; it has to keep reading as the uppercase header it was.
  const {head} =
      folderOf({id: '__pinned__', name: 'Pinned', header: 'section'});
  assert.equal(head.className, 'row frow sechdrow');
  assert.deepEqual(glyphsIn(head), ['chev']);
  assert.equal(head.querySelector('.tl').textContent, 'Pinned');
});

test('a folder head shows the folder glyph and the chevron', () => {
  const {head} = folderOf({id: 'G1', name: 'Work'});
  assert.deepEqual(glyphsIn(head), ['fic-g', 'chev']);
  // The glyph stack leads and the name follows it; reversed, the folder reads
  // as a name with its icons trailing off the end of the row.
  assert.deepEqual(head.children.map((c) => c.className), ['fic', 'tl']);
});

test('a custom folder icon lands as text', () => {
  // The icon is a user-supplied character, so it goes in the same way a title
  // does and replaces the drawn glyph rather than joining it.
  const {head} = folderOf({id: 'G1', name: 'Work', icon: '<b>x</b>'});
  const custom = head.querySelector('.fic').children[0];
  assert.equal(custom.textContent, '<b>x</b>');
  assert.equal(custom.children.length, 0);
});

test('folder rows are draggable only when the folder says so', () => {
  const pinned =
      folderOf({id: '__pinned__', draggableRows: true, tabs: [{id: '1'}]});
  assert.equal(pinned.fold.querySelector('.tabrow').draggable, true);
  const group = folderOf({id: 'G1', tabs: [{id: '1'}]});
  assert.ok(!group.fold.querySelector('.tabrow').draggable);
});

test('a folder with no fields at all still renders both halves', () => {
  const {head, fold} = folderOf({});
  assert.equal(head.dataset.folderId, '');
  assert.equal(fold.dataset.folderBody, '');
  assert.equal(fold.querySelectorAll('.tabrow').length, 0);
});

// --- the pinned section: a mixed list of tabs and folders --------------------
// snapshot.pinned is an item list now - a folder entry counts as one item
// among the pinned tabs, sharing the same drop zone.

test('the pinned section renders a folder entry between two tab entries', () => {
  const root = document.createElement('div');
  renderSidebar(root, {
    pinned: [
      {id: 't1', title: 'Before', url: 'https://before.example/'},
      {kind: 'folder', id: 'G1', name: 'Group', tabs: [{id: 'g1'}]},
      {id: 't2', title: 'After', url: 'https://after.example/'},
    ],
  });
  // The pinned fold header carries the same dropzone name, so its head is a
  // drop target too. .foldin is what disambiguates the body from it.
  const zone = root.querySelector('.foldin[data-dropzone="pinned"]');
  const items = [...zone.children].filter((el) => el.matches('.tabrow, .frow'));
  assert.equal(items.length, 3);
  assert.equal(items[0].dataset.tabId, 't1');
  assert.equal(items[1].dataset.folderId, 'G1');
  assert.equal(items[2].dataset.tabId, 't2');
});

test('a pinned folder\'s own members render inside it, draggable', () => {
  const root = document.createElement('div');
  renderSidebar(root, {
    pinned: [{kind: 'folder', id: 'G1', name: 'Group',
              tabs: [{id: 'g1'}, {id: 'g2'}]}],
  });
  const body = root.querySelector('.foldin[data-dropzone="folder:G1"]');
  const rows = body.querySelectorAll('.tabrow');
  assert.equal(rows.length, 2);
  assert.equal(rows[0].draggable, true);
});

// --- buildSpaceDots ---------------------------------------------------------

// The tinted chip inside a dot, which is what carries the label and colour.
const chipAt = (bar, i) => roomDots(bar)[i].querySelector('.d');

// Descendants, not children: the rooms live inside their own scroller
//, and only they carry a space index.
const roomDots = (bar) =>
    [...bar.querySelectorAll('.sdot')].filter((d) => d.dataset.spaceIndex !== undefined);

test('the active index is the only dot marked', () => {
  const bar = buildSpaceDots([{name: 'A'}, {name: 'B'}, {name: 'C'}], 1);
  assert.deepEqual(roomDots(bar).map((d) => d.classList.contains('active')),
                   [false, true, false]);
});

test('each dot carries its index as the string the click handler parses', () => {
  // The handler does Number(dot.dataset.spaceIndex) and hands the result
  // straight to the swipe controller, so an index that is off is a switch to
  // the wrong space.
  const bar = buildSpaceDots([{name: 'A'}, {name: 'B'}, {name: 'C'}], 0);
  assert.deepEqual(roomDots(bar).map((d) => d.dataset.spaceIndex),
                   ['0', '1', '2']);
});

test('every dot is labelled with its space name, markup and all', () => {
  // A space name is user-supplied, and an inactive dot draws no text at all, so
  // the attribute is both the only label a dot has and the only safe place for
  // the name to go.
  const bar = buildSpaceDots([{name: 'Alpha'}, {name: '<img src=x onerror=1>'}], 0);
  assert.deepEqual(roomDots(bar).map((d) => d.title),
                   ['Alpha', '<img src=x onerror=1>']);
  assert.equal(roomDots(bar)[1].children.length, 1, 'the chip, and nothing else');
});

test('only the active dot shows an initial', () => {
  const bar = buildSpaceDots([{name: 'Alpha'}, {name: 'Beta'}], 1);
  assert.equal(chipAt(bar, 0).textContent, '');
  assert.equal(chipAt(bar, 1).textContent, 'B');
});

test('an explicit space icon beats the name initial', () => {
  const bar = buildSpaceDots([{name: 'Zed', icon: 'Z!'}], 0);
  assert.equal(chipAt(bar, 0).textContent, 'Z!');
});

test('a space icon containing markup lands as text', () => {
  const bar = buildSpaceDots([{name: 'X', icon: '<img src=x onerror=1>'}], 0);
  assert.equal(chipAt(bar, 0).textContent, '<img src=x onerror=1>');
  assert.equal(chipAt(bar, 0).children.length, 0);
});

// the Arc look. A space that is not selected is a plain grey dot,
// and the selected one is its bare glyph in the space's ink.
test('no dot is filled, and only the active glyph takes its space ink', () => {
  const bar =
      buildSpaceDots([{name: 'A', color: '#111'}, {name: 'B', color: '#222'}], 1);
  assert.equal(chipAt(bar, 0).style.getPropertyValue('background'), '');
  assert.equal(chipAt(bar, 1).style.getPropertyValue('background'), '');
  assert.ok(!chipAt(bar, 0).style.color);
  assert.ok(chipAt(bar, 1).style.color, 'the active glyph is inked');
});

test('an overview space is labelled and never tinted', () => {
  // The overview chip is the same glyph in both states, and a tint would make
  // it read as an ordinary space.
  const bar = buildSpaceDots([{overview: true, name: 'O', color: '#111'},
                              {overview: true, name: 'P', color: '#222'}], 0);
  assert.deepEqual(roomDots(bar).map((d) => d.classList.contains('ovdot')),
                   [true, true]);
  for (const dot of roomDots(bar)) {
    const chip = dot.querySelector('.d');
    assert.equal(chip.textContent, '::');
    assert.equal(chip.style.getPropertyValue('background'), '');
  }
});

test('a space with nothing set renders a dot rather than throwing', () => {
  const bar = buildSpaceDots([{}], 0);
  assert.equal(roomDots(bar).length, 1);
  assert.equal(chipAt(bar, 0).textContent, '');
});

// --- a row dragged over the dots -------------------------------

const dragover = (el) => el.dispatchEvent(new Event('dragover', {bubbles: true}));

test('hovering a dot with a dragged row slides the rail to that space', () => {
  const hits = [];
  const bar = buildSpaceDots([{name: 'A'}, {name: 'B'}], 0, false,
                             {onDotHover: (i) => hits.push(i)});
  dragover(roomDots(bar)[1]);
  assert.deepEqual(hits, [1]);
});

test('the hover jump is throttled, so crossing the bar does not stampede', () => {
  // Back-to-back dragover events are what a real drag produces - one per
  // frame. Without the dwell every frame would restart the slide and the rail
  // would never arrive anywhere.
  const hits = [];
  const bar = buildSpaceDots([{name: 'A'}, {name: 'B'}, {name: 'C'}], 0, false,
                             {onDotHover: (i) => hits.push(i)});
  dragover(roomDots(bar)[1]);
  dragover(roomDots(bar)[2]);
  dragover(roomDots(bar)[1]);
  assert.deepEqual(hits, [1]);
});

test('a dot dragged over another does not jump - that gesture reorders', () => {
  const hits = [];
  const bar = buildSpaceDots([{name: 'A'}, {name: 'B'}], 0, false,
                             {onDotHover: (i) => hits.push(i)});
  roomDots(bar)[0].dispatchEvent(new Event('dragstart', {bubbles: true}));
  dragover(roomDots(bar)[1]);
  assert.deepEqual(hits, []);
});

test('dropping a row on a dot reports that space', () => {
  const drops = [];
  const bar = buildSpaceDots([{name: 'A'}, {name: 'B'}], 0, false,
                             {onDotDrop: (i) => drops.push(i)});
  roomDots(bar)[1].dispatchEvent(new Event('drop', {bubbles: true}));
  assert.deepEqual(drops, [1]);
});

test('dropping a dot on a dot is a reorder, not a tab move', () => {
  const drops = [];
  const bar = buildSpaceDots([{name: 'A'}, {name: 'B'}], 0, false,
                             {onDotDrop: (i) => drops.push(i)});
  roomDots(bar)[0].dispatchEvent(new Event('dragstart', {bubbles: true}));
  roomDots(bar)[1].dispatchEvent(new Event('drop', {bubbles: true}));
  assert.deepEqual(drops, []);
});

test('the address row carries the quick menu button', () => {
  const row = buildUrlRow({tabs: [], favorites: []});
  const button = row.querySelector('.qm-open');
  assert.ok(button, 'no puzzle button beside the address field');
  assert.equal(button.title, 'Extensions and site');
});

test('the button reports its own box, not the field\'s', () => {
  const h2 = installDOM({pathname: '/sidebar'});
  const row = buildUrlRow({tabs: [], favorites: []});
  document.body.appendChild(row);
  const button = row.querySelector('.qm-open');
  button.setRect({left: 190, top: 46, width: 20, height: 20});
  h2.sent.length = 0;
  button.click();
  assert.deepEqual(h2.sent, [['quickMenuToggled', [190, 46, 20, 20]]]);
});
