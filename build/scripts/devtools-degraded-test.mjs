#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the degraded build must never dock devtools.
//
//   node --test build/scripts/devtools-degraded-test.mjs
//
// "Degraded" is a non-mac build. atrium_devtools_bridge.cc holds the every-
// platform stubs under !BUILDFLAG(IS_MAC), and they must answer "cannot dock"
// unconditionally - that is what makes a vanilla build behave exactly as stock
// DevToolsWindow does, always undocked.
//
// Read from the source rather than compiled, because this machine cannot build
// the non-mac path. A browsertest cannot cover it either: on mac the gate is a
// registry lookup and every Browser in a test IS registered, so the only
// negative a browsertest can reach is the null-pointer guard.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const BRIDGE =
    path.join(SRC, 'chrome/browser/ui/atrium/atrium_devtools_bridge.cc');

const source = () => fs.readFileSync(BRIDGE, 'utf8');

// Everything this file defines sits inside the non-mac arm, so the guard is the
// arm itself plus the bodies within it.
function nonMacArm(text) {
  const start = text.indexOf('#if !BUILDFLAG(IS_MAC)');
  assert.notEqual(start, -1, 'no !BUILDFLAG(IS_MAC) arm, so nothing is stubbed');
  const end = text.indexOf('#endif', start);
  assert.notEqual(end, -1, 'the non-mac arm is never closed');
  return text.slice(start, end);
}

test('the non-mac can-dock stub returns false unconditionally', () => {
  const arm = nonMacArm(source());
  const body = arm.match(
      /bool AtriumCanDockDevtools\([^)]*\)\s*{([\s\S]*?)\n}/);
  assert.ok(body, 'no AtriumCanDockDevtools body in the non-mac arm');
  // Unconditionally: one statement, and it is `return false`. A branch here
  // would mean some degraded configuration can dock.
  const statements = body[1].split(';').map((s) => s.trim()).filter(Boolean);
  assert.deepEqual(statements, ['return false'],
                   `the stub is not a bare return false: ${body[1].trim()}`);
});

test('the non-mac update stub does nothing', () => {
  const arm = nonMacArm(source());
  const body = arm.match(
      /void MaybeUpdateAtriumDockedDevtools\(([\s\S]*?)\)\s*{([\s\S]*?)}/);
  assert.ok(body, 'no MaybeUpdateAtriumDockedDevtools body in the non-mac arm');
  assert.equal(body[2].trim(), '',
               `the degraded update stub has a body: ${body[2].trim()}`);
});
