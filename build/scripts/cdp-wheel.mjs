#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// a wheel over CDP into the page's widget host. The VM's cgevent
// scroll never became an NSEvent in the app on the runs that tried it.
//   node build/scripts/cdp-wheel.mjs <port> <target> <x> <y> <deltaY> [steps] [move]
// `move` sends mouseMoved steps 1px apart instead of a wheel, to tell a move
// AppKit never delivered from one the renderer dropped.

import {connect} from './sidebar-probe-lib.mjs';
import http from 'node:http';

const [port, match, xs, ys, dys, stepss, kind] = process.argv.slice(2);
const fail = (why) => { console.log(`BLIND ${why}`); process.exit(3); };
if (!port || !match || !xs || !ys || !dys) {
  fail('usage: <port> <target> <x> <y> <deltaY> [steps]');
}
const x = Number(xs), y = Number(ys), total = Number(dys);
const steps = Math.max(1, Number(stepss || 6));

const list = await new Promise((res, rej) => {
  const req = http.get({host: '127.0.0.1', port: Number(port), path: '/json/list'}, (r) => {
    let body = '';
    r.on('data', (d) => body += d);
    r.on('end', () => { try { res(JSON.parse(body)); } catch (e) { rej(e); } });
  });
  req.on('error', rej);
  req.setTimeout(5000, () => req.destroy(new Error('timeout listing targets')));
}).catch((e) => fail(`cannot list targets: ${e.message}`));
const target = list.find((t) => (t.url || '').includes(match));
if (!target) {
  fail(`no target matching "${match}" among ${list.map((t) => t.url).join(' , ') || '(none)'}`);
}
let ws;
let send;
try {
  ({ws, send} = await connect(target));
} catch (e) {
  fail(e.message);
}
try {
  const per = total / steps;
  for (let i = 0; i < steps; i++) {
    await send('Input.dispatchMouseEvent',
               kind === 'move'
                   ? {type: 'mouseMoved', x: x + i, y}
                   : {type: 'mouseWheel', x, y, deltaX: 0, deltaY: per});
  }
  console.log(`OK ${JSON.stringify({steps, total})}`);
} catch (e) {
  fail(e.message);
} finally {
  ws.close();
}
