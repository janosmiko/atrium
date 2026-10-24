// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//
// evaluates one expression in a named Atrium surface and prints the
// result, so a scenario can ask a surface a question instead of only watching
// its log and its pixels.
//
// Runs on the HOST against a forwarded debugging port. The VM has no node, and
// the harness is already a host-side script, so this needs no new tooling
// inside the probe machine.
//
//   node cdp-eval.mjs <port> <target-substring> <expression>
//
// Prints one line: OK <json>, or BLIND <reason>. It says BLIND rather than
// printing a falsy value whenever it could not look properly - no target, no
// socket, a thrown expression. A probe that returns 0 when it cannot see is
// worse than no probe, because 0 is also a real answer.
import {connect} from './sidebar-probe-lib.mjs';

import http from 'node:http';

// The global WebSocket, not the `ws` package: this runs from the repo with no
// install step, and a probe that needs node_modules is a probe that reports
// BLIND on a clean checkout.

const [port, match, expression] = process.argv.slice(2);
const fail = (why) => { console.log(`BLIND ${why}`); process.exit(3); };
if (!port || !match || !expression) fail('usage: <port> <target> <expression>');

const list = await new Promise((res, rej) => {
  const req = http.get({host: '127.0.0.1', port: Number(port), path: '/json/list'}, (r) => {
    let body = '';
    r.on('data', (d) => body += d);
    r.on('end', () => { try { res(JSON.parse(body)); } catch (e) { rej(e); } });
  });
  req.on('error', rej);
  req.setTimeout(5000, () => req.destroy(new Error('timeout listing targets')));
}).catch((e) => fail(`cannot list targets: ${e.message}`));

// An exact path first. Two windows of one surface share a URL, so naming one
// takes a fragment (chrome://atrium/sidebar#incognito) - and a bare substring
// then lets "/sidebar" pick either of the two.
// The hash is part of the key: without it "/sidebar" matches the fragmented
// sibling exactly as well, which is the ambiguity this exists to remove.
const pathOf = (url) => {
  try {
    const parsed = new URL(url);
    return parsed.pathname + parsed.hash;
  } catch {
    return '';
  }
};
const target = list.find((t) => pathOf(t.url || '') === match) ||
    list.find((t) => (t.url || '').includes(match));
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

// sidebar.css's `html { transition: background-color .3s ease }`
// makes a set-then-read see the pre-write value. A reflow cannot
// advance that clock, so this cancels transitions for the call instead.
const NO_TRANSITIONS_CLASS = '__atrium_probe_no_transitions';
const wrapped = `(() => {
  const clear = () => document.querySelectorAll('.${NO_TRANSITIONS_CLASS}')
      .forEach((el) => el.remove());
  clear();
  const s = document.createElement('style');
  s.className = '${NO_TRANSITIONS_CLASS}';
  s.textContent = '*,*::before,*::after{transition:none!important}';
  (document.head || document.documentElement).appendChild(s);
  void document.documentElement.offsetHeight;
  // Lifted next microtask, before any rAF tick a returned promise schedules -
  // a fade check awaits one that samples a real transition across
  // several of those, and must see it running, not frozen for the wait.
  Promise.resolve().then(() => { clear(); void document.documentElement.offsetHeight; });
  return (0, eval)(${JSON.stringify(expression)});
})()`;

try {
  // Runtime.enable already happened inside connect().
  const out = await send('Runtime.evaluate',
                         {expression: wrapped, returnByValue: true, awaitPromise: true});
  if (out.result?.exceptionDetails) {
    fail(`expression threw: ${out.result.exceptionDetails.text}`);
  }
  console.log(`OK ${JSON.stringify(out.result?.result?.value ?? null)}`);
} catch (e) {
  fail(e.message);
} finally {
  ws.close();
}
