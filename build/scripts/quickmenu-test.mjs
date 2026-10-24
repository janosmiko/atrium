#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The /quickmenu route: the share row, the site row and the extension grid.
//
//   node --test build/scripts/quickmenu-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

const h = installDOM({pathname: '/quickmenu'});
const {installQuickMenu} = await import(path.join(ATRIUM_DIR, 'quickmenu.js'));

const SCOPE = 'testscope';

function fresh() {
  h.reset();
  const root = document.createElement('div');
  document.body.appendChild(root);
  installQuickMenu(root);
  window.atriumSetQuickMenuIconScope(SCOPE);
  return root;
}

function action(id, overrides = {}) {
  return {id, name: id, actionTitle: id, visible: true, hasPopup: false,
          pinned: false, iconScope: SCOPE, ...overrides};
}

const WEB_SITE = {host: 'example.com', secure: true, shareable: true};
const CHROME_SITE = {host: '', secure: false, shareable: false};

test('installing announces the renderer is live', () => {
  h.reset();
  const root = document.createElement('div');
  document.body.appendChild(root);
  installQuickMenu(root);
  assert.deepEqual(h.sent, [['quickMenuReady', []]]);
});

test('one tile per extension, in the order the browser sent them', () => {
  const root = fresh();
  window.atriumSetQuickMenuActions([action('ublock'), action('onepassword')]);
  const tiles = root.querySelectorAll('.qm-tile');
  assert.equal(tiles.length, 2);
  assert.deepEqual(tiles.map((t) => t.dataset.extensionId),
                   ['ublock', 'onepassword']);
  assert.equal(tiles[0].querySelector('img').src,
               '/exticon/' + SCOPE + '/ublock.png');
});

test('the pin dot follows the pinned field, and so does the title', () => {
  const root = fresh();
  window.atriumSetQuickMenuActions(
      [action('ublock', {pinned: true}), action('other')]);
  const tiles = root.querySelectorAll('.qm-tile');
  assert.equal(tiles[0].classList.contains('qm-pinned'), true);
  assert.equal(tiles[0].title, 'ublock - pinned');
  assert.equal(tiles[1].classList.contains('qm-pinned'), false);
  assert.equal(tiles[1].title, 'other');
});

test('an extension name never becomes a node', () => {
  const root = fresh();
  window.atriumSetQuickMenuActions(
      [action('id', {actionTitle: '<img src=x onerror=boom>'})]);
  const tile = root.querySelector('.qm-tile');
  assert.equal(tile.querySelectorAll('img').length, 1, 'only the icon');
  assert.equal(tile.title, '<img src=x onerror=boom>');
});

test('a tile click runs the action and asks to close', () => {
  const root = fresh();
  window.atriumSetQuickMenuActions([action('ublock')]);
  h.sent.length = 0;
  const tile = root.querySelector('.qm-tile');
  // A menu tile reports its own screen rect, so the popup opens against the
  // tile the user pressed rather than the menu's corner.
  tile.setRect({left: 30, top: 50, width: 40, height: 40});
  window.screenX = 200;
  window.screenY = 100;
  tile.click();
  assert.deepEqual(h.sent,
                   [['pillActionClicked', ['ublock', [230, 150, 40, 40]]]]);
});

test('a right click opens the extension context menu with the tile rect', () => {
  const root = fresh();
  window.atriumSetQuickMenuActions([action('ublock')]);
  const tile = root.querySelector('.qm-tile');
  tile.setRect({left: 30, top: 50, width: 40, height: 40});
  window.screenX = 200;
  window.screenY = 100;
  h.sent.length = 0;
  tile.dispatchEvent(
      new Event('contextmenu', {bubbles: true, cancelable: true}));
  assert.deepEqual(
      h.sent,
      [['extensionChipContextMenu', ['ublock', [230, 150, 40, 40]]]]);
});

test('a long press toggles the pin and swallows the click that follows', async () => {
  const root = fresh();
  window.atriumSetQuickMenuActions([action('ublock')]);
  const tile = root.querySelector('.qm-tile');
  h.sent.length = 0;
  tile.dispatchEvent(new Event('pointerdown', {bubbles: true}));
  await new Promise((resolve) => setTimeout(resolve, 550));
  tile.dispatchEvent(new Event('pointerup', {bubbles: true}));
  tile.click();
  assert.deepEqual(h.sent, [['extensionPinToggled', ['ublock']]],
                   'the action must not also run');
});

test('a native long-press contextmenu after the timer does not re-toggle', async () => {
  const root = fresh();
  window.atriumSetQuickMenuActions([action('ublock')]);
  const tile = root.querySelector('.qm-tile');
  h.sent.length = 0;
  tile.dispatchEvent(new Event('pointerdown', {bubbles: true}));
  await new Promise((resolve) => setTimeout(resolve, 550));
  tile.dispatchEvent(
      new Event('contextmenu', {bubbles: true, cancelable: true}));
  assert.deepEqual(h.sent, [['extensionPinToggled', ['ublock']]],
                   'the timer already sent it - contextmenu must not resend');
});

test('a grid rebuild mid-press cancels the timer for the detached tile', async () => {
  const root = fresh();
  window.atriumSetQuickMenuActions([action('ublock')]);
  const tile = root.querySelector('.qm-tile');
  h.sent.length = 0;
  tile.dispatchEvent(new Event('pointerdown', {bubbles: true}));
  window.atriumSetQuickMenuActions([action('ublock')]);
  await new Promise((resolve) => setTimeout(resolve, 550));
  assert.deepEqual(h.sent, [], 'the stale tile must not toggle the pin');
});

test('a short press runs the action rather than toggling the pin', () => {
  const root = fresh();
  window.atriumSetQuickMenuActions([action('ublock')]);
  const tile = root.querySelector('.qm-tile');
  h.sent.length = 0;
  tile.dispatchEvent(new Event('pointerdown', {bubbles: true}));
  tile.dispatchEvent(new Event('pointerup', {bubbles: true}));
  tile.setRect({left: 30, top: 50, width: 40, height: 40});
  window.screenX = 200;
  window.screenY = 100;
  tile.click();
  assert.deepEqual(h.sent,
                   [['pillActionClicked', ['ublock', [230, 150, 40, 40]]]]);
});

test('with nothing installed the store link appears and the grid is empty', () => {
  const root = fresh();
  window.atriumSetQuickMenuActions([]);
  assert.equal(root.querySelectorAll('.qm-tile').length, 0);
  assert.ok(root.querySelector('.qm-store'));
});

// The widget delegates no new-window navigation, so an <a target="_blank">
// here would drop the click silently.
test('the store control asks the browser to open the store', () => {
  const root = fresh();
  window.atriumSetQuickMenuActions([]);
  const store = root.querySelector('.qm-store');
  assert.equal(store.tagName, 'BUTTON');
  h.sent.length = 0;
  store.click();
  assert.deepEqual(h.sent, [['quickMenuStore', []]]);
});

test('with something installed the store link is gone', () => {
  const root = fresh();
  window.atriumSetQuickMenuActions([action('ublock')]);
  assert.equal(root.querySelector('.qm-store'), null);
});

test('Manage asks the browser for the extensions page', () => {
  const root = fresh();
  window.atriumSetQuickMenuActions([]);
  h.sent.length = 0;
  root.querySelector('.qm-manage').click();
  assert.deepEqual(h.sent, [['quickMenuManage', []]]);
});

test('the site row shows the host and its state, and opens page info', () => {
  const root = fresh();
  window.atriumSetQuickMenuSite(WEB_SITE);
  assert.equal(root.querySelector('.qm-site-host').textContent, 'example.com');
  assert.equal(root.querySelector('.qm-site-state').textContent, 'Secure');
  h.sent.length = 0;
  root.querySelector('.qm-site').click();
  assert.deepEqual(h.sent, [['quickMenuSiteInfo', []]]);
});

test('an insecure site says so and wears the broken lock', () => {
  const root = fresh();
  window.atriumSetQuickMenuSite({...WEB_SITE, secure: false});
  assert.equal(root.querySelector('.qm-site-state').textContent, 'Not secure');
  assert.equal(root.querySelector('.qm-site').classList.contains('qm-insecure'),
               true);
  // Two shackle paths plus the body: the snapped glyph. A shut lock has one.
  assert.equal(
      root.querySelectorAll('.qm-site-glyph path').length, 2,
      'the broken lock draws its shackle in two pieces');
  window.atriumSetQuickMenuSite(WEB_SITE);
  assert.equal(root.querySelectorAll('.qm-site-glyph path').length, 1);
});

test('share and copy reach the browser on a web page', () => {
  const root = fresh();
  window.atriumSetQuickMenuSite(WEB_SITE);
  h.sent.length = 0;
  root.querySelector('.qm-share-btn').click();
  root.querySelector('.qm-copy-btn').click();
  assert.deepEqual(h.sent, [['quickMenuShare', []], ['quickMenuCopy', []]]);
});

test('the share row is disabled and silent on a non-web URL', () => {
  const root = fresh();
  window.atriumSetQuickMenuSite(CHROME_SITE);
  const share = root.querySelector('.qm-share-btn');
  assert.equal(share.disabled, true);
  h.sent.length = 0;
  share.click();
  assert.deepEqual(h.sent, [], 'a disabled row sends nothing');
});

test('every name this route sends is registered with the browser', () => {
  const registered = new Set(
      fs.readFileSync(
            path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
            'utf8')
          .split('kSurfaceMessages[] = {')[1]
          .split('};')[0]
          .match(/"[^"]+"/g)
          .map((quoted) => quoted.slice(1, -1)));
  for (const name of ['quickMenuReady', 'quickMenuToggled', 'quickMenuManage',
                      'quickMenuStore', 'quickMenuSiteInfo', 'quickMenuShare',
                      'quickMenuCopy', 'extensionPinToggled',
                      'pillActionClicked', 'extensionChipContextMenu']) {
    assert.ok(registered.has(name), `${name} missing from kSurfaceMessages`);
  }
});

test('the card reads its colours and font from the overlay tokens', () => {
  const css = fs.readFileSync(path.join(ATRIUM_DIR, 'quickmenu.css'), 'utf8');
  assert.doesNotMatch(css, /rgba?\(|#[0-9a-fA-F]{3,8}\b/,
                      'no literal colour - overlay.css owns them');
  assert.match(css, /\.qm-card\s*{[^}]*width:\s*232px/,
               'the card is 232px wide');
  assert.match(css, /\.qm-grid\s*{[^}]*repeat\(4,/,
               'four tiles per row');
});

test('no file in the repo still names a deleted surface or setting', () => {
  const REPO = path.resolve(path.dirname(new URL(import.meta.url).pathname),
                            '..', '..');
  const dead = ['pill.js', 'sidebar-chrome-row.js', 'pinnedExtensionPlacement'];
  const roots = [
    path.join(REPO, 'overlay/chrome/browser/resources/atrium'),
    path.join(REPO, 'overlay/chrome/browser/ui/atrium'),
    path.join(REPO, 'overlay/third_party/atrium/core/src'),
    path.join(REPO, 'rust/atrium-core/src'),
    path.join(REPO, 'build/scripts'),
    path.join(REPO, 'build/scenarios'),
    path.join(REPO, 'build/vm-fixtures'),
  ];
  // This guard's own dead-name list, and the settings.rs regression tests
  // that load/round-trip a file still carrying the retired key, are not
  // leftovers - allow exactly those paths.
  const allowed = new Set([
    path.join(REPO, 'build/scripts/quickmenu-test.mjs'),
    path.join(REPO, 'overlay/third_party/atrium/core/src/settings.rs'),
    path.join(REPO, 'rust/atrium-core/src/settings.rs'),
  ]);
  const offenders = [];
  const walk = (dir) => {
    for (const entry of fs.readdirSync(dir, {withFileTypes: true})) {
      const full = path.join(dir, entry.name);
      // The harness writes run output under vm-fixtures/results. Those are
      // recordings of old builds, not source.
      if (entry.isDirectory()) {
        if (entry.name !== 'results' && entry.name !== 'target') {
          walk(full);
        }
        continue;
      }
      if (allowed.has(full)) {
        continue;
      }
      const text = fs.readFileSync(full, 'utf8').toLowerCase();
      for (const name of dead) {
        if (text.includes(name.toLowerCase())) {
          offenders.push(`${full} names ${name}`);
        }
      }
    }
  };
  for (const root of roots) {
    if (fs.existsSync(root)) {
      walk(root);
    }
  }
  assert.deepEqual(offenders, []);
});
