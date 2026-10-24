#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// A favourite drag that ends without a drop puts the
// tiles back, and a drag never touches a grid it did not start in.
//
//   ./build/scripts/vm-cdp.sh && node build/scripts/fav-cancel-probe.mjs
//
// Needs two spaces whose favourites are BOUND to them (spaceID set), so pane 0
// and pane 1 render different grids - see sidebar-regression.sh.
//
// WHY SYNTHETIC DragEvents HERE, when the rule is real input.
//
// Both behaviours are decided at DROP or DRAGEND, and neither is reachable
// through the browser's own pipeline from CDP: Input.dispatchMouseEvent stops
// driving a native drag after its first move in this surface, and
// setInterceptDrags plus dispatchDragEvent do not deliver dragover either. That
// limitation is documented in drag-input-probe.mjs, which is why it asserts
// only as far as dragover.
//
// So the split is: drag-input-probe.mjs proves with REAL input that a drag
// survives dragstart and engages a target, and this probe drives the rest with
// synthesised events against the REAL deployed module in the REAL browser DOM -
// real layout, real geometry, real node identity, only the event objects made
// by hand. That is strictly more than the unit suite can see, and strictly less
// than a human dragging. It cannot prove Chromium delivers dragend on an Esc
// cancel; it proves what the module does when it arrives.

import {connect} from './sidebar-probe-lib.mjs';

const PORT = process.env.ATRIUM_CDP_PORT || '9334';
const BASE = `http://127.0.0.1:${PORT}`;

const surfaces = (await (await fetch(`${BASE}/json/list`)).json())
    .filter((t) => t.url.includes('top-chrome/sidebar'));
if (!surfaces.length) {
  console.error(`no sidebar surface on :${PORT}`);
  process.exit(2);
}

// Every AtriumBrowserWindow builds a surface, including per-profile windows
// that are never shown. The biggest one is the window on screen.
let best = null;
for (const s of surfaces) {
  const c = await connect(s);
  const size = await c.ev('window.outerWidth * window.outerHeight');
  if (!best || size > best.size) { best?.c.ws.close(); best = {c, size}; }
  else { c.ws.close(); }
}
const {ev} = best.c;

let failures = 0;
const check = (label, actual, expected) => {
  const ok = JSON.stringify(actual) === JSON.stringify(expected);
  if (!ok) { failures++; }
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}` +
      (ok ? '' : `\n       expected ${JSON.stringify(expected)}` +
                 `\n       actual   ${JSON.stringify(actual)}`));
};

// Build freshness first. Everything below passes against the OLD module too -
// it just passes for the wrong reason - so a stale binary has to fail loudly
// here rather than quietly further down.
// Read through the module system rather than fetch(): the WebUI request filter
// serves the surface's own imports, not arbitrary paths, so fetch() is refused.
// Both fixes live inside installFavoriteDrag, so its source is the whole answer.
const served = await ev(
    `import('/sidebar-fav-drag.js').then((m) => {
       const t = m.installFavoriteDrag.toString();
       return {restore: t.includes('restore && baseOrder'),
               identity: (t.match(/!== grid/g) || []).length};
     })`, true);
check('the deployed module has the restore', served?.restore, true);
check('the deployed module has the identity guard in both handlers',
      served?.identity, 2);
if (!served?.restore || served?.identity !== 2) {
  console.log('\nthe VM is running an older build - deploy before reading further');
  best.c.ws.close();
  process.exit(1);
}

const PANES = `[...document.querySelectorAll('.railpane')]`;
const layout = await ev(`(() => {
  const panes = ${PANES};
  return panes.map((p) => [...p.querySelectorAll('.fav')].map((f) => f.dataset.favId));
})()`);
console.log(`favourites per pane: ${JSON.stringify(layout)}\n`);

const source = layout.findIndex((f) => f.length >= 3);
const other = layout.findIndex((f, i) => i !== source && f.length >= 1);
if (source < 0 || other < 0) {
  console.error('fixture needs one pane with 3+ favourites and another with 1+');
  best.c.ws.close();
  process.exit(2);
}
console.log(`dragging inside pane ${source}, intruding on pane ${other}\n`);

// Preview a move, then end the drag WITHOUT dropping.
const cancelled = await ev(`(() => {
  const grid = ${PANES}[${source}].querySelector('[data-dropzone="favorites"]');
  const tiles = [...grid.querySelectorAll('.fav')];
  const before = tiles.map((f) => f.dataset.favId);
  const dt = new DataTransfer();
  tiles[0].dispatchEvent(new DragEvent('dragstart', {bubbles: true, dataTransfer: dt}));
  const third = tiles[2].getBoundingClientRect();
  grid.dispatchEvent(new DragEvent('dragover', {bubbles: true, dataTransfer: dt,
      clientX: (third.left + third.right) / 2 + 20,
      clientY: (third.top + third.bottom) / 2}));
  const preview = [...grid.querySelectorAll('.fav')].map((f) => f.dataset.favId);
  tiles[0].dispatchEvent(new DragEvent('dragend', {bubbles: true, dataTransfer: dt}));
  const after = [...grid.querySelectorAll('.fav')].map((f) => f.dataset.favId);
  const marked = grid.querySelectorAll('.favdragging').length;
  return {before, preview, after, marked};
})()`);

check('the preview really did move a tile, so the restore has work to do',
      cancelled.preview[0] !== cancelled.before[0], true);
check('a cancelled drag puts the tiles back', cancelled.after, cancelled.before);
check('and drops the dragging marker', cancelled.marked, 0);

// Start in one pane's grid, hover and drop on another's.
const intrusion = await ev(`(() => {
  window.__sent = [];
  if (!chrome.send.__wrapped) {
    const real = chrome.send.bind(chrome);
    chrome.send = (n, a) => { window.__sent.push([n, a]); return real(n, a); };
    chrome.send.__wrapped = true;
  }
  const mine = ${PANES}[${source}].querySelector('[data-dropzone="favorites"]');
  const theirs = ${PANES}[${other}].querySelector('[data-dropzone="favorites"]');
  const tiles = [...mine.querySelectorAll('.fav')];
  const before = tiles.map((f) => f.dataset.favId);
  const dt = new DataTransfer();
  tiles[0].dispatchEvent(new DragEvent('dragstart', {bubbles: true, dataTransfer: dt}));

  const box = theirs.getBoundingClientRect();
  const at = {clientX: (box.left + box.right) / 2, clientY: (box.top + box.bottom) / 2};
  theirs.dispatchEvent(new DragEvent('dragover', {bubbles: true, dataTransfer: dt, ...at}));
  const during = [...mine.querySelectorAll('.fav')].map((f) => f.dataset.favId);
  theirs.dispatchEvent(new DragEvent('drop', {bubbles: true, dataTransfer: dt, ...at}));
  const after = [...mine.querySelectorAll('.fav')].map((f) => f.dataset.favId);
  tiles[0].dispatchEvent(new DragEvent('dragend', {bubbles: true, dataTransfer: dt}));
  return {before, during, after,
          reorders: window.__sent.filter((m) => m[0] === 'sidebarFavoriteReordered')};
})()`);

check('hovering another pane\'s grid does not reflow this one',
      intrusion.during, intrusion.before);
check('dropping on another pane\'s grid does not reorder this one',
      intrusion.after, intrusion.before);
check('and commits nothing to the browser', intrusion.reorders, []);

best.c.ws.close();
console.log(`\n${failures ? failures + ' FAILED' : 'all checks passed'}`);
process.exit(failures ? 1 : 0);
