#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The grammar on an Atrium surface. vim-grammar-test.mjs owns the
// grammar itself; these cases are about reconciling it with a surface that
// already had keys of its own.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');
const GRAMMAR = fs.readFileSync(path.join(ATRIUM_DIR, 'vim-grammar.js'),
                                'utf8');

// atrium.html loads vim-grammar.js as a classic script before the modules, so
// the global is already there when the surface module runs.
async function load() {
  installDOM();
  delete globalThis.__atriumVimGrammar;
  new Function(GRAMMAR)();
  const mod = await import(
      path.join(ATRIUM_DIR, 'vim-surface.js') + '?t=' + Math.random());
  return mod;
}

function makeRoot(count) {
  const root = globalThis.document.createElement('div');
  for (let i = 0; i < count; i++) {
    const row = globalThis.document.createElement('div');
    row.className = 'row tabrow';
    row.dataset.tabId = 't' + i;
    root.appendChild(row);
  }
  globalThis.document.body.appendChild(root);
  return root;
}

function press(character) {
  const event = new Event('keydown', {bubbles: true});
  event.key = character;
  globalThis.document.dispatchEvent(event);
  return event;
}

async function install(count, {enabled = true} = {}) {
  const {installVimSurface} = await load();
  const root = makeRoot(count);
  const sent = [];
  const handle = installVimSurface({
    root,
    send: (name, args) => sent.push([name, args]),
    isEnabled: () => enabled,
  });
  return {root, sent, handle};
}

function cursorIndex(root) {
  const rows = [...root.querySelectorAll('.row.tabrow')];
  return rows.findIndex((r) => r.classList.contains('atrium-vim-cursor'));
}

test('j and k move the cursor down and up', async () => {
  const {root} = await install(4);
  press('j');
  assert.equal(cursorIndex(root), 1);
  press('j');
  assert.equal(cursorIndex(root), 2);
  press('k');
  assert.equal(cursorIndex(root), 1);
});

// A list has ends. Wrapping would move the cursor somewhere the user did not
// look, which is worse than not moving.
test('the cursor stops at both ends rather than wrapping', async () => {
  const {root} = await install(3);
  press('k');
  assert.equal(cursorIndex(root), 0);
  press('j');
  press('j');
  press('j');
  press('j');
  assert.equal(cursorIndex(root), 2);
});

test('gg and G reach the first and last row', async () => {
  const {root} = await install(5);
  press('G');
  assert.equal(cursorIndex(root), 4);
  press('g');
  press('g');
  assert.equal(cursorIndex(root), 0);
});

test('Enter activates the row the cursor is on', async () => {
  const {root, sent} = await install(3);
  press('j');
  press('Enter');
  assert.deepEqual(sent, [['sidebarTabClicked', ['t1']]]);
});

// With the toggle off the surface must behave exactly as it did before
// vim mode existed, which means no cursor and no message.
test('with vim mode off no key does anything', async () => {
  const {root, sent} = await install(3, {enabled: false});
  press('j');
  press('G');
  press('Enter');
  assert.equal(cursorIndex(root), -1);
  assert.deepEqual(sent, []);
});

// The real conflict. The sidebar handles no printable key of its own,
// but a rename field takes every key while it has focus.
test('a focused rename field keeps its own typing', async () => {
  const {root} = await install(3);
  press('j');
  assert.equal(cursorIndex(root), 1);
  const input = globalThis.document.createElement('input');
  globalThis.document.body.appendChild(input);
  input.focus();
  press('j');
  assert.equal(cursorIndex(root), 1, 'j reached the grammar while typing');
});

// Enter and Escape belong to the menus and the rename field, so vim must not
// take them from a control that has focus.
test('a focused control keeps Enter', async () => {
  const {root, sent} = await install(3);
  const button = globalThis.document.createElement('button');
  globalThis.document.body.appendChild(button);
  button.focus();
  press('Enter');
  assert.deepEqual(sent, []);
});

test('a browser verb is sent rather than run on the surface', async () => {
  const {sent} = await install(3);
  press('x');
  assert.deepEqual(sent, [['vimVerb', ['close-tab']]]);
});

test('a pair reaches the browser as one verb', async () => {
  const {sent} = await install(3);
  press('g');
  press('t');
  assert.deepEqual(sent, [['vimVerb', ['next-tab']]]);
});

// The conflict the survey found. ai-panel.js opens on a bare printable
// hotkey listening on the document, so a surface that swallowed every key vim
// has no verb for would kill it with vim mode on.
test('a key with no vim verb is left for whoever else claims it', async () => {
  const {sent} = await install(3);
  let reached = '';
  globalThis.document.addEventListener('keydown', (e) => {
    reached += e.key;
  });
  const backtick = press('`');
  const q = press('q');
  assert.equal(reached, '`q');
  assert.equal(backtick.defaultPrevented, false);
  assert.equal(q.defaultPrevented, false);
  assert.deepEqual(sent, []);
});

// The other half: a key vim DOES own must not reach the surface's own
// listeners, or j would both move the cursor and do whatever else j does.
test('a key vim owns is taken from the other listeners', async () => {
  await install(3);
  const event = press('j');
  assert.equal(event.defaultPrevented, true);
});

// The AI panel is one long answer, so j scrolls it rather than moving a
// cursor over rows it does not have.
test('a reading surface scrolls instead of cursoring', async () => {
  const {installVimSurface} = await load();
  const root = makeRoot(0);
  const log = globalThis.document.createElement('div');
  log.scrollTop = 0;
  log.scrollHeight = 900;
  root.appendChild(log);
  installVimSurface({root, send: () => {}, isEnabled: () => true,
                     scroller: () => log});
  press('j');
  assert.equal(log.scrollTop, 60);
  press('k');
  assert.equal(log.scrollTop, 0);
  press('G');
  assert.equal(log.scrollTop, 900);
  press('g');
  press('g');
  assert.equal(log.scrollTop, 0);
});

// Enter activates a row. A reading surface has none, so it must not swallow
// the key the composer needs to send a message.
test('a reading surface leaves Enter alone', async () => {
  const {installVimSurface} = await load();
  const root = makeRoot(0);
  const log = globalThis.document.createElement('div');
  installVimSurface({root, send: () => {}, isEnabled: () => true,
                     scroller: () => log});
  const event = press('Enter');
  assert.equal(event.defaultPrevented, false);
});

// Hint verbs too. A surface has no frames, so it allocates its
// own labels from zero rather than asking the browser for a slice.
test('f labels every row and a label activates it', async () => {
  const {root, sent} = await install(3);
  press('f');
  const labels = [...root.querySelectorAll('.atrium-vim-hint')]
      .map((el) => el.textContent);
  assert.deepEqual(labels, ['a', 's', 'd']);
  press('s');
  assert.deepEqual(sent, [['sidebarTabClicked', ['t1']]]);
  assert.equal(root.querySelectorAll('.atrium-vim-hint').length, 0);
});

// While hints are up every key is a label, so a motion key must not also
// move the cursor underneath them.
test('a hint label is not also a motion', async () => {
  const {root} = await install(4);
  press('f');
  press('d');
  assert.equal(cursorIndex(root), -1, 'd moved the cursor as well as picking');
});

// Escape leaves hint mode with nothing picked, and the rows come back clean.
test('Escape clears the hints without activating', async () => {
  const {root, sent} = await install(3);
  press('f');
  press('Escape');
  assert.equal(root.querySelectorAll('.atrium-vim-hint').length, 0);
  assert.deepEqual(sent, []);
});

// The load-order trap: a surface whose grammar never loaded looks exactly
// like vim mode being off, so assert the global rather than assume it.
test('the surface module finds the grammar global', async () => {
  await load();
  assert.ok(globalThis.__atriumVimGrammar);
  assert.equal(typeof globalThis.__atriumVimGrammar.makeGrammar, 'function');
});
