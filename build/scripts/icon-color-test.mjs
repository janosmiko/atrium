#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The ring takes the favicon's colour, and a grey mark leaves the tint mix.
//
//   node --test build/scripts/icon-color-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

const {dominantColor, ringLookFor, MIN_ICON_CHROMA, contrastRatio, parseRgb,
       ringColorOn, cssRgb, rowBackdrop, RING_MIN_CONTRAST, READABLE_CONTRAST,
       RAMP_STEPS, SEL_WASH_ALPHA, NATIVE_SEL_ALPHA} =
    await import(path.join(ATRIUM_DIR, 'icon-color.js'));

function pixels(list) {
  const out = new Uint8ClampedArray(list.length * 4);
  list.forEach(([r, g, b, a = 255], i) => out.set([r, g, b, a], i * 4));
  return out;
}

test('a red mark on a white ground reads red', () => {
  // YouTube's shape: a small red mark on a white tile. A plain mean is pink.
  const px = pixels([...Array(12).fill([255, 255, 255]), [255, 0, 0],
                     [255, 0, 0]]);
  const [r, g, b] = dominantColor(px);
  assert.ok(r > 200 && g < 80 && b < 80, `read ${r},${g},${b}`);
});

test('a grey mark names no colour', () => {
  const px = pixels([[40, 40, 40], [200, 200, 200], [255, 255, 255]]);
  assert.equal(dominantColor(px), null);
});

test('transparent pixels do not count', () => {
  // A round icon on a transparent tile: the corners must not pull toward
  // black.
  const px = pixels([[0, 0, 0, 0], [0, 0, 0, 0], [30, 120, 220]]);
  assert.deepEqual(dominantColor(px), [30, 120, 220]);
});

test('the chroma floor is what separates the two', () => {
  const low = MIN_ICON_CHROMA - 1;
  assert.equal(dominantColor(pixels([[100, 100, 100 + low]])), null);
  assert.deepEqual(dominantColor(pixels([[100, 100, 100 + low + 1]])),
                   [100, 100, 100 + low + 1]);
});

test('a canvas that cannot be read answers null rather than throwing', () => {
  const doc = {createElement: () => ({getContext: () => null})};
  assert.equal(ringLookFor({src: 'no-ctx'}, {doc}).color, null);
  const throwing = {createElement: () => { throw new Error('no canvas'); }};
  assert.equal(ringLookFor({src: 'throwing'}, {doc: throwing}).color, null);
});

test('a readable canvas yields a CSS colour', () => {
  const data = pixels([[200, 30, 30]]);
  const ctx = {drawImage() {}, getImageData: () => ({data})};
  const doc = {createElement: () => ({getContext: () => ctx})};
  assert.equal(ringLookFor({src: 'readable'}, {doc}).color, 'rgb(200 30 30)');
});

test('ringLookFor memoizes by icon source and backdrop across pushes', () => {
  const data = pixels([[200, 30, 30]]);
  let reads = 0;
  const ctx = {drawImage() {}, getImageData: () => { reads++; return {data}; }};
  const doc = {createElement: () => ({getContext: () => ctx})};
  const push1 = ringLookFor({src: 'https://a.example/favicon.ico'}, {doc});
  const push2 = ringLookFor({src: 'https://a.example/favicon.ico'}, {doc});
  assert.equal(reads, 1);
  assert.deepEqual(push2, push1);
  ringLookFor({src: 'https://b.example/favicon.ico'}, {doc});
  assert.equal(reads, 2);
});

// --- the ring has to be seen on the darkest and lightest tint -------

// The two ends of the preset range: #12282E is the darkest surround the
// appearance settings offer, #F0F4F8 the lightest.
const DARK_TINT = '#12282E';
const LIGHT_TINT = '#F0F4F8';

// Hue in degrees, or null for a grey. The ramp scales the channel DIFFERENCES
// by one factor, so this is what "the favicon colour is kept" means
// numerically.
function hueOf([r, g, b]) {
  const max = Math.max(r, g, b);
  const span = max - Math.min(r, g, b);
  if (!span) {
    return null;
  }
  const h = max === r ? ((g - b) / span) % 6
      : max === g    ? (b - r) / span + 2
                     : (r - g) / span + 4;
  return (h * 60 + 360) % 360;
}

const HUE_TOLERANCE_DEG = 2;

// The ground the ring is actually seen against. Only the focused row rings
// and the focused row is the selected one, which paints --selfill's
// white wash over the tint.
const groundOf = (tint) => rowBackdrop(parseRgb(tint));

function assertKeepsRing(icon, tint) {
  const ground = groundOf(tint);
  const out = ringColorOn(icon, ground);
  const ratio = contrastRatio(out, ground);
  assert.ok(ratio >= RING_MIN_CONTRAST,
            `${cssRgb(out)} on ${cssRgb(ground)} reads ${ratio.toFixed(2)}:1`);
  const drift = Math.abs(hueOf(out) - hueOf(icon));
  assert.ok(Math.min(drift, 360 - drift) <= HUE_TOLERANCE_DEG,
            `hue moved ${drift.toFixed(1)} deg to ${cssRgb(out)}`);
  return out;
}

test('the selected row is the tint under a white wash, not the bare tint', () => {
  assert.deepEqual(rowBackdrop([18, 40, 46]), [89, 105, 109]);
  assert.deepEqual(rowBackdrop([18, 40, 46], 0), [18, 40, 46]);
  assert.deepEqual(rowBackdrop([18, 40, 46], 1), [255, 255, 255]);
  // The native look washes the surround with its ink, not with white.
  assert.deepEqual(rowBackdrop([255, 255, 255], NATIVE_SEL_ALPHA, [32, 33, 36]),
                   [206, 206, 207]);
  assert.equal(rowBackdrop(null), null);
  assert.equal(SEL_WASH_ALPHA, 0.3, 'sidebar.css paints rgba(255,255,255,.3)');
});

test('grading against the bare tint leaves the ring invisible on the row', () => {
  // The defect this pass fixed. A red icon graded against #12282E alone reads
  // 3.01:1 against the tint and 1.12:1 against the row the wash paints.
  const tint = parseRgb(DARK_TINT);
  const ground = groundOf(DARK_TINT);
  const naive = ringColorOn([200, 30, 30], tint);
  assert.ok(contrastRatio(naive, tint) >= RING_MIN_CONTRAST, 'premise');
  assert.ok(contrastRatio(naive, ground) < RING_MIN_CONTRAST,
            `${cssRgb(naive)} would already clear the wash`);
  assert.ok(contrastRatio(ringColorOn([200, 30, 30], ground), ground) >=
                RING_MIN_CONTRAST);
});

test('a red favicon clears 3:1 on the darkest tint and keeps its hue', () => {
  assertKeepsRing([200, 30, 30], DARK_TINT);
});

test('a red favicon clears 3:1 on the lightest tint and keeps its hue', () => {
  assertKeepsRing([200, 30, 30], LIGHT_TINT);
});

test('a dark icon on a dark row is lifted toward white, not left as it was', () => {
  // The case the report is about: a navy mark on the washed near-black row
  // reads 1.4:1 unadjusted, which is the ring nobody could see.
  const icon = [20, 40, 90];
  const ground = groundOf(DARK_TINT);
  assert.ok(contrastRatio(icon, ground) < RING_MIN_CONTRAST, 'pick a fainter icon');
  const out = assertKeepsRing(icon, DARK_TINT);
  assert.ok(out[2] > icon[2], `${cssRgb(out)} is no lighter than the icon`);
});

test('a pale icon on a light row is pushed toward black', () => {
  const icon = [250, 230, 120];
  const ground = groundOf(LIGHT_TINT);
  assert.ok(contrastRatio(icon, ground) < RING_MIN_CONTRAST, 'pick a paler icon');
  const out = assertKeepsRing(icon, LIGHT_TINT);
  assert.ok(out[0] < icon[0], `${cssRgb(out)} is no darker than the icon`);
});

test('an icon that already clears the bar is left alone', () => {
  const bright = ringColorOn([0, 0, 0], groundOf(DARK_TINT));
  assert.deepEqual(bright, [0, 0, 0]);
});

test('the direction follows the same ink threshold the sidebar text uses', () => {
  // InkForBackdrop keeps white ink while white clears READABLE_CONTRAST, and
  // that verdict is also why the chosen end is always reachable: a row too
  // light for white ink is dark enough for black.
  assert.equal(READABLE_CONTRAST, 4.5);
  assert.ok(contrastRatio([255, 255, 255], groundOf(DARK_TINT)) >= READABLE_CONTRAST);
  assert.ok(contrastRatio([255, 255, 255], groundOf(LIGHT_TINT)) < READABLE_CONTRAST);
  assert.ok(contrastRatio([0, 0, 0], groundOf(LIGHT_TINT)) >= RING_MIN_CONTRAST);
});

test('the ramp is fine enough to stop near the bar, not far past it', () => {
  // RAMP_STEPS is the whole reason the answer keeps as much of the icon as it
  // can. A coarse ramp clears the bar too, by overshooting past the colour.
  const icon = [20, 40, 90];
  const ground = groundOf(DARK_TINT);
  const fine = contrastRatio(ringColorOn(icon, ground), ground);
  const coarse = contrastRatio(ringColorOn(icon, ground, {steps: 2}), ground);
  assert.ok(fine >= RING_MIN_CONTRAST && fine < 3.3,
            `the shipped ramp landed at ${fine.toFixed(2)}:1`);
  assert.ok(coarse >= 4, `a 2-step ramp landed at ${coarse.toFixed(2)}:1`);
  assert.ok(RAMP_STEPS >= 32, `${RAMP_STEPS} steps is too coarse`);
});

test('contrastRatio matches the WCAG ends', () => {
  assert.equal(Math.round(contrastRatio([255, 255, 255], [0, 0, 0])), 21);
  assert.equal(contrastRatio([120, 30, 30], [120, 30, 30]), 1);
});

test('parseRgb reads the forms a tint arrives in, and refuses a preset name', () => {
  assert.deepEqual(parseRgb('#12282E'), [18, 40, 46]);
  assert.deepEqual(parseRgb('#abc'), [170, 187, 204]);
  assert.deepEqual(parseRgb('rgb(200 30 30)'), [200, 30, 30]);
  assert.deepEqual(parseRgb('rgba(200, 30, 30, 0.5)'), [200, 30, 30]);
  // The core hands a host's hue over as hsl(), the one form a row's colour
  // arrives in.
  assert.deepEqual(parseRgb('hsl(0,100%,50%)'), [255, 0, 0]);
  assert.deepEqual(parseRgb('hsl(120, 100%, 25%)'), [0, 128, 0]);
  assert.deepEqual(parseRgb('hsl(0 0% 100%)'), [255, 255, 255]);
  // A tint that never resolved to a colour leaves the ring where it was,
  // rather than being adjusted against a guess.
  assert.equal(parseRgb('oceanBlue'), null);
  assert.equal(parseRgb(''), null);
});

test('an unreadable backdrop leaves the icon colour untouched', () => {
  assert.deepEqual(ringColorOn([200, 30, 30], null), [200, 30, 30]);
});

test('ringLookFor adjusts the canvas colour against the row it will sit on', () => {
  const data = pixels([[20, 40, 90]]);
  const ctx = {drawImage() {}, getImageData: () => ({data})};
  const doc = {createElement: () => ({getContext: () => ctx})};
  const ground = groundOf(DARK_TINT);
  const plain = ringLookFor({src: 'lift-test'}, {doc}).color;
  const lifted = ringLookFor({src: 'lift-test'}, {doc, backdrop: ground}).color;
  assert.equal(plain, 'rgb(20 40 90)');
  assert.notEqual(lifted, plain);
  assert.ok(contrastRatio(parseRgb(lifted), ground) >= RING_MIN_CONTRAST,
            `${lifted} is still faint on ${cssRgb(ground)}`);
  // A CSS string is taken as well as a triple: the sidebar hands it one of
  // each, depending on whether the tint parsed.
  assert.equal(ringLookFor({src: 'lift-test'}, {doc, backdrop: cssRgb(ground)}).color, lifted);
});

// --- a multi-colour icon wears its palette, each colour where it sits --------

const {iconPalette, paletteGradient} =
    await import(path.join(ATRIUM_DIR, 'icon-color.js'));

// Four quadrants like the Microsoft mark: red top-left, green top-right,
// blue bottom-left, yellow bottom-right, on a 4x4 grid.
function quadrants() {
  const px = [];
  for (let y = 0; y < 4; y++) {
    for (let x = 0; x < 4; x++) {
      const top = y < 2, left = x < 2;
      px.push(top ? (left ? [240, 60, 40] : [60, 190, 60])
                  : (left ? [30, 100, 230] : [250, 200, 20]));
    }
  }
  return pixels(px);
}

test('a four-colour icon names four colours at their own angles', () => {
  const palette = iconPalette(quadrants(), 4);
  assert.equal(palette.length, 4);
  const hue = ({rgb: [r, g, b]}) => r > g && r > b ? (g > 150 ? 'yellow' : 'red') : g > b ? 'green' : 'blue';
  assert.deepEqual(palette.map(hue), ['green', 'yellow', 'blue', 'red']);
  assert.deepEqual(palette.map((p) => p.angle), [45, 135, 225, 315]);
});

test('a one-colour icon has no palette, so the plain ring stays', () => {
  assert.equal(iconPalette(pixels([[200, 30, 30], [200, 30, 30]]), 2), null);
  assert.equal(iconPalette(pixels([[128, 128, 128]]), 1), null);
});

test('a sliver of a second hue is an edge, not a colour', () => {
  const px = Array.from({length: 40}, () => [200, 30, 30]);
  px.push([30, 100, 230]);
  assert.equal(iconPalette(pixels(px), 41), null);
});

test('the gradient closes the wheel and lifts each stop against the row', () => {
  const ground = groundOf(DARK_TINT);
  const css = paletteGradient([{rgb: [20, 40, 90], angle: 45},
                               {rgb: [240, 60, 40], angle: 225}], ground);
  assert.match(css, /^conic-gradient\(from 0deg, /);
  const stops = css.match(/rgb\([^)]*\) -?\d+deg/g);
  assert.deepEqual(stops.map((s) => s.split(' ').pop()),
                   ['-135deg', '45deg', '225deg', '405deg']);
  assert.equal(stops[0].split(' ').slice(0, 3).join(' '), stops[2].split(' ').slice(0, 3).join(' '));
  for (const stop of stops) {
    const rgb = parseRgb(stop.replace(/ -?\d+deg$/, ''));
    assert.ok(contrastRatio(rgb, ground) >= RING_MIN_CONTRAST, stop);
  }
});

test('ringLookFor\'s gradient reads the canvas and answers null for one colour', () => {
  const four = {drawImage() {}, getImageData: () => ({data: quadrants()})};
  // The reader draws at 16px, so the 4x4 data is read with width 16: the
  // angles are off but the count is what this checks.
  const doc = {createElement: () => ({getContext: () => four})};
  assert.match(ringLookFor({src: 'quad'}, {doc}).gradient, /^conic-gradient/);
  const one = {drawImage() {}, getImageData: () => ({data: pixels([[200, 30, 30]])})};
  assert.equal(ringLookFor({src: 'solo'}, {doc: {createElement: () => ({getContext: () => one})}}).gradient, null);
});

test('a red that straddles the seam of the hue wheel is one colour, not two', () => {
  // 350 and 10 degrees sit in the last and first bucket: one run across the seam.
  const px = [...Array(8).fill([230, 30, 70]), ...Array(8).fill([230, 70, 30]),
              ...Array(16).fill([30, 100, 230])];
  const palette = iconPalette(pixels(px), 8);
  assert.equal(palette.length, 2);
  assert.equal(iconPalette(pixels([]), 0), null);
});
