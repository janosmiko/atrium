#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// drives a real drag in the VM's HTML sidebar and checks the row
// actually moves, in the model as well as on screen.
//
//   ./build/scripts/vm-cdp.sh && node build/scripts/row-drag-probe.mjs
//
// This exists because the unit tests cannot catch the bug that shipped here:
// installRowDrag collected rows from the WHOLE root, and the rail holds one
// pane per space, so a 3-row list produced drop index 5. Both index maths were
// right; the element list was wrong. Only a real DOM with a real rail shows it.
//
// LIMITATION: this probe synthesises DragEvents with dispatchEvent, which
// bypasses the browser entirely. It verifies handlers and index maths and
// CANNOT see whether a real pointer starts or survives a drag - it passed
// while dragging had never worked once. make
// drag-input-probe is the check that covers that.

import {connect} from './sidebar-probe-lib.mjs';

const PORT = process.env.ATRIUM_CDP_PORT || '9334';

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

await call('Runtime.enable');

// Capture what the surface asks the browser to do, so a wrong index is visible
// as an index rather than only as "the row did not move".
await ev(`window.__sent = [];
  if (!chrome.send.__wrapped) {
    const real = chrome.send.bind(chrome);
    chrome.send = (n, a) => { window.__sent.push([n, a]); return real(n, a); };
    chrome.send.__wrapped = true;
  }`);

// No rail when there is only one space - renderSidebar draws straight into
// the root - so fall back rather than dereferencing null. [data-space-index]
// skips the rowless .ovrailpane, which a bare '.railpane' matches first.
const PANE = `(document.querySelector('.railpane[data-space-index]') || document.querySelector('.sidebar'))`;
const ROWS = `[...${PANE}.querySelectorAll('.rows > .tabrow')]`;
const titles = () => ev(`${ROWS}.map((r) => r.querySelector('.tl').textContent)`);

const before = await titles();
if (before.length < 3) {
  console.error(`need at least 3 tab rows in the active space, found ${before.length}`);
  process.exit(2);
}
console.log(`rows: ${JSON.stringify(before)}\n`);

// A drag is dragstart -> dragover -> drop -> dragend. dragover matters: without
// it no indicator is ever positioned, and preventDefault there is what allows
// the drop at all.
const drag = (fromIndex, edge) => ev(`(() => {
  window.__sent = [];
  const rows = ${ROWS};
  const dt = new DataTransfer();
  const src = rows[${fromIndex}];
  src.dispatchEvent(new DragEvent('dragstart', {bubbles: true, dataTransfer: dt}));
  // Measured HERE, after dragstart. Revealing the empty-section affordances
  // reflows the sidebar, so a Y captured before the drag points at where a row
  // used to be. A real pointer never uses a stale coordinate - dragover fires
  // continuously and the geometry is re-read each time.
  const live = ${ROWS};
  const y = '${edge}' === 'bottom'
      ? live[live.length - 1].getBoundingClientRect().bottom - 2
      : live[0].getBoundingClientRect().top + 2;
  src.dispatchEvent(new DragEvent('dragover', {bubbles: true, dataTransfer: dt, clientY: y}));
  const line = document.querySelector('.dropline');
  const shown = !!line && !line.hidden;
  src.dispatchEvent(new DragEvent('drop', {bubbles: true, dataTransfer: dt, clientY: y}));
  src.dispatchEvent(new DragEvent('dragend', {bubbles: true, dataTransfer: dt}));
  // The cross-window tear-off tracking clears itself on every
  // dragend, unrelated to the reorder this probe checks.
  return {shown, sent: window.__sent.filter((m) => m[0] !== 'sidebarTabDragMoved')};
})()`);

const settle = () => new Promise((r) => setTimeout(r, 2000));

// 1. Drag the last row to the top.
const last = before.length - 1;
const up = await drag(last, 'top');
check('the indicator appears while dragging', up.shown, true);
check('dragging to the top sends slot 0', up.sent, [['sidebarTabReordered', [last, 0]]]);
await settle();
check('the row moved to the top',
    await titles(), [before[last], ...before.slice(0, last)]);

// 2. Drag it back down to the end. This is the direction that was broken:
//    the index ran past the end of the list because rows from other panes
//    were counted.
const now = await titles();
const down = await drag(0, 'bottom');
check('dragging to the end stays inside the pane',
    down.sent, [['sidebarTabReordered', [0, last]]]);
await settle();
check('the row moved back to the end',
    await titles(), [...now.slice(1), now[0]]);
check('the order is restored', await titles(), before);

// 3. A drop on the row's own position must not send anything at all. Row 0
//    aimed at the top of the list is the one slot that resolves to no move.
const noop = await drag(0, 'top');
check('dropping a row onto itself sends nothing', noop.sent, []);

// 4. Nothing is left behind for the next gesture to trip over.
check('no row is left marked as dragging',
    await ev(`document.querySelectorAll('.dragging').length`), 0);
check('the indicator is hidden after the drop',
    await ev(`(() => { const l = document.querySelector('.dropline'); return !l || l.hidden; })()`),
    true);

ws.close();
console.log(`\n${failures ? failures + ' FAILED' : 'all checks passed'}`);
process.exit(failures ? 1 : 0);
