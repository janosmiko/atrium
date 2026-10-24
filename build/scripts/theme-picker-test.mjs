#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// The hue-ring/SV-square picker headlessly.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const dom = installDOM();

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {mountColorPicker} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/theme-picker.js'));

const CENTER = 95;
const RING_RADIUS = 81;

function mountPicker(t, opts = {}) {
  const changes = [];
  const picker = mountColorPicker({onChange: (c) => changes.push(c), ...opts});
  t.after(() => picker.destroy());
  const root = document.body.querySelector('.theme-picker');
  const canvas = root.querySelector('.theme-picker-canvas');
  canvas.setRect({left: 0, top: 0, width: 190, height: 190});
  return {picker, root, canvas, changes};
}

function anchorEl() {
  const anchor = document.createElement('div');
  anchor.setRect({left: 10, top: 10, width: 20, height: 20});
  document.body.appendChild(anchor);
  return anchor;
}

test('open seeds the hex and alpha controls from the given colour', (t) => {
  const {picker, root} = mountPicker(t);
  picker.open(anchorEl(), '#FF0000', 0.5);
  assert.equal(root.hidden, false);
  assert.equal(root.querySelector('.theme-picker-hex').value, '#FF0000');
  assert.equal(root.querySelector('.theme-picker-alpha').value, '50');
  assert.equal(root.querySelector('.theme-picker-alpha-value').textContent, '50%');
});

test('dragging the hue ring to 120 degrees reports pure green', (t) => {
  const {picker, canvas, changes} = mountPicker(t);
  picker.open(anchorEl(), '#FF0000', 1);
  // A point on the ring at angle 120deg from the centre (atan2 convention,
  // same one drawPicker uses to place the hue swatch on the ring).
  const dx = RING_RADIUS * Math.cos((120 * Math.PI) / 180);
  const dy = RING_RADIUS * Math.sin((120 * Math.PI) / 180);
  canvas.dispatchEvent(new Event('pointerdown', {
    pointerId: 1, clientX: CENTER + dx, clientY: CENTER + dy, bubbles: true,
  }));
  assert.equal(changes.length, 1);
  assert.equal(changes[0].hex, '#00ff00');
  assert.equal(changes[0].alpha, 1);
});

test('dragging inside the SV square to s=1,v=0.5 halves the value channel', (t) => {
  const {picker, canvas, changes} = mountPicker(t);
  picker.open(anchorEl(), '#FF0000', 1);
  // The right edge at vertical centre is s=1, v=0.5. At hue 0 that is
  // half-value pure red.
  canvas.dispatchEvent(new Event('pointerdown', {
    pointerId: 1, clientX: CENTER + 48, clientY: CENTER, bubbles: true,
  }));
  assert.equal(changes.at(-1).hex, '#800000');
});

test('the alpha slider reports the fraction, not the percent', (t) => {
  const {root, changes} = mountPicker(t);
  const alphaInput = root.querySelector('.theme-picker-alpha');
  alphaInput.value = '42';
  alphaInput.dispatchEvent(new Event('input', {bubbles: true}));
  assert.equal(changes.at(-1).alpha, 0.42);
  assert.equal(root.querySelector('.theme-picker-alpha-value').textContent, '42%');
});

test('typing a hex round-trips to the same colour', (t) => {
  const {root, changes} = mountPicker(t);
  const hexInput = root.querySelector('.theme-picker-hex');
  hexInput.value = '#00ff00';
  hexInput.dispatchEvent(new Event('change', {bubbles: true}));
  assert.equal(changes.at(-1).hex, '#00ff00');
});

test('a click outside the picker closes it', (t) => {
  const {picker, root} = mountPicker(t);
  picker.open(anchorEl(), '#FF0000', 1);
  assert.equal(root.hidden, false);
  document.body.dispatchEvent(new Event('pointerdown', {bubbles: true}));
  assert.equal(root.hidden, true);
});

test('destroy removes the body-level node', (t) => {
  const picker = mountColorPicker({});
  t.after(() => picker.destroy());
  assert.ok(document.body.querySelector('.theme-picker'));
  picker.destroy();
  assert.equal(document.body.querySelector('.theme-picker'), null);
});

test.after(() => dom.reset());
