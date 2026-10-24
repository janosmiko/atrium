#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// overlay.css: the classes every overlay consumer relies on exist, and the
// tokens they read are defined on bare :root.
//
//   node --test build/scripts/overlay-classes-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const css = fs.readFileSync(
    path.join(SRC, 'chrome/browser/resources/atrium/overlay.css'), 'utf8');

for (const cls of ['.ov-card', '.ov-row', '.ov-key', '.ov-btn', '.ov-button',
                   '.ov-button.ov-primary', '.ov-scrim', '.ov-hint']) {
  test(`${cls} is defined`, () => {
    assert.match(css, new RegExp(cls.replace(/\./g, '\\.') + '\\s*[,{]'));
  });
}

test('every token the dark block redefines is defined on bare :root first', () => {
  const root = css.slice(0, css.indexOf('@media'));
  const dark = css.slice(css.indexOf('@media'));
  const names = [...dark.matchAll(/(--ov-[a-z-]+):/g)].map((m) => m[1]);
  for (const name of new Set(names)) {
    assert.ok(root.includes(name + ':'), name + ' has no :root definition');
  }
});

// The literal-color gate proves no consumer writes a raw color, not that a
// var(--ov-*) it reads actually resolves - a token can be referenced and
// never defined, as courtyard.css shipped with three of them.
const defined = new Set([...css.matchAll(/(--ov-[a-z-]+):/g)].map((m) => m[1]));
const CONSUMERS = ['commandbar.css', 'toast-stack.css', 'permsheet.css',
                   'hint.css', 'peek-controls.css', 'courtyard.css'];

for (const file of CONSUMERS) {
  test(`every --ov-* token ${file} reads is defined in overlay.css`, () => {
    const consumerCss = fs.readFileSync(
        path.join(SRC, 'chrome/browser/resources/atrium', file), 'utf8');
    const referenced =
        [...consumerCss.matchAll(/var\((--ov-[a-z-]+)/g)].map((m) => m[1]);
    for (const name of new Set(referenced)) {
      assert.ok(defined.has(name), name + ' is referenced but never defined');
    }
  });
}
