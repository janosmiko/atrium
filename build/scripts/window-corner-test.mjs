#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// A rewrite of the platform file from an old base once dropped the window
// server radius push, and jankyborders drew square corners again.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const mm = fs.readFileSync(
    path.join(SRC, 'chrome/browser/ui/atrium/atrium_window_platform_mac.mm'),
    'utf8');

test('UpdateCornerMask pushes the radius to the window server', () => {
  // Not 'void UpdateCornerMask': ff05b3fab moved it onto AtriumWindowPlatform,
  // and a split on the old free-function spelling silently read an empty body.
  const body = mm.split('UpdateCornerMask(float radius)')[1] || '';
  assert.match(body, /SetServerCornerRadius\(ns_window, radius\)/);
  assert.match(mm, /dlsym\(skylight, "SLSTransactionSetWindowCornerRadius"\)/);
});
