#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the Overview's Spaces section, which rendered a placeholder.
//
//   node --test build/scripts/spaces-section-test.mjs

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

const {renderSpacesSection} =
    await import(path.join(ATRIUM_DIR, 'spaces-section.js'));

const REGISTERED = new Set(
    fs.readFileSync(
          path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
          'utf8')
        .split('kSurfaceMessages[] = {')[1]
        .split('};')[0]
        .match(/"[^"]+"/g)
        .map((quoted) => quoted.slice(1, -1)));

const SPACES = [
  {name: 'Work', color: '#4488cc'},
  {name: 'Home', color: '#cc8844'},
];

function section(spaces = SPACES) {
  const {root, dispose} = renderSpacesSection();
  h.sent.length = 0;
  h.window.atriumSetOverviewSpaces(spaces);
  return {root, dispose};
}

const cards = (root) => root.querySelectorAll('.ovcard');

test('one card per space', () => {
  const {root} = section();
  assert.deepEqual(cards(root).map((c) => c.textContent), ['Work', 'Home']);
});

test('the Overview space is not a room you can jump to', () => {
  // The model may or may not carry it depending on the arm, so it is filtered
  // either way rather than assumed absent.
  const {root} = section([{name: 'Overview', overview: true}, ...SPACES]);
  assert.deepEqual(cards(root).map((c) => c.textContent), ['Work', 'Home']);
});

test('a card carries the space tint, which is what identifies it', () => {
  const {root} = section();
  assert.match(cards(root)[0].style.background, /#4488cc/);
});

test('clicking a card switches to that space by index', () => {
  const {root} = section();
  cards(root)[1].dispatchEvent(new Event('click', {bubbles: true}));
  // The same verb the sidebar dots and the swipe use, so the Overview cannot
  // switch spaces by a route of its own.
  assert.deepEqual(h.sent, [['sidebarSpaceSelected', [1]]]);
});

test('the index is the position AFTER the Overview space is filtered out', () => {
  // Sending the raw model index would jump to the wrong room whenever the arm
  // includes the Overview entry.
  const {root} = section([{name: 'Overview', overview: true}, ...SPACES]);
  cards(root)[1].dispatchEvent(new Event('click', {bubbles: true}));
  assert.deepEqual(h.sent, [['sidebarSpaceSelected', [1]]]);
});

// drag to reorder.
const drag = (card, type) => card.dispatchEvent(new Event(type, {bubbles: true}));

test('dropping one card on another reorders by sidebar position', () => {
  const {root} = section();
  const [work, home] = cards(root);
  drag(work, 'dragstart');
  drag(home, 'drop');
  assert.deepEqual(h.sent, [['sidebarSpaceReordered', [0, 1]]]);
});

test('a card dropped on itself sends nothing', () => {
  const {root} = section();
  const [work] = cards(root);
  drag(work, 'dragstart');
  drag(work, 'drop');
  assert.deepEqual(h.sent, []);
});

test('a drop with no drag behind it sends nothing', () => {
  // dragend clears the source, so a stray drop cannot reorder against whatever
  // the last drag happened to be.
  const {root} = section();
  const [work, home] = cards(root);
  drag(work, 'dragstart');
  drag(work, 'dragend');
  drag(home, 'drop');
  assert.deepEqual(h.sent, []);
});

test('the positions are the FILTERED ones, as the click sends', () => {
  // Raw model indices would reorder the wrong pair whenever the arm carries
  // the Overview entry.
  const {root} = section([{name: 'Overview', overview: true}, ...SPACES]);
  const [work, home] = cards(root);
  drag(work, 'dragstart');
  drag(home, 'drop');
  assert.deepEqual(h.sent, [['sidebarSpaceReordered', [0, 1]]]);
});

test('a space name lands as text, never as markup', () => {
  const hostile = '<img src=x onerror=alert(1)>';
  const {root} = section([{name: hostile, color: '#123456'}]);
  const card = cards(root)[0];
  assert.equal(card.textContent, hostile);
  assert.equal(card.children.length, 0);
});

test('with no spaces it says so rather than rendering nothing', () => {
  const {root} = section([]);
  assert.equal(cards(root).length, 0);
  assert.ok(root.querySelector('.ovempty'));
});

test('it asks for its list on mount, and that name is registered', () => {
  // The section mounts on a rail click, after the Overview's own ready push has
  // been and gone - without this it renders empty until something else fires.
  h.sent.length = 0;
  renderSpacesSection();
  assert.deepEqual(h.sent, [['spacesListReady', []]]);
  assert.ok(REGISTERED.has('spacesListReady'));
});

test('spaces-section.js is registered in atrium_resources.grd', () => {
  // An unregistered module is a blank surface at runtime, not a build error.
  const grd = fs.readFileSync(path.join(ATRIUM_DIR, 'atrium_resources.grd'), 'utf8');
  assert.match(grd, /file="spaces-section\.js"/);
});
