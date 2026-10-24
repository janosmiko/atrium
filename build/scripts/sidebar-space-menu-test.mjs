#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// empty-space and favourite-tile menus, in full lab ordering.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const h = installDOM();

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {installRowMenu, closeMenu, openSnoozeMenu} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/sidebar-menu.js'));
const {dismissMenus, menuRegistrationCount} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/menu-dismiss.js'));
const {setBindings} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/hint-triggers.js'));

// Read from the header rather than copied, so a rename on either side shows up
// here instead of at the next launch.
const REGISTERED = new Set(
    fs.readFileSync(
          path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
          'utf8')
        .split('kSurfaceMessages[] = {')[1]
        .split('};')[0]
        .match(/"[^"]+"/g)
        .map((quoted) => quoted.slice(1, -1)));
const {buildEmptySpaceMenuItems, buildFavoriteMenuItems, buildMirrorMenuItem,
       buildSpaceDotMenuItems} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/sidebar.js'));

const fire = (node, type, init = {}) => {
  const event = new Event(type, {bubbles: true, ...init});
  node.dispatchEvent(event);
  return event;
};

const labelsOf = (items) => items.map((item) => item.label);
const separatorIndexes = (items) =>
    items.reduce((acc, item, i) => (item.label ? acc : [...acc, i]), []);

// --- buildEmptySpaceMenuItems -------------------------------------

const SNAPSHOT = {
  activeSpace: 0,
  spaces: [{name: 'Work', profileId: 'work'}, {name: 'Home', profileId: 'home'}],
};

function emptyItems(snapshot = SNAPSHOT, hooks = {}) {
  const emit = hooks.emit || (() => {});
  const openRewind = hooks.openRewind || (() => {});
  const renameSpace = hooks.renameSpace || (() => {});
  const pickIcon = hooks.pickIcon || (() => {});
  return buildEmptySpaceMenuItems(
      snapshot, {emit, openRewind, renameSpace, pickIcon});
}

test('the empty-space menu matches the lab item set and order', () => {
  assert.deepEqual(labelsOf(emptyItems()), [
    'Change Space Icon', 'Change Space Name', 'Change Theme', 'Set Profile…',
    undefined,
    'New Tab', 'New Folder', 'New Workspace',
    undefined,
    'Downloads', 'Archived Tabs', 'Snoozed Tabs', 'Shelf', 'Notes', 'Courtyard',
    undefined,
    'Rewind', 'Hide Sidebar', 'Settings',
  ]);
});

test('the empty-space menu\'s separators sit where the lab groups them', () => {
  assert.deepEqual(separatorIndexes(emptyItems()), [4, 8, 15]);
});

test('no empty-space menu item is disabled', () => {
  // Set Profile's submenu OPENS - its own entries are what's disabled.
  const disabled = emptyItems()
      .filter((item) => item.label && item.disabled)
      .map((item) => item.label);
  assert.deepEqual(disabled, []);
});

test('Change Space Icon opens the picker for the space the menu was opened over', () => {
  let pickedIndex;
  emptyItems({...SNAPSHOT, activeSpace: 1}, {pickIcon: (index) => {
    pickedIndex = index;
  }}).find((item) => item.label === 'Change Space Icon').action();
  assert.equal(pickedIndex, 1);
});

function sentBy(label, snapshot = SNAPSHOT, hooks = {}) {
  const sent = [];
  emptyItems(snapshot, {...hooks, emit: (name, args) => sent.push([name, args])})
      .find((item) => item.label === label).action();
  return sent;
}

for (const [label, section] of [
  ['Downloads', 'downloads'], ['Archived Tabs', 'archived'],
  ['Snoozed Tabs', 'snoozed'], ['Shelf', 'shelf'], ['Notes', 'notes'],
]) {
  test(`${label} opens the Overview at ${section}`, () => {
    assert.deepEqual(sentBy(label),
                     [['sidebarOverviewSectionOpened', [section]]]);
  });
}

test('New Workspace creates a space, the same message as the dot rail\'s +', () => {
  assert.deepEqual(sentBy('New Workspace'), [['sidebarSpaceCreated', []]]);
});

test('Hide Sidebar toggles the sidebar', () => {
  assert.deepEqual(sentBy('Hide Sidebar'), [['sidebarToggleRequested', []]]);
});

test('Change Space Name renames the space the menu was opened over', () => {
  let editedPane;
  const sent = sentBy('Change Space Name', {...SNAPSHOT, activeSpace: 1}, {
    renameSpace: (index, commit) => {
      editedPane = index;
      commit('Errands');
    },
  });
  assert.equal(editedPane, 1, 'every space pane has a name row - edit the active one');
  assert.deepEqual(sent, [['sidebarSpaceRenamed', [1, 'Errands']]]);
});

test('every message the empty-space menu sends is registered', () => {
  for (const name of ['sidebarOverviewSectionOpened', 'sidebarSpaceRenamed',
                      'sidebarSpaceIconSet',
                      'sidebarSpaceCreated', 'sidebarToggleRequested']) {
    assert.ok(REGISTERED.has(name), name);
  }
});

test('sidebarSpaceThemeRequested is registered - an unregistered name is FATAL in the browser', () => {
  assert.ok(REGISTERED.has('sidebarSpaceThemeRequested'));
});

test('Change Theme names the space the menu was opened over', () => {
  const sent = [];
  emptyItems({...SNAPSHOT, activeSpace: 1},
             {emit: (name, args) => sent.push([name, args])})
      .find((i) => i.label === 'Change Theme').action();
  assert.deepEqual(sent, [['sidebarSpaceThemeRequested', [1]]]);
});

test('Change Theme carries the dot rect for the studio window', () => {
  const sent = [];
  const items = buildSpaceDotMenuItems(2, {
    emit: (name, args) => sent.push([name, args]),
    rect: {x: 40.4, y: 700.6, width: 14, height: 14},
  });
  items.find((item) => item.label === 'Change Theme').action();
  assert.deepEqual(sent, [['sidebarSpaceThemeRequested',
                           [2, {x: 40, y: 701, width: 14, height: 14}]]]);
});

test('Change Theme with no rect sends just the space index', () => {
  const sent = [];
  const items = buildSpaceDotMenuItems(
      2, {emit: (name, args) => sent.push([name, args])});
  items.find((item) => item.label === 'Change Theme').action();
  assert.deepEqual(sent, [['sidebarSpaceThemeRequested', [2]]]);
});

// The space name row folds the pinned section, so it wears .frow and used to
// fall to the folder menu - which offers Rename/Delete Folder against
// PINNED_FOLD_ID, a pseudo-group the core knows nothing about.
function spaceNameFixture({pinned}) {
  h.reset();
  const root = h.document.createElement('div');
  root.setRect({left: 0, top: 0, width: 200, height: 300});
  const name = h.document.createElement('div');
  name.className = pinned ? 'wsname frow' : 'wsname';
  if (pinned) {
    name.dataset.folderId = '__pinned__';
  }
  root.appendChild(name);
  h.body.appendChild(root);
  const sent = [];
  installRowMenu(root, {
    send: (msg, args) => sent.push([msg, args]),
    emptyMenuItems: (emit) => emptyItems(SNAPSHOT, {emit}),
  });
  return {root, name, sent};
}

test('a right-click on the space name row opens the space menu', () => {
  const {root, name} = spaceNameFixture({pinned: false});
  rightClick(name);
  assert.equal(items(root)[0].textContent, 'Change Space Icon');
});

test('the space name row keeps the space menu once it folds pinned tabs', () => {
  const {root, name} = spaceNameFixture({pinned: true});
  rightClick(name);
  assert.equal(items(root)[0].textContent, 'Change Space Icon');
  assert.equal(items(root).some((el) => el.textContent === 'Rename Folder'),
               false, 'the pinned fold is not a group to rename');
});

test('Change Theme on the space name row asks for that space\'s studio', () => {
  const {root, name, sent} = spaceNameFixture({pinned: true});
  rightClick(name);
  items(root).find((el) => el.textContent === 'Change Theme').click();
  assert.deepEqual(sent, [['sidebarSpaceThemeRequested', [0]]]);
});

// the lab's space menu opens settings on the Spaces pane.
test('Settings opens the settings window on the Spaces pane', () => {
  const sent = [];
  emptyItems(SNAPSHOT, {emit: (name, args) => sent.push([name, args])})
      .find((i) => i.label === 'Settings').action();
  assert.deepEqual(sent, [['openSettingsPane', ['spaces']]]);
});

test('New Tab sends sidebarNewTab', () => {
  const sent = [];
  emptyItems(SNAPSHOT, {emit: (name, args) => sent.push([name, args])})
      .find((i) => i.label === 'New Tab').action();
  assert.deepEqual(sent, [['sidebarNewTab', []]]);
});

test('New Folder sends sidebarFolderCreated', () => {
  const sent = [];
  emptyItems(SNAPSHOT, {emit: (name, args) => sent.push([name, args])})
      .find((i) => i.label === 'New Folder').action();
  assert.deepEqual(sent, [['sidebarFolderCreated', []]]);
});

test('Courtyard sends sidebarCourtyardOpened', () => {
  const sent = [];
  emptyItems(SNAPSHOT, {emit: (name, args) => sent.push([name, args])})
      .find((i) => i.label === 'Courtyard').action();
  assert.deepEqual(sent, [['sidebarCourtyardOpened', []]]);
});

test('Rewind calls the injected opener rather than a message', () => {
  let opened = 0;
  emptyItems(SNAPSHOT, {openRewind: () => opened++})
      .find((i) => i.label === 'Rewind').action();
  assert.equal(opened, 1);
});

test('Set Profile checks the active space\'s profile', () => {
  const submenu = emptyItems({...SNAPSHOT, activeSpace: 0})
      .find((i) => i.label === 'Set Profile…').items;
  assert.deepEqual(labelsOf(submenu), ['✓ work', '  home']);
  assert.equal(submenu.every((i) => i.disabled), true);
});

test('Set Profile clears the check off the space no longer selected', () => {
  const submenu = emptyItems({...SNAPSHOT, activeSpace: 1})
      .find((i) => i.label === 'Set Profile…').items;
  assert.deepEqual(labelsOf(submenu), ['  work', '✓ home']);
});

test('Set Profile lists each profile once', () => {
  const snapshot = {
    activeSpace: 0,
    spaces: [{profileId: 'work'}, {profileId: 'work'}, {profileId: 'home'}],
  };
  const submenu = emptyItems(snapshot).find((i) => i.label === 'Set Profile…').items;
  assert.deepEqual(labelsOf(submenu), ['✓ work', '  home']);
});

test('Set Profile labels a profile by its display name when the chip carries one', () => {
  const snapshot = {
    activeSpace: 0,
    spaces: [{profileId: 'Profile 7', profileName: 'Personal'}, {profileId: 'Profile 9'}],
  };
  const submenu = emptyItems(snapshot).find((i) => i.label === 'Set Profile…').items;
  assert.deepEqual(labelsOf(submenu), ['✓ Personal', '  Profile 9']);
});

// --- buildFavoriteMenuItems --------------------------------------------

test('the favourite-tile menu matches the lab item set and order', () => {
  const items = buildFavoriteMenuItems({dataset: {favId: 'https://x.example/'}});
  assert.deepEqual(labelsOf(items), [
    'Copy Link', 'Share…', 'Summarize Page',
    undefined,
    'Change Icon',
    undefined,
    'Add Split View', 'Mirror View…',
    undefined,
    'Put on Shelf', 'Add to Dock',
    undefined,
    'Remove from Favorites',
  ]);
});

test('the favourite-tile menu\'s separators sit where the lab groups them', () => {
  const items = buildFavoriteMenuItems({dataset: {favId: 'https://x.example/'}});
  assert.deepEqual(separatorIndexes(items), [3, 5, 8, 11]);
});

test('only Copy Link and Put on Shelf are enabled - the rest have no correctly-targeted message', () => {
  const items = buildFavoriteMenuItems({dataset: {favId: 'https://x.example/'}});
  const enabledLabels = items
      .filter((item) => item.label && !item.disabled)
      .map((item) => item.label);
  assert.deepEqual(
      enabledLabels, ['Copy Link', 'Put on Shelf', 'Remove from Favorites']);
});

// favourites were a one-way door - the entry rendered disabled
// and the core had no remove export at all.
test('Remove from Favorites sends the tile\'s url', () => {
  const sent = [];
  const favCell = {dataset: {favId: 'https://x.example/page'}, title: 'X'};
  buildFavoriteMenuItems(favCell, (name, args) => sent.push([name, args]))
      .find((i) => i.label === 'Remove from Favorites').action();
  assert.deepEqual(sent, [['sidebarFavoriteRemoved', ['https://x.example/page']]]);
});

test('a tile with no url sends nothing rather than an empty remove', () => {
  const sent = [];
  buildFavoriteMenuItems({dataset: {}}, (name, args) => sent.push([name, args]))
      .find((i) => i.label === 'Remove from Favorites').action();
  assert.deepEqual(sent, []);
});

test('Copy Link copies the tile\'s id, which is already the page URL', () => {
  const favCell = {dataset: {favId: 'https://x.example/page'}};
  buildFavoriteMenuItems(favCell).find((i) => i.label === 'Copy Link').action();
  assert.deepEqual(h.clipboard, ['https://x.example/page']);
});

// a favourite has no tab id, so Put on Shelf sends [url, title]
// instead of the row-menu's tab-id message.
test('Put on Shelf sends the favourite\'s url and title', () => {
  const favCell = {dataset: {favId: 'https://x.example/page'}, title: 'Example Page'};
  const sent = [];
  buildFavoriteMenuItems(favCell, (name, args) => sent.push([name, args]))
      .find((i) => i.label === 'Put on Shelf').action();
  assert.deepEqual(sent, [
    ['sidebarFavoriteShelved', ['https://x.example/page', 'Example Page']],
  ]);
});

test('Put on Shelf falls back to the url when the tile has no title', () => {
  const favCell = {dataset: {favId: 'https://x.example/page'}, title: ''};
  const sent = [];
  buildFavoriteMenuItems(favCell, (name, args) => sent.push([name, args]))
      .find((i) => i.label === 'Put on Shelf').action();
  assert.deepEqual(sent, [
    ['sidebarFavoriteShelved', ['https://x.example/page', 'https://x.example/page']],
  ]);
});

// --- installRowMenu wiring: disabled rendering, submenu, the .fav route -----

function menuFixture() {
  h.reset();
  const root = h.document.createElement('div');
  root.className = 'sbroot';
  root.setRect({left: 0, top: 0, width: 200, height: 300});

  const grid = h.document.createElement('div');
  grid.className = 'favgrid';
  grid.dataset.dropzone = 'favorites';
  const tile = h.document.createElement('div');
  tile.className = 'fav';
  tile.dataset.favId = 'https://x.example/';
  tile.title = 'Example';
  grid.appendChild(tile);
  root.appendChild(grid);

  h.body.appendChild(root);
  const sent = [];
  installRowMenu(root, {
    send: (name, args) => sent.push([name, args]),
    emptyMenuItems: (emit) => emptyItems(SNAPSHOT, {emit}),
    favoriteMenuItems: (favCell, emit) => buildFavoriteMenuItems(favCell, emit),
  });
  return {root, sent, tile};
}

const rightClick = (node) => fire(node, 'contextmenu', {clientX: 10, clientY: 10});
const items = (root) => [...root.querySelectorAll('.ctxitem')];


test('a right-click on a favourite tile opens the favourite-tile menu', () => {
  const {root, tile} = menuFixture();
  const event = rightClick(tile);
  assert.equal(event.defaultPrevented, true);
  assert.deepEqual(
      items(root).map((el) => el.textContent),
      ['Copy Link', 'Share…', 'Summarize Page', 'Change Icon', 'Add Split View',
       'Mirror View…', 'Put on Shelf', 'Add to Dock', 'Remove from Favorites']);
});

test('a right-click on empty space opens the empty-space menu', () => {
  const {root} = menuFixture();
  rightClick(root);
  assert.equal(items(root)[0].textContent, 'Change Space Icon');
});

test('a disabled item renders as a disabled button with the disabled class', () => {
  const {root, tile} = menuFixture();
  rightClick(tile);
  const shareBtn = items(root).find((el) => el.textContent === 'Share…');
  assert.equal(shareBtn.disabled, true);
  assert.equal(shareBtn.classList.contains('ctxitem-disabled'), true);
});

test('clicking a disabled item sends nothing and leaves the menu open', () => {
  const {root, tile, sent} = menuFixture();
  rightClick(tile);
  items(root).find((el) => el.textContent === 'Share…').click();
  assert.deepEqual(sent, []);
  assert.notEqual(root.querySelector('.ctxmenu'), null);
});

test('clicking Put on Shelf on a favourite tile sends its url and title', () => {
  const {root, tile, sent} = menuFixture();
  rightClick(tile);
  items(root).find((el) => el.textContent === 'Put on Shelf').click();
  assert.deepEqual(sent, [
    ['sidebarFavoriteShelved', [tile.dataset.favId, tile.title]],
  ]);
});

test('choosing Copy Link sends nothing and closes the menu', () => {
  const {root, tile, sent} = menuFixture();
  rightClick(tile);
  items(root).find((el) => el.textContent === 'Copy Link').click();
  assert.deepEqual(sent, []);
  assert.deepEqual(h.clipboard, ['https://x.example/']);
  assert.equal(root.querySelector('.ctxmenu'), null);
});

test('choosing Set Profile opens a submenu with the checkmark, not an action', () => {
  const {root} = menuFixture();
  rightClick(root);
  items(root).find((el) => el.textContent === 'Set Profile…').click();
  assert.deepEqual(items(root).map((el) => el.textContent), ['✓ work', '  home']);
});

test('the profile submenu\'s own entries are disabled', () => {
  const {root} = menuFixture();
  rightClick(root);
  items(root).find((el) => el.textContent === 'Set Profile…').click();
  assert.equal(items(root).every((el) => el.disabled), true);
});

// --- buildMirrorMenuItem ------------------------------------

test('Mirror View lists every other profile then Logged out', () => {
  const item = buildMirrorMenuItem(SNAPSHOT, 'https://x.example/', () => {});
  assert.equal(item.label, 'Mirror View…');
  assert.equal(item.disabled, undefined);
  assert.deepEqual(labelsOf(item.items), ['as home', undefined, 'Logged out']);
});

test('Mirror View is disabled with no snapshot to derive choices from', () => {
  const item = buildMirrorMenuItem(undefined, 'https://x.example/', () => {});
  assert.equal(item.disabled, true);
  assert.equal(item.items, undefined);
});

test('choosing a Mirror View profile sends the url and that profile id', () => {
  const sent = [];
  const item = buildMirrorMenuItem(
      SNAPSHOT, 'https://x.example/', (name, args) => sent.push([name, args]));
  item.items.find((i) => i.label === 'as home').action();
  assert.deepEqual(
      sent, [['sidebarTabMirrored', ['https://x.example/', 'home']]]);
});

test('choosing Logged out sends mirror.js\'s LOGGED_OUT sentinel', () => {
  const sent = [];
  const item = buildMirrorMenuItem(
      SNAPSHOT, 'https://x.example/', (name, args) => sent.push([name, args]));
  item.items.find((i) => i.label === 'Logged out').action();
  assert.equal(sent[0][1][1], ' logged-out');
});

test('the favourite-tile menu\'s Mirror View is live given a snapshot naming another profile', () => {
  const items2 = buildFavoriteMenuItems(
      {dataset: {favId: 'https://x.example/'}}, () => {}, SNAPSHOT);
  const mirror = items2.find((i) => i.label === 'Mirror View…');
  assert.equal(mirror.disabled, undefined);
  assert.deepEqual(labelsOf(mirror.items), ['as home', undefined, 'Logged out']);
});

// --- Mirror View in the tab row's own context menu ---------

function rowMenuFixture(dropzone, {mirrorMenuItem} = {}) {
  h.reset();
  const root = h.document.createElement('div');
  root.setRect({left: 0, top: 0, width: 200, height: 300});
  const zone = h.document.createElement('div');
  zone.className = 'rows';
  zone.dataset.dropzone = dropzone;
  const row = h.document.createElement('div');
  row.className = 'row tabrow';
  row.dataset.tabId = 't1';
  row.dataset.url = 'https://x.example/page';
  zone.appendChild(row);
  root.appendChild(zone);
  h.body.appendChild(root);
  const sent = [];
  installRowMenu(root, {send: (name, args) => sent.push([name, args]), mirrorMenuItem});
  return {root, row, sent};
}

test('the tab row menu sits Mirror View in its own group, ahead of Put on Shelf', () => {
  const {root, row} = rowMenuFixture('tabs', {
    mirrorMenuItem: (url, emit) => buildMirrorMenuItem(SNAPSHOT, url, emit),
  });
  rightClick(row);
  assert.deepEqual(items(root).map((el) => el.textContent),
      // Copy Link leads every row menu, as it does in the
      // lab and in the favourite menu.
      // Rename joins them, right after Copy Link.
      // Duplicate and the Archive pair join too. This fixture
      // renders one row, so nothing is below it.
      ['Copy Link', 'Rename', 'Pin Tab', 'Snooze', 'Mirror View…', 'Duplicate',
       'Put on Shelf', 'Archive Tab', 'Close Tab']);
});

test('the pinned row menu sits Mirror View in its own group, ahead of Put on Shelf', () => {
  const {root, row} = rowMenuFixture('pinned', {
    mirrorMenuItem: (url, emit) => buildMirrorMenuItem(SNAPSHOT, url, emit),
  });
  rightClick(row);
  assert.deepEqual(items(root).map((el) => el.textContent),
      // A pinned row is offered neither Archive entry: both address the
      // regular list by index.
      ['Copy Link', 'Rename', 'Unpin Tab', 'Add to Dock', 'Mirror View…',
       'Duplicate', 'Put on Shelf', 'Close Tab']);
});

test('the tab row menu omits Mirror View when the caller supplies no mirrorMenuItem', () => {
  const {root, row} = rowMenuFixture('tabs');
  rightClick(row);
  assert.deepEqual(items(root).map((el) => el.textContent),
      ['Copy Link', 'Rename', 'Pin Tab', 'Snooze', 'Duplicate', 'Put on Shelf',
       'Archive Tab', 'Close Tab']);
});

test('the tab row\'s Mirror View passes that row\'s own url, not a favourite\'s', () => {
  const sent = [];
  const {root, row} = rowMenuFixture('tabs', {
    mirrorMenuItem: (url, emit) => buildMirrorMenuItem(
        SNAPSHOT, url, (name, args) => sent.push([name, args])),
  });
  rightClick(row);
  items(root).find((el) => el.textContent === 'Mirror View…').click();
  items(root).find((el) => el.textContent === 'as home').click();
  assert.deepEqual(
      sent, [['sidebarTabMirrored', ['https://x.example/page', 'home']]]);
});

// --- the window-level dismiss ------------------------------------------------

// The state report goes over chrome.send, not the injected sender, so h.sent
// is where it lands and an item's action log stays readable.
test('opening the menu reports it open, and closing it reports it closed', () => {
  const {root, sent} = menuFixture();
  rightClick(root);
  assert.deepEqual(h.sent, [['sidebarMenuOpened', []]]);
  closeMenu(root);
  assert.deepEqual(h.sent, [['sidebarMenuOpened', []], ['sidebarMenuClosed', []]]);
  assert.deepEqual(sent, []);
});

test('a scroll with no menu open reports nothing', () => {
  const {root} = menuFixture();
  closeMenu(root);
  closeMenu(root);
  assert.deepEqual(h.sent, []);
});

test('the native dismiss push closes the open menu and reports it closed', () => {
  const {root} = menuFixture();
  rightClick(root);
  assert.notEqual(root.querySelector('.ctxmenu'), null);
  dismissMenus();
  assert.equal(root.querySelector('.ctxmenu'), null);
  assert.deepEqual(h.sent.at(-1), ['sidebarMenuClosed', []]);
});

test('the dismiss receiver the native push calls is installed on window', () => {
  const {root} = menuFixture();
  rightClick(root);
  window.atriumDismissMenus();
  assert.equal(root.querySelector('.ctxmenu'), null);
});

test('both dismiss messages are registered, so neither aborts the browser', () => {
  assert.ok(REGISTERED.has('sidebarMenuOpened'));
  assert.ok(REGISTERED.has('sidebarMenuClosed'));
});

// The nav row is root's previous SIBLING, and it sits inside the sidebar rect
// the native monitor excludes - so nothing else would ever close this menu.
test('a press on the nav row beside the sidebar body closes the menu', () => {
  const {root} = menuFixture();
  const navRow = h.document.createElement('div');
  navRow.className = 'sbnav';
  h.body.insertBefore(navRow, root);
  rightClick(root);
  assert.notEqual(root.querySelector('.ctxmenu'), null);
  fire(navRow, 'pointerdown');
  assert.equal(root.querySelector('.ctxmenu'), null);
});

// The registry is module-global and nothing about a root's DOM shows an entry
// still holding it, so a leak here grows silently.
test('a second install drops the first root\'s registration', () => {
  menuFixture();
  assert.equal(menuRegistrationCount(), 1);
  menuFixture();
  assert.equal(menuRegistrationCount(), 1, 'the detached root is still pinned');
});

// --- overlay design language -------------------------------------------------

test('the menu card wears ov-card and every row wears ov-row', () => {
  const {root} = menuFixture();
  rightClick(root);
  const menu = root.querySelector('.ctxmenu');
  assert.equal(menu.classList.contains('ov-card'), true);
  for (const item of items(root)) {
    assert.equal(item.classList.contains('ov-row'), true);
  }
});

test('the snooze menu and its alert row wear the same overlay classes', () => {
  h.reset();
  const root = h.document.createElement('div');
  root.setRect({left: 0, top: 0, width: 200, height: 300});
  h.body.appendChild(root);
  const menu = openSnoozeMenu(root, 10, 10, {index: 0, emit: () => {}});
  assert.equal(menu.classList.contains('ov-card'), true);
  assert.equal(
      root.querySelector('.snoozealert').classList.contains('ov-row'), true);
  for (const item of items(root)) {
    assert.equal(item.classList.contains('ov-row'), true);
  }
});

// A hand check per file, not the shared overlay-literal-gate.sh: sidebar.css
// is shared with rules this suite has no business judging, so this reads only
// the menu's own block rather than the whole file.
test('the menu rules in sidebar.css read tokens, not literal colors', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  const start = css.indexOf('.ctxmenu {');
  const end = css.indexOf('.ctxrule {', start);
  assert.ok(start >= 0 && end > start, 'the menu rule block moved or was renamed');
  const block = css.slice(start, css.indexOf('}', end) + 1);
  assert.doesNotMatch(block, /rgba?\(|#[0-9a-fA-F]{3,8}\b/);
});

// --- row keycaps -------------------------------------------------------

function favMenuFixture(favoriteMenuItems) {
  h.reset();
  const root = h.document.createElement('div');
  root.className = 'sbroot';
  root.setRect({left: 0, top: 0, width: 200, height: 300});
  const grid = h.document.createElement('div');
  grid.className = 'favgrid';
  grid.dataset.dropzone = 'favorites';
  const tile = h.document.createElement('div');
  tile.className = 'fav';
  tile.dataset.favId = 'https://x.example/';
  grid.appendChild(tile);
  root.appendChild(grid);
  h.body.appendChild(root);
  installRowMenu(root, {send: () => {}, favoriteMenuItems});
  return {root, tile};
}

test('a row whose action has a binding shows one keycap per key', () => {
  setBindings({demoAction: '⌘K'});
  try {
    const {root, tile} = favMenuFixture(
        () => [{label: 'Demo', actionId: 'demoAction', action: () => {}}]);
    rightClick(tile);
    const caps = [...items(root)[0].querySelectorAll('.ov-key')]
        .map((el) => el.textContent);
    assert.deepEqual(caps, ['⌘', 'K']);
  } finally {
    setBindings({});
  }
});

test('a row whose action has no binding shows no keycap', () => {
  const {root, tile} = favMenuFixture(
      () => [{label: 'Demo', actionId: 'demoAction', action: () => {}}]);
  rightClick(tile);
  assert.equal(items(root)[0].querySelector('.ov-key'), null);
});

// the two above drive a made-up action, so they stayed green for as
// long as no real menu item carried an actionId. These drive the shipped items.
const itemLabelled = (root, label) =>
    items(root).find((el) => el.textContent.startsWith(label));
const capsOf = (el) => [...el.querySelectorAll('.ov-key')].map((k) => k.textContent);

test('Pin Tab shows the keys pinToggleTab is bound to', () => {
  setBindings({pinToggleTab: '⌘D'});
  try {
    const {root, row} = rowMenuFixture('tabs');
    rightClick(row);
    assert.deepEqual(capsOf(itemLabelled(root, 'Pin Tab')), ['⌘', 'D']);
  } finally {
    setBindings({});
  }
});

test('Unpin Tab shows them too, since it runs the same action', () => {
  setBindings({pinToggleTab: '⌘D'});
  try {
    const {root, row} = rowMenuFixture('pinned');
    rightClick(row);
    assert.deepEqual(capsOf(itemLabelled(root, 'Unpin Tab')), ['⌘', 'D']);
  } finally {
    setBindings({});
  }
});

test('rebinding the action changes what the menu shows', () => {
  setBindings({pinToggleTab: '⌃⇧P'});
  try {
    const {root, row} = rowMenuFixture('tabs');
    rightClick(row);
    assert.deepEqual(capsOf(itemLabelled(root, 'Pin Tab')), ['⌃', '⇧', 'P']);
  } finally {
    setBindings({});
  }
});

test('an unbound item renders with no keycap span at all', () => {
  setBindings({pinToggleTab: '⌘D'});
  try {
    const {root, row} = rowMenuFixture('tabs');
    rightClick(row);
    assert.equal(itemLabelled(root, 'Close Tab').querySelector('.ov-key'), null);
    assert.equal(itemLabelled(root, 'Close Tab').textContent, 'Close Tab');
  } finally {
    setBindings({});
  }
});

test('with nothing bound, Pin Tab reads exactly as it did before', () => {
  const {root, row} = rowMenuFixture('tabs');
  rightClick(row);
  assert.equal(itemLabelled(root, 'Pin Tab').textContent, 'Pin Tab');
  assert.equal(itemLabelled(root, 'Pin Tab').querySelector('.ov-key'), null);
});
