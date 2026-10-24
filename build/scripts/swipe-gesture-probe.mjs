#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// asserts the swipe against a RUNNING Atrium, not a mock.
//
// build/scripts/swipe-machine-test.mjs proves the machine's arithmetic. It cannot
// prove the wheel listener is attached, that the rail moves, or that the
// placeholder badge renders - which is exactly the gap that let "smooth"
// claims survive on the SwiftUI arm.
//
// Never run against the host desktop. Inside the VM:
//
//   ./build/scripts/vm.sh run-atrium \
//       --remote-debugging-port=9334
//   ssh -N -L 9334:127.0.0.1:9334 admin@<vm-ip> &
//   ATRIUM_CDP_PORT=9334 node build/scripts/swipe-gesture-probe.mjs
//
// 9334, not 9333: a tunnel left open on the port a LOCAL Atrium wants makes
// that Atrium fail to bind its own devtools server, and every reading then
// silently describes the VM instead. It once cost a wasted verification round.
// Close the tunnel when done: pkill -f '9334:127.0.0.1:9334'
//
// Exits non-zero on the first failed assertion, so it works as a gate.

import {execFileSync} from 'node:child_process';
import os from 'node:os';
import path from 'node:path';
import {connect as libConnect, SURFACE_STATE_JS, surfaceRank}
  from './sidebar-probe-lib.mjs';

// 9334, matching sidebar-probe-lib and the two Makefile targets that run this
// with no port set. It defaulted to 9333, so `make swipe-probe` looked on a
// port nothing was serving and then reported that port back - correct about
// what it did, useless about why.
const PORT = process.env.ATRIUM_CDP_PORT || '9334';
const base = `http://127.0.0.1:${PORT}`;

let failures = 0;
function check(ok, what, detail) {
  console.log(`${ok ? '  ok  ' : '  FAIL'}  ${what}${detail ? `  (${detail})` : ''}`);
  if (!ok) {
    failures++;
  }
}

let list;
try {
  list = await (await fetch(`${base}/json/list`)).json();
} catch {
  console.error(`No debug port on ${PORT}. Launch Atrium in the VM with ` +
      `--remote-debugging-port=${PORT} and ` +
      `forward the port (ssh -N -L ${PORT}:127.0.0.1:${PORT} ...).`);
  process.exit(1);
}

const sidebars = list.filter((t) => t.url.includes('top-chrome/sidebar'));
if (!sidebars.length) {
  console.error('No HTML sidebar target found.');
  process.exit(1);
}

// Module-level, because this probe closes and reopens the socket as it walks
// candidate surfaces and the rest of the file reaches `ws` and `send` by name.
// Only the PLUMBING moved to the lib - this file already imported
// the surface-ranking half from there and kept a second connect of its own.
let ws;
let send;

async function connect(target) {
  ({ws, send} = await libConnect(target));
}

// One sidebar per WINDOW, and every window has its own tab strip. The folder
// only exists past a tab count (see kFoldedFrom), so picking the first target
// tests whichever window happened to sort first - which for a while looked
// exactly like "the folder is broken".
//
// ranking by CONTENT was the wrong correction. Every
// AtriumBrowserWindow builds a surface, including per-profile windows that are
// never shown, and those render identical rows - so "richest" regularly picked
// a hidden one, whose elements have no layout. That is what threw
// `getComputedStyle: parameter 1 is not of type 'Element'` from railX(): on a
// surface that was never laid out, .rail is simply absent.
//
// Visibility decides now, using sidebar-probe-lib's shared scoring rather than
// a fourth inline copy of it. Content stays as the tie-break it always was, and
// is still returned because the retry below and the printed header both read it.
async function pickSidebar() {
  let best = null;
  for (const candidate of sidebars) {
    await connect(candidate);
    const state = await evaluateRaw(SURFACE_STATE_JS).catch(() => null);
    const score = await evaluateRaw(`(() => ({
      folders: document.querySelectorAll('.frow').length,
      rows: document.querySelectorAll('.tabrow').length,
    }))()`);
    ws.close();
    const m = state ? JSON.parse(state) : {v: false, w: 0, a: 0};
    const visible = surfaceRank(m);
    const content = (score?.folders ? 1000 : 0) + (score?.rows || 0);
    if (!best || visible > best.visible ||
        (visible === best.visible && content > best.content)) {
      best = {target: candidate, visible, content, area: m.a, score};
    }
  }
  return best;
}

async function evaluateRaw(expression) {
  const r = await send('Runtime.evaluate', {expression, returnByValue: true});
  return r.result?.result?.value;
}
const evaluate = async (expression) => {
  const r = await send('Runtime.evaluate', {expression, returnByValue: true});
  if (r.result?.exceptionDetails) {
    throw new Error(r.result.exceptionDetails.exception?.description ||
        'evaluate threw');
  }
  return r.result?.result?.value;
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

// The placeholder folder only exists past a tab count, and a restored session
// usually has fewer. Rather than fail on a precondition - which reads as "the
// folder is broken" and cost a wrong diagnosis once already - the probe makes
// its own: window.open with a user gesture puts tabs in the SAME window, where
// /json/new opens a new one (with its own sidebar) and helps nothing.
async function ensureAWindowWithEnoughTabs() {
  const page = list.find((t) => t.type === 'page' && /^https?:/.test(t.url));
  if (!page) {
    return;
  }
  await connect(page);
  for (let i = 0; i < 3; i++) {
    await send('Runtime.evaluate', {
      expression: `window.open('about:blank?probe=${i}', '_blank')`,
      userGesture: true,
    });
    await new Promise((r) => setTimeout(r, 900));
  }
  ws.close();
  await new Promise((r) => setTimeout(r, 800));
}

// The rail needs more than one SPACE, and the sidebar renders
// the real model - so a one-space profile has genuinely nothing to swipe
// between and no rail at all. Placeholders used to hide that. Add a space the
// way the probe already adds tabs, rather than failing on a precondition.
function ensureTwoSpaces() {
  const key = path.join(process.env.TART_HOME || path.join(os.homedir(), '.tart'),
      'atrium-probe-key');
  const ip = execFileSync('./build/scripts/vm.sh', ['ip'], {encoding: 'utf8'}).trim();
  // Atrium is killed FIRST, before the file is touched. It writes
  // its in-memory model over spaces.json as it goes down, so a running
  // instance silently reverts whatever is staged here - measured directly:
  // the file read ['Personal', 'Probe Space'] before a pkill and ['Personal']
  // after it. That makes this function's own advice ("relaunch and re-run")
  // impossible to follow, because relaunching means killing the instance that
  // then eats the edit. Every re-run just re-reported "added, now 2". That is
  // the reason `make webui-probe` has never come back green, and no amount of
  // fixing the picker or the port would have reached it.
  //
  // Killing the app this probe is about to talk to is safe here only because
  // every branch that writes also exits asking for a relaunch.
  const script = `
python3 - <<'PY'
import json, os, sys, subprocess, time
if not os.path.isdir('/Users/admin'):
    print('refusing: no /Users/admin, this is not the Tart VM'); sys.exit(1)
p = os.path.expanduser('~/Library/Application Support/Atrium/spaces.json')
if not os.path.exists(p):
    print('no spaces.json'); sys.exit(0)
d = json.load(open(p))
if len(d.get('spaces', [])) > 1:
    print('already', len(d['spaces'])); sys.exit(0)
subprocess.run(['pkill', '-x', 'Atrium'])
time.sleep(2)
if not os.path.exists(p):
    print('no spaces.json'); sys.exit(0)
d = json.load(open(p))
spaces = d.get('spaces', [])
if len(spaces) > 1:
    print('already', len(spaces)); sys.exit(0)
if not spaces:
    print('no space to clone'); sys.exit(0)
extra = json.loads(json.dumps(spaces[0]))
extra['id'] = '00000000-0000-4000-8000-0000000009e2'
extra['name'] = 'Probe Space'
extra['order'] = len(spaces)
extra['tabs'] = []
extra['pinnedTabs'] = []
spaces.append(extra)
# A real tab GROUP too: there is no invented folder, so a
# profile with no groups has nothing foldable and the folder assertions
# below have nothing to assert against.
first = spaces[0]
if not first.get('groups'):
    first['groups'] = [{'id': '00000000-0000-4000-8000-00000000f01d',
                        'name': 'Probe Folder', 'collapsed': False}]
# Membership, not just existence: a group with no member tabs renders as an
# EMPTY folder, and 'clicking a folder folds it' then measures 0px -> 0px.
# The precondition this probe asserts is a folder with tabs IN it.
if not any(t.get('groupID') for t in first.get('tabs', [])):
    gid = first['groups'][0]['id']
    for tab in first.get('tabs', [])[:2]:
        tab['groupID'] = gid
json.dump(d, open(p, 'w'))
print('added, now', len(spaces))
PY`;
  return execFileSync('ssh', [
    '-i', key, '-o', 'StrictHostKeyChecking=no',
    '-o', 'UserKnownHostsFile=/dev/null', '-o', 'LogLevel=ERROR',
    `admin@${ip}`, script,
  ], {encoding: 'utf8'}).trim();
}

// spaces.json is read at startup, so a space added now needs a relaunch. Say
// so plainly instead of failing later with "1 pane", which reads as the rail
// being broken.
const spacesState = ensureTwoSpaces();
if (spacesState.startsWith('added')) {
  console.log(`\nPrepared the VM: ${spacesState}.`);
  console.log('Atrium was stopped so the edit would survive - relaunch and re-run.');
  process.exit(1);
}
// an unprepared profile used to fall straight through to the
// assertions, which then failed with "0 panes" and read as the rail being
// broken. This probe cannot make a profile out of nothing, so it says what is
// missing and stops, rather than reporting a product failure it invented.
if (spacesState.startsWith('no ')) {
  console.error(`\nThe VM profile is not prepared: ${spacesState}.`);
  console.error('Stage a spaces fixture into the VM and launch Atrium once, e.g.');
  console.error('  scp build/scenarios/fixtures/two-tabs.spaces.json \\');
  console.error("    admin@$(./build/scripts/vm.sh ip):'~/Library/Application Support/Atrium/spaces.json'");
  console.error('then ./build/scripts/vm.sh run-atrium' +
      ' --remote-debugging-port=9334 and re-run.');
  process.exit(1);
}

let picked = await pickSidebar();
if (!picked.score.folders) {
  await ensureAWindowWithEnoughTabs();
  picked = await pickSidebar();
}
await connect(picked.target);

console.log('\nsidebar probe' +
    ` (window with ${picked.score.rows} tab rows` +
    `, ${picked.score.folders ? 'a folder' : 'no folder'})`);

// First half: the browser must have supplied several spaces on its own.
// Injecting them here (which build/scripts/swipe-bench.mjs does) would prove nothing
// about the running app.
// Scoped to .wsname: the placeholder FOLDER carries the same badge,
// and counting every badge on the page made this assertion drift the moment a
// second kind of placeholder appeared.
const shape = await evaluate(`(() => ({
  panes: document.querySelectorAll('.railpane').length,
  badges: document.querySelectorAll('.wsname .phbadge').length,
  badgeText: document.querySelector('.wsname .phbadge')?.textContent || '',
  names: [...document.querySelectorAll('.wsname')].map(
      (n) => n.firstElementChild?.nextElementSibling?.textContent || ''),
}))()`);
check(shape.panes > 1, 'the sidebar shows several spaces',
    `${shape.panes} panes: ${shape.names.join(', ')}`);
// Inverted since these are the profile's REAL spaces now, so a
// placeholder badge on one would mean the invented data came back.
check(shape.badges === 0, 'the spaces are real, not placeholders',
    `${shape.badges} placeholder badges, names: ${shape.names.join(', ')}`);

const railX = () => evaluate(
    `new DOMMatrixReadOnly(getComputedStyle(
        document.querySelector('.rail')).transform).m41`);

// The rail is N panes wide; if nothing static clips it the DOCUMENT
// becomes the horizontal scroller, so a swipe scrolls the page and a scrollbar
// appears along the bottom. Asserted before AND after a gesture, because the
// overflow only appears once the rail is built.
const overflow = () => evaluate(`(() => {
  const d = document.documentElement;
  // Only auto/scroll counts: 'hidden' is the clip that fixes this, and
  // 'visible' does not scroll, it overflows onto whatever does.
  const over = [...document.querySelectorAll('*')].filter(
      (el) => el.scrollWidth > el.clientWidth + 1 &&
              ['auto', 'scroll'].includes(getComputedStyle(el).overflowX));
  return {
    doc: d.scrollWidth - d.clientWidth,
    scrollers: over.map((el) => el.className || el.tagName),
  };
})()`);

// First half: the rail follows the fingers while they are still down.
// Wheel events are dispatched through the real input pipeline (Input.
// dispatchMouseEvent), not synthesised on the element, so this exercises the
// listener as the trackpad reaches it.
const rect = await evaluate(
    `JSON.stringify(document.querySelector('.rail').getBoundingClientRect())`);
const box = JSON.parse(rect);
const at = {x: Math.round(box.width / 2), y: Math.round(box.height / 2)};

// Fractions of a pane, not pixels: the commit thresholds are relative to the
// sidebar width and the user can drag it.
const paneW = box.width;
async function wheel(fraction, steps, gapMs, y = at.y) {
  const deltaX = (fraction * paneW) / steps;
  for (let i = 0; i < steps; i++) {
    if (i) {
      await sleep(gapMs);  // no trailing sleep: the release is inferred from
    }                      // silence, and this must read before that fires
    await send('Input.dispatchMouseEvent', {
      type: 'mouseWheel', x: at.x, y,
      deltaX, deltaY: 0, pointerType: 'mouse',
    });
  }
}

// A known starting space. Without this the run inherits wherever the previous
// one left the rail, and "commits to exactly one space" fails at the rail end
// for a reason that has nothing to do with the swipe.
await evaluate('window.atriumSwipeTo && window.atriumSwipeTo(0)');
await sleep(900);

// The rail must reach the bottom of the window, not just the bottom
// of its own content: the wheel listener lives on the route root, and an
// auto-height root left the empty strip below the last row outside it - a
// swipe started there did nothing. elementFromPoint is the assertion because
// "no element here" is exactly what the dead zone was.
const fill = await evaluate(`(() => {
  const root = document.getElementById('atrium-root');
  const low = document.elementFromPoint(30, innerHeight - 20);
  return {
    viewport: innerHeight,
    root: Math.round(root.getBoundingClientRect().height),
    atBottom: low ? (low.className || low.tagName) : null,
  };
})()`);
check(fill.root >= fill.viewport - 1 && fill.atBottom !== null,
    'the sidebar reaches the bottom of the window',
    `root ${fill.root}px of ${fill.viewport}px, bottom hits ${fill.atBottom || 'NOTHING'}`);

const before = await overflow();
check(before.doc <= 0, 'the document is not horizontally scrollable at rest',
    `overflow ${before.doc}px, scrollable: ${before.scrollers.join(', ') || 'none'}`);

const start = await railX();
// Well under the 15% commit threshold, and slow enough that the flick gate
// cannot fire either.
await wheel(0.10, 10, 25);
const during = await railX();
check(Math.abs(during - start) > 4, 'the rail tracks the fingers',
    `moved ${(during - start).toFixed(1)}px of a ${paneW.toFixed(0)}px pane`);

const mid = await overflow();
check(mid.doc <= 0, 'the document does not scroll during a swipe',
    `overflow ${mid.doc}px, scrollable: ${mid.scrollers.join(', ') || 'none'}`);

// Second half: release rolls a short swipe back...
await sleep(1200);
const rolledBack = await railX();
check(Math.abs(rolledBack - start) < 1.5, 'a short swipe rolls back on release',
    `settled at ${rolledBack.toFixed(1)}px, started at ${start.toFixed(1)}px`);

// ...and commits a long one - by exactly one space, never two.
await wheel(0.45, 12, 20);
await sleep(1600);
const committed = await railX();
check(Math.abs((start - committed) - paneW) < paneW * 0.06,
    'a long swipe commits to exactly one space',
    `moved ${(start - committed).toFixed(1)}px of a ${paneW.toFixed(0)}px pane`);

// Back to the first space before the folder block. A committed
// swipe really switches the window, so the assertions above leave the probe on
// a DIFFERENT space - and the tab group lives in the first one. Without this
// the folder assertions fail for a reason that has nothing to do with folders.
await evaluate('window.atriumSwipeTo && window.atriumSwipeTo(0)');
await sleep(1500);

// The behaviour rather than the geometry: a gesture started well
// below the last row must move the rail. This is the report - "swipe works in
// the upper section, does nothing in the lower area".
const lowStart = await railX();
await wheel(0.10, 10, 25, fill.viewport - 40);
const lowMoved = await railX();
check(Math.abs(lowMoved - lowStart) > 4,
    'a swipe started below the content still moves the rail',
    `moved ${(lowMoved - lowStart).toFixed(1)}px from y=${fill.viewport - 40}`);
await sleep(1200);

// The folder rows render, and the collapse
// animation needs a toggle to run. Driven through a real click, not by calling the
// model, and measured mid-transition - a fold that jumps straight to 0fr is
// the rebuild bug, not a working animation.
const folder = await evaluate(`(() => {
  const head = document.querySelector('.frow');
  if (!head) return null;
  return {id: head.dataset.folderId,
          badged: !!head.querySelector('.phbadge'),
          rows: document.querySelectorAll('.fold .tabrow').length};
})()`);
check(folder !== null && folder.rows > 0,
    'there is a folder with real tabs in it',
    folder ? `${folder.rows} rows in "${folder.id}"` : 'no folder rendered');
// Inverted since the folder is a REAL tab group now, so a
// placeholder badge on it would mean the invented data came back.
check(folder !== null && !folder.badged,
    'the folder is a real tab group, not an invented one',
    folder ? `named "${folder.id}"` : 'no folder');

if (folder) {
  // reads 'none' when there is no .fold rather than throwing. This
  // probe's whole complaint was that it died before asserting anything, and a
  // getComputedStyle(null) here does exactly that - a missing element becomes
  // a stack trace instead of a named failing check. parseFloat('none') is NaN,
  // and every comparison below is false against NaN, so an absent fold fails
  // its assertions and says which one.
  const foldRows = () => evaluate(
      `(() => { const f = document.querySelector('.fold');
                return f ? getComputedStyle(f).gridTemplateRows : 'none'; })()`);
  // Start from OPEN. The fold persists, so a previous run - or
  // the user - can leave it closed, and "clicking a folder folds it" would
  // then measure an unfold and report 0px -> 0px. The probe establishes what
  // it asserts rather than assuming it.
  if (parseFloat(await foldRows()) < 1) {
    await evaluate(`document.querySelector('.frow')?.click()`);
    await sleep(600);
  }
  const open = await foldRows();
  // A DOM click, not Input.dispatchMouseEvent: the folder's y moves with the
  // tab count, and hit-testing is not what is under test here - the click
  // handler and the animation are. The wheel and hover paths that DO depend on
  // real input are driven through the pipeline elsewhere.
  await evaluate(`document.querySelector('.frow')?.click()`);
  await sleep(80);
  const mid = await foldRows();
  await sleep(500);
  const shut = await foldRows();
  check(parseFloat(shut) < 1 && parseFloat(open) > 1,
      'clicking a folder folds it',
      `${open} -> ${shut}`);
  check(parseFloat(mid) < parseFloat(open) && parseFloat(mid) > parseFloat(shut),
      'the collapse animates rather than jumping',
      `mid-transition ${mid}`);

  // The snapshot is re-pushed on every tab title change. Rebuilt from
  // what is actually on screen rather than invented, so the probe does not
  // quietly replace the browser's space set and leave the next run starting
  // somewhere else - which is exactly what it did the first time.
  await evaluate(`(() => {
    const names = [...document.querySelectorAll('.wsname')]
        .map((n) => n.firstElementChild?.nextElementSibling?.textContent || '');
    window.atriumSetSidebar({
      tabs: [{id: '1', title: 'One'}, {id: '2', title: 'Two'}],
      spaces: names.map((name) => ({name})),
      spacesArePlaceholder: true,
      // this payload omitted activeSpace, and the surface compared it
      // raw, so no pane was the active one and the top-level tabs and folders
      // below were dropped from every pane - the folder vanished outright and
      // the two assertions after this one failed for a reason that had nothing
      // to do with folds. Taken from the rail rather than hardcoded so the push
      // describes the space actually on screen.
      activeSpace: Number(document.querySelector('.rail')?.dataset?.activeSpace ?? 0),
      // closed: true - what the browser really sends once the fold has
      // persisted. The surface used to override this and keep its
      // own state; it believes the model now, so injecting false here would
      // assert the OLD contract.
      folders: [{id: ${JSON.stringify(folder.id)}, name: 'Sample Folder',
                 closed: true,
                 tabs: [{id: '3', title: 'Three'}]}],
    });
  })()`);
  await sleep(400);
  const afterPush = await foldRows();
  check(parseFloat(afterPush) < 1,
      'an unrelated re-push does not lose the fold',
      `${afterPush} after the push`);

  await evaluate(`document.querySelector('.frow')?.click()`);
  await sleep(500);
  check(parseFloat(await foldRows()) > 1, 'clicking again unfolds it');
}

const stats = await evaluate('window.__swipeStats || null');
if (stats) {
  console.log(`\n  gesture frames: median ${stats.median.toFixed(1)}ms  ` +
      `p95 ${stats.p95.toFixed(1)}ms  worst ${stats.worst.toFixed(1)}ms  ` +
      `(${stats.frames} frames)`);
} else {
  console.log('\n  (no __swipeStats - the gesture never ran a frame loop)');
}

ws.close();
console.log(failures ? `\n${failures} assertion(s) failed` : '\nall assertions passed');
process.exit(failures ? 1 : 0);
