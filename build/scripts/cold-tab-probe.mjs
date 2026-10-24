#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// clicking a COLD sidebar row reopens its tab.
//
//   ./build/scripts/vm-cdp.sh && node build/scripts/cold-tab-probe.mjs
//
// A cold row is a persisted tab with no live counterpart - the tab was
// discarded, or the session restored without it. It has an empty id, so
// ActivateSidebarTab has nothing to match and the row was silently inert:
// the user saw a tab they could not click, while every other row worked.

import {connect} from './sidebar-probe-lib.mjs';

const PORT = process.env.ATRIUM_CDP_PORT || '9334';
const BASE = `http://127.0.0.1:${PORT}`;

const list = async () => (await (await fetch(`${BASE}/json/list`)).json());
const targets = await list();
const target = targets.find((t) => t.url.includes('top-chrome/sidebar'));
if (!target) {
  console.error(`no sidebar target on :${PORT}`);
  process.exit(2);
}

const {ws, send, ev} = await connect(target);
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

let failures = 0;
const check = (label, actual, expected) => {
  const ok = JSON.stringify(actual) === JSON.stringify(expected);
  if (!ok) { failures++; }
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}` +
      (ok ? '' : `\n       expected ${JSON.stringify(expected)}` +
                 `\n       actual   ${JSON.stringify(actual)}`));
};

await send('Runtime.enable');
// Matched on the dropzone rather than on a wrapper class, because that IS the
// condition sidebar.js's cold branch tests (`parentElement.dataset.dropzone ===
// 'tabs'`) - so the probe selects exactly the rows the code under test would
// route, and cannot drift from it again. The previous selector named a
// `.railpane` ancestor that no longer wraps these rows; it matched nothing and
// the probe exited with "fixture has no tab rows at all", which reads like a
// broken fixture rather than a stale selector.
const ROWS = `[...document.querySelectorAll('[data-dropzone="tabs"] > .tabrow')]`;

const rows = await ev(`${ROWS}.map((r) => ({id: r.dataset.tabId, url: r.dataset.url,
                                            title: r.querySelector('.tl').textContent}))`);
console.log(`rows: ${JSON.stringify(rows)}\n`);

let cold = rows.findIndex((r) => !r.id);
if (cold < 0) {
  // A NATURAL cold row needs a discarded tab, which this harness cannot
  // manufacture: restoring a space opens its persisted tabs, and closing one
  // removes the persisted entry with it. So the cold STATE is synthesised on a
  // real row - what is under test is the surface's routing and the browser's
  // handling of it, both of which only look at the id being absent.
  if (!rows.length) {
    console.error('fixture has no tab rows at all');
    process.exit(2);
  }
  console.log('(no natural cold row; clearing one row\'s id to synthesise it)\n');
  await ev(`${ROWS}[0].dataset.tabId = ''`);
  cold = 0;
  rows[0].id = '';
}

check('a cold row still carries its URL', !!rows[cold].url, true);

await ev(`window.__sent = [];
  if (!chrome.send.__wrapped) {
    const real = chrome.send.bind(chrome);
    chrome.send = (n, a) => { window.__sent.push([n, a]); return real(n, a); };
    chrome.send.__wrapped = true;
  }`);

const before = (await list()).filter((t) => t.type === 'page').length;
console.log(`live page targets before: ${before}`);

await ev(`${ROWS}[${cold}].click()`);
const sent = await ev(`window.__sent`);
// Presence, not sole occupant: a re-render emits sidebarFavoriteCount too, and
// asserting the exact list makes this fail for a reason that has nothing to do
// with the cold row.
check('clicking a cold row asks for its URL, not an empty id',
    sent.filter((m) => m[0] === 'sidebarColdTabClicked'),
    [['sidebarColdTabClicked', [rows[cold].url]]]);
check('it does not also send an empty-id activate',
    sent.some((m) => m[0] === 'sidebarTabClicked'), false);

await wait(4000);
const after = (await list()).filter((t) => t.type === 'page');
console.log(`live page targets after : ${after.length}`);
check('a tab was opened', after.length > before, true);
check('it opened the row\'s own URL',
    after.some((t) => t.url.startsWith(rows[cold].url.replace(/\/$/, ''))), true);

// The URL crosses from the renderer, so it is attacker-controlled in the same
// way a page title is. Anything outside http/https/about must be dropped.
const pagesBefore = (await list()).filter((t) => t.type === 'page').length;
await ev(`chrome.send('sidebarColdTabClicked', ['javascript:void(document.title="pwned")'])`);
await wait(2500);
const pagesAfter = (await list()).filter((t) => t.type === 'page').length;
check('a javascript: URL is refused', pagesAfter, pagesBefore);

ws.close();
console.log(`\n${failures ? failures + ' FAILED' : 'all checks passed'}`);
process.exit(failures ? 1 : 0);
