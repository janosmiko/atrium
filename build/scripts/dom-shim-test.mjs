#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// parentElement, matchMedia, CSS.escape - each had a per-test
// stand-in because none were in dom-shim.mjs.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

const h = installDOM();

test('parentElement returns the nearest element ancestor', () => {
  const parent = document.createElement('div');
  const child = document.createElement('span');
  parent.appendChild(child);
  assert.equal(child.parentElement, parent);
});

test('parentElement is null for a node with no parent', () => {
  const el = document.createElement('div');
  assert.equal(el.parentElement, null);
});

test('parentElement is null when the parent is a document fragment', () => {
  const frag = document.createDocumentFragment();
  const el = document.createElement('div');
  frag.appendChild(el);
  assert.equal(el.parentElement, null);
});

test('matchMedia defaults to no match', () => {
  h.reset();
  assert.equal(globalThis.matchMedia('(prefers-reduced-motion: reduce)').matches, false);
});

test('setMatchMedia drives the result a caller reads back', () => {
  h.reset();
  h.setMatchMedia((query) => query.includes('reduce'));
  assert.equal(globalThis.matchMedia('(prefers-reduced-motion: reduce)').matches, true);
  assert.equal(globalThis.matchMedia('(prefers-color-scheme: dark)').matches, false);
});

test('reset() restores matchMedia to its no-match default', () => {
  h.setMatchMedia(() => true);
  h.reset();
  assert.equal(globalThis.matchMedia('(prefers-reduced-motion: reduce)').matches, false);
});

// Reference values from the CSSOM serialize-an-identifier algorithm - the
// same cases the widely-used css.escape polyfill ships as its own test suite.
for (const [input, expected] of [
  ['', ''],
  ['0', '\\30 '],
  ['1a2b3c', '\\31 a2b3c'],
  ['-1a2b3c', '-\\31 a2b3c'],
  ['--a', '--a'],
  ['-', '\\-'],
  ['\0', '�'],
  ['a\0b', 'a�b'],
  ['.foo#bar', '\\.foo\\#bar'],
  ['()[]{}', '\\(\\)\\[\\]\\{\\}'],
]) {
  test(`CSS.escape(${JSON.stringify(input)}) === ${JSON.stringify(expected)}`, () => {
    assert.equal(globalThis.CSS.escape(input), expected);
  });
}

test('CSS.escape leaves an ordinary folder id untouched', () => {
  assert.equal(globalThis.CSS.escape('folder-a1'), 'folder-a1');
});

// Known browser API names the shipped modules are known to call, mapped to
// how the shim exposes each. `token` is the substring grepped for in module
// source; `present` asserts the shim's side of the contract.
const ATRIUM_APIS = [
  {token: 'ResizeObserver', present: () => typeof globalThis.ResizeObserver === 'function'},
  {token: 'matchMedia', present: () => typeof globalThis.matchMedia === 'function'},
  {token: 'CSS.escape', present: () => typeof globalThis.CSS?.escape === 'function'},
  {token: '.parentElement',
   present: () => 'parentElement' in Object.getPrototypeOf(document.createElement('div'))},
  {token: 'requestAnimationFrame',
   present: () => typeof globalThis.requestAnimationFrame === 'function'},
  {token: 'cancelAnimationFrame',
   present: () => typeof globalThis.cancelAnimationFrame === 'function'},
  {token: 'setTimeout', present: () => typeof globalThis.setTimeout === 'function'},
  {token: 'clearTimeout', present: () => typeof globalThis.clearTimeout === 'function'},
  {token: 'setInterval', present: () => typeof globalThis.setInterval === 'function'},
  {token: 'clearInterval', present: () => typeof globalThis.clearInterval === 'function'},
  {token: 'getComputedStyle', present: () => typeof globalThis.getComputedStyle === 'function'},
  {token: 'performance.now', present: () => typeof globalThis.performance?.now === 'function'},
  {token: 'chrome.send', present: () => typeof globalThis.chrome?.send === 'function'},
  {token: 'CustomEvent', present: () => typeof globalThis.CustomEvent === 'function'},
  {token: 'getBoundingClientRect',
   present: () => typeof document.createElement('div').getBoundingClientRect === 'function'},
  {token: 'addEventListener',
   present: () => typeof document.createElement('div').addEventListener === 'function'},
];

let sourceCache = null;
function allModuleSource() {
  if (sourceCache) {
    return sourceCache;
  }
  const files = fs.readdirSync(ATRIUM_DIR).filter((f) => f.endsWith('.js'));
  sourceCache = files.map((f) => fs.readFileSync(path.join(ATRIUM_DIR, f), 'utf8')).join('\n');
  return sourceCache;
}

test('every shim-covered API a shipped module actually calls is present', () => {
  const source = allModuleSource();
  const checked = [];
  for (const api of ATRIUM_APIS) {
    if (!source.includes(api.token)) {
      continue;
    }
    checked.push(api.token);
    assert.ok(api.present(), `shim is missing ${api.token}, which a shipped module calls`);
  }
  // A regression in the module scan itself (a renamed directory, an empty
  // read) would make every assertion above vacuously pass - guard that too.
  assert.ok(checked.length >= 10,
      `expected most of the ${ATRIUM_APIS.length} known APIs to show up in ` +
      `shipped modules, only matched: ${checked.join(', ')}`);
});
