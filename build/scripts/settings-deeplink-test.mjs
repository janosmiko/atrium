#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// opening settings ON a named pane, from somewhere other than
// Cmd+comma.
//
//   node --test build/scripts/settings-deeplink-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');

const h = installDOM({pathname: '/settings'});

const {installSettings, PANES} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/settings.js'));

function installed() {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installSettings(root);
  h.sent.length = 0;
  return root;
}

const openPane = (id) => h.window.atriumSelectSettingsPane(id);

const railSelection = (root) =>
    [...root.querySelectorAll('.settings-rail-item')]
        .find((el) => el.classList.contains('selected'))
        ?.dataset.pane;

test('the browser can open the page on a named pane', () => {
  const root = installed();
  openPane('spaces');
  assert.equal(railSelection(root), 'spaces');
});

test('a deep link reports the pane it landed on, as a click does', () => {
  // The browser has no other way to learn where the page ended up, and the
  // resolution below can land somewhere other than what was asked for.
  const root = installed();
  openPane('spaces');
  assert.deepEqual(h.sent.filter(([name]) => name === 'settingsPaneSelected'),
                   [['settingsPaneSelected', ['spaces']]]);
});

test('a retired pane name lands on the pane that absorbed it', () => {
  // 'shortcuts' merged into Inputs. A caller built against the old name gets
  // the right pane rather than General.
  const root = installed();
  openPane('shortcuts');
  assert.equal(railSelection(root), 'inputs');
});

test('a pane name this build does not know falls back rather than blanking', () => {
  const root = installed();
  openPane('no-such-pane');
  assert.equal(railSelection(root), PANES[0].id);
});

test('opening the pane already shown is not reported as a change', () => {
  // Cmd+comma onto General, then a deep link to General: the window raises and
  // nothing moved, so there is nothing to tell the browser.
  const root = installed();
  openPane(PANES[0].id);
  assert.deepEqual(h.sent.filter(([name]) => name === 'settingsPaneSelected'), []);
});

test('an empty section leaves the page where it is', () => {
  // Show() with no section is the plain Cmd+comma open, and it must not drag
  // the window back to General when the page is already on another pane.
  const root = installed();
  openPane('spaces');
  h.sent.length = 0;
  openPane('');
  assert.equal(railSelection(root), 'spaces');
  assert.deepEqual(h.sent.filter(([name]) => name === 'settingsPaneSelected'), []);
});
