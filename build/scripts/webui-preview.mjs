#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Renders a WebUI chrome route in an ordinary browser tab, for looking at it.
//
// Why this exists: /sidebar and /foyer have no host widget yet, so they cannot
// be seen by launching Atrium. And the privileged page they DO live on is an
// offscreen WebContents, which cannot be screenshotted. This inlines the real
// resource files - no copies - with a sample snapshot, so what you look at is
// the shipping render code.
//
// It is a viewer, not a test: sample data is invented here. For the real thing
// use CDP against chrome://atrium.top-chrome/<route> in a running Atrium.
//
//   node build/scripts/webui-preview.mjs sidebar
//   node build/scripts/webui-preview.mjs foyer  /tmp/foyer.html

import {readFileSync, writeFileSync} from 'node:fs';
import {homedir, tmpdir} from 'node:os';
import {join} from 'node:path';

const RES = join(
    homedir(), 'chromium-atrium/src/chrome/browser/resources/atrium');

const SAMPLES = {
  sidebar: {
    entry: 'renderSidebar',
    snapshot: {
      spaceName: 'Work',
      spaceTint: '#5b8def',
      favorites: [
        {id: 'a', title: 'Linear', color: '#5e6ad2', selected: true},
        {id: 'b', title: 'GitHub', color: '#8b949e'},
        {id: 'c', title: 'Figma', color: '#a259ff'},
        {id: 'd', title: 'Notion', color: '#e8e8e8'},
        {id: 'e', title: 'Sentry', color: '#e1567c'},
        {id: 'f', title: 'Docs', color: '#4285f4'},
      ],
      pinned: [{id: 'p1', title: 'Atrium backlog', color: '#5e6ad2',
                selected: true}],
      folders: [
        {id: 'f1', name: 'Chromium', closed: false, tabs: [
          {id: 'ft1', title: 'patches/0058', color: '#4285f4'},
          {id: 'ft2', title: 'atrium_ui.cc', color: '#8b949e'},
        ]},
        {id: 'f2', name: 'Reading', closed: true, icon: '★',
         tabs: [{id: 'ft3', title: 'later', color: '#aaa'}]},
      ],
      tabs: [
        {id: 't1', title: 'A very long tab title that has to truncate somewhere',
         color: '#e1567c'},
        {id: 't2', title: 'swipe-lab.html', color: '#a259ff'},
      ],
      spaces: [
        {name: 'Overview', overview: true},
        {name: 'Work', color: '#5b8def'},
        {name: 'Personal', color: '#e1567c'},
        {name: 'Side', color: '#36c5f0'},
      ],
      activeSpace: 1,
    },
    // The sidebar is a full-height column.
    frame: `#atrium-root{height:100vh;width:280px}
            body{background:linear-gradient(160deg,#2b3444,#1b2130)}`,
  },
  foyer: {
    entry: 'renderFoyer',
    snapshot: {
      title: 'Trusted Types - web.dev',
      url: 'https://web.dev/articles/trusted-types',
      tint: '#5b8def',
    },
    // The foyer is a thin strip above web content.
    frame: `#atrium-root{height:38px;background:linear-gradient(180deg,#2b3444,#242c3a);
              border-bottom:1px solid rgba(91,141,239,.6)}
            body{background:#11151d}
            .page{height:200px;display:flex;align-items:center;justify-content:center;
              color:rgba(255,255,255,.25);font-size:12px}`,
    after: '<div class="page">web content</div>',
  },
  pill: {
    entry: 'renderPill',
    // renderPill takes the action array directly, not a snapshot object.
    snapshot: [
      {id: 'aaaa', name: 'uBlock Origin', visible: true, hasPopup: true,
       actionTitle: 'uBlock Origin', badgeText: '12',
       badgeBackground: 0xFF2E7D32, badgeTextColor: 0xFFFFFFFF},
      {id: 'bbbb', name: 'Bitwarden', visible: true, hasPopup: true,
       actionTitle: 'Bitwarden', badgeText: '',
       badgeBackground: 0, badgeTextColor: 0},
    ],
    frame: `#atrium-root{padding:14px}
            body{background:linear-gradient(160deg,#2b3444,#1b2130)}
            /* No extension icons outside the browser - show the cells. */
            .pill-action{outline:1px dashed rgba(255,255,255,.25)}`,
  },
};

const route = process.argv[2] || 'sidebar';
const spec = SAMPLES[route];
if (!spec) {
  console.error(`unknown route "${route}" - try: ${Object.keys(SAMPLES).join(', ')}`);
  process.exit(2);
}
const out = process.argv[3] || join(tmpdir(), `atrium-${route}-preview.html`);

// Strip the module syntax so it runs as a classic script. The BODIES are
// untouched: if the shipping render drifts, this drifts with it.
const js = readFileSync(join(RES, `${route}.js`), 'utf8')
               .replace(/export function/g, 'function')
               .replace(/^import .*$/gm, '');
const css = readFileSync(join(RES, `${route}.css`), 'utf8');

const html = `<!doctype html>
<html><head><meta charset="utf-8"><title>Atrium ${route} preview</title>
<style>
html,body{margin:0;min-height:100%;color:rgba(255,255,255,.92);
  font-family:system-ui,-apple-system,sans-serif}
${spec.frame}
${css}
</style></head>
<body>
<div id="atrium-root"></div>
${spec.after || ''}
<script>
${js}
window.chrome = {send: (name, args) => console.log('chrome.send', name, args)};
${spec.entry}(document.getElementById('atrium-root'), ${JSON.stringify(spec.snapshot)});
</script>
</body></html>`;

writeFileSync(out, html);
console.log(out);
