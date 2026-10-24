#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// press versus hold on the Back control.
//
//   node --test build/scripts/hold-gesture-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

installDOM();

const SRC = process.env.CHROMIUM_DIR ?
    path.join(process.env.CHROMIUM_DIR, 'src') :
    path.join(os.homedir(), 'chromium-atrium/src');
const {
  HOLD_THRESHOLD_MS,
  PENDING,
  PRESS,
  HOLD,
  CANCELLED,
  newHoldState,
  onPointerDown,
  onPointerMove,
  onHoldTimerTick,
  onPointerUp,
} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/hold-gesture.js'));

test('a short press is a press', () => {
  const s = newHoldState();
  assert.equal(onPointerDown(s, 0, 10, 10), PENDING);
  // The timer runs while the pointer is down and must say nothing yet.
  assert.equal(onHoldTimerTick(s, 100), PENDING);
  assert.equal(onPointerUp(s), PRESS);
});

test('one millisecond short of the threshold is still a press in waiting', () => {
  const s = newHoldState();
  onPointerDown(s, 0, 10, 10);
  assert.equal(onHoldTimerTick(s, HOLD_THRESHOLD_MS - 1), PENDING);
  assert.equal(onHoldTimerTick(s, HOLD_THRESHOLD_MS), HOLD);
});

// The mistake that would move the page twice: the scrubber opens, and the
// release that commits it also navigates back.
test('the release after a hold is not also a press', () => {
  const s = newHoldState();
  onPointerDown(s, 0, 10, 10);
  assert.equal(onHoldTimerTick(s, HOLD_THRESHOLD_MS), HOLD);
  assert.equal(onPointerUp(s), CANCELLED);
});

// The other one: a timer that keeps ticking opens the scrubber on top of
// itself.
test('the hold fires exactly once per press', () => {
  const s = newHoldState();
  onPointerDown(s, 0, 10, 10);
  assert.equal(onHoldTimerTick(s, HOLD_THRESHOLD_MS), HOLD);
  assert.equal(onHoldTimerTick(s, 1000), PENDING);
  assert.equal(onHoldTimerTick(s, 3000), PENDING);
});

// And the third: a swipe that happens to start on this control.
test('drifting past the slop cancels the whole press', () => {
  const s = newHoldState();
  onPointerDown(s, 0, 10, 10);
  assert.equal(onPointerMove(s, 13, 12), PENDING, 'inside the slop radius');
  assert.equal(onPointerMove(s, 40, 10), CANCELLED);
  // Cancelled means neither action, so the timer and the release must both go
  // quiet rather than fall back to navigating.
  assert.equal(onHoldTimerTick(s, 2000), PENDING);
  assert.equal(onPointerUp(s), CANCELLED);
});

// A dropped timer must lose the scrubber, not the back button.
test('a long press whose timer never ticked still navigates', () => {
  const s = newHoldState();
  onPointerDown(s, 0, 10, 10);
  assert.equal(onPointerUp(s), PRESS);
});

test('a second pointerdown starts from nothing', () => {
  const s = newHoldState();
  onPointerDown(s, 0, 10, 10);
  assert.equal(onHoldTimerTick(s, HOLD_THRESHOLD_MS), HOLD);
  // No release arrives. The next press begins here.
  onPointerDown(s, 10000, 50, 50);
  assert.equal(onHoldTimerTick(s, 10010), PENDING,
               'the previous press must not leave its fired flag behind');
  assert.equal(onPointerUp(s), PRESS);
});

test('events outside a press do nothing', () => {
  const s = newHoldState();
  assert.equal(onPointerMove(s, 99, 99), PENDING);
  assert.equal(onHoldTimerTick(s, 1000), PENDING);
  assert.equal(onPointerUp(s), CANCELLED);
});

// The button's own wiring: which message each outcome sends, and that the other
// one never goes out. A press that also opened the scrubber, or a hold that
// also navigated, is the whole failure this feature can have.
test('the Back button sends exactly one message per gesture', async () => {
  const {buildBackButton} = await import(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar-nav-row.js'));

  const sent = [];
  const button = buildBackButton((name, args) => sent.push([name, args]));

  const down = (t, x = 10, y = 10) =>
      button.dispatchEvent({type: 'pointerdown', timeStamp: t, clientX: x,
                            clientY: y});
  const up = () => button.dispatchEvent({type: 'pointerup'});

  // A short press: one navigate, nothing else.
  down(0);
  up();
  assert.deepEqual(sent, [['sidebarBackPressed', []]]);

  // A hold: the timer fires, and the release afterwards adds nothing. The
  // clock is taken over rather than waited on, so the test does not sleep.
  sent.length = 0;
  const realSetTimeout = globalThis.setTimeout;
  let pending = null;
  globalThis.setTimeout = (fn) => {
    pending = fn;
    return 1;
  };
  try {
    down(0);
    assert.ok(pending, 'a press must arm the hold timer');
    pending();
    assert.deepEqual(sent, [['sidebarBackHeld', []]]);
    up();
    assert.deepEqual(sent, [['sidebarBackHeld', []]],
                     'the release after a hold must not also navigate');
  } finally {
    globalThis.setTimeout = realSetTimeout;
  }
});
