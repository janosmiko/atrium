#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// has the PAGE let go of the sidebar's trailing edge?

//   ./build/scripts/vm-cdp.sh && node build/scripts/resizer-input-probe.mjs

// The seam used to carry two handles and the band between them resized from
// neither. This is the page half of the fix: nothing in the surface claims the
// band, and a press inside it sends nothing.

// The native half is a VLOG - launch with --vmodule=atrium_browser_window=1 and
// read "Atrium: sidebar grip band" out of /tmp/atrium-run.log.
import {connect} from './sidebar-probe-lib.mjs';

const REACH = 4;  // kSidebarGripReach in atrium_sidebar_grip_bounds.h

const list = await (await fetch('http://127.0.0.1:9334/json/list')).json();
const surfaces = list.filter((x) => x.url.includes('top-chrome/sidebar'));
if (!surfaces.length) {
  console.log('FAIL: no sidebar surface - is Atrium running with --remote-debugging-port=9334?');
  process.exit(2);
}

const surface = await connect(surfaces[0]);
const {ws, send} = surface;
// A press inside the grip band can hold a pointer capture that never
// answers, so ev()'s timeout throw is caught and turned into a sentinel.
const TIMED_OUT = Symbol('timedOut');
const evalx = async (expr) => {
  try {
    return await surface.ev(expr);
  } catch (e) {
    if (/timed out/.test(e.message)) return TIMED_OUT;
    throw e;
  }
};

const geometry = await evalx(`(() => {
  const s = document.querySelector('.sidebar');
  if (!s) return null;
  const b = s.getBoundingClientRect();
  return {w: Math.round(b.width), h: Math.round(b.height)};
})()`);
if (!geometry || geometry === TIMED_OUT) {
  console.log('FAIL: no .sidebar in the surface - nothing below would mean anything');
  ws.close();
  process.exit(1);
}
const midY = Math.floor(geometry.h / 2);
// The band's page-side half, walked one pixel at a time. The surface has no
// notion of which edge it is docked on - the seam is native - so this is always
// its trailing edge.
const columns = [];
for (let i = 0; i < REACH; ++i) {
  columns.push(geometry.w - REACH + i);
}

const hits = await evalx(`(() => {
  const xs = ${JSON.stringify(columns)};
  return xs.map((x) => {
    const e = document.elementFromPoint(x, ${midY});
    return e ? (e.className || e.tagName) : 'none';
  });
})()`);
if (hits === TIMED_OUT || !Array.isArray(hits)) {
  console.log('FAIL: the surface stopped answering evaluates - no verdict');
  ws.close();
  process.exit(1);
}
console.log(`sidebar ${geometry.w}x${geometry.h}${geometry.onRight ? ' (docked right)' : ''}`);
for (let i = 0; i < columns.length; ++i) {
  console.log(`  x=${columns[i]}  ${hits[i]}`);
}

const stillThere = await evalx(`document.querySelectorAll('.resizer').length`);
if (stillThere) {
  console.log(`FAIL: ${stillThere} .resizer element(s) still in the page. Two handles on ` +
              'one seam is the defect, whatever each of them does on its own.');
  ws.close();
  process.exit(1);
}

// A press the page would have claimed before. It must produce no message now:
// the grip is a view, and views never reach a renderer.
const armed = await evalx(`
  window.__probe = [];
  const original = chrome.send;
  chrome.send = (name, args) => { window.__probe.push([name, args]); return original(name, args); };
  typeof chrome.send === 'function' ? 'ok' : 'no-chrome-send'`);
if (armed !== 'ok') {
  console.log(`FAIL: could not instrument the page (armed=${JSON.stringify(armed)}), so a ` +
              'silent result below would prove nothing');
  ws.close();
  process.exit(1);
}

const pressX = columns[Math.floor(columns.length / 2)];
await send('Input.dispatchMouseEvent',
           {type: 'mousePressed', x: pressX, y: midY, button: 'left', buttons: 1, clickCount: 1});
for (const dx of [20, 50, 84]) {
  await send('Input.dispatchMouseEvent',
             {type: 'mouseMoved', x: pressX + dx, y: midY, button: 'left', buttons: 1});
}
await send('Input.dispatchMouseEvent',
           {type: 'mouseReleased', x: pressX + 84, y: midY, button: 'left', buttons: 0, clickCount: 1});

const sent = await evalx('window.__probe ? JSON.stringify(window.__probe) : null');
ws.close();
if (sent === TIMED_OUT || sent === null) {
  console.log('FAIL: the instrumentation vanished mid-probe - the surface reloaded, so nothing ' +
              'observed here is about the press');
  process.exit(1);
}
const widthMessages = JSON.parse(sent).filter(([name]) => name.startsWith('sidebarWidth'));
if (widthMessages.length) {
  console.log(`FAIL: the page still drives the width: ${sent}`);
  process.exit(1);
}
console.log(`PASS: no .resizer, and a press at x=${pressX} sent no width message ` +
            `(page traffic: ${sent}). Native half: grep "sidebar grip band" in the run log.`);
