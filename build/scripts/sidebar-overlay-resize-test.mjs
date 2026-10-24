#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the reveal panel's width handle, drawn by the page.

//   node --test build/scripts/sidebar-overlay-resize-test.mjs

// A handle wired to no message is dead, and one wired to a name
// kSurfaceMessages does not carry is FATAL in the browser process.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');
const h = installDOM({pathname: '/sidebar'});
const {buildOverlayResizer, installOverlayResizer} =
    await import(path.join(ATRIUM_DIR, 'sidebar-overlay-resize.js'));

const REGISTERED = new Set(
    fs.readFileSync(
          path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
          'utf8')
        .split('kSurfaceMessages[] = {')[1]
        .split('};')[0]
        .match(/"[^"]+"/g)
        .map((quoted) => quoted.slice(1, -1)));

const css =
    fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar-overlay-resize.css'), 'utf8');

function handle() {
  h.reset();
  const sent = [];
  const el = buildOverlayResizer({send: (name, args) => sent.push([name, args])});
  document.body.appendChild(el);
  return {el, sent};
}

// The shim's pointer capture is a set, which is all the module asks of it.
function pointer(el, type, screenX, id = 1) {
  const event = new Event(type, {bubbles: true});
  event.button = 0;
  event.pointerId = id;
  event.screenX = screenX;
  el.dispatchEvent(event);
}

test('both messages the handle sends are registered natively', () => {
  for (const name of ['sidebarOverlayWidthLive', 'sidebarOverlayWidthCommit']) {
    assert.ok(REGISTERED.has(name),
              `${name} is not in kSurfaceMessages, so it is fatal`);
  }
});

test('a drag sends live ticks and one commit, in that order', () => {
  const {el, sent} = handle();
  document.documentElement.clientWidth = 220;
  pointer(el, 'pointerdown', 500);
  pointer(el, 'pointermove', 540);
  pointer(el, 'pointermove', 560);
  pointer(el, 'pointerup', 560);
  assert.deepEqual(sent, [
    ['sidebarOverlayWidthLive', [260]],
    ['sidebarOverlayWidthLive', [280]],
    ['sidebarOverlayWidthCommit', [280]],
  ]);
});

// screenX, not clientX: the panel's frame moves under the pointer as it grows,
// so a client-space delta measures the drag against a ruler the drag is moving.
test('the width comes from the screen delta, not the moving viewport', () => {
  const {el, sent} = handle();
  document.documentElement.clientWidth = 220;
  pointer(el, 'pointerdown', 500);
  document.documentElement.clientWidth = 260;  // the panel grew under it
  pointer(el, 'pointermove', 540);
  assert.deepEqual(sent, [['sidebarOverlayWidthLive', [260]]]);
});

test('docked right, dragging the other way widens', () => {
  const {el, sent} = handle();
  document.documentElement.classList.add('atrium-sidebar-on-right');
  document.documentElement.clientWidth = 220;
  pointer(el, 'pointerdown', 500);
  pointer(el, 'pointermove', 460);
  assert.deepEqual(sent, [['sidebarOverlayWidthLive', [260]]]);
  document.documentElement.classList.remove('atrium-sidebar-on-right');
});

test('a move with no press sends nothing', () => {
  const {el, sent} = handle();
  pointer(el, 'pointermove', 540);
  assert.deepEqual(sent, []);
});

test('the handle stays hidden until the sidebar floats', () => {
  assert.match(css, /\.ovresize\s*\{[^}]*display:\s*none/);
  assert.match(css, /\.atrium-sidebar-floating \.ovresize\s*\{[^}]*display:\s*block/);
  assert.match(css, /\.ovresize\s*\{[^}]*cursor:\s*col-resize/);
});

// a 2px full-height line beside the native seam's 4px
// inset pill read as a different control, not the same one.
test('the grip mark copies the native seam pill', () => {
  const band = css.match(/\.ovresize\s*\{[^}]*\}/)[0];
  assert.match(band, /--grip-mark:\s*4px/);
  assert.match(band, /--grip-inset:\s*var\(--ov-radius-card/);
  const mark = css.match(/\.ovresize::after\s*\{[^}]*\}/)[0];
  assert.match(mark, /width:\s*var\(--grip-mark\)/);
  assert.match(mark, /top:\s*var\(--grip-inset\)/);
  assert.match(mark, /border-radius:\s*calc\(var\(--grip-mark\)\s*\/\s*2\)/);
  // Offset from the seam edge rather than centred in the band: the gutter can
  // be the narrower of the two, and centring then put the mark against the
  // panel's own card.
  assert.match(band, /--grip-offset:\s*calc\([^;]*--grip-mark/);
  assert.match(mark, /right:\s*var\(--grip-offset\)/);
  const flipped = css.match(
      /#atrium-root\.aionright \.ovresize::after\s*\{[^}]*\}/)[0];
  assert.match(flipped, /left:\s*var\(--grip-offset\)/);
});

test('a second install replaces the first rather than orphaning it', () => {
  h.reset();
  const root = document.createElement('div');
  document.body.appendChild(root);
  installOverlayResizer(root);
  installOverlayResizer(root);
  assert.equal(document.body.querySelectorAll('.ovresize').length, 1);
});
