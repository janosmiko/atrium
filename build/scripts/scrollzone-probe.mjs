#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// only the tab sections scroll, no scrollbar is drawn, and a swipe
// locks scrolling for its duration.
//
//   ./build/scripts/vm-cdp.sh && node build/scripts/scrollzone-probe.mjs
//
// Needs a space with enough tabs to overflow the pane - 12+ - or the scroll
// assertions pass for the wrong reason (nothing to scroll).
//
// The wheel events here go through Input.dispatchMouseEvent's sibling
// Input.dispatchMouseEvent(type: mouseWheel), which IS the browser's own input
// pipeline, so the swipe machine sees what a trackpad would. What it cannot
// reproduce is the momentum phase - see build/scripts/swipe-machine-test.mjs's
// REAL_FLICK for that half, captured from real hardware.

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
// that are never shown; the biggest is the one on screen.
let best = null;
for (const s of surfaces) {
  const c = await connect(s);
  const size = await c.ev('window.outerWidth * window.outerHeight');
  if (!best || size > best.size) { best?.c.ws.close(); best = {c, size}; }
  else { c.ws.close(); }
}
const {ev, send} = best.c;
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

let failures = 0;
const check = (label, actual, expected) => {
  const ok = JSON.stringify(actual) === JSON.stringify(expected);
  if (!ok) { failures++; }
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}` +
      (ok ? '' : `\n       expected ${JSON.stringify(expected)}` +
                 `\n       actual   ${JSON.stringify(actual)}`));
};

// [data-space-index] skips the rowless .ovrailpane, which a bare
// '.railpane' matches first.
const PANE = `(document.querySelector('.railpane[data-space-index]') || document)`;

const shape = await ev(`(() => {
  const pane = ${PANE};
  const zone = pane.querySelector('.scrollzone');
  if (!zone) { return null; }
  return {
    zoneKids: [...zone.children].map((n) => n.className),
    favInside: zone.contains(pane.querySelector('.favgrid')),
    dotsInside: !!pane.querySelector('.spacedots') &&
                zone.contains(pane.querySelector('.spacedots')),
    overflows: zone.scrollHeight > zone.clientHeight + 4,
    scrollable: getComputedStyle(zone).overflowY,
  };
})()`);
if (!shape) {
  console.error('no .scrollzone in the rendered sidebar');
  best.c.ws.close();
  process.exit(1);
}
console.log(`scroll zone: ${JSON.stringify(shape.zoneKids)}\n`);

check('the favourites grid is outside the scroll zone', shape.favInside, false);
check('the space dots are outside it too', shape.dotsInside, false);
check('the zone is the scrolling element', shape.scrollable, 'auto');
check('the fixture actually overflows, so scrolling is under test',
      shape.overflows, true);

// The bar is chrome, not content. Measured, not read off the stylesheet: a
// zero-width scrollbar is the only thing that proves the rule took effect.
const barWidth = await ev(`(() => {
  const z = ${PANE}.querySelector('.scrollzone');
  return z.offsetWidth - z.clientWidth;
})()`);
check('no scrollbar is drawn', barWidth, 0);

// A vertical wheel over the tab list must still scroll it.
const scrolled = await ev(`(() => {
  const z = ${PANE}.querySelector('.scrollzone');
  z.scrollTop = 0;
  return z.scrollTop;
})()`);
check('the zone starts at the top', scrolled, 0);

const box = await ev(`(() => {
  const z = ${PANE}.querySelector('.scrollzone');
  const r = z.getBoundingClientRect();
  return {x: r.left + r.width / 2, y: r.top + r.height / 2};
})()`);
for (let i = 0; i < 4; i++) {
  await send('Input.dispatchMouseEvent', {
    type: 'mouseWheel', x: box.x, y: box.y, deltaX: 0, deltaY: 60,
  });
  await wait(30);
}
await wait(300);
const afterScroll = await ev(
    `${PANE}.querySelector('.scrollzone').scrollTop`);
check('a vertical wheel still scrolls the tab list', afterScroll > 0, true);

// A horizontal flick must lock the zone while it runs. Sampled DURING the
// gesture: the lock is released the moment the machine idles, so reading it
// afterwards would always show it off.
await ev(`window.__lockSeen = false;`);
await ev(`(() => {
  const root = document.querySelector('.sbroot') || document.body;
  const tick = () => {
    if (root.classList.contains('swipelock')) { window.__lockSeen = true; }
    requestAnimationFrame(tick);
  };
  tick();
})()`);
for (let i = 0; i < 8; i++) {
  await send('Input.dispatchMouseEvent', {
    type: 'mouseWheel', x: box.x, y: box.y, deltaX: -40, deltaY: 0,
  });
  await wait(16);
}
await wait(200);
const lockSeen = await ev('window.__lockSeen');
check('a horizontal flick locks the scroll zone while it runs', lockSeen, true);

await wait(1200);
const lockCleared = await ev(
    `!(document.querySelector('.sbroot') || document.body).classList.contains('swipelock')`);
check('and the lock is released once the swipe settles', lockCleared, true);

best.c.ws.close();
console.log(`\n${failures ? failures + ' FAILED' : 'all checks passed'}`);
process.exit(failures ? 1 : 0);
