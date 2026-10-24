#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// drives the real atrium.js entry point against the real
// #atrium-root, unlike the sibling suites which pass a div they built.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

test('atrium.js\'s import graph reaches both Skylight modules', () => {
  const atriumSrc = fs.readFileSync(path.join(ATRIUM_DIR, 'atrium.js'), 'utf8');
  const wiringMatch = atriumSrc.match(/from '\.\/([\w-]+)\.js'/g) || [];
  const wiringFiles = wiringMatch.map((m) => m.match(/'\.\/([\w-]+)\.js'/)[1]);
  const wiring = wiringFiles.find((f) => {
    const src = fs.readFileSync(path.join(ATRIUM_DIR, `${f}.js`), 'utf8');
    return /from '\.\/skylight-day-cycle\.js'/.test(src) &&
        /from '\.\/skylight-ambient\.js'/.test(src);
  });
  assert.ok(wiring,
      'no file atrium.js imports (directly or one hop away) imports both ' +
      'skylight-day-cycle.js and skylight-ambient.js');
});

installDOM({pathname: '/sidebar'});

// This is what atrium.html actually provides - not a host built for
// Skylight's benefit - and nothing below touches it again directly.
const root = document.createElement('div');
root.setAttribute('id', 'atrium-root');
document.body.append(root);

await import(path.join(ATRIUM_DIR, 'atrium.js'));

test('the day-cycle gradient reaches the real #atrium-root .bglayer, not a test host', () => {
  const found = document.getElementById('atrium-root');
  assert.equal(found, root, 'must be the element atrium.html provides');

  const layer = found.querySelector('.bglayer');
  assert.ok(layer, 'atrium.js\'s own install path must have created .bglayer');
  assert.ok(layer.style.background.includes('rgba(20, 24, 29, 0)'),
      'the real .bglayer must carry the day-cycle base gradient, painted ' +
      'by atrium.js calling into skylight-day-cycle.js - not by this test');
  assert.ok(!/#[0-9A-Fa-f]{6}|rgb\(/.test(layer.style.background),
      'every layer here must be transparent or rgba. An opaque ' +
      'colour covers the window backdrop the sidebar is meant to read ' +
      'through, and the reporter saw the whole column go black');
});

test('the ambient animation and the day-cycle gradient compose on the same element', () => {
  const layer = document.getElementById('atrium-root').querySelector('.bglayer');
  assert.ok(layer.style.background.includes('rgba(20, 24, 29, 0)'),
      'day-cycle\'s paint must still be present once ambient is also wired');
  assert.ok(layer.matches('.skyambient .bglayer'),
      'skylight-ambient.css\'s selector must actually match this element - ' +
      'the class and the background-painted node must be the ancestor/' +
      'descendant pair the CSS rule expects, not two unrelated elements');
});

// The live wiring runs a real setInterval (skylight-day-cycle.js's own
// 60s repaint clock) - clear it or the process never exits.
test.after(() => {
  // The root captured above: a later test's installDOM replaces the global
  // document, so a lookup through it finds nothing and the interval leaks.
  clearInterval(root.querySelector('.bglayer')?._skyClockHandle);
});

// the modules always composed every state and were handed pinned
// defaults, so what was missing was the state reaching them.

test('installSkylight forwards nightShift to the day cycle', async () => {
  const {installSkylight} =
      await import(path.join(ATRIUM_DIR, 'skylight.js'));
  const h = installDOM();
  const root = h.document.createElement('div');
  h.body.appendChild(root);

  // nightShift false means the sky layers do not paint, so the layer keeps the
  // flat base rather than a gradient stack over it.
  // The day cycle arms a real interval to repaint on the hour, which would
  // keep this process alive - the same handle rewind-bar-test clears.
  const paint = (nightShift) => {
    const layer = installSkylight(root, {nightShift});
    const background = layer.style.background;
    clearInterval(layer._skyClockHandle);
    delete layer._skyClockHandle;
    return background;
  };

  // 21:00, pinned because the day cycle reads the real clock and its hour-13
  // band paints no layers at all - Night Shift is then invisible either way and
  // this failed for an hour a day.
  const Real = globalThis.Date;
  globalThis.Date = class extends Real {
    constructor(...args) {
      super(...(args.length ? args : [2026, 0, 1, 21, 0, 0]));
    }
  };
  try {
    const flat = paint(false);
    const lit = paint(true);
    assert.notEqual(
        flat, lit,
        'nightShift did not reach the day cycle - both painted the same');
  } finally {
    globalThis.Date = Real;
  }
});

test('atrium.js exposes the receiver C++ pushes Skylight state through', () => {
  // PushSkylightToSidebar executes the literal window.atriumSetSkylight, so a
  // rename here loses the push with no error on either side.
  const atriumSrc = fs.readFileSync(path.join(ATRIUM_DIR, 'atrium.js'), 'utf8');
  assert.match(atriumSrc, /window\.atriumSetSkylight\s*=/);

  const cc = fs.readFileSync(
      path.join(SRC, 'chrome/browser/ui/atrium/atrium_browser_window.cc'), 'utf8');
  assert.match(cc, /atriumSetSkylight/);
});
