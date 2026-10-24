#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// day-cycle sky gradient, ported from swipe-lab.html:2078-2100
// (skyCurve/skyLayers) and :2593-2598 (spaceBG).

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

const {skyCurve, dayCycleLayers, spaceDayCycleBackground, setSkylightDayCycle} =
    await import(path.join(ATRIUM_DIR, 'skylight-day-cycle.js'));

const h = installDOM({pathname: '/sidebar'});

function makeHost() {
  h.reset();
  const host = document.createElement('div');
  host.setAttribute('id', 'atrium-root');
  document.body.append(host);
  return host;
}

// The lab's own keyframes (swipe-lab.html:2083): [hour, warm, dark].
const KEYFRAMES = [
  [0, -.15, .72], [4, -.2, .78], [6, -.55, .38], [9, -.12, .06],
  [13, 0, 0], [16, .12, .02], [18.5, .68, .12], [20.5, .3, .42],
  [22.5, -.05, .66], [24, -.15, .72],
];

// The lab's own formula hits float rounding at a couple of these boundaries
// (e.g. 0.3 + -0.35 !== -0.05 to the last bit) - true of the lab too, since
// it is the same arithmetic, so the bar is "matches", not "exact literal".
function closeTo(actual, expected, hour, label) {
  assert.ok(Math.abs(actual - expected) < 1e-9,
      `${label} at hour ${hour}: got ${actual}, want ${expected}`);
}

test('skyCurve matches the lab at each of the ten keyframe hours', () => {
  for (const [hour, warm, dark] of KEYFRAMES) {
    const got = skyCurve(hour);
    closeTo(got.warm, warm, hour, 'warm');
    closeTo(got.dark, dark, hour, 'dark');
  }
});

test('the curve is continuous across midnight', () => {
  assert.deepEqual(skyCurve(0), skyCurve(24));
  // A scrubber past 24 wraps rather than re-reading as midnight.
  assert.deepEqual(skyCurve(25), skyCurve(1));
  assert.notDeepEqual(skyCurve(25), skyCurve(0));
});

test('the background updates on its own, provable via a demo hour override', () => {
  const host = makeHost();
  const base = 'linear-gradient(165deg, #0F2B46, #08192B)';

  setSkylightDayCycle(base, {host, hour: 9});
  assert.equal(host.style.background,
      dayCycleLayers(9).join(',') + ',' + base,
      'hour 9 must paint hour 9\'s own layers');

  setSkylightDayCycle(base, {host, hour: 20.5});
  assert.equal(host.style.background,
      dayCycleLayers(20.5).join(',') + ',' + base,
      'scrubbing the demo hour forward must repaint with no other caller action');

  // The live path (no hour override) repaints off its own clock, not just once.
  let scheduledFn = null;
  let cleared = 0;
  const clock = {
    set: (ms, fn) => {
      assert.equal(ms, 60000, 'must repaint on the lab\'s own 60s cadence');
      scheduledFn = fn;
      return 1;
    },
    clear: () => {
      cleared++;
    },
  };
  let hourNow = 9;
  const now = () => ({getHours: () => Math.floor(hourNow), getMinutes: () => (hourNow % 1) * 60});

  setSkylightDayCycle(base, {host, clock, now});
  assert.equal(host.style.background,
      dayCycleLayers(9).join(',') + ',' + base, 'first live paint uses the current hour');

  hourNow = 20.5;
  scheduledFn();
  assert.equal(host.style.background,
      dayCycleLayers(20.5).join(',') + ',' + base,
      'the clock tick alone must repaint with the new hour - no caller re-invoked setSkylightDayCycle');

  // A second call must not leave the first interval running behind it.
  setSkylightDayCycle(base, {host, clock, now});
  assert.equal(cleared, 1, 'the earlier live interval must be cleared, not left leaking');
});

test('Night Shift off falls back to the plain per-space gradient with no sky layers', () => {
  const base = 'linear-gradient(165deg, #0F2B46, #08192B)';
  assert.equal(spaceDayCycleBackground(base, {nightShift: false, hour: 20.5}), base);

  const host = makeHost();
  setSkylightDayCycle(base, {host, nightShift: false, hour: 20.5});
  assert.equal(host.style.background, base,
      'Night Shift off must paint only the base gradient, never the sky layers');
});
