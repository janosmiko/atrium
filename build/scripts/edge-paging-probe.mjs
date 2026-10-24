#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// holding a dragged row at the sidebar's edge pages to the next
// space, and dropping there moves the tab into it.
//
//   ./build/scripts/vm-cdp.sh && node build/scripts/edge-paging-probe.mjs
//
// The dwell is real time, so this probe genuinely waits. That is the point:
// the page must NOT fire on a drag that merely passes over the edge, and only
// a clock can tell those two apart.

import {connect} from './sidebar-probe-lib.mjs';

import {execFileSync} from 'node:child_process';

import {probeKeyPath, vmHost} from './vm-model.mjs';

const PORT = process.env.ATRIUM_CDP_PORT || '9334';
const VM = vmHost();
const KEY = probeKeyPath();

// Reads the persisted model out of the VM. The save is DEBOUNCED, so a read
// taken straight after the drop sees the old file and reads as "the move never
// happened" - which is exactly the wrong conclusion, and one this probe drew
// three times before the wait was added.
const spacesInVM = () => JSON.parse(execFileSync('ssh', [
  '-i', KEY, '-o', 'StrictHostKeyChecking=no', '-o', 'UserKnownHostsFile=/dev/null',
  '-o', 'LogLevel=ERROR', VM,
  `python3 -c "import json,os;d=json.load(open(os.path.expanduser('~/Library/Application Support/Atrium/spaces.json')));` +
  `print(json.dumps([[s['name'],[t.get('title') or '' for t in s['tabs']]] for s in d['spaces']]))"`,
], {encoding: 'utf8'}));

const targets = await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json();
const candidates = targets.filter((t) => t.url.includes('top-chrome/sidebar'));
if (!candidates.length) {
  console.error('no sidebar target on :' + PORT + '. Is Atrium running in the VM?');
  process.exit(2);
}

// One surface exists per window, including the per-profile windows that are
// never shown. They all render identical rows, so taking the first reads a
// window nobody is looking at - and a hidden one has no laid-out rail at all,
// which reports as "found 0 panes".
let target = candidates[0];
if (candidates.length > 1) {
  for (const candidate of candidates) {
    const probe = new WebSocket(candidate.webSocketDebuggerUrl);
    try {
      await new Promise((r, x) => {
        probe.addEventListener('open', r, {once: true});
        probe.addEventListener('error', x, {once: true});
      });
      const visible = await new Promise((r) => {
        probe.addEventListener('message', (e) => {
          const m = JSON.parse(e.data);
          if (m.id === 1) { r(m.result?.result?.value === true); }
        });
        probe.send(JSON.stringify({id: 1, method: 'Runtime.evaluate', params: {
          expression: 'document.visibilityState === "visible" && ' +
                      'document.querySelectorAll(".railpane").length > 0',
          returnByValue: true}}));
      });
      probe.close();
      if (visible) { target = candidate; break; }
    } catch {
      probe.close();
    }
  }
}

const {ws, send: call, ev} = await connect(target);

let failures = 0;
const check = (label, actual, expected) => {
  const ok = JSON.stringify(actual) === JSON.stringify(expected);
  if (!ok) {
    failures++;
  }
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}` +
      (ok ? '' : `\n       expected ${JSON.stringify(expected)}` +
                 `\n       actual   ${JSON.stringify(actual)}`));
};
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

await call('Runtime.enable');

// :not(.ovrailpane) excludes the overview pane ahead of the
// per-space ones - it renders no space of its own by design.
const panes = await ev(
    `[...document.querySelectorAll('.railpane:not(.ovrailpane)')].map((p) => p.dataset.spaceIndex)`);
if (!panes || panes.length < 2) {
  console.error(`needs a rail with 2+ spaces, found ${panes ? panes.length : 0}`);
  process.exit(2);
}
console.log(`panes: ${JSON.stringify(panes)}\n`);

check('every rail pane declares which space it is', panes, ['0', '1'].slice(0, panes.length)
    .concat(panes.slice(2).map((_, i) => String(i + 2))));

// Start from the first space. The rail remembers where it was left, so a
// re-run that begins at the LAST space would ask to page past the end - which
// is correctly refused, and reads as "paging is broken" if you assume the
// starting point.
await ev(`document.querySelector('.sdot[data-space-index="0"]')?.click()`);
await wait(900);

await ev(`window.__sent = [];
  if (!chrome.send.__wrapped) {
    const real = chrome.send.bind(chrome);
    chrome.send = (n, a) => { window.__sent.push([n, a]); return real(n, a); };
    chrome.send.__wrapped = true;
  }`);

// Start a drag on the first pane's first tab row and hold it at the trailing
// edge. dragover is dispatched once: after that the pointer is "still", which
// is exactly the case the poll exists to serve.
const started = await ev(`(() => {
  window.__sent = [];
  window.__railBefore = document.querySelector('.rail').style.transform;
  const pane = document.querySelector('.railpane[data-space-index="0"]');
  const row = [...pane.querySelector('[data-dropzone="tabs"]').children]
      .filter((e) => e.matches('.tabrow'))[1];
  const root = document.querySelector('#atrium-root');
  const box = root.getBoundingClientRect();
  window.__dt = new DataTransfer();
  row.dispatchEvent(new DragEvent('dragstart', {bubbles: true, dataTransfer: window.__dt}));
  // Well inside the trailing edge zone (24px), measured on the sidebar's own
  // viewport rather than the rail, which is one pane per space and far wider.
  root.dispatchEvent(new DragEvent('dragover', {bubbles: true, dataTransfer: window.__dt,
      clientX: box.right - 6, clientY: box.top + box.height / 2}));
  return {title: row.querySelector('.tl').textContent, x: box.right - 6};
})()`);
console.log(`dragging: ${JSON.stringify(started.title)} held at x=${started.x}\n`);

// Before the dwell elapses nothing may have happened.
await wait(150);
check('a drag merely passing the edge has not paged yet',
    await ev(`window.__sent.filter((s) => s[0] === 'sidebarSpaceSelected').length`), 0);

// After it, a page - allowing for the settle. The message is sent when the
// slide COMMITS the new index, not when the page is decided, so the wait
// covers the dwell plus the settle animation rather than the dwell alone.
await wait(1200);
const paged = await ev(
    `window.__sent.filter((s) => s[0] === 'sidebarSpaceSelected').map((s) => s[1][0])`);
check('holding at the edge pages once the dwell elapses', paged.length >= 1, true);
check('the rail moved to the next space', paged[paged.length - 1] ?? null, 1);
check('the rail actually slid', await ev(
    `document.querySelector('.rail').style.transform !== window.__railBefore`), true);

// Drop on the space now shown. Its pane is the destination, which is what
// makes this a cross-space move rather than a reorder.
const dropped = await ev(`(() => {
  window.__sent = [];
  const pane = document.querySelector('.railpane[data-space-index="1"]');
  const zone = pane.querySelector('[data-dropzone="tabs"]');
  const box = zone.getBoundingClientRect();
  zone.dispatchEvent(new DragEvent('dragover', {bubbles: true, dataTransfer: window.__dt,
      clientY: box.top + 4}));
  zone.dispatchEvent(new DragEvent('drop', {bubbles: true, dataTransfer: window.__dt,
      clientY: box.top + 4}));
  document.querySelector('.railpane').dispatchEvent(
      new DragEvent('dragend', {bubbles: true, dataTransfer: window.__dt}));
  return window.__sent;
})()`);
// Presence, not sole occupant: a re-render emits sidebarFavoriteCount in the
// same batch, and asserting the exact list fails for a reason unrelated to the
// move.
// The 5th argument is the destination slot. It used to append
// unconditionally, landing the tab somewhere the indicator never showed.
const move = dropped.filter((m) => m[0] === 'sidebarTabMovedToSpace');
check('dropping in the other space sends a cross-space move',
    move.map((m) => m[1].slice(0, 4)), [[0, 1, 'tabs', 1]]);
check('and carries the slot it was dropped at',
    Number.isInteger(move[0] && move[0][1][4]), true);

// The poll must not outlive the drag - it is the only thing this module leaves
// running between events.
//
// Measured as "the count stops GROWING", not "the count is zero". A
// cross-profile move legitimately announces once more after the drop: the
// model's active space changed, the rail rebuilds and adopts it, and adopting
// a different index announces it. That is one settling message, not a poll
// still running - and asserting zero conflated the two, which only showed up
// on a five-profile fixture where every space switch crosses profiles.
await wait(900);
const settled = await ev(
    `window.__sent.filter((s) => s[0] === 'sidebarSpaceSelected').length`);
await wait(1600);
check('paging stops when the drag ends', await ev(
    `window.__sent.filter((s) => s[0] === 'sidebarSpaceSelected').length`), settled);
check('no drag state survives', await ev(
    `document.querySelectorAll('.dragging, .dropinto, #atrium-root.dragactive').length`), 0);

// The model is the real assertion: the messages above only prove the surface
// asked for the right thing.
const dragged = started.title;
console.log(`\nwaiting for the debounced save...`);
await wait(8000);
const spaces = spacesInVM();
const source = spaces[0][1];
const destination = spaces[1][1];
check('the tab left the space it was dragged from',
    source.includes(dragged), false);
check('the tab arrived in the space it was dropped on',
    destination.includes(dragged), true);
console.log(`  ${spaces[0][0]} -> ${JSON.stringify(source)}`);
console.log(`  ${spaces[1][0]} -> ${JSON.stringify(destination)}`);

ws.close();
console.log(`\n${failures ? failures + ' FAILED' : 'all checks passed'}`);
process.exit(failures ? 1 : 0);
