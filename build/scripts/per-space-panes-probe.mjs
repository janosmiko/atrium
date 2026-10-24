#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// each rail pane renders its own space, not the active one retinted.
//
//   ./build/scripts/vm-cdp.sh && node build/scripts/per-space-panes-probe.mjs
//
// Needs two spaces with DIFFERENT tabs. This is the swipe-lag check: while
// every pane drew the active space's rows, swiping showed the outgoing space's
// icons until a rebuild landed after the animation.
import {visibleSidebar, checker} from './sidebar-probe-lib.mjs';

const {ev, ws} = await visibleSidebar();
const {check, done} = checker();

// :not(.ovrailpane) excludes the overview pane ahead of the
// per-space ones - it renders no space of its own by design.
const panes = await ev(`[...document.querySelectorAll('.railpane:not(.ovrailpane)')].map((p) => ({
  space: p.dataset.spaceIndex,
  name: (p.querySelector('.wsname') || {}).textContent,
  rows: [...p.querySelectorAll('.rows > .tabrow .tl')].map((n) => n.textContent || '(live)'),
}))`);
if (panes.length < 2) {
  console.error(`needs a rail with 2+ spaces, found ${panes.length}`);
  process.exit(2);
}
for (const p of panes) {
  console.log(`pane ${p.space} (${p.name}): ${JSON.stringify(p.rows)}`);
}

check('every pane declares its space',
    panes.every((p) => p.space !== undefined), true);
check('panes render different rows',
    new Set(panes.map((p) => JSON.stringify(p.rows))).size > 1, true);

ws.close();
done();
