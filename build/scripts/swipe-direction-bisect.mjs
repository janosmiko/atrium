#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// which way does a scrub commit, in every version of swipe.js?
//
// Answers "when did the direction change?" without building Chromium. Each
// version is judged in about a second, against twelve minutes for a build,
// which is what made a sixteen-commit sweep affordable at all.
//
//   build/scripts/swipe-direction-bisect.mjs
//   build/scripts/swipe-direction-bisect.mjs <path-to-a-swipe.js>
//
// With no argument it walks every commit that ever touched swipe.js, oldest
// first, and prints the landing for each. With a path it judges that one file.
//
// The gesture is swipe-scroll-phase's first scrub, reproduced from the
// numbers the scenario actually produced in the VM: starting on 'Elsewhere' of
// three spaces, dx +90 spread over 45 steps at 20ms. The wheel listener has
// already flipped the sign by the time the machine sees it, so +90 here is a
// rightward physical scrub.
//
// Versions before feat(404) do not export createSwipeMachine and are reported
// as undrivable rather than skipped silently.
//
// The wheel listener itself was fixed (it no longer negates deltaX), so
// this script's fixed TOTAL=+90 calibration - "already flipped by the
// listener" - describes every commit up to and including that bisect, not
// the listener as it stands after the fix. Driven at HEAD it will still
// report BACKWARD: that is the machine's own internal convention, which the
// fix deliberately left untouched, and is expected, not a regression. This
// script is the historical record of the bug, not a live direction gate.

import {execFileSync} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const REL = 'chrome/browser/resources/atrium/swipe.js';

const W = 220;      // the sidebar width the probe runs at
const TOTAL = 90;   // cgevent dx, as it reaches the machine
const STEPS = 45;   // 900ms of 20ms steps
const GAP = 0.02;
const START = 1;    // 'Elsewhere', the scenario's startup space

const git = (...args) =>
    execFileSync('git', ['-C', SRC, ...args], {encoding: 'utf8', maxBuffer: 1 << 26});

// Returns a line describing where this swipe.js lands, or why it cannot say.
async function judge(file) {
  let mod;
  try {
    mod = await import(`file://${file}`);
  } catch (e) {
    return `unloadable (${e.message.split('\n')[0]})`;
  }
  if (typeof mod.createSwipeMachine !== 'function') {
    return 'no exported machine (predates feat(404))';
  }
  const m = mod.createSwipeMachine({spaceCount: 3, width: () => W});
  m.spaceIndex = START;

  let t = 0;
  for (let i = 0; i < STEPS; i++) {
    t += GAP;
    m.delta(TOTAL / STEPS, 0, t);
  }
  const p = m.progress();
  m.end();
  // Settle the animation. The cap is a guard against a machine that never
  // returns to idle, which would otherwise hang the sweep.
  for (let i = 0; i < 600 && m.state !== mod.S.idle; i++) {
    t += 1 / 60;
    m.step(1 / 60, t);
  }
  const moved = m.spaceIndex - START;
  const dir = moved > 0 ? 'FORWARD' : moved < 0 ? 'BACKWARD' : 'no commit';
  return `p ${p.toFixed(3)}  landed ${m.spaceIndex}  ${dir}`;
}

const arg = process.argv[2];
if (arg) {
  console.log(await judge(path.resolve(arg)));
  process.exit(0);
}

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'swipe-bisect-'));
const hashes = git('log', '--reverse', '--format=%H', '--', REL).trim().split('\n');

for (const h of hashes) {
  const file = path.join(tmp, `${h}.mjs`);
  fs.writeFileSync(file, git('show', `${h}:${REL}`));
  const subject = git('log', '-1', '--format=%s', h).trim().slice(0, 44);
  console.log(`${h.slice(0, 8)} ${subject.padEnd(44)} ${await judge(file)}`);
}
fs.rmSync(tmp, {recursive: true, force: true});
