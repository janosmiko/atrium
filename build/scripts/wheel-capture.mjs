#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Records the raw wheel stream a real trackpad produces, and what the swipe
// machine does with it.
//
//   make run FLAGS='--remote-debugging-port=9333'
//   make wheel-capture      # swipe until one FAILS, then Ctrl-C
//
// Runs until interrupted. The bug it exists for now is intermittent - it can
// take ten or twenty seconds of swiping to provoke - and the old fixed 6s
// window meant racing a stopwatch and losing the reproduction.
// CAPTURE_SECONDS still forces a fixed window if you want one.
//
// It also FINDS the failure rather than making you read the whole stream: a
// gesture whose horizontal events the machine CONSUMED while the rail never
// moved is a dead swipe, and only those are printed in full.
//
// This exists because synthetic wheel events do not reproduce these bugs. macOS
// sends a MOMENTUM TAIL after the fingers lift - dozens of decaying events over
// about a second - and CDP's dispatchMouseEvent has no tail at all, so neither
// a VM probe nor a unit test can produce one. Inventing its shape is what made
// an earlier round of probes useless.
//
// Read-only: it adds listeners and reads state. It never dispatches input.
import {visibleSidebar, wait} from './sidebar-probe-lib.mjs';

const SECONDS = Number(process.env.CAPTURE_SECONDS || 0);  // 0 = until Ctrl-C
const PORT = process.env.ATRIUM_CDP_PORT || '9333';

// Offline re-analysis of a stream already on disk. Every recording is saved as
// it is made, so a summary that asks the wrong question - which has happened
// twice now - costs a re-read rather than another session of deliberate
// swiping at the trackpad.
const analyseArg = process.argv.indexOf('--analyse');
const OFFLINE = analyseArg > -1 ? process.argv[analyseArg + 1] : null;
if (analyseArg > -1 && !OFFLINE) {
  console.error('--analyse needs a path, e.g. build/logs/wheel-capture-1234.json');
  process.exit(2);
}

// The crash this replaces: Ctrl-C landed while the script was awaiting the
// final CDP read, node's event loop had nothing else referenced, and it exited
// with "unsettled top-level await" - throwing away the entire recording. The
// WebSocket does not hold the loop open on its own, so hold it open here and
// release it only after the last drain has been written.
const keepAlive = setInterval(() => {}, 1 << 30);


// A bare stack trace here reads as "the tool is broken" when the real answer is
// almost always that Atrium was launched without the debugging port.
let sidebar = null;
try {
  if (!OFFLINE) { sidebar = await visibleSidebar(); }
} catch (err) {
  console.error(`Could not reach a sidebar surface on :${PORT}.`);
  console.error('');
  console.error('  Atrium must be running WITH the debugging port:');
  console.error("    make run FLAGS='--remote-debugging-port=9333'");
  console.error('');
  console.error(`  (${err.message})`);
  process.exit(2);
}
const ev = sidebar ? sidebar.ev : async () => null;
const ws = sidebar ? sidebar.ws : {close() {}};

// One sampler in the page: every wheel event, tagged with the machine state at
// the moment it was handled and the rail offset just after. Sampling here
// rather than polling means nothing is missed between frames.
const installed = OFFLINE ? 'offline' : await ev(`(() => {
  if (window.__wcInstalled) { return 'already'; }
  window.__wcInstalled = true;
  window.__wt0 = performance.now();
  window.__wheel = [];
  window.__marks = [];
  const railX = () => {
    const rail = document.querySelector('.rail');
    const m = /translateX\\((-?[\\d.]+)px\\)/.exec(rail && rail.style.transform || '');
    return m ? Number(m[1]) : 0;
  };
  // Bubble phase, so it runs AFTER the surface's own handler: defaultPrevented
  // then says whether the machine took the event or left it to the tab list.
  document.addEventListener('wheel', (e) => {
    const d = window.__swipeDebug ? window.__swipeDebug() : null;
    window.__wheel.push({
      t: Math.round(performance.now() - window.__wt0),
      dx: Math.round(e.deltaX * 10) / 10,
      dy: Math.round(e.deltaY * 10) / 10,
      took: e.defaultPrevented,
      rail: Math.round(railX()),
      st: d ? d.state : '?',
      sw: d ? d.swallowing : null,
      tail: d && d.tail ? d.tail : null,
      // WheelEvent.momentum - macOS's own fingers-on/coasting answer, which
      // retires every heuristic beside it. Recorded so a captured
      // stream can be pinned as a fixture that carries it.
      mom: typeof e.momentum === 'boolean' ? e.momentum : null,
    });
  }, false);
  const real = chrome.send.bind(chrome);
  chrome.send = (n, a) => {
    if (n === 'sidebarSpaceSelected') {
      window.__marks.push([Math.round(performance.now() - window.__wt0),
                           'commit ' + JSON.stringify(a)]);
    }
    return real(n, a);
  };
  return 'ok';
})()`);

if (!OFFLINE && !await ev('typeof window.__swipeDebug === "function"')) {
  console.log('NOTE: this build has no window.__swipeDebug - rebuild to get the');
  console.log('      machine state beside each event. Continuing without it.\n');
}
if (installed === 'already') {
  console.log('(reusing the sampler already installed on this surface)\n');
}

if (!OFFLINE) {
  console.log(SECONDS
      ? `Recording ${SECONDS}s.`
      : 'Recording until Ctrl-C. Swipe until one FAILS, then stop.');
  console.log('Keep the pointer over the sidebar throughout.\n');
}

// Drain to disk AS IT RECORDS, once a second, rather than once at the end.
//
// Ctrl-C used to kill the process while it was awaiting the final read, which
// threw away the whole recording - "unsettled top-level await" - and asking
// for a third attempt at a gesture that takes real effort to provoke is not
// acceptable. The file now exists from the first second, so no shutdown path,
// clean or otherwise, can lose more than the last tick.
const {writeFileSync, mkdirSync, readFileSync} = await import('node:fs');
mkdirSync('build/logs', {recursive: true});
const OUT = `build/logs/wheel-capture-${Date.now()}.json`;

let wheel = [];
let marks = [];
if (OFFLINE) {
  clearInterval(keepAlive);
  const saved = JSON.parse(readFileSync(OFFLINE, 'utf8'));
  wheel = saved.wheel || [];
  marks = saved.marks || [];
  console.log(`analysing ${OFFLINE}: ${wheel.length} events, ${marks.length} commit(s)\n`);
}
const drain = async () => {
  try {
    wheel = await ev('window.__wheel') || wheel;
    marks = await ev('window.__marks') || marks;
    if (wheel.length) {
      writeFileSync(OUT, JSON.stringify({wheel, marks}, null, 1));
    }
  } catch {
    // The surface went away (relaunch, crash). Whatever reached disk stands.
  }
};

let stop = false;
process.on('SIGINT', () => {
  if (stop) { process.exit(130); }   // a second Ctrl-C means business
  stop = true;
  console.log('\nstopping, saving...');
});

if (!OFFLINE) {
  const started = Date.now();
  while (!stop && (!SECONDS || Date.now() - started < SECONDS * 1000)) {
    await wait(1000);
    await drain();
  }
  await drain();
  clearInterval(keepAlive);
  if (wheel.length) {
    console.log(`raw stream saved to ${OUT}`);
    console.log(`re-analyse it with:  node build/scripts/wheel-capture.mjs --analyse ${OUT}\n`);
  }
}

if (!wheel || !wheel.length) {
  console.log('\nNo wheel events reached the sidebar. Was the pointer over it?');
  ws.close();
  process.exit(2);
}

// A dead swipe does NOT segment cleanly out of continuous trackpad input: a
// first attempt split 832 events over 24 seconds into two "gestures", one of
// them 731 events long, and any dead stretch inside it was invisible against
// the blob's own total movement.
//
// So scan for the stretch itself: consecutive events the machine TOOK
// (defaultPrevented, so the delta reached it) that are horizontal, over which
// the rail does not move. That is the failure regardless of where gestures
// begin and end.
const RAIL_EPS = 2;        // px; a rail that moves less than this has not moved
const MIN_EVENTS = 6;      // shorter runs happen at a rail end, legitimately
const MIN_MS = 200;

const horizontal = (e) => Math.abs(e.dx) > Math.abs(e.dy) * 1.2;
// A stretch of MOMENTUM being swallowed is the suppression doing its job, not a
// failure, however long it runs. Only FINGER events being dropped are a dead
// swipe. Before the machine told the two apart this detector could not
// tell them apart, and reported 16 "dead swipes" in a session where swiping
// worked and committed 20 times.
const MIN_FINGER = 4;
const fingerCount = (events) => events.filter((e) => e.mom === false).length;

const stretches = [];
let run = null;
for (const e of wheel) {
  const eligible = e.took && horizontal(e);
  if (eligible && run && Math.abs(e.rail - run.rail0) <= RAIL_EPS) {
    run.events.push(e);
    continue;
  }
  if (run && run.events.length >= MIN_EVENTS &&
      run.events[run.events.length - 1].t - run.events[0].t >= MIN_MS &&
      fingerCount(run.events) >= MIN_FINGER) {
    stretches.push(run);
  }
  run = eligible ? {rail0: e.rail, events: [e]} : null;
}
if (run && run.events.length >= MIN_EVENTS &&
    run.events[run.events.length - 1].t - run.events[0].t >= MIN_MS &&
    fingerCount(run.events) >= MIN_FINGER) {
  stretches.push(run);
}

const total = wheel[wheel.length - 1].t / 1000;
const tookN = wheel.filter((e) => e.took).length;
console.log(`${wheel.length} wheel events over ${total.toFixed(1)}s; ` +
            `${tookN} taken by the machine; ${marks.length} commit(s)`);
console.log(`swallowing seen on ${wheel.filter((e) => e.sw === true).length} event(s)\n`);

if (!stretches.length) {
  console.log('No dead stretch found: every run of horizontal events the machine');
  console.log('took also moved the rail. If a swipe visibly died in this session,');
  console.log(`re-analyse the saved stream - the detector, not the app, is wrong.`);
  ws.close();
  process.exit(0);
}

console.log(`${stretches.length} DEAD STRETCH(ES) - ` +
            `horizontal events taken, rail did not move:\n`);
for (const st of stretches) {
  const g = st.events;
  const span = g[g.length - 1].t - g[0].t;
  console.log(`=== ${g.length} events over ${span}ms at t=${g[0].t}ms, ` +
              `rail pinned at ${st.rail0}px ===`);
  // What came immediately before decides between a stranded flag and a rail
  // end: a commit just before means the previous gesture set the suppression.
  const at = wheel.indexOf(g[0]);
  const before = wheel.slice(Math.max(0, at - 4), at);
  if (before.length) {
    console.log('    preceding events:');
    for (const e of before) {
      console.log(`   ${String(e.t).padStart(8)} ${String(e.dx).padStart(6)} ` +
                  `${String(e.dy).padStart(6)} took=${e.took} rail=${e.rail} ` +
                  `${e.st} sw=${e.sw}`);
    }
  }
  const lastCommit = marks.filter(([t]) => t < g[0].t).pop();
  if (lastCommit) {
    console.log(`    last commit ${g[0].t - lastCommit[0]}ms earlier: ${lastCommit[1]}`);
  }
  const swallowed = g.filter((e) => e.sw === true && e.tail);
  if (swallowed.length) {
    console.log(`    SWALLOWED BY THE MOMENTUM TAIL on ${swallowed.length} event(s).`);
    console.log('    breaksThroughTail needs age>=0.18 AND sawGap, then jump or ramp:');
    console.log('      jump = |dx| >= max(6, 3*ema)      ramp = accel>=2 and |dx| >= max(6, 1.5*ema)');
    for (const e of swallowed.slice(0, 12)) {
      const t = e.tail;
      const jump = Math.abs(e.dx) >= Math.max(6, 3 * t.ema);
      const ramp = t.accel >= 2 && Math.abs(e.dx) >= Math.max(6, 1.5 * t.ema);
      const past = t.age >= 0.18 && t.sawGap;
      const why = !past
          ? (t.age < 0.18 ? 'age<0.18' : 'no gap seen yet')
          : (jump || ramp ? 'SHOULD HAVE BROKEN THROUGH' : 'neither jump nor ramp');
      console.log(
          `   ${String(e.t).padStart(7)} dx=${String(e.dx).padStart(6)} ` +
          `ema=${String(t.ema).padStart(6)} accel=${t.accel} sawGap=${t.sawGap} ` +
          `age=${t.age}  -> ${why}`);
    }
    console.log();
  }
  console.log('   t(ms)     dx     dy   taken   rail  state          swallowing');
  for (const e of g.slice(0, 40)) {
    console.log(
        `${String(e.t).padStart(8)} ${String(e.dx).padStart(6)} ` +
        `${String(e.dy).padStart(6)} ${String(e.took).padStart(7)} ` +
        `${String(e.rail).padStart(6)}  ${String(e.st).padEnd(15)}${e.sw}`);
  }
  if (g.length > 40) { console.log(`   ... ${g.length - 40} more`); }
  console.log();
}

console.log('\nHow to read it:');
console.log('  taken=true   the surface called preventDefault, so the machine got the delta');
console.log('  state        stuck at idle/armed while taken=true means the delta was dropped');
console.log('  swallowing   true here is the momentum-tail suppression eating the gesture');
ws.close();
process.exit(0);
