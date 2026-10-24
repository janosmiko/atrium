#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// no icon slot paints a tint, asserted end to end.
//
// It replaces a probe that pinned the opposite: a per-host hue behind
// every slot, switched off by a `hasicon` class once an icon decoded. The hue
// was a stand-in for a favicon before /urlicon served one, so it only ever
// appeared where a lookup found nothing - a random coloured square. It is gone,
// and a slot with no icon is an empty square.
//
//   ./build/scripts/vm.sh run-atrium \
//       --remote-debugging-port=9334
//   ./build/scripts/vm-cdp.sh
//   node build/scripts/favicon-tint-probe.mjs
//
// The favicon served here is RGBA with transparent corners. The earlier probe
// icon was opaque, so it painted over the tint and hid this defect entirely.
import {connect} from './sidebar-probe-lib.mjs';

import {execFileSync} from 'node:child_process';
import os from 'node:os';
import path from 'node:path';

const PORT = process.env.ATRIUM_CDP_PORT || '9334';
const SITE_PORT = '8098';
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

// Built in the VM with python's zlib alone - no image tools, no network. Colour
// type 6 is RGBA: a filled centre with a fully transparent 8px margin, so most
// of the slot is see-through and anything painted below it is measurable.
const SITE = `
mkdir -p /tmp/tintsite && cd /tmp/tintsite
python3 - <<'PY'
import zlib, struct
def chunk(t, d):
    c = t + d
    return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
w = h = 32
rows = []
for y in range(h):
    px = b''
    for x in range(w):
        inner = 8 <= x < 24 and 8 <= y < 24
        px += (b'\\x00\\x66\\xff\\xff' if inner else b'\\x00\\x00\\x00\\x00')
    rows.append(b'\\x00' + px)
png = (b'\\x89PNG\\r\\n\\x1a\\n'
       + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
       + chunk(b'IDAT', zlib.compress(b''.join(rows)))
       + chunk(b'IEND', b''))
open('favicon.png','wb').write(png)
open('index.html','w').write('<html><head><link rel="icon" type="image/png" '
  'href="/favicon.png"><title>Tint Probe</title></head><body>tint</body></html>')
PY
pkill -f 'http.server ${SITE_PORT}' 2>/dev/null || true
cd /tmp/tintsite && nohup python3 -m http.server ${SITE_PORT} >/tmp/tintsite.log 2>&1 &
sleep 2
curl -s -o /dev/null -w '%{http_code}' http://127.0.0.1:${SITE_PORT}/favicon.png
`;

console.log('\nfavicon tint probe');
const status = vm(SITE).trim().split('\n').pop();
check(status === '200', 'the VM serves a page whose favicon has transparent pixels',
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
const slots = await sb.ev(`(() => {
  const read = (slot) => {
    const img = slot.querySelector('.fvimg');
    return {
      // The computed value is what decides the pixels. Reading the stylesheet
      // would pass on a build where something else paints the slot.
      bg: getComputedStyle(slot).backgroundColor,
      decoded: !!img && img.naturalWidth > 0,
      hasImg: !!img,
    };
  };
  const all = [...document.querySelectorAll('.row .fv, .fav .fvi')].map(read);
  return {
    total: all.length,
    withIcon: all.filter((s) => s.decoded),
    withoutIcon: all.filter((s) => !s.hasImg),
  };
})()`);

// The iconless half cannot be produced by browsing: every live tab resolves an
// icon here, while the tiles the user reported are keyed by URL and answer 404
// for anything the history icon database has never seen. So it is pushed.
await sb.ev(`window.atriumSetSidebar({
  spaceName: 'Probe',
  favorites: [{id: 'https://nothing.invalid/tint', title: 'unresolvable'}],
  tabs: [],
})`);
await new Promise((r) => setTimeout(r, 1500));
const empty = await sb.ev(`(() => {
  const slots = [...document.querySelectorAll('.fav .fvi')];
  return {
    total: slots.length,
    withoutIcon: slots.filter((s) => !s.querySelector('.fvimg'))
                      .map((s) => getComputedStyle(s).backgroundColor),
  };
})()`);
sb.ws.close();

const transparent = (bg) =>
    bg === 'rgba(0, 0, 0, 0)' || bg === 'transparent';

check(slots.total > 0, 'the sidebar drew icon slots at all', `${slots.total} slots`);
// Load-bearing: with no decoded icon anywhere, every assertion below is vacuous
// and would pass on a build that never clears the tint.
check(slots.withIcon.length > 0,
    'at least one favicon decoded, so the cleared case is really exercised',
    `${slots.withIcon.length} of ${slots.total} decoded`);
check(slots.withIcon.every((s) => transparent(s.bg)),
    'a decoded icon has nothing painted behind its transparent pixels',
    slots.withIcon.map((s) => s.bg).join(', ') || 'none');
// The half the user reported: a tile whose URL resolves nothing was a coloured
// square. Vacuous without a slot in that state, hence the count.
check(empty.withoutIcon.length > 0,
    'the pushed favourite resolved no icon, so the empty case is exercised',
    `${empty.withoutIcon.length} of ${empty.total} tiles without an icon`);
check(empty.withoutIcon.every(transparent),
    'and a tile with no icon is an empty square, not a coloured one',
    empty.withoutIcon.join(', ') || 'none');

console.log(failures ? `\n${failures} assertion(s) failed` : '\nall assertions passed');
process.exit(failures ? 1 : 0);
