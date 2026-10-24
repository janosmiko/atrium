#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// drives real cross-section drops in the VM's HTML sidebar.
//
//   ./build/scripts/vm-cdp.sh && node build/scripts/row-drop-probe.mjs
//
// Every assertion reads the sidebar AFTER the browser has pushed a new
// snapshot, so what is checked is the model round trip - drag, chrome.send,
// ABI, SpacesStore, save, re-push, re-render - and not merely that some
// JavaScript ran.
//
// LIMITATION: this probe synthesises DragEvents with dispatchEvent, which
// bypasses the browser entirely. It verifies handlers and index maths and
// CANNOT see whether a real pointer starts or survives a drag - it passed
// while dragging had never worked once. make
// drag-input-probe is the check that covers that.
//
// Needs a CLEAN fixture: favourites are keyed by (profile, url) and addFavorite
// is idempotent, so a re-run against a space whose tab is already favourited
// sees no new tile and reads as a failure.
//
// Two modes, chosen by what the space holds. With no pinned tabs it checks the
// FIRST pin - the case where the target does not exist at rest and has to be
// revealed by the drag. With pinned tabs it checks the full set of crossings.

import {connect} from './sidebar-probe-lib.mjs';

const PORT = process.env.ATRIUM_CDP_PORT || '9334';

const targets = await (await fetch(`http://127.0.0.1:${PORT}/json/list`)).json();
const target = targets.find((t) => t.url.includes('top-chrome/sidebar'));
if (!target) {
  console.error('no sidebar target on :' + PORT + '. Is Atrium running in the VM?');
  process.exit(2);
}

const {ws, send: call, ev} = await connect(target);

let failures = 0;
const check = (label, actual, expected) => {
  const ok = JSON.stringify(actual) === JSON.stringify(expected);
  if (!ok) {
    failures++;
  }
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}` +
      (ok ? '' : `\n       expected ${JSON.stringify(expected)}` +
                 `\n       actual   ${JSON.stringify(actual)}`));
};

await call('Runtime.enable');

// No rail when there is only one space, so fall back to the bare sidebar.
// [data-space-index] skips the rowless .ovrailpane, which a bare
// '.railpane' matches first.
const PANE = `(document.querySelector('.railpane[data-space-index]') || document.querySelector('.sidebar'))`;
// "pinned" is on two elements: the always-drawn header (the empty-list
// fallback target) and its .foldin body, which holds the real rows and must
// win. Favourites are a sibling of every pane, so they fall back to document.
const zoneExpr = (pane, sel) =>
    `(${pane}.querySelector('.foldin[data-dropzone="${sel}"]') || ` +
    `${pane}.querySelector('[data-dropzone="${sel}"]') || ` +
    `document.querySelector('[data-dropzone="${sel}"]'))`;
const titlesIn = (section) => ev(`(() => {
  const z = ${zoneExpr(PANE, section)};
  if (!z) { return null; }
  return [...z.children].filter((e) => e.matches('.tabrow'))
      .map((e) => e.querySelector('.tl').textContent);
})()`);
const favCount = () => ev(
    `document.querySelectorAll('[data-dropzone="favorites"] .fav').length`);
const settle = () => new Promise((r) => setTimeout(r, 2500));

// A drag from one section into another, as the browser would deliver it:
// dragstart, dragover (which is what allows the drop at all), drop, dragend.
//
// `edge` picks where in the destination to aim - and it is measured AFTER
// dragstart, on purpose. dragstart reveals the empty-section drop affordances,
// and revealing them reflows the sidebar, so a Y captured beforehand points at
// where a row used to be. A real drag re-reads the pointer on every dragover,
// which amounts to the same thing.
const dragTo = (fromSection, fromIndex, toSection, edge = 'top') => ev(`(() => {
  window.__sent = [];
  if (!chrome.send.__wrapped) {
    const real = chrome.send.bind(chrome);
    chrome.send = (n, a) => { window.__sent.push([n, a]); return real(n, a); };
    chrome.send.__wrapped = true;
  }
  const pane = ${PANE};
  const kids = (z) => [...z.children].filter((e) => e.matches('.tabrow'));
  const src = kids(${zoneExpr('pane', fromSection)})[${fromIndex}];
  const zone = ${zoneExpr('pane', toSection)};
  const dt = new DataTransfer();
  src.dispatchEvent(new DragEvent('dragstart', {bubbles: true, dataTransfer: dt}));
  const active = document.querySelector('#atrium-root').classList.contains('dragactive');

  const rows = kids(${zoneExpr('pane', toSection)});
  const box = zone.getBoundingClientRect();
  const y = '${edge}' === 'bottom'
      ? (rows.length ? rows[rows.length - 1].getBoundingClientRect().bottom - 2
                     : box.bottom - 2)
      : (rows.length ? rows[0].getBoundingClientRect().top + 2 : box.top + 2);

  zone.dispatchEvent(new DragEvent('dragover', {bubbles: true, dataTransfer: dt, clientY: y}));
  const lit = zone.classList.contains('dropinto');
  zone.dispatchEvent(new DragEvent('drop', {bubbles: true, dataTransfer: dt, clientY: y}));
  src.dispatchEvent(new DragEvent('dragend', {bubbles: true, dataTransfer: dt}));
  // rowsAtDrop is what the destination held when the drop landed - the only
  // count the expected slot can be checked against, because the list has
  // already changed by the time the next push arrives.
  // The cross-window tear-off tracking clears itself on every
  // dragend, unrelated to what this probe checks.
  return {active, lit, rowsAtDrop: rows.length,
          sent: window.__sent.filter((m) => m[0] !== 'sidebarTabDragMoved')};
})()`);

console.log(`pinned : ${JSON.stringify(await titlesIn('pinned'))}`);
console.log(`tabs   : ${JSON.stringify(await titlesIn('tabs'))}`);
console.log(`favs   : ${await favCount()}\n`);

const tabsBefore = await titlesIn('tabs');
const pinnedBefore = await titlesIn('pinned');
if (!tabsBefore || tabsBefore.length < 3) {
  console.error('fixture needs at least 3 regular tabs');
  process.exit(2);
}

// FIRST-PIN MODE. An empty section renders no rows, so without a revealed
// strip there is nothing on screen to drop onto and the first pin can never be
// made by dragging - the same shape of bug as a placeholder folder that is not
// drawn until it already has contents.
if (!pinnedBefore || !pinnedBefore.length) {
  check('an empty pinned section still offers a drop target',
      await ev(`!!${PANE}.querySelector('[data-dropzone="pinned"]')`), true);
  // The empty-list fallback is the space name row itself now (buildSpaceName),
  // always laid out - there is no separate strip to reveal.
  check('that target is laid out at rest, not revealed on dragstart', await ev(
      `getComputedStyle(${PANE}.querySelector('[data-dropzone="pinned"]')).display`), 'flex');

  const first = await dragTo('tabs', 1, 'pinned');
  check('a drag still marks the root active', first.active, true);
  check('dropping on it pins into the empty list',
      first.sent, [['sidebarTabPinned', [1, 0]]]);
  await settle();
  check('the tab is now pinned', await titlesIn('pinned'), [tabsBefore[1]]);
  check('the section now has its own fold body holding the row', await ev(
      `!!${PANE}.querySelector('.foldin[data-dropzone="pinned"]')`), true);

  ws.close();
  console.log(`\n${failures ? failures + ' FAILED' : 'all checks passed (first-pin mode)'}`);
  process.exit(failures ? 1 : 0);
}

if (pinnedBefore.length < 2) {
  console.error('cross-section mode needs >=2 pinned tabs (or 0 for first-pin mode)');
  process.exit(2);
}

// 1. Pin: a regular row dropped at the top of the pinned list.
const pin = await dragTo('tabs', 1, 'pinned');
check('a drag marks the root active so hidden targets appear', pin.active, true);
check('pinning sends the pin message with an unadjusted slot',
    pin.sent, [['sidebarTabPinned', [1, 0]]]);
await settle();
check('the pinned list gained the row at the top',
    await titlesIn('pinned'), [tabsBefore[1], ...pinnedBefore]);
check('the regular list lost it',
    (await titlesIn('tabs')).includes(tabsBefore[1]), false);

// 2. Unpin: straight back out, to the end of the regular list.
const unpin = await dragTo('pinned', 0, 'tabs', 'bottom');
check('unpinning sends the unpin message with the append slot',
    unpin.sent, [['sidebarTabUnpinned', [0, unpin.rowsAtDrop]]]);
await settle();
check('the pinned list is back to what it was',
    await titlesIn('pinned'), pinnedBefore);
check('the row is back in the regular list',
    (await titlesIn('tabs')).includes(tabsBefore[1]), true);

// 3. Reorder inside the pinned list - a different array in the model, so a
//    different message. Sent as a plain reorder it would move a regular tab.
const pinnedMove = await dragTo('pinned', 1, 'pinned');
check('a pinned reorder sends its own message',
    pinnedMove.sent, [['sidebarPinnedReordered', [1, 0]]]);
await settle();
check('the pinned list reordered',
    await titlesIn('pinned'), [pinnedBefore[1], pinnedBefore[0], ...pinnedBefore.slice(2)]);

// 4. Favourite: the grid lights up as one target rather than showing a line.
const favsBefore = await favCount();
const fav = await dragTo('tabs', 0, 'favorites');
check('the favourites grid lights up as a whole', fav.lit, true);
check('promoting sends the favourite message with its section',
    fav.sent, [['sidebarRowFavorited', [0, 'tabs']]]);
await settle();
check('the favourites grid gained a tile', await favCount(), favsBefore + 1);

// 5. Nothing is left behind for the next gesture.
check('no drag state survives the drop', await ev(
    `document.querySelectorAll('.dragging, .dropinto, #atrium-root.dragactive').length`), 0);

ws.close();
console.log(`\n${failures ? failures + ' FAILED' : 'all checks passed'}`);
process.exit(failures ? 1 : 0);
