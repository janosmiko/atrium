#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The two phase fill behind the address text, docked and floating.
//
//   node --test build/scripts/load-fill-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

const h = installDOM({pathname: '/sidebar'});

const {createLoadFill} = await import(path.join(ATRIUM_DIR, 'load-fill.js'));

function host() {
  h.reset();
  const row = h.document.createElement('div');
  row.className = 'urlrow';
  const input = h.document.createElement('input');
  input.className = 'urlbar';
  row.appendChild(input);
  h.body.appendChild(row);
  return row;
}

test('the fill goes in first so the text paints over it', () => {
  const row = host();
  const fill = createLoadFill(row);
  assert.equal(row.childNodes[0], fill.element);
  assert.equal(fill.element.className, 'loadfill');
});

test('waiting spans the field and carries the breathing class', () => {
  const fill = createLoadFill(host());
  fill.setPhase('waiting', 0);
  assert.equal(fill.element.style.width, '100%');
  assert.equal(fill.element.classList.contains('waiting'), true);
  assert.equal(fill.element.classList.contains('receiving'), false);
});

test('receiving sets the width to the fraction', () => {
  const fill = createLoadFill(host());
  fill.setPhase('waiting', 0);
  fill.setPhase('receiving', 0.4);
  assert.equal(fill.element.style.width, '40%');
  assert.equal(fill.element.classList.contains('receiving'), true);
  assert.equal(fill.element.classList.contains('waiting'), false);
});

test('a lower fraction never shrinks the fill', () => {
  // Chromium resets its own progress on a mid-load navigation, and a bar
  // travelling backward is what the old native bar avoided too.
  const fill = createLoadFill(host());
  fill.setPhase('waiting', 0);
  fill.setPhase('receiving', 0.6);
  fill.setPhase('receiving', 0.1);
  assert.equal(fill.element.style.width, '60%');
});

test('done fills the field and hands over to the fade', () => {
  const fill = createLoadFill(host());
  fill.setPhase('waiting', 0);
  fill.setPhase('receiving', 0.3);
  fill.setPhase('done', 1);
  assert.equal(fill.element.style.width, '100%');
  assert.equal(fill.element.classList.contains('done'), true);
  assert.equal(fill.element.classList.contains('receiving'), false);
});

test('a new load during the fade starts a fresh waiting phase', () => {
  const fill = createLoadFill(host());
  fill.setPhase('receiving', 0.8);
  fill.setPhase('done', 1);
  fill.setPhase('waiting', 0);
  assert.equal(fill.element.classList.contains('done'), false);
  assert.equal(fill.element.classList.contains('waiting'), true);
  fill.setPhase('receiving', 0.05);
  assert.equal(fill.element.style.width, '5%',
               'the old run carried its max into the new one');
});

test('sidebar.css carries the phase looks the design settled on', () => {
  const css = fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar.css'), 'utf8');
  assert.match(css, /\.loadfill\s*{[^}]*background:\s*rgb\(var\(--atrium-ink\)/);
  assert.match(css, /\.loadfill\.receiving\s*{[^}]*opacity:\s*\.2/);
  assert.match(css,
      /\.loadfill\.receiving\s*{[^}]*transition:\s*width \.25s ease-out/);
  assert.match(css, /\.loadfill\.done\s*{[^}]*transition:\s*opacity \.26s/);
  assert.match(css, /animation:\s*loadbreathe 1\.2s/);
  // The keyframes live in overlay.css, not here. Two sheets each defining
  // loadbreathe were loaded into one document until they were consolidated,
  // and the losing copy was the one the eye actually saw.
  const overlay = fs.readFileSync(path.join(ATRIUM_DIR, 'overlay.css'), 'utf8');
  assert.match(overlay, /@keyframes loadbreathe\s*{[^@]*\.14[^@]*\.26/s);
  assert.ok(!/@keyframes loadbreathe/.test(css),
            'sidebar.css must not define a second loadbreathe');
});

test('the field clips the fill to its own rounded box', () => {
  const css = fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar.css'), 'utf8');
  const row = /\n\.urlrow\s*{([^}]*)}/.exec(css);
  assert.ok(row, 'the .urlrow rule is gone');
  assert.match(row[1], /position:\s*relative/);
  assert.match(row[1], /overflow:\s*hidden/);
  assert.match(row[1], /border-radius:\s*8px/);
});

// The insecure plate on .urlbar is opaque, so a fill painted under it never
// shows. It must sit above the input and never eat the input's clicks/typing.
test('the fill paints above the input and never blocks it', () => {
  const css = fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar.css'), 'utf8');
  const fill = /\n\.loadfill\s*{([^}]*)}/.exec(css);
  assert.ok(fill, 'the .loadfill rule is gone');
  assert.match(fill[1], /pointer-events:\s*none/);
  const fillZ = /z-index:\s*(-?\d+)/.exec(fill[1]);
  assert.ok(fillZ, '.loadfill carries no z-index');
  const urlbarZ = /\.urlbar\s*{[^}]*z-index:\s*(-?\d+)/.exec(css);
  assert.ok(Number(fillZ[1]) > 0, '.loadfill needs a positive z-index');
  assert.ok(!urlbarZ || Number(fillZ[1]) > Number(urlbarZ[1]),
             '.loadfill must outrank any z-index on .urlbar');
});
