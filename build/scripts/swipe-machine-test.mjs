#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the HTML sidebar's swipe machine, driven headlessly.
//
// The machine is deliberately DOM-free so it can run here. That is the whole
// reason the port is split from its installer: the SwiftUI arm's swipe cost
// days precisely because "does it commit?" could only be answered by hand, on
// a running app, one gesture at a time.
//
//   make swipe-test
//
// Deltas are passed the way the wheel listener passes them: dx is already
// sign-flipped, so dx < 0 swipes toward the NEXT space. Same as the lab.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {createSwipeMachine, DEFAULT_TUNING, S} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/swipe.js'));

const W = 260;
const FRAME = 1 / 60;

function machine(spaceCount = 4, spaceIndex = 0) {
  const m = createSwipeMachine({spaceCount, width: () => W});
  m.spaceIndex = spaceIndex;
  return m;
}

// Feeds a gesture as `events` wheel deltas totalling `fraction` of the sidebar
// width, `gap` seconds apart. A small gap is a flick, 0.05 is a slow drag.
// Past tailQuietGap a gap is a lift, and the next event starts a new gesture.
function swipe(m, {fraction, events = 10, gap = 0.01, from = 0}) {
  let t = from;
  const dx = -(fraction * W) / events;
  for (let i = 0; i < events; i++) {
    t += gap;
    m.delta(dx, 0, t);
  }
  return t;
}

// Runs frames until the machine settles, so assertions land on the resting
// state rather than on a mid-spring sample.
function settle(m, from, maxFrames = 600) {
  let t = from;
  for (let i = 0; i < maxFrames; i++) {
    t += FRAME;
    m.step(FRAME, t);
    if (m.state === S.idle) {
      return t;
    }
  }
  assert.fail(`never settled; stuck in ${m.state}`);
}

// A gesture commits the moment it crosses commitThreshold, while the fingers
// are still moving - Arc trades the pane during a slow drag, not at the lift.
test('a slow swipe commits at the threshold, during the movement', () => {
  const m = machine();
  const t = swipe(m, {fraction: 0.40, gap: 0.05});
  assert.equal(m.state, S.settlingCommit, 'committed before the lift');
  m.end();
  settle(m, t);
  assert.equal(m.spaceIndex, 1);
  assert.equal(m.displayP, 0);
});

test('a short slow swipe rolls back', () => {
  const m = machine();
  const t = swipe(m, {fraction: 0.10, gap: 0.05});
  m.end();
  settle(m, t);
  assert.equal(m.spaceIndex, 0);
});

test('a slow drag and a flick commit at the same threshold', () => {
  const flick = machine();
  let t = swipe(flick, {fraction: 0.20, gap: 0.005});
  flick.end();
  settle(flick, t);
  assert.equal(flick.spaceIndex, 1, 'a fast 20% swipe should commit');

  const drag = machine();
  t = swipe(drag, {fraction: 0.20, gap: 0.05});
  drag.end();
  settle(drag, t);
  assert.equal(drag.spaceIndex, 1, 'a slow 20% swipe should commit too');
});

// The cancel gesture after an early commit: reversing the fingers takes the
// settle over at once (a flip breaks through the swallow), and the same
// threshold back lands on the origin.
test('reversing after the commit, before the lift, returns to the origin', () => {
  const m = machine();
  let t = 0;
  while (m.state !== S.settlingCommit) {
    t += FRAME;
    m.delta(-(0.02 * W), 0, t);
    m.step(FRAME, t);
  }
  for (let i = 0; i < 10; i++) {
    t += FRAME;
    m.delta(0.03 * W, 0, t);
    m.step(FRAME, t);
  }
  settle(m, t);
  assert.equal(m.spaceIndex, 0, 'the reversal cancels the early commit');
});

// The takeover's start is measured through the same resist curve as
// progress(), or a rail still panes away from a long jump's target reads as
// movement and a brush of the trackpad commits one space past the target.
test('a brush during a long jump does not commit past its target', () => {
  const m = machine(12, 0);
  m.jumpTo(7);
  let t = 0;
  for (let i = 0; i < 14; i++) {
    t += FRAME;
    m.step(FRAME, t);
  }
  assert.ok(m.displayP > 0.3, `still a way off, rail at ${m.displayP}`);
  m.delta(-6, 0, t + 0.01);
  m.delta(-20, 0, t + 0.02);
  assert.equal(m.state, S.tracking, 'a 10% brush commits nothing');
  settle(m, t + 0.02);
  assert.equal(m.spaceIndex, 7);
});

// Drives one event per frame, the way the RAF loop interleaves them, until
// the commit, and returns how long the glide then took to land.
function glideAfterCommit(m, dx) {
  let t = 0;
  while (m.state !== S.settlingCommit) {
    t += FRAME;
    m.delta(dx, 0, t);
    m.step(FRAME, t);
    assert.ok(t < 1, 'the gesture never committed');
  }
  return settle(m, t) - t;
}

// Only a crossing that involves a leading pane (the Overview) is the quick
// ~100ms trade. Space to space keeps the ordinary spring.
test('the glide onto the Overview lands within 0.2s', () => {
  const m = createSwipeMachine({spaceCount: 4, width: () => W, quickPanes: 1});
  m.spaceIndex = 1;
  const took = glideAfterCommit(m, 0.02 * W);
  assert.equal(m.spaceIndex, 0);
  assert.ok(took <= 0.2, `the glide took ${took.toFixed(3)}s`);
});

test('the glide off the Overview lands within 0.2s', () => {
  const m = createSwipeMachine({spaceCount: 4, width: () => W, quickPanes: 1});
  m.spaceIndex = 0;
  const took = glideAfterCommit(m, -(0.02 * W));
  assert.equal(m.spaceIndex, 1);
  assert.ok(took <= 0.2, `the glide took ${took.toFixed(3)}s`);
});

test('the glide between two spaces keeps the ordinary spring', () => {
  const m = createSwipeMachine({spaceCount: 4, width: () => W, quickPanes: 1});
  m.spaceIndex = 1;
  const took = glideAfterCommit(m, -(0.02 * W));
  assert.equal(m.spaceIndex, 2);
  assert.ok(took > 0.3, `the glide took only ${took.toFixed(3)}s`);
});

// setTuning has to reach the machine that is already running,
// not just a value it was constructed with - the settings surface applies a
// preset to a sidebar it does not share a page with.
test('setTuning changes what the running machine does, not just its tuning field', () => {
  const m = machine();
  m.setTuning({commitThreshold: 0.05});
  assert.equal(m.tuning.commitThreshold, 0.05);

  const t = swipe(m, {fraction: 0.10, gap: 0.05});
  m.end();
  settle(m, t);
  assert.equal(m.spaceIndex, 1,
      'the same 10% swipe that rolls back at the default 0.15 commits ' +
      'once commitThreshold is lowered below it');
});

test('the display tracks the finger and commits nothing below the threshold', () => {
  const m = machine();
  let t = 0;
  const seen = [];
  // Interleaved, the way the real loop runs: one wheel event per frame, one
  // step per frame. Feeding the whole gesture first would trip the
  // silence-inferred release.
  for (let i = 0; i < 20; i++) {
    t += FRAME;
    m.delta(-(0.12 * W) / 20, 0, t);
    seen.push(m.step(FRAME, t));
  }
  const p = m.progress();
  assert.ok(p > 0.11 && p < 0.13, `progress should follow the finger, got ${p}`);
  assert.ok(m.displayP > 0.5 * p && m.displayP <= p + 1e-9,
      `display should chase progress, got ${m.displayP} of ${p}`);
  assert.deepEqual(seen, [...seen].sort((a, b) => a - b),
      'the display must not jump backwards mid-gesture');
  assert.equal(m.spaceIndex, 0, 'nothing commits below the threshold');
});

test('a vertical gesture is ignored, not swiped', () => {
  const m = machine();
  let t = 0;
  for (let i = 0; i < 10; i++) {
    t += 0.01;
    m.delta(0, -20, t);
  }
  assert.equal(m.state, S.ignoring);
  assert.ok(Math.abs(m.progress()) < 1e-9);
});

test('the rail end rubber-bands and never commits', () => {
  const m = machine(4, 0);
  // Toward the PREVIOUS space, of which there is none at index 0.
  const t = swipe(m, {fraction: -0.90, gap: 0.01});
  assert.ok(Math.abs(m.progress()) < DEFAULT_TUNING.railEndCap,
      `rail end should resist, got ${m.progress()}`);
  m.end();
  settle(m, t);
  assert.equal(m.spaceIndex, 0);
});

test('one long swipe cannot tunnel through several spaces', () => {
  const m = machine(4, 0);
  // Three full widths in a single unbroken gesture.
  const t = swipe(m, {fraction: 3.0, events: 60, gap: 0.008});
  settle(m, t);
  assert.equal(m.spaceIndex, 1, 'a single gesture may advance one space');
});

test('wheel silence infers the release', () => {
  const m = machine();
  let t = swipe(m, {fraction: 0.10, gap: 0.01});
  // No end() - only the passage of time, which is all a trackpad gives us.
  t += DEFAULT_TUNING.wheelEndGap + FRAME;
  m.step(FRAME, t);
  // The display never moved (no step ran during the swipe), so the cancel
  // settle can finish inside this same step.
  assert.notEqual(m.state, S.tracking, 'silence should resolve the gesture');
  settle(m, t);
  assert.equal(m.spaceIndex, 0);
});

test('a gesture never leaves the rail parked between spaces', () => {
  const m = machine();
  // A violent flick: the kind that makes an explicit spring integrator blow up.
  const t = swipe(m, {fraction: 2.4, events: 8, gap: 0.004});
  const end = settle(m, t);
  assert.equal(m.state, S.idle);
  assert.equal(m.displayP, 0);
  assert.ok(Number.isFinite(m.step(FRAME, end + FRAME)));
});

test('a second swipe takes over a running settle without a visual jump', () => {
  const m = machine(4, 0);
  let t = swipe(m, {fraction: 0.40, gap: 0.01});
  m.end();
  for (let i = 0; i < 4; i++) {
    t += FRAME;
    m.step(FRAME, t);
  }
  assert.equal(m.state, S.settlingCommit);
  // Absolute rail position, in pane units. This is what the eye sees, and it
  // is what must not move when the new gesture rebases the machine.
  const before = m.spaceIndex + m.displayP;
  // A REAL second swipe: a pause, then a ramping burst. This used to be a
  // single 4px nudge, which is smaller than the first flick's own events and
  // therefore indistinguishable from a decaying momentum tail - and taking
  // over on it is exactly how a tail hijacked the settle and paged a second
  // space. A finger accelerates; a tail only ever decays.
  t += 0.2;
  let took = null;
  for (const dx of [-9, -16, -26]) {
    t += 0.012;
    m.delta(dx, 0, t);
    if (took === null && m.state === S.tracking) {
      took = m.spaceIndex + m.displayP;
    }
  }
  // The three events cross the threshold, so the new gesture has committed.
  assert.equal(m.state, S.settlingCommit, 'the new gesture owns the rail');
  assert.equal(m.spaceIndex, 1, 'the pending commit finalizes on takeover');
  assert.ok(took !== null && Math.abs(took - before) < 1e-6,
      `takeover jumped the rail: ${before} -> ${took}`);
});

test('two swipes back to back advance two spaces', () => {
  const m = machine(4, 0);
  let t = swipe(m, {fraction: 0.40, gap: 0.01});
  m.end();
  t = settle(m, t);
  t = swipe(m, {fraction: 0.40, gap: 0.01, from: t + 0.3});
  m.end();
  settle(m, t);
  assert.equal(m.spaceIndex, 2);
});

test('jumpTo animates to any space and lands there', () => {
  const m = machine(4, 0);
  assert.equal(m.jumpTo(3), 3, 'reports the space it will land on');
  assert.equal(m.jumpTo(9), 3, 'a target past the last space clamps');
  const t = settle(m, 0);
  assert.equal(m.spaceIndex, 3);
  assert.equal(m.displayP, 0);
  m.jumpTo(0);
  settle(m, t);
  assert.equal(m.spaceIndex, 0);
});

test('a long jumpTo travels one pane, not the whole distance', () => {
  // From space 1 to space 5 the rail used to pass 2, 3 and 4. The controller
  // lifts those off the rail, so the machine only has a pane to cover.
  const m = machine(6, 1);
  m.jumpTo(5);
  assert.equal(m.settleTarget, 1, 'one pane forward');
  assert.equal(m.settleTargetIndex, 5);
  let t = 0;
  let peak = 0;
  for (let i = 0; i < 600 && m.state !== S.idle; i++) {
    t += FRAME;
    m.step(FRAME, t);
    peak = Math.max(peak, Math.abs(m.displayP));
  }
  assert.ok(peak < 1.3, `the rail never left the pane pair, peak ${peak}`);
  assert.equal(m.spaceIndex, 5);
  m.jumpTo(0);
  assert.equal(m.settleTarget, -1, 'one pane back');
  settle(m, t);
  assert.equal(m.spaceIndex, 0);
});

// Mid-glide the rail shows 0 beside 4, with 1 to 3 lifted off. A swipe that
// takes over and heads back has to land on 0, the pane it can see, not on 3.
test('a swipe back over a long jump lands on the space the jump left', () => {
  const m = machine(5, 0);
  m.jumpTo(4);
  let t = 0;
  while (m.displayP < 0.5) {
    t += FRAME;
    m.step(FRAME, t);
  }
  t += FRAME;
  m.delta(0.05 * W, 0, t);
  assert.equal(m.state, S.tracking, 'the swipe takes the glide over');
  for (let i = 0; i < 60 && m.state !== S.settlingCommit; i++) {
    t += FRAME;
    m.delta(0.05 * W, 0, t);
    m.step(FRAME, t);
  }
  assert.equal(m.state, S.settlingCommit, 'the drag back commits');
  assert.equal(m.settleTargetIndex, 0, 'the neighbour behind is the origin');
  settle(m, t);
  assert.equal(m.spaceIndex, 0);
  assert.equal(m.neighbor(1), 1, 'at rest the rail is whole again');
});

test('a jumpTo that interrupts a glide keeps the rail where it is', () => {
  // spaceIndex is where displayP is measured from, so moving it onto the
  // abandoned target left the rail a whole pane out of position.
  const m = machine(5, 0);
  m.jumpTo(4);
  let t = 0;
  for (let i = 0; i < 6; i++) {
    t += FRAME;
    m.step(FRAME, t);
  }
  const before = m.spaceIndex + m.displayP;
  assert.ok(before > 0.05, `the glide has started, rail at ${before}`);
  m.jumpTo(1);
  assert.equal(m.spaceIndex, 0, 'the rail has arrived nowhere yet');
  t += FRAME;
  m.step(FRAME, t);
  const after = m.spaceIndex + m.displayP;
  assert.ok(Math.abs(after - before) < 0.5,
            `the rail carried on from ${before}, not ${after}`);
  settle(m, t);
  assert.equal(m.spaceIndex, 1, 'and the last target is where it stops');
});

test('a press reversing a glide never teleports the rail, however long the rail', () => {
  // Scaled by |settleTarget| the divergence guard read a reversal's ordinary
  // remaining travel as a spring blowing up and snapped it home: 3.78 panes in
  // one frame at 6 spaces, 5.30 at 8. Four stayed under the old constant.
  for (const count of [4, 6, 8]) {
    const m = machine(count, 0);
    m.jumpTo(count - 1);
    let t = 0;
    for (let i = 0; i < 14; i++) {
      t += FRAME;
      m.step(FRAME, t);
    }
    let prev = m.spaceIndex + m.displayP;
    assert.ok(prev > 0.3, `${count}: the glide is well under way, rail at ${prev}`);
    m.jumpTo(0);
    let worst = 0;
    for (let i = 0; i < 600 && m.state !== S.idle; i++) {
      t += FRAME;
      m.step(FRAME, t);
      const now = m.spaceIndex + m.displayP;
      worst = Math.max(worst, Math.abs(now - prev));
      prev = now;
    }
    assert.ok(worst < 1,
              `${count} spaces: the rail moved ${worst.toFixed(2)} panes in one frame`);
    assert.equal(m.spaceIndex, 0, `${count}: and it still lands on the last target`);
  }
});

test('a mid-gesture commit runs the divergence guard at its own span, not a stale one', () => {
  // The mid-gesture commit enters settlingCommit by hand and used to leave
  // settleSpan alone, so a long jumpTo earlier in the session left the guard
  // slack for a settle that travels a pane at most.
  const m = machine(9, 0);
  m.jumpTo(8);
  // A jump is one pane of travel now, so the wide span is planted by hand.
  m.settleSpan = 8;
  m.spaceIndex = 0;
  m.state = S.idle;
  m.sessionOpen = false;
  swipe(m, {fraction: 1.2, events: 12, gap: 0.01});
  assert.equal(m.state, S.settlingCommit, 'the gesture committed');
  // One pane, not zero: the settle starts from the DISPLAY, and the follower
  // can still be a whole pane behind the finger when the threshold is reached.
  assert.ok(m.settleSpan <= 1,
            `the settle spans a pane at most, got ${m.settleSpan}`);
});

test('a single space has nothing to swipe to', () => {
  const m = machine(1, 0);
  const t = swipe(m, {fraction: 0.90, gap: 0.01});
  m.end();
  settle(m, t);
  assert.equal(m.spaceIndex, 0);
});

// one flick must commit one space, momentum tail included.
//
// A trackpad does not stop when the fingers lift - macOS keeps sending wheel
// events with decaying magnitudes for about a second, and the event RATE decays
// with them, so the gaps widen toward the end. That is the shape that broke it:
// suppression was cleared by any gap past wheelEndGap (90ms), so once the tail
// thinned out the remaining events were taken for a new gesture and committed a
// second space - about a second after the flick, which is exactly what the user
// saw.

/// A flick, then the momentum tail macOS actually sends: magnitude decaying
/// geometrically while the interval between events grows.
function flickWithTail(m, {peak = 42, tailEvents = 30, from = 0} = {}) {
  let t = from;
  // Finger phase: a short, fast burst.
  for (let i = 0; i < 5; i++) {
    t += 0.012;
    m.delta(-peak, 0, t);
  }
  // Momentum phase: decaying magnitude, widening gaps.
  let mag = peak;
  let gap = 0.016;
  for (let i = 0; i < tailEvents; i++) {
    mag *= 0.88;
    gap *= 1.09;
    t += gap;
    m.delta(-mag, 0, t);
    // The rail is stepped between events, as the RAF loop does - this is where
    // the swallow flag was being cleared.
    m.step(FRAME, t);
  }
  return t;
}

test('a flick plus its momentum tail commits exactly one space', () => {
  const m = machine(5, 0);
  const t = flickWithTail(m);
  settle(m, t);
  assert.equal(m.spaceIndex, 1,
      'the tail must not be taken for a second gesture');
});

test('the tail does not commit even when its gaps grow past wheelEndGap', () => {
  // Explicitly the failure mode: gaps that start under the threshold and end
  // well over it, which is every real tail.
  const m = machine(5, 0);
  let t = 0;
  for (let i = 0; i < 5; i++) { t += 0.012; m.delta(-40, 0, t); }
  let mag = 40;
  for (const gap of [0.02, 0.05, 0.09, 0.14, 0.2, 0.28, 0.36]) {
    mag *= 0.8;
    t += gap;
    m.delta(-mag, 0, t);
    m.step(FRAME, t);
  }
  settle(m, t);
  assert.equal(m.spaceIndex, 1);
});

test('a deliberate re-swipe after a real pause still commits again', () => {
  // The suppression must not become a lockout: a genuine second swipe, after
  // the quiet a human leaves between two flicks, has to work.
  const m = machine(5, 0);
  let t = swipe(m, {fraction: 0.5, gap: 0.012});
  t = settle(m, t);
  assert.equal(m.spaceIndex, 1);
  t = swipe(m, {fraction: 0.5, gap: 0.012, from: t + 0.5});
  settle(m, t);
  assert.equal(m.spaceIndex, 2, 'a real second flick must still page');
});

test('the momentum tail after a finger lift does not commit a second space', () => {
  // THE bug. macOS leaves a gap between the last finger event and the first
  // momentum event. Past wheelEndGap (90ms) the machine ends the gesture and
  // commits - correct - and then the tail's first event lands in
  // settlingCommit, where delta() takes over the settle as a NEW gesture with
  // no tail check at all, because swallowing is only armed by a MID-gesture
  // commit. The tail then drives that gesture to a second commit, about a
  // second after the flick.
  const m = machine(5, 0);
  let t = 0;
  for (let i = 0; i < 5; i++) { t += 0.012; m.delta(-40, 0, t); }

  // Fingers lift: quiet long enough for step() to end the gesture.
  for (let i = 0; i < 8; i++) { t += FRAME; m.step(FRAME, t); }
  assert.equal(m.state, S.settlingCommit, 'the flick committed on release');

  // Momentum: decaying magnitude, widening gaps, ~1s of it.
  let mag = 40;
  let gap = 0.016;
  for (let i = 0; i < 30; i++) {
    mag *= 0.9;
    gap *= 1.06;
    t += gap;
    m.delta(-mag, 0, t);
    m.step(FRAME, t);
  }
  settle(m, t);
  assert.equal(m.spaceIndex, 1,
      'one flick is one space; the tail must not commit another');
});

// A REAL trackpad flick, captured with make wheel-capture on the machine that
// reported the bug (170px sidebar). Simulated tails did not reproduce it: macOS
// does not decay smoothly from the flick, it ENDS the finger phase and then
// BEGINS a momentum phase after a pause of its own, restarting at high
// velocity. Here that is 47px, a 102ms gap, then 182px - and the old 60ms
// finger-lift threshold read that pair as a deliberate re-swipe, so one flick
// paged twice.
//
// Keep this stream verbatim. It is the only input that ever reproduced the bug.
const REAL_FLICK = [
  [1889, 3], [1897, 1], [1914, 73], [1929, 148], [1946, 135], [1963, 119],
  [1980, 205], [1996, 190], [2013, 178], [2030, 168], [2047, 158], [2063, 150],
  [2080, 137], [2097, 132], [2114, 127], [2130, 123], [2147, 117], [2164, 112],
  [2181, 108], [2197, 102], [2214, 99], [2230, 94], [2247, 89], [2264, 84],
  [2280, 81], [2297, 75], [2314, 70], [2330, 66], [2347, 62], [2364, 57],
  [2380, 53], [2397, 49], [2412, 47],
  [2514, 182], [2530, 78], [2547, 21], [2564, 19], [2581, 17], [2597, 16],
  [2614, 14], [2630, 13], [2647, 13], [2664, 11], [2681, 10], [2697, 9],
  [2714, 9], [2730, 8], [2747, 7], [2763, 7], [2781, 6], [2797, 6], [2814, 5],
  [2830, 5], [2847, 5], [2864, 4], [2881, 4], [2897, 4], [2914, 4], [2931, 3],
  [2947, 3], [2964, 3], [2981, 3], [2997, 2],
  [3086, 10], [3114, 4], [3131, 2], [3148, 1], [3164, 1], [3181, 1], [3198, 1],
  [3213, 1], [3231, 1], [3248, 1], [3263, 1], [3281, 1], [3313, 1], [3331, 1],
  [3347, 0], [3364, 1],
];

function replay(m, stream, width) {
  let prev = stream[0][0] / 1000;
  for (const [ms, dx] of stream) {
    const t = ms / 1000;
    for (let f = prev; f < t - 1e-9; f += FRAME) {
      m.step(FRAME, Math.min(f + FRAME, t));
    }
    prev = t;
    m.delta(-dx, 0, t);       // the listener passes -deltaX
  }
  return prev;
}

test('a real captured trackpad flick pages exactly one space', () => {
  const m = createSwipeMachine({spaceCount: 5, width: () => 170});
  m.spaceIndex = 0;
  const t = replay(m, REAL_FLICK, 170);
  settle(m, t);
  assert.equal(m.spaceIndex, 1,
      'the momentum phase must not be taken for a second swipe');
});

// the axis lock's vertical branch must not be terminal.
//
// `ignoring` is where a gesture goes when the axis lock resolves vertical, and
// delta() drops every event while it is set. step() inferred the end of a
// gesture from silence for `tracking` and `overshoot` only, so nothing ever
// left `ignoring` - one diagonal scroll killed swiping until the surface was
// rebuilt. Found on a live stuck sidebar: state 'ignoring', running true.
//
// Reproducible without a momentum tail, unlike the rest of this file's tail
// cases: the axis lock is deterministic.
test('a vertical gesture does not leave the machine unable to swipe', () => {
  const m = createSwipeMachine({spaceCount: 3, width: 260});

  // Diagonal, but vertical-dominant: passes the surface's own intent guard and
  // then resolves to `ignoring` at the axis lock.
  let t = 0;
  for (let i = 0; i < 6; i++) {
    m.delta(4, 30, t);
    t += 0.016;
  }
  assert.equal(m.state, S.ignoring, 'the axis lock should have gone vertical');

  // The fingers lift. Silence is the only signal a trackpad gives.
  for (let i = 0; i < 20; i++) {
    t += 0.016;
    m.step(0.016, t);
  }
  assert.equal(m.state, S.idle,
               'silence must resolve `ignoring`, or every later delta is dropped');

  // A deliberate horizontal swipe now has to work.
  for (let i = 0; i < 10; i++) {
    m.delta(-40, 0, t);
    t += 0.016;
  }
  assert.ok(m.state === S.tracking || m.state === S.settlingCommit,
            `a swipe after a vertical gesture must track, got ${m.state}`);
});

// the real WheelEvent.momentum flag beats every heuristic.
//
// macOS already labels each wheel event as fingers-on or coasting, Blink
// exposes it as WheelEvent.momentum (stable, verified live on the surface),
// and the SwiftUI arm read the same thing off NSEvent.momentumPhase. The
// suppression machinery below only exists because the HTML port did not.
test('a finger event during the tail is a new gesture, whatever its shape', () => {
  const m = createSwipeMachine({spaceCount: 5, width: 189});

  let t = 0;
  for (let i = 0; i < 5; i++) {           // a flick that commits, ~200px
    m.delta(-42, 0, t, false);
    m.step(0.016, t);
    t += 0.016;
  }
  for (let i = 0; i < 40; i++) {          // let the commit settle
    t += 0.016;
    m.step(0.016, t);
  }
  const after = m.spaceIndex;
  assert.equal(after, 1, 'the first flick pages one space');

  for (let i = 0; i < 20; i++) {          // its momentum tail, labelled
    m.delta(-Math.max(2, 90 - i * 5), 0, t, true);
    m.step(0.016, t);
    t += 0.016;
  }
  assert.equal(m.spaceIndex, after, 'the tail must not page a second space');

  // A finger event, deliberately SMALL and mid-tail - exactly the shape every
  // magnitude heuristic gets wrong, and unambiguous with the flag.
  for (let i = 0; i < 5; i++) {
    m.delta(-42, 0, t, false);
    m.step(0.016, t);
    t += 0.016;
  }
  for (let i = 0; i < 40; i++) {
    t += 0.016;
    m.step(0.016, t);
  }
  assert.equal(m.spaceIndex, after + 1,
               'a finger event during the tail must start a new gesture');
});

test('a labelled momentum tail alone never pages a space', () => {
  const m = createSwipeMachine({spaceCount: 5, width: 189});
  let t = 0;
  for (let i = 0; i < 40; i++) {
    m.delta(-200, 0, t, true);            // huge, but coasting
    m.step(0.016, t);
    t += 0.016;
  }
  assert.equal(m.spaceIndex, 0, 'momentum alone must never page');
});

// one continuous swipe pages exactly ONE space, however long it is.
//
// The machine commits and suppresses on reaching a full space width, but
// the rule "a finger event during suppression is a new gesture" then cleared
// that suppression with the rest of the same swipe - so a long drag tunnelled
// through several spaces. The lift qualifier is what separates the two.
test('one long continuous swipe pages exactly one space', () => {
  const m = createSwipeMachine({spaceCount: 5, width: 189});
  let t = 0;
  // 60 finger events at a real trackpad cadence, no lift: ~2500px, more than
  // thirteen space widths.
  for (let i = 0; i < 60; i++) {
    m.delta(-42, 0, t, false);
    m.step(0.016, t);
    t += 0.016;
  }
  for (let i = 0; i < 60; i++) {
    t += 0.016;
    m.step(0.016, t);
  }
  assert.equal(m.spaceIndex, 1,
               `a single swipe must page one space, paged ${m.spaceIndex}`);
});

// The other half of the same rule: once the fingers DO lift, the next swipe
// must page again. Same cadence, with one real gap in the middle.
test('a lift between two swipes still pages twice', () => {
  const m = createSwipeMachine({spaceCount: 5, width: 189});
  let t = 0;
  const swipe = () => {
    for (let i = 0; i < 6; i++) {
      m.delta(-42, 0, t, false);
      m.step(0.016, t);
      t += 0.016;
    }
    for (let i = 0; i < 30; i++) {
      t += 0.016;
      m.step(0.016, t);
    }
  };
  swipe();
  assert.equal(m.spaceIndex, 1);
  t += 0.12;              // the fingers leave the trackpad
  swipe();
  assert.equal(m.spaceIndex, 2, 'a swipe after a real lift must page again');
});

// a re-swipe that arrives while the previous tail is STILL RUNNING.
//
// Captured verbatim from the user's trackpad, with the real momentum flag this
// time (build/fixtures/wheel-capture-1786018499687.json, t=5816-7400). Two
// deliberate flicks the same way, 668ms apart, both easily strong enough to
// commit on their own.
//
// There is NO SILENCE between them. The first flick's tail is still emitting
// when the fingers come back down - the last momentum event is 49ms before the
// first finger event - so every rule that waits for a quiet stretch to infer
// the lift misses this one.
//
// This replaces an earlier two-column capture that had been marked todo.
// That one recorded only time and dx, so it could not exercise
// the momentum flag the fix turns on, and there was no way to add the column
// afterwards without inventing it.
//
// Third column is WheelEvent.momentum: 1 coasting, 0 fingers on the glass.
const REAL_TAIL_RESWIPE = [
  [0, -13, 0], [0.068, -221, 0], [0.085, -104, 0], [0.101, -140, 0],
  [0.118, -140, 1], [0.134, -138, 1], [0.151, -131, 1], [0.168, -125, 1],
  [0.185, -119, 1], [0.201, -113, 1], [0.218, -105, 1], [0.234, -100, 1],
  [0.251, -96, 1], [0.268, -92, 1], [0.284, -87, 1], [0.301, -82, 1],
  [0.318, -77, 1], [0.334, -72, 1], [0.351, -68, 1], [0.368, -63, 1],
  [0.384, -59, 1], [0.401, -55, 1], [0.418, -51, 1], [0.435, -47, 1],
  [0.451, -43, 1], [0.468, -41, 1], [0.485, -37, 1], [0.501, -35, 1],
  [0.518, -32, 1], [0.535, -29, 1], [0.551, -26, 1], [0.568, -24, 1],
  [0.585, -22, 1], [0.601, -20, 1], [0.619, -19, 1], [0.668, -9, 0],
  [0.701, -91, 0], [0.718, -120, 0], [0.736, -147, 0], [0.751, -141, 0],
  [0.767, -146, 0], [0.785, -167, 1], [0.802, -160, 1], [0.818, -152, 1],
  [0.835, -142, 1], [0.851, -136, 1], [0.868, -128, 1], [0.885, -119, 1],
  [0.901, -114, 1], [0.919, -109, 1], [0.942, -104, 1], [0.959, -192, 1],
  [0.975, -90, 1], [0.984, -85, 1], [1.001, -81, 1], [1.018, -76, 1],
  [1.036, -72, 1], [1.052, -66, 1], [1.068, -62, 1], [1.084, -58, 1],
  [1.102, -54, 1], [1.117, -51, 1], [1.134, -47, 1], [1.151, -43, 1],
  [1.168, -40, 1], [1.185, -37, 1], [1.202, -34, 1], [1.218, -31, 1],
  [1.235, -28, 1],
];

test('a re-swipe into a live momentum tail still pages a second space', () => {
  const m = createSwipeMachine({spaceCount: 5, width: 189});
  m.spaceIndex = 2;                       // mid-rail, so both flicks have room
  const seen = [];
  let last = m.spaceIndex;
  for (const [t, dx, mom] of REAL_TAIL_RESWIPE) {
    m.delta(-dx, 0, t, !!mom);
    m.step(0.016, t);
    if (m.spaceIndex !== last) {
      seen.push(m.spaceIndex);
      last = m.spaceIndex;
    }
  }
  // The second glide outlives the capture.
  settle(m, REAL_TAIL_RESWIPE.at(-1)[0]);
  if (m.spaceIndex !== last) {
    seen.push(m.spaceIndex);
  }
  assert.equal(seen.length, 2,
               `two flicks must page two spaces, paged ${seen.length}: ${seen}`);
});
