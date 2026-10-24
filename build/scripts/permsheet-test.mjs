#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the /permsheet route - the glass sheet a permission request
// shows, and the two verbs (Allow/Block) it can send back.
//
// Enter must NOT grant. The sheet steals focus the moment it appears,
// so a page that asks the user to hold Enter and then calls getUserMedia had the
// autorepeat land on a freshly focused sheet and grant camera and microphone. A
// key that grants has to be one the user aimed at the sheet.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');

const h = installDOM({pathname: '/permsheet'});

const {installPermissionSheet} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/permsheet.js'));

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
installPermissionSheet(root);

const READY = h.sent.splice(0, h.sent.length);

const click = (el) => el.dispatchEvent(new Event('click', {bubbles: true}));

test('install announces the renderer is live', () => {
  assert.deepEqual(READY, [['permissionSheetReady', []]]);
});

test('a pushed request renders the origin and every permission', () => {
  window.atriumSetPermissionSheet(
      {origin: 'https://example.com', permissions: ['access your location']});
  assert.equal(root.querySelector('.psorigin').textContent,
               'https://example.com');
  const rows = root.querySelectorAll('.pslist > div');
  assert.deepEqual(rows.map((n) => n.textContent), ['access your location']);
});

test('Allow sends permissionSheetAllow with no argument', () => {
  h.sent.length = 0;
  click(root.querySelector('.psallow'));
  assert.deepEqual(h.sent, [['permissionSheetAllow', []]]);
});

test('Block sends permissionSheetBlock with no argument', () => {
  h.sent.length = 0;
  click(root.querySelector('.psblock'));
  assert.deepEqual(h.sent, [['permissionSheetBlock', []]]);
});

test('every name it sends is registered in kSurfaceMessages', () => {
  // Drives both buttons itself. Reading whatever the earlier tests left in
  // h.sent misses Allow, because each of them clears the buffer first - a
  // mutant name on the Allow binding passed this check.
  h.sent.length = 0;
  click(root.querySelector('.psallow'));
  click(root.querySelector('.psblock'));
  const names = new Set(h.sent.map((m) => m[0]).concat(READY.map((m) => m[0])));
  assert.notEqual(names.size, 0);
  for (const name of names) {
    assert.ok(REGISTERED.has(name), `${name} is not in kSurfaceMessages`);
  }
});

// Dispatched on an element and bubbled: the sheet listens on `document`, and the
// shim's document takes listeners but has no dispatchEvent of its own.
const key = (name) =>
    root.dispatchEvent(new Event('keydown', {key: name, bubbles: true}));

test('Enter does not grant', () => {
  // The whole defect in one assertion. Enter used to send permissionSheetAllow
  // directly, and the sheet takes focus on show, so an in-flight autorepeat
  // decided it.
  h.sent.length = 0;
  key('Enter');
  assert.deepEqual(h.sent, [],
                   'Enter still reaches the browser from the sheet');
});

test('Escape still blocks', () => {
  // Blocking is the safe direction, so the escape hatch stays on the keyboard.
  h.sent.length = 0;
  key('Escape');
  assert.deepEqual(h.sent, [['permissionSheetBlock', []]]);
});

test('a click on Allow still grants', () => {
  // The negative above must not have been achieved by breaking the sheet.
  h.sent.length = 0;
  click(root.querySelector('.psallow'));
  assert.deepEqual(h.sent, [['permissionSheetAllow', []]]);
});

test('the sheet wears the shared classes', () => {
  window.atriumSetPermissionSheet(
      {origin: 'https://example.com', permissions: ['access your location']});
  assert.equal(root.querySelector('.psscrim').classList.contains('ov-scrim'),
               true);
  assert.equal(root.querySelector('.pscard').classList.contains('ov-card'),
               true);
  const allow = root.querySelector('.psallow');
  assert.equal(allow.classList.contains('ov-button'), true);
  assert.equal(allow.classList.contains('ov-primary'), true);
  const block = root.querySelector('.psblock');
  assert.equal(block.classList.contains('ov-button'), true);
  assert.equal(block.classList.contains('ov-primary'), false);
});
