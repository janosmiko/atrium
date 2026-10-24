#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// KEPT MANUAL PROBE. Not in `make webui-probe`, because it needs two
// spaces on DIFFERENT profiles in one slot - a state the probe VM snapshot does
// not carry and no scenario fixture stages yet. Run it by hand after arranging
// that; wire it the day a fixture can produce it.
// the sidebar must act on the Browser whose rows it is showing.
//
//   ./build/scripts/vm-cdp.sh && node build/scripts/cross-profile-probe.mjs
//
// Needs two spaces on DIFFERENT profiles in one slot, and a real window
// (ATRIUM_STARTUP_WINDOW=1 plus appState.windowSpaces naming a space).
//
// Switching to a space on another profile takes the kInWindowSwap branch: the
// presenter's window stays on screen and the other controller's tab is swapped
// into its pane. From then on presenter != visible. The sidebar renders the
// VISIBLE space's rows while selection and activation used to resolve against
// the PRESENTER's Browser - so no row is ever marked, and clicking a live row
// scans the wrong tab strip and silently does nothing.

import {connect} from './sidebar-probe-lib.mjs';

const PORT = process.env.ATRIUM_CDP_PORT || '9334';
const BASE = `http://127.0.0.1:${PORT}`;

const list = async () => (await (await fetch(`${BASE}/json/list`)).json());

// Pick the biggest window: every AtriumBrowserWindow builds a surface,
// including per-profile windows that are never shown.
const surfaces = (await list()).filter((t) => t.url.includes('top-chrome/sidebar'));
if (!surfaces.length) {
  console.error(`no sidebar surface on :${PORT}`);
  process.exit(2);
}

let best = null;
for (const s of surfaces) {
  const c = await connect(s);
  const size = await c.ev('window.outerWidth * window.outerHeight');
  if (!best || size > best.size) {
    best?.c.ws.close();
    best = {c, size, target: s};
  } else {
    c.ws.close();
  }
}
const {ev} = best.c;
console.log(`${surfaces.length} surface(s); reading the largest (${best.size}px2)\n`);

let failures = 0;
const check = (label, actual, expected) => {
  const ok = JSON.stringify(actual) === JSON.stringify(expected);
  if (!ok) { failures++; }
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}` +
      (ok ? '' : `\n       expected ${JSON.stringify(expected)}` +
                 `\n       actual   ${JSON.stringify(actual)}`));
};
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

const PANE = `document.querySelector('.railpane[data-space-index="0"]') || document.querySelector('.railpane')`;
const rows = () => ev(`(() => {
  const pane = ${PANE};
  return [...pane.querySelectorAll('.rows > .tabrow')].map((r) => ({
    id: r.dataset.tabId, sel: r.classList.contains('sel'),
    title: r.querySelector('.tl').textContent,
  }));
})()`);
const spaceName = () => ev(`(${PANE}).querySelector('.wsname').textContent.trim()`);

console.log(`space on screen: ${await spaceName()}`);
const before = await rows();
console.log(`rows: ${JSON.stringify(before)}\n`);
check('the launch space marks its active row',
    before.some((r) => r.sel), true);

// Switch to the space on the OTHER profile. This is the cross-profile
// activation that leaves presenter != visible.
console.log('switching to space index 1 (a different profile)...');
await ev(`chrome.send('sidebarSpaceSelected', [1])`);
await wait(4000);

console.log(`space on screen: ${await spaceName()}`);
const after = await rows();
console.log(`rows: ${JSON.stringify(after)}\n`);

check('the sidebar now shows the other space\'s rows',
    JSON.stringify(after.map((r) => r.title)) !== JSON.stringify(before.map((r) => r.title)),
    true);
// THE assertion. Before the fix, selection resolved against the presenter's
// Browser, which does not own any of these tabs, so nothing was ever marked.
check('a row is still marked selected after the cross-profile switch',
    after.some((r) => r.sel), true);

const live = after.filter((r) => r.id);
const other = live.find((r) => !r.sel);
if (!other) {
  console.log('(only one live row after the switch - cannot test activation)');
} else {
  console.log(`clicking live row id=${other.id}, which is not the active one`);
  await ev(`[...(${PANE}).querySelectorAll('.rows > .tabrow')]` +
           `.find((r) => r.dataset.tabId === '${other.id}').click()`);
  await wait(3000);
  const clicked = await rows();
  check('clicking a live row activates it',
      clicked.find((r) => r.id === other.id)?.sel, true);
}

best.c.ws.close();
console.log(`\n${failures ? failures + ' FAILED' : 'all checks passed'}`);
process.exit(failures ? 1 : 0);
