#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the chip, the pop and the rebuild.

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
const {dotAt, installDotMagnify} = await import(
    path.join(ATRIUM_DIR, 'sidebar-dot-magnify.js'));
const {buildSpaceDots} = await import(path.join(ATRIUM_DIR, 'sidebar.js'));

const CSS = fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar.css'), 'utf8');

// Four dots 20px wide, so the centres land on 10, 30, 50 and 70.
function buildBar({activeIndex = -1} = {}) {
  h.reset();
  const bar = document.createElement('div');
  bar.className = 'spacedots';
  bar.setRect({left: 0, top: 0, width: 80, height: 26});
  // The bar scrolls when the dots outgrow it, and the pick reads through that.
  bar.scrollLeft = 0;
  ['Work', 'Play', 'Read', 'Code'].forEach((name, i) => {
    const dot = document.createElement('button');
    dot.className = i === activeIndex ? 'sdot active' : 'sdot';
    dot.dataset.init = name[0];
    dot.dataset.tint = '#336699';
    dot.setLayoutBox({left: i * 20, top: 0, width: 20, height: 26});
    const glyph = document.createElement('span');
    glyph.className = 'd';
    dot.appendChild(glyph);
    bar.appendChild(dot);
  });
  h.body.appendChild(bar);
  lastRefresh = installDotMagnify(bar);
  return bar;
}

let lastRefresh = null;

const magnified = (bar) =>
    [...bar.children].findIndex((d) => d.classList.contains('mag'));

function moveTo(bar, x) {
  bar.dispatchEvent(new Event('mousemove', {bubbles: true, clientX: x,
                                            clientY: 10}));
}

test('the dot under the cursor wears the chip and shows its initial', () => {
  const bar = buildBar();
  moveTo(bar, 52);

  assert.equal(magnified(bar), 2);
  assert.equal(bar.children[2].firstChild.textContent, 'R');
  assert.equal(bar.children[2].firstChild.style.background, '#336699');
});

// the active dot is a bare glyph, so the hover chip must not stay
// behind it, and its ink comes back.
test('leaving the active dot drops the chip fill and restores its ink', () => {
  const bar = buildBar({activeIndex: 1});
  const glyph = bar.children[1].firstChild;
  glyph.textContent = 'P';
  bar.children[1].dataset.ink = 'rgb(1, 2, 3)';
  glyph.style.color = 'rgb(1, 2, 3)';
  moveTo(bar, 32);
  assert.equal(glyph.style.color, '', 'the chip draws its own white initial');

  bar.dispatchEvent(new Event('mouseleave', {bubbles: true}));
  assert.equal(glyph.style.background, '');
  assert.equal(glyph.style.color, 'rgb(1, 2, 3)');
  assert.equal(glyph.textContent, 'P', 'the active glyph keeps its initial');
});

test('leaving the bar puts every dot back', () => {
  const bar = buildBar();
  moveTo(bar, 52);

  bar.dispatchEvent(new Event('mouseleave', {bubbles: true}));
  assert.equal(magnified(bar), -1);
  assert.equal(bar.children[2].firstChild.textContent, '',
               'the initial belongs to the chip, not to the dot');
});

test('moving on hands the chip over, and the dot left goes plain', () => {
  const bar = buildBar();
  moveTo(bar, 12);
  moveTo(bar, 72);

  assert.equal(magnified(bar), 3);
  assert.equal(bar.children[0].firstChild.textContent, '');
});

test('the active dot keeps its initial when the chip moves away', () => {
  const bar = buildBar({activeIndex: 0});
  bar.children[0].firstChild.textContent = 'W';
  moveTo(bar, 12);
  moveTo(bar, 72);

  // The demotion is a CSS rule on .active, so the text has to survive here or
  // the dot comes back blank when the cursor leaves.
  assert.equal(bar.children[0].firstChild.textContent, 'W');
});

test('the pop plays on a move', () => {
  const bar = buildBar();
  moveTo(bar, 12);
  assert.match(bar.children[0].firstChild.style.animation, /dotpop/);
});

test('a scroll re-magnifies silently', () => {
  const bar = buildBar();
  moveTo(bar, 12);
  bar.children[0].firstChild.style.animation = '';

  // The dots slide under a still cursor, so dot 1 is now the nearest.
  [...bar.children].forEach(
      (dot, i) => dot.setLayoutBox({left: i * 20 - 20, top: 0, width: 20,
                                    height: 26}));
  bar.dispatchEvent(new Event('scroll', {bubbles: true}));

  assert.equal(magnified(bar), 1, 'the scroll did not re-derive the chip');
  assert.ok(!bar.children[1].firstChild.style.animation,
            'popping each dot a scroll slides past reads as a stutter');
});

test('reduced motion keeps the chip and drops the pop', () => {
  h.setMatchMedia((query) => query.includes('reduced-motion'));
  const bar = buildBar();
  h.setMatchMedia((query) => query.includes('reduced-motion'));
  moveTo(bar, 12);

  assert.equal(magnified(bar), 0, 'the magnification is the information');
  assert.ok(!bar.children[0].firstChild.style.animation);
  h.setMatchMedia(() => false);
});

test('a rebuild re-derives the chip from where the cursor is', () => {
  const first = buildBar();
  moveTo(first, 52);

  const rebuilt = buildBar();
  const refresh = lastRefresh;
  document.elementFromPoint = () => rebuilt.children[2];
  refresh();

  assert.equal(magnified(rebuilt), 2,
               'fresh elements carry no hover, so the chip has to be re-derived');
  delete document.elementFromPoint;
});

test('a cursor that is not over the bar magnifies nothing', () => {
  const first = buildBar();
  moveTo(first, 52);

  const rebuilt = buildBar();
  const refresh = lastRefresh;
  document.elementFromPoint = () => h.body;
  refresh();

  assert.equal(magnified(rebuilt), -1);
  delete document.elementFromPoint;
});

test('the chip, the shove and the demotion are styled', () => {
  assert.match(CSS, /\.sdot\.mag \.d\s*{[^}]*border-radius/,
               'the chip is the dot grown into a rounded square');
  assert.match(CSS, /\.sdot\.mag ~ \.sdot\s*{[^}]*translateX/,
               'the dots after the chip do not step aside');
  assert.match(CSS, /\.sdot:has\(~ \.sdot\.mag\)\s*{[^}]*translateX/,
               'the dots before the chip do not step aside');
  assert.match(CSS, /\.spacedots:has\(\.sdot\.mag\) \.sdot\.active:not\(\.mag\):not\(\.ovdot\)/,
               'the active chip has to demote, and the Overview dot must not');
});

// the docked row centres its chips inside its own content box, so
// an asymmetric side padding moves the middle. --sb-pad-lead is 8px and
// --sb-pad-trail is 2px, which put the chips 3px right of the row's centre.
test('the docked dot row is padded the same on both sides', () => {
  const rule = CSS.match(/\.rail ~ \.spacedots\s*{([\s\S]*?)\n}/);
  assert.ok(rule, 'the docked dot row rule is gone');
  const padding = rule[1].match(/\n\s*padding:\s*([^;]+);/);
  assert.ok(padding, 'the docked dot row sets no padding at all');
  const sides = padding[1].trim().split(/\s+/);
  const [left, right] = sides.length === 4 ? [sides[3], sides[1]]
                                           : [sides[1], sides[1]];
  assert.equal(left, right,
               `left ${left} and right ${right} differ, so "justify-content: ` +
               'center" centres the chips somewhere other than the row');
});

test('reduced motion is honoured in the stylesheet too', () => {
  // The stylesheet carries several of these blocks, so the dots' own one has
  // to be picked out rather than the first that matches.
  const blocks =
      [...CSS.matchAll(/@media \(prefers-reduced-motion: reduce\)\s*{([\s\S]*?)\n}/g)]
          .map((m) => m[1]);
  assert.ok(blocks.some((b) => /\.sdot\.mag \.d/.test(b)),
            'nothing drops the pop when the reader asks for less motion');
});

// placeDotBloom appends .dotbloom to the same bar, and both the
// measure and the paint walked bar.children. The bloom has no .d child, so a
// pick that landed on it threw and left `magnified` holding its index - the
// next pass then returned early and the bar stayed dead.
function addBloom(bar) {
  const bloom = document.createElement('div');
  bloom.className = 'dotbloom';
  bloom.setLayoutBox({left: 0, top: 0, width: 26, height: 26});
  bar.appendChild(bloom);
  return bloom;
}

test('the bloom under the dots is not a dot the pick can land on', () => {
  const bar = buildBar({activeIndex: 0});
  addBloom(bar);

  moveTo(bar, 12);

  const dots = [...bar.querySelectorAll('.sdot')];
  assert.equal(dots.findIndex((d) => d.classList.contains('mag')), 0);
  assert.equal(dots[0].firstChild.textContent, 'W');
  assert.equal(bar.querySelector('.dotbloom').classList.contains('mag'), false);
});

test('a pass that crossed the bloom does not wedge the next one', () => {
  const bar = buildBar({activeIndex: 0});
  addBloom(bar);
  const dots = [...bar.querySelectorAll('.sdot')];

  // Over the bloom's own centre first, then away to a far dot. Before the fix
  // the first move threw and the second was a no-op.
  moveTo(bar, 13);
  moveTo(bar, 72);

  assert.equal(dots.findIndex((d) => d.classList.contains('mag')), 3);
  assert.equal(dots[3].firstChild.textContent, 'C');
});

// buildSpaceDots never gave the leading Overview button or the
// trailing + a data-init, so the chip painted an empty glyph back onto them.
function layoutFullBar(bar) {
  bar.setRect({left: 0, top: 0, width: 200, height: 26});
  bar.scrollLeft = 0;
  const scroller = bar.querySelector('.dotscroll');
  scroller.setRect({left: 0, top: 0, width: 200, height: 26});
  scroller.scrollLeft = 0;
  [...bar.querySelectorAll('.sdot')].forEach(
      (dot, i) => dot.setLayoutBox({left: i * 20, top: 0, width: 18, height: 26}));
}

test('hovering the leading Overview button keeps its :: glyph', () => {
  h.reset();
  const spaces = [{name: 'A', icon: 'H', color: '#336699'},
                  {name: 'B', color: '#993366'}];
  const bar = buildSpaceDots(spaces, 0, /*withLeadingOverview=*/ true);
  h.body.appendChild(bar);
  layoutFullBar(bar);

  const ov = bar.querySelector('.ovdot');
  assert.equal(ov.firstChild.textContent, '::');

  moveTo(bar, 9);
  assert.ok(ov.classList.contains('mag'), 'ovdot magnified');
  assert.equal(ov.firstChild.textContent, '::', 'chip shows the glyph while hovered');

  bar.dispatchEvent(new Event('mouseleave', {bubbles: true}));
  assert.equal(ov.firstChild.textContent, '::', 'glyph survives the hover ending');
});

test('hovering the trailing + keeps its glyph', () => {
  h.reset();
  const spaces = [{name: 'A', icon: 'H', color: '#336699'},
                  {name: 'B', color: '#993366'}];
  const bar = buildSpaceDots(spaces, 0, /*withLeadingOverview=*/ true);
  h.body.appendChild(bar);
  layoutFullBar(bar);

  const dots = [...bar.querySelectorAll('.sdot')];
  const add = bar.querySelector('.addws');
  const addIndex = dots.indexOf(add);
  assert.equal(add.firstChild.textContent, '+');

  moveTo(bar, addIndex * 20 + 9);
  assert.ok(add.classList.contains('mag'), 'addws magnified');
  assert.equal(add.firstChild.textContent, '+', 'chip shows the glyph while hovered');

  bar.dispatchEvent(new Event('mouseleave', {bubbles: true}));
  assert.equal(add.firstChild.textContent, '+', 'glyph survives the hover ending');
});

test('a dot settling back to rest does not glide through a grey pill', () => {
  const rest = CSS.match(/\n\.sdot \.d\s*{([\s\S]*?)\n}/);
  assert.ok(rest, 'the resting dot rule is gone');
  assert.doesNotMatch(rest[1], /transition:/,
      'a shrink back to rest must snap, not glide the grey fill through every size');

  const demoted = CSS.match(
      /\.spacedots:has\(\.sdot\.mag\) \.sdot\.active:not\(\.mag\):not\(\.ovdot\) \.d\s*{([\s\S]*?)\n}/);
  assert.ok(demoted, 'the demoted-active rule is gone');
  assert.match(demoted[1], /transition:\s*none/,
      'the demotion still carries .sdot.active .d\'s class and must not glide either');

  assert.match(
      CSS, /\.sdot\.active \.d,\n\.sdot\.mag \.d\s*{[^}]*transition:[^;]*width/,
      'magnifying and becoming active still glide to their own size');
});

// a per-space dot scrolled out of .dotscroll's visible
// box still has a real offsetLeft, so its raw centre can coincide with the
// button painted at that same page x - the clipped dot must not win there.
function layoutOverflowingBar(bar, scrollLeft) {
  bar.setRect({left: 0, top: 0, width: 150, height: 26});
  bar.scrollLeft = 0;
  bar.querySelector('.ovdot').setLayoutBox({left: 0, top: 0, width: 28, height: 26});
  const scroller = bar.querySelector('.dotscroll');
  scroller.setRect({left: 30, top: 0, width: 60, height: 26});
  scroller.scrollLeft = scrollLeft;
  [...scroller.querySelectorAll('.sdot')].forEach(
      (dot, i) => dot.setLayoutBox({left: i * 14, top: 0, width: 12, height: 26}));
  bar.querySelector('.addws').setLayoutBox({left: 92, top: 0, width: 28, height: 26});
}

// the hover chip used to read a pre-flattened dataset.init string
// straight into textContent, which for a ph: value painted the literal
// string rather than the Phosphor glyph.
test('hovering a dot with a ph: icon shows its Phosphor glyph', () => {
  h.reset();
  const spaces = [{name: 'A', icon: 'ph:star', color: '#336699'},
                  {name: 'B', color: '#993366'}];
  const bar = buildSpaceDots(spaces, -1, /*withLeadingOverview=*/ false);
  h.body.appendChild(bar);
  layoutFullBar(bar);

  moveTo(bar, 9);
  const dot = bar.querySelector('.sdot.mag');
  const svg = dot.firstChild.firstChild;
  assert.equal(svg.tagName.toLowerCase(), 'svg');
  assert.equal(svg.dataset.icon, 'star');
});

test('a dot scrolled under a button never out-hovers it', () => {
  h.reset();
  const spaces = ['A', 'B', 'C', 'D', 'E', 'F'].map((name) => ({name}));
  const bar = buildSpaceDots(spaces, -1, /*withLeadingOverview=*/ true);
  h.body.appendChild(bar);

  layoutOverflowingBar(bar, /*scrollLeft=*/ 0);
  // Dot 5's raw centre (106) lands exactly on the + painted there.
  moveTo(bar, 106);
  assert.ok(bar.querySelector('.sdot.mag')?.classList.contains('addws'),
            'a dot clipped out of the scroller must not beat the + under it');

  layoutOverflowingBar(bar, /*scrollLeft=*/ 34);
  // Dot 0's raw centre (2) is now closer to Overview than to any visible dot.
  bar.dispatchEvent(new Event('mousemove', {bubbles: true, clientX: 2, clientY: 10}));
  assert.ok(bar.querySelector('.sdot.mag')?.classList.contains('ovdot'),
            'a dot clipped out of the scroller must not beat Overview either');
});

test('dotAt returns the dot within the bloom-shift', () => {
  const bar = buildBar();
  assert.equal(dotAt(bar, 10 + 9), bar.children[0],
               'still the centre-10 dot at the far edge of the 9px shift');
});

test('dotAt is null on the bar padding and a real gap', () => {
  h.reset();
  const spaces = ['A', 'B', 'C', 'D', 'E', 'F'].map((name) => ({name}));
  const bar = buildSpaceDots(spaces, -1, /*withLeadingOverview=*/ true);
  h.body.appendChild(bar);
  layoutOverflowingBar(bar, /*scrollLeft=*/ 0);

  // Midway between Overview's centre (14) and dot0's (36): 11px from both.
  assert.equal(dotAt(bar, 25), null, 'the gap beside Overview is not a dot');
  // Past the + button's centre (106) and its 10px radius, into the bar's own
  // padding: a click there must not switch to the nearest dot.
  assert.equal(dotAt(bar, 130), null, 'the bar padding past + is not a dot');
});

test('dotAt never returns a dot clipped out of the scroller', () => {
  h.reset();
  const spaces = ['A', 'B', 'C', 'D', 'E', 'F'].map((name) => ({name}));
  const bar = buildSpaceDots(spaces, -1, /*withLeadingOverview=*/ true);
  h.body.appendChild(bar);

  layoutOverflowingBar(bar, /*scrollLeft=*/ 0);
  assert.ok(dotAt(bar, 106)?.classList.contains('addws'),
            'dot 5 is clipped out here, so 106 must resolve to the + under it');

  layoutOverflowingBar(bar, /*scrollLeft=*/ 34);
  // Dot 0's raw centre (2) is 12px from Overview's own (14) - past the 10px
  // radius, so null is the correct pick here. What matters is it is never
  // the clipped dot, the one thing a bare nearest-centre search would find.
  const clipped = bar.querySelectorAll('.dotscroll .sdot')[0];
  assert.notEqual(dotAt(bar, 2), clipped,
                   'dot 0 is clipped out here and must never win the pick');
});

test('a dot within the magnify overhang of the edge is skipped', () => {
  h.reset();
  const spaces = ['A', 'B', 'C', 'D', 'E', 'F'].map((name) => ({name}));
  const bar = buildSpaceDots(spaces, -1, /*withLeadingOverview=*/ true);
  h.body.appendChild(bar);

  layoutOverflowingBar(bar, /*scrollLeft=*/ 4);
  // Dot 0's centre (32) sits 2px inside the scroller's edge (30) - visible,
  // but its .mag glyph (20px in a 12px slot) would overhang the clip by 2px.
  bar.dispatchEvent(new Event('mousemove', {bubbles: true, clientX: 32, clientY: 10}));
  const mag = bar.querySelector('.sdot.mag');
  assert.equal(mag?.dataset.spaceIndex, '1',
               'a dot too close to the edge to fully show its chip must not win the pick');
});
