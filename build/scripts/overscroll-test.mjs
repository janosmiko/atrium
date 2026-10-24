#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the tab list's vertical rubber band, driven headlessly.
//
//   make webui-test
//
// The model holds no DOM, for the same reason the swipe machine does not: "does
// it spring back, and with what damping" is otherwise a question you can only
// answer by feel, one gesture at a time.
//
// Deltas arrive the way the wheel listener passes them - already sign-flipped,
// same convention as the rail - so dy < 0 pulls the content UP, toward the end
// of the list.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const RES = path.join(SRC, 'chrome/browser/resources/atrium');
const {createOverscrollModel} = await import(path.join(RES, 'overscroll.js'));
const {DEFAULT_TUNING, resistPast} = await import(path.join(RES, 'swipe.js'));

const FRAME = 1 / 60;

// A list SHORTER than its viewport - the case the task is about, where native
// overflow does nothing at all.
const noOverflow = () => ({scrollTop: 0, scrollHeight: 200, clientHeight: 400});
// A list that overflows, parked in the middle with room to move either way.
const midScroll = () => ({scrollTop: 300, scrollHeight: 1200, clientHeight: 400});

function pull(m, {dy, steps = 10, t0 = 0, geom = noOverflow}) {
  let t = t0;
  for (let i = 0; i < steps; i++) {
    t += 0.016;
    m.delta(dy / steps, t, geom());
  }
  return t;
}

function settle(m, t, frames = 600) {
  for (let i = 0; i < frames && m.offset !== 0; i++) {
    t += FRAME;
    m.step(FRAME, t);
  }
  return t;
}

test('a vertical gesture moves a list that does not overflow', () => {
  const m = createOverscrollModel();
  pull(m, {dy: 240});
  assert.ok(m.offset > 0,
            `an unoverflowing list must still take the gesture, offset ${m.offset}`);
});

test('the pull resists rather than tracking the finger one to one', () => {
  const m = createOverscrollModel();
  pull(m, {dy: 240});
  assert.ok(m.offset < 240,
            `240px of input must not yield 240px of travel, got ${m.offset}`);
});

// The criterion the task states as "the SAME asymptotic resistance the rail
// uses at its ends". Asserted by recomputing the travel through the rail's own
// exported curve rather than against a number copied out of a run - swap in a
// different easing, a tanh or a plain clamp, and this fails.
//
// The one adaptation is spelled out here rather than hidden: the rail's cap is
// in pane units and a list has no pane to be a fraction of, so pixels of input
// are scaled by overscrollScale before entering the same curve.
test('the resistance curve is the rail own, not a lookalike', () => {
  const m = createOverscrollModel();
  const {overscrollCap: cap, overscrollScale: scale} = m.tuning;
  for (const input of [40, 240, 1000]) {
    const fresh = createOverscrollModel();
    pull(fresh, {dy: input, steps: 1});
    const expected = resistPast(input / scale * cap, cap);
    assert.equal(fresh.offset.toFixed(4), expected.toFixed(4),
                 `${input}px of input must follow the rail's curve`);
  }
});

test('travel is bounded however hard the list is pulled', () => {
  const m = createOverscrollModel();
  pull(m, {dy: 100000, steps: 50});
  assert.ok(m.offset < m.tuning.overscrollCap,
            `offset ${m.offset} must stay under the cap ${m.tuning.overscrollCap}`);
});

test('release springs the list back to rest', () => {
  const m = createOverscrollModel();
  const t = pull(m, {dy: 240});
  assert.ok(m.offset > 0);
  m.end(t);
  settle(m, t);
  assert.equal(m.offset, 0, 'the list must come to rest exactly at zero');
});

// "the same damping the rail uses". Both springs are built from
// springDamp and settleDur, so a change to either moves both surfaces together.
test('the spring is built from the rail damping constants', () => {
  const m = createOverscrollModel();
  assert.equal(m.tuning.springDamp, DEFAULT_TUNING.springDamp);
  assert.equal(m.tuning.settleDur, DEFAULT_TUNING.settleDur);
});

test('the spring does not overshoot past zero on the way back', () => {
  const m = createOverscrollModel();
  let t = pull(m, {dy: 240});
  m.end(t);
  let worst = 0;
  for (let i = 0; i < 600 && m.offset !== 0; i++) {
    t += FRAME;
    m.step(FRAME, t);
    worst = Math.min(worst, m.offset);
  }
  assert.ok(worst > -1, `a critically damped return must not bounce, dipped to ${worst}`);
});

// A list with somewhere to scroll must be left alone entirely, or the
// rubber band eats ordinary scrolling.
test('a scrollable list mid-way is not taken at all', () => {
  const m = createOverscrollModel();
  const taken = m.delta(-40, 0.016, midScroll());
  assert.equal(taken, false, 'native scrolling owns this gesture');
  assert.equal(m.offset, 0);
});

test('an overflowing list still bands once it reaches its end', () => {
  const m = createOverscrollModel();
  const atBottom = () => ({scrollTop: 800, scrollHeight: 1200, clientHeight: 400});
  pull(m, {dy: -240, geom: atBottom});
  assert.ok(m.offset < 0, `past the bottom edge the band must engage, got ${m.offset}`);
});

test('an overflowing list at its end is still free to scroll back', () => {
  const m = createOverscrollModel();
  const atBottom = () => ({scrollTop: 800, scrollHeight: 1200, clientHeight: 400});
  assert.equal(m.delta(40, 0.016, atBottom()), false,
               'scrolling back toward the middle is ordinary scrolling');
});

// The guard above declines only while accum is 0, so once the band
// engaged at an edge it claimed the whole rest of the gesture and damped it. A
// trackpad flick never pauses long enough for end() to reset accum.
test('a gesture that unwinds the band hands the rest back to the list', () => {
  const m = createOverscrollModel();
  const atTop = () => ({scrollTop: 0, scrollHeight: 1200, clientHeight: 400});
  assert.equal(m.delta(60, 0.016, atTop()), true, 'the band takes the pull');
  assert.ok(m.offset > 0, 'and it moved');

  // Back the other way, past where the band started.
  assert.equal(m.delta(-90, 0.032, atTop()), false,
               'the rest of the gesture is ordinary scrolling');
  assert.equal(m.offset, 0, 'and the band is out of the way');
  assert.equal(m.dragging, false,
               'a momentum event arriving now must not read as a mid-drag lift');

  // Still scrolling, still not the band's.
  assert.equal(m.delta(-90, 0.048, midScroll()), false);
});

// A pull that only eases off is still the band's: releasing it early would
// snap the list while the finger still holds it away from the edge.
test('easing off without crossing zero stays with the band', () => {
  const m = createOverscrollModel();
  const atTop = () => ({scrollTop: 0, scrollHeight: 1200, clientHeight: 400});
  m.delta(60, 0.016, atTop());
  assert.equal(m.delta(-20, 0.032, atTop()), true, 'still the band s');
  assert.ok(m.offset > 0, 'and still held away from the edge');
});

// the horizontal gesture belongs to the rail and must pass straight
// through, or a swipe would drag the tab list around with it.
test('a horizontal gesture is not taken', () => {
  const m = createOverscrollModel();
  assert.equal(m.delta(0, 0.016, noOverflow()), false);
  assert.equal(m.offset, 0);
});

test('reduced motion returns to rest without a spring', () => {
  const m = createOverscrollModel({reducedMotion: true});
  const t = pull(m, {dy: 240});
  m.end(t);
  m.step(FRAME, t + FRAME);
  assert.equal(m.offset, 0, 'reduced motion snaps back rather than animating');
});

test('a second pull during the return takes over cleanly', () => {
  const m = createOverscrollModel();
  let t = pull(m, {dy: 240});
  m.end(t);
  t += FRAME;
  m.step(FRAME, t);
  const midReturn = m.offset;
  assert.ok(midReturn > 0, 'precondition: still returning');
  t += 0.016;
  m.delta(40, t, noOverflow());
  assert.ok(m.offset >= midReturn,
            'a new pull must continue from where the list is, not jump');
});
