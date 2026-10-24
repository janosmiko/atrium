#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// icons.js: every overlay mark is an SVG node built without innerHTML.
//
//   node --test build/scripts/icons-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const FILE = path.join(SRC, 'chrome/browser/resources/atrium/icons.js');

installDOM({pathname: '/commandbar'});
const {icon, ICON_NAMES} = await import(FILE);

test('every name builds an svg with one or more paths', () => {
  for (const name of ICON_NAMES) {
    const el = icon(name);
    assert.equal(el.tagName.toLowerCase(), 'svg', name);
    assert.equal(el.classList.contains('ov-icon'), true, name);
    assert.ok(el.querySelectorAll('path, circle, rect').length >= 1, name);
  }
});

test('the six command bar kinds are names', () => {
  for (const kind of ['url', 'search', 'favorite', 'action', 'tab', 'space']) {
    assert.ok(ICON_NAMES.includes(kind), kind);
  }
});

test('an unknown name falls back to the url mark', () => {
  assert.equal(icon('nope').dataset.icon, 'url');
});

test('the module never touches innerHTML', () => {
  assert.doesNotMatch(
      fs.readFileSync(FILE, 'utf8'), /\.innerHTML\s*=|innerHTML\s*\+=/);
});
