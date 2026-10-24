#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// KEPT MANUAL PROBE. Not in `make webui-probe`: it needs favourites
// staged in the profile, and on a bare probe profile it reports 5 failed
// assertions. That is the missing fixture, not a regression - checked on
// 2026-08-19 before this note was written. Wire it once a fixture stages them.
// a favourite is its own tab, asserted end to end.
//
//   ./build/scripts/vm.sh run-atrium \
//       --remote-debugging-port=9334
//   ./build/scripts/vm-cdp.sh
//   node build/scripts/favourite-tab-probe.mjs
//
// The favourite this clicks REDIRECTS, which is the case a URL comparison
// cannot answer and the one that made every click stack another tab.
import {connect} from './sidebar-probe-lib.mjs';

import {execFileSync} from 'node:child_process';
import os from 'node:os';
import path from 'node:path';

const PORT = process.env.ATRIUM_CDP_PORT || '9334';
const KEY = path.join(process.env.TART_HOME || path.join(os.homedir(), '.tart'),
    'atrium-probe-key');
const FAVOURITE = 'http://127.0.0.1:8000/redirect-to-lab.html';

let failures = 0;
function check(ok, what, detail) {
  console.log(`${ok ? '  ok  ' : '  FAIL'}  ${what}${detail ? `  (${detail})` : ''}`);
  if (!ok) {
    failures++;
  }
}

const list = await (async () => {
  try {
    return await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json();
  } catch {
    console.error(`No debug port on ${PORT}. Run ./build/scripts/vm-cdp.sh first.`);
    process.exit(1);
  }
})();

const sidebar = list.find((t) => t.url.includes('top-chrome/sidebar'));
if (!sidebar) {
  console.error('No HTML sidebar found.');
  process.exit(1);
}

const {ws, ev} = await connect(sidebar);
const settle = (ms) => new Promise((r) => setTimeout(r, ms));

const state = () => ev(`(() => {
  const tile = [...document.querySelectorAll('.fav')]
      .find((f) => f.dataset.favId === ${JSON.stringify(FAVOURITE)});
  return {
    found: !!tile,
    selected: !!tile && tile.classList.contains('sel'),
    rows: [...document.querySelectorAll('.rows > .tabrow .tl')]
        .map((n) => n.textContent),
  };
})()`);

const clickTile = () => ev(`(() => {
  const tile = [...document.querySelectorAll('.fav')]
      .find((f) => f.dataset.favId === ${JSON.stringify(FAVOURITE)});
  if (!tile) return false;
  tile.dispatchEvent(new MouseEvent('click', {bubbles: true}));
  return true;
})()`);

console.log('\nfavourite tab probe');
const before = await state();
check(before.found, 'the redirecting favourite has a tile', FAVOURITE);
check(!before.rows.some((t) => /Redirecting/.test(t)),
      'and no tab row for it before anything is clicked',
      before.rows.join(' | ') || 'no rows');

check(await clickTile(), 'the tile took a click');
await settle(4000);
const first = await state();
check(!first.rows.some((t) => /Redirecting/.test(t)),
      'the favourite it opened is still not a tab row',
      first.rows.join(' | ') || 'no rows');
// The tile has to light up, or the page is open with nothing on screen saying
// so - which is what a URL comparison gave once the redirect had landed.
check(first.selected, 'the tile shows it is the page in front');

const rowsAfterFirst = first.rows.length;
check(await clickTile(), 'the tile took a second click');
await settle(4000);
const second = await state();
check(second.rows.length === rowsAfterFirst,
      'the second click added no row',
      `${rowsAfterFirst} then ${second.rows.length}`);
check(second.selected, 'and the tile is still the page in front');

ws.close();
console.log(failures ? `\n${failures} assertion(s) failed` : '\nall assertions passed');
process.exit(failures ? 1 : 0);
