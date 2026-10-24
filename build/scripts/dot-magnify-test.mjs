#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the pick is pure, so the hysteresis is pinned without a pointer.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {HYSTERESIS_PX, nearestDot} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/sidebar-dot-magnify.js'));

// Four dots, 20px apart, centres at 10, 30, 50, 70.
const CENTRES = [10, 30, 50, 70];

test('the dot nearest the cursor wins', () => {
  assert.equal(nearestDot(12, CENTRES), 0);
  assert.equal(nearestDot(48, CENTRES), 2);
  assert.equal(nearestDot(200, CENTRES), 3, 'past the end the last dot is nearest');
});

test('an empty bar magnifies nothing', () => {
  assert.equal(nearestDot(10, []), null);
});

test('at the midpoint the dot already magnified keeps it', () => {
  // 20 is exactly between dot 0 and dot 1. Without hysteresis the pick would
  // flip on every sub-pixel move of the cursor.
  assert.equal(nearestDot(20, CENTRES, 0), 0);
  assert.equal(nearestDot(20, CENTRES, 1), 1);
});

test('the cursor has to get meaningfully closer to switch', () => {
  // The threshold is on the DIFFERENCE of the two distances, so crossing it
  // takes half that much travel past the midpoint.
  const travel = HYSTERESIS_PX / 2;
  assert.equal(nearestDot(20 + travel - 0.5, CENTRES, 0), 0,
               'switched before the next dot was the threshold closer');
  assert.equal(nearestDot(20 + travel + 0.5, CENTRES, 0), 1);
});

test('a stale current index cannot hold the pick', () => {
  // A rebuild can leave an index past the end of a shorter bar.
  assert.equal(nearestDot(12, CENTRES, 9), 0);
});
