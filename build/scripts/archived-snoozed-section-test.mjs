#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The auto-archive and snooze triggers do not exist yet - these lists never
// grow in this build, so the tests below only prove the reader.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');
const OVERVIEW_JS = path.join(ATRIUM_DIR, 'overview.js');

// overview.js keeps per-module state, so each test needs its own instance -
// same cache-busting query as shelf-section-test.mjs.
let instance = 0;

function freshOverviewPane() {
  const dom = installDOM({pathname: '/sidebar'});
  const root = dom.document.createElement('div');
  root.setAttribute('id', 'atrium-root');
  dom.document.body.append(root);
  return {dom, root};
}

// The sidebar's snapshot carries this, so the rows the sections draw resolve
// their icons against the slot's presenter rather than the window's profile.
const SCOPE = 'testscope';

async function openSection(root, sectionId) {
  // through renderOverview, the way buildRail reaches it. The
  // /overview route this used to import went with the retired widget.
  const {renderOverview} = await import(`${OVERVIEW_JS}?route=${instance++}`);
  renderOverview(root);
  const {setIconScope} = await import(path.join(ATRIUM_DIR, 'icon-scope.js'));
  setIconScope(SCOPE);
  root.querySelector(`[data-ovsec="${sectionId}"]`).click();
  return root.querySelector('.ovpane');
}

const ARCHIVED_IN_SPACE = {
  id: 'arch-1',
  url: 'https://example.com/',
  title: 'Example',
  spaceID: 's1',
  spaceIndex: 0,
  spaceName: 'Work',
  archivedAt: 10,
  reason: 'userClosed',
};

const ARCHIVED_REMOVED_SPACE = {
  id: 'arch-2',
  url: 'https://gone.example/',
  title: 'Gone',
  archivedAt: 20,
  reason: 'archived',
};

const SNOOZED_IN_SPACE = {
  id: 'snz-1',
  url: 'https://example.com/',
  title: 'Example',
  spaceID: 's1',
  spaceIndex: 0,
  spaceName: 'Work',
  wakeAt: 100,
  alert: true,
};

test('an archived row shows its space, and Restore sends the id, ' +
         'url and space index',
     async () => {
       const {dom, root} = freshOverviewPane();
       const pane = await openSection(root, 'archived');
       dom.window.atriumSetArchived([ARCHIVED_IN_SPACE]);

       const rows = pane.querySelectorAll('.dlrow');
       assert.equal(rows.length, 1);
       assert.equal(rows[0].querySelector('.dln').textContent, 'Example');
       assert.match(rows[0].querySelector('.dlcap').textContent, /Work/);

       rows[0].querySelectorAll('.dlact')[0].click();
       assert.deepEqual(dom.sent.at(-1),
           ['archivedListRestore', ['arch-1', 'https://example.com/', 0]]);
     });

test('an archived row shows its close reason and time, and ' +
         'its icon comes from the URL-keyed favicon cache',
     async () => {
       const {dom, root} = freshOverviewPane();
       const pane = await openSection(root, 'archived');
       dom.window.atriumSetArchived([ARCHIVED_IN_SPACE]);

       const row = pane.querySelector('.dlrow');
       assert.match(row.querySelector('.dlcap').textContent, /Closed/);

       const img = row.querySelector('.dlico img');
       assert.equal(
           img.src, '/urlicon/' + SCOPE + '/' +
               encodeURIComponent(ARCHIVED_IN_SPACE.url) + '.png');
     });

test('the TTL sweep and a window close each render their own ' +
         'reason text',
     async () => {
       const {dom, root} = freshOverviewPane();
       const pane = await openSection(root, 'archived');
       dom.window.atriumSetArchived([
         {...ARCHIVED_IN_SPACE, id: 'arch-3', reason: 'archived'},
         {...ARCHIVED_IN_SPACE, id: 'arch-4', reason: 'windowClosed'},
       ]);

       const caps = [...pane.querySelectorAll('.dlcap')].map((c) => c.textContent);
       assert.ok(caps.some((c) => c.includes('Archived')));
       assert.ok(caps.some((c) => c.includes('Window closed')));
     });

test('a removed space falls back to the lab\'s own placeholder',
     async () => {
       const {dom, root} = freshOverviewPane();
       const pane = await openSection(root, 'archived');
       dom.window.atriumSetArchived([ARCHIVED_REMOVED_SPACE]);

       assert.match(
           pane.querySelector('.dlcap').textContent, /removed space/);

       pane.querySelectorAll('.dlact')[0].click();
       assert.deepEqual(dom.sent.at(-1),
           ['archivedListRestore', ['arch-2', 'https://gone.example/', -1]]);
     });

test('delete forever asks for confirmation, then sends archivedListDelete',
     async () => {
       const {dom, root} = freshOverviewPane();
       const pane = await openSection(root, 'archived');
       dom.window.atriumSetArchived([ARCHIVED_IN_SPACE]);

       dom.window.confirm = () => true;
       pane.querySelectorAll('.dlact')[1].click();
       assert.deepEqual(dom.sent.at(-1), ['archivedListDelete', ['arch-1']]);
     });

test('delete forever sends nothing when the confirmation is declined',
     async () => {
       const {dom, root} = freshOverviewPane();
       const pane = await openSection(root, 'archived');
       dom.window.atriumSetArchived([ARCHIVED_IN_SPACE]);

       dom.window.confirm = () => false;
       const before = dom.sent.length;
       pane.querySelectorAll('.dlact')[1].click();
       assert.equal(dom.sent.length, before);
     });

test('mounting the Archived section asks for a fresh push - ' +
         'overviewReady\'s own push ran before this section registered ' +
         'atriumSetArchived',
     async () => {
       const {dom, root} = freshOverviewPane();
       await openSection(root, 'archived');
       assert.deepEqual(dom.sent.at(-1), ['archivedListReady', []]);
     });

test('an empty Archived list shows the lab\'s own empty-state copy',
     async () => {
       const {dom, root} = freshOverviewPane();
       const pane = await openSection(root, 'archived');
       dom.window.atriumSetArchived([]);

       assert.equal(pane.querySelectorAll('.dlrow').length, 0);
       assert.match(
           pane.querySelector('.ovempty').textContent, /Nothing archived/);
     });

test('a snoozed row shows its space, wake time and alert flag, ' +
         'and Wake sends the id, url, space index, title and alert flag ' +
         '(the receiver has nothing to show without them)',
     async () => {
       const {dom, root} = freshOverviewPane();
       const pane = await openSection(root, 'snoozed');
       dom.window.atriumSetSnoozed([SNOOZED_IN_SPACE]);

       const rows = pane.querySelectorAll('.dlrow');
       assert.equal(rows.length, 1);
       const cap = rows[0].querySelector('.dlcap').textContent;
       assert.match(cap, /Work/);
       assert.match(cap, /alert set/);

       rows[0].querySelectorAll('.dlact')[0].click();
       assert.deepEqual(dom.sent.at(-1), [
         'snoozedListWake',
         ['snz-1', 'https://example.com/', 0, 'Example', true]
       ]);
     });

test('Delete sends snoozedListDelete after confirmation', async () => {
  const {dom, root} = freshOverviewPane();
  const pane = await openSection(root, 'snoozed');
  dom.window.atriumSetSnoozed([SNOOZED_IN_SPACE]);

  dom.window.confirm = () => true;
  pane.querySelectorAll('.dlact')[1].click();
  assert.deepEqual(dom.sent.at(-1), ['snoozedListDelete', ['snz-1']]);
});

test('mounting the Snoozed section asks for a fresh push - ' +
         'overviewReady\'s own push ran before this section registered ' +
         'atriumSetSnoozed',
     async () => {
       const {dom, root} = freshOverviewPane();
       await openSection(root, 'snoozed');
       assert.deepEqual(dom.sent.at(-1), ['snoozedListReady', []]);
     });

test('an empty Snoozed list shows the lab\'s own empty-state copy',
     async () => {
       const {dom, root} = freshOverviewPane();
       const pane = await openSection(root, 'snoozed');
       dom.window.atriumSetSnoozed([]);

       assert.equal(pane.querySelectorAll('.dlrow').length, 0);
       assert.match(
           pane.querySelector('.ovempty').textContent, /Nothing snoozed/);
     });

test('the sections are bounded and dispose their window seams', async () => {
  const {dom, root} = freshOverviewPane();
  await openSection(root, 'archived');
  const {renderArchivedSection, renderSnoozedSection} = await import(
      `${ATRIUM_DIR}/archived-snoozed-section.js?direct=${instance++}`);

  const archived = renderArchivedSection();
  assert.equal(archived.root.className, 'ovlist');
  assert.equal(typeof dom.window.atriumSetArchived, 'function');
  archived.dispose();
  assert.equal(dom.window.atriumSetArchived, undefined);

  const snoozed = renderSnoozedSection();
  assert.equal(snoozed.root.className, 'ovlist');
  assert.equal(typeof dom.window.atriumSetSnoozed, 'function');
  snoozed.dispose();
  assert.equal(dom.window.atriumSetSnoozed, undefined);
});

// Cmd+Shift+A builds the whole list as the Overview glides in, and 200 rows
// each drew and fetched an icon inside that glide.
test('a long list lays out and fetches only the rows on screen', async () => {
  const {dom, root} = freshOverviewPane();
  const pane = await openSection(root, 'archived');
  dom.window.atriumSetArchived([ARCHIVED_IN_SPACE]);
  const img = pane.querySelector('.dlrow img');
  assert.equal(img.getAttribute('loading'), 'lazy');

  const css = fs.readFileSync(path.join(ATRIUM_DIR, 'overview.css'), 'utf8');
  const row = /(^|\n)\.dlrow\s*{([^}]*)}/.exec(css);
  assert.match(row[2], /content-visibility:\s*auto/);
  // Without a size, a skipped row counts as zero high and the list scrolls
  // to nothing.
  assert.match(row[2], /contain-intrinsic-size:\s*auto\s+\d+px/);
});
