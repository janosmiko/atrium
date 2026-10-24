#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// drags a sidebar row with REAL browser input and reports which drag
// events actually fire.
//
//   ./build/scripts/vm-cdp.sh && node build/scripts/drag-input-probe.mjs
//
// Input.dispatchMouseEvent goes through the browser's own input pipeline, so
// this exercises the path a hand does. Every other drag probe in this repo
// synthesises DragEvents with dispatchEvent, which bypasses the browser
// entirely - they all pass while the feature has never worked once.
//
// Passes when the drag survives dragstart and a drop target engages
// (dragover). A completed drop is NOT asserted: CDP mouse events stop driving
// a native drag after the first move in this surface, and drag interception
// does not deliver dragover either.
//
// That is still the check worth having. A layout reflow at
// dragstart once cancelled the drag outright - dragstart, dragend, nothing
// between - and every synthetic-DragEvent probe in this repo passed straight
// through it, because they dispatch dragover themselves.
import {connect} from './sidebar-probe-lib.mjs';

const t = await (await fetch('http://127.0.0.1:9334/json/list')).json();
const surfaces = t.filter((x) => x.url.includes('top-chrome/sidebar'));
if (!surfaces.length) { console.log('no sidebar surface'); process.exit(2); }

let best = null;
for (const s of surfaces) {
  const c = await connect(s);
  const v = await c.ev('document.visibilityState + "|" + document.documentElement.clientWidth');
  if (String(v).startsWith('visible') && !best) { best = c; } else { c.ws.close(); }
}
if (!best) { console.log('no visible sidebar surface'); process.exit(2); }
const {send, ev, ws} = best;
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

await ev(`window.__log = [];
  for (const t of ['dragstart','dragover','drop','dragend']) {
    document.addEventListener(t, () => {
      if (window.__log[window.__log.length-1] !== t) { window.__log.push(t); }
    }, true);
  }`);

const box = await ev(`(() => {
  // The VISIBLE pane. Pane 0 sits off-screen once the rail is translated, and
  // dispatching real mouse input at its coordinates lands outside the viewport.
  const panes = [...document.querySelectorAll('.railpane')];
  const rail = document.querySelector('.rail');
  let pane = panes[0];
  if (rail && panes.length) {
    const m = /translateX\\((-?[\\d.]+)px\\)/.exec(rail.style.transform || '');
    const w = panes[0].getBoundingClientRect().width || 1;
    const i = m ? Math.round(-Number(m[1]) / w) : 0;
    pane = panes[Math.min(Math.max(i, 0), panes.length - 1)] || panes[0];
  }
  const rows = [...(pane || document).querySelectorAll('.rows > .tabrow')];
  if (rows.length < 2) { return null; }
  const a = rows[0].getBoundingClientRect(), b = rows[rows.length-1].getBoundingClientRect();
  return {ax: a.x + a.width/2, ay: a.y + a.height/2,
          zx: b.x + b.width/2, zy: b.y + b.height/2};
})()`);
if (!box) { console.log('need >=2 tab rows in the VM fixture'); process.exit(2); }

await send('Input.dispatchMouseEvent',
    {type: 'mousePressed', x: box.ax, y: box.ay, button: 'left', clickCount: 1, buttons: 1});
for (let i = 1; i <= 8; i++) {
  await send('Input.dispatchMouseEvent', {type: 'mouseMoved', button: 'left', buttons: 1,
    x: box.ax + (box.zx - box.ax) * i / 8,
    y: box.ay + (box.zy - box.ay) * i / 8});
  await wait(120);
}
await send('Input.dispatchMouseEvent',
    {type: 'mouseReleased', x: box.zx, y: box.zy, button: 'left', buttons: 0});
await wait(1500);

const events = await ev('window.__log');
console.log('sidebar surface, real input ->', JSON.stringify(events));

// PASS CONDITION: the drag must survive dragstart and reach dragover.
//
// Not "must reach drop", deliberately. CDP mouse events stop driving a native
// drag after its first move in this surface, and Input.setInterceptDrags plus
// Input.dispatchDragEvent did not deliver dragover either - so a completed drop
// is not reachable from here and asserting it would make this permanently red.
//
// dragover is the signal that matters anyway. It is exactly what a past reflow bug broke:
// a layout reflow at dragstart cancelled the drag, giving dragstart then dragend
// with nothing between and no drop target ever engaging. Any regression of that
// class fails this check.
const survived = events.includes('dragover');
console.log(survived
    ? 'PASS: the drag survives dragstart and a drop target engages'
    : 'FAIL: dragstart is followed straight by dragend - the drag is cancelled');
best.ws.close();
process.exit(survived ? 0 : 1);
