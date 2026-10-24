#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// each Overview section used to set its own left padding and most
// set none, so the filter chips, the space cards and the tab rows all started
// against the icon rail's border.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const CSS = fs.readFileSync(
    path.join(SRC, 'chrome/browser/resources/atrium/overview.css'), 'utf8');

function rule(selector) {
  const escaped = selector.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const found = new RegExp(`(^|\\n)${escaped}\\s*{([^}]*)}`).exec(CSS);
  assert.ok(found, `no rule for ${selector}`);
  return found[2];
}

test('the column beside the rail carries one left inset', () => {
  const main = rule('.ovmain');
  const inset = /padding-inline-start:\s*(\d+)px/.exec(main);
  assert.ok(inset, '.ovmain sets no left inset');
  assert.ok(Number(inset[1]) > 0, 'a zero inset is what the report describes');
  // Without this the inset would widen the column instead of insetting it.
  assert.match(main, /box-sizing:\s*border-box/);
});

// .ovhead sat 2px further left than the lists below it, which is visible
// against a heading that size.
test('the section name starts on the same edge as its content', () => {
  const head = /padding:\s*([^;]+);/.exec(rule('.ovhead'));
  assert.ok(head, '.ovhead sets no padding');
  const parts = head[1].trim().split(/\s+/);
  const left = parts.length >= 4 ? parts[3] : parts[1] || parts[0];
  assert.equal(left, '0');
});
