#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');

const h = installDOM({pathname: '/theme'});
const {installThemeWindow} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/theme-window.js'));

const REGISTERED = new Set(
    fs.readFileSync(
          path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
          'utf8')
        .split('kSurfaceMessages[] = {')[1]
        .split('};')[0]
        .match(/"[^"]+"/g)
        .map((quoted) => quoted.slice(1, -1)));

const SPEC = {
  name: 'Studio',
  stops: [{x: 0.2, y: 0.2, color: '#3366CC', alpha: 1}],
  opacity: 0.3,
  effects: {grain: 0, vignette: 0, frost: 0, warmth: 0},
  darkAdaptation: 0,
  version: 1,
};
const PAYLOAD = {
  spaceId: '77777777-7777-7777-7777-777777777777',
  spaceName: 'Personal',
  accent: '#3366CC',
  spec: SPEC,
};

const root = document.createElement('div');
document.body.appendChild(root);
installThemeWindow(root);
const READY = h.sent.splice(0, h.sent.length);

const head = () => root.querySelector('.tw-head');
// The window sits at (400, 300) on screen unless a test moves it.
const pointer = (target, type, x, y, extra = {}) => target.dispatchEvent(
    new Event(type, {bubbles: true, clientX: x, clientY: y,
                     screenX: x + 400, screenY: y + 300, pointerId: 1,
                     button: 0, ...extra}));

test('install announces the renderer is live', () => {
  assert.deepEqual(READY, [['themeStudioReady', []]]);
});

test('every name this route sends is registered', () => {
  for (const name of ['themeStudioReady', 'themeStudioDragBy',
                      'themeStudioClose', 'settingsThemePreview',
                      'settingsThemeChanged']) {
    assert.ok(REGISTERED.has(name), `${name} missing from kSurfaceMessages`);
  }
});

test('a payload mounts the studio under the space name', () => {
  assert.equal(window.atriumSetThemeStudio(PAYLOAD), 'studio set');
  assert.equal(root.querySelector('.tw-name').textContent, 'Personal');
  assert.equal(root.querySelectorAll('input[type="range"]').length, 6);
  assert.ok(root.querySelector('canvas'));
});

test('a second payload replaces the first studio', () => {
  window.atriumSetThemeStudio(PAYLOAD);
  window.atriumSetThemeStudio({...PAYLOAD, spaceName: 'Work'});
  assert.equal(root.querySelectorAll('.tw-name').length, 1);
  assert.equal(root.querySelector('.tw-name').textContent, 'Work');
  assert.equal(root.querySelectorAll('canvas').length, 1);
});

test('a slider edit previews on the payload space', () => {
  window.atriumSetThemeStudio(PAYLOAD);
  h.sent.length = 0;
  const slider = root.querySelector('.theme-studio-slider[data-key="opacity"]');
  slider.value = '0.92';
  slider.dispatchEvent(new Event('input', {bubbles: true}));
  // onPreview is coalesced through requestAnimationFrame (theme-studio.js).
  h.tick();
  const preview = h.sent.find(([name]) => name === 'settingsThemePreview');
  assert.ok(preview, 'no preview left the page');
  assert.equal(preview[1][0], PAYLOAD.spaceId);
  assert.equal(preview[1][1].opacity, 0.92);
});

test('dragging the header sends integer deltas and stops on release', () => {
  window.atriumSetThemeStudio(PAYLOAD);
  h.sent.length = 0;
  pointer(head(), 'pointerdown', 100, 20);
  pointer(head(), 'pointermove', 112.6, 25.2);
  pointer(head(), 'pointermove', 120, 25.2);
  pointer(head(), 'pointerup', 120, 25.2);
  pointer(head(), 'pointermove', 200, 200);
  assert.deepEqual(h.sent, [
    ['themeStudioDragBy', [13, 5]],
    ['themeStudioDragBy', [7, 0]],
  ]);
});

test('a window that moved under the pointer does not send a reversed delta',
     () => {
  window.atriumSetThemeStudio(PAYLOAD);
  h.sent.length = 0;
  pointer(head(), 'pointerdown', 100, 20, {screenX: 500, screenY: 300});
  pointer(head(), 'pointermove', 110, 20, {screenX: 510, screenY: 300});
  // The browser moved the window by the 10px it was sent, so the same
  // screen point is now 10px nearer the window's left edge.
  pointer(head(), 'pointermove', 100, 20, {screenX: 510, screenY: 300});
  pointer(head(), 'pointermove', 105, 20, {screenX: 515, screenY: 300});
  pointer(head(), 'pointerup', 105, 20, {screenX: 515, screenY: 300});
  assert.deepEqual(h.sent, [
    ['themeStudioDragBy', [10, 0]],
    ['themeStudioDragBy', [5, 0]],
  ]);
});

test('a drag started on the close button sends nothing', () => {
  window.atriumSetThemeStudio(PAYLOAD);
  h.sent.length = 0;
  const close = root.querySelector('.tw-close');
  pointer(close, 'pointerdown', 10, 10);
  pointer(close, 'pointermove', 30, 30);
  pointer(close, 'pointerup', 30, 30);
  assert.deepEqual(h.sent, []);
});

test('the close button and Escape ask the browser to close', () => {
  window.atriumSetThemeStudio(PAYLOAD);
  h.sent.length = 0;
  root.querySelector('.tw-close').dispatchEvent(
      new Event('click', {bubbles: true}));
  document.dispatchEvent(new Event('keydown', {bubbles: true, key: 'Escape'}));
  assert.deepEqual(h.sent, [['themeStudioClose', []], ['themeStudioClose', []]]);
});
