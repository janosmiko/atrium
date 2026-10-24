#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// real favicons in the HTML sidebar, asserted end to end.
//
//   ./build/scripts/vm.sh run-atrium \
//       --remote-debugging-port=9334
//   ./build/scripts/vm-cdp.sh
//   node build/scripts/favicon-probe.mjs
//
// The probe serves its own page, because the VM has no external network - so
// no site there ever HAS a favicon, and "nothing was cached" is
// indistinguishable from "caching is broken". Chasing that difference is most
// of what this task cost.
//
// What it proves, in order: the browser stored bytes for the tab, the WebUI
// host served them back, and the row's <img> decoded them. The middle step is
// the one that used to fail - the icons lived on a host of their own and the
// request never reached the data source.

import {connect} from './sidebar-probe-lib.mjs';

import {execFileSync} from 'node:child_process';
import os from 'node:os';
import path from 'node:path';

const PORT = process.env.ATRIUM_CDP_PORT || '9334';
const SITE_PORT = '8099';
const KEY = path.join(process.env.TART_HOME || path.join(os.homedir(), '.tart'),
    'atrium-probe-key');

let failures = 0;
function check(ok, what, detail) {
  console.log(`${ok ? '  ok  ' : '  FAIL'}  ${what}${detail ? `  (${detail})` : ''}`);
  if (!ok) {
    failures++;
  }
}

function vm(script) {
  return execFileSync('ssh', [
    '-i', KEY, '-o', 'StrictHostKeyChecking=no',
    '-o', 'UserKnownHostsFile=/dev/null', '-o', 'LogLevel=ERROR',
    `admin@${vmIp()}`, script,
  ], {encoding: 'utf8'});
}

let cachedIp;
function vmIp() {
  if (!cachedIp) {
    cachedIp = execFileSync('./build/scripts/vm.sh', ['ip'], {encoding: 'utf8'}).trim();
  }
  return cachedIp;
}

// A 32x32 PNG built in the VM with nothing but python's zlib - no image tools,
// no download, so this works on a machine with no network at all.
const SITE = `
mkdir -p /tmp/favsite && cd /tmp/favsite
python3 - <<'PY'
import zlib, struct
def chunk(t, d):
    c = t + d
    return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
w = h = 32
raw = b''.join(b'\\x00' + b'\\xff\\x00\\xaa' * w for _ in range(h))
png = (b'\\x89PNG\\r\\n\\x1a\\n'
       + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
       + chunk(b'IDAT', zlib.compress(raw))
       + chunk(b'IEND', b''))
open('favicon.png','wb').write(png)
open('index.html','w').write('<html><head><link rel="icon" type="image/png" '
  'href="/favicon.png"><title>Favicon Probe</title></head><body>probe</body></html>')
PY
pkill -f 'http.server ${SITE_PORT}' 2>/dev/null || true
cd /tmp/favsite && nohup python3 -m http.server ${SITE_PORT} >/tmp/favsite.log 2>&1 &
sleep 2
curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:${SITE_PORT}/favicon.png
`;

console.log('\nfavicon probe');
const status = vm(SITE).trim().split('\n').pop();
check(status === '200', 'the VM serves a page that has a favicon',
    `local site returned ${status}`);

const base = `http://127.0.0.1:${PORT}`;
let list;
try {
  list = await (await fetch(`${base}/json/list`)).json();
} catch {
  console.error(`No debug port on ${PORT}. Run ./build/scripts/vm-cdp.sh first.`);
  process.exit(1);
}

const page = list.find((t) => t.type === 'page');
if (!page) {
  console.error('No ordinary tab to navigate. Open one in the VM.');
  process.exit(1);
}
const tab = await connect(page);
await tab.send('Page.enable');
await tab.send('Page.navigate', {url: `http://127.0.0.1:${SITE_PORT}/`});
await new Promise((r) => setTimeout(r, 6000));
tab.ws.close();

const sidebar = list.find((t) => t.url.includes('top-chrome/sidebar'));
if (!sidebar) {
  console.error('No HTML sidebar found.');
  process.exit(1);
}
const sb = await connect(sidebar);
const imgs = await sb.ev(`(() => {
  const all = [...document.querySelectorAll('.fvimg')];
  return {
    present: all.length,
    loaded: all.filter((i) => i.naturalWidth > 0).length,
    sizes: all.map((i) => i.naturalWidth + 'x' + i.naturalHeight).join(', '),
    sameHost: all.every((i) => new URL(i.src).pathname.startsWith('/favicon/')),
  };
})()`);
sb.ws.close();

// present > 0 is load-bearing: the error handler REMOVES an img that 404s, so
// zero surviving elements is exactly what the bug looked like.
check(imgs.present > 0, 'favicon <img> elements survive the load',
    `${imgs.present} present`);
check(imgs.loaded === imgs.present && imgs.present > 0,
    'every one decoded real bytes',
    `${imgs.loaded}/${imgs.present} loaded, sizes ${imgs.sizes || 'none'}`);
check(imgs.sameHost, 'they are served from the WebUI host, not a host of their own');

console.log(failures ? `\n${failures} assertion(s) failed` : '\nall assertions passed');
process.exit(failures ? 1 : 0);
