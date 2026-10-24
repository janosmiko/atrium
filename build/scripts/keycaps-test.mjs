#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// keycaps.js: a binding string becomes one cap per key.
//
//   node --test build/scripts/keycaps-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');

installDOM({pathname: '/commandbar'});

const {splitBinding, keycaps} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/keycaps.js'));

test('modifier glyphs and a letter each become a cap', () => {
  assert.deepEqual(splitBinding('⇧⌘Z'), ['⇧', '⌘', 'Z']);
});

test('the four macOS modifiers all split', () => {
  assert.deepEqual(splitBinding('⌃⌥⇧⌘T'), ['⌃', '⌥', '⇧', '⌘', 'T']);
});

test('a word stays one cap', () => {
  assert.deepEqual(splitBinding('click'), ['click']);
  assert.deepEqual(splitBinding('⇧click'), ['⇧', 'click']);
});

test('a named key stays one cap', () => {
  assert.deepEqual(splitBinding('↩'), ['↩']);
  assert.deepEqual(splitBinding('esc'), ['esc']);
  assert.deepEqual(splitBinding('⌘←'), ['⌘', '←']);
});

test('an empty binding yields no caps', () => {
  assert.deepEqual(splitBinding(''), []);
  assert.deepEqual(splitBinding(undefined), []);
});

test('keycaps builds one .ov-key per cap and no innerHTML', () => {
  const el = keycaps('⇧⌘J');
  assert.equal(el.className, 'ov-keys');
  const caps = el.querySelectorAll('.ov-key');
  assert.equal(caps.length, 3);
  assert.equal(caps[2].textContent, 'J');
  assert.equal(el.textContent, '⇧⌘J');
});
