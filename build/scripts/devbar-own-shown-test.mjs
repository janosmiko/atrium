#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Split forces every pane to wear a bar, but must not hide #sbUrl.
//   node --test build/scripts/devbar-own-shown-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');

const h = installDOM({pathname: '/sidebar'});
const {installSidebar} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/sidebar.js'));

function fresh() {
  h.reset();
  const root = document.createElement('div');
  h.body.appendChild(root);
  installSidebar(root);
  return root;
}

test('split-forced bar (shown, not ownShown) keeps the sidebar urlrow up', () => {
  const root = fresh();
  window.atriumSetDevBarShown(false, true);
  assert.equal(root.classList.contains('devbar-shown'), false);
  // The broader effects (ext strip, nav handoff) still react to a
  // split-forced bar.
  assert.equal(document.documentElement.classList.contains('devbar-shown'),
               true);
});

test('the user\'s own toggle hides the urlrow whether split or not', () => {
  const root = fresh();
  window.atriumSetDevBarShown(true, true);
  assert.equal(root.classList.contains('devbar-shown'), true);

  window.atriumSetDevBarShown(true, false);
  assert.equal(root.classList.contains('devbar-shown'), true);
});

test('leaving split restores what the toggle alone decides', () => {
  const root = fresh();
  // Split-forced, toggle off.
  window.atriumSetDevBarShown(false, true);
  assert.equal(root.classList.contains('devbar-shown'), false);

  // Split closes: shown now follows the toggle alone, still off.
  window.atriumSetDevBarShown(false, false);
  assert.equal(root.classList.contains('devbar-shown'), false);
  assert.equal(document.documentElement.classList.contains('devbar-shown'),
               false);
});

test('a single-argument call keeps the pre-split, one-bar behavior', () => {
  const root = fresh();
  window.atriumSetDevBarShown(true);
  assert.equal(root.classList.contains('devbar-shown'), true);
  assert.equal(document.documentElement.classList.contains('devbar-shown'),
               true);

  window.atriumSetDevBarShown(false);
  assert.equal(root.classList.contains('devbar-shown'), false);
  assert.equal(document.documentElement.classList.contains('devbar-shown'),
               false);
});
