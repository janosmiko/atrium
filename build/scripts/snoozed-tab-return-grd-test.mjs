#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// snoozed-tab-return.js was never in the .grd.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const GRD = path.join(SRC, 'chrome/browser/resources/atrium/atrium_resources.grd');

test('snoozed-tab-return.js is registered in atrium_resources.grd', () => {
  const grd = fs.readFileSync(GRD, 'utf8');
  assert.match(grd, /<include[^>]+file="snoozed-tab-return\.js"[^>]*\/>/,
      'snoozed-tab-return.js has no <include> entry - it never ships to the ' +
      'browser, no matter what imports it');
});
