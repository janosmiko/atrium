#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Measures the HTML sidebar's swipe frame timings.
//
// DevTools cannot be opened on this surface - it lives in the reserved chrome
// context, which user profiles are blocked from reaching. So
// the measurement is driven over the debug port instead.
//
//   make run FLAGS='--remote-debugging-port=9333'
//   make swipe-bench
//
// Reports the median frame interval. Compare it against your display's refresh:
// 120Hz wants 8.3ms, 60Hz wants 16.7ms. A median at double the target means
// every second frame is being missed.

import {connect} from './sidebar-probe-lib.mjs';

const PORT = process.env.ATRIUM_CDP_PORT || '9333';
const base = `http://127.0.0.1:${PORT}`;

let list;
try {
  list = await (await fetch(`${base}/json/list`)).json();
} catch {
  console.error(`No debug port on ${PORT}. Launch with:\n` +
    `  make run FLAGS='--remote-debugging-port=${PORT}'`);
  process.exit(1);
}

const targets = list.filter((t) => t.url.includes('top-chrome/sidebar'));
if (!targets.length) {
  console.error('No HTML sidebar found.');
  process.exit(1);
}

const SPACES = [
  {name: 'Work', color: '#5b8def'}, {name: 'Personal', color: '#e1567c'},
  {name: 'Side', color: '#36c5f0'}, {name: 'Reading', color: '#a259ff'},
];

for (const target of targets) {
  const {ws, send} = await connect(target);

  // Keep whatever real tabs are already showing; just add spaces so the rail
  // is built. Measuring the real content is the point.
  const ready = await send('Runtime.evaluate', {
    expression: `(() => {
      const spaces = ${JSON.stringify(SPACES)};
      const rows = [...document.querySelectorAll('.tabrow')].map((r) => ({
        id: r.dataset.tabId, title: r.querySelector('.tl').textContent,
        color: getComputedStyle(r).getPropertyValue('--favc'),
      }));
      window.atriumSetSidebar({spaceName: 'Work', spaceTint: '#5b8def',
        tabs: rows, spaces, activeSpace: 0});
      return document.querySelectorAll('.railpane').length;
    })()`, returnByValue: true});
  if (!ready.result?.result?.value) { ws.close(); continue; }

  const runs = [];
  for (let i = 1; i <= 4; i++) {
    await send('Runtime.evaluate', {expression: `window.atriumSwipeTo(${i % 4})`});
    await new Promise((r) => setTimeout(r, 1600));
    const stats = (await send('Runtime.evaluate',
      {expression: 'window.__frameStats', returnByValue: true})).result.result.value;
    if (stats) runs.push(stats);
  }
  if (!runs.length) { ws.close(); continue; }

  console.log(`\nSwipe frame intervals (${runs.length} swipes):`);
  runs.forEach((r, i) => console.log(
    `  swipe ${i + 1}:  median ${r.median.toFixed(1)}ms   p95 ${r.p95.toFixed(1)}ms   worst ${r.worst.toFixed(1)}ms`));
  const median = runs.reduce((a, r) => a + r.median, 0) / runs.length;
  console.log(`\n  mean of medians: ${median.toFixed(1)}ms  (~${Math.round(1000 / median)}fps)`);
  // Judged against the ACTUAL display rate, not a guess. Hardcoding 120Hz
  // here once produced a verdict line that was simply wrong on a 144Hz panel.
  const hz = Number(process.env.ATRIUM_DISPLAY_HZ || 0);
  if (!hz) {
    console.log('  -> set ATRIUM_DISPLAY_HZ=<your refresh rate> for a verdict');
    console.log('     (system_profiler SPDisplaysDataType | grep Hz)');
  } else {
    const target = 1000 / hz;
    const ratio = median / target;
    console.log(`  target at ${hz}Hz is ${target.toFixed(1)}ms -> ratio ${ratio.toFixed(2)}x`);
    console.log(ratio < 1.3 ? '  -> presenting on EVERY frame'
      : ratio < 2.4 ? '  -> presenting on every SECOND frame (half rate)'
      : '  -> worse than half rate');
  }
  ws.close();

  // CONTROL: the same rAF probe in an ORDINARY tab. Without it, "half rate"
  // could just as easily be the whole browser or the display, and the number
  // above would be blamed on the chrome surface for no reason.
  const page = list.find((t) => t.type === 'page' && /^https?:/.test(t.url));
  if (!page) {
    console.log('\n  (no ordinary http tab open - open one for the control)');
    process.exit(0);
  }
  const {ws: cws, send: csend} = await connect(page);
  // Only frame timings are read - no page content is touched.
  await csend('Runtime.evaluate', {expression: `
    (() => { window.__ctl = null; const t = []; let l = performance.now();
      const f = (n) => { t.push(n - l); l = n;
        if (t.length < 60) requestAnimationFrame(f);
        else { const s = [...t].sort((a, b) => a - b);
               window.__ctl = s[Math.floor(s.length / 2)]; } };
      requestAnimationFrame(f); })()`});
  await new Promise((r) => setTimeout(r, 1800));
  const ctl = (await csend('Runtime.evaluate',
    {expression: 'window.__ctl', returnByValue: true})).result.result.value;
  cws.close();
  if (ctl) {
    console.log(`\n  CONTROL - ordinary tab (${new URL(page.url).host}): median ${ctl.toFixed(1)}ms`);
    const ratio = median / ctl;
    console.log(ratio > 1.5
      ? '  -> the chrome surface is SLOWER than a normal tab: specific to us'
      : '  -> a normal tab runs at the same rate: the cap is browser/display-wide, not ours');
  }
  process.exit(0);
}
console.error('No sidebar rendered a rail.');
process.exit(1);
