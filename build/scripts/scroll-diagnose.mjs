#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Read-only: samples the sidebar while YOU scroll, and reports what actually
// moved.
//
//   make run FLAGS='--remote-debugging-port=9333'
//   make scroll-diagnose      # then scroll the sidebar up and down
//
// "The whole sidebar scrolls" has several possible mechanisms and they need
// different fixes, so this names the one in play instead of inviting a guess:
//
//   the scroll zone moved         working as intended
//   the favourites grid moved     something above the zone is scrolling, so
//                                 the split is not being applied
//   sbroot/sidebar/pane moved     a container that should be overflow:hidden
//                                 is scrolling
//   window/document moved         the page itself is scrolling under the app
//
// Dispatches no input of its own - the gesture has to be a real one.
import {visibleSidebar, wait} from './sidebar-probe-lib.mjs';

const SECONDS = Number(process.env.SCROLL_SECONDS || 8);
const PORT = process.env.ATRIUM_CDP_PORT || '9333';

let sidebar;
try {
  sidebar = await visibleSidebar();
} catch (err) {
  console.error(`Could not reach a sidebar surface on :${PORT}.`);
  console.error("  make run FLAGS='--remote-debugging-port=9333'");
  console.error(`  (${err.message})`);
  process.exit(2);
}
const {ev, ws} = sidebar;

const SNAP = `(() => {
  const q = (s) => document.querySelector(s);
  const top = (s) => {
    const el = q(s);
    return el ? Math.round(el.getBoundingClientRect().top) : null;
  };
  const pane = q('.railpane') || document;
  return {
    winY: Math.round(window.scrollY),
    docEl: document.scrollingElement ? document.scrollingElement.scrollTop : -1,
    body: document.body.scrollTop,
    sbroot: q('.sbroot') ? q('.sbroot').scrollTop : null,
    rail: q('.rail') ? q('.rail').scrollTop : null,
    pane: q('.railpane') ? q('.railpane').scrollTop : null,
    sidebar: pane.querySelector('.sidebar') ? pane.querySelector('.sidebar').scrollTop : null,
    zone: pane.querySelector('.scrollzone') ? pane.querySelector('.scrollzone').scrollTop : null,
    // Screen positions: what a person actually sees move.
    favTop: top('.railpane .favgrid'),
    nameTop: top('.railpane .wsname'),
    dotsTop: top('.railpane .spacedots'),
    zoneTop: top('.railpane .scrollzone'),
    hasZone: !!pane.querySelector('.scrollzone'),
  };
})()`;

const first = await ev(SNAP);
if (!first.hasZone) {
  console.log('This build has NO .scrollzone - it predates the fix. Rebuild first.\n');
}
console.log(`Scroll the sidebar up and down for ${SECONDS}s. Nothing is dispatched from here.\n`);

const seen = {};
const track = (snap) => {
  for (const [k, v] of Object.entries(snap)) {
    if (typeof v !== 'number') { continue; }
    const s = seen[k] || (seen[k] = {min: v, max: v});
    s.min = Math.min(s.min, v);
    s.max = Math.max(s.max, v);
  }
};
track(first);

const started = Date.now();
while (Date.now() - started < SECONDS * 1000) {
  await wait(100);
  try {
    track(await ev(SNAP));
  } catch {
    break;
  }
}

const moved = (k) => (seen[k] ? seen[k].max - seen[k].min : 0);
console.log('what moved, and by how much:\n');
const rows = [
  ['zone.scrollTop', 'the scroll zone - INTENDED', moved('zone')],
  ['favgrid top', 'favourites on screen - must NOT move', moved('favTop')],
  ['wsname top', 'space name on screen - must NOT move', moved('nameTop')],
  ['spacedots top', 'space dots on screen - must NOT move', moved('dotsTop')],
  ['scrollzone top', 'the zone itself on screen - must NOT move', moved('zoneTop')],
  ['sbroot.scrollTop', 'root container', moved('sbroot')],
  ['sidebar.scrollTop', 'pane container', moved('sidebar')],
  ['railpane.scrollTop', 'rail pane', moved('pane')],
  ['document/window', 'the page itself', Math.max(moved('winY'), moved('docEl'), moved('body'))],
];
for (const [name, what, px] of rows) {
  console.log(`  ${String(px + 'px').padStart(8)}  ${name.padEnd(20)} ${what}`);
}

const bad = rows.filter(([n, , px]) => px > 2 && n !== 'zone.scrollTop');
console.log();
if (!moved('zone') && !bad.length) {
  console.log('Nothing moved at all - was the pointer over the sidebar, and is the');
  console.log('list long enough to overflow?');
} else if (!bad.length) {
  console.log('Correct: only the scroll zone moved. Favourites, the space name and');
  console.log('the dots stayed put.');
} else {
  console.log('WRONG - these moved and should not have:');
  for (const [n, what, px] of bad) { console.log(`  ${n} (${what}) by ${px}px`); }
}
ws.close();
process.exit(0);
