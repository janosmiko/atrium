#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');

const h = installDOM({pathname: '/find'});
const {installFind} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/find.js'));

const REGISTERED = new Set(
    fs.readFileSync(
          path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
          'utf8')
        .split('kSurfaceMessages[] = {')[1]
        .split('};')[0]
        .match(/"[^"]+"/g)
        .map((quoted) => quoted.slice(1, -1)));

const root = document.createElement('div');
document.body.appendChild(root);
installFind(root);
// Read before any test runs, since a later one moves the keyboard around.
const INSTALL_FOCUS = document.activeElement;
const READY = h.sent.splice(0, h.sent.length);

const field = () => root.querySelector('.fb-input');
const count = () => root.querySelector('.fb-count').textContent;
const key = (name, init = {}) => field().dispatchEvent(
    new Event('keydown', {bubbles: true, key: name, ...init}));
const type = (text) => {
  field().value = text;
  field().dispatchEvent(new Event('input', {bubbles: true}));
};

test('install announces the renderer is live', () => {
  assert.deepEqual(READY, [['findReady', []]]);
});

test('install hands the keyboard to the field', () => {
  assert.equal(
      INSTALL_FOCUS, field(),
      'the field never took focus, so the first open types into nothing');
});

test('typing asks the browser to search', () => {
  h.sent.length = 0;
  type('atrium');
  assert.deepEqual(h.sent, [['findQueryChanged', ['atrium']]]);
});

test('an emptied field still reports, so the browser can clear', () => {
  h.sent.length = 0;
  type('');
  assert.deepEqual(h.sent, [['findQueryChanged', ['']]]);
});

test('a result reads n of total', () => {
  window.atriumSetFindResult({active: 3, total: 12});
  assert.equal(count(), '3 of 12');
});

test('no match reads 0 of 0, not an empty label', () => {
  window.atriumSetFindResult({active: 0, total: 0});
  assert.equal(count(), '0 of 0');
});

test('a pending result draws nothing rather than a wrong number', () => {
  window.atriumSetFindResult({active: -1, total: -1});
  assert.equal(count(), '');
});

test('Enter and Shift+Enter walk the matches', () => {
  h.sent.length = 0;
  key('Enter');
  key('Enter', {shiftKey: true});
  assert.deepEqual(h.sent, [['findNext', []], ['findPrevious', []]]);
});

test('Cmd+G and Cmd+Shift+G walk them too, because the menu never sees them',
     () => {
       h.sent.length = 0;
       key('g', {metaKey: true});
       key('g', {metaKey: true, shiftKey: true});
       assert.deepEqual(h.sent, [['findNext', []], ['findPrevious', []]]);
     });

test('an IME commit does not walk the matches', () => {
  h.sent.length = 0;
  assert.ok(key('Enter', {isComposing: true}), 'the commit was swallowed');
  assert.ok(
      key('Enter', {isComposing: true, shiftKey: true}),
      'the commit was swallowed');
  assert.deepEqual(h.sent, []);
});

test('Tab and the clipboard chords reach the field untouched', () => {
  h.sent.length = 0;
  assert.ok(key('Tab'), 'Tab was swallowed');
  assert.ok(key('a', {metaKey: true}), 'Cmd+A was swallowed');
  assert.ok(key('c', {metaKey: true}), 'Cmd+C was swallowed');
  assert.deepEqual(h.sent, []);
});

test('Escape closes', () => {
  h.sent.length = 0;
  key('Escape');
  assert.deepEqual(h.sent, [['findClose', []]]);
});

test('the three buttons send the same three verbs', () => {
  h.sent.length = 0;
  for (const cls of ['.fb-prev', '.fb-next', '.fb-close']) {
    root.querySelector(cls).dispatchEvent(new Event('click', {bubbles: true}));
  }
  assert.deepEqual(
      h.sent, [['findPrevious', []], ['findNext', []], ['findClose', []]]);
});

test('a reset refills the field and selects it', () => {
  window.atriumResetFind({query: 'seed'});
  assert.equal(field().value, 'seed');
  assert.equal(field().selectionStart, 0);
  assert.equal(field().selectionEnd, 4);
});

test('a reset takes the keyboard back to the field', () => {
  field().blur();
  window.atriumResetFind({query: 'seed'});
  assert.equal(
      document.activeElement, field(),
      'the field stayed blurred, so a reopened bar types into nothing');
});

test('a second install leaves one bar that still sends the verbs', () => {
  installFind(root);
  assert.equal(root.querySelectorAll('.fb').length, 1);
  h.sent.length = 0;
  key('Enter');
  root.querySelector('.fb-close').dispatchEvent(
      new Event('click', {bubbles: true}));
  assert.deepEqual(h.sent, [['findNext', []], ['findClose', []]]);
});

test('every name it sends is registered in kSurfaceMessages', () => {
  const names = new Set(h.sent.map((m) => m[0]).concat(READY.map((m) => m[0])));
  assert.notEqual(names.size, 0);
  for (const name of names) {
    assert.ok(REGISTERED.has(name), `${name} is not in kSurfaceMessages`);
  }
});

test('the module never uses innerHTML', () => {
  const file = path.join(SRC, 'chrome/browser/resources/atrium/find.js');
  assert.doesNotMatch(
      fs.readFileSync(file, 'utf8'), /\.innerHTML\s*=|innerHTML\s*\+=/);
});
