#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// a favourite-bound active tab marks its TILE.
//
//   ./build/scripts/vm-cdp.sh && node build/scripts/favourite-selection-probe.mjs
//
// Needs the window's active tab to be at a favourite's URL. Tiles are keyed by
// URL rather than a live tab id, and a favourite-bound tab is never persisted
// into space.tabs - so before this, nothing anywhere in the sidebar was marked
// whenever such a tab was active.
import {visibleSidebar, checker, PANE} from './sidebar-probe-lib.mjs';

const {ev, ws} = await visibleSidebar();
const {check, done} = checker();

// Not scoped to PANE: the favourites grid sits beside the rail, a
// sibling of every space's pane rather than a descendant of one.
const tiles = await ev(
    `[...document.querySelectorAll('.favgrid .fav')].map((f) => ({id: f.dataset.favId, sel: f.classList.contains('sel')}))`);
if (!tiles.length) {
  console.error('fixture needs at least one favourite');
  process.exit(2);
}
console.log(`tiles: ${JSON.stringify(tiles)}`);

const marked = tiles.filter((t) => t.sel);
check('exactly one tile is marked', marked.length, 1);
check('something in the sidebar is marked at all',
    marked.length > 0 ||
    (await ev(`${PANE}.querySelectorAll('.tabrow.sel').length`)) > 0, true);

ws.close();
done();
