#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const FILE = path.join(SRC, 'chrome/browser/resources/atrium/space-glyph.js');

installDOM();
const {spaceGlyph} = await import(FILE);

test('an emoji value renders as a text node', () => {
  const node = spaceGlyph('🗂️', 'Work');
  assert.equal(node.nodeType, 3);
  assert.equal(node.textContent, '🗂️');
});

test('a plain-text value renders as a text node', () => {
  const node = spaceGlyph('W', 'Work');
  assert.equal(node.nodeType, 3);
  assert.equal(node.textContent, 'W');
});

test('a known ph: value builds an svg with data-icon and no innerHTML use', () => {
  const node = spaceGlyph('ph:star', 'Work');
  assert.equal(node.tagName.toLowerCase(), 'svg');
  assert.equal(node.getAttribute('viewBox'), '0 0 256 256');
  assert.equal(node.getAttribute('fill'), 'currentColor');
  assert.equal(node.dataset.icon, 'star');
  assert.ok(node.querySelectorAll('path, circle, rect').length >= 1);
});

test('an unknown ph: name falls back to the first letter of the space name', () => {
  const node = spaceGlyph('ph:not-a-real-icon', 'Work');
  assert.equal(node.nodeType, 3);
  assert.equal(node.textContent, 'W');
});

test('a prototype-key ph: name falls back rather than resolving off the prototype', () => {
  const node = spaceGlyph('ph:__proto__', 'Work');
  assert.equal(node.nodeType, 3);
  assert.equal(node.textContent, 'W');
});

test('an empty value falls back to the first letter of the space name', () => {
  const node = spaceGlyph('', 'Work');
  assert.equal(node.nodeType, 3);
  assert.equal(node.textContent, 'W');
});

test('an empty value with no space name falls back to an empty string', () => {
  const node = spaceGlyph('', '');
  assert.equal(node.nodeType, 3);
  assert.equal(node.textContent, '');
});
