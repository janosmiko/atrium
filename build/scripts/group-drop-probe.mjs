#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// dropping a tab row on a folder puts it in that group.
//
//   ./build/scripts/vm-cdp.sh && node build/scripts/group-drop-probe.mjs
//
// Needs a space with at least one group and two ungrouped rows.
//
// The drop is synthesised. A real pointer cannot be driven past dragstart in
// this surface, so make drag-input-probe covers the part synthetic events
// cannot see - that a drag survives dragstart at all - and this asserts the
// PERSISTED membership rather than the DOM.
import {visibleSidebar, checker, wait, PANE} from './sidebar-probe-lib.mjs';
import {readModel, settle} from './vm-model.mjs';

const {ev, ws} = await visibleSidebar();
const {check, done} = checker();

const zones = await ev(
    `[...${PANE}.querySelectorAll('[data-dropzone^="folder:"]')].map((z) => z.dataset.dropzone)`);
if (!zones.length) {
  console.error('fixture needs a space with at least one group');
  process.exit(2);
}
const groupID = zones[0].slice('folder:'.length);
console.log(`folder zone: ${groupID}`);

const before = readModel().spaces[0];
console.log(`members before: ${before.tabs.filter((t) => t.groupID).length}`);

const sent = await ev(`(() => {
  window.__sent = [];
  if (!chrome.send.__w) {
    const real = chrome.send.bind(chrome);
    chrome.send = (n, a) => { window.__sent.push([n, a]); return real(n, a); };
    chrome.send.__w = true;
  }
  const pane = ${PANE};
  const src = pane.querySelector('[data-dropzone="tabs"] > .tabrow');
  const folder = pane.querySelector('[data-dropzone^="folder:"]');
  const dt = new DataTransfer();
  src.dispatchEvent(new DragEvent('dragstart', {bubbles: true, dataTransfer: dt}));
  const b = folder.getBoundingClientRect();
  folder.dispatchEvent(new DragEvent('dragover', {bubbles: true, dataTransfer: dt,
      clientX: b.left + 4, clientY: b.top + 4}));
  folder.dispatchEvent(new DragEvent('drop', {bubbles: true, dataTransfer: dt,
      clientX: b.left + 4, clientY: b.top + 4}));
  src.dispatchEvent(new DragEvent('dragend', {bubbles: true, dataTransfer: dt}));
  return window.__sent.filter((m) => m[0] === 'sidebarTabGrouped');
})()`);
// The message grew a source section and a destination member slot. -1 is the
// declared default when nothing more specific was dropped at (sidebar.js's
// 'group' case).
check('the drop asks for that group', sent, [['sidebarTabGrouped', [0, groupID, 'tabs', -1]]]);

console.log('waiting for the debounced save...');
await settle();
const after = readModel().spaces[0];
const members = after.tabs.filter((t) => t.groupID === groupID);
check('a tab now claims membership', members.length >= 1, true);
console.log(`  members: ${JSON.stringify(after.tabs.map((t) => [t.title || '(live)', t.groupID ? 'grouped' : '-']))}`);

ws.close();
done();
