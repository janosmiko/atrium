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

const h = installDOM({pathname: '/peekctl'});

const {installPeekControls} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/peek-controls.js'));

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
installPeekControls(root);

const READY = h.sent.splice(0, h.sent.length);

const click = (el) => el.dispatchEvent(new Event('click', {bubbles: true}));

test('install renders the two buttons in order, with their marks and labels',
     () => {
       const buttons = root.querySelectorAll('.pkc .ov-btn');
       assert.equal(buttons.length, 2);
       assert.equal(buttons[0].classList.contains('pkc-close'), true);
       assert.equal(buttons[0].getAttribute('aria-label'), 'Close peek');
       assert.equal(
           buttons[0].querySelector('svg.ov-icon').dataset.icon, 'close');
       assert.equal(buttons[1].classList.contains('pkc-popout'), true);
       assert.equal(buttons[1].getAttribute('aria-label'), 'Open as a tab');
       assert.equal(
           buttons[1].querySelector('svg.ov-icon').dataset.icon, 'popout');
     });

test('install announces the renderer is live', () => {
  assert.deepEqual(READY, [['peekControlsReady', []]]);
});

test('Close sends peekControl with close', () => {
  h.sent.length = 0;
  click(root.querySelector('.pkc-close'));
  assert.deepEqual(h.sent, [['peekControl', ['close']]]);
});

test('Popout sends peekControl with popout', () => {
  h.sent.length = 0;
  click(root.querySelector('.pkc-popout'));
  assert.deepEqual(h.sent, [['peekControl', ['popout']]]);
});

test('every name it sends is registered in kSurfaceMessages', () => {
  h.sent.length = 0;
  click(root.querySelector('.pkc-close'));
  click(root.querySelector('.pkc-popout'));
  const names = new Set(h.sent.map((m) => m[0]).concat(READY.map((m) => m[0])));
  assert.notEqual(names.size, 0);
  for (const name of names) {
    assert.ok(REGISTERED.has(name), `${name} is not in kSurfaceMessages`);
  }
});

test('the module never uses innerHTML', () => {
  const file = path.join(
      SRC, 'chrome/browser/resources/atrium/peek-controls.js');
  assert.doesNotMatch(
      fs.readFileSync(file, 'utf8'), /\.innerHTML\s*=|innerHTML\s*\+=/);
});
