#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

// The three joined in the order BuildVimPopupInstallScript joins them: the
// grammar publishes the global vim-page reads, and vim-popup drives vim-page.
const SOURCE = ['vim-grammar.js', 'vim-page.js', 'vim-popup.js']
                   .map((name) => fs.readFileSync(
                            path.join(ATRIUM_DIR, name), 'utf8'))
                   .join('\n');

function loadPopup() {
  const h = installDOM();
  delete globalThis.__atriumVimGrammar;
  delete globalThis.window.__atriumVim;
  delete globalThis.window.__atriumVimPopup;
  globalThis.window.scrollBy = () => {};
  globalThis.window.innerHeight = 400;
  new Function(SOURCE)();
  return h;
}

function addButton(label) {
  const button = globalThis.document.createElement('button');
  button.textContent = label;
  button.setRect({left: 10, top: 10, width: 60, height: 20});
  globalThis.document.body.appendChild(button);
  return button;
}

function press(key) {
  const event = new globalThis.Event('keydown', {bubbles: true, key});
  globalThis.document.dispatchEvent(event);
  return event;
}

const labels = () =>
    [...globalThis.document.body.querySelectorAll('.atrium-vim-hint')]
        .map((el) => el.textContent);

test('f labels the controls inside the popup', () => {
  loadPopup();
  addButton('One');
  addButton('Two');

  press('f');
  assert.deepEqual(labels(), ['a', 's']);
});

test('the popup allocates its own labels from the start', () => {
  // Not a slice of the page beneath: the user is looking at the popup, and two
  // sequences sharing a keyspace is the bug the cross-frame total prevents.
  loadPopup();
  addButton('Only');
  press('f');
  assert.deepEqual(labels(), ['a']);
});

test('a label activates the control it sits on', () => {
  loadPopup();
  const first = addButton('One');
  const second = addButton('Two');
  const clicked = [];
  first.addEventListener('click', () => clicked.push('one'));
  second.addEventListener('click', () => clicked.push('two'));

  press('f');
  press('s');
  assert.deepEqual(clicked, ['two']);
  assert.deepEqual(labels(), [], 'the labels come down once one is picked');
});

test('Escape leaves hint mode without activating anything', () => {
  loadPopup();
  const button = addButton('One');
  const clicked = [];
  button.addEventListener('click', () => clicked.push('one'));

  press('f');
  press('Escape');
  assert.deepEqual(labels(), []);
  assert.deepEqual(clicked, []);
});

test("a key the popup does not claim is left for the extension", () => {
  loadPopup();
  addButton('One');

  // defaultPrevented, not "did my listener run": the bridge and the
  // extension's own handler sit on the same node, so a listener firing proves
  // nothing about whether the key was claimed.
  assert.equal(press('j').defaultPrevented, false,
               'claiming every key would break the extension shortcuts');
  assert.equal(press('x').defaultPrevented, false);
  assert.equal(press('f').defaultPrevented, true, 'f is the one it does claim');
});

test('a focused field types rather than labelling', () => {
  loadPopup();
  addButton('One');
  const field = globalThis.document.createElement('input');
  globalThis.document.body.appendChild(field);
  globalThis.document.activeElement = field;

  press('f');
  assert.deepEqual(labels(), [], 'f in a text field is the letter f');
});

test('the bridge installs once, however many times the script is evaluated', () => {
  loadPopup();
  const first = globalThis.window.__atriumVimPopup;
  new Function(SOURCE)();
  assert.equal(globalThis.window.__atriumVimPopup, first,
               'a second listener would run every verb twice');
});

test('dispose takes the labels and the listener away', () => {
  loadPopup();
  addButton('One');
  press('f');
  assert.equal(labels().length, 1, 'setup');

  globalThis.window.__atriumVimPopup.dispose();
  assert.deepEqual(labels(), []);
  press('f');
  assert.deepEqual(labels(), [], 'the listener outlived the popup');
});
