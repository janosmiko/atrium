#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// nearest-slot geometry for a drop in the favourites grid.
//
//   make grid-test
//
// Ported case for case from FavoritesGridDropTests.swift. The hysteresis half
// is the reason porting beat re-deriving: those rules were earned across
// several fixes for "icons constantly jumping around", and the last test
// pins the trap that cost a silent bug - the preview and the commit must
// resolve the SAME slot.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {nearestSlot, GRID_BOUNDARY_HYSTERESIS} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/sidebar-grid-drop.js'));

const box = (x, y, w, h) => ({left: x, top: y, right: x + w, bottom: y + h});

// a, b in row 0 (columns 0 and 1); c alone in row 1. 60px tiles, 6px gutters,
// mirroring the grid this feeds.
const threeTiles = [box(0, 0, 60, 40), box(66, 0, 60, 40), box(0, 46, 60, 40)];

test('a point between two tiles in a row inserts between them', () => {
  // Between a (centre 30) and b (centre 96), past a's centre.
  assert.equal(nearestSlot({x: 63, y: 20}, threeTiles), 1);
});

test('a point before the first tile inserts at zero', () => {
  assert.equal(nearestSlot({x: -20, y: 20}, threeTiles), 0);
});

test('a point past the last tile appends', () => {
  // Directly below c. In a 2-column grid any point far to the RIGHT is
  // horizontally closer to b's column whatever the row, so "past the grid" for
  // a partial last row reads as "below its own column".
  assert.equal(nearestSlot({x: 30, y: 200}, threeTiles), 3);
});

test('a point between rows resolves against the nearest row\'s tile', () => {
  // Aligned with b's column, between the rows but closer to row 0. The nearest
  // tile is b and the point is past b's centre, so it inserts after b - the
  // row-1 gap under b, which holds no tile.
  assert.equal(nearestSlot({x: 96, y: 30}, threeTiles), 2);
});

test('a point exactly on a boundary resolves deterministically', () => {
  // Exactly halfway between a's centre (30) and b's (96).
  assert.equal(nearestSlot({x: 63, y: 20}, threeTiles), 1);
});

test('a grid with no laid-out tile appends', () => {
  assert.equal(nearestSlot({x: 10, y: 10}, [null, null, null]), 3);
  assert.equal(nearestSlot({x: 10, y: 10}, []), 0);
});

test('a tile with no frame yet is skipped', () => {
  // b is not laid out - resolution still works against a and c alone.
  const missing = [threeTiles[0], null, threeTiles[2]];
  assert.equal(nearestSlot({x: -20, y: 20}, missing), 0);
});

// boundary hysteresis for the live preview.
// A 3-tile row, 100px each: centres at 50, 150, 250.
const row = [box(0, 0, 100, 100), box(100, 0, 100, 100), box(200, 0, 100, 100)];

test('without a sticky slot the boundary is unchanged', () => {
  // This is what the commit path relies on, so it must not shift by a pixel.
  assert.equal(nearestSlot({x: 149, y: 50}, row), 1);
  assert.equal(nearestSlot({x: 150, y: 50}, row), 2);
});

test('a sticky slot suppresses jitter across the boundary', () => {
  // The reported bug: the pointer sits essentially still near a centre while
  // sub-pixel movement straddles it, and each flip re-animates the grid.
  assert.equal(nearestSlot({x: 152, y: 50}, row, 1), 1);
  assert.equal(nearestSlot({x: 148, y: 50}, row, 2), 2);
});

test('a decisive crossing still flips immediately', () => {
  assert.equal(nearestSlot({x: 170, y: 50}, row, 1), 2);
  assert.equal(nearestSlot({x: 130, y: 50}, row, 2), 1);
});

test('stickiness never holds across a different tile', () => {
  // Moving onto another tile is a real target change, however small the step.
  assert.equal(nearestSlot({x: 249, y: 50}, row, 1), 2);
  assert.equal(nearestSlot({x: 251, y: 50}, row, 1), 3);
});

test('a stale sticky slot is ignored', () => {
  assert.equal(nearestSlot({x: 10, y: 50}, row, 99), 0);
});

test('the sticky slot changes the answer inside the dead zone', () => {
  // THE trap. When the Swift commit path recomputed without stickyIndex,
  // releasing without moving landed the tile one slot from what was displayed
  // - silently, because nothing in the UI showed the change. This pins that
  // the parameter genuinely changes the answer here, which is exactly what a
  // commit that dropped it would lose.
  const point = {x: 152, y: 50};  // 2px past a centre, inside the 6px zone
  assert.equal(nearestSlot(point, row, 1), 1, 'the preview holds');
  assert.equal(nearestSlot(point, row), 2, 'the bare threshold flips');
  assert.notEqual(nearestSlot(point, row, 1), nearestSlot(point, row));
});

test('a drop with no preview falls back to the bare threshold', () => {
  assert.equal(nearestSlot({x: 152, y: 50}, row, null), 2);
});

test('the dead zone is narrower than half a tile', () => {
  // Not a magic-number check: a zone wider than half a tile would make the
  // far side of a neighbour unreachable without overshooting it entirely.
  assert.ok(GRID_BOUNDARY_HYSTERESIS < 50);
});
