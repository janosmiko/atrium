#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const FILE = path.join(SRC, 'chrome/browser/resources/atrium/phosphor/fill.js');
const LICENSE = path.join(SRC, 'chrome/browser/resources/atrium/phosphor/LICENSE');

const GRID = [
  'star', 'bookmark-simple', 'heart', 'flag', 'lightning', 'triangle',
  'asterisk', 'bell', 'lightbulb', 'shapes', 'squares-four', 'dots-nine',
  'stack', 'database', 'archive', 'copy', 'folder', 'tray', 'calendar',
  'envelope', 'check-square', 'file', 'book-open', 'chat-circle-dots',
  'users', 'terminal-window', 'wrench', 'square', 'egg', 'circle', 'moon',
  'sun', 'planet', 'leaf', 'cloud', 'paw-print', 'basket', 'gift', 'bed',
  'fork-knife', 'barbell', 'airplane', 'music-note', 'palette',
  'video-camera', 'bandaids', 'code', 'baseball', 'map-trifold', 'campfire',
  'pizza', 'skull', 'receipt', 'thumbs-up', 'train',
];

const {PHOSPHOR_NAMES, PHOSPHOR_SHAPES} = await import(FILE);

test('every grid name is present, in order', () => {
  assert.deepEqual(PHOSPHOR_NAMES, GRID);
});

test('every grid name has a shape entry with only path/circle/rect tags', () => {
  for (const name of GRID) {
    const shapes = PHOSPHOR_SHAPES[name];
    assert.ok(Array.isArray(shapes) && shapes.length >= 1, name);
    for (const [tag, attrs] of shapes) {
      assert.ok(['path', 'circle', 'rect'].includes(tag), `${name}: ${tag}`);
      assert.equal(typeof attrs, 'object', name);
    }
  }
});

test('no shape carries a script/href/event-handler attribute', () => {
  for (const [name, shapes] of Object.entries(PHOSPHOR_SHAPES)) {
    for (const [, attrs] of shapes) {
      for (const key of Object.keys(attrs)) {
        assert.ok(!/^on/i.test(key), `${name}: ${key}`);
        assert.notEqual(key, 'href', name);
        assert.notEqual(key, 'xlink:href', name);
      }
    }
  }
});

test('the module never touches innerHTML', () => {
  assert.doesNotMatch(
      fs.readFileSync(FILE, 'utf8'), /\.innerHTML\s*=|innerHTML\s*\+=/);
});

test('the MIT license ships beside the vendored glyphs', () => {
  assert.match(fs.readFileSync(LICENSE, 'utf8'), /MIT License/);
});
