#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// installSidebar's delegated click routing and its chrome.send payloads.
//
//   node --test build/scripts/sidebar-install-test.mjs
//
// One listener on the root decides what every click in the sidebar means, and
// each branch ends in a chrome.send. That pair IS the surface's contract with
// C++, and both halves fail quietly in their own way:
//
//   routing   a branch that claims a click the branch below it wanted leaves a
//             control dead with no error anywhere - the close button firing an
//             activate as well, or a cold row sending an empty id.
//   payload   an unregistered name is FATAL in the BROWSER process
//             (content::WebUIImpl::ProcessWebUIMessage NOTREACHEDs), and a
//             wrong argument TYPE is a silent no-op, because every handler in
//             atrium_browser_window.mm gates on is_string/is_bool/is_int.
//
// So the exact [name, args] pair is asserted, not just that something was sent.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';
import {waitFor} from './wait-for.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');

const h = installDOM({pathname: '/sidebar'});

const {installSidebar, renderSidebar, createFoldState, PINNED_FOLD_ID} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/sidebar.js'));
const {iconKey, setIconScope} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/icon-scope.js'));

// The registered names, read from the header rather than copied, so a rename on
// either side shows up here instead of at the next launch.
const REGISTERED = new Set(
    fs.readFileSync(
          path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
          'utf8')
        .split('kSurfaceMessages[] = {')[1]
        .split('};')[0]
        .match(/"[^"]+"/g)
        .map((quoted) => quoted.slice(1, -1)));

// One live row, one cold row, and a cold row in every section that renders one -
// the shape a restored session produces.
const SNAPSHOT = {
  spaceName: 'Work',
  spaceTint: '#4488cc',
  favorites: [{id: 'https://fav.example/', title: 'Fav'}],
  pinned: [
    {id: 'p1', title: 'Pinned live', url: 'https://pin.example/'},
    {id: '', title: 'Pinned cold', url: 'https://coldpin.example/'},
  ],
  folders: [{
    id: 'G1',
    name: 'Reading',
    tabs: [
      {id: 'g1', title: 'Grouped', url: 'https://g.example/'},
      {id: '', title: 'Grouped cold', url: 'https://coldg.example/'},
    ],
  }],
  tabs: [
    {id: 't1', title: 'First', url: 'https://one.example/'},
    {id: 't2', title: 'Second', url: 'https://two.example/'},
    {id: '', title: 'Cold', url: 'https://cold.example/'},
    {title: 'Neither'},
  ],
};

const RAIL_SNAPSHOT = {
  ...SNAPSHOT,
  activeSpace: 0,
  spaces: [{name: 'Work', color: '#48c'}, {name: 'Play', color: '#c84'}],
};

// The same shape restored with both folds already shut. Every other snapshot
// here arrives fully open, which leaves the collapsed half of the render
// unexercised - and a fold routes off the state the DOM is IN, so that half
// decides what the first click reports.
const COLLAPSED_SNAPSHOT = {
  ...SNAPSHOT,
  pinnedCollapsed: true,
  folders: [{...SNAPSHOT.folders[0], closed: true}],
};

function fresh() {
  h.reset();
  const root = document.createElement('div');
  h.body.appendChild(root);
  installSidebar(root);
  return root;
}

// Installed, pushed, and with the install handshake drained, so a test asserts
// only what its own gesture produced.
function mount(snapshot = SNAPSHOT) {
  const root = fresh();
  h.window.atriumSetSidebar(snapshot);
  h.sent.length = 0;
  return root;
}

const click = (el) => el.dispatchEvent(new Event('click', {bubbles: true}));

// The nth SPACE pane. The Overview pane sits at rail position 0,
// so a bare .railpane[0] now lands on a pane that holds no rows.
const spacePane = (root, i = 0) =>
    root.querySelectorAll('.railpane')
        .filter((p) => !p.classList.contains('ovrailpane'))[i];

const rowsIn = (root, section) =>
    root.querySelectorAll(`[data-dropzone="${section}"] .tabrow`);

// One wheel event and the frame after it, the way a trackpad interleaves them.
function drag(root, deltaX, events) {
  for (let i = 0; i < events; i++) {
    root.dispatchEvent(new Event('wheel', {bubbles: true, deltaX, deltaY: 0}));
    h.tick();
  }
}

// The swipe settles over frames; a dot press is not finished until it stops.
function runFrames(limit = 400) {
  let frames = 0;
  while (h.pendingFrames && frames < limit) {
    h.tick();
    frames++;
  }
  return frames;
}

// --- the handshake ----------------------------------------------------------

test('installing announces the surface is live', () => {
  fresh();
  // The push is a reply to this. Without it C++ pushes into a document that has
  // no atriumSetSidebar yet and the snapshot is silently lost.
  assert.deepEqual(h.sent, [['sidebarReady', []]]);
});

test('the push entrypoint lands on window under the name C++ calls', () => {
  fresh();
  // PushSidebarSnapshot executes the literal "window.atriumSetSidebar(...)", so
  // renaming this breaks every push with no error on either side.
  assert.equal(typeof h.window.atriumSetSidebar, 'function');
});

test('a snapshot reports its favourite count back', () => {
  fresh();
  h.window.atriumSetSidebar(SNAPSHOT);
  assert.deepEqual(h.sent.filter((m) => m[0] === 'sidebarFavoriteCount'),
                   [['sidebarFavoriteCount', [1]]]);
});

test('a snapshot with no fields at all still renders', () => {
  // Every field is optional on the wire: the dylib-less arm emits a flat tab
  // list and nothing else.
  const root = fresh();
  h.window.atriumSetSidebar({});
  assert.equal(root.querySelectorAll('.sidebar').length, 1);
  assert.deepEqual(h.sent.filter((m) => m[0] === 'sidebarFavoriteCount'),
                   [['sidebarFavoriteCount', [0]]]);
});

test('a push with no snapshot at all is treated as an empty one', () => {
  const root = fresh();
  h.window.atriumSetSidebar(undefined);
  assert.equal(root.querySelectorAll('.sidebar').length, 1);
});

// --- the applied-push counter -------------------------------------------------

// A parked push and an applied one leave the same run log, so a VM scenario
// cannot tell a surface that redrew from one frozen on a stale frame. This
// counter is the only thing that separates them, and it is read by name.
test('every applied push bumps the counter the scenarios read', () => {
  fresh();
  h.window.__atriumApplyCount = 0;
  h.window.atriumSetSidebar(SNAPSHOT);
  assert.equal(h.window.__atriumApplyCount, 1);
  h.window.atriumSetSidebar(SNAPSHOT);
  assert.equal(h.window.__atriumApplyCount, 2);
});

// The drag latch parks pushes so a rebuild cannot destroy the node the drag
// started from. A dragstart that never sees its dragend leaves it armed, and
// from then on every push parks - which is what a frozen sidebar looks like.
test('a push arriving mid-drag parks, and the drag ending applies it', () => {
  const root = mount();
  const tile = root.querySelector('.fav');
  h.window.__atriumApplyCount = 0;

  tile.dispatchEvent(new Event('dragstart', {bubbles: true}));
  h.window.atriumSetSidebar(SNAPSHOT);
  assert.equal(h.window.__atriumApplyCount, 0,
               'a push mid-drag must not rebuild the drag source away');

  root.dispatchEvent(new Event('dragend', {bubbles: true}));
  assert.equal(h.window.__atriumApplyCount, 1,
               'and the end of the drag drains what was parked');
});

// A drag released over another window never delivers dragend, and the sidebar
// then stops redrawing for the rest of the session. A live drag fires dragover
// continuously, so silence is the signal.
test('a drag that never ends stops holding pushes back', async () => {
  const root = mount();
  const tile = root.querySelector('.fav');
  h.window.__atriumApplyCount = 0;

  tile.dispatchEvent(new Event('dragstart', {bubbles: true}));
  h.window.atriumSetSidebar(SNAPSHOT);
  assert.equal(h.window.__atriumApplyCount, 0,
               'the control: it still parks while the drag looks live');

  // No dragend, no drop, and no dragover to say the drag is still going.
  try {
    await waitFor(() => h.window.__atriumApplyCount === 1);
  } finally {
    // The edge poll is a real interval, so a drag left in flight keeps the
    // test runner alive after the file is done.
    root.dispatchEvent(new Event('dragend', {bubbles: true}));
  }
});

// The watchdog must not fire under a drag that is genuinely still moving, or
// it rebuilds the sidebar under the pointer.
test('a drag still firing dragover keeps holding pushes back', async () => {
  const root = mount();
  const tile = root.querySelector('.fav');
  h.window.__atriumApplyCount = 0;

  tile.dispatchEvent(new Event('dragstart', {bubbles: true}));
  h.window.atriumSetSidebar(SNAPSHOT);

  for (let i = 0; i < 8; i++) {
    await new Promise((done) => setTimeout(done, 100));
    root.dispatchEvent(new Event('dragover', {bubbles: true}));
  }
  assert.equal(h.window.__atriumApplyCount, 0,
               'a moving drag must still park, however long it runs');

  root.dispatchEvent(new Event('dragend', {bubbles: true}));
  assert.equal(h.window.__atriumApplyCount, 1);
});

// --- the sidebar fades in as one piece ------------------------------

test('the root is marked drawn on its first render, and the sheet hides it until then', () => {
  const root = mount();
  assert.ok(root.classList.contains('drawn'));
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  assert.match(css, /#atrium-root\.sbroot \{[^}]*opacity: 0;/);
  assert.match(css, /#atrium-root\.sbroot\.drawn \{\s*opacity: 1;/);
});


// --- activating a row -------------------------------------------------------

test('clicking a row activates the row that was clicked', () => {
  const root = mount();
  click(rowsIn(root, 'tabs')[1]);
  assert.deepEqual(h.sent, [['sidebarTabClicked', ['t2']]]);
});

// The optimistic ring promises a navigation, and activating a live tab makes
// none - so nothing arrives to clear the mark and it pulses for its full
// timeout. True whether the click moves focus or not.
test('clicking a row that has a live tab starts no ring, moved or not', () => {
  const root = mount({
    ...SNAPSHOT,
    tabs: [{id: 'rc1', title: 'First', url: 'https://rc1.example/', selected: true},
           {id: 'rc2', title: 'Second', url: 'https://rc2.example/'}],
  });
  const rowFor = (id) => root.querySelector(`[data-tab-id="${id}"]`);
  click(rowFor('rc1'));
  assert.equal(rowFor('rc1').classList.contains('loading'), false,
               'a re-click on the focused row drew a ring for nothing');
  assert.equal(h.sent.some((m) => m[0] === 'sidebarTabClicked'), true,
               'the click still has to reach the host');
  click(rowFor('rc2'));
  assert.equal(rowFor('rc2').classList.contains('loading'), false,
               'switching to another loaded tab drew a ring for nothing');
});

// The other half, or the pair above would pass on a build that never rings at
// all: a tile with no live tab HAS to open one, and that does load.
test('clicking a cold favourite tile still rings at once', () => {
  const root = mount({
    ...SNAPSHOT,
    favorites: [{id: 'https://cold.example/', title: 'Cold'}],
  });
  const tile = root.querySelector('.fav');
  assert.ok(tile, 'no tile to click');
  // Always set, empty when there is no live tab (sidebar.js:169), so cold is
  // falsy rather than absent - which is what the click branch tests.
  assert.ok(!tile.dataset.favTab, 'this tile must be cold');
  click(tile);
  assert.equal(tile.classList.contains('loading'), true,
               'a cold tile opens a page and must say so before it arrives');
});

test('a grouped row activates like any other', () => {
  // It lives in a folder body rather than the tab list, so it reaches the
  // handler through a different section - but it has a live id, and that is all
  // an activate needs.
  const root = mount();
  click(rowsIn(root, 'folder:G1')[0]);
  assert.deepEqual(h.sent, [['sidebarTabClicked', ['g1']]]);
});

test('clicking a cold row reopens it by URL', () => {
  // A cold row is persisted but has no live tab, so there is no id to
  // activate - the URL is the only handle on it.
  const root = mount();
  click(rowsIn(root, 'tabs')[2]);
  assert.deepEqual(h.sent, [['sidebarColdTabClicked', ['https://cold.example/']]]);
});

// a cold row's click marked nothing, so it never rang at all. Each
// test below uses a url of its own, not the shared fixture's cold.example -
// loadingKeys is a module singleton with a real timer, and a key another test
// also used can still be live here and mark this row for the wrong reason.
test('clicking a cold row still rings at once', () => {
  const root = mount({
    ...SNAPSHOT,
    tabs: [{id: '', title: 'Cold', url: 'https://cold-row.example/'}],
  });
  const row = rowsIn(root, 'tabs')[0];
  assert.ok(!row.dataset.tabId, 'this row must be cold');
  click(row);
  assert.equal(row.classList.contains('loading'), true,
               'a cold row opens a page and must say so before it arrives');
});

test('a cold row keeps its ring through the snapshot its reopen lands', () => {
  // The reopen answers with a snapshot naming the new tab, selected - the row
  // is rebuilt, and the mark has to survive by key, as a cold favourite's mark
  // already does across the same kind of rebuild.
  const cold = {
    ...SNAPSHOT,
    tabs: [{id: '', title: 'Cold', url: 'https://cold-reopen.example/'}],
  };
  const root = mount(cold);
  click(rowsIn(root, 'tabs')[0]);
  h.window.atriumSetSidebar({
    ...cold,
    tabs: [{
      id: 'newid',
      title: 'Cold',
      url: 'https://cold-reopen.example/',
      selected: true,
    }],
  });
  const reborn = rowsIn(root, 'tabs')[0];
  assert.equal(reborn.dataset.tabId, 'newid');
  assert.equal(reborn.classList.contains('loading'), true,
               'the ring dropped when the snapshot rebuilt the row');
});

test('a cold row never sends an activate with an empty id', () => {
  // dataset.tabId is '' for a cold row, and '' is truthy
  // to nobody, so the row was silently inert. Sending it anyway is the other
  // failure - CloseSidebarTab-style id matching would never match it.
  const root = mount();
  click(rowsIn(root, 'tabs')[2]);
  assert.equal(h.sent.some((m) => m[0] === 'sidebarTabClicked'), false);
});

test('a row whose tab is live in another window sends that id, not a reopen', () => {
  // The row has no id in THIS window and a url like any cold row, so without a
  // branch of its own it fell into the cold path and reopened the page - the
  // second copy the task exists to stop. Sending elsewhereTabId instead reaches
  // BrowserController::ActivateTab, which moves the live page here.
  const root = mount({
    ...SNAPSHOT,
    tabs: [{
      id: '',
      elsewhere: true,
      elsewhereTabId: '77',
      title: 'Live next door',
      url: 'https://moved.example/',
    }],
  });
  click(rowsIn(root, 'tabs')[0]);
  assert.deepEqual(h.sent, [['sidebarTabClicked', ['77']]]);
  assert.equal(h.sent.some((m) => m[0] === 'sidebarColdTabClicked'), false,
               'reopening it by url is the duplicate this replaces');
});

// Each test below uses a url of its own, not the shared fixture's - loadingKeys
// is a module singleton with a real timer, and a key another test also used can
// still be live here and mark this row for the wrong reason.
test('clicking a row live in another window rings at once', () => {
  const root = mount({
    ...SNAPSHOT,
    tabs: [{
      id: '',
      elsewhere: true,
      elsewhereTabId: '77',
      title: 'Live next door',
      url: 'https://elsewhere-tab.example/',
    }],
  });
  const row = rowsIn(root, 'tabs')[0];
  click(row);
  assert.equal(row.classList.contains('loading'), true,
               'the click has to mark the row before the resolve arrives');
});

test('a row live elsewhere keeps its ring through the snapshot its resolve lands', () => {
  const elsewhere = {
    ...SNAPSHOT,
    tabs: [{
      id: '',
      elsewhere: true,
      elsewhereTabId: '78',
      title: 'Live next door',
      url: 'https://resolve-tab.example/',
    }],
  };
  const root = mount(elsewhere);
  click(rowsIn(root, 'tabs')[0]);
  h.window.atriumSetSidebar({
    ...elsewhere,
    tabs: [{
      id: '78',
      title: 'Live next door',
      url: 'https://resolve-tab.example/',
      selected: true,
    }],
  });
  const resolved = rowsIn(root, 'tabs')[0];
  assert.equal(resolved.dataset.tabId, '78');
  assert.equal(resolved.classList.contains('loading'), true,
               'the ring dropped when the resolve rebuilt the row');
});

test('an identical snapshot still rebuilds, because the swipe needs the pass', () => {
  // A redundant-render skip would return before swipe.attach(), the only
  // thing that clears the swipe machine's awaitingAck latch, so a skipped
  // push would leave a swipe never acknowledged and open to being undone by
  // the next push that does render.
  //
  // Pinned in the direction that matters now: an identical push must still
  // reach the render, because that pass is what tells the rail its gesture
  // landed.
  const root = mount();
  const first = root.querySelector('.row');
  h.window.atriumSetSidebar(JSON.parse(JSON.stringify(SNAPSHOT)));
  assert.notEqual(root.querySelector('.row'), first,
                  'an identical push must still rebuild - the swipe reads it');
});

test('a cold PINNED row reopens its page', () => {
  // Reopening by URL does not lose the pinning. open_tab's adoptable_row searches pinned_tabs BEFORE tabs, so
  // the reopened tab adopts the pinned row it came from and the pin survives -
  // see adopting_a_pinned_row_leaves_it_pinned. Refusing the click would leave
  // a pinned tab that had never loaded and could not be clicked at all.
  const root = mount();
  const row = rowsIn(root, 'pinned')[1];
  click(row);
  assert.deepEqual(h.sent, [['sidebarColdTabClicked', [row.dataset.url]]]);
});

test('a cold row inside a folder takes no route either', () => {
  // Same reason as a pinned one: it would come back outside its group.
  const root = mount();
  click(rowsIn(root, 'folder:G1')[1]);
  assert.deepEqual(h.sent, []);
});

test('a row with neither an id nor a URL is inert', () => {
  const root = mount();
  click(rowsIn(root, 'tabs')[3]);
  assert.deepEqual(h.sent, []);
});

// --- closing a row ----------------------------------------------------------

test('the close button closes and does not also activate', () => {
  // Both controls are the same click: the button sits inside the row, so the
  // row branch would claim it too. Exactly one message, and it is the close.
  const root = mount();
  click(rowsIn(root, 'tabs')[0].querySelector('.x'));
  assert.deepEqual(h.sent, [['sidebarTabClosed', ['t1']]]);
});

test('the close glyph counts as the close button', () => {
  // The real click lands on the SVG path, never on the button itself - the
  // glyph fills it. Matching the target instead of walking up from it would
  // make the X activate the tab.
  const root = mount();
  const glyph = rowsIn(root, 'tabs')[0].querySelector('.x').querySelector('path');
  click(glyph);
  assert.deepEqual(h.sent, [['sidebarTabClosed', ['t1']]]);
});

test('close carries the id of its own row', () => {
  const root = mount();
  click(rowsIn(root, 'tabs')[1].querySelector('.x'));
  assert.deepEqual(h.sent, [['sidebarTabClosed', ['t2']]]);
});

test('a pinned row closes through the same message', () => {
  const root = mount();
  click(rowsIn(root, 'pinned')[0].querySelector('.x'));
  assert.deepEqual(h.sent, [['sidebarTabClosed', ['p1']]]);
});

// --- option-click morphs the close button into snooze --------

const altClick = (el) =>
    el.dispatchEvent(new Event('click', {bubbles: true, altKey: true}));

// menu-dismiss.js reports every open and close so the window knows whether to
// push a dismiss. Assertions about what a CONTROL did read past those.
const actions = (sent) => sent.filter(([name]) => !name.startsWith('sidebarMenu'));

test('option-click on a regular row\'s close button opens the snooze menu instead of closing it', () => {
  const root = mount();
  altClick(rowsIn(root, 'tabs')[0].querySelector('.x'));
  assert.deepEqual(actions(h.sent), [], 'nothing closes and nothing sends yet');
  assert.notEqual(root.querySelector('.snoozemenu'), null);
});

test('picking a duration after an option-click sends the row index, a wake time and the alert flag', () => {
  const root = mount();
  altClick(rowsIn(root, 'tabs')[1].querySelector('.x'));
  root.querySelector('.snoozemenu').querySelectorAll('button.ctxitem')[0].click();
  assert.equal(actions(h.sent).length, 1);
  const [name, args] = actions(h.sent)[0];
  assert.equal(name, 'sidebarTabSnoozed');
  assert.equal(args[0], 1);
  assert.equal(typeof args[1], 'number');
  assert.equal(args[2], false);
});

test('option-click on a pinned row still just closes it - snooze has no index there', () => {
  const root = mount();
  altClick(rowsIn(root, 'pinned')[0].querySelector('.x'));
  assert.deepEqual(h.sent, [['sidebarTabClosed', ['p1']]]);
});

// --- the rest of the controls -----------------------------------------------

test('the new tab button sends new tab with no arguments', () => {
  const root = mount();
  click(root.querySelector('.newtab'));
  assert.deepEqual(h.sent, [['sidebarNewTab', []]]);
});

// There is no New Folder button - it would duplicate a context-menu entry
// sending the same message, and a coordinate that measured blank sat inside its
// empty interior, so two VM runs created folders instead of clicking a row. The
// menu route keeps its own test in sidebar-space-menu-test, so nothing here is
// left uncovered.

test('a favourite tile whose tab is next door sends that id and rings nothing', () => {
  // The losing window draws the tile dim, and a click on it moves the tab
  // here rather than loading a second copy - so no ring, the way a live tile
  // rings nothing.
  const root = mount({
    ...SNAPSHOT,
    favorites: [{
      id: 'https://away.example/',
      title: 'Away',
      elsewhere: true,
      elsewhereTabId: '77',
      selected: true,
    }],
  });
  const tile = root.querySelector('.fav');
  assert.ok(tile.classList.contains('elsewhere'));
  assert.ok(tile.classList.contains('sel'), 'the frozen window keeps the ring');
  click(tile);
  assert.deepEqual(h.sent, [['sidebarFavoriteClicked', ['https://away.example/', '77']]]);
  assert.equal(tile.classList.contains('loading'), false);
});

test('clicking a favourite tile opens the favourite', () => {
  // A tile carries a URL as its id. It must not fall into the cold-row route,
  // which would reopen it as a tab rather than going through OpenFavoriteURL.
  const root = mount();
  click(root.querySelector('.fav'));
  assert.deepEqual(h.sent,
                   [['sidebarFavoriteClicked', ['https://fav.example/', '']]]);
});

test('a favourite with a live tab sends it, so the click activates that tab', () => {
  // Matching the URL again at click time is what made a favourite spawn a
  // second tab: the page it lands on after a redirect is not the URL stored on
  // the tile, so the lookup missed and opened another one.
  const root = mount(
      {...SNAPSHOT, favorites: [{id: 'https://fav.example/', tabId: '42'}]});
  click(root.querySelector('.fav'));
  assert.deepEqual(h.sent,
                   [['sidebarFavoriteClicked', ['https://fav.example/', '42']]]);
});

test('clicking the sidebar background sends nothing', () => {
  // Not .wsname any more - the default fixture pins tabs, and that row is
  // now the pinned fold's own header, so a click on it is a click on a
  // control, not on the background.
  const root = mount();
  click(root.querySelector('.sidebar'));
  assert.deepEqual(h.sent, []);
});

test('the favourites grid is the drop target that promotes a tab', () => {
  // The section name is the whole handle installRowDrag has on the
  // grid, and losing it breaks nothing you can see: the tiles still render and
  // still click, the drop just quietly resolves to no target.
  const root = mount();
  assert.equal(root.querySelector('.favgrid').dataset.dropzone, 'favorites');
});

test('an empty favourites grid is a drop target too', () => {
  // The state where it matters most is the one with nothing to render: without
  // it the FIRST favourite could never be made by dragging at all.
  const root = mount({...SNAPSHOT, favorites: []});
  const grid = root.querySelector('.favgrid');
  assert.equal(grid.dataset.dropzone, 'favorites');
  assert.equal(grid.classList.contains('empty'), true);
});

test('tab, pinned, favourite and grouped rows can all be picked up', () => {
  // A grouped row is draggable now - the ungroup gesture reads it via its own
  // folder:<id> section, addressed by its member index.
  const root = mount();
  assert.equal(rowsIn(root, 'tabs')[0].draggable, true, 'a tab row');
  assert.equal(rowsIn(root, 'pinned')[0].draggable, true, 'a pinned row');
  assert.equal(root.querySelector('.fav').draggable, true, 'a favourite tile');
  assert.equal(rowsIn(root, 'folder:G1')[0].draggable, true, 'a grouped row');
});

// --- a folder head reorders among folders -------------------------

test('a folder head can be picked up', () => {
  const root = mount();
  assert.equal(
      root.querySelector('.frow[data-folder-id="G1"]').draggable, true);
});

test('the pinned section header is not draggable the way a folder head is', () => {
  // PINNED_FOLD_ID reuses the .frow class for its chevron, but it is a fixed
  // anchor - dragging it around the folders list means nothing.
  const root = mount();
  assert.equal(
      !!root.querySelector(`[data-folder-id="${PINNED_FOLD_ID}"]`).draggable,
      false);
});

test('folder heads share one dropzone, so they can reorder among each other', () => {
  const root = mount({...SNAPSHOT, folders: [...SNAPSHOT.folders,
                                              {id: 'G2', name: 'Later', tabs: []}]});
  const list = root.querySelector('[data-dropzone="folders"]');
  assert.ok(list, 'a shared folders zone');
  assert.equal(list.contains(root.querySelector('.frow[data-folder-id="G1"]')),
               true);
  assert.equal(list.contains(root.querySelector('.frow[data-folder-id="G2"]')),
               true);
  assert.equal(
      list.contains(root.querySelector(`[data-folder-id="${PINNED_FOLD_ID}"]`)),
      false, 'the pinned header is not a folder to reorder');
});

test('dropping a folder head on another folder reorders them', () => {
  const root = mount({...SNAPSHOT, folders: [...SNAPSHOT.folders,
                                              {id: 'G2', name: 'Later', tabs: []}]});
  const g1 = root.querySelector('.frow[data-folder-id="G1"]');
  const g2 = root.querySelector('.frow[data-folder-id="G2"]');

  dragTo(g1, g2);

  assert.deepEqual(h.sent, [['sidebarFolderReordered', [0, 1]]]);
});

// --- folds ------------------------------------------------------------------

test('the pinned fold reports the state it moved to', () => {
  // C++ persists the state explicitly rather than toggling, so a
  // double click cannot leave the surface and the model on opposite values.
  const root = mount();
  const header = root.querySelector(`[data-folder-id="${PINNED_FOLD_ID}"]`);
  click(header);
  assert.deepEqual(h.sent, [['sidebarPinnedToggled', [true]]]);
  h.sent.length = 0;
  click(header);
  assert.deepEqual(h.sent, [['sidebarPinnedToggled', [false]]]);
});

test('the pinned fold state is a boolean', () => {
  // SetPinnedCollapsed is reached only through args[0].is_bool(); anything else
  // is dropped without a word.
  const root = mount();
  click(root.querySelector(`[data-folder-id="${PINNED_FOLD_ID}"]`));
  assert.equal(typeof h.sent[0][1][0], 'boolean');
});

test('the pinned section is not reported as a tab group', () => {
  // PINNED_FOLD_ID is a sentinel, not a group id. Sent as one it would ask
  // SetTabGroupCollapsed for a group called __pinned__, and the real pinned
  // state would never move.
  const root = mount();
  click(root.querySelector(`[data-folder-id="${PINNED_FOLD_ID}"]`));
  assert.equal(h.sent.some((m) => m[0] === 'sidebarFolderToggled'), false);
});

test('a folder toggle names the folder and the state it moved to', () => {
  const root = mount();
  click(root.querySelector('.frow[data-folder-id="G1"]'));
  assert.deepEqual(h.sent, [['sidebarFolderToggled', ['G1', true]]]);
});

test('a folder toggle is not reported as the pinned section', () => {
  const root = mount();
  click(root.querySelector('.frow[data-folder-id="G1"]'));
  assert.equal(h.sent.some((m) => m[0] === 'sidebarPinnedToggled'), false);
});

test('a fold reports the state the DOM moved to, not a remembered one', () => {
  // Reopening it must report false. Reading the intent map instead of the live
  // fold would report the folder's model state for one the user has never
  // touched, and the two disagree exactly once - on the first click.
  const root = mount();
  const header = root.querySelector('.frow[data-folder-id="G1"]');
  click(header);
  h.sent.length = 0;
  click(header);
  assert.deepEqual(h.sent, [['sidebarFolderToggled', ['G1', false]]]);
});

test('a fold that arrives closed renders closed', () => {
  // Both halves: the body carries the collapse, the header carries the chevron.
  const root = mount(COLLAPSED_SNAPSHOT);
  assert.equal(
      root.querySelector('.fold[data-folder-body="G1"]').classList.contains(
          'closed'),
      true, 'the folder body');
  assert.equal(
      root.querySelector('.frow[data-folder-id="G1"]').classList.contains(
          'closedH'),
      true, 'the folder header');
  assert.equal(
      root.querySelector(`.fold[data-folder-body="${PINNED_FOLD_ID}"]`)
          .classList.contains('closed'),
      true, 'the pinned body');
});

test('clicking a folder that arrived closed reports that it opened', () => {
  // The click reads the state off the live DOM, so a body rendered without the
  // collapse reads as open and the first click reports CLOSED - persisting the
  // fold shut at the exact moment the user asked for it open, and the section
  // they just opened snaps back on the next push.
  const root = mount(COLLAPSED_SNAPSHOT);
  click(root.querySelector('.frow[data-folder-id="G1"]'));
  assert.deepEqual(h.sent, [['sidebarFolderToggled', ['G1', false]]]);
});

test('clicking a pinned section that arrived closed reports that it opened', () => {
  const root = mount(COLLAPSED_SNAPSHOT);
  click(root.querySelector(`[data-folder-id="${PINNED_FOLD_ID}"]`));
  assert.deepEqual(h.sent, [['sidebarPinnedToggled', [false]]]);
});

// --- a drop into a folded section ---------------------------------

const dragTo = (source, target) => {
  source.dispatchEvent(new Event('dragstart', {bubbles: true}));
  target.dispatchEvent(new Event('drop', {bubbles: true}));
};

test('a drop on a closed folder header opens it and files the tab', () => {
  const root = mount(COLLAPSED_SNAPSHOT);
  const header = root.querySelector('.frow[data-folder-id="G1"]');
  dragTo(rowsIn(root, 'tabs')[0], header);

  assert.equal(
      root.querySelector('.fold[data-folder-body="G1"]').classList.contains(
          'closed'),
      false, 'the body opened');
  assert.equal(header.classList.contains('closedH'), false, 'the header opened');
  assert.deepEqual(
      h.sent,
      [['sidebarFolderToggled', ['G1', false]],
       ['sidebarTabGrouped', [0, 'G1', 'tabs', -1]]]);
});

test('a drop on an already-open folder header sends no spurious toggle', () => {
  const root = mount();
  dragTo(rowsIn(root, 'tabs')[0], root.querySelector('.frow[data-folder-id="G1"]'));

  assert.equal(h.sent.some((m) => m[0] === 'sidebarFolderToggled'), false);
  assert.deepEqual(h.sent, [['sidebarTabGrouped', [0, 'G1', 'tabs', -1]]]);
});

test('a drop on an empty folder\'s head files the tab into it', () => {
  // The body of an empty folder has no height and no hint to aim at, so the
  // head is the only target.
  const root = mount({...SNAPSHOT, folders: [...SNAPSHOT.folders,
                                              {id: 'G2', name: 'Empty', tabs: []}]});
  assert.equal(root.querySelector('.fold[data-folder-body="G2"] .foldhint'),
               null, 'no hint rendered');
  dragTo(rowsIn(root, 'tabs')[0],
         root.querySelector('.frow[data-folder-id="G2"]'));

  assert.deepEqual(h.sent, [['sidebarTabGrouped', [0, 'G2', 'tabs', -1]]]);
});

test('a drop on a row inside a folder files the tab into that folder', () => {
  const root = mount();
  dragTo(rowsIn(root, 'tabs')[0], rowsIn(root, 'folder:G1')[0]);

  assert.deepEqual(h.sent, [['sidebarTabGrouped', [0, 'G1', 'tabs', -1]]]);
});

// A folder among the pinned tabs is one item of that list, so every slot sent
// for the pinned section counts it as one.
const PINNED_FOLDER_SNAPSHOT = {
  ...SNAPSHOT,
  pinned: [
    {kind: 'tab', id: 'p1', title: 'Pin One', url: 'https://pin1.example/'},
    {kind: 'folder', id: 'PF', name: 'Work', tabs: [
      {id: 'w1', title: 'Work A', url: 'https://wa.example/'},
      {id: 'w2', title: 'Work B', url: 'https://wb.example/'},
    ]},
    {kind: 'tab', id: 'p2', title: 'Pin Two', url: 'https://pin2.example/'},
  ],
};

// The pinned zone's own rows: a folder head counts once, the rows in its body
// do not. The shim has no layout, so a drop on any of them lands at the END
// slot, which is 3 here.
const pinnedItems = (root) =>
    [...root.querySelector('.foldin[data-dropzone="pinned"]').children].filter(
        (el) => el.matches('.tabrow, .frow'));

test('a pinned folder is one draggable item among the pinned rows', () => {
  const root = mount(PINNED_FOLDER_SNAPSHOT);
  assert.deepEqual(pinnedItems(root).map((el) => el.className.includes('frow')),
                   [false, true, false]);
  assert.equal(root.querySelector('.frow[data-folder-id="PF"]').draggable, true);
  assert.equal(rowsIn(root, 'folder:PF')[0].draggable, true,
               'a row inside the pinned folder');
});

test('a pinned row dropped on the pinned folder head joins it', () => {
  const root = mount(PINNED_FOLDER_SNAPSHOT);
  dragTo(pinnedItems(root)[0], root.querySelector('.frow[data-folder-id="PF"]'));

  assert.deepEqual(h.sent, [['sidebarTabGrouped', [0, 'PF', 'pinned', -1]]]);
});

test('a row dragged out of the pinned folder names its member and item slot', () => {
  const root = mount(PINNED_FOLDER_SNAPSHOT);
  dragTo(rowsIn(root, 'folder:PF')[1], pinnedItems(root)[2]);

  assert.deepEqual(h.sent, [['sidebarTabUngrouped', ['PF', 1, 'pinned', 3]]]);
});

test('a regular folder head dropped among the pinned rows pins the folder', () => {
  const root = mount(PINNED_FOLDER_SNAPSHOT);
  dragTo(root.querySelector('.frow[data-folder-id="G1"]'), pinnedItems(root)[2]);

  assert.deepEqual(h.sent, [['sidebarFolderPinned', ['G1', 3]]]);
});

test('a row dropped on a sibling inside its own folder reorders the members', () => {
  const root = mount(PINNED_FOLDER_SNAPSHOT);
  const members = rowsIn(root, 'folder:PF');
  // No layout in the shim, so the drop lands at the end slot, 2 for two rows.
  dragTo(members[0], members[1]);

  assert.deepEqual(h.sent, [['sidebarFolderMemberReordered', ['PF', 0, 1]]]);
});

// --- the hover carried across a rebuild --------------------------------------
// Chromium gives a head rebuilt under a still pointer :hover only on its next
// hover update, so the glyph painted for a frame where the chevron was.

const RENAMED = {
  ...SNAPSHOT,
  folders: [{...SNAPSHOT.folders[0], name: 'Reading list'}],
};

test('a rebuild keeps the hover on the folder head under the pointer', () => {
  const root = mount();
  const before = root.querySelector('.frow[data-folder-id="G1"]');
  before.hovered = true;
  h.window.atriumSetSidebar(RENAMED);

  const after = root.querySelector('.frow[data-folder-id="G1"]');
  assert.notEqual(after, before, 'the rail was rebuilt');
  assert.ok(after.classList.contains('hoverhold'));
  assert.equal(root.querySelectorAll('.hoverhold').length, 1);
});

test('a rebuild keeps the hover on the workspace row too', () => {
  const root = mount();
  root.querySelector(`.wsname[data-folder-id="${PINNED_FOLD_ID}"]`).hovered =
      true;
  h.window.atriumSetSidebar(RENAMED);

  assert.ok(root.querySelector(`.wsname[data-folder-id="${PINNED_FOLD_ID}"]`)
                .classList.contains('hoverhold'));
});

test('a second push before the pointer moves keeps the carried hover', () => {
  const root = mount();
  root.querySelector('.frow[data-folder-id="G1"]').hovered = true;
  h.window.atriumSetSidebar(RENAMED);
  h.window.atriumSetSidebar(SNAPSHOT);

  assert.ok(root.querySelector('.frow[data-folder-id="G1"]')
                .classList.contains('hoverhold'));
});

// favicons arrive on their own, so a list under a still pointer is
// rebuilt without the user doing anything. The row's fill and its close button
// flickered off and back for a frame each time.
test('a rebuild keeps the hover on the tab row under the pointer', () => {
  const root = mount();
  const before = root.querySelector('.row.tabrow[data-tab-id="t1"]');
  before.hovered = true;
  h.window.atriumSetSidebar(RENAMED);

  const after = root.querySelector('.row.tabrow[data-tab-id="t1"]');
  assert.notEqual(after, before, 'the list was rebuilt');
  assert.ok(after.classList.contains('hoverhold'));
  assert.equal(root.querySelectorAll('.hoverhold').length, 1);
});

test('a second push before the pointer moves keeps the tab row hover', () => {
  const root = mount();
  root.querySelector('.row.tabrow[data-tab-id="t1"]').hovered = true;
  h.window.atriumSetSidebar(RENAMED);
  h.window.atriumSetSidebar(SNAPSHOT);

  assert.ok(root.querySelector('.row.tabrow[data-tab-id="t1"]')
                .classList.contains('hoverhold'));
});

test('the first pointer move releases a carried tab row hover', () => {
  const root = mount();
  root.querySelector('.row.tabrow[data-tab-id="t1"]').hovered = true;
  h.window.atriumSetSidebar(RENAMED);
  root.querySelector('.row.tabrow[data-tab-id="t1"]')
      .dispatchEvent(new Event('pointermove', {bubbles: true}));

  assert.equal(root.querySelector('.hoverhold'), null);
});

// A cold row carries no tab id, so there is nothing to find it again by. It
// must not hand the hold to a different row that happens to share the empty
// key - the first tabrow in the list would otherwise take it.
test('a rebuild carries nothing for a hovered row with no tab id', () => {
  const root = mount();
  const rows = [...root.querySelectorAll('.row.tabrow')];
  const cold = rows.find((r) => !r.dataset.tabId);
  assert.ok(cold, 'the fixture has a cold row');
  cold.hovered = true;
  h.window.atriumSetSidebar(RENAMED);

  assert.equal(root.querySelector('.hoverhold'), null);
});

test('a rebuild with no head under the pointer carries nothing', () => {
  const root = mount();
  h.window.atriumSetSidebar(RENAMED);
  assert.equal(root.querySelector('.hoverhold'), null);
});

test('the first pointer move hands the hover back to the browser', () => {
  const root = mount();
  root.querySelector('.frow[data-folder-id="G1"]').hovered = true;
  h.window.atriumSetSidebar(RENAMED);
  root.querySelector('.frow[data-folder-id="G1"]')
      .dispatchEvent(new Event('pointermove', {bubbles: true}));

  assert.equal(root.querySelector('.hoverhold'), null);
});

test('the pointer leaving the sidebar releases the carried hover', () => {
  const root = mount();
  root.querySelector('.frow[data-folder-id="G1"]').hovered = true;
  h.window.atriumSetSidebar(RENAMED);
  root.dispatchEvent(new Event('pointerleave', {bubbles: false}));

  assert.equal(root.querySelector('.hoverhold'), null);
});

test('a drop on the closed pinned header opens it and pins the tab', () => {
  const root = mount(COLLAPSED_SNAPSHOT);
  dragTo(rowsIn(root, 'tabs')[0],
         root.querySelector(`[data-folder-id="${PINNED_FOLD_ID}"]`));

  assert.equal(
      root.querySelector(`.fold[data-folder-body="${PINNED_FOLD_ID}"]`)
          .classList.contains('closed'),
      false, 'the pinned body opened');
  assert.deepEqual(
      h.sent, [['sidebarPinnedToggled', [false]], ['sidebarTabPinned', [0, 0]]]);
});

// --- the space dots ---------------------------------------------------------

// dot[0] is the synthetic Overview dot, so real space 0
// (Work) is dot[1] and real space 1 (Play) is dot[2].

test('clicking the leftmost dot slides to Overview and selects no space', () => {
  // Sending nothing is the point: Overview is not a space, and
  // announcing one would switch the browser behind the user.
  const root = mount(RAIL_SNAPSHOT);
  click(root.querySelectorAll('.spacedots .sdot')[0]);
  assert.deepEqual(h.sent, []);
});

test('a second Overview press returns to the space it came from', () => {
  // RAIL_SNAPSHOT is active on space 0, so the return has
  // to name 0 - landing on the first space by luck would pass a weaker check.
  const root = mount(RAIL_SNAPSHOT);
  click(root.querySelectorAll('.spacedots .sdot')[2]);
  runFrames();
  h.sent.length = 0;

  const ovdot = () => root.querySelector('.spacedots .sdot.ovdot');
  // Filtered: a rebuild also reports sidebarFavoriteCount, which says nothing
  // about which space is showing.
  const selected = () => h.sent.filter(([n]) => n === 'sidebarSpaceSelected');
  click(ovdot());
  runFrames();
  assert.deepEqual(selected(), [], 'Overview is not a space, so none is selected');

  click(ovdot());
  runFrames();
  assert.deepEqual(selected(), [['sidebarSpaceSelected', [1]]],
                   'the return names the space the rail left, not space 0');
});

test('entering and leaving the Overview pane reports the parked state', () => {
  // The browser owns the sidebar's width, and the Overview pane needs more of
  // it than a space pane does. Nothing else tells the browser the rail left.
  const root = mount(RAIL_SNAPSHOT);
  const parked = () => h.sent.filter(([n]) => n === 'sidebarOverviewPaneActive');

  click(root.querySelector('.spacedots .sdot.ovdot'));
  runFrames();
  assert.deepEqual(parked(), [['sidebarOverviewPaneActive', [true]]]);

  h.sent.length = 0;
  click(root.querySelectorAll('.spacedots .sdot')[1]);
  runFrames();
  assert.deepEqual(parked(), [['sidebarOverviewPaneActive', [false]]],
                   'picking a space did not report the rail leaving Overview');
});

test('leaving the Overview reports on the first frame of movement, not at rest', () => {
  // The narrow is what the user waits for on the way back. Reported at the
  // commit it ran after the whole crossing, as the last thing on screen.
  const root = mount(RAIL_SNAPSHOT);
  const parked = () => h.sent.filter(([n]) => n === 'sidebarOverviewPaneActive');
  click(root.querySelector('.spacedots .sdot.ovdot'));
  runFrames();
  h.sent.length = 0;

  // A finger, not a dot: a jump decides its target on frame one, a drag
  // decides nothing until the lift. Positive deltaX moves toward the spaces.
  drag(root, 14, 6);
  assert.ok(h.pendingFrames, 'the gesture is still live');
  assert.deepEqual(parked(), [['sidebarOverviewPaneActive', [false]]],
                   'six frames of movement off the Overview did not narrow');
  runFrames();
  assert.deepEqual(parked(), [['sidebarOverviewPaneActive', [false]]],
                   'the settle reported it a second time');
});

test('a drag that turns back onto the Overview widens it again', () => {
  const root = mount(RAIL_SNAPSHOT);
  const parked = () => h.sent.filter(([n]) => n === 'sidebarOverviewPaneActive');
  click(root.querySelector('.spacedots .sdot.ovdot'));
  runFrames();
  h.sent.length = 0;

  drag(root, 14, 3);
  drag(root, -14, 3);
  runFrames();
  assert.deepEqual(parked(), [['sidebarOverviewPaneActive', [false]],
                              ['sidebarOverviewPaneActive', [true]]]);
});

test('a push while parked on Overview does not report the pane again', () => {
  // A title change re-pushes the snapshot. Reporting per push would restart the
  // width animation on every one of them.
  const root = mount(RAIL_SNAPSHOT);
  click(root.querySelector('.spacedots .sdot.ovdot'));
  runFrames();
  h.sent.length = 0;

  h.window.atriumSetSidebar(RAIL_SNAPSHOT);
  runFrames();
  assert.deepEqual(h.sent.filter(([n]) => n === 'sidebarOverviewPaneActive'), []);
});

test('a hurried second Overview press still returns to the space it came from', () => {
  // The same return, taken before the first press has landed. Read off
  // spaceIndex, which only moves on arrival, the toggle saw the space being
  // LEFT and asked for Overview again, so the space never came back.
  const root = mount(RAIL_SNAPSHOT);
  click(root.querySelectorAll('.spacedots .sdot')[2]);
  runFrames();
  h.sent.length = 0;

  const ovdot = () => root.querySelector('.spacedots .sdot.ovdot');
  click(ovdot());
  h.tick();
  h.tick();
  click(ovdot());
  runFrames();
  assert.equal(ovdot().classList.contains('active'), false,
               'the rail did not stay parked on Overview');
  assert.equal(root.querySelectorAll('.spacedots .sdot')[2].classList
                   .contains('active'),
               true, 'it came back to the space it left, not to space 0');
  // The burst holds both presses and the rail lands on the space the browser
  // is already on, so nothing switches.
  assert.deepEqual(h.sent.filter(([n]) => n === 'sidebarSpaceSelected'), []);
});

test('a dot press announces nothing until the slide lands', () => {
  // A dot press and a swipe report a switch the same way: the controller
  // announces when the rail arrives. Announcing on press as well would switch
  // the browser twice for one gesture.
  const root = mount(RAIL_SNAPSHOT);
  click(root.querySelectorAll('.spacedots .sdot')[2]);
  assert.deepEqual(h.sent, []);
});

test('a dot press switches to the space it names', () => {
  const root = mount(RAIL_SNAPSHOT);
  click(root.querySelectorAll('.spacedots .sdot')[2]);
  runFrames();
  assert.deepEqual(h.sent, [['sidebarSpaceSelected', [1]]]);
});

test('the announced space is an integer', () => {
  // SwitchToSpaceIndex is reached only through args[0].is_int(); a "1" read off
  // the dataset would be dropped in silence.
  const root = mount(RAIL_SNAPSHOT);
  click(root.querySelectorAll('.spacedots .sdot')[2]);
  runFrames();
  assert.equal(Number.isInteger(h.sent[0][1][0]), true);
});

test('pressing the space already showing announces nothing', () => {
  const root = mount(RAIL_SNAPSHOT);
  click(root.querySelectorAll('.spacedots .sdot')[1]);
  runFrames();
  assert.deepEqual(h.sent, []);
});

test('the rail records which space the rows came from', () => {
  // Not the same as the pane a row is DRAWN in: a cross-space drag resolves its
  // source against this, and paging has already moved the browser's active
  // space by the time the drop lands. A stuck 0 moves the wrong tab
  // out of the wrong space, and nothing on the surface looks any different.
  const root = mount({...RAIL_SNAPSHOT, activeSpace: 1});
  assert.equal(root.querySelector('.rail').dataset.activeSpace, '1');
});

// Within one profile every pane holds the same tiles, so a per-pane copy is
// pure duplication that CSS hides but a count or a selector still sees.
const ONE_PROFILE_SNAPSHOT = {
  ...SNAPSHOT,
  activeSpace: 0,
  favorites: [{id: 'https://a.test/'}, {id: 'https://b.test/'},
              {id: 'https://c.test/'}],
  spaces: [{
    name: 'Favs', color: '#48c', profileId: 'Default',
    favorites: [{id: 'https://a.test/'}, {id: 'https://b.test/'},
                {id: 'https://c.test/'}],
  }],
};

test('a three-favourite snapshot draws three tiles, not one set per pane', () => {
  const root = mount(ONE_PROFILE_SNAPSHOT);
  assert.equal(root.querySelectorAll('.fav').length, 3);
});

test('a re-pushed snapshot still draws three tiles', () => {
  const root = mount(ONE_PROFILE_SNAPSHOT);
  h.window.atriumSetSidebar(ONE_PROFILE_SNAPSHOT);
  assert.equal(root.querySelectorAll('.fav').length, 3);
});

// The count C++ resolves a favourite chord's slot against.
test('the reported favourite count is the number of tiles shown', () => {
  const root = fresh();
  h.window.atriumSetSidebar(ONE_PROFILE_SNAPSHOT);
  const counts = h.sent.filter(([name]) => name === 'sidebarFavoriteCount');
  assert.deepEqual(counts.at(-1), ['sidebarFavoriteCount', [3]]);
  assert.equal(root.querySelectorAll('.fav').length, 3);
});

// A foreign profile's pane carries its own tiles, and those are not slots the
// active space's chords can open.
test('a foreign profile\'s tiles are left out of the reported count', () => {
  fresh();
  h.window.atriumSetSidebar({
    ...ONE_PROFILE_SNAPSHOT,
    spaces: [
      ONE_PROFILE_SNAPSHOT.spaces[0],
      {name: 'Work', profileId: 'Work', favorites: [{id: 'https://w.test/'}]},
    ],
  });
  const counts = h.sent.filter(([name]) => name === 'sidebarFavoriteCount');
  assert.deepEqual(counts.at(-1), ['sidebarFavoriteCount', [3]]);
});

// The extension strip rides with the pinned grid, so the rebuild that follows a
// cross-profile switch has to leave the profile the rail LEFT with a strip of
// its own to swipe back in with.
const pin = (id) => [{id, name: id, actionTitle: id, pinned: true}];
const TWO_PROFILE_SNAPSHOT = {
  ...ONE_PROFILE_SNAPSHOT,
  spaces: [ONE_PROFILE_SNAPSHOT.spaces[0],
           {name: 'Work', profileId: 'Work', favorites: [{id: 'https://w.test/'}]}],
};

test('a rebuild after a profile switch leaves the other profile its pins', () => {
  const root = fresh();
  h.window.atriumSetSidebar({...TWO_PROFILE_SNAPSHOT, activeSpace: 0});
  h.window.atriumSetSidebarExtensions(pin('work-ext'), 'Work');
  h.window.atriumSetSidebarExtensions(pin('home-ext'), 'Default');
  h.window.atriumSetSidebar({...TWO_PROFILE_SNAPSHOT, activeSpace: 1});

  const copies = [...root.querySelectorAll('.railpane')]
      .filter((p) => p.querySelector('.exs-row .exs-chip'));
  assert.deepEqual(copies.map((p) => p.dataset.profileId), ['Default'],
      'the pane of the profile the rail left has nothing to slide back in');
  assert.deepEqual(
      [...copies[0].querySelectorAll('.exs-chip')].map(
          (c) => c.dataset.extensionId),
      ['home-ext']);
});

// pinnedFavGrid runs before buildRail sets the icon scope for a
// rail push. Only the pinned copy is drawn within one profile, and it is a
// direct child of root.
test('a favourite gets its icon on the very first rail push', () => {
  setIconScope('');
  const root = mount({...RAIL_SNAPSHOT, iconScope: 'coldscope'});
  const pinned = [...root.children].find((el) => el.classList.contains('favgrid'));
  const img = pinned && pinned.querySelector('.fav .fvimg');
  assert.ok(img, 'the cold push left the pinned tile with no icon at all');
  assert.equal(
      img.src,
      '/urlicon/coldscope/' +
          encodeURIComponent(RAIL_SNAPSHOT.favorites[0].id) + '.png');
});

// the browser answers a space switch by asking the page to glide,
// and it names the space. The rail counts PANES, and Overview holds pane 0 -
// every other caller in this file writes `index + OVERVIEW_PANES`. Passing the
// space straight through sends the rail one space BACK, which is the reported
// "it swipes, then returns on its own": the swipe commits to a pane, reports
// its space, and the answer glides to the pane below it.
test('the glide names a space, and lands on that space pane', () => {
  const root = mount(RAIL_SNAPSHOT);
  assert.equal(h.window.atriumSwipeTo(1), 'swiping to 2',
               'space 1 is pane 2 - Overview is pane 0');
  assert.equal(root.querySelector('.rail').dataset.activeSpace, '0',
               'the model has not moved yet, only the rail is travelling');
});

// A push during the browser's own glide used to park until the rail settled,
// so a routed tab's row appeared ~0.9s after the tab did.
test('a push during a glide rebuilds at once and the glide carries on', () => {
  const root = mount(RAIL_SNAPSHOT);
  h.window.atriumSwipeTo(1);
  for (let i = 0; i < 6; i++) {
    h.tick();
  }
  const before = root.querySelector('.rail').style.transform;
  assert.notEqual(before, 'translateX(0.00px)', 'the glide has started');
  h.window.atriumSetSidebar({
    ...RAIL_SNAPSHOT,
    activeSpace: 1,
    tabs: [{id: '99', title: 'Routed here', url: 'https://routed.example/'}],
  });
  assert.ok(root.querySelector('.tabrow[data-tab-id="99"]'),
            'the push was parked behind the glide');
  const after = root.querySelector('.rail').style.transform;
  assert.notEqual(after, 'translateX(0.00px)', 'the rebuild landed the rail at rest');
  assert.equal(h.pendingFrames > 0, true, 'the glide loop stopped');
  runFrames();
  assert.equal(root.querySelector('.rail').style.transform,
               'translateX(-520.00px)', 'the glide did not reach pane 2');
});

test('a push during a trackpad gesture still waits', () => {
  const root = mount(RAIL_SNAPSHOT);
  for (let i = 0; i < 4; i++) {
    root.dispatchEvent(new Event('wheel', {bubbles: true, deltaX: 14, deltaY: 0}));
    h.tick();
  }
  h.window.atriumSetSidebar({
    ...RAIL_SNAPSHOT,
    tabs: [{id: '98', title: 'Mid gesture', url: 'https://gesture.example/'}],
  });
  assert.equal(root.querySelector('.tabrow[data-tab-id="98"]'), null,
               'a gesture push rebuilt the nodes under the finger');
});

// The same off-by-one at the other end, and the worse half: asked for space 0
// it would park on Overview, which is not a space at all.
test('a glide to the first space does not land on Overview', () => {
  mount({...RAIL_SNAPSHOT, activeSpace: 1});
  assert.equal(h.window.atriumSwipeTo(0), 'swiping to 1');
});

// The x on a pinned tile with nothing loaded removes
// the tile. sidebarTabClosed carries a tab id, and a cold row has none, so that
// button was inert.
test('the x on a cold pinned row removes the pin by its index', () => {
  const root = mount(COLLAPSED_SNAPSHOT.pinnedCollapsed ? SNAPSHOT : SNAPSHOT);
  const pinnedRows = rowsIn(root, 'pinned');
  // SNAPSHOT's second pinned row is the cold one.
  click(pinnedRows[1].querySelector('.x'));
  assert.deepEqual(h.sent, [['sidebarPinnedTabRemoved', [1]]]);
});

// the same inert x on a cold REGULAR row. Five chrome://settings
// rows restored after a quit could be neither opened nor closed.
test('the x on a cold regular row removes it by its rendered index', () => {
  const root = mount();
  const rows = rowsIn(root, 'tabs');
  // SNAPSHOT's third regular row is the cold one.
  click(rows[2].querySelector('.x'));
  assert.deepEqual(h.sent, [['sidebarColdTabRemoved', [2]]]);
});

// A row live in ANOTHER window also has no tab id here. Its tab
// is open, so the cold remove must not take its row.
test('the x on a row live elsewhere is not a cold remove', () => {
  const root = mount();
  const row = rowsIn(root, 'tabs')[2];
  row.dataset.elsewhereTabId = 'w2-7';
  click(row.querySelector('.x'));
  assert.deepEqual(h.sent.filter(([n]) => n === 'sidebarColdTabRemoved'), []);
});

// a page that has not delivered a title yet, or a row saved before
// one arrived, drew a row with no text at all. The host stands in, as the tab
// strip's own title does.
test('a row with no title shows its host', () => {
  const root = mount({
    ...SNAPSHOT,
    tabs: [...SNAPSHOT.tabs, {id: 't9', title: '', url: 'https://untitled.example/page?x=1'}],
  });
  const rows = rowsIn(root, 'tabs');
  const labels = [...rows].map((r) => r.querySelector('.tl').textContent);
  assert.equal(labels[rows.length - 1], 'untitled.example');
  assert.equal(labels[0], 'First', 'a titled row keeps its title');
});

test('the x on a live regular row still closes it by id', () => {
  const root = mount();
  click(rowsIn(root, 'tabs')[0].querySelector('.x'));
  assert.deepEqual(h.sent, [['sidebarTabClosed', ['t1']]]);
});

test('the x on a loaded pinned row still closes the tab, keeping the pin', () => {
  const root = mount();
  click(rowsIn(root, 'pinned')[0].querySelector('.x'));
  assert.deepEqual(h.sent, [['sidebarTabClosed', ['p1']]]);
});

// The pinned folder counts as one item ahead of the loose pin behind it - a
// `.tabrow`-only index would land this on the folder instead (root cause of
// the x, Unpin, Add to Dock, Rename and Move to all missing their row).
test('the x on a cold pinned row after a folder removes it by its item index', () => {
  const root = mount({
    ...PINNED_FOLDER_SNAPSHOT,
    pinned: [...PINNED_FOLDER_SNAPSHOT.pinned.slice(0, 2),
             {kind: 'tab', id: '', title: 'Pin Two', url: 'https://pin2.example/'}],
  });
  const items = pinnedItems(root);
  assert.equal(items.length, 3, 'p1, the PF folder head, and the cold pin');
  click(items[2].querySelector('.x'));
  assert.deepEqual(h.sent, [['sidebarPinnedTabRemoved', [2]]]);
});

test('double-click renaming a pinned row after a folder sends its item index', () => {
  const root = mount(PINNED_FOLDER_SNAPSHOT);
  const row = pinnedItems(root)[2];
  row.dispatchEvent(new Event('dblclick', {bubbles: true}));
  const input = row.querySelector('input');
  assert.ok(input, 'no rename field opened');
  input.value = 'Renamed';
  input.dispatchEvent(new Event('keydown', {bubbles: true, key: 'Enter'}));
  assert.deepEqual(h.sent, [['sidebarTabRenamed', [2, 1, 'Renamed']]]);
});

// the lab's Clear control, in the divider above the ungrouped rows.
test('Clear sends sidebarTabsCleared', () => {
  const root = mount();
  click(root.querySelector('[data-sidebar-clear]'));
  assert.deepEqual(h.sent, [['sidebarTabsCleared', []]]);
});

test('the section header is a bare line with an icon, no TABS label', () => {
  const root = mount();
  const header = root.querySelector('.sechdr');
  assert.equal(header.textContent.trim(), '');
  const clear = header.querySelector('[data-sidebar-clear]');
  assert.ok(clear.querySelector('svg'), 'the control is an icon, not text');
  assert.equal(clear.getAttribute('aria-label'), 'Clear tabs');
});

// One line the whole row wide. The icon takes no width at rest and opens at
// the line's right end on hover, so the line gives up that end to it. On the
// root, not .sidebar: the favourites and address row are hoisted outside it.
test('the divider is one line that yields its right end to the icon on sidebar hover', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  assert.match(css, /\.sechdr::before \{\n  content: '';\n  flex: 1;/);
  assert.doesNotMatch(css, /\.sechdr[^\n]*::after/);
  assert.match(css, /\.sechdrclear \{[^}]*max-width: 0;[^}]*overflow: hidden;/);
  assert.match(css, /#atrium-root\.sbroot:hover \.sechdrclear \{\n  max-width: 14px;/);
  assert.doesNotMatch(css, /\.sidebar:hover \.sechdrclear/);
});

test('a space with no ungrouped rows offers no Clear', () => {
  // A control that does nothing is worse than an absent one - the header is
  // only rendered when there are tabs to clear.
  const root = mount({...SNAPSHOT, tabs: []});
  assert.equal(root.querySelector('[data-sidebar-clear]'), null);
});

// the dots bar carried click-to-jump and nothing else.
test('dropping one dot on another reorders the spaces', () => {
  const root = mount(RAIL_SNAPSHOT);
  const dots = [...root.querySelectorAll('.spacedots .sdot')]
                   .filter((d) => d.dataset.spaceIndex !== undefined);
  dots[0].dispatchEvent(new Event('dragstart', {bubbles: true}));
  dots[1].dispatchEvent(new Event('drop', {bubbles: true}));
  assert.deepEqual(h.sent, [['sidebarSpaceReordered', [0, 1]]]);
});

test('a dot dropped on itself sends nothing', () => {
  const root = mount(RAIL_SNAPSHOT);
  const dot = [...root.querySelectorAll('.spacedots .sdot')]
                  .find((d) => d.dataset.spaceIndex === '0');
  dot.dispatchEvent(new Event('dragstart', {bubbles: true}));
  dot.dispatchEvent(new Event('drop', {bubbles: true}));
  assert.deepEqual(h.sent, []);
});

// the dots are also a drop target for a dragged tab row.
test('dropping a dragged row on a dot moves that tab to the space', () => {
  const root = mount(RAIL_SNAPSHOT);
  const row = root.querySelector('[data-dropzone="tabs"] .tabrow');
  row.dispatchEvent(new Event('dragstart', {bubbles: true}));
  const target = [...root.querySelectorAll('.spacedots .sdot')]
                     .find((d) => d.dataset.spaceIndex === '1');
  target.dispatchEvent(new Event('drop', {bubbles: true}));
  assert.deepEqual(h.sent, [['sidebarTabMovedToSpace', [0, 0, 'tabs', 1, -1]]]);
});

test('a row dropped on the dot of its own space sends nothing', () => {
  const root = mount(RAIL_SNAPSHOT);
  const row = root.querySelector('[data-dropzone="tabs"] .tabrow');
  row.dispatchEvent(new Event('dragstart', {bubbles: true}));
  const target = [...root.querySelectorAll('.spacedots .sdot')]
                     .find((d) => d.dataset.spaceIndex === '0');
  target.dispatchEvent(new Event('drop', {bubbles: true}));
  assert.deepEqual(h.sent, []);
});

test('a favourite dropped on a dot sends no tab move', () => {
  const root = mount(RAIL_SNAPSHOT);
  const tile = root.querySelector('.fav');
  tile.dispatchEvent(new Event('dragstart', {bubbles: true}));
  const target = [...root.querySelectorAll('.spacedots .sdot')]
                     .find((d) => d.dataset.spaceIndex === '1');
  target.dispatchEvent(new Event('drop', {bubbles: true}));
  assert.equal(h.sent.some((m) => m[0] === 'sidebarTabMovedToSpace'), false);
});

test('a dot drop with no drag in flight sends nothing', () => {
  const root = mount(RAIL_SNAPSHOT);
  const target = [...root.querySelectorAll('.spacedots .sdot')]
                     .find((d) => d.dataset.spaceIndex === '1');
  target.dispatchEvent(new Event('drop', {bubbles: true}));
  assert.deepEqual(h.sent, []);
});

test('the "+" at the end of the bar creates a space', () => {
  const root = mount(RAIL_SNAPSHOT);
  click(root.querySelector('[data-sidebar-new-space]'));
  assert.deepEqual(h.sent, [['sidebarSpaceCreated', []]]);
});

test('the "+" is not a room, so it carries no space index to jump to', () => {
  // It is .sdot in the lab's markup too, and a jump resolved off position
  // rather than the index would switch to a space that does not exist.
  const root = mount(RAIL_SNAPSHOT);
  const add = root.querySelector('[data-sidebar-new-space]');
  assert.equal(add.dataset.spaceIndex, undefined);
});

test('a glow sits under the active dot and carries that space colour', () => {
  const root = mount(RAIL_SNAPSHOT);
  const bloom = root.querySelector('.dotbloom');
  assert.ok(bloom, 'no bloom rendered');
  // RAIL_SNAPSHOT names space 0, whose colour is #48c.
  assert.match(bloom.style.background, /#48c/);
});

test('one dot bar exists and it marks the active space', () => {
  // A press routes off dataset.spaceIndex, not the class, so every other dot
  // test still passes while the user cannot tell which space they are on.
  const root = mount(RAIL_SNAPSHOT);
  assert.equal(root.querySelectorAll('.spacedots').length, 1);
  // Only the rooms: the bar is led by the Overview dot and trailed by the "+"
  //, and both are .sdot in the lab's markup too.
  const marked = [...root.querySelectorAll('.spacedots .sdot')]
                     .filter((dot) => dot.dataset.spaceIndex !== undefined)
                     .map((dot) => dot.classList.contains('active'));
  // RAIL_SNAPSHOT names space 0, and one shared bar answers the model rather
  // than a pane.
  assert.deepEqual(marked, [true, false]);
});

test('one address row exists and no pane holds a copy', () => {
  // Per-pane copies were the whole defect: each pane owned one and
  // the rail translated them all, so the bar scrolled away with the workspace.
  const root = mount(RAIL_SNAPSHOT);
  assert.equal(root.querySelectorAll('.urlrow').length, 1);
  const row = root.querySelector('.urlrow');
  assert.equal(row.parentNode, root, 'the row is the rail\'s sibling, not a child of it');
  for (const pane of root.querySelectorAll('.railpane')) {
    assert.equal(pane.contains(row), false, 'a pane must not carry the row');
  }
});

// The flag pushed WITH a full space list. The browser sends none, so the
// length alone would pass this - and that is the guard which stops holding
// the day a producer sends spaces alongside the flag.
test('an incognito snapshot never rails, whatever spaces it carries', () => {
  const root = mount({...RAIL_SNAPSHOT, incognito: true});
  assert.equal(root.querySelector('.rail'), null);
  assert.equal(root.querySelector('.spacedots'), null);
  assert.equal(root.querySelector('.wsname'), null);
  assert.equal(root.querySelectorAll('.tabrow').length > 0, true,
               'the window still shows its own tabs');
});

test('the pinned chrome row survives a snapshot push', () => {
  // It is installed outside the root exactly so replaceChildren() cannot take
  // it - a row that vanishes on the first push looks like it was never built.
  const root = mount(RAIL_SNAPSHOT);
  h.window.atriumSetSidebar(RAIL_SNAPSHOT);
  const rows = h.body.querySelectorAll('.sbnav');
  assert.equal(rows.length, 1);
  assert.equal(root.contains(rows[0]), false);
  assert.deepEqual(
      rows[0].children.map((n) => n.className),
      ['sbtd', 'nb sbtoggle', 'nb first sbback', 'nb sbfwd', 'nb sbreload']);
});

// --- the fold intent map ----------------------------------------------------
//
// The snapshot is re-pushed on every tab title change, so a fold survives the
// push that has not caught up with it only because createFoldState overlays it
//. None of that is reachable from a single push, which is all the
// tests above perform - so the overlay is exercised directly here.

test('a folded folder stays folded when the next push still says open', () => {
  // The bug this fixed: the fold sprang back open while the user looked at it.
  const folds = createFoldState();
  folds.record('G1', true);
  assert.equal(
      folds.applyTo({folders: [{id: 'G1', closed: false}]}).folders[0].closed,
      true);
});

test('a folder with no intent takes the state the push gave it', () => {
  const folds = createFoldState();
  assert.equal(
      folds.applyTo({folders: [{id: 'G1', closed: true}]}).folders[0].closed,
      true);
});

test('an intent is dropped once a push agrees with it', () => {
  // Optimistic only. Held forever it would mask a fold changed in the SwiftUI
  // arm or in another window - this surface would pin its own answer and never
  // see theirs again.
  const folds = createFoldState();
  folds.record('G1', true);
  folds.applyTo({folders: [{id: 'G1', closed: true}]});
  assert.equal(
      folds.applyTo({folders: [{id: 'G1', closed: false}]}).folders[0].closed,
      false);
});

test('the pinned intent overlays the space-level flag', () => {
  const folds = createFoldState();
  folds.record(PINNED_FOLD_ID, true);
  assert.equal(folds.applyTo({pinnedCollapsed: false}).pinnedCollapsed, true);
});

test('the pinned intent survives a push that carries no folders', () => {
  // The pinned section is not in `folders`, so a prune that treats
  // every unlisted id as a departed group throws its intent away - and the very
  // next push, which has not caught up, springs the section open again.
  const folds = createFoldState();
  folds.record(PINNED_FOLD_ID, true);
  folds.prune({folders: []});
  assert.equal(folds.applyTo({pinnedCollapsed: false}).pinnedCollapsed, true);
});

test('a departed folder does not keep its intent alive', () => {
  // The other half of prune: the map would grow for the life of the window, and
  // a group id that came back would wear a fold set on a different folder.
  const folds = createFoldState();
  folds.record('G1', true);
  folds.prune({folders: []});
  assert.equal(
      folds.applyTo({folders: [{id: 'G1', closed: false}]}).folders[0].closed,
      false);
});

// --- attacker-controlled text -----------------------------------------------

test('a hostile title lands as text, never as markup', () => {
  // Titles come from the page. Trusted Types blocks innerHTML outright here, so
  // the only way this regresses is a hand-built element tree - which would still
  // be a scriptable injection at browser privilege.
  const hostile = '<img src=x onerror="alert(1)">';
  const root = mount({tabs: [{id: 't9', title: hostile, url: 'https://x.example/'}]});
  const label = rowsIn(root, 'tabs')[0].querySelector('.tl');
  assert.equal(label.textContent, hostile);
  assert.equal(label.children.length, 0, 'the title parsed into elements');
});

test('a hostile title never becomes the payload', () => {
  const hostile = '"], ["sidebarNewTab';
  const root = mount({tabs: [{id: 't9', title: hostile, url: 'https://x.example/'}]});
  click(rowsIn(root, 'tabs')[0]);
  assert.deepEqual(h.sent, [['sidebarTabClicked', ['t9']]]);
});

test('a cold row forwards its URL verbatim as one string argument', () => {
  // The surface does not judge the scheme - OpenSidebarColdTab does, and it
  // accepts only http/https/about. What this side owes the browser is an
  // unmangled string: a URL rebuilt or concatenated here would be a different
  // URL than the one that was checked.
  const url = 'https://one.example/a?b=1&c=%20#frag';
  const root = mount({tabs: [{id: '', title: 'Cold', url}]});
  click(rowsIn(root, 'tabs')[0]);
  assert.deepEqual(h.sent, [['sidebarColdTabClicked', [url]]]);
  assert.equal(typeof h.sent[0][1][0], 'string');
});

// --- #sbUrl -------------------------------------------------------

test('the address row is display-only, never a field to type in', () => {
  const root = mount();
  const bar = root.querySelector('#sbUrl');
  assert.equal(bar.readOnly, true);
  assert.equal(bar.tabIndex, -1);
});

test('a click on the row hands its box to the window', () => {
  const root = mount();
  const bar = root.querySelector('#sbUrl');
  bar.setRect({left: 12, top: 96, width: 240, height: 34});
  h.sent.length = 0;
  bar.dispatchEvent(new Event('click', {bubbles: true}));
  assert.deepEqual(h.sent, [['sidebarAddressRowClicked', [12, 96, 240, 34]]]);
});

test('a click on the quick menu button sends only its own message, not the row\'s', () => {
  const root = mount();
  const button = root.querySelector('.qm-open');
  button.setRect({left: 190, top: 46, width: 20, height: 20});
  h.sent.length = 0;
  click(button);
  assert.deepEqual(h.sent, [['quickMenuToggled', [190, 46, 20, 20]]]);
});

// the anchored palette's own opaque card does not fully mask the
// row's rounded corners (commandbar.css), so the click hides it outright.

test('a click on the row hides it, before the anchored card can grow', () => {
  const root = mount();
  const bar = root.querySelector('#sbUrl');
  bar.setRect({left: 12, top: 96, width: 240, height: 34});
  bar.dispatchEvent(new Event('click', {bubbles: true}));
  assert.equal(bar.classList.contains('addr-hidden'), true);
});

test('atriumSetAddressRowHidden shows the row again', () => {
  const root = mount();
  const bar = root.querySelector('#sbUrl');
  bar.classList.add('addr-hidden');
  h.window.atriumSetAddressRowHidden(false);
  assert.equal(bar.classList.contains('addr-hidden'), false);
  h.window.atriumSetAddressRowHidden(true);
  assert.equal(bar.classList.contains('addr-hidden'), true);
});

// Cmd+T opens the bar for a tab that does not exist yet, and the previous
// row kept its ring the whole time. The button carries the selection while
// the bar is open, and a snapshot that lands meanwhile must not undo it.
test('a pending new tab moves the selection onto the button', () => {
  const root = mount();
  h.window.atriumSetCommandBarNewTabPending(true);
  assert.equal(root.classList.contains('cb-newtab-pending'), true);
  h.window.atriumSetSidebar(JSON.parse(JSON.stringify(SNAPSHOT)));
  assert.equal(root.classList.contains('cb-newtab-pending'), true,
               'a push while the bar is open dropped the pending state');
  h.window.atriumSetCommandBarNewTabPending(false);
  assert.equal(root.classList.contains('cb-newtab-pending'), false);
});

test('Enter on the row submits nothing at all', () => {
  const root = mount();
  const bar = root.querySelector('#sbUrl');
  h.sent.length = 0;
  bar.dispatchEvent(new Event('keydown', {bubbles: true, key: 'Enter'}));
  assert.deepEqual(h.sent, [], 'the row still owns an edit');
});

test('a focus on the row does not gate the hotkeys', () => {
  // A read-only field swallows no keystroke, so reporting it as text focus
  // would block every gated shortcut while the row merely has the caret.
  const root = mount();
  const bar = root.querySelector('#sbUrl');
  h.sent.length = 0;
  bar.dispatchEvent(new Event('focusin', {bubbles: true}));
  assert.deepEqual(h.sent.filter((m) => m[0] === 'sidebarTextFocusChanged'), []);
});

test('sidebarAddressRowClicked is registered with the browser', () => {
  assert.ok(REGISTERED.has('sidebarAddressRowClicked'));
});

test('atriumSetSidebarUrl updates the field live, between snapshot pushes', () => {
  // The redirect-follow path: UrlBarObserver calls this on every committed
  // navigation, independent of the next full sidebar snapshot.
  const root = mount();
  h.window.atriumSetSidebarUrl('https://redirected.example');
  const bar = root.querySelector('#sbUrl');
  assert.equal(bar.title, 'https://redirected.example');
  h.window.atriumSetSidebarUrl('');
  assert.equal(bar.title, '');
});

// --- the field shows the page's host --------------------------------

test('the url field shows the pushed origin as a host', () => {
  const root = mount();
  h.window.atriumSetSidebarUrl('https://redirected.example');
  const bar = root.querySelector('#sbUrl');
  assert.equal(bar.value, 'redirected.example');
  h.window.atriumSetSidebarUrl('');
  assert.equal(bar.value, '');
});

test('the field mounts with the active tab host, not empty', () => {
  const root = mount({
    ...SNAPSHOT,
    tabs: [{id: 't1', title: 'First', url: 'https://first.example/page', selected: true}],
  });
  assert.equal(root.querySelector('#sbUrl').value, 'first.example');
});

test('a load phase for the active tab paints the docked field', () => {
  const root = mount({
    ...SNAPSHOT,
    tabs: [{id: 't1', title: 'First', url: 'https://first.example/page', selected: true}],
  });
  h.window.atriumLoadPhase('t1', 'waiting', 0);
  const fill = root.querySelector('.urlrow .loadfill');
  assert.ok(fill, 'no .loadfill in .urlrow');
  assert.equal(fill.classList.contains('waiting'), true);
});

test('a waiting or receiving phase for a tab that is not the active one is dropped', () => {
  const root = mount({
    ...SNAPSHOT,
    tabs: [{id: 't1', title: 'First', url: 'https://first.example/page', selected: true}],
  });
  h.window.atriumLoadPhase('t1', 'waiting', 0);
  h.window.atriumLoadPhase('t2', 'receiving', 0.5);
  const fill = root.querySelector('.loadfill');
  assert.equal(fill.classList.contains('waiting'), true);
  assert.equal(fill.classList.contains('receiving'), false);
});

// The tab a `done` names is the one C++ was watching, even if it is no longer
// the active one, so this push must never be dropped or that tab's fill would
// stay lit forever.
test('a done push for a tab that lost focus still clears the fill', () => {
  const root = mount({
    ...SNAPSHOT,
    tabs: [{id: 't1', title: 'First', url: 'https://first.example/page', selected: true}],
  });
  h.window.atriumLoadPhase('t1', 'waiting', 0);
  h.window.atriumSetSidebar({
    ...SNAPSHOT,
    tabs: [{id: 't2', title: 'Second', url: 'https://second.example/page', selected: true}],
  });
  h.window.atriumLoadPhase('t1', 'done', 1);
  const fill = root.querySelector('.urlrow .loadfill');
  assert.equal(fill.classList.contains('waiting'), false);
  assert.equal(fill.classList.contains('receiving'), false);
  h.window.atriumSetSidebar({
    ...SNAPSHOT,
    tabs: [{id: 't2', title: 'Second', url: 'https://second.example/page', selected: true}],
  });
  const rebuilt = root.querySelector('.urlrow .loadfill');
  assert.equal(rebuilt.classList.contains('waiting'), false);
  assert.equal(rebuilt.classList.contains('receiving'), false);
});

// A favourite claims the live tab and the space then draws no row for it, so
// the only element standing for the focused tab is the tile. The guard read
// rows alone and dropped every push, which killed the field AND the ring.
test('a load phase reaches the field and the ring when only a tile is selected',
     () => {
       const root = mount({
         ...SNAPSHOT,
         pinned: [],
         folders: [],
         tabs: [{id: '', title: 'Cold', url: 'https://cold.example/'}],
         favorites: [{
           id: 'https://live.example/page',
           title: 'Live',
           tabId: 't9',
           selected: true,
         }],
       });
       h.window.atriumTabLoading('t9', true);
       h.window.atriumLoadPhase('t9', 'receiving', 0.5);
       const fill = root.querySelector('.urlrow .loadfill');
       assert.equal(fill.classList.contains('receiving'), true);
       const tile = root.querySelector('.fav.sel');
       assert.equal(tile.classList.contains('receiving'), true);
       // Phase 2 is a rotating sweep now, not a growing fill - no angle to hold.
       assert.equal(tile.style.getPropertyValue('--fillang'), '');
     });

// --- rotating sweep replaces the growing fill --------------------------------
// Own ids per test - loading-mark.js's marks are module state, not per-mount.

test('a loading tab with no phase pushed yet stays in phase 1 - no receiving', () => {
  const root = mount({
    ...SNAPSHOT,
    tabs: [{id: 'ph1', title: 'First', url: 'https://ph1.example/page', selected: true}],
  });
  h.window.atriumTabLoading('ph1', true);
  const row = root.querySelector('[data-tab-id="ph1"]');
  assert.equal(row.classList.contains('loading'), true);
  assert.equal(row.classList.contains('receiving'), false);
});

test('a waiting push keeps the ring in phase 1', () => {
  const root = mount({
    ...SNAPSHOT,
    tabs: [{id: 'ph2', title: 'First', url: 'https://ph2.example/page', selected: true}],
  });
  h.window.atriumTabLoading('ph2', true);
  h.window.atriumLoadPhase('ph2', 'waiting', 0);
  const row = root.querySelector('[data-tab-id="ph2"]');
  assert.equal(row.classList.contains('receiving'), false);
});

test('a receiving push moves the ring, and both the row and its tile, into phase 2', () => {
  const root = mount({
    ...SNAPSHOT,
    tabs: [{id: 'ph3', title: 'First', url: 'https://ph3.example/page', selected: true}],
    favorites: [{id: 'https://ph3.example/page', title: 'Fav', tabId: 'ph3'}],
  });
  h.window.atriumTabLoading('ph3', true);
  h.window.atriumLoadPhase('ph3', 'receiving', 0.5);
  const row = root.querySelector('[data-tab-id="ph3"]');
  const tile = root.querySelector('.fav[data-fav-tab="ph3"]');
  assert.equal(row.classList.contains('loading'), true);
  assert.equal(row.classList.contains('receiving'), true);
  assert.equal(tile.classList.contains('receiving'), true);
});

// The ring outlives the done push, so dropping phase 2 there spent the rest of
// the hold replaying phase 1's pulse - a load that ended on the wrong animation.
test('a done push leaves the ring in phase 2 for the stop to end', () => {
  const root = mount({
    ...SNAPSHOT,
    tabs: [{id: 'ph4', title: 'First', url: 'https://ph4.example/page', selected: true}],
  });
  h.window.atriumTabLoading('ph4', true);
  h.window.atriumLoadPhase('ph4', 'receiving', 0.5);
  h.window.atriumLoadPhase('ph4', 'done', 1);
  const row = root.querySelector('[data-tab-id="ph4"]');
  assert.equal(row.classList.contains('receiving'), true);
});

test('a row that never started loading carries neither loading nor receiving', () => {
  const root = mount(SNAPSHOT);
  const row = root.querySelector('[data-tab-id="t1"]');
  assert.equal(row.classList.contains('loading'), false);
  assert.equal(row.classList.contains('receiving'), false);
});

// Pins the native phase string to the class it produces, so a phase pushed at
// the wrong point in the navigation - or renamed on the C++ side - fails here
// instead of silently landing on the wrong animation.
test('each native phase string maps to the ring class the CSS keys off', () => {
  const cases = [
    ['waiting', false],
    ['receiving', true],
    ['done', false],
  ];
  for (const [phase, wantsReceiving] of cases) {
    const id = `pin-${phase}`;
    const root = mount({
      ...SNAPSHOT,
      tabs: [{id, title: 'First', url: `https://${id}.example/`, selected: true}],
    });
    h.window.atriumTabLoading(id, true);
    h.window.atriumLoadPhase(id, phase, 0.5);
    const row = root.querySelector(`[data-tab-id="${id}"]`);
    assert.equal(row.classList.contains('receiving'), wantsReceiving,
                 `phase "${phase}" should map receiving to ${wantsReceiving}`);
  }
});

test('sidebar.css drives phase 1 with a pulse, never the comet sweep', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  const body = ruleBody(css, '.loading > .brdr::after {');
  assert.match(body, /animation:\s*ringpulse/);
  assert.doesNotMatch(body, /cometspin/);
});

test('sidebar.css drives phase 2 with the rotating comet, never a growing fill', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  const body = ruleBody(css, '.receiving > .brdr::before {');
  assert.match(body, /animation:\s*cometspin/);
  assert.doesNotMatch(body, /--fillang/);
  assert.doesNotMatch(body, /conic-gradient\(from -90deg/);
});

test('the growing-fill custom property is gone from sidebar.css', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  assert.doesNotMatch(css, /--fillang/);
});

// --- reduced motion still tells the ring's states apart -----------------------

test('reduced motion turns phase 1 into a static dashed ring, not a stilled pulse', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  const body = ruleBody(
      reducedMotionBlock(css), '.loading:not(.receiving) > .brdr::after {');
  assert.match(body, /animation:\s*none/);
  assert.match(body, /border-style:\s*dashed/);
  assert.match(body, /opacity:\s*1/);
});

test('reduced motion hides phase 2\'s comet arc instead of freezing it mid-sweep', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  const body = ruleBody(reducedMotionBlock(css), '.receiving > .brdr::before {');
  assert.match(body, /animation:\s*none/);
  assert.match(body, /opacity:\s*0/);
});

test('reduced motion turns phase 2 into a thicker static ring than idle or phase 1', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  const block = reducedMotionBlock(css);
  const phase2 = ruleBody(block, '.receiving > .brdr::after {');
  assert.match(phase2, /animation:\s*none/);
  assert.match(phase2, /opacity:\s*1/);
  const idleRule = ruleBody(css, '.brdr::after {');
  // The idle ring reads --selring, which the hover fill is inset by, so its
  // width has to come from the token rather than off the border shorthand.
  const idleWidth = idleRule.match(/border(?:-width)?:\s*(\d+(?:\.\d+)?)px/) ||
      (/border:\s*var\(--selring\)/.test(idleRule) &&
       css.match(/--selring:\s*(\d+(?:\.\d+)?)px/));
  const phase2Width = phase2.match(/border-width:\s*(\d+(?:\.\d+)?)px/);
  assert.ok(idleWidth && phase2Width, 'both rules declare a border width');
  assert.notEqual(phase2Width[1], idleWidth[1],
                   'phase 2 must not share idle\'s ring width');
  const phase1 = ruleBody(block, '.loading:not(.receiving) > .brdr::after {');
  assert.doesNotMatch(phase1, /border-width/,
                       'phase 1 stays at the idle width - only its dash pattern differs');
});

test('a selected row in each of idle, phase 1 and phase 2 carries a distinct ' +
     'class combination for the reduced-motion rules to key off',
     () => {
       const root = mount({
         ...SNAPSHOT,
         tabs: [{id: 'rm1', title: 'First', url: 'https://rm1.example/page', selected: true}],
       });
       const row = () => root.querySelector('[data-tab-id="rm1"]');
       assert.equal(row().classList.contains('loading'), false, 'idle');
       h.window.atriumTabLoading('rm1', true);
       assert.equal(row().classList.contains('loading'), true, 'phase 1');
       assert.equal(row().classList.contains('receiving'), false, 'phase 1');
       h.window.atriumLoadPhase('rm1', 'receiving', 0.5);
       assert.equal(row().classList.contains('receiving'), true, 'phase 2');
     });

test('a push repaints the row even while the caret sits on it', () => {
  // The click that opens the palette leaves the caret here, and the palette
  // takes the keyboard from another document - so a repaint that waited for
  // this row to lose focus would never come.
  const root = mount();
  const bar = root.querySelector('#sbUrl');
  bar.focus();
  h.window.atriumSetSidebarUrl('https://arrived.example');
  assert.equal(bar.value, 'arrived.example');
});

// --- insecure-origin treatment ---------------------------------

const INSECURE_SNAPSHOT = {
  ...SNAPSHOT,
  tabs: [{id: 't1', title: 'First', url: 'http://plain.example/page', selected: true}],
};

test('a plain-http origin paints #sbUrl red with Not secure and the host', () => {
  const root = mount(INSECURE_SNAPSHOT);
  const bar = root.querySelector('#sbUrl');
  assert.equal(bar.classList.contains('insecure'), true);
  assert.equal(bar.placeholder, 'Not secure - plain.example');
  assert.equal(bar.style.color, '#96201F');
});

test('https clears the insecure mark and restores the search placeholder', () => {
  const root = mount();
  const bar = root.querySelector('#sbUrl');
  assert.equal(bar.classList.contains('insecure'), false);
  assert.equal(bar.placeholder, '⌕ Search or enter URL');
});

test('http://localhost is treated as secure, both at build and on a live push', () => {
  const localhostSnapshot = {
    ...SNAPSHOT,
    tabs: [{
      id: 't1',
      title: 'Dev',
      url: 'http://localhost:3000/dashboard',
      selected: true,
    }],
  };
  const root = mount(localhostSnapshot);
  assert.equal(root.querySelector('#sbUrl').classList.contains('insecure'), false);

  h.window.atriumSetSidebarUrl('http://localhost:5173');
  assert.equal(root.querySelector('#sbUrl').classList.contains('insecure'), false);
});

test('a loopback IP literal is treated as secure like every other browser', () => {
  const root = mount();
  h.window.atriumSetSidebarUrl('http://127.0.0.1:8080/');
  assert.equal(root.querySelector('#sbUrl').classList.contains('insecure'), false);
  h.window.atriumSetSidebarUrl('http://[::1]:8080/');
  assert.equal(root.querySelector('#sbUrl').classList.contains('insecure'), false);
});

test('the rest of 127.0.0.0/8, including shorthand, is loopback too', () => {
  const root = mount();
  h.window.atriumSetSidebarUrl('http://127.0.0.53:8080/');
  assert.equal(root.querySelector('#sbUrl').classList.contains('insecure'), false);
  h.window.atriumSetSidebarUrl('http://127.1:8080/');
  assert.equal(root.querySelector('#sbUrl').classList.contains('insecure'), false);
});

test('a host merely prefixed with localhost is not treated as secure', () => {
  // A startsWith('localhost') match would let
  // localhost.evil.com through as if it were the real thing.
  const root = mount();
  h.window.atriumSetSidebarUrl('http://localhost.evil.com/');
  const bar = root.querySelector('#sbUrl');
  assert.equal(bar.classList.contains('insecure'), true);
  assert.equal(bar.placeholder, 'Not secure - localhost.evil.com');
});

test('atriumSetSidebarUrl flips the live field to insecure and back', () => {
  const root = mount();
  const bar = root.querySelector('#sbUrl');
  h.window.atriumSetSidebarUrl('http://plain.example/');
  assert.equal(bar.classList.contains('insecure'), true);
  assert.equal(bar.placeholder, 'Not secure - plain.example');
  h.window.atriumSetSidebarUrl('https://plain.example/');
  assert.equal(bar.classList.contains('insecure'), false);
  assert.equal(bar.placeholder, '⌕ Search or enter URL');
});

// RenderSidebar replaces every child, so a rebuild mid workspace-switch
// must carry the insecure mark from its first frame, not a later catch-up.
test('repeated rebuilds mid workspace-switch never drop the insecure mark', () => {
  const root = fresh();
  for (let i = 0; i < 5; i++) {
    renderSidebar(root, {...INSECURE_SNAPSHOT, spaceTint: `#${i}${i}${i}${i}${i}${i}`});
    const bar = root.querySelector('#sbUrl');
    assert.equal(bar.classList.contains('insecure'), true, `rebuild ${i} dropped insecure`);
    assert.equal(bar.style.color, '#96201F', `rebuild ${i} lost the ink`);
  }
});

test('sidebar.css carries the insecure ground and ring', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  assert.match(css, /\.urlbar\.insecure\s*{[^}]*background:\s*#FBE3E3/);
  assert.match(css, /\.urlbar\.insecure\s*{[^}]*inset 0 0 0 1px rgba\(150, 32, 31, \.35\)/);
});

// --- the fatal class --------------------------------------------------------

test('every name this surface sends is registered with the browser', () => {
  // An unregistered name takes down the BROWSER process, so this is not a
  // tidiness check. build/scripts/verify-webui-messages.py greps the whole tree
  // statically; this catches the same class from the names actually produced by
  // a run, which is the set a static grep cannot prove is reachable.
  const root = mount(RAIL_SNAPSHOT);
  const pane = spacePane(root);
  click(rowsIn(pane, 'tabs')[0]);
  click(rowsIn(pane, 'tabs')[2]);
  click(rowsIn(pane, 'tabs')[0].querySelector('.x'));
  click(pane.querySelector('.newtab'));
  // No .newfolder click: there is no such button. sidebarFolderCreated is
  // still produced, by the context menu, and its own test covers that route.
  // From the root: the grid is beside the rail, not inside a pane.
  click(root.querySelector('.fav'));
  click(pane.querySelector(`[data-folder-id="${PINNED_FOLD_ID}"]`));
  click(pane.querySelector('.frow[data-folder-id="G1"]'));
  click(root.querySelectorAll('.spacedots .sdot')[0]);
  click(root.querySelectorAll('.spacedots .sdot')[2]);
  runFrames();

  const names = [...new Set(h.sent.map((m) => m[0]))];
  // Eight, not nine, because there is no New Folder button. Its message
  // is still produced - by the context menu - and the floor is about how many
  // distinct names this SURFACE sends from clicks, so losing a sender lowers it
  // honestly rather than weakening the check.
  assert.ok(names.length >= 8, `only ${names.length} names exercised`);
  assert.deepEqual(names.filter((n) => !REGISTERED.has(n)), []);
});

// the fly-to-Overview flourish's entrypoint, same seam
// atriumTabSnoozeReturned already uses - no real C++ caller yet either.

test('the archive entrypoint lands on window under the name C++ will call', () => {
  fresh();
  assert.equal(typeof h.window.atriumTabsArchived, 'function');
});

test('archiving a row flies a clone toward the Overview dot', async () => {
  const root = mount(RAIL_SNAPSHOT);
  const pane = spacePane(root);
  const row = rowsIn(pane, 'tabs')[0];
  row.setRect({left: 0, top: 100, width: 200, height: 30});
  root.querySelector('.spacedots .sdot.ovdot').setRect({left: 0, top: 0, width: 20, height: 20});

  h.window.atriumTabsArchived(['t1']);

  // The hook's default clock is a real setTimeout, matching production.
  await waitFor(() => h.body.querySelector('.archivefly'));
});

test('archiving an id with no matching row is a no-op, not a throw', () => {
  const root = mount(RAIL_SNAPSHOT);
  root.querySelector('.spacedots .sdot.ovdot')
      .setRect({left: 0, top: 0, width: 20, height: 20});
  assert.doesNotThrow(() => h.window.atriumTabsArchived(['no-such-id']));
  assert.equal(h.body.querySelector('.archivefly'), null);
});

test('the row itself leaves on the next push whether or not a flight ran', () => {
  const root = mount(SNAPSHOT);
  assert.equal(rowsIn(root, 'tabs').some((r) => r.dataset.tabId === 't1'), true);

  h.window.atriumTabsArchived(['t1']);
  h.window.atriumSetSidebar({
    ...SNAPSHOT, tabs: SNAPSHOT.tabs.filter((t) => t.id !== 't1'),
  });

  assert.equal(rowsIn(root, 'tabs').some((r) => r.dataset.tabId === 't1'), false,
               'the data-driven rebuild removes the row regardless of the flourish');
});

// the PAGE must not scroll - only the tab list does.
//
// There are no html/body rules in this tree, so both default to auto height
// with body's 8px margin while .sbroot is 100vh. The document ends up taller
// than the viewport and the whole surface scrolls as one - space name,
// favourites, tabs and dots moving together - which is what the user saw
// through three rounds of fixing the INNER containers.
test('installing the sidebar stops the page itself from scrolling', () => {
  const h = installDOM();
  const root = h.document.createElement('div');
  h.body.appendChild(root);
  installSidebar(root);

  for (const el of [h.document.documentElement, h.document.body]) {
    assert.equal(el.style.overflow, 'hidden', 'the page must not scroll');
    assert.equal(el.style.height, '100%');
    assert.equal(el.style.margin, '0', "body's default margin makes the document taller than the viewport");
  }
});

// --- text focus --------------------------------------------------
//
// A chord macOS treats as text editing must not fire its hotkey while the user
// is typing in the sidebar. The gate that decides reads the ACTIVE TAB's focus
// live and cannot see this surface, because the sidebar is its own
// WebContents - so this message is the whole of the browser's knowledge, and a
// wrong one is a keystroke eaten or a hotkey dead for the session.
//
// Each of these builds its own shim rather than using the file-level one. The
// page-scroll test above calls installDOM() a second time, which replaces the
// globals installSidebar reads while the file-level `h` keeps pointing at the
// first shim - so a test after it that mixes the two watches an outbox nothing
// writes to.
function focusHarness() {
  const own = installDOM();
  const root = own.document.createElement('div');
  own.body.appendChild(root);
  installSidebar(root);
  own.sent.length = 0;
  const field = (tag = 'input') => {
    const el = own.document.createElement(tag);
    root.appendChild(el);
    return el;
  };
  return {own, field};
}

// Dispatched ON the field, not on the root: the listener is delegated, so the
// event has to bubble for the handler to see the real target.
const focusIn = (target) =>
    target.dispatchEvent(new Event('focusin', {bubbles: true}));

const focusOut = (target, relatedTarget = null) =>
    target.dispatchEvent(new Event('focusout', {bubbles: true, relatedTarget}));

test('a field taking the keyboard is reported', () => {
  const {own, field} = focusHarness();
  focusIn(field());
  assert.deepEqual(own.sent, [['sidebarTextFocusChanged', [true]]]);
});

test('a contenteditable counts as a field', () => {
  const {own, field} = focusHarness();
  const el = field('div');
  el.isContentEditable = true;
  focusIn(el);
  assert.deepEqual(own.sent, [['sidebarTextFocusChanged', [true]]]);
});

test('focusing something that is not a field reports nothing', () => {
  const {own, field} = focusHarness();
  // Already false, and the state moved to is still false. A message here would
  // be noise, and it is the kind of noise that hides a real one.
  focusIn(field('button'));
  assert.deepEqual(own.sent, []);
});

test('leaving the last field reports the keyboard is free again', () => {
  const {own, field} = focusHarness();
  const el = field();
  focusIn(el);
  own.sent.length = 0;
  focusOut(el);
  assert.deepEqual(own.sent, [['sidebarTextFocusChanged', [false]]]);
});

test('moving between two fields never reports a gap', () => {
  const {own, field} = focusHarness();
  const first = field();
  const second = field();
  focusIn(first);
  own.sent.length = 0;
  // focusout fires BEFORE focusin. Without reading relatedTarget the browser
  // would be told the keyboard is free for the length of that gap, and a chord
  // arriving in it fires its hotkey mid-edit.
  focusOut(first, second);
  focusIn(second);
  assert.deepEqual(own.sent, []);
});

test('the same state is never reported twice', () => {
  const {own, field} = focusHarness();
  const el = field();
  focusIn(el);
  own.sent.length = 0;
  focusIn(el);
  assert.deepEqual(own.sent, []);
});

// the devbar-shown class is the CSS-side half of "never both
// visible" (sidebar.css: #atrium-root.devbar-shown .urlbar{display:none}) -
// #sbUrl itself is never removed, only hidden, so CSS can still
// restyle it once the devbar takes over.
test('atriumSetDevBarShown(true) marks the root so CSS hides #sbUrl', () => {
  // renderSidebar directly, not mount()/atriumSetSidebar - the latter can
  // defer to a fold-settling timeout an earlier test left running on the
  // real clock, which would make #sbUrl's presence here a race.
  const root = fresh();
  renderSidebar(root, SNAPSHOT);
  window.atriumSetDevBarShown(true);
  assert.equal(root.classList.contains('devbar-shown'), true);
  assert.ok(root.querySelector('#sbUrl'), '#sbUrl stays in the DOM');
});

test('atriumSetDevBarShown(false) clears the mark', () => {
  const root = fresh();
  window.atriumSetDevBarShown(true);
  window.atriumSetDevBarShown(false);
  assert.equal(root.classList.contains('devbar-shown'), false);
});

test('sidebar.css hides the in-flow .urlbar under the devbar-shown mark', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  assert.match(
      css,
      /#atrium-root\.devbar-shown\s+\.sidebar\s+\.urlbar\s*{\s*display:\s*none;?\s*}/);
});

function ruleBody(css, selector) {
  const at = css.indexOf(selector);
  assert.ok(at >= 0, `${selector} is in sidebar.css`);
  return css.slice(at, css.indexOf('}', at));
}

// The file carries other reduced-motion queries too, so the search starts
// after the ring's own keyframes to land on the right one.
function reducedMotionBlock(css) {
  const marker = '@media (prefers-reduced-motion: reduce)';
  const at = css.indexOf(marker, css.indexOf('@keyframes ringpulse'));
  assert.ok(at >= 0, 'the reduced-motion query is in sidebar.css');
  const open = css.indexOf('{', at);
  let depth = 0;
  let i = open;
  for (; i < css.length; i++) {
    if (css[i] === '{') depth++;
    else if (css[i] === '}' && --depth === 0) break;
  }
  return css.slice(open + 1, i);
}

// "opacity .16s ease .2s" -> {duration: 0.16, delay: 0.2}.
function timing(body, property) {
  const m = body.match(
      new RegExp(`transition:[^;]*${property}\\s+(\\d*\\.?\\d+)s[^,;]*?(?:\\s(\\d*\\.?\\d+)s)?[,;]`));
  assert.ok(m, `${property} has a transition`);
  return {duration: Number(m[1]), delay: Number(m[2] || 0)};
}

// the hoisted row is absolutely positioned over content whose room
// is a margin that slides for .2s, so whichever goes first is the whole bug.
// These pin the order, not the numbers.
test('the docked bar fades in only after the rows have finished sliding', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  const slide = timing(ruleBody(css, '.railpane .sidebar {'), 'padding-top');
  const reveal = timing(ruleBody(css, '#atrium-root.sbroot > .urlrow'), 'opacity');
  assert.ok(slide.duration > 0, 'the rows still slide');
  assert.ok(reveal.duration > 0, 'the bar still fades');
  assert.ok(
      reveal.delay >= slide.duration + slide.delay,
      `the bar waits out the rows: ${reveal.delay}s >= ${slide.duration + slide.delay}s`);
});

test('the rows move up only after the floating bar has faded out', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  const hide = timing(
      ruleBody(css, '#atrium-root.devbar-shown.sbroot > .urlrow'), 'opacity');
  const raise = timing(
      ruleBody(css, '#atrium-root.devbar-shown .railpane .sidebar'), 'padding-top');
  assert.ok(raise.duration > 0, 'the rows still slide');
  assert.ok(
      raise.delay >= hide.duration + hide.delay,
      `the rows wait out the bar: ${raise.delay}s >= ${hide.duration + hide.delay}s`);
});

test('the hoisted row is hidden by opacity, never by display', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  const hidden = ruleBody(css, '#atrium-root.devbar-shown.sbroot > .urlrow');
  assert.doesNotMatch(hidden, /display:\s*none/);
  assert.match(hidden, /opacity:\s*0/);
  // Or a faded-out field still answers the tab key.
  assert.match(hidden, /visibility:\s*hidden/);
});


// the window's appearance reaches the root element as three
// values C++ writes directly - the same way --atrium-docked-width and the
// floating flag arrive. These pin the half that lives here: what the
// stylesheet does with them.
// The hoisted grid and address row are absolute children of the root, so a
// hold on .sidebar alone leaves them re-flowing with the slot.
test('the sidebar root holds the docked width and shifts by what the slot lost', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  const root = css.match(/#atrium-root\.sbroot \{([^}]*)\}/)[1];
  assert.match(root, /width:\s*var\(--atrium-docked-width, 100%\);/);
  assert.match(
      root,
      /margin-left:\s*calc\(100% - var\(--atrium-docked-width, 100%\)\);/);
  const sidebar = css.match(/\n\.sidebar \{([^}]*)\}/)[1];
  assert.doesNotMatch(sidebar, /--atrium-docked-width/);
  assert.doesNotMatch(sidebar, /margin-left/);
});

// The panel paints the window's backdrop stack under the page natively, so a
// wash here would composite the tint twice and could never carry the picture.
test('the floating page paints no wash of its own and evens out its insets', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  const floating = [...css.matchAll(/html\.atrium-sidebar-floating[^{]*\{([^}]*)\}/g)]
      .map((m) => m[1]);
  assert.ok(floating.length > 0, 'the floating class still has rules');
  for (const body of floating) {
    assert.doesNotMatch(body, /background:/);
  }
  assert.doesNotMatch(css, /--atrium-float-wash-floor/);
  const inset = css.match(/html\.atrium-sidebar-floating \{([^}]*)\}/);
  assert.ok(inset, 'the floating root sets its own insets');
  // Both named. Copying trail from lead evened them out only while the sidebar
  // docked left - on the right the dock has already swapped which is narrow,
  // so the copy would have carried 2px to both edges.
  assert.match(inset[1], /--sb-pad-lead:\s*8px/);
  assert.match(inset[1], /--sb-pad-trail:\s*8px/);
});

// lead is the window edge and trail the page edge, so which
// physical side each one is has to follow the dock.
test('docking right swaps the two side insets', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  const root = css.match(/^:root \{([^}]*)\}/m);
  assert.ok(root, ':root still declares the pair');
  assert.match(root[1], /--sb-pad-lead:\s*8px/);
  assert.match(root[1], /--sb-pad-trail:\s*2px/);
  const right = css.match(/html\.atrium-sidebar-on-right \{([^}]*)\}/);
  assert.ok(right, 'the right dock has its own rule');
  assert.match(right[1], /--sb-pad-lead:\s*2px/);
  assert.match(right[1], /--sb-pad-trail:\s*8px/);
});

// The class is what the CSS keys on, and only the floating overlay used to set
// it - a docked-right sidebar never got one and so never mirrored.
test('the sidebar binding sets the on-right class, not only the shelf zone', () => {
  const js = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.js'), 'utf8');
  const binding = js.match(
      /window\.atriumSetSidebarOnRight = [\s\S]*?\n  \};/);
  assert.ok(binding, 'the binding is still there');
  assert.match(binding[0], /classList\.toggle\(\s*'atrium-sidebar-on-right'/);
  assert.match(binding[0], /shelfZone\.setOnRight\(onRight\)/);
});

test('the glass ink block is scoped to the sidebar root, not html', () => {
  // this sheet serves every route. On html the block painted the
  // settings page's labels white on white.
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  assert.match(css, /html:not\(\.atrium-native-chrome\) #atrium-root\.sbroot \{/);
  assert.doesNotMatch(css, /^html:not\(\.atrium-native-chrome\) \{/m);
});

// The window paints the surround under the sidebar document, docked and
// floating, with the page glow on top. A fill here covered that glow.
test('the native-app surround is painted by the window, not the sidebar', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  assert.doesNotMatch(
      css, /html\.atrium-native-chrome:has\(#atrium-root\.sbroot\)\s*{/);
});

// The chrome's ink is white by rule, not by accident:
// atrium_browser_window.cc pins kChromeInkHex to #FFFFFF and the core refuses
// any tint drift that would cost contrast against it. A near-white surround is
// the one background that rule never covered, so the mode carries its own ink.
test('the native-app rule points every ink token at the pushed colour', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  const rule = css.match(/html\.atrium-native-chrome\s*{[^}]*}/)[0];
  assert.match(rule, /--atrium-ink:\s*var\(--atrium-surround-ink, 255 255 255\)/);
  for (const token of ['--fg', '--fill', '--fillhover', '--selfill', '--ringb']) {
    assert.match(rule, new RegExp(`\\${token}:\\s*rgb\\(var\\(--atrium-ink\\)`), token);
  }
});

// The rewind bar and the history strip paint their own dark slab, so their
// white is not the sidebar's ink and must not follow it.
test('the popovers that carry their own dark slab keep their white', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  for (const sel of ['.rwbtn', '.histthumb']) {
    const rule = css.match(new RegExp(`\\${sel}\\s*{[^}]*}`))[0];
    assert.match(rule, /rgba\(255, 255, 255/, sel);
  }
});

// --- the loading ring belongs to the focused tab --------------------

// Its own shim, for the reason focusHarness above gives: the file-level `h` no
// longer owns the globals installSidebar reads.

// Its own pair of tab ids per test too - the marks live in loading-mark.js's
// module state, which outlives one mount.

// Own shim, for the reason focusHarness gives.
function carryHarness() {
  const own = installDOM({pathname: '/sidebar'});
  const root = own.document.createElement('div');
  own.body.appendChild(root);
  installSidebar(root);
  const push = (snapshot) =>
      own.window.atriumSetSidebar({...SNAPSHOT, iconScope: 'carryscope', ...snapshot});
  push({});
  return {own, root, push};
}

// Every img the sidebar makes from here on. A handoff's fresh img is detached
// until it loads, so the tree alone cannot reach it.
function recordImgs(own) {
  const made = [];
  const create = own.document.createElement;
  own.document.createElement = (tag) => {
    const el = create(tag);
    if (tag === 'img') {
      made.push(el);
    }
    return el;
  };
  return made;
}

// A push rebuilds the whole tree, and a fresh img paints its letter plate until
// the browser answers. The renderer's image cache does not always hold the
// bitmap (a prune drops the oldest entries first), and on the host the first
// favourite click after launch turned the first five tiles into plates for
// half a second. The rebuild adopts the loaded URL-keyed img nodes of the tree
// it replaces instead of fetching them again.
test('a push carries the loaded URL-keyed icons of the outgoing tree into the new one', () => {
  const {own, root, push} = carryHarness();
  const before = root.querySelectorAll('.fvimg');
  const byUrl = before.filter((img) => img.src.startsWith('/urlicon/'));
  assert.ok(byUrl.length >= 2, 'a tile and a cold row asked for a URL-keyed icon');
  for (const img of before) {
    img.dispatchEvent(new Event('load'));
  }
  const loaded = new Map(before.map((img) => [img.src, img]));

  push({});
  const after = root.querySelectorAll('.fvimg');
  assert.equal(after.length, before.length, 'the same slots ask again');
  for (const img of after.filter((i) => i.src.startsWith('/urlicon/'))) {
    assert.equal(img, loaded.get(img.src),
                 'the rebuilt slot holds the node that already loaded');
    assert.ok(img.classList.contains('ok'), 'and it is painted at once');
  }
  own.sent.length = 0;
});

// A cross-profile space switch in one window moves the snapshot to the other
// profile's icon scope, so every URL-keyed path changes. Matching on the whole
// path carried nothing, and every slot showed its plate until a reload.
// The old node holds the slot only until the new scope answers. Kept for good,
// it showed the other profile's bytes and never asked this one.
test('a scope flip shows the old icon until the new scope\'s icon loads', () => {
  const {own, root, push} = carryHarness();
  push({iconScope: 'a'});
  const before = root.querySelectorAll('.fvimg');
  for (const img of before) {
    img.dispatchEvent(new Event('load'));
  }
  const loaded = new Map(before
      .filter((img) => img.src.startsWith('/urlicon/a/'))
      .map((img) => [img.src.slice('/urlicon/a/'.length), img]));
  assert.ok(loaded.size >= 2, 'a tile and a cold row asked for a URL-keyed icon');

  const asked = recordImgs(own);
  push({iconScope: 'b'});
  const slots = root.querySelectorAll('.fvimg')
      .filter((img) => img.src.startsWith('/urlicon/'));
  assert.equal(slots.length, loaded.size, 'the same slots ask again');
  for (const img of slots) {
    const key = img.src.replace(/^\/urlicon\/[^/]*\//, '');
    assert.equal(img, loaded.get(key),
                 'the slot holds the node that loaded under the old scope');
    assert.ok(img.classList.contains('ok'), 'and it is painted at once');
  }
  const fresh = asked.filter((img) => img.src.startsWith('/urlicon/b/'));
  assert.equal(fresh.length, loaded.size, 'every slot asks the new scope');

  for (const img of fresh) {
    img.dispatchEvent(new Event('load'));
  }
  const settled = root.querySelectorAll('.fvimg')
      .filter((img) => img.src.startsWith('/urlicon/'));
  assert.equal(settled.length, loaded.size);
  for (const img of settled) {
    assert.ok(fresh.includes(img), `the old node stayed: ${img.src}`);
    assert.ok(img.classList.contains('ok'));
  }
});

// Only a URL's icon may cross scopes. A tab id names one tab browser-wide, so
// another scope's path for it is never the same row.
test('a tab icon key keeps its scope, a URL icon key drops it', () => {
  assert.notEqual(iconKey('/favicon/a/7.png'), iconKey('/favicon/b/7.png'));
  assert.equal(iconKey('/urlicon/a/x.png'), iconKey('/urlicon/b/x.png'));
  assert.notEqual(iconKey('/urlicon/a/x.png'), '');
});

// One URL twice: a favourite and a cold pinned row.
const DUP_URL = 'https://dup.example/';
const DUP_ROWS = {favorites: [{id: DUP_URL, title: 'Dup'}], folders: [], tabs: [],
                  pinned: [{id: '', title: 'Dup', url: DUP_URL}]};
const dupPath = (scope) => `/urlicon/${scope}/${encodeURIComponent(DUP_URL)}.png`;
const dupIcons = (root) =>
    root.querySelectorAll('.fvimg').filter((img) => img.src.endsWith(
        encodeURIComponent(DUP_URL) + '.png'));

test('a same-scope push keeps every loaded copy of one URL', () => {
  const {own, root, push} = carryHarness();
  push({...DUP_ROWS, iconScope: 'a'});
  const loaded = dupIcons(root);
  assert.equal(loaded.length, 2, 'a tile and a row ask for the same URL');
  for (const img of loaded) {
    img.dispatchEvent(new Event('load'));
  }
  const asked = recordImgs(own);
  push({...DUP_ROWS, iconScope: 'a'});
  assert.deepEqual(new Set(dupIcons(root)), new Set(loaded),
                   'a loaded copy was dropped');
  assert.deepEqual(asked.filter((img) => img.src === dupPath('a')), [],
                   'a kept icon was asked for again');
});

// A flip leaves the old scope's node as a placeholder beside a current-scope
// node for the same URL. The current one is kept for good, whichever comes
// first in the tree.
test('a push keeps the node already under the current scope', () => {
  const {own, root, push} = carryHarness();
  push({...DUP_ROWS, iconScope: 'a'});
  for (const img of dupIcons(root)) {
    img.dispatchEvent(new Event('load'));
  }
  const firstAsk = recordImgs(own);
  push({...DUP_ROWS, iconScope: 'b'});
  const fresh = firstAsk.filter((img) => img.src === dupPath('b'));
  assert.equal(fresh.length, 2, 'both slots ask the new scope');
  fresh[1].dispatchEvent(new Event('load'));
  const current = fresh[1];
  const [oldNode] = dupIcons(root).filter((img) => img.src === dupPath('a'));
  assert.ok(oldNode, 'the other slot still shows the old scope\'s node');
  assert.ok(dupIcons(root).includes(current));

  const secondAsk = recordImgs(own);
  push({...DUP_ROWS, iconScope: 'b'});
  assert.ok(dupIcons(root).includes(current),
            'the current-scope node was dropped for the old one');
  assert.equal(secondAsk.filter((img) => img.src === dupPath('b')).length, 1,
               'only the slot holding the old node asks again');
});

// A miss under the new scope is the honest state, so the slot shows its plate
// rather than the other profile's icon.
test('a scope flip whose new icon fails leaves the plate, not the old icon', () => {
  const {own, root, push} = carryHarness();
  push({iconScope: 'a'});
  for (const img of root.querySelectorAll('.fvimg')) {
    img.dispatchEvent(new Event('load'));
  }
  const asked = recordImgs(own);
  push({iconScope: 'b'});
  assert.ok(root.querySelectorAll('.fvimg').length > 0, 'the old nodes hold the slots');
  // Twice: a live row falls back once before it gives up.
  for (let round = 0; round < 2; ++round) {
    for (const img of asked) {
      img.dispatchEvent(new Event('error'));
    }
  }
  assert.deepEqual(root.querySelectorAll('.fvimg').map((img) => img.src), [],
                   'an old node outlived the miss');
});

// An off-the-record profile's icons must not reach a regular profile's rows, or
// the other way round, so a scope change on either side carries nothing.
for (const [from, to] of [[false, true], [true, false]]) {
  test(`a scope change ${from ? 'out of' : 'into'} an off-the-record profile carries no icon`, () => {
    const {root, push} = carryHarness();
    push({iconScope: 'a', incognito: from});
    const before = root.querySelectorAll('.fvimg');
    for (const img of before) {
      img.dispatchEvent(new Event('load'));
    }
    assert.ok(before.some((img) => img.src.startsWith('/urlicon/a/')),
              'a URL-keyed icon loaded, so there was something to carry');

    push({iconScope: 'b', incognito: to});
    const after = root.querySelectorAll('.fvimg');
    assert.ok(after.length > 0, 'the new tree asks for icons');
    for (const img of after) {
      assert.ok(!before.includes(img), `a loaded node crossed: ${img.src}`);
    }
  });
}

// The page scope follows the window's target browser. The chip's bytes live
// with the profile that pushed the pins, so a snapshot must not move the chip.
test('a snapshot scope change leaves the extension chips alone', () => {
  const {own, root} = carryHarness();
  own.window.atriumSetSidebar({...ONE_PROFILE_SNAPSHOT, iconScope: 'a'});
  own.window.atriumSetSidebarExtensions(
      pin('ext').map((p) => ({...p, iconScope: 'e'})), 'Default');
  const strip = [...root.parentNode.children]
      .find((el) => el.classList.contains('exs-row'));
  assert.ok(strip, 'the strip is installed');
  const chipSrc = () => strip.querySelector('.crx-icon').src;
  assert.ok(chipSrc().startsWith('/exticon/e/'), chipSrc());

  own.window.atriumSetSidebar({...ONE_PROFILE_SNAPSHOT, iconScope: 'b'});
  assert.ok(chipSrc().startsWith('/exticon/e/'),
            `the chip took the page scope: ${chipSrc()}`);
});

// The tab-keyed cache changes under one path when a page swaps its favicon,
// so a row with a live tab asks again, and keeps its old icon until then.
test('a push asks again for a live row\'s tab-keyed icon', () => {
  const {own, root, push} = carryHarness();
  // Ids of its own: a 404 in another test memoises its rows as cold.
  const rows = {pinned: [], folders: [],
                tabs: [{id: 'refetch1', title: 'R', url: 'https://r.example/'}]};
  push(rows);
  const tabIcons = () => root.querySelectorAll('.fvimg')
      .filter((img) => img.src.startsWith('/favicon/'));
  const live = tabIcons();
  assert.ok(live.length >= 1, 'a live row asked for its tab-keyed icon');
  for (const img of live) {
    img.dispatchEvent(new Event('load'));
  }
  const asked = recordImgs(own);
  push(rows);
  assert.deepEqual(tabIcons(), live, 'the loaded node holds the slot meanwhile');
  const fresh = asked.filter((img) => img.src.startsWith('/favicon/'));
  assert.equal(fresh.length, live.length, 'every live row asks again');

  for (const img of fresh) {
    img.dispatchEvent(new Event('load'));
  }
  assert.deepEqual(tabIcons(), fresh, 'the refetch took the slot');
});

test('a refetch that 404s still hands the slot to the URL-keyed answer', () => {
  const {own, root, push} = carryHarness();
  const rows = {pinned: [], folders: [],
                tabs: [{id: 'refetch2', title: 'R', url: 'https://r2.example/'}]};
  push(rows);
  const old = root.querySelector('.row .fvimg');
  assert.ok(old.src.startsWith('/favicon/'), old.src);
  old.dispatchEvent(new Event('load'));

  const asked = recordImgs(own);
  push(rows);
  const fresh = asked.find((img) => img.src === old.src);
  fresh.dispatchEvent(new Event('error'));
  assert.ok(fresh.src.startsWith('/urlicon/'), `no fallback: ${fresh.src}`);
  assert.equal(root.querySelector('.row .fvimg'), old, 'the old node waits');
  fresh.dispatchEvent(new Event('load'));
  assert.equal(root.querySelector('.row .fvimg'), fresh);
  assert.ok(fresh.classList.contains('ok'));
});

test('a push does not adopt an icon that never loaded, or one under another src', () => {
  const {root, push} = carryHarness();
  const pending = root.querySelector('.fav .fvimg');
  assert.equal(pending.classList.contains('ok'), false);
  push({});
  assert.notEqual(root.querySelector('.fav .fvimg'), pending,
                  'an in-flight img is not carried, it is asked again');

  const img = root.querySelector('.fav .fvimg');
  img.dispatchEvent(new Event('load'));
  push({favorites: [{id: 'https://other.example/', title: 'Other'}]});
  assert.notEqual(root.querySelector('.fav .fvimg'), img,
                  'a tile for another URL gets its own img');
});

function ringHarness(snapshot) {
  const own = installDOM({pathname: '/sidebar'});
  const root = own.document.createElement('div');
  own.body.appendChild(root);
  installSidebar(root);
  own.window.atriumSetSidebar(snapshot);
  own.sent.length = 0;
  return {own, root};
}

function focusSnapshot(a, b, selected) {
  const tab = (id, n) => ({
    id,
    title: `Tab ${n}`,
    url: `https://${id}.example/`,
    selected: id === selected,
  });
  return {
    spaceName: 'Focus',
    spaceTint: '#12282E',
    favorites: [{id: `https://${a}.example/`, title: 'A', tabId: a},
                {id: `https://${b}.example/`, title: 'B', tabId: b}],
    tabs: [tab(a, 1), tab(b, 2)],
  };
}

const ringedRows = (root) =>
    root.querySelectorAll('.tabrow.loading').map((r) => r.dataset.tabId);

test('a load on the focused tab rings its row', () => {
  const {own, root} = ringHarness(focusSnapshot('r1', 'r2', 'r1'));
  own.window.atriumTabLoading('r1', true);
  assert.deepEqual(ringedRows(root), ['r1']);
  assert.equal(root.querySelectorAll('.fav.loading').length, 1,
               'the favourite tile for the same tab rings with it');
});

test('a load in a background tab draws no ring', () => {
  const {own, root} = ringHarness(focusSnapshot('r3', 'r4', 'r3'));
  own.window.atriumTabLoading('r4', true);
  assert.deepEqual(ringedRows(root), []);
  assert.equal(root.querySelectorAll('.loading').length, 0);
});

test('a snapshot that moves focus takes the ring off the row it left', () => {
  const {own, root} = ringHarness(focusSnapshot('r5', 'r6', 'r5'));
  own.window.atriumTabLoading('r5', true);
  assert.deepEqual(ringedRows(root), ['r5'], 'the ring never started');

  own.window.atriumSetSidebar(focusSnapshot('r5', 'r6', 'r6'));
  assert.deepEqual(ringedRows(root), [],
                   'the load is still running, but not on the focused tab');
});

test('a load-start after the switch rings the tab that now has focus', () => {
  const {own, root} = ringHarness(focusSnapshot('r7', 'r8', 'r7'));
  own.window.atriumTabLoading('r7', true);
  own.window.atriumSetSidebar(focusSnapshot('r7', 'r8', 'r8'));
  own.window.atriumTabLoading('r8', true);
  assert.deepEqual(ringedRows(root), ['r8']);
});

test('clicking a favourite mid-load moves the ring to the tile clicked', () => {
  // The report: both rings ran. The click is the focus change, so the ring on
  // the row being left has to stop before any push comes back.
  const {own, root} = ringHarness(focusSnapshot('r9', 'r10', 'r9'));
  own.window.atriumTabLoading('r9', true);
  click(root.querySelectorAll('.fav')[1]);
  // Empty, not ['r10']: the tile clicked is bound to a live tab, so it opens
  // nothing and rings nothing. The row LEFT still has to go dark,
  // which is what this test was filed for.
  assert.deepEqual(ringedRows(root), []);
  assert.deepEqual(own.sent, [['sidebarFavoriteClicked',
                               ['https://r10.example/', 'r10']]]);
});

test('clicking a row mid-load stops the ring on the row being left', () => {
  const {own, root} = ringHarness(focusSnapshot('r11', 'r12', 'r11'));
  own.window.atriumTabLoading('r11', true);
  click(rowsIn(root, 'tabs')[1]);
  assert.deepEqual(ringedRows(root), [],
                   'the row left kept its ring, or the live row clicked drew one');
});

test('a push still naming the old tab cannot blank the clicked row', () => {
  // The snapshot in flight when the click lands was built before it, and the
  // browser sends one on every title change - so this is the common case, not
  // a race. Adopting it blanked the new ring until the next push.
  const {own, root} = ringHarness(focusSnapshot('r13', 'r14', 'r13'));
  own.window.atriumTabLoading('r13', true);
  click(rowsIn(root, 'tabs')[1]);
  own.window.atriumTabLoading('r14', true);
  assert.deepEqual(ringedRows(root), ['r14']);

  own.window.atriumSetSidebar(focusSnapshot('r13', 'r14', 'r13'));
  assert.deepEqual(ringedRows(root), ['r14'], 'the stale push won');

  own.window.atriumSetSidebar(focusSnapshot('r13', 'r14', 'r14'));
  assert.deepEqual(ringedRows(root), ['r14'], 'the push that agreed lost it');
});

// A push rebuilds the whole tree, and a fresh img paints its letter plate until
// the browser answers. The renderer's image cache does not always hold the
// bitmap (a prune drops the oldest entries first), and on the host the first
// favourite click after launch turned the first five tiles into plates for
// half a second. The rebuild adopts the loaded img nodes of the tree it
// replaces instead of fetching them again.
