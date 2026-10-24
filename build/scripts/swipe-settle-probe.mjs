#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// a committed swipe lands on ONE space and stays there.
//
//   ./build/scripts/vm-cdp.sh && node build/scripts/swipe-settle-probe.mjs
//
// Needs 3+ spaces. Sends a real wheel flick, then watches the rail long enough
// for the browser's space switch and its snapshot push to arrive.
//
// The failure this catches: attach() adopts the snapshot's active space on
// every push, and a push that predates the switch still names the OLD space -
// so the rail is dragged back, then forward again when the switch lands. On a
// fast flick that reads as creeping, an overshoot, or a jump two spaces on.
import {visibleSidebar, checker, wait} from './sidebar-probe-lib.mjs';

const {send, ev, ws} = await visibleSidebar();
const {check, done} = checker();

const panes = await ev(`document.querySelectorAll('.railpane').length`);
if (panes < 3) {
  console.error(`needs 3+ spaces, found ${panes}`);
  process.exit(2);
}

const railIndex = () => ev(`(() => {
  const rail = document.querySelector('.rail');
  const p = document.querySelector('.railpane');
  const m = /translateX\\((-?[\\d.]+)px\\)/.exec(rail.style.transform || '');
  const w = p.getBoundingClientRect().width || 1;
  return m ? Math.round(-Number(m[1]) / w * 100) / 100 : 0;
})()`);
// The VISIBLE pane's name. A document-wide .wsname query returns pane 0's,
// which is not where the rail is once it has moved.
const spaceName = () => ev(`(() => {
  const panes = [...document.querySelectorAll('.railpane')];
  const rail = document.querySelector('.rail');
  const m = /translateX\\((-?[\\d.]+)px\\)/.exec(rail.style.transform || '');
  const w = panes[0].getBoundingClientRect().width || 1;
  const i = m ? Math.round(-Number(m[1]) / w) : 0;
  const pane = panes[Math.min(Math.max(i, 0), panes.length - 1)];
  return (pane.querySelector('.wsname') || {}).textContent;
})()`);

console.log(`start: index ${await railIndex()} (${await spaceName()})`);

// A real wheel flick: several fast horizontal deltas, then nothing - the same
// shape as a trackpad swipe released mid-motion.
const box = await ev(`(() => {
  const b = document.querySelector('#atrium-root').getBoundingClientRect();
  return {x: b.left + b.width / 2, y: b.top + b.height / 2};
})()`);
for (let i = 0; i < 6; i++) {
  await send('Input.dispatchMouseEvent', {
    type: 'mouseWheel', x: box.x, y: box.y, deltaX: 42, deltaY: 0,
  });
  await wait(16);
}

// Let the settle finish, the browser switch, and its push arrive.
await wait(1500);
const settled = await railIndex();
console.log(`after settle: index ${settled} (${await spaceName()})`);

// Nothing may move after this point.
await wait(2500);
const later = await railIndex();
console.log(`2.5s later  : index ${later} (${await spaceName()})`);

check('the rail landed on a whole space', Number.isInteger(settled), true);
check('it advanced exactly one space', settled, 1);
check('and did not move again', later, settled);

// THE race, driven directly rather than waited for. Between the surface
// announcing a space and the browser switching, a push can still carry the OLD
// active space. Adopting it drags the rail back to the space the user just
// left - which is the creeping and overshoot. Feeding a stale snapshot by hand
// is the only way to hit that window on purpose.
await ev(`(() => {
  const snap = window.__lastSnapshot;
  if (!snap) { return false; }
  window.atriumSetSidebar({...snap, activeSpace: 0});
  return true;
})()`).catch(() => false);
await wait(900);
const afterStale = await railIndex();
console.log(`after a stale push naming space 0: index ${afterStale}`);
check('a stale snapshot does not drag the rail back', afterStale, settled);

ws.close();
done();
