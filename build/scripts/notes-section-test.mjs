#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Reaches the Notes section through the real /overview route, same pattern
// as overview-rail-test.mjs - not a test-built host.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import os from 'node:os';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');
const OVERVIEW_JS = path.join(ATRIUM_DIR, 'overview.js');

// overview.js keeps per-module state, so each test needs its own instance -
// same cache-busting query as downloads-section-test.mjs.
let instance = 0;

function freshNotesPane() {
  const dom = installDOM({pathname: '/sidebar'});
  const root = dom.document.createElement('div');
  root.setAttribute('id', 'atrium-root');
  dom.document.body.append(root);
  return {dom, root};
}

async function openNotesSection(root) {
  // through renderOverview, the way buildRail reaches it. The
  // /overview route this used to import went with the retired widget.
  const {renderOverview} = await import(`${OVERVIEW_JS}?route=${instance++}`);
  renderOverview(root);
  root.querySelector('[data-ovsec="notes"]').click();
  return root.querySelector('.ovpane');
}

const SPACE_ENTRY = {
  id: 'note-1',
  spaceID: 's1',
  spaceIndex: 0,
  spaceName: 'Work',
  spaceIcon: 'W',
  spaceAccent: '#335577',
  title: 'Notes — Work',
  body: 'remember the thing',
  updatedAt: 10,
};

const CUSTOM_ENTRY = {
  id: 'note-2',
  title: 'Grocery list',
  body: 'eggs milk bread',
  updatedAt: 20,
};

test('one entry per space pad and one per freestanding note, each ' +
         'with a preview',
     async () => {
       const {dom, root} = freshNotesPane();
       const pane = await openNotesSection(root);

       dom.window.atriumSetNotesList([SPACE_ENTRY, CUSTOM_ENTRY]);

       const rows = pane.querySelectorAll('.dlrow');
       // First row is the "+ New Note" header, matching the lab's list shape.
       assert.equal(rows.length, 3);
       assert.equal(rows[1].querySelector('.dln').textContent, 'Notes — Work');
       assert.match(rows[1].querySelector('.shq').textContent,
           /remember the thing/);
       assert.equal(rows[2].querySelector('.dln').textContent, 'Grocery list');
       assert.match(rows[2].querySelector('.shq').textContent,
           /eggs milk bread/);
     });

test('a space pad entry draws a ph: icon as a Phosphor svg, not the literal ' +
         'string',
     async () => {
       const {dom, root} = freshNotesPane();
       const pane = await openNotesSection(root);
       dom.window.atriumSetNotesList(
           [{...SPACE_ENTRY, spaceIcon: 'ph:star'}]);

       const svg = pane.querySelectorAll('.dlrow')[1]
           .querySelector('.dlico').firstChild;
       assert.equal(svg.tagName.toLowerCase(), 'svg');
       assert.equal(svg.dataset.icon, 'star');
     });

test('+ New Note sends notesListCreate immediately', async () => {
  const {dom, root} = freshNotesPane();
  const pane = await openNotesSection(root);
  dom.window.atriumSetNotesList([]);

  pane.querySelector('[data-notes-new]').click();
  assert.deepEqual(dom.sent.at(-1), ['notesListCreate', []]);
});

test('mounting the section asks for a fresh push - overviewReady\'s own ' +
         'push ran before this section registered atriumSetNotesList',
     async () => {
       const {dom, root} = freshNotesPane();
       await openNotesSection(root);
       assert.deepEqual(dom.sent.at(-1), ['notesListReady', []]);
     });

test('clicking Open sends notesListOpen with the entry\'s id and ' +
         'space index',
     async () => {
       const {dom, root} = freshNotesPane();
       const pane = await openNotesSection(root);
       dom.window.atriumSetNotesList([SPACE_ENTRY, CUSTOM_ENTRY]);

       pane.querySelector('[data-notes-open="note-1"]').click();
       assert.deepEqual(dom.sent.at(-1), ['notesListOpen', ['note-1', 0]]);

       pane.querySelector('[data-notes-open="note-2"]').click();
       assert.deepEqual(dom.sent.at(-1), ['notesListOpen', ['note-2', -1]],
           'a freestanding note has no space index');
     });

test('x removes from the list without deleting; Option-x deletes ' +
         'behind a confirmation',
     async () => {
       const {dom, root} = freshNotesPane();
       const pane = await openNotesSection(root);
       dom.window.atriumSetNotesList([CUSTOM_ENTRY]);

       const removeBtn = pane.querySelector('[data-notes-remove="note-2"]');
       removeBtn.dispatchEvent(new Event('click', {altKey: false}));
       assert.deepEqual(dom.sent.at(-1), ['notesListDismiss', ['note-2']]);

       const confirmCalls = [];
       dom.window.confirm = (message) => {
         confirmCalls.push(message);
         return true;
       };
       removeBtn.dispatchEvent(new Event('click', {altKey: true}));
       assert.equal(confirmCalls.length, 1,
           'Option-click asks before deleting');
       assert.deepEqual(dom.sent.at(-1), ['notesListDelete', ['note-2']]);

       const sentBefore = dom.sent.length;
       dom.window.confirm = () => false;
       removeBtn.dispatchEvent(new Event('click', {altKey: true}));
       assert.equal(dom.sent.length, sentBefore,
           'declining the confirmation deletes nothing');
     });

test('the list renders exactly what it is given - the empty-body drop ' +
         'rule for space pads lives in the Rust ABI, not here',
     async () => {
       const {dom, root} = freshNotesPane();
       const pane = await openNotesSection(root);

       dom.window.atriumSetNotesList(
           [{...CUSTOM_ENTRY, id: 'note-3', body: ''}]);
       assert.equal(pane.querySelectorAll('.dlrow').length, 2,
           'a blank freestanding note still renders');
     });

test('the section is bounded and disposes its window seam', async () => {
  const {dom, root} = freshNotesPane();
  await openNotesSection(root);
  const {renderNotesSection} =
      await import(`${ATRIUM_DIR}/notes-section.js?direct=${instance++}`);

  const {root: sectionRoot, dispose} = renderNotesSection();
  assert.equal(sectionRoot.className, 'ovlist',
      'shares the scrollable, bounded container downloads uses');
  assert.equal(typeof dom.window.atriumSetNotesList, 'function');

  dispose();
  assert.equal(dom.window.atriumSetNotesList, undefined,
      'dispose must unhook the window seam');
});
