#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//
//   node --test build/scripts/overlay-shadow-spec-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {parseShadowLayer, splitShadowLayers} = await import(path.join(
    SRC, 'chrome/browser/resources/atrium/overlay-shadow-spec.js'));

test('splitShadowLayers keeps a colour function\'s own commas together', () => {
  const layers = splitShadowLayers(
      'rgba(46, 22, 30, 0.3) 0px 5px 14px 0px, ' +
      'color(srgb 0.8 0.1 0.2 / 0.45) 0px 0px 0px 1px');
  assert.equal(layers.length, 2);
  assert.equal(layers[0], 'rgba(46, 22, 30, 0.3) 0px 5px 14px 0px');
});

test('splitShadowLayers treats none as no layers', () => {
  assert.deepEqual(splitShadowLayers('none'), []);
  assert.deepEqual(splitShadowLayers(''), []);
});

test('parseShadowLayer reads offset, blur and an rgba colour', () => {
  const spec = parseShadowLayer('rgba(46, 22, 30, 0.3) 0px 5px 14px 0px');
  assert.deepEqual(spec, {offsetX: 0, offsetY: 5, blur: 14, r: 46, g: 22, b: 30, a: 0.3});
});

test('parseShadowLayer defaults alpha to 1 for a plain rgb() colour', () => {
  const spec = parseShadowLayer('rgb(10, 20, 30) 0px 4px 12px');
  assert.deepEqual(spec, {offsetX: 0, offsetY: 4, blur: 12, r: 10, g: 20, b: 30, a: 1});
});

test('parseShadowLayer refuses a color() ring layer - not its job', () => {
  assert.equal(
      parseShadowLayer('color(srgb 0.8 0.1 0.2 / 0.45) 0px 0px 0px 1px'),
      null);
});

test('parseShadowLayer refuses a layer with no length values', () => {
  assert.equal(parseShadowLayer('rgba(46, 22, 30, 0.3)'), null);
});
