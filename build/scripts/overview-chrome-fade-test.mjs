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
const CSS = fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar.css'), 'utf8');

const h = installDOM({pathname: '/sidebar'});
const {applyOverviewFade, overviewShare} =
    await import(path.join(ATRIUM_DIR, 'sidebar.js'));
const {renderOverview} = await import(path.join(ATRIUM_DIR, 'overview.js'));

// The bar and the strip are siblings of the root, not children of it: both are
// hoisted so a swipe cannot carry them off.
function host() {
  h.reset();
  const outer = document.createElement('div');
  h.body.appendChild(outer);
  const root = document.createElement('div');
  root.setAttribute('id', 'atrium-root');
  const strip = document.createElement('div');
  strip.className = 'exs-row';
  const bar = document.createElement('div');
  bar.className = 'urlrow';
  const rail = document.createElement('div');
  rail.className = 'rail';
  const overview = document.createElement('div');
  overview.className = 'railpane ovrailpane';
  const pane = document.createElement('div');
  pane.className = 'railpane';
  rail.append(overview, pane);
  root.append(bar, rail);
  outer.append(strip, root);
  return {root, bar, strip, overview, pane};
}

test('the share follows the rail across the Overview pane', () => {
  assert.equal(overviewShare(0), 1, 'parked on the Overview');
  assert.equal(overviewShare(0.4), 0.6);
  assert.equal(overviewShare(1), 0, 'the first space');
  assert.equal(overviewShare(2.5), 0, 'nothing past the first space fades it');
});

test('a rail dragged past the Overview does not overshoot the fade', () => {
  assert.equal(overviewShare(-0.5), 1);
});

test('the crossing is one number on each surface it fades', () => {
  const {root, strip, bar, overview, pane} = host();
  applyOverviewFade(root, 0.25);

  for (const el of [strip, bar, overview, pane]) {
    assert.equal(el.style.getPropertyValue('--ov-share'), '0.25', el.className);
  }
  // Written on the shared parent, it restyled every row of the sidebar on
  // every frame of the glide.
  assert.equal(strip.parentNode.style.getPropertyValue('--ov-share'), '');
  // Nothing inline on the surfaces themselves. That is the point - an inline
  // value outranks every stylesheet rule, including the ones that hide them.
  assert.ok(!bar.style.opacity);
  assert.ok(!strip.style.opacity);
  assert.ok(!strip.style.transform);
});

test('the crossing is marked while it is running and not after', () => {
  const {root, strip} = host();
  const outer = strip.parentNode;

  applyOverviewFade(root, 0.25);
  assert.ok(outer.classList.contains('ovcross'));

  applyOverviewFade(root, 0);
  assert.ok(!outer.classList.contains('ovcross'),
            'the crossing rules outlived the crossing');
  assert.equal(strip.style.getPropertyValue('--ov-share'), '0');
});

test('the share reaches no element below the surfaces', () => {
  const prop = /@property\s+--ov-share\s*{([^}]*)}/.exec(CSS);
  assert.ok(prop, '--ov-share is not registered');
  assert.match(prop[1], /inherits:\s*false/);
  assert.match(prop[1], /initial-value:\s*0/);
});

test('past halfway the surfaces take no clicks', () => {
  const {root, strip} = host();
  const outer = strip.parentNode;

  applyOverviewFade(root, 0.6);
  assert.ok(outer.classList.contains('ovpast'));

  applyOverviewFade(root, 0.4);
  assert.ok(!outer.classList.contains('ovpast'), 'before halfway it is still the bar');
});

test('a hidden strip stays hidden through a whole crossing', () => {
  const {root, strip} = host();
  strip.classList.add('exs-hidden');
  for (const share of [0, 0.25, 0.6, 1, 0.4, 0]) {
    applyOverviewFade(root, share);
  }

  // No guard in applyOverviewFade does this any more. It writes nothing to the
  // strip at all, so exs-hidden simply wins in the cascade the way it always
  // should have.
  assert.ok(!strip.style.transform);
  assert.ok(!strip.style.opacity);
  assert.ok(!strip.style.pointerEvents);
});

test('every surface of the crossing scales and fades off the one number', () => {
  const rule = /\.ovcross[^{]*{([^}]*)}/.exec(CSS);
  assert.ok(rule, 'no crossing rule in the stylesheet');

  const sel = CSS.slice(CSS.indexOf('.ovcross'), CSS.indexOf('.ovcross') + 400);
  for (const surface of ['.urlrow', '.exs-row', '.favgrid', '.railpane']) {
    assert.ok(sel.includes(surface), surface + ' is left out of the crossing');
  }
  // The scale property rather than transform: swipe.js writes the favourites
  // grid's slide inline, and an inline transform beats any stylesheet rule.
  assert.match(rule[1], /(^|\s)scale:\s*calc\([^)]*--ov-share/,
               'the outgoing side does not scale off the share');
  assert.match(rule[1], /opacity:\s*calc\([^)]*--ov-share/,
               'the outgoing side does not fade off the share');
  // While the swipe drives these frame by frame, a transition trails the finger
  // by its own duration.
  assert.match(rule[1], /transition:\s*none/);
  // A layer per surface, or each frame repaints the rows under it.
  assert.match(rule[1], /will-change:\s*scale,\s*opacity/);
});

test('the Overview grows in as the space shrinks away', () => {
  const rule = /\.ovcross\s+\.ovrailpane\s*{([^}]*)}/.exec(CSS);
  assert.ok(rule, 'the Overview pane has no crossing rule');
  assert.match(rule[1], /(^|\s)scale:\s*calc\([^)]*--ov-share/);
  assert.match(rule[1], /opacity:\s*var\(--ov-share/);
  assert.match(rule[1], /transition:\s*none/);
  assert.match(rule[1], /will-change:\s*scale,\s*opacity/);
});

test('the crossing outranks the rules it has to beat', () => {
  // #atrium-root.sbroot > .urlrow carries a .16s opacity transition with a .2s
  // delay. A bare .ovcross .urlrow loses to it and the bar trails the swipe by
  // over a third of a second.
  assert.ok(CSS.includes('.ovcross #atrium-root.sbroot > .urlrow'),
            'the crossing rule cannot outrank the docked address row');
  assert.ok(CSS.includes('.ovcross #atrium-root.sbroot > .favgrid'),
            'the crossing rule cannot outrank the docked favourites grid');
});

test('the Overview pane reserves room for the traffic lights only', () => {
  const pane = /(^|\n)\.ovrailpane\s*{([^}]*)}/.exec(CSS);
  assert.ok(pane, 'no rule for .ovrailpane');
  const top = /padding:\s*(\d+)px/.exec(pane[2]);
  assert.ok(top, 'the pane reserves no top room');
  assert.equal(Number(top[1]), 46,
               'the address row is gone here, so its 34px belongs to the pane');
});

test('the pane names the section it is showing', () => {
  h.reset();
  const root = document.createElement('div');
  root.setAttribute('id', 'atrium-root');
  h.body.appendChild(root);
  renderOverview(root);

  const head = root.querySelector('.ovhead');
  assert.ok(head, 'the pane draws no header');
  root.querySelector('[data-ovsec="archived"]').click();
  assert.equal(head.textContent, 'Archived');

  root.querySelector('[data-ovsec="notes"]').click();
  assert.equal(head.textContent, 'Notes');
});

test('the section content sits under the header, not beside it', () => {
  h.reset();
  const root = document.createElement('div');
  root.setAttribute('id', 'atrium-root');
  h.body.appendChild(root);
  renderOverview(root);

  const head = root.querySelector('.ovhead');
  const pane = root.querySelector('.ovpane');
  assert.equal(head.parentElement, pane.parentElement,
               'the header and the pane have to share the column');
  assert.equal(head.nextElementSibling, pane);
});
