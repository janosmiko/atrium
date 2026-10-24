#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Put-on-Shelf is not built yet - not covered here.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import os from 'node:os';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');
const OVERVIEW_JS = path.join(ATRIUM_DIR, 'overview.js');

// overview.js keeps per-module state, so each test needs its own instance -
// same cache-busting query as notes-section-test.mjs.
let instance = 0;

function freshShelfPane() {
  const dom = installDOM({pathname: '/sidebar'});
  const root = dom.document.createElement('div');
  root.setAttribute('id', 'atrium-root');
  dom.document.body.append(root);
  return {dom, root};
}

async function openShelfSection(root) {
  // through renderOverview, the way buildRail reaches it. The
  // /overview route this used to import went with the retired widget.
  const {renderOverview} =
      await import(`${OVERVIEW_JS}?route=${instance++}`);
  renderOverview(root);
  root.querySelector('[data-ovsec="shelf"]').click();
  return root.querySelector('.ovpane');
}

const PAGE_ENTRY = {
  id: 'shelf-1',
  kind: 'page',
  title: 'Example Domain',
  url: 'https://example.com/',
  spaceID: 's1',
  spaceName: 'Work',
  addedAt: 10,
  highlights: ['first quote', 'second quote', 'third quote'],
};

const CLIP_ENTRY = {
  id: 'shelf-2',
  kind: 'clip',
  title: 'A clipped link',
  url: 'https://example.org/',
  addedAt: 20,
  highlights: [],
};

test('each item shows its source space and at most two highlight ' +
         'quotes',
     async () => {
       const {dom, root} = freshShelfPane();
       const pane = await openShelfSection(root);

       dom.window.atriumSetShelf([PAGE_ENTRY, CLIP_ENTRY]);

       const rows = pane.querySelectorAll('.dlrow');
       // First row is the item-count header, matching the lab's list shape.
       assert.equal(rows.length, 3);
       assert.equal(rows[1].querySelector('.dln').textContent,
           'Example Domain');
       assert.match(rows[1].querySelector('.dlcap').textContent, /Work/);
       const quotes = rows[1].querySelectorAll('.shq');
       assert.equal(quotes.length, 2, 'capped at two quotes even with three');
       assert.match(quotes[0].textContent, /first quote/);
       assert.match(quotes[1].textContent, /second quote/);

       assert.equal(rows[2].querySelector('.dln').textContent,
           'A clipped link');
       assert.match(rows[2].querySelector('.dlcap').textContent,
           /removed space/, 'no spaceName falls back the way the lab does');
     });

test('Open sends shelfListOpen with the entry\'s id and url',
     async () => {
       const {dom, root} = freshShelfPane();
       const pane = await openShelfSection(root);
       dom.window.atriumSetShelf([PAGE_ENTRY]);

       pane.querySelector('[data-shelf-open="shelf-1"]').click();
       assert.deepEqual(dom.sent.at(-1),
           ['shelfListOpen', ['shelf-1', 'https://example.com/']]);
     });

test('Remove sends shelfListRemove with the entry\'s id', async () => {
  const {dom, root} = freshShelfPane();
  const pane = await openShelfSection(root);
  dom.window.atriumSetShelf([PAGE_ENTRY]);

  pane.querySelector('[data-shelf-remove="shelf-1"]').click();
  assert.deepEqual(dom.sent.at(-1), ['shelfListRemove', ['shelf-1']]);
});

test('Copy as Markdown writes one link per item, with quotes ' +
         'nested under it',
     async () => {
       const {dom, root} = freshShelfPane();
       const pane = await openShelfSection(root);
       dom.window.atriumSetShelf([PAGE_ENTRY, CLIP_ENTRY]);

       pane.querySelector('[data-shelf-copy-markdown]').click();

       assert.equal(dom.clipboard.length, 1);
       assert.equal(
           dom.clipboard[0],
           '- [Example Domain](https://example.com/)\n' +
               '  - "first quote"\n' +
               '  - "second quote"\n' +
               '  - "third quote"\n' +
               '- [A clipped link](https://example.org/)');
     });

test('a title with `](` and a quote with a newline export without ' +
         'breaking the link syntax',
     async () => {
       const {shelfToMarkdown} =
           await import(`${ATRIUM_DIR}/shelf-section.js?direct=${instance++}`);

       const md = shelfToMarkdown([{
         id: 'hostile',
         title: 'Evil](https://attacker.example/)[Reopened',
         url: 'https://example.com/)(injected',
         highlights: ['line one\nline two'],
       }]);

       assert.equal(
           md,
           '- [Evil\\](https://attacker.example/)\\[Reopened]' +
               '(https://example.com/%29%28injected)\n' +
               '  - "line one line two"');
     });

test('mounting the section asks for a fresh push - overviewReady\'s own ' +
         'push ran before this section registered atriumSetShelf',
     async () => {
       const {dom, root} = freshShelfPane();
       await openShelfSection(root);
       assert.deepEqual(dom.sent.at(-1), ['shelfListReady', []]);
     });

test('an empty Shelf shows the lab\'s own empty-state copy', async () => {
  const {dom, root} = freshShelfPane();
  const pane = await openShelfSection(root);
  dom.window.atriumSetShelf([]);

  assert.equal(pane.querySelectorAll('.dlrow').length, 0);
  assert.match(pane.querySelector('.ovempty').textContent, /Shelf is empty/);
});

test('the section is bounded and disposes its window seam', async () => {
  const {dom, root} = freshShelfPane();
  await openShelfSection(root);
  const {renderShelfSection} =
      await import(`${ATRIUM_DIR}/shelf-section.js?direct=${instance++}`);

  const {root: sectionRoot, dispose} = renderShelfSection();
  assert.equal(sectionRoot.className, 'ovlist',
      'shares the scrollable, bounded container downloads/notes use');
  assert.equal(typeof dom.window.atriumSetShelf, 'function');

  dispose();
  assert.equal(dom.window.atriumSetShelf, undefined,
      'dispose must unhook the window seam');
});
