#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the tab row context menu, driven by REAL browser input.
//
//   ./build/scripts/vm-cdp.sh && node build/scripts/menu-probe.mjs
//
// Right-click arrives via Input.dispatchMouseEvent and the menu item is clicked
// the same way, so this exercises the path a hand does rather than dispatching
// synthetic events the browser never produced.
//
// Needs at least one regular tab row; it pins one, so the fixture is consumed.
import {connect} from './sidebar-probe-lib.mjs';

const t = await (await fetch('http://127.0.0.1:9334/json/list')).json();
let best = null;
for (const s of t.filter((x) => x.url.includes('top-chrome/sidebar'))) {
  const {ws, send, ev} = await connect(s);
  if (await ev('document.visibilityState === "visible"') && !best) {
    best = {send, ev, ws};
  } else {
    ws.close();
  }
}
const {send, ev} = best;
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
let failures = 0;
const check = (label, actual, expected) => {
  const ok = JSON.stringify(actual) === JSON.stringify(expected);
  if (!ok) { failures++; }
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}` +
      (ok ? '' : `\n       expected ${JSON.stringify(expected)}\n       actual   ${JSON.stringify(actual)}`));
};
// [data-space-index] skips the rowless .ovrailpane, which a bare
// '.railpane' matches first.
const PANE = `(document.querySelector('.railpane[data-space-index]') || document.querySelector('.sidebar'))`;

await ev(`window.__sent = [];
  if (!chrome.send.__w) {
    const real = chrome.send.bind(chrome);
    chrome.send = (n, a) => { window.__sent.push([n, a]); return real(n, a); };
    chrome.send.__w = true;
  }`);

const rightClick = async (selector) => {
  const box = await ev(`(() => {
    const el = ${PANE}.querySelector('${selector}');
    if (!el) { return null; }
    const b = el.getBoundingClientRect();
    return {x: b.left + b.width / 2, y: b.top + b.height / 2};
  })()`);
  if (!box) { return null; }
  await send('Input.dispatchMouseEvent',
      {type: 'mousePressed', x: box.x, y: box.y, button: 'right', clickCount: 1});
  await send('Input.dispatchMouseEvent',
      {type: 'mouseReleased', x: box.x, y: box.y, button: 'right', clickCount: 1});
  await wait(400);
  return ev(`[...document.querySelectorAll('.ctxmenu .ctxitem')].map((b) => b.textContent)`);
};

// The menu has grown well past Pin/Close (rename, snooze,
// archive...), and the Pin label now carries its keycap ("Pin Tab⌘D") - so
// this checks for the two actions this probe drives, not the full menu.
const regularItems = await rightClick('[data-dropzone="tabs"] > .tabrow');
check('a regular row offers a Pin action',
    regularItems?.some((t) => t.startsWith('Pin Tab')), true);
check('a regular row offers Close', regularItems?.includes('Close Tab'), true);
check('the menu is inside the surface', await ev(`(() => {
  const m = document.querySelector('.ctxmenu');
  const r = document.querySelector('#atrium-root').getBoundingClientRect();
  const b = m.getBoundingClientRect();
  return b.left >= r.left - 1 && b.right <= r.right + 1;
})()`), true);

// Click Pin with real input.
const pin = await ev(`(() => {
  const b = [...document.querySelectorAll('.ctxmenu .ctxitem')]
      .find((x) => x.textContent.startsWith('Pin Tab')).getBoundingClientRect();
  window.__sent = [];
  return {x: b.left + b.width / 2, y: b.top + b.height / 2};
})()`);
await send('Input.dispatchMouseEvent', {type: 'mousePressed', x: pin.x, y: pin.y, button: 'left', clickCount: 1});
await send('Input.dispatchMouseEvent', {type: 'mouseReleased', x: pin.x, y: pin.y, button: 'left', clickCount: 1});
await wait(600);
check('choosing Pin sends the pin message',
    (await ev('window.__sent')).filter((m) => m[0] === 'sidebarTabPinned'),
    [['sidebarTabPinned', [0, 0]]]);
check('the menu closed', await ev(`document.querySelectorAll('.ctxmenu').length`), 0);

await wait(2500);
const pinnedItems = await rightClick('[data-dropzone="pinned"] > .tabrow');
check('a pinned row offers an Unpin action',
    pinnedItems?.some((t) => t.startsWith('Unpin Tab')), true);
check('a pinned row offers Close', pinnedItems?.includes('Close Tab'), true);
await ev(`document.querySelectorAll('.ctxmenu').forEach((m) => m.remove())`);

best.ws.close();
console.log(`\n${failures ? failures + ' FAILED' : 'all checks passed'}`);
process.exit(failures ? 1 : 0);
