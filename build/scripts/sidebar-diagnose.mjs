#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Dumps what the HTML sidebar is actually rendering, against a running Atrium.
//
//   make run FLAGS='--remote-debugging-port=9333'
//   make sidebar-diagnose
//
// Read-only: it evaluates expressions in the surface and never clicks, closes
// or focuses anything, so it is safe to point at a real browsing session.
//
// It exists because "the sidebar does not work" has at least three distinct
// causes that look identical from the outside, and one dump separates them:
//
//   ids all ""        -> nothing joined to a live tab; the row is inert by
//                        design and the bug is in the persisted->live join
//   ids duplicated    -> several rows share one tab, so they all activate it
//   ids populated     -> the join is fine and the bug is downstream, in
//                        ActivateSidebarTab or the window it looks in

import {connect} from './sidebar-probe-lib.mjs';

const PORT = process.env.ATRIUM_CDP_PORT || '9333';
const BASE = `http://127.0.0.1:${PORT}`;

let targets;
try {
  targets = await (await fetch(`${BASE}/json/list`)).json();
} catch {
  console.error(`nothing answering on ${BASE}.`);
  console.error(`Launch with: make run FLAGS='--remote-debugging-port=${PORT}'`);
  process.exit(2);
}

// EVERY AtriumBrowserWindow builds a sidebar surface, including the per-profile
// windows that are never shown. A user with five spaces on five profiles has
// five surfaces, all rendering identical rows - so picking the first match
// reads a hidden window and reports its state as if it were the one on screen.
// That mistake produced a wrong diagnosis once already; pick the biggest
// window, which is the one a person is looking at.
const sidebars = targets.filter((t) => t.url.includes('top-chrome/sidebar'));
let sidebar = sidebars[0];
if (sidebars.length > 1) {
  const measured = [];
  for (const candidate of sidebars) {
    const probe = new WebSocket(candidate.webSocketDebuggerUrl);
    try {
      await new Promise((r, x) => {
        probe.addEventListener('open', r, {once: true});
        probe.addEventListener('error', x, {once: true});
      });
      const size = await new Promise((r) => {
        probe.addEventListener('message', (e) => {
          const m = JSON.parse(e.data);
          if (m.id === 1) { r(m.result?.result?.value ?? 0); }
        });
        // BOTH signals, because each alone picks the wrong surface somewhere:
        //
        //   clientWidth  - 0 for a window whose document was never laid out,
        //                  which is how the VM's hidden windows present. But
        //                  ranking BY it picks the widest SIDEBAR, and a real
        //                  user's sidebar is often narrower than the 220px
        //                  default the hidden windows keep.
        //   window area  - the real window is far bigger than a never-shown
        //                  one, but a never-laid-out window still reports a
        //                  plausible size.
        //
        // So: drop anything not laid out, then take the biggest window.
        probe.send(JSON.stringify({
          id: 1, method: 'Runtime.evaluate',
          params: {expression: 'JSON.stringify({c: document.documentElement.clientWidth,' +
                               ' a: window.outerWidth * window.outerHeight,' +
                               ' v: document.visibilityState})',
                   returnByValue: true},
        }));
      });
      const m = JSON.parse(size || '{"c":0,"a":0,"v":"hidden"}');
      measured.push({candidate, laidOut: m.c > 0, area: m.a, width: m.c,
                     visible: m.v === 'visible'});
    } catch {
      measured.push({candidate, laidOut: false, area: 0, width: 0, visible: false});
    } finally {
      probe.close();
    }
  }
  // visibilityState first - a never-shown window reports 'hidden' and that is
  // a statement of fact rather than a guess from geometry. Size only breaks
  // ties among genuinely visible ones.
  const visible = measured.filter((m) => m.visible && m.laidOut);
  const usable = visible.length ? visible : measured.filter((m) => m.laidOut);
  const ranked = (usable.length ? usable : measured).sort((a, b) => b.area - a.area);
  sidebar = ranked[0].candidate;
  console.log(`${sidebars.length} sidebar surfaces exist (one per window, ` +
              `including never-shown per-profile ones).`);
  console.log(`surfaces: ${measured.map((m) =>
      `${Math.round(m.area / 1000)}k/${m.width}px` +
      (m.visible ? ' VISIBLE' : ' hidden') +
      (m.laidOut ? '' : ' unlaid')).join(', ')}`);
  console.log(`reading the largest laid-out window ` +
              `(${Math.round(ranked[0].area / 1000)}k, ${ranked[0].width}px sidebar)\n`);
}
if (!sidebar) {
  console.error('Atrium is running but has no HTML sidebar surface.');
  console.error(`Targets: ${targets.map((t) => t.type + ':' + t.url.slice(0, 60)).join('\n         ')}`);
  process.exit(2);
}

const pages = targets.filter((t) => t.type === 'page');
console.log(`live page targets: ${pages.length}`);
for (const page of pages.slice(0, 12)) {
  console.log(`  ${page.url.slice(0, 90)}`);
}
console.log();

const {ws, send, ev} = await connect(sidebar);
await send('Runtime.enable');

// Read the VISIBLE pane, not the first one. The rail translates to show pane N,
// and reading pane 0 reports a pane the user is not looking at - the same class
// of mistake as reading the first of five surfaces.
const PANE = `(() => {
  const rail = document.querySelector('.rail');
  const panes = [...document.querySelectorAll('.railpane')];
  if (!rail || !panes.length) {
    return document.querySelector('.sidebar');
  }
  // From the rail's own transform, not from rects. A pane is as wide as the
  // rail's viewport, and the rail translates by whole panes, so -N*width means
  // pane N is on screen. Reading rects got this wrong.
  const shift = /translateX\\((-?[\\d.]+)px\\)/.exec(rail.style.transform || '');
  const width = panes[0].getBoundingClientRect().width || 1;
  const index = shift ? Math.round(-Number(shift[1]) / width) : 0;
  return panes[Math.min(Math.max(index, 0), panes.length - 1)] || panes[0];
})()`;
const rows = async (selector, label) => {
  const data = await ev(`(() => {
    const pane = ${PANE};
    if (!pane) { return null; }
    return [...pane.querySelectorAll('${selector}')].map((r) => ({
      id: r.dataset.tabId ?? r.dataset.favId ?? '',
      title: (r.querySelector('.tl') || {}).textContent || '',
      selected: r.classList.contains('sel'),
      draggable: r.draggable === true,
    }));
  })()`);
  if (!data || !data.length) {
    console.log(`${label}: none`);
    return [];
  }
  console.log(`${label}: ${data.length}`);
  for (const [i, r] of data.entries()) {
    console.log(`  [${i}] id=${JSON.stringify(r.id)}` +
                ` sel=${r.selected} drag=${r.draggable} ${JSON.stringify(r.title)}`);
  }
  return data;
};

// Which space this pane is actually rendering. The surface picks its own
// active space (activeSpaceID(forSlot:)), which is not necessarily
// appState.activeSpaceID - a window can be showing a different one, and then
// "no tab rows" just means THAT space is empty.
const shown = await ev(`(() => {
  const pane = ${PANE};
  const name = pane && pane.querySelector('.wsname');
  const rail = document.querySelector('.rail');
  const panes = [...document.querySelectorAll('.railpane')];
  return {
    space: name ? name.textContent.trim() : null,
    panes: panes.length,
    activeSpaceAttr: rail ? rail.dataset.activeSpace : null,
    visiblePane: pane ? pane.dataset.spaceIndex : null,
    dots: document.querySelectorAll('.sdot').length,
    railTransform: rail ? rail.style.transform : null,
  };
})()`);
console.log(`surface shows space ${JSON.stringify(shown.space)} in pane ` +
            `${shown.visiblePane} of ${shown.panes} (rail transform ${shown.railTransform || 'none'})`);
console.log(`rail data-active-space = ${shown.activeSpaceAttr}`);
if (shown.visiblePane !== null && shown.activeSpaceAttr !== null &&
    shown.visiblePane !== shown.activeSpaceAttr) {
  console.log(`  MISMATCH: the pane on screen is ${shown.visiblePane} but the rows`);
  console.log(`  belong to space ${shown.activeSpaceAttr}. A drag resolves its SOURCE`);
  console.log(`  from data-active-space and its DESTINATION from the pane it is`);
  console.log(`  dropped on, so in this state an ordinary reorder is read as a`);
  console.log(`  CROSS-SPACE MOVE - which appends to another space instead of`);
  console.log(`  reordering, or resolves to nothing.`);
}
console.log();

// installRowDrag only claims a row whose PARENT declares a dropzone. A row that
// renders but sits in an undeclared container is draggable to the eye and inert
// in practice - and favourites would still work, because they are claimed by a
// different module with its own selector.
const zones = await ev(`(() => {
  const pane = ${PANE};
  if (!pane) { return null; }
  const rowsEl = pane.querySelector('.rows');
  return {
    rowsHasZone: rowsEl ? (rowsEl.dataset.dropzone || null) : 'no .rows at all',
    zonesPresent: [...pane.querySelectorAll('[data-dropzone]')]
        .map((z) => z.dataset.dropzone),
    firstRowParentZone: (() => {
      const r = pane.querySelector('.rows > .tabrow');
      return r && r.parentElement ? (r.parentElement.dataset.dropzone || null) : null;
    })(),
  };
})()`);
console.log(`drop zones present: ${JSON.stringify(zones?.zonesPresent)}`);
console.log(`tab row's parent declares zone: ${JSON.stringify(zones?.firstRowParentZone)}`);
if (zones && zones.firstRowParentZone === null && zones.rowsHasZone !== 'no .rows at all') {
  console.log('  ^ installRowDrag ignores a row whose parent declares no zone,');
  console.log('    so these rows cannot be dragged while favourites still can.');
}
console.log();

const tabs = await rows('.rows > .tabrow', 'regular tab rows');
const pinned = await rows('[data-dropzone="pinned"] > .tabrow', 'pinned rows');
await rows('.favgrid .fav', 'favourites');

// Anything the three selectors above missed - rows inside folders, or a row in
// a container this tool does not know about. A row that renders but is not
// counted anywhere is itself the finding.
const stray = await ev(`(() => {
  const pane = ${PANE};
  if (!pane) { return 0; }
  const known = new Set([
    ...pane.querySelectorAll('.rows > .tabrow'),
    ...pane.querySelectorAll('[data-dropzone="pinned"] > .tabrow'),
  ]);
  return [...pane.querySelectorAll('.tabrow')].filter((r) => !known.has(r)).length;
})()`);
if (stray) {
  console.log(`rows in other containers (folders etc.): ${stray}`);
}

console.log();
const ids = tabs.map((r) => r.id);
const empty = ids.filter((v) => !v).length;
const dupes = ids.filter((v, i) => v && ids.indexOf(v) !== i).length;
if (!tabs.length) {
  console.log('VERDICT: no regular tab rows at all - the snapshot has no tabs for this space.');
} else if (empty === ids.length) {
  console.log('VERDICT: EVERY row id is empty. No row joined to a live tab, so every');
  console.log('         row is inert by design. The bug is in the persisted->live join.');
} else if (empty) {
  console.log(`VERDICT: ${empty}/${ids.length} row ids are empty - those rows are inert.`);
  console.log('         Partial join failure; the empty ones have no live counterpart.');
} else if (dupes) {
  console.log(`VERDICT: ${dupes} duplicate id(s) - those rows all activate the same tab.`);
} else {
  console.log('VERDICT: every row has a distinct live id, so the join is fine.');
  console.log('         If clicking still does nothing the bug is downstream:');
  console.log('         ActivateSidebarTab searches THIS window\'s tab strip only.');
}
// What would a drag of the first row onto the second resolve to? Computed with
// the surface's own dropDecision, so it reports the real answer rather than an
// assumption - and it performs no drop.
if (tabs.length >= 2) {
  const decision = await ev(`(async () => {
    const mod = await import('./sidebar-drag.js');
    const pane = ${PANE};
    const zone = pane.querySelector('[data-dropzone="tabs"]');
    const rows = [...zone.children].filter((e) => e.matches('.tabrow'));
    const rail = document.querySelector('.rail');
    const source = rail ? Number(rail.dataset.activeSpace) : undefined;
    const target = pane.dataset.spaceIndex !== undefined
        ? Number(pane.dataset.spaceIndex) : undefined;
    return {
      source, target,
      decision: mod.dropDecision({
        from: {section: 'tabs', index: 0, space: source},
        to: {section: 'tabs', slot: rows.length, space: target},
      }),
    };
  })()`).catch((e) => ({error: String(e)}));
  console.log(`drag dry-run: dragging row 0 to the end resolves to ` +
              `${JSON.stringify(decision.decision ?? decision)}`);
  if (decision.decision && decision.decision.kind === 'moveToSpace') {
    console.log('  ^ a plain reorder is being read as a cross-space move.');
  }
}

console.log(`selected row: ${tabs.findIndex((r) => r.selected)}`);
console.log(`pinned rows : ${pinned.length}`);

// --watch: record what the surface SENDS when a human clicks, without this
// tool clicking anything itself. That is the one observation that separates
// "the click never reaches the handler" from "the message is sent and the
// browser ignores it" - two different bugs with identical symptoms.
if (process.argv.includes('--watch')) {
  await ev(`(() => {
    window.__watch = [];
    if (!chrome.send.__watched) {
      const real = chrome.send.bind(chrome);
      chrome.send = (name, args) => {
        window.__watch.push([name, args]);
        return real(name, args);
      };
      chrome.send.__watched = true;
    }
    // Passive listeners, capture phase, so they see the event even if
    // something downstream stops propagation.
    if (!window.__watchClicks) {
      window.__watchClicks = true;
      const where = (e) => {
        const row = e.target.closest && e.target.closest('.tabrow');
        if (row) { return '.tabrow id=' + (row.dataset.tabId || '(cold)'); }
        const fav = e.target.closest && e.target.closest('.fav');
        return fav ? '.fav' : 'elsewhere';
      };
      document.addEventListener('click', (e) => {
        window.__watch.push(['DOM click', where(e)]);
      }, true);
      // The decisive ones. Every drag probe in this repo SYNTHESISES these,
      // which proves the handlers work but says nothing about whether a real
      // mouse press makes the browser start a drag at all - a top-chrome WebUI
      // is hosted in a views widget, and native HTML5 drag there is not a given.
      for (const type of ['mousedown', 'dragstart', 'dragover', 'drop', 'dragend']) {
        document.addEventListener(type, (e) => {
          window.__watch.push(['DOM ' + type, where(e)]);
        }, true);
      }
    }
    return true;
  })()`);

  console.log('\nWATCHING for 30s. Try BOTH: click a tab row, then drag one up or down.');
  console.log('(mousedown with no dragstart after it = the browser never starts a drag)');
  const deadline = Date.now() + 30000;
  let seen = 0;
  while (Date.now() < deadline) {
    await new Promise((r) => setTimeout(r, 1000));
    const events = await ev(`window.__watch`).catch(() => null);
    if (Array.isArray(events) && events.length > seen) {
      for (const [name, args] of events.slice(seen)) {
        console.log(`  ${name}: ${JSON.stringify(args)}`);
      }
      seen = events.length;
    }
  }
  console.log(seen ? '\nDone.' : '\nNothing recorded - the click never reached the page at all.');
}

ws.close();
process.exit(0);

ws.close();
