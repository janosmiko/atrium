#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Theme Studio: the settings module headlessly, no build, no browser.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const dom = installDOM();

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {mountThemeStudio, THEME_PRESETS, rasterizeMesh, harmonyStops} =
    await import(
        path.join(SRC, 'chrome/browser/resources/atrium/theme-studio.js'));

const HEX_RE = /^#[0-9a-fA-F]{6}$/;

function makeSpec(overrides = {}) {
  return {
    name: 'Custom',
    stops: [
      {x: 0.2, y: 0.2, color: '#268BD2', alpha: 0.9},
      {x: 0.8, y: 0.7, color: '#CB4B16', alpha: 0.6},
    ],
    opacity: 0.8,
    effects: {grain: 0.2, vignette: 0.1, frost: 0, warmth: 0.05},
    darkAdaptation: 0.45,
    nightShiftEnabled: true,
    ambientEnabled: false,
    version: 1,
    ...overrides,
  };
}

function mount(t, opts = {}) {
  const container = document.createElement('div');
  document.body.appendChild(container);
  const calls = {preview: [], commit: [], removed: 0};
  const handle = mountThemeStudio(container, {
    spec: makeSpec(),
    spaceName: 'Reading',
    accent: '#7fa8a0',
    onPreview: (spec) => calls.preview.push(spec),
    onCommit: (spec) => calls.commit.push(spec),
    onRemove: () => calls.removed++,
    ...opts,
  });
  t.after(() => {
    handle.destroy();
    container.remove();
  });
  return {container, handle, calls};
}

function slider(container, key) {
  return container.querySelector(`.theme-studio-slider[data-key="${key}"]`);
}

function toggle(container, key) {
  return container.querySelector(`.theme-studio-toggle[data-toggle="${key}"]`);
}

function fireInput(el, value) {
  el.value = String(value);
  el.dispatchEvent(new Event('input', {bubbles: true}));
}

// --- mount --------------------------------------------------------------

test('mount renders a canvas, the stop list, the sliders and both toggles', (t) => {
  const {container} = mount(t);
  assert.equal(container.querySelectorAll('.theme-studio-canvas').length, 1);

  const swatches = container.querySelectorAll('.theme-studio-swatch');
  assert.equal(swatches.length, 2);

  assert.equal(slider(container, 'opacity').value, '0.8');
  assert.equal(slider(container, 'grain').value, '0.2');
  assert.equal(slider(container, 'vignette').value, '0.1');
  assert.equal(slider(container, 'warmth').value, '0.05');
  assert.equal(slider(container, 'darkAdaptation').value, '0.45');

  const nightShift = toggle(container, 'nightShiftEnabled');
  const ambient = toggle(container, 'ambientEnabled');
  assert.equal(nightShift.getAttribute('aria-pressed'), 'true');
  assert.equal(ambient.getAttribute('aria-pressed'), 'false');
});

test('the space name reaches the DOM as text, never markup', (t) => {
  const {container} = mount(t, {spaceName: '<b>Reading</b>'});
  const name = container.querySelector('.theme-studio-space-name');
  assert.equal(name.textContent, '<b>Reading</b>');
  assert.equal(name.querySelectorAll('b').length, 0);
});

test('the theme name is trimmed and capped at 64 characters', (t) => {
  const {handle} = mount(t, {spec: makeSpec({name: `  ${'n'.repeat(100)}  `})});
  assert.equal(handle.spec().name, 'n'.repeat(64));
});

// --- add / remove stop caps ----------------------------------------------

test('add stop caps at 5', (t) => {
  const {container, handle} = mount(t, {spec: makeSpec({stops: [
    {x: 0.1, y: 0.1, color: '#111111', alpha: 1},
    {x: 0.2, y: 0.2, color: '#222222', alpha: 1},
    {x: 0.3, y: 0.3, color: '#333333', alpha: 1},
    {x: 0.4, y: 0.4, color: '#444444', alpha: 1},
    {x: 0.5, y: 0.5, color: '#555555', alpha: 1},
  ]})});
  assert.equal(handle.spec().stops.length, 5);
  container.querySelector('.theme-studio-add-stop').click();
  assert.equal(handle.spec().stops.length, 5,
      'a 6th stop must never be added');
});

test('remove floors at 1', (t) => {
  const {container, handle} = mount(t, {
    spec: makeSpec({stops: [{x: 0.5, y: 0.5, color: '#123456', alpha: 1}]}),
  });
  const removeBtn = container.querySelector('.theme-studio-remove-stop');
  assert.equal(removeBtn, null,
      'the only stop has nothing to remove it down to');
  assert.equal(handle.spec().stops.length, 1);
});

test('removing down to one stop drops the remove button', (t) => {
  const {container} = mount(t, {spec: makeSpec({stops: [
    {x: 0.2, y: 0.2, color: '#111111', alpha: 1},
    {x: 0.8, y: 0.8, color: '#222222', alpha: 1},
  ]})});
  container.querySelector('.theme-studio-remove-stop').click();
  assert.equal(container.querySelectorAll('.theme-studio-swatch').length, 1);
  assert.equal(container.querySelector('.theme-studio-remove-stop'), null);
});

// --- presets ---------------------------------------------------------------

test('a preset click replaces the stops and keeps night shift and ambient', (t) => {
  const {container, handle} = mount(t, {
    spec: makeSpec({nightShiftEnabled: true, ambientEnabled: true}),
  });
  const preset = THEME_PRESETS[3];
  container.querySelector(`.theme-studio-preset[data-preset-index="3"]`)
      .click();
  const spec = handle.spec();
  assert.equal(spec.stops.length, preset.stops.length);
  assert.equal(spec.stops[0].color, preset.stops[0].color.toUpperCase());
  assert.equal(spec.nightShiftEnabled, true,
      'preset identity excludes night shift');
  assert.equal(spec.ambientEnabled, true,
      'preset identity excludes ambient');
});

// --- live preview vs commit --------------------------------------------

test('moving a slider fires onPreview with the new value and never onCommit', (t) => {
  const {container, calls} = mount(t);
  fireInput(slider(container, 'opacity'), 0.3);
  dom.tick();
  assert.equal(calls.commit.length, 0);
  assert.ok(calls.preview.length >= 1);
  assert.equal(calls.preview.at(-1).opacity, 0.3);
});

test('toggling night shift fires onPreview and never onCommit', (t) => {
  const {container, calls} = mount(t);
  toggle(container, 'nightShiftEnabled').click();
  dom.tick();
  assert.equal(calls.commit.length, 0);
  assert.equal(calls.preview.at(-1).nightShiftEnabled, false);
});

test('moving the frost slider fires onPreview with the new frost value', (t) => {
  const {container, calls} = mount(t);
  fireInput(slider(container, 'frost'), 12);
  dom.tick();
  assert.ok(calls.preview.length >= 1);
  assert.equal(calls.preview.at(-1).effects.frost, 12);
});

test('ten moves in one frame send one preview, and it is the last value', (t) => {
  const {container, calls} = mount(t);
  for (let i = 1; i <= 10; i++) {
    fireInput(slider(container, 'opacity'), i / 10);
  }
  assert.equal(calls.preview.length, 0, 'a send before the frame ran');
  dom.tick();
  assert.equal(calls.preview.length, 1);
  assert.equal(calls.preview[0].opacity, 1);
});

// --- dot drag / dblclick / right-click -----------------------------------

function dot(container, i) {
  return container.querySelector(`.theme-studio-dot[data-stop-index="${i}"]`);
}

test('dragging a dot moves the stop and fires onPreview', (t) => {
  const {container, handle, calls} = mount(t);
  const canvasWrap = container.querySelector('.theme-studio-canvas-wrap');
  canvasWrap.setRect({left: 0, top: 0, width: 200, height: 100});
  const d = dot(container, 0);
  d.dispatchEvent(new Event('pointerdown', {pointerId: 1, clientX: 40, clientY: 20, bubbles: true}));
  d.dispatchEvent(new Event('pointermove', {pointerId: 1, clientX: 100, clientY: 60, bubbles: true}));
  d.dispatchEvent(new Event('pointerup', {pointerId: 1, clientX: 100, clientY: 60, bubbles: true}));
  const stop = handle.spec().stops[0];
  assert.equal(stop.x, 0.5);
  assert.equal(stop.y, 0.6);
  dom.tick();
  assert.ok(calls.preview.length >= 1);
  assert.equal(calls.preview.at(-1).stops[0].x, 0.5);
});

test('a cancelled drag ends the gesture like pointerup', (t) => {
  const {container, handle} = mount(t);
  const canvasWrap = container.querySelector('.theme-studio-canvas-wrap');
  canvasWrap.setRect({left: 0, top: 0, width: 200, height: 100});
  const d = dot(container, 0);
  d.dispatchEvent(new Event('pointerdown', {pointerId: 1, clientX: 40, clientY: 20, bubbles: true}));
  d.dispatchEvent(new Event('pointercancel', {pointerId: 1, bubbles: true}));
  const xAfterCancel = handle.spec().stops[0].x;
  d.dispatchEvent(new Event('pointermove', {pointerId: 1, clientX: 180, clientY: 90, bubbles: true}));
  assert.equal(handle.spec().stops[0].x, xAfterCancel,
      'a move after pointercancel must not still be bound');
});

test('pressing an unselected dot selects and drags it in one gesture', (t) => {
  const {container, handle} = mount(t);
  const canvasWrap = container.querySelector('.theme-studio-canvas-wrap');
  canvasWrap.setRect({left: 0, top: 0, width: 200, height: 100});
  const d = dot(container, 1);
  d.dispatchEvent(new Event('pointerdown', {pointerId: 1, clientX: 160, clientY: 70, bubbles: true}));
  d.dispatchEvent(new Event('pointermove', {pointerId: 1, clientX: 20, clientY: 10, bubbles: true}));
  d.dispatchEvent(new Event('pointerup', {pointerId: 1, clientX: 20, clientY: 10, bubbles: true}));
  assert.equal(handle.spec().stops[1].x, 0.1);
  assert.equal(handle.spec().stops[1].y, 0.1);
});

test('double-click on empty canvas adds a stop', (t) => {
  const {container, handle} = mount(t);
  const canvasWrap = container.querySelector('.theme-studio-canvas-wrap');
  canvasWrap.setRect({left: 0, top: 0, width: 200, height: 100});
  const before = handle.spec().stops.length;
  canvasWrap.dispatchEvent(new Event('dblclick', {clientX: 100, clientY: 50, bubbles: true}));
  assert.equal(handle.spec().stops.length, before + 1);
});

test('right-click on a dot removes its stop', (t) => {
  const {container, handle} = mount(t);
  const before = handle.spec().stops.length;
  dot(container, 0).dispatchEvent(new Event('contextmenu', {bubbles: true}));
  assert.equal(handle.spec().stops.length, before - 1);
});

test('randomize yields 2..4 valid stops', (t) => {
  const {container, handle} = mount(t);
  container.querySelector('.theme-studio-randomize').click();
  const stops = handle.spec().stops;
  assert.ok(stops.length >= 2 && stops.length <= 4, `got ${stops.length} stops`);
  for (const stop of stops) {
    assert.match(stop.color, HEX_RE);
  }
});

test('Save fires onCommit once with a sanitized spec', (t) => {
  const {container, calls} = mount(t, {
    spec: makeSpec({opacity: 4, effects: {grain: -1, vignette: 2, frost: 999, warmth: 8}}),
  });
  container.querySelector('.theme-studio-save').click();
  assert.equal(calls.commit.length, 1);
  const spec = calls.commit[0];
  assert.equal(spec.opacity, 1);
  assert.equal(spec.effects.grain, 0);
  assert.equal(spec.effects.vignette, 1);
  assert.equal(spec.effects.frost, 30);
  assert.equal(spec.effects.warmth, 1);
});

test('Remove fires onRemove', (t) => {
  const {container, calls} = mount(t);
  container.querySelector('.theme-studio-remove').click();
  assert.equal(calls.removed, 1);
});

test('destroy leaves the container empty', (t) => {
  const {container, handle} = mount(t);
  handle.destroy();
  assert.equal(container.childNodes.length, 0);
});

// --- rasterizeMesh -----------------------------------------------------

test('rasterizeMesh of a one-stop spec is that colour', (t) => {
  const spec = makeSpec({
    stops: [{x: 0.5, y: 0.5, color: '#336699', alpha: 1}],
    opacity: 1,
    effects: {grain: 0, vignette: 0, frost: 0, warmth: 0},
  });
  const buf = rasterizeMesh(spec, 4, 4);
  for (let i = 0; i < buf.length; i += 4) {
    assert.equal(buf[i], 0x33);
    assert.equal(buf[i + 1], 0x66);
    assert.equal(buf[i + 2], 0x99);
    assert.equal(buf[i + 3], 255);
  }
});

test('rasterizeMesh returns a straight-alpha RGBA8 buffer of the right size', (t) => {
  const buf = rasterizeMesh(makeSpec(), 16, 9);
  assert.equal(buf.length, 16 * 9 * 4);
});

test('the dark option pulls a solid colour toward black', (t) => {
  const spec = makeSpec({
    stops: [{x: 0.5, y: 0.5, color: '#FF0000', alpha: 1}],
    opacity: 1, darkAdaptation: 1,
    effects: {grain: 0, vignette: 0, frost: 0, warmth: 0},
  });
  const plain = rasterizeMesh(spec, 2, 2);
  const dark = rasterizeMesh(spec, 2, 2, {dark: true});
  assert.deepEqual([plain[0], plain[1], plain[2]], [255, 0, 0]);
  assert.deepEqual([dark[0], dark[1], dark[2]], [67, 10, 10]);
});

test('the warmth option shifts red up and blue down', (t) => {
  const spec = makeSpec({
    stops: [{x: 0.5, y: 0.5, color: '#808080', alpha: 1}],
    opacity: 1, darkAdaptation: 0,
    effects: {grain: 0, vignette: 0, frost: 0, warmth: 0},
  });
  const plain = rasterizeMesh(spec, 2, 2);
  const warm = rasterizeMesh(spec, 2, 2, {warmth: 1});
  assert.deepEqual([plain[0], plain[1], plain[2]], [128, 128, 128]);
  assert.deepEqual([warm[0], warm[1], warm[2]], [143, 128, 113]);
});

test('the frost effect smooths a hard edge between two stops', (t) => {
  const stops = [
    {x: 0, y: 0.5, color: '#FF0000', alpha: 1},
    {x: 1, y: 0.5, color: '#0000FF', alpha: 1},
  ];
  const w = 32, h = 16, row = Math.floor(h / 2), x = Math.floor(w / 2);
  const at = (buf, px) => buf[(row * w + px) * 4];
  const sharpSpec = makeSpec({stops, opacity: 1, darkAdaptation: 0,
      effects: {grain: 0, vignette: 0, frost: 0, warmth: 0}});
  const blurredSpec = makeSpec({stops, opacity: 1, darkAdaptation: 0,
      effects: {grain: 0, vignette: 0, frost: 40, warmth: 0}});
  const sharp = rasterizeMesh(sharpSpec, w, h);
  const blurred = rasterizeMesh(blurredSpec, w, h);
  const sharpDiff = Math.abs(at(sharp, x) - at(sharp, x + 1));
  const blurredDiff = Math.abs(at(blurred, x) - at(blurred, x + 1));
  assert.equal(sharpDiff, 20);
  assert.equal(blurredDiff, 8);
  assert.ok(blurredDiff < sharpDiff);
});

test('the vignette effect darkens the corner but not the centre', (t) => {
  const stops = [{x: 0.5, y: 0.5, color: '#AAAAAA', alpha: 1}];
  const w = 32, h = 32;
  const channelSum = (buf, px, py) => {
    const i = (py * w + px) * 4;
    return buf[i] + buf[i + 1] + buf[i + 2];
  };
  const flatSpec = makeSpec({stops, opacity: 1, darkAdaptation: 0,
      effects: {grain: 0, vignette: 0, frost: 0, warmth: 0}});
  const vignettedSpec = makeSpec({stops, opacity: 1, darkAdaptation: 0,
      effects: {grain: 0, vignette: 0.8, frost: 0, warmth: 0}});
  const flat = rasterizeMesh(flatSpec, w, h);
  const vignetted = rasterizeMesh(vignettedSpec, w, h);
  assert.equal(channelSum(flat, 0, 0), channelSum(flat, w / 2, h / 2));
  assert.ok(channelSum(vignetted, 0, 0) < channelSum(vignetted, w / 2, h / 2));
  assert.equal(channelSum(vignetted, 0, 0), 420);
  assert.equal(channelSum(vignetted, w / 2, h / 2), 510);
});

test('ambientTime rotates the mesh and swaps the sampled colours', (t) => {
  const spec = makeSpec({
    stops: [
      {x: 0.2, y: 0.5, color: '#FF0000', alpha: 1},
      {x: 0.8, y: 0.5, color: '#0000FF', alpha: 1},
    ],
    opacity: 1, darkAdaptation: 0,
    effects: {grain: 0, vignette: 0, frost: 0, warmth: 0},
  });
  const w = 40, h = 20;
  const sx = Math.round(0.2 * (w - 1)), sy = Math.round(0.5 * (h - 1));
  const idx = (sy * w + sx) * 4;
  const plain = rasterizeMesh(spec, w, h);
  const ambient = rasterizeMesh(spec, w, h, {ambientTime: 74.78});
  assert.deepEqual([plain[idx], plain[idx + 1], plain[idx + 2]], [255, 0, 0]);
  assert.ok(ambient[idx] < 10, `red channel should collapse, got ${ambient[idx]}`);
  assert.ok(ambient[idx + 2] > 240, `blue channel should dominate, got ${ambient[idx + 2]}`);
});

// --- harmonyStops --------------------------------------------------------

test('harmonyStops returns 2..4 stops with valid hex', (t) => {
  const stops = harmonyStops();
  assert.ok(stops.length >= 2 && stops.length <= 4, `got ${stops.length} stops`);
  for (const stop of stops) {
    assert.match(stop.color, HEX_RE);
    assert.ok(stop.x >= 0 && stop.x <= 1);
    assert.ok(stop.y >= 0 && stop.y <= 1);
    assert.ok(stop.alpha > 0 && stop.alpha <= 1);
  }
});

test.after(() => dom.reset());
