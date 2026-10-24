#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// createSwipeController, the DOM half of the workspace swipe.
//
//   make webui-test
//
// swipe-machine-test.mjs already answers "does this gesture commit?". This
// suite asks the questions that only exist once the machine is wired to a
// browser: does a wheel stream reach it, does the rail get painted once per
// frame rather than once per event, and does the browser hear about a space
// change exactly once.
//
// The bugs it exists to catch are the ones the machine cannot see:
//   - a paint per wheel event (a trackpad sends ~60/s and macOS batches none
//     of them), which is a re-layout storm the machine would never notice;
//   - a second sidebarSpaceSelected for one swipe, which pages the browser
//     twice;
//   - attach() adopting the model's active space while the surface's own
//     announcement is still unacknowledged. The browser switches spaces
//     asynchronously, so the next push can still name the OLD space, and
//     adopting it drags the rail back and then forward again.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {createSwipeController} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/swipe.js'));

const W = 260;
const FRAME = 16;

// A fresh DOM per surface, not reset(): the controller adds a resize listener
// to window and never removes it, so a shared window would accumulate one per
// test and repaint rails belonging to controllers that are long gone.
function surface({count = 4, active = 0, width = W, onIdle, attach = true,
                  leadingPanes} = {}) {
  const h = installDOM();
  const root = h.document.createElement('div');
  // The shim has no layout engine and no clientWidth, and the controller reads
  // the sidebar width off root every frame, so the test declares it.
  root.clientWidth = width;
  const rail = h.document.createElement('div');
  rail.className = 'rail';
  root.appendChild(rail);
  h.body.appendChild(root);
  const c = createSwipeController(root, {onIdle, leadingPanes});
  if (attach) {
    c.attach(count, active);
  }
  return {h, root, rail, c, width};
}

// the listener negates deltaX again, so a POSITIVE deltaX here moves
// toward the NEXT space. The negation was dropped on 2026-08-10 to match
// Cmd+Shift+Right and the user overturned that on real hardware 2026-08-17 - a
// rightward swipe goes back, as it does in Safari and Arc. Negated HERE rather
// than by re-flipping every fixture literal below: the tests are named after
// physical gestures, and the sign that reaches the machine is the listener's
// business, not each test's.
function wheel(target, {deltaX = 0, deltaY = 0, momentum} = {}) {
  const e = new Event('wheel', {bubbles: true, deltaX: -deltaX, deltaY, momentum});
  target.dispatchEvent(e);
  return e;
}

function railX(rail) {
  const m = /^translateX\((-?[\d.]+)px\)$/.exec(rail.style.transform || '');
  assert.ok(m, `expected a rail transform, got ${rail.style.transform}`);
  return Number(m[1]);
}

// Absolute rail position in pane units: 0 sits on space 0, 1 on space 1.
const railPos = (rail, width = W) => -railX(rail) / width;

const commits = (h) => h.sent.filter(([name]) => name === 'sidebarSpaceSelected');

// One wheel event and the frame that follows it, the way the two interleave on
// a real trackpad.
function drag(h, root, {events, deltaX}) {
  for (let i = 0; i < events; i++) {
    wheel(root, {deltaX});
    h.tick(FRAME);
  }
}

// Frames until the loop parks itself, so assertions land at rest rather than
// on a mid-spring sample.
function runToRest(h, c, maxFrames = 400) {
  for (let i = 0; i < maxFrames; i++) {
    h.tick(FRAME);
    if (!c.isBusy()) {
      return;
    }
  }
  assert.fail('the rail never came to rest');
}

// The rail transform is the only paint this surface does, so counting writes
// to it counts paints.
function recordPaints(rail) {
  const writes = [];
  Object.defineProperty(rail.style, 'transform', {
    configurable: true,
    get: () => writes[writes.length - 1] ?? '',
    set: (v) => writes.push(v),
  });
  return writes;
}

test('a wheel stream slides the rail toward the next space', () => {
  const {h, root, rail, c} = surface();
  assert.equal(railX(rail), 0);
  drag(h, root, {events: 6, deltaX: -12});
  const p = railPos(rail);
  assert.ok(p > 0.02 && p < 1, `rail should be part way to space 1, got ${p}`);
  assert.equal(c.spaceIndex(), 0, 'nothing is committed mid-gesture');
});

test('a burst of wheel events paints once per frame, not once per event', () => {
  // A trackpad delivers ~60 events a second and the browser coalesces none of
  // them. Writing the transform per event is a re-layout per event.
  const {h, root, rail} = surface();
  const painted = recordPaints(rail);
  for (let i = 0; i < 5; i++) {
    wheel(root, {deltaX: 12});
  }
  assert.equal(painted.length, 0, 'a wheel event must not paint synchronously');
  assert.equal(h.pendingFrames, 1, 'five events, one scheduled frame');
  h.tick(FRAME);
  assert.equal(painted.length, 1, 'one frame, one paint');
});

test('a committed swipe announces the new space exactly once', () => {
  const {h, root, c} = surface();
  drag(h, root, {events: 10, deltaX: -14});
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 1);
  assert.deepEqual(commits(h), [['sidebarSpaceSelected', [1]]]);
});

// C++ swaps the web card the moment this message lands, so the announce time
// is when the old page stops drawing.
test('a committed swipe announces while the rail is still travelling', () => {
  const {h, root, rail, c} = surface();
  // Four events cross the threshold, so the commit lands inside the drag.
  drag(h, root, {events: 4, deltaX: -14});
  const at = railPos(rail);
  assert.deepEqual(commits(h), [['sidebarSpaceSelected', [1]]]);
  // spaceIndex is what settleStep moves on arrival.
  assert.equal(c.spaceIndex(), 0, 'announced before the settle landed');
  assert.ok(at !== null && at < 0.9, `rail should still be in transit, at ${at}`);
  runToRest(h, c);
  assert.deepEqual(commits(h), [['sidebarSpaceSelected', [1]]],
      'and the arrival does not announce it a second time');
});

// SelectSpace glides the rail to the space it was just told about, and that
// echo now arrives on the gesture's own settle rather than after it.
test('the browser echoing the commit back does not restart the settle', () => {
  const {h, root, c} = surface();
  drag(h, root, {events: 10, deltaX: -14});
  for (let i = 0; i < 60 && commits(h).length === 0; i++) {
    h.tick(FRAME);
  }
  c.jumpTo(1);
  assert.equal(c.isGliding(), false, 'the gesture still owns the settle');
  assert.equal(c.debug().swallowing, true, 'the momentum tail is still swallowed');
  assert.deepEqual(
      h.sent.filter(([name]) => name === 'sidebarSpaceScrubEnded'), [],
      'the backdrop scrub was not ended uncommitted');
  runToRest(h, c);
  assert.deepEqual(commits(h), [['sidebarSpaceSelected', [1]]]);
});

// The announce now opens a window the arrival used to close in the same frame:
// a rebuild can land while the settle still runs, carrying the space the model
// held BEFORE the switch.
test('a rebuild between the announce and the arrival keeps the announced space', () => {
  const {h, root, rail, c} = surface();
  drag(h, root, {events: 10, deltaX: -14});
  for (let i = 0; i < 60 && commits(h).length === 0; i++) {
    h.tick(FRAME);
  }
  assert.deepEqual(commits(h), [['sidebarSpaceSelected', [1]]]);
  c.attach(4, 0);
  assert.equal(railPos(rail), 1, 'the rebuild landed on the announced space');
  h.tick(FRAME);
  assert.equal(railPos(rail), 1, 'and the spring did not resume from mid-transit');
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 1, 'and the spring did not carry it back');
  assert.equal(railPos(rail), 1);
  assert.deepEqual(commits(h), [['sidebarSpaceSelected', [1]]],
      'the browser was told once and never contradicted');
});

test('a swipe that rolls back tells the browser nothing', () => {
  // Below both commit thresholds. A spurious message here switches the user's
  // space on a gesture they abandoned.
  const {h, root, c} = surface();
  drag(h, root, {events: 4, deltaX: 6});
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 0);
  // The scrub messages are the backdrop tracking the finger, not
  // a switch - those are checked in their own tests below.
  assert.deepEqual(commits(h), []);
});

const ticks = (h) => h.sent.filter(([name]) => name === 'sidebarSwipeHaptic');

// The wheel events, counted from 1, after which a tap arrived.
function dragTaps(h, root, {events, deltaX}) {
  const at = [];
  for (let i = 1; i <= events; i++) {
    const before = ticks(h).length;
    drag(h, root, {events: 1, deltaX});
    if (ticks(h).length > before) {
      at.push(i);
    }
  }
  return at;
}

// The paint count at each tap, so a test can tell which frame tapped.
function recordTaps(rail) {
  const paints = recordPaints(rail);
  const taps = [];
  const send = globalThis.chrome.send;
  globalThis.chrome.send = (name, args) => {
    if (name === 'sidebarSwipeHaptic') {
      taps.push(paints.length);
    }
    send(name, args);
  };
  return {paints, taps};
}

const posOf = (transform, width = W) =>
  -Number(/translateX\((-?[\d.]+)px\)/.exec(transform)[1]) / width;

test('a slow drag taps at each detent and at the commit', () => {
  const {h, root, c} = surface();
  // 4px events: detents at 13px and 26px, the commit at 39px.
  const at = dragTaps(h, root, {events: 10, deltaX: -4});
  assert.equal(c.targetIndex(), 1, 'the drag committed');
  assert.equal(at.length, 3, `taps after events ${at}`);
});

const strengths = (h) => ticks(h).map(([, [strength]]) => strength);

test('crisp, the default, taps soft detents and a plain commit and landing', () => {
  const {h, root, c} = surface();
  // 4px events: two soft detents, then the commit.
  drag(h, root, {events: 10, deltaX: -4});
  assert.deepEqual(strengths(h), ['soft', 'soft', 'plain']);
  runToRest(h, c);
  assert.deepEqual(strengths(h), ['soft', 'soft', 'plain', 'plain'],
      'no settle taps, then the landing');
});

test('smooth adds soft settle taps and a soft landing', () => {
  const {h, root, c} = surface();
  c.setHaptics('smooth');
  drag(h, root, {events: 10, deltaX: -4});
  assert.deepEqual(strengths(h), ['soft', 'soft', 'plain']);
  runToRest(h, c);
  const settle = strengths(h).slice(3);
  assert.ok(settle.length >= 6, `a tap every eighth pane: ${settle}`);
  assert.ok(settle.every((s) => s === 'soft'), `${settle}`);
});

test('off never taps', () => {
  const {h, root, c} = surface();
  c.setHaptics('off');
  drag(h, root, {events: 10, deltaX: -4});
  runToRest(h, c);
  assert.deepEqual(ticks(h), []);
});

test('an unknown haptics name falls back to crisp', () => {
  const {h, root, c} = surface();
  c.setHaptics('buzz');
  drag(h, root, {events: 10, deltaX: -4});
  assert.deepEqual(strengths(h), ['soft', 'soft', 'plain']);
});

test('a faster drag taps as often, closer together', () => {
  // One after the other: each surface installs its own global chrome.send.
  const slow = surface();
  const slowAt = dragTaps(slow.h, slow.root, {events: 10, deltaX: -4});
  const fast = surface();
  const fastAt = dragTaps(fast.h, fast.root, {events: 5, deltaX: -8});
  assert.equal(fastAt.length, slowAt.length);
  assert.ok(fastAt.at(-1) - fastAt[0] < slowAt.at(-1) - slowAt[0],
      `fast ${fastAt} against slow ${slowAt}`);
});

test('a committed smooth swipe taps while the rail settles and once when it lands', () => {
  const {h, root, rail, c} = surface();
  c.setHaptics('smooth');
  const {paints, taps} = recordTaps(rail);
  drag(h, root, {events: 3, deltaX: -14});
  const atCommit = taps.length;
  runToRest(h, c);
  const landing = paints.findIndex((v) => Math.abs(posOf(v) - 1) < 0.01) + 1;
  const settle = taps.slice(atCommit);
  assert.ok(settle.length >= 3, `settle taps at paints ${settle}`);
  assert.equal(settle.at(-1), landing, 'the last tap is the landing');
  assert.equal(settle.filter((p) => p === landing).length, 1);
});

test('a rolled back swipe taps only while the fingers drag', () => {
  const {h, root, c} = surface();
  drag(h, root, {events: 4, deltaX: 6});
  const dragged = ticks(h).length;
  assert.equal(dragged, 1, 'one detent at 13px of 24px');
  runToRest(h, c);
  assert.equal(ticks(h).length, dragged);
});

test('a jumpTo never taps', () => {
  const {h, c} = surface();
  c.jumpTo(3);
  runToRest(h, c);
  assert.deepEqual(ticks(h), []);
});

test('momentum never taps, even when it carries the drag to a commit', () => {
  const {h, root, c} = surface();
  // 8px of finger: past the axis lock, short of the first detent.
  drag(h, root, {events: 2, deltaX: -4});
  for (let i = 0; i < 5; i++) {
    wheel(root, {deltaX: -14, momentum: true});
    h.tick(FRAME);
  }
  runToRest(h, c);
  assert.deepEqual(ticks(h), []);
});

test('a flick that takes over a settle and commits in one event taps', () => {
  const {h, root, c} = surface();
  c.setHaptics('smooth');
  drag(h, root, {events: 3, deltaX: -14});
  // A momentum event proves the fingers lifted, so the next flick is new.
  wheel(root, {deltaX: -2, momentum: true});
  h.tick(FRAME);
  const before = ticks(h).length;
  wheel(root, {deltaX: -60, momentum: false});
  assert.equal(c.targetIndex(), 2, 'the flick committed the next space');
  assert.equal(ticks(h).length, before + 1, 'the commit tapped');
  runToRest(h, c);
  assert.ok(ticks(h).length >= before + 3, 'its settle and landing tapped');
});

test('a real captured trackpad flick never taps after the rail lands', () => {
  const {h, root, rail, c} = surface({count: 5, width: 170});
  const {paints, taps} = recordTaps(rail);
  replay(h, root, REAL_FLICK);
  runToRest(h, c);
  const landing =
      paints.findIndex((v) => Math.abs(posOf(v, 170) - 1) < 0.01) + 1;
  assert.ok(landing > 0, 'the rail landed on the next space');
  assert.ok(taps.length >= 2, `taps at paints ${taps}`);
  assert.ok(taps.every((p) => p <= landing),
      `the momentum tail tapped: taps ${taps}, landing ${landing}`);
});

// the settings surface calls this on a controller it built
// long ago, not at construction - proving the machine underneath actually
// reads the new tuning, not just that setTuning stored it somewhere.
test('setTuning changes what an already-running controller commits', () => {
  const {h, root, c} = surface();
  c.setTuning({commitThreshold: 0.05});
  // Same size gesture the untouched-default test above rolls back with
  // nothing sent - below the default threshold, but past the lowered one.
  drag(h, root, {events: 4, deltaX: -6});
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 1, 'the lowered threshold now commits it');
  assert.deepEqual(commits(h), [['sidebarSpaceSelected', [1]]]);
});

test('onIdle fires once, when the rail comes to rest', () => {
  let idle = 0;
  const {h, root, c} = surface({onIdle: () => idle++});
  drag(h, root, {events: 10, deltaX: 14});
  assert.equal(idle, 0, 'not while the gesture is live');
  assert.equal(c.isBusy(), true);
  runToRest(h, c);
  assert.equal(idle, 1);
  assert.equal(c.isBusy(), false, 'callers gate their re-render on this');
});

// A REAL trackpad flick, captured with make wheel-capture on the machine that
// reported the bug (170px sidebar). Kept verbatim, and identical to the copy
// pinned in swipe-machine-test.mjs: no simulated tail ever reproduced the bug,
// because macOS ends the finger phase and then RESTARTS momentum at high
// velocity after a pause of its own (47px, a 102ms gap, then 182px).
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

// Replays a capture at its recorded wall-clock times, running frames in the
// gaps. The controller reads its own clock, so the shim's is set rather than
// advanced.
//
// the listener no longer negates deltaX, so the raw capture (still
// verbatim, still identical to swipe-machine-test.mjs's copy) is negated
// here at the call site instead - the same place swipe-machine-test.mjs's own
// replay() already does it (`m.delta(-dx, ...)`). Editing the captured
// numbers themselves would have desynced the two "identical" copies silently.
function replay(h, root, stream) {
  let prev = stream[0][0];
  for (const [ms, deltaX] of stream) {
    for (let at = prev + FRAME; at < ms; at += FRAME) {
      h.setNow(at);
      h.tick(0);
    }
    h.setNow(ms);
    h.tick(0);
    wheel(root, {deltaX: -deltaX});
    prev = ms;
  }
}

test('a real captured trackpad flick pages one space and announces it once', () => {
  const {h, root, c} = surface({count: 5, width: 170});
  replay(h, root, REAL_FLICK);
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 1,
      'the momentum phase must not be taken for a second swipe');
  assert.deepEqual(commits(h), [['sidebarSpaceSelected', [1]]],
      'one flick is one message to the browser');
});

test('attach adopts the space the model says is active', () => {
  const {rail, c} = surface({count: 4, active: 0});
  c.attach(4, 2);
  assert.equal(c.spaceIndex(), 2);
  assert.equal(railPos(rail), 2, 'and the rail is painted on it');
});

test('attach does not announce back the space the model just named', () => {
  // The rail adopted 2 because the model said 2. Announcing 2 is an echo, and
  // it reaches the browser as a space switch nobody asked for - the same loop
  // the unacknowledged-commit hold exists to break, running the other way.
  const {h, root, c} = surface({count: 4, active: 0});
  c.attach(4, 2);
  assert.equal(c.spaceIndex(), 2);
  // Any gesture will do; it just has to run the loop past the point where a
  // stale `announced` would notice the mismatch.
  drag(h, root, {events: 3, deltaX: 6});
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 2, 'the nudge is under both thresholds');
  assert.deepEqual(commits(h), []);
});

test('attach with no active space keeps the one it had', () => {
  // A caller with nothing to offer must behave as it did before the model
  // gained a space producer.
  const {c} = surface({count: 4, active: 2});
  c.attach(4);
  assert.equal(c.spaceIndex(), 2);
});

test('attach clamps an active space the snapshot no longer has', () => {
  const {c} = surface();
  c.attach(2, 7);
  assert.equal(c.spaceIndex(), 1);
  assert.equal(c.spaceCount(), 2);
  c.attach(0, 0);
  assert.equal(c.spaceCount(), 1, 'a snapshot with no spaces still leaves one');
});

// the optimistic overlay. Believe the model, except where the
// surface has said something the model has not answered yet.
test('attach ignores a stale active space while the commit is unacknowledged', () => {
  const {h, root, rail, c} = surface();
  drag(h, root, {events: 10, deltaX: -14});
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 1);

  // The push that predates the browser's switch still names the old space.
  c.attach(4, 0);
  assert.equal(c.spaceIndex(), 1, 'the local index holds until the model agrees');
  assert.equal(railPos(rail), 1, 'and the rail does not creep back a space');
});

test('attach adopts the model again once it acknowledges the announced space', () => {
  // The other half: the hold is until the ack, not forever. Held forever, a
  // space switched from anywhere else would never reach the rail again.
  const {h, root, c} = surface();
  drag(h, root, {events: 10, deltaX: 14});
  runToRest(h, c);

  c.attach(4, 1);
  assert.equal(c.spaceIndex(), 1, 'the ack');
  c.attach(4, 0);
  assert.equal(c.spaceIndex(), 0, 'after the ack the model wins again');
});

test('attach lands the rail at rest rather than part way between panes', () => {
  // A rebuild replaces the rail's nodes, so an in-flight offset describes
  // geometry that no longer exists.
  const {h, root, rail, c} = surface();
  // Two events stay under the threshold, so the gesture is still live.
  drag(h, root, {events: 2, deltaX: -14});
  assert.ok(railPos(rail) > 0, 'mid-gesture the rail sits between panes');
  c.attach(4, 0);
  assert.equal(railX(rail), 0);
});

// overshoot between two spaces is a bounce and stays. Past the first
// space lies the Overview, a whole surface rather than a sidebar pane, so a
// spring dipping into it reads as the Overview arriving.

// The lowest position the rail is painted at over a whole settle. The clamp is
// invisible at rest, so only a per-frame sample can see it.
function lowestRailPos(h, c, rail, maxFrames = 400) {
  let low = railPos(rail);
  for (let i = 0; i < maxFrames; i++) {
    h.tick(FRAME);
    low = Math.min(low, railPos(rail));
    if (!c.isBusy()) {
      return low;
    }
  }
  assert.fail('the rail never came to rest');
}

test('settling onto the first space never paints into the Overview', () => {
  const {h, rail, c} = surface({count: 3, active: 2, leadingPanes: 1});
  c.jumpTo(1);
  const low = lowestRailPos(h, c, rail);
  assert.ok(low >= 1 - 1e-9,
            `the spring reached ${low.toFixed(3)}, inside the Overview`);
  assert.equal(railPos(rail), 1, 'and it still lands on the first space');
});

test('leaving the Overview paints every position on the way out', () => {
  // Applied to a settle that starts ON the Overview, the clamp pinned every
  // frame at the first space, so the rows over the rail read a share of 0 from
  // the moment the finger lifted and snapped home.
  const {h, rail, c} = surface({count: 3, active: 0, leadingPanes: 1});
  const seen = [];
  c.jumpTo(1);
  for (let i = 0; i < 400 && c.isBusy(); i++) {
    h.tick(FRAME);
    seen.push(railPos(rail));
  }
  const between = seen.filter((at) => at > 0.05 && at < 0.95);
  assert.ok(between.length >= 3,
            `the rail jumped the crossing, positions ${JSON.stringify(seen.slice(0, 6))}`);
  assert.equal(railPos(rail), 1, 'and it still lands on the first space');
});

test('the pinned grid rides the overshoot past its own pane', () => {
  // The spring off the Overview sweeps past the first space and back. The
  // profile span clamps to the landing pane, so the copy sat still while the
  // pane bounced under it.
  const {h, root, rail, c} = surface({count: 3, active: 0, leadingPanes: 1});
  const pinned = h.document.createElement('div');
  pinned.className = 'favgrid';
  pinned.dataset.profileId = 'home';
  root.insertBefore(pinned, rail);
  for (const id of ['', 'home', 'work']) {
    const pane = h.document.createElement('div');
    pane.className = 'railpane';
    pane.dataset.profileId = id;
    rail.appendChild(pane);
  }
  c.jumpTo(1);
  let past = 0;
  for (let i = 0; i < 400 && c.isBusy(); i++) {
    h.tick(FRAME);
    if (railPos(rail) > 1 + 1e-6) {
      past++;
      assert.equal(pinned.style.transform,
                   `translateX(${(railX(rail) + W).toFixed(2)}px)`,
                   `frame ${i}: the copy left pane 1 at ${railPos(rail)}`);
    }
  }
  assert.ok(past >= 2, `the spring never overshot, ${past} frames past`);
  assert.equal(pinned.style.transform, '', 'and it rests untransformed');
});

test('a drag toward the Overview still reveals it', () => {
  // The other half, and the reason the clamp is on the settle only: dragging
  // into the Overview is how the user opens it.
  const {h, root, rail, c} = surface({count: 3, active: 1, leadingPanes: 1});
  drag(h, root, {events: 4, deltaX: 14});
  const at = railPos(rail);
  assert.ok(at < 1, `a live drag stopped at ${at.toFixed(3)}, revealing nothing`);
});

test('overshoot between two ordinary spaces is unchanged', () => {
  // The clamp is a floor at the first space, not a ban on overshoot: a settle
  // that lands well clear of the Overview still bounces past its target.
  const {h, rail, c} = surface({count: 4, active: 3, leadingPanes: 1});
  c.jumpTo(2);
  const low = lowestRailPos(h, c, rail);
  assert.ok(low < 2, `the spring stopped dead at ${low.toFixed(3)}`);
  assert.equal(railPos(rail), 2);
});

test('jumpTo slides to the named space and announces it once', () => {
  const {h, rail, c} = surface({count: 4});
  assert.equal(c.jumpTo(3), 3, 'it reports where it will land');
  assert.equal(c.isBusy(), true);
  assert.equal(c.spaceIndex(), 0, 'not landed yet');
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 3);
  assert.equal(railPos(rail), 3);
  assert.deepEqual(commits(h), [['sidebarSpaceSelected', [3]]]);
});

// Panes 1 to 3 leave the rail, so 0 and 4 sit side by side and the slide is
// one pane wide. The transform counts rail slots, not panes, mid-glide.
test('a long jumpTo lifts the panes between off the rail and slides once', () => {
  const {h, rail, c} = surface({count: 5});
  const panes = [];
  for (let k = 0; k < 5; k++) {
    const pane = h.document.createElement('div');
    pane.className = 'railpane';
    rail.appendChild(pane);
    panes.push(pane);
  }
  c.jumpTo(4);
  for (let i = 0; i < 4; i++) {
    h.tick(FRAME);
  }
  assert.deepEqual(panes.map((p) => p.classList.contains('jumped')),
                   [false, true, true, true, false]);
  const mid = railPos(rail);
  assert.ok(mid > 0.05 && mid < 1.3, `one slot of travel at most, at ${mid}`);
  runToRest(h, c);
  assert.deepEqual(panes.map((p) => p.classList.contains('jumped')),
                   [false, false, false, false, false], 'every pane is back');
  assert.equal(railPos(rail), 4);
  assert.deepEqual(commits(h), [['sidebarSpaceSelected', [4]]]);
  // And back the other way: pane 4 sits in slot 1 while 1 to 3 are lifted.
  c.jumpTo(0);
  h.tick(FRAME);
  assert.equal(panes[2].classList.contains('jumped'), true);
  const back = railPos(rail);
  assert.ok(back > -0.3 && back < 1, `slot coordinates, at ${back}`);
  runToRest(h, c);
  assert.equal(Math.abs(railPos(rail)), 0);
});

// The two panes either side of the rail position, read through the lifted gap.
function panesOnScreen(panes, rail) {
  const slots = panes.map((p, k) => k)
      .filter((k) => !panes[k].classList.contains('jumped'));
  const at = railPos(rail);
  return [slots[Math.floor(at)], slots[Math.ceil(at)]];
}

test('a swipe that takes over a long jump keeps both panes on screen', () => {
  const {h, root, rail, c} = surface({count: 5});
  const panes = [];
  for (let k = 0; k < 5; k++) {
    const pane = h.document.createElement('div');
    pane.className = 'railpane';
    rail.appendChild(pane);
    panes.push(pane);
  }
  c.jumpTo(4);
  for (let i = 0; i < 4; i++) {
    h.tick(FRAME);
  }
  assert.deepEqual(panesOnScreen(panes, rail), [0, 4], 'mid-glide');
  wheel(root, {deltaX: 2});
  h.tick(FRAME);
  assert.deepEqual(panesOnScreen(panes, rail), [0, 4],
                   'the takeover frame must not swap 0 for 3');
  drag(h, root, {events: 12, deltaX: 14});
  runToRest(h, c);
  const staged = scrubs(h).filter(([name]) => name === 'sidebarSpaceScrubBegan')
      .map(([, [i]]) => i);
  assert.deepEqual(staged, [0], 'the backdrop blends toward the pane on screen');
  assert.equal(c.spaceIndex(), 0, 'dragging back returns to the pane shown');
  assert.equal(Math.abs(railPos(rail)), 0);
  assert.deepEqual(panes.map((p) => p.classList.contains('jumped')),
                   [false, false, false, false, false], 'every pane is back');
});

// Six frames is about 100ms, the pace of a hand clicking dots in a hurry.
function clickDots(h, c, targets, framesBetween = 6, onFrame) {
  for (const target of targets) {
    c.jumpTo(target);
    for (let i = 0; i < framesBetween; i++) {
      h.tick(FRAME);
      onFrame?.();
    }
  }
}

function railPanes(h, rail, count) {
  const panes = [];
  for (let k = 0; k < count; k++) {
    const pane = h.document.createElement('div');
    pane.className = 'railpane';
    rail.appendChild(pane);
    panes.push(pane);
  }
  return panes;
}

// How much of each pane shows. A pane can leave the screen only by sliding
// off, so one that shows any real part of itself cannot be gone a frame later.
function paneShares(panes, rail) {
  const [a, b] = panesOnScreen(panes, rail);
  const f = railPos(rail) - Math.floor(railPos(rail));
  const shares = new Map([[a, 1 - f]]);
  shares.set(b, (shares.get(b) || 0) + f);
  return shares;
}

function watchForPops(panes, rail) {
  let prev = null;
  const pops = [];
  return {
    frame() {
      const now = paneShares(panes, rail);
      for (const [k, share] of prev || []) {
        // Past what the spring moves in one frame.
        if (share > 0.1 && !now.has(k)) {
          pops.push(`pane ${k} at ${share.toFixed(2)} vanished, rail ${railPos(rail).toFixed(3)}`);
        }
      }
      prev = now;
    },
    pops,
  };
}

test('next-space presses in a quick burst slide through every space shown', () => {
  const {h, rail, c} = surface({count: 5});
  const panes = railPanes(h, rail, 5);
  const watch = watchForPops(panes, rail);
  clickDots(h, c, [1, 2, 3], 6, watch.frame);
  for (let i = 0; i < 200 && c.isBusy(); i++) {
    h.tick(FRAME);
    watch.frame();
  }
  assert.deepEqual(watch.pops, []);
  assert.equal(c.spaceIndex(), 3);
  assert.equal(railPos(rail), 3);
});

// The rail transform moves by panes here as lifted panes return to the
// layout, so only the panes on screen tell a pop.
test('three quick jumps across far dots never swap a pane in place', () => {
  for (let gap = 4; gap <= 24; gap += 2) {
    const {h, rail, c} = surface({count: 12});
    const panes = railPanes(h, rail, 12);
    const watch = watchForPops(panes, rail);
    clickDots(h, c, [4], 4, watch.frame);
    clickDots(h, c, [10], gap, watch.frame);
    clickDots(h, c, [6], 200, watch.frame);
    assert.deepEqual(watch.pops, [], `second jump held ${gap} frames`);
    assert.equal(c.spaceIndex(), 6);
  }
});

test('a dot between the two panes of a long glide never swaps either one', () => {
  for (let frames = 2; frames <= 14; frames += 2) {
    const {h, rail, c} = surface({count: 8});
    const panes = railPanes(h, rail, 8);
    const watch = watchForPops(panes, rail);
    clickDots(h, c, [6], frames, watch.frame);
    clickDots(h, c, [3], 200, watch.frame);
    assert.deepEqual(watch.pops, [], `pressed after ${frames} frames`);
    assert.equal(c.spaceIndex(), 3);
    assert.equal(railPos(rail), 3);
  }
});

test('a dot pressed while a swipe holds a long jump never swaps a pane in place', () => {
  const {h, root, rail, c} = surface({count: 6});
  const panes = railPanes(h, rail, 6);
  const watch = watchForPops(panes, rail);
  c.jumpTo(4);
  for (let i = 0; i < 4; i++) {
    h.tick(FRAME);
    watch.frame();
  }
  wheel(root, {deltaX: 2});
  h.tick(FRAME);
  watch.frame();
  c.jumpTo(5);
  for (let i = 0; i < 200 && c.isBusy(); i++) {
    h.tick(FRAME);
    watch.frame();
  }
  assert.deepEqual(watch.pops, []);
  assert.equal(c.spaceIndex(), 5);
});

test('dots clicked in a burst switch the browser only to the space the rail lands on', () => {
  // Each announcement is a whole space switch, so one per press blinks the
  // window. Press 1 is held for a short window to see whether a second comes.
  const {h, rail, c} = surface({count: 5});
  clickDots(h, c, [1, 3, 2, 4, 3]);
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 3, 'the last dot pressed wins');
  assert.equal(railPos(rail), 3);
  assert.deepEqual(commits(h), [['sidebarSpaceSelected', [3]]]);
});

test('a burst that ends on the space it started in announces nothing', () => {
  const {h, c} = surface({count: 5});
  clickDots(h, c, [1, 3, 1, 3, 0]);
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 0);
  assert.deepEqual(commits(h), []);
});

test('a single dot press announces after the hold window and before the rail lands', () => {
  const {h, c} = surface({count: 6});
  c.jumpTo(5);
  for (let i = 0; i < 6; i++) {
    h.tick(FRAME);
  }
  assert.deepEqual(commits(h), [], 'held inside the window');
  for (let i = 0; i < 14; i++) {
    h.tick(FRAME);
  }
  assert.deepEqual(commits(h), [['sidebarSpaceSelected', [5]]]);
  assert.equal(c.isBusy(), true, 'announced before the rail landed');
  runToRest(h, c);
  assert.deepEqual(commits(h), [['sidebarSpaceSelected', [5]]], 'landing does not announce again');
});

// Mid-burst the rail is still on the space it is leaving, and announcing that
// switched the browser back to it.
test('next-space presses in a burst never switch the browser backwards', () => {
  const {h, c} = surface({count: 5});
  clickDots(h, c, [1, 2, 3, 4], 3);
  runToRest(h, c);
  const targets = commits(h).map(([, [i]]) => i);
  assert.deepEqual(targets, [...targets].sort((a, b) => a - b), `${targets}`);
  assert.equal(targets.at(-1), 4);
});

// A burst holds its announce until the rail lands. A swipe that takes the
// burst over is a gesture again, and its commit announces at the commit.
test('a swipe that takes over a burst announces its own target at once', () => {
  const {h, root, c} = surface({count: 5});
  c.jumpTo(1);
  for (let i = 0; i < 6; i++) {
    h.tick(FRAME);
  }
  c.jumpTo(2);
  for (let i = 0; i < 6; i++) {
    h.tick(FRAME);
  }
  assert.deepEqual(commits(h), [], 'the burst holds');
  drag(h, root, {events: 4, deltaX: -14});
  assert.equal(c.isBusy(), true, 'still settling');
  assert.deepEqual(commits(h).at(-1), ['sidebarSpaceSelected', [3]],
      'the takeover commit announces before the rail lands');
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 3);
});

test('targetIndex names where the rail is headed, spaceIndex where it still is', () => {
  // What a SECOND press has to decide against. spaceIndex only moves on
  // arrival, so the Overview toggle and the drag-edge pager read this instead.
  const {h, c} = surface({count: 5});
  assert.equal(c.targetIndex(), 0, 'at rest the two agree');
  c.jumpTo(3);
  for (let i = 0; i < 6; i++) {
    h.tick(FRAME);
  }
  assert.equal(c.spaceIndex(), 0, 'not landed yet');
  assert.equal(c.targetIndex(), 3);
  runToRest(h, c);
  assert.equal(c.targetIndex(), 3);
  assert.equal(c.spaceIndex(), 3);
});

test('a dot pressed mid-glide moves the rail on from where it is', () => {
  // A whole pane in one frame is not a spring at all - it is the offset being
  // measured from a base that moved underneath it.
  const {h, rail, c} = surface({count: 5});
  c.jumpTo(4);
  for (let i = 0; i < 6; i++) {
    h.tick(FRAME);
  }
  const glidingAt = railPos(rail);
  assert.ok(glidingAt > 0.05, `the glide has started, rail at ${glidingAt}`);
  c.jumpTo(1);
  h.tick(FRAME);
  assert.ok(Math.abs(railPos(rail) - glidingAt) < 0.5,
      `the rail carried on from ${glidingAt}, not ${railPos(rail)}`);
  runToRest(h, c);
  assert.equal(railPos(rail), 1);
});

// The browser pushes a snapshot while its own glide runs, and the rail used
// to land at rest on the rebuild. A routed tab's row then waited ~0.9s.
test('a rebuild mid-glide keeps the glide going', () => {
  const {h, root, c} = surface({count: 5});
  c.jumpTo(3);
  for (let i = 0; i < 6; i++) {
    h.tick(FRAME);
  }
  assert.equal(c.isGliding(), true);
  const before = railPos(root.querySelector('.rail'));
  assert.ok(before > 0.05, `the glide has started, rail at ${before}`);
  root.replaceChildren(Object.assign(
      h.document.createElement('div'), {className: 'rail'}));
  c.attach(5, 3);
  const fresh = root.querySelector('.rail');
  assert.ok(Math.abs(railPos(fresh) - before) < 0.3,
      `the new rail carried on from ${before}, not ${railPos(fresh)}`);
  assert.equal(c.isBusy(), true, 'the rebuild ended the glide');
  runToRest(h, c);
  assert.equal(railPos(fresh), 3);
  assert.deepEqual(commits(h), [['sidebarSpaceSelected', [3]]]);
});

test('a rebuild mid-glide with a new pane count lands at rest', () => {
  const {h, rail, c} = surface({count: 4});
  c.jumpTo(2);
  for (let i = 0; i < 6; i++) {
    h.tick(FRAME);
  }
  c.attach(5, 2);
  assert.equal(railPos(rail), 2);
});

test('a gesture is not a glide', () => {
  const {h, root, c} = surface();
  drag(h, root, {events: 4, deltaX: -14});
  assert.equal(c.isBusy(), true);
  assert.equal(c.isGliding(), false);
});

test('a vertical wheel is left to the tab list', () => {
  const {h, root, rail} = surface();
  const e = wheel(root, {deltaY: 40});
  assert.equal(e.defaultPrevented, false, 'the list must still scroll');
  assert.equal(h.pendingFrames, 0, 'and no frame is burned on it');
  assert.equal(railX(rail), 0);
});

test('a live gesture keeps the wheel even when the finger drifts vertical', () => {
  // The cursor does not move during a swipe, so whatever is under it is not
  // what the fingers meant. Handing a diagonal event on to the list scrolls it
  // out from under a gesture that is still running.
  const {h, root} = surface();
  drag(h, root, {events: 3, deltaX: 14});
  const e = wheel(root, {deltaX: 2, deltaY: 40});
  assert.equal(e.defaultPrevented, true);
});

test('a momentum tail keeps the wheel too, drift and all', () => {
  // The gesture is over by here - it committed and is settling - so the state
  // test alone would hand this to the tab list. A tail is no more horizontal
  // than the flick that threw it, and it lands under the same motionless
  // cursor, so it is the same gesture's to swallow.
  const {h, root, c} = surface();
  drag(h, root, {events: 10, deltaX: -14});
  // The finger lift reads as silence, and the commit that arms tail
  // suppression lands a few frames into it.
  for (let i = 0; i < 7; i++) {
    h.tick(FRAME);
  }
  const tail = wheel(root, {deltaX: -9, deltaY: 20});
  assert.equal(tail.defaultPrevented, true, 'the tail is not the list to scroll');
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 1);
  assert.deepEqual(commits(h), [['sidebarSpaceSelected', [1]]]);
});

test('which axis leads decides a diagonal wheel, with slop for a real finger', () => {
  // No swipe is perfectly horizontal. Yielding on any vertical component at
  // all sends a genuine flick to the tab list instead; yielding on none of it
  // eats the list's scrolling. Both edges of the slop are pinned here, because
  // a bare "deltaY beats deltaX" test passes at any width of it.
  const {h, root} = surface();
  const lean = wheel(root, {deltaX: 40, deltaY: 40});
  assert.equal(lean.defaultPrevented, true, 'equal parts is still a swipe');
  assert.equal(h.pendingFrames, 1, 'and the loop is running for it');

  const list = surface();
  const down = wheel(list.root, {deltaX: 10, deltaY: 40});
  assert.equal(down.defaultPrevented, false, 'clearly vertical is the list');
  assert.equal(list.h.pendingFrames, 0);
});

test('a lone space swallows nothing', () => {
  const {h, root, c} = surface({count: 1});
  const e = wheel(root, {deltaX: 40});
  assert.equal(e.defaultPrevented, false);
  assert.equal(h.pendingFrames, 0);
  assert.equal(c.isBusy(), false);
});

test('the surface is inert before attach and after detach', () => {
  const {h, root, c} = surface({attach: false});
  const before = wheel(root, {deltaX: 40});
  assert.equal(before.defaultPrevented, false, 'there is no rail yet');
  c.attach(4, 0);
  c.detach();
  const after = wheel(root, {deltaX: 40});
  assert.equal(after.defaultPrevented, false);
  assert.equal(h.pendingFrames, 0);
  assert.equal(c.spaceCount(), 1);
});

test('detach lets go of the rail it was painting', () => {
  // The resize listener is installed once per route and never removed, so a
  // rail still held after the route tore its tree down is pinned for the rest
  // of the session, and painted into.
  const {h, rail, c} = surface();
  c.detach();
  const painted = recordPaints(rail);
  h.window.dispatchEvent(new Event('resize'));
  assert.equal(painted.length, 0);
});

// the lab yields to the dot bar unconditionally, but the lab's bar
// can hold many spaces. Here it usually fits, and yielding meant a swipe that
// started over the dots did nothing at all.
test('a dot bar that overflows keeps its own scrolling', () => {
  const {h, root} = surface();
  const dots = h.document.createElement('div');
  // .dotscroll: the scroller, not the outer .spacedots row.
  dots.className = 'dotscroll';
  // Declared, not measured - the shim has no layout.
  dots.scrollWidth = 400;
  dots.clientWidth = 100;
  root.appendChild(dots);
  const e = wheel(dots, {deltaX: 40});
  assert.equal(e.defaultPrevented, false);
});

test('a dot bar that fits does not eat the swipe', () => {
  const {h, root} = surface();
  const dots = h.document.createElement('div');
  dots.className = 'dotscroll';
  dots.scrollWidth = 100;
  dots.clientWidth = 100;
  root.appendChild(dots);
  const e = wheel(dots, {deltaX: 40});
  assert.equal(e.defaultPrevented, true, 'a swipe may start over the dots');
});

// Overview and + sit outside the scroller now, so .spacedots (the
// outer row) never overflows - only .dotscroll (nested inside it) does.
test('an overflowing bar yields even from a dot nested in the scroller', () => {
  const {h, root} = surface();
  const bar = h.document.createElement('div');
  bar.className = 'spacedots';
  const scroller = h.document.createElement('div');
  scroller.className = 'dotscroll';
  scroller.scrollWidth = 400;
  scroller.clientWidth = 100;
  const dot = h.document.createElement('div');
  dot.className = 'sdot';
  const glyph = h.document.createElement('span');
  glyph.className = 'd';
  dot.appendChild(glyph);
  scroller.appendChild(dot);
  bar.appendChild(scroller);
  root.appendChild(bar);
  const e = wheel(glyph, {deltaX: 40});
  assert.equal(e.defaultPrevented, false);
});

test('a press mid-gesture turns the rail around instead of parking it', () => {
  // Under both commit thresholds, so a resolved gesture rolls back and the
  // direction of travel is the tell. The silence timeout cannot be what ends
  // this one: only 16ms of quiet passes, against a 90ms gap.
  const {h, root, rail, c} = surface();
  drag(h, root, {events: 4, deltaX: -6});
  const before = railPos(rail);
  h.tick(FRAME);
  const chasing = railPos(rail);
  assert.ok(chasing > before, 'the rail is still chasing the finger');
  root.dispatchEvent(new Event('pointerdown', {bubbles: true}));
  h.tick(FRAME);
  assert.ok(railPos(rail) < chasing,
      `the press should resolve the gesture, rail at ${railPos(rail)}`);
  runToRest(h, c);
  assert.equal(railX(rail), 0);
  assert.equal(c.spaceIndex(), 0);
});

test('a press with nothing running leaves the loop asleep', () => {
  // Ending a gesture that is not there clears the tail suppression a settling
  // commit is relying on, and the frame it starts reports a settle that never
  // happened. The loop is deliberately off between gestures - this surface is
  // on screen all session, so an always-on rAF is a permanent wakeup.
  let idle = 0;
  const {h, root, c} = surface({onIdle: () => idle++});
  root.dispatchEvent(new Event('pointerdown', {bubbles: true}));
  assert.equal(h.pendingFrames, 0, 'an idle surface schedules no frame');
  assert.equal(c.isBusy(), false);
  h.tick(FRAME);
  assert.equal(idle, 0, 'and callers are not told a rail settled');
});

test('a resize mid-gesture keeps the rail on the offset it had', () => {
  // The offset is in pane units, so a width change re-derives the pixels from
  // it. Repainting at rest instead snaps a running flick back to the pane it
  // started from.
  const {h, root, rail} = surface();
  drag(h, root, {events: 6, deltaX: -14});
  const p = railPos(rail);
  assert.ok(p > 0.03, `mid-gesture the rail sits between panes, got ${p}`);
  root.clientWidth = 300;
  h.window.dispatchEvent(new Event('resize'));
  assert.ok(Math.abs(railPos(rail, 300) - p) < 0.001,
      `same offset at the new width, got ${railPos(rail, 300)} want ${p}`);
});

test('a resize repaints the rail at the new sidebar width', () => {
  // The native side owns the width and the user can drag it. Painting a stale
  // one leaves the rail offset by the difference on every space but the first.
  const {h, root, rail} = surface({count: 4, active: 2});
  assert.equal(railX(rail), -2 * W);
  root.clientWidth = 300;
  h.window.dispatchEvent(new Event('resize'));
  assert.equal(railX(rail), -600);
});

// Two swipes in a row both page a space.
//
// NOT a regression test for the swipe lock, and it must not be mistaken for one: it
// passes against the stranded-`swallowing` bug as well, because a synthetic
// drag has no MOMENTUM TAIL and the tail is what keeps the suppression alive
// past the settle. Reproducing that needs a captured trackpad stream (see
// REAL_FLICK in swipe-machine-test.mjs and `make wheel-capture`).
test('two swipes in a row each page a space', () => {
  const {h, root, c} = surface();

  drag(h, root, {events: 10, deltaX: -14});
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 1);

  for (let i = 0; i < 40; i++) {
    h.tick(FRAME);
  }

  drag(h, root, {events: 10, deltaX: -14});
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 2);
});

// a rightward scrub and Cmd+Shift+Right must move the same way.
//
// swipe-direction-bisect.mjs already pinned what "rightward" means at the
// wire: swipe-scroll-phase's dx=+90 cgevent scrub - a rightward
// physical gesture, confirmed by the switchToSpace VM instrument - arrives
// at this listener as deltaX = -90 (negative). That calibration is real-world
// fact, not an implementation choice, so it is reproduced RAW here rather
// than through the wheel() helper above, whose sign is this file's own
// abstraction for "a gesture", not a claim about physical direction.
//
// Cmd+Shift+Right is CycleSpace(1) - forward, toward the next space
// (atrium_native_hotkey_map.mm). Before the fix this scrub landed on
// index - 1 (backward); the fix makes it land on index + 1, matching the
// chord.
test('a rightward scrub goes BACK, the way Safari and Arc do', () => {
  // Decided by the user on real hardware 2026-08-17 after the
  // opposite choice shipped on 2026-08-10. Raw events, bypassing wheel(): the
  // sign the listener receives is the whole subject here.
  //
  // This deliberately DISAGREES with Cmd+Shift+Right, which still moves
  // forward. That is an open question - a gesture and a chord
  // pointing opposite ways is a defect either way round, and flipping a
  // standard arrow chord is the user's call, not this test's.
  const {h, root, c} = surface({count: 3, active: 1});
  for (let i = 0; i < 45; i++) {
    root.dispatchEvent(new Event('wheel', {bubbles: true, deltaX: -2, deltaY: 0}));
    h.tick(FRAME);
  }
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 0,
      'a rightward scrub (deltaX -90 total) must reach the PREVIOUS space');
});

// A rail of two panes on two profiles, plus the pinned grid drawn for the
// first. Panes render once per snapshot, so the only thing a gesture can move
// is which grid is visible.
function profileSurface() {
  const {h, root, rail, c} = surface({count: 2, active: 0});
  const pinned = h.document.createElement('div');
  pinned.className = 'favgrid';
  pinned.dataset.profileId = 'home';
  root.insertBefore(pinned, rail);
  // The extension strip is the root's SIBLING, not a child - the tab list's
  // rebuilds must never touch it.
  const strip = h.document.createElement('div');
  strip.className = 'exs-row';
  root.parentNode.insertBefore(strip, root);
  for (const id of ['home', 'work']) {
    const pane = h.document.createElement('div');
    pane.className = 'railpane';
    pane.dataset.profileId = id;
    rail.appendChild(pane);
  }
  return {h, root, rail, c, pinned, strip,
          panes: [...rail.querySelectorAll('.railpane')]};
}

// The reported blank: one root-wide class showed BOTH spanned grids, and the
// home pane holds empty slots, so the outgoing half went blank mid-slide.
test('mid-slide only the foreign pane is marked to show its own grid', () => {
  const {h, root, panes} = profileSurface();
  drag(h, root, {events: 6, deltaX: -12});
  assert.equal(root.classList.contains('xprofile'), true,
               'the span crosses a profile');
  assert.equal(panes[0].classList.contains('otherprofile'), false,
               'the home pane must keep the pinned grid over it');
  assert.equal(panes[1].classList.contains('otherprofile'), true,
               'the work pane shows its own tiles');
});

// The pinned copy is what the same-profile pane shows in place of its slots,
// so hiding it mid-slide is what produced the blank.
test('the pinned grid stays up while the pane it was drawn for is spanned',
     () => {
       const {h, root} = profileSurface();
       drag(h, root, {events: 6, deltaX: -12});
       assert.equal(root.classList.contains('pinnedoff'), false);
     });

// Once the rail rests on the other profile the copy is stale, and holding it
// up flashes the outgoing profile's tiles over the incoming pane.
test('the pinned grid goes once no pane on screen is its own', () => {
  const {h, root, c} = profileSurface();
  drag(h, root, {events: 20, deltaX: -12});
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 1, 'the swipe committed to the work pane');
  assert.equal(root.classList.contains('pinnedoff'), true);
});

// Drawn for pane 0, the rail origin, so a copy that travels with its own
// pane must read the same transform the rail itself just got.
test('the pinned grid travels with the pane it was drawn for, mid-slide', () => {
  const {h, root, rail} = profileSurface();
  drag(h, root, {events: 6, deltaX: -12});
  const pinned = [...root.children].find((el) => el.classList.contains('favgrid'));
  assert.equal(pinned.style.transform, rail.style.transform,
               'pane 0 is the rail origin, so the two transforms coincide');
});

// Two spaces of 'home' precede the 'work' pane the swipe is leaving from.
// Tracking the FIRST same-profile pane in the rail (index 0) rather than the
// one actually spanned (index 1) flew the copy off past the real pane.
test('the pinned grid tracks the spanned pane, not the first same-profile one',
     () => {
       const {h, root, rail, c} = surface({count: 3, active: 1});
       const pinned = h.document.createElement('div');
       pinned.className = 'favgrid';
       pinned.dataset.profileId = 'home';
       root.insertBefore(pinned, rail);
       for (const id of ['home', 'home', 'work']) {
         const pane = h.document.createElement('div');
         pane.className = 'railpane';
         pane.dataset.profileId = id;
         rail.appendChild(pane);
       }
       drag(h, root, {events: 6, deltaX: -12});
       assert.equal(root.classList.contains('xprofile'), true);
       const expected = `translateX(${(railX(rail) + W).toFixed(2)}px)`;
       assert.equal(pinned.style.transform, expected,
                    'drawn for pane 1, one pane-width ahead of the rail origin');
     });

// The strip and the grid are one block over the pane they were drawn for. Two
// mechanisms would let them separate mid-swipe.
test('the pinned strip rides with the pinned grid across a profile', () => {
  const {h, root, rail, pinned, strip} = profileSurface();
  drag(h, root, {events: 6, deltaX: -12});
  assert.equal(root.classList.contains('xprofile'), true);
  assert.equal(strip.style.transform, pinned.style.transform);
  assert.equal(strip.style.transform, rail.style.transform,
               'pane 0 is the rail origin, so the two transforms coincide');
});

// Inside one profile the pins are identical pane to pane, so the strip has to
// stand still while the rail moves under it.
test('the pinned strip stays put inside one profile', () => {
  const {h, root, rail} = surface({count: 2, active: 0});
  const pinned = h.document.createElement('div');
  pinned.className = 'favgrid';
  pinned.dataset.profileId = 'home';
  root.insertBefore(pinned, rail);
  const strip = h.document.createElement('div');
  strip.className = 'exs-row';
  root.parentNode.insertBefore(strip, root);
  for (let i = 0; i < 2; i++) {
    const pane = h.document.createElement('div');
    pane.className = 'railpane';
    pane.dataset.profileId = 'home';
    rail.appendChild(pane);
  }
  drag(h, root, {events: 6, deltaX: -12});
  assert.equal(root.classList.contains('xprofile'), false);
  assert.equal(strip.style.transform, '');
  assert.equal(strip.classList.contains('pinnedoff'), false);
});

// The class rides on the strip itself: it precedes the root in the document,
// and CSS has no way back to a preceding sibling.
test('the pinned strip goes once no pane on screen is its own', () => {
  const {h, root, c, strip} = profileSurface();
  drag(h, root, {events: 6, deltaX: -12});
  assert.equal(strip.classList.contains('pinnedoff'), false,
               'the home pane is still spanned, so the strip is what it shows');
  drag(h, root, {events: 14, deltaX: -12});
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 1);
  assert.equal(strip.classList.contains('pinnedoff'), true);
});

// The strip is never rebuilt, so a class it takes mid-glide (exs-hidden, the
// devbar's top) plays its .2s transition as a drop from above while the rail
// slides sideways. The lock turns those transitions off for the glide.
test('the pinned strip is locked while the rail glides and freed at rest', () => {
  const {h, c, strip} = profileSurface();
  c.jumpTo(1);
  h.tick(FRAME);
  h.tick(FRAME);
  assert.equal(strip.classList.contains('swipelock'), true,
               'the strip must drop its transitions for the glide');
  runToRest(h, c);
  assert.equal(strip.classList.contains('swipelock'), false,
               'a resting strip gets its own fades back');
});

// Outside a cross-profile span the content never changes pane to pane, so
// the copy must hold its fixed spot rather than tracking a pane index.
test('the pinned grid holds its fixed position outside a cross-profile span',
     () => {
       const {h, root, rail} = surface({count: 2, active: 0});
       const pinned = h.document.createElement('div');
       pinned.className = 'favgrid';
       pinned.dataset.profileId = 'home';
       root.insertBefore(pinned, rail);
       for (let i = 0; i < 2; i++) {
         const pane = h.document.createElement('div');
         pane.className = 'railpane';
         pane.dataset.profileId = 'home';
         rail.appendChild(pane);
       }
       drag(h, root, {events: 6, deltaX: -12});
       assert.equal(root.classList.contains('xprofile'), false);
       assert.equal(pinned.style.transform, '');
     });

// A fast switch starts a settle whose target is already spaceIndex while the
// rail is still up to a pane away. Clamping to [spaceIndex, target] then glued
// the grid to the incoming pane of the SAME profile, and it slid in with it.
function sameProfileSurface() {
  const {h, root, rail, c} =
      surface({count: 3, active: 1, leadingPanes: 1, attach: false});
  const pinned = h.document.createElement('div');
  pinned.className = 'favgrid';
  pinned.dataset.profileId = 'home';
  root.insertBefore(pinned, rail);
  for (const id of ['', 'home', 'home']) {
    const pane = h.document.createElement('div');
    pane.className = 'railpane';
    pane.dataset.profileId = id;
    rail.appendChild(pane);
  }
  c.attach(3, 1);
  const moved = [];
  const step = () => {
    h.tick(16);
    // Under a pixel is the rail-end bounce, which the grid follows on purpose.
    // The defect slid it 100px and more.
    const t = pinned.style.transform;
    const px = Math.abs(parseFloat(/translateX\(([-\d.]+)px\)/.exec(t)?.[1]));
    if (t && !(px < 1)) {
      moved.push(t);
    }
  };
  return {h, root, c, moved, step};
}

test('a hotkey jump reversed mid-glide keeps a same-profile grid still', () => {
  const {c, moved, step} = sameProfileSurface();
  c.jumpTo(2);
  for (let i = 0; i < 8; i++) {
    step();
  }
  c.jumpTo(1);
  for (let i = 0; i < 400 && c.isBusy(); i++) {
    step();
  }
  assert.deepEqual(moved, []);
});

test('a flick taken over by a second swipe keeps a same-profile grid still',
     () => {
       const {root, c, moved, step} = sameProfileSurface();
       const send = (dx, momentum) => root.dispatchEvent(new Event(
           'wheel', {bubbles: true, deltaX: -dx, deltaY: 0, momentum}));
       for (let i = 0; i < 3; i++) {
         send(-40, false);
         step();
       }
       for (let i = 0; i < 2; i++) {
         send(-3, true);
         step();
       }
       send(3, false);
       for (let i = 0; i < 400 && c.isBusy(); i++) {
         step();
       }
       assert.deepEqual(moved, []);
     });

// the backdrop scrub. The window blends the incoming space's theme
// over the outgoing one at a weight the swipe reports every frame, the way
// the lab's render() recomputes blendWeight(p) from the live position. These
// pin the message protocol the browser side answers: one stage per neighbour,
// a weight stream that tracks the rail, and one end that says commit or not.
const scrubs = (h) => h.sent.filter(([name]) => name.startsWith('sidebarSpaceScrub'));
const scrubNames = (h) => scrubs(h).map(([name]) => name);
const blends = (h) => h.sent.filter(([name]) => name === 'sidebarSpaceScrubBlend')
                          .map(([, args]) => args[0]);

test('a slow drag stages the neighbour once and streams a rising blend weight', () => {
  const {h, root} = surface();
  drag(h, root, {events: 6, deltaX: -8});
  const staged = scrubs(h).filter(([name]) => name === 'sidebarSpaceScrubBegan');
  assert.deepEqual(staged, [['sidebarSpaceScrubBegan', [1]]],
                   'one stage for the one neighbour the rail moved toward');
  const w = blends(h);
  assert.ok(w.length >= 3, `expected a weight per frame, got ${w.length}`);
  for (let i = 1; i < w.length; i++) {
    assert.ok(w[i] > w[i - 1], `weight must rise with the rail: ${w}`);
  }
  assert.ok(w[w.length - 1] > 0 && w[w.length - 1] < 1);
  assert.ok(!scrubNames(h).includes('sidebarSpaceScrubEnded'),
            'the gesture is still live');
});

test('the blend weight follows the rail position, full at the lab distance', () => {
  // The lab completes the blend at blendFactor x commitThreshold = 0.9 of the
  // width, so a rail 0.45 of the way across sits at weight 0.5.
  const {h, root, rail} = surface();
  drag(h, root, {events: 8, deltaX: -8});
  const at = railPos(rail);
  const last = blends(h).at(-1);
  assert.ok(Math.abs(last - Math.min(1, at / 0.9)) < 0.02,
            `weight ${last} does not match the rail at ${at}`);
});

test('a rolled-back swipe cancels the scrub after the weight returns to zero', () => {
  const {h, root, c} = surface();
  drag(h, root, {events: 4, deltaX: -6});
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 0);
  assert.deepEqual(commits(h), []);
  assert.equal(scrubs(h).at(-1)[0], 'sidebarSpaceScrubEnded');
  assert.deepEqual(scrubs(h).at(-1)[1], [false]);
  assert.ok(blends(h).at(-1) < 0.01, `the weight must land at zero: ${blends(h).at(-1)}`);
});

test('a committed swipe ends the scrub with a commit once the weight reaches one', () => {
  const {h, root, c} = surface();
  drag(h, root, {events: 10, deltaX: -14});
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 1);
  assert.deepEqual(commits(h), [['sidebarSpaceSelected', [1]]]);
  assert.deepEqual(scrubs(h).at(-1), ['sidebarSpaceScrubEnded', [true]]);
  assert.equal(blends(h).at(-1), 1);
  const names = h.sent.map(([name]) => name);
  assert.ok(names.indexOf('sidebarSpaceSelected') < names.lastIndexOf('sidebarSpaceScrubEnded'),
            'the switch lands while the scrub still owns the backdrop');
  const ends = scrubNames(h).filter((n) => n === 'sidebarSpaceScrubEnded');
  assert.equal(ends.length, 1, 'one end per gesture');
});

test('a drag that crosses back over its origin restages toward the other neighbour', () => {
  const {h, root} = surface({count: 4, active: 1});
  drag(h, root, {events: 4, deltaX: -6});
  drag(h, root, {events: 10, deltaX: 6});
  const names = scrubNames(h).filter((n) => n !== 'sidebarSpaceScrubBlend');
  assert.deepEqual(names, ['sidebarSpaceScrubBegan', 'sidebarSpaceScrubEnded',
                           'sidebarSpaceScrubBegan']);
  const stages = scrubs(h).filter(([name]) => name === 'sidebarSpaceScrubBegan');
  assert.deepEqual(stages.map(([, a]) => a), [[2], [0]]);
  assert.deepEqual(scrubs(h).find(([name]) => name === 'sidebarSpaceScrubEnded')[1],
                   [false], 'the abandoned side is dropped, not promoted');
});

test('jumpTo drives the settle without a scrub', () => {
  // The browser already cross-fades the backdrop for a keyboard or command
  // bar switch, so a scrub here would replay the same fade twice.
  const {h, c} = surface({count: 4});
  c.jumpTo(2);
  runToRest(h, c);
  assert.deepEqual(scrubs(h), []);
});

test('a drag toward the Overview stages nothing', () => {
  // The Overview is a pane, not a space: it has no theme to blend toward.
  const {h, root, c} = surface({count: 4, active: 1, leadingPanes: 1});
  drag(h, root, {events: 6, deltaX: 8});
  assert.deepEqual(scrubs(h).filter(([name]) => name !== 'sidebarSpaceScrubBlend'), []);
  runToRest(h, c);
  assert.deepEqual(scrubs(h), []);
});

test('the scrub names space indices, not rail positions', () => {
  const {h, root} = surface({count: 4, active: 1, leadingPanes: 1});
  drag(h, root, {events: 6, deltaX: -8});
  assert.deepEqual(scrubs(h).find(([name]) => name === 'sidebarSpaceScrubBegan'),
                   ['sidebarSpaceScrubBegan', [1]], 'rail 2 is space 1');
});

test('a finger wobbling on the origin does not restage the neighbour every frame', () => {
  // Each stage rasters a theme in the browser. Sub-pixel sign flips around
  // the origin must not reach it.
  const {h, root} = surface({count: 4, active: 1});
  drag(h, root, {events: 1, deltaX: -1});
  drag(h, root, {events: 1, deltaX: 2});
  drag(h, root, {events: 1, deltaX: -2});
  const stages = scrubs(h).filter(([name]) => name === 'sidebarSpaceScrubBegan');
  assert.ok(stages.length <= 1, `staged ${stages.length} times for a 1-2px wobble`);
});

test('the dead zone keeps a staged side while the rail hovers at the origin', () => {
  // Reaching the zone from the outside must not drop and restage the same
  // side - the drop is a cancel the browser answers with a repaint.
  const {h, root} = surface({count: 4, active: 1});
  drag(h, root, {events: 6, deltaX: -8});
  drag(h, root, {events: 6, deltaX: 8});
  const names = scrubNames(h).filter((n) => n !== 'sidebarSpaceScrubBlend');
  assert.deepEqual(names.slice(0, 1), ['sidebarSpaceScrubBegan']);
  const ends = names.filter((n) => n === 'sidebarSpaceScrubEnded').length;
  assert.ok(ends <= 1, `ended ${ends} times`);
});

const scrubEvents = (h) => scrubs(h).filter(([name]) => name !== 'sidebarSpaceScrubBlend')
                                    .map(([name, args]) => [name, ...args]);

test('a second flick during the settle promotes the first side and stages the next', () => {
  // The rail passes space 1 on its way to space 2 without a pause. The
  // browser must hear the first blend land before the next one is staged, or
  // it promotes a layer the rail has already left behind.
  const {h, root, c} = surface({count: 4});
  // The commit lands on the third event, and the settle is short, so the
  // gap below has to come while it still runs.
  drag(h, root, {events: 4, deltaX: -14});
  for (let i = 0; i < 4; i++) {
    h.tick(FRAME);
  }
  assert.equal(c.targetIndex(), 1, 'the first flick committed and is settling');
  assert.equal(c.isBusy(), true, 'still settling');
  // Past the tail-quiet gap in one jump, so the machine reads the next
  // stream as a new gesture, while the spring (dt capped) is still mid-settle.
  h.setNow(18 * FRAME + 300);
  h.tick(0);
  assert.equal(c.isBusy(), true, 'still settling after the gap');
  drag(h, root, {events: 10, deltaX: -14});
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 2);
  assert.deepEqual(scrubEvents(h), [
    ['sidebarSpaceScrubBegan', 1], ['sidebarSpaceScrubEnded', true],
    ['sidebarSpaceScrubBegan', 2], ['sidebarSpaceScrubEnded', true],
  ]);
});

test('a jump during a live drag drops the scrub before the jump announces', () => {
  const {h, root, c} = surface({count: 5});
  drag(h, root, {events: 4, deltaX: -6});
  assert.deepEqual(scrubEvents(h), [['sidebarSpaceScrubBegan', 1]]);
  c.jumpTo(3);
  const blendsBefore = blends(h).length;
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 3);
  const names = h.sent.map(([name]) => name);
  assert.ok(names.indexOf('sidebarSpaceScrubEnded') < names.indexOf('sidebarSpaceSelected'),
            'the drop lands before the jump is announced');
  assert.deepEqual(scrubEvents(h).at(-1), ['sidebarSpaceScrubEnded', false]);
  assert.equal(blends(h).length, blendsBefore, 'the jump streams no weights');
});

test('a rail rebuild mid-drag closes the scrub and the next drag starts clean', () => {
  const {h, root, c} = surface({count: 4});
  drag(h, root, {events: 4, deltaX: -6});
  c.detach();
  assert.deepEqual(scrubEvents(h).at(-1), ['sidebarSpaceScrubEnded', false]);
  runToRest(h, c);
  const rail = h.document.createElement('div');
  rail.className = 'rail';
  root.replaceChildren(rail);
  c.attach(5, 2);
  h.sent.length = 0;
  drag(h, root, {events: 4, deltaX: -6});
  assert.deepEqual(scrubEvents(h), [['sidebarSpaceScrubBegan', 3]],
                   'measured from the rebuilt rail, not the stale origin');
  assert.ok(blends(h).every((w) => w < 0.5), `stale origin snapped the weight: ${blends(h)}`);
});

test('a swipe back during the momentum tail blends between the same two spaces', () => {
  // The scrub of a committed swipe stays open until the tail goes quiet. A
  // swipe back before that measured from the stale origin, so the bounce past
  // the landed space staged its far neighbour and the backdrop dipped.
  const {h, root, c} = surface({count: 7, active: 2});
  const flick = (dir) => {
    const send = (deltaX, momentum) => {
      root.dispatchEvent(new Event('wheel', {bubbles: true, deltaX, deltaY: 0, momentum}));
      h.tick(FRAME);
    };
    for (let i = 0; i < 8; i++) {
      send(18 * dir, false);
    }
    for (let i = 0, m = 16 * dir; i < 50; i++, m *= 0.93) {
      send(m, true);
    }
  };
  flick(1);
  flick(-1);
  runToRest(h, c);
  assert.equal(c.spaceIndex(), 2);
  assert.deepEqual(scrubEvents(h), [
    ['sidebarSpaceScrubBegan', 3], ['sidebarSpaceScrubEnded', true],
    ['sidebarSpaceScrubBegan', 2], ['sidebarSpaceScrubEnded', true],
  ]);
});
