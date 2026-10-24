#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// dragging a favourite tile reorders the grid, live and on commit.
//
//   ./build/scripts/vm-cdp.sh && node build/scripts/fav-drag-probe.mjs
//
// Asserts the persisted model, not only the message. The save is DEBOUNCED, so
// there is a wait before that read - without it the old file comes back and
// reads as "the reorder never happened".
//
// LIMITATION: this probe synthesises DragEvents with dispatchEvent, which
// bypasses the browser entirely. It verifies handlers and index maths and
// CANNOT see whether a real pointer starts or survives a drag - it passed
// while dragging had never worked once. make
// drag-input-probe is the check that covers that.

import {connect} from './sidebar-probe-lib.mjs';

import {execFileSync} from 'node:child_process';

import {probeKeyPath, vmHost} from './vm-model.mjs';

const PORT = process.env.ATRIUM_CDP_PORT || '9334';
const VM = vmHost();
const KEY = probeKeyPath();

const favouritesInVM = () => JSON.parse(execFileSync('ssh', [
  '-i', KEY, '-o', 'StrictHostKeyChecking=no', '-o', 'UserKnownHostsFile=/dev/null',
  '-o', 'LogLevel=ERROR', VM,
  `python3 -c "import json,os;d=json.load(open(os.path.expanduser('~/Library/Application Support/Atrium/spaces.json')));` +
  `print(json.dumps([f['title'] for f in d.get('favorites',[])]))"`,
], {encoding: 'utf8'}));

const targets = await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json();
const target = targets.find((t) => t.url.includes('top-chrome/sidebar'));
if (!target) {
  console.error('no sidebar target on :' + PORT + '. Is Atrium running in the VM?');
  process.exit(2);
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

const GRID = `document.querySelector('.railpane [data-dropzone="favorites"]') ` +
             `|| document.querySelector('[data-dropzone="favorites"]')`;
// Favourites carry their URL as their id, which is what identifies one.
const order = () => ev(`[...(${GRID}).querySelectorAll('.fav')].map((f) => f.dataset.favId)`);

const before = await order();
if (!before || before.length < 3) {
  console.error(`needs at least 3 favourites, found ${before ? before.length : 0}`);
  process.exit(2);
}
console.log(`favourites: ${JSON.stringify(before)}\n`);

check('favourite tiles are draggable',
    await ev(`[...(${GRID}).querySelectorAll('.fav')].every((f) => f.draggable)`), true);

// Drag tile 0 onto the far side of tile 2. Held over that tile's centre plus a
// clear margin, so the hysteresis dead zone is not what decides the slot.
const dragged = await ev(`(() => {
  window.__sent = [];
  if (!chrome.send.__wrapped) {
    const real = chrome.send.bind(chrome);
    chrome.send = (n, a) => { window.__sent.push([n, a]); return real(n, a); };
    chrome.send.__wrapped = true;
  }
  const grid = ${GRID};
  const tiles = [...grid.querySelectorAll('.fav')];
  const src = tiles[0];
  const dt = new DataTransfer();
  src.dispatchEvent(new DragEvent('dragstart', {bubbles: true, dataTransfer: dt}));
  const marked = src.classList.contains('favdragging');

  const third = tiles[2].getBoundingClientRect();
  const x = (third.left + third.right) / 2 + 20;
  const y = (third.top + third.bottom) / 2;
  grid.dispatchEvent(new DragEvent('dragover', {bubbles: true, dataTransfer: dt,
      clientX: x, clientY: y}));
  const preview = [...grid.querySelectorAll('.fav')].map((f) => f.dataset.favId);
  const moved = [...grid.querySelectorAll('.fav')].filter((f) => f.style.transition).length;

  grid.dispatchEvent(new DragEvent('drop', {bubbles: true, dataTransfer: dt,
      clientX: x, clientY: y}));
  src.dispatchEvent(new DragEvent('dragend', {bubbles: true, dataTransfer: dt}));
  // The cross-window tear-off tracking clears itself on every
  // dragend, unrelated to the reorder this probe checks.
  return {marked, preview, moved,
          sent: window.__sent.filter((m) => m[0] !== 'sidebarTabDragMoved')};
})()`);

check('the dragged tile is marked', dragged.marked, true);
// The preview is the whole point: it must show the order that gets committed.
const expectedPreview = [before[1], before[2], before[0], ...before.slice(3)];
check('the grid previews the move live', dragged.preview, expectedPreview);
// FLIP: the transform is set to the OLD offset and immediately cleared, so the
// tile transitions back to zero from where it was. The inline transform is
// therefore already empty here - the transition is the trace that survives.
check('the other tiles animate from where they were', dragged.moved > 0, true);
check('the drop commits the previewed move',
    dragged.sent, [['sidebarFavoriteReordered', [0, 2]]]);

await wait(400);
check('no drag state survives', await ev(
    `(${GRID}).querySelectorAll('.favdragging').length`), 0);

console.log('\nwaiting for the debounced save...');
await wait(8000);
const persisted = favouritesInVM();
console.log(`  persisted: ${JSON.stringify(persisted)}`);
// The model stores titles; the grid keys on URL. Compare by position instead:
// what matters is that the first favourite is no longer first.
check('the model committed the same move',
    await order(), expectedPreview);

ws.close();
console.log(`\n${failures ? failures + ' FAILED' : 'all checks passed'}`);
process.exit(failures ? 1 : 0);
