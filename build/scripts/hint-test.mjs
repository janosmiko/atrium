#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// hint.js: one bubble at a time, on the shared overlay layer, on a fake clock.
//
//   node --test build/scripts/hint-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const h = installDOM({pathname: '/sidebar'});
const {showHint, hideHint, currentHint, installHintOverlay, hintTop} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/hint.js'));
const {makeFakeClock} = await import('./fake-clock.mjs');

function fresh() {
  hideHint();
  document.body.replaceChildren();
  const root = document.createElement('div');
  root.id = 'atrium-root';
  document.body.appendChild(root);
  return makeFakeClock();
}

test('mouse hints use the compact floating-surface radius', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/hint.css'), 'utf8');
  assert.match(css,
      /\.hint\s*\{[^}]*border-radius:\s*var\(--ov-radius-compact\)/);
});

test('a hint shows its text and one cap per key', () => {
  const clock = fresh();
  const el = showHint({text: 'Hold to open in a peek', keys: '⇧click'}, {clock});
  assert.equal(el.classList.contains('ov-hint'), true);
  assert.equal(el.classList.contains('ov-card'), true);
  assert.equal(el.querySelector('.hint-text').textContent, 'Hold to open in a peek');
  const caps = [...el.querySelectorAll('.hint-keys .ov-key')].map((c) => c.textContent);
  assert.deepEqual(caps, ['⇧', 'click']);
});

test('the bubble mounts beside the root, not inside it', () => {
  const clock = fresh();
  const el = showHint({text: 'x'}, {clock});
  assert.equal(el.parentNode, document.body);
  assert.equal(el.nextElementSibling, document.getElementById('atrium-root'));
});

test('a second hint replaces the first', () => {
  const clock = fresh();
  showHint({text: 'one'}, {clock});
  showHint({text: 'two'}, {clock});
  assert.equal(document.querySelectorAll('.hint').length, 1);
  assert.equal(currentHint().querySelector('.hint-text').textContent, 'two');
});

test('a hint expires after ttlMs and not before', () => {
  const clock = fresh();
  showHint({text: 'x', ttlMs: 1000}, {clock});
  clock.advance(999);
  assert.ok(currentHint());
  clock.advance(1);
  assert.equal(currentHint(), null);
});

test('hideHint removes the bubble and cancels the timer', () => {
  const clock = fresh();
  showHint({text: 'x', ttlMs: 1000}, {clock});
  hideHint();
  assert.equal(currentHint(), null);
  clock.advance(1000);
  assert.equal(document.querySelectorAll('.hint').length, 0);
});

test('a named anchor lands in the dataset, an element anchor reads as element', () => {
  const clock = fresh();
  assert.equal(showHint({text: 'x', anchor: 'top-right'}, {clock}).dataset.hintAnchor, 'top-right');
  const row = document.createElement('div');
  row.setRect({left: 10, top: 20, width: 100, height: 30});
  assert.equal(showHint({text: 'x', anchor: row}, {clock}).dataset.hintAnchor, 'element');
});

test('an element anchor near the bottom clamps the bubble into the viewport', () => {
  const clock = fresh();
  window.innerHeight = 400;
  const row = document.createElement('div');
  row.setRect({left: 10, top: 390, width: 100, height: 20});
  const el = showHint({text: 'x', anchor: row}, {clock});
  const top = parseFloat(el.style['--hint-top']);
  assert.ok(top <= 400, `top ${top} must not exceed window.innerHeight`);
});

// The shim has no engine, so a bubble showHint builds measures 0 tall and can
// never exercise the flip through place(). hintTop is the geometry itself.
test('hintTop sits below the anchor while the bubble fits there', () => {
  assert.equal(hintTop({top: 100, height: 26}, 28, 660), 134);
});

// The measured AI panel geometry: below fits the viewport, so only an asked-for
// side moves the bubble off the prompt field.
test('hintTop honours a request to sit above the anchor', () => {
  assert.equal(hintTop({top: 600, height: 26}, 31, 680, 'above'), 561);
  assert.equal(hintTop({top: 600, height: 26}, 31, 680), 634);
});

test('hintTop gives way to the other side rather than leave the viewport', () => {
  assert.equal(hintTop({top: 10, height: 26}, 31, 680, 'above'), 44);
  assert.equal(hintTop({top: 640, height: 26}, 31, 680), 601);
});

test('hintTop clamps into the viewport when neither side has room', () => {
  const top = hintTop({top: 2, height: 640}, 28, 660);
  assert.ok(top >= 8 && top + 28 <= 660, `top ${top} left the viewport`);
});

test('a plain rect anchor (from HintOverlayWidget) places the same as an element would', () => {
  const clock = fresh();
  window.innerWidth = 800;
  window.innerHeight = 600;
  const el = showHint({text: 'x', anchor: {left: 10, top: 20, width: 0, height: 0}},
                       {clock});
  assert.equal(el.dataset.hintAnchor, 'element');
  assert.equal(el.style['--hint-left'], '10px');
});

test('native: an element anchor sends sidebarHintShow and returns no DOM node', () => {
  const clock = fresh();
  h.sent.length = 0;
  const row = document.createElement('div');
  row.setRect({left: 3, top: 4, width: 50, height: 12});
  const marker = showHint({text: 'x', keys: 'hold', anchor: row, ttlMs: 1200},
                           {clock, native: 'sidebar'});
  assert.equal(document.querySelectorAll('.hint').length, 0);
  assert.deepEqual(h.sent, [['sidebarHintShow', ['x', 'hold', 3, 4, 50, 12, 1200]]]);
  assert.equal(currentHint(), marker);
  hideHint();
  assert.deepEqual(h.sent.at(-1), ['sidebarHintHide', []]);
  assert.equal(currentHint(), null);
});

// Its own message pair, because the window translates the
// anchor rect out of the sending surface's client space and the floating bar
// sits wherever it floats.
test('native: the devBar surface sends its own message pair', () => {
  const clock = fresh();
  h.sent.length = 0;
  const button = document.createElement('div');
  button.setRect({left: 7, top: 8, width: 22, height: 22});
  showHint({text: 'Back', keys: '⌘[', anchor: button, ttlMs: 4000},
            {clock, native: 'devBar'});
  assert.equal(document.querySelectorAll('.hint').length, 0);
  assert.deepEqual(
      h.sent, [['devBarHintShow', ['Back', '⌘[', 7, 8, 22, 22, 4000]]]);
  hideHint();
  assert.deepEqual(h.sent.at(-1), ['devBarHintHide', []]);
});

test('native: a string anchor still renders locally', () => {
  const clock = fresh();
  h.sent.length = 0;
  const el = showHint({text: 'x', anchor: 'top-right'}, {clock, native: 'sidebar'});
  assert.equal(el.dataset.hintAnchor, 'top-right');
  assert.deepEqual(h.sent, []);
});

test('installHintOverlay wires the native push and announces hintReady', () => {
  fresh();
  h.sent.length = 0;
  installHintOverlay();
  assert.deepEqual(h.sent, [['hintReady', []]]);
  window.atriumShowHint({text: 'y'});
  assert.equal(currentHint().querySelector('.hint-text').textContent, 'y');
  window.atriumHideHint();
  assert.equal(currentHint(), null);
});

test('the native messages are registered with the browser', () => {
  const registered = new Set(
      fs.readFileSync(
            path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
            'utf8')
          .split('kSurfaceMessages[] = {')[1]
          .split('};')[0]
          .match(/"[^"]+"/g)
          .map((quoted) => quoted.slice(1, -1)));
  for (const name of ['sidebarHintShow', 'sidebarHintHide', 'devBarHintShow',
                      'devBarHintHide', 'hintReady']) {
    assert.ok(registered.has(name), `${name} is fatal, not registered`);
  }
});

// two hover hints side by side did not line up, because the card
// took the height of its tallest child and a keycap is taller than a line of
// text. The shim has no engine, so the box the sheet asks for is the measure.
function ovRule(css, selector) {
  const at = css.indexOf(`\n${selector} {`);
  assert.notEqual(at, -1, `${selector} is gone from overlay.css`);
  return css.slice(at, css.indexOf('}', at));
}

function pxOf(rule, prop) {
  const found = new RegExp(`${prop}:\\s*([\\d.]+)px`).exec(rule);
  return found ? parseFloat(found[1]) : 0;
}

// Padding, line box and the bottom border a keycap draws its edge with.
function keycapHeight(rule) {
  const pad = /padding:\s*([\d.]+)px/.exec(rule);
  const font = /font:[^;]*?\/([\d.]+)/.exec(rule);
  const line = font ? parseFloat(font[1]) : 1;
  const size = /font:[^;]*?([\d.]+)px\//.exec(rule);
  return (pad ? parseFloat(pad[1]) : 0) * 2 +
      (size ? parseFloat(size[1]) : 0) * line + pxOf(rule, 'border-bottom');
}

test('the hint card fixes its own height, so a keycap cannot grow it', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/overlay.css'), 'utf8');
  const hint = ovRule(css, '.ov-hint');
  const height = pxOf(hint, 'height');
  assert.ok(height > 0, '.ov-hint sizes to its content, so a keyed bubble is taller');
  assert.match(hint, /box-sizing:\s*border-box/);
  for (const selector of ['.ov-key', 'html.atrium-native-chrome .ov-key']) {
    const cap = keycapHeight(ovRule(css, selector));
    assert.ok(cap <= height,
              `${selector} is ${cap}px inside a ${height}px card, so it squashes`);
  }
});

// the AI panel delegates its hovers from #atrium-root, so `host`
// there IS the root. A bubble appended to it read .aipanelroot's glass paper
// rather than the overlay card the sidebar's hint wears.
test('a host inside the root still mounts the bubble beside the root', () => {
  const clock = fresh();
  const root = document.querySelector('#atrium-root');
  const el = showHint({text: 'Bullet points'}, {clock, host: root});
  assert.equal(el.parentNode, document.body);
  assert.equal(el.nextElementSibling, root);
});
