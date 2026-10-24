#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the live-reflow provisional-order math.
//
//   make reflow-test
//
// Ported case for case from FavoritesReflowMathTests.swift. The parity test at
// the bottom is the reason the Swift file exists at all and is why it was
// ported rather than re-derived: the order shown while dragging must BE the
// order committed on drop, not merely agree with it most of the time.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {provisionalOrder} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/sidebar-drag.js'));

test('dragging forward moves the item after the target slot', () => {
  // a b c d, dragging "a" (0) to slot 3 (before "d") -> b c a d
  assert.deepEqual(provisionalOrder(['a', 'b', 'c', 'd'], 0, 3),
      ['b', 'c', 'a', 'd']);
});

test('dragging backward moves the item before the target slot', () => {
  // a b c d, dragging "d" (3) to slot 1 (before "b") -> a d b c
  assert.deepEqual(provisionalOrder(['a', 'b', 'c', 'd'], 3, 1),
      ['a', 'd', 'b', 'c']);
});

test('hovering slot zero moves the item to the front', () => {
  assert.deepEqual(provisionalOrder(['a', 'b', 'c'], 2, 0), ['c', 'a', 'b']);
});

test('hovering the slot past the end appends the item', () => {
  assert.deepEqual(provisionalOrder(['a', 'b', 'c'], 0, 3), ['b', 'c', 'a']);
});

test('hovering its own slot changes nothing', () => {
  assert.deepEqual(provisionalOrder(['a', 'b', 'c'], 1, 1), ['a', 'b', 'c']);
});

test('hovering immediately after its own slot changes nothing', () => {
  // destination === source + 1 means "insert right after itself" - no actual
  // movement, the same guard the commit path applies.
  assert.deepEqual(provisionalOrder(['a', 'b', 'c'], 1, 2), ['a', 'b', 'c']);
});

test('an out-of-range source changes nothing', () => {
  assert.deepEqual(provisionalOrder(['a', 'b', 'c'], 5, 1), ['a', 'b', 'c']);
});

test('a negative destination changes nothing', () => {
  assert.deepEqual(provisionalOrder(['a', 'b', 'c'], 0, -1), ['a', 'b', 'c']);
});

test('a destination beyond the count changes nothing', () => {
  assert.deepEqual(provisionalOrder(['a', 'b', 'c'], 0, 4), ['a', 'b', 'c']);
});

test('an empty list changes nothing', () => {
  assert.deepEqual(provisionalOrder([], 0, 0), []);
});

test('the input array is never mutated', () => {
  // Not in the Swift suite, because Swift arrays are values and cannot be.
  // In JS they can, and a preview that edited the rendered array in place
  // would corrupt the very order it is previewing.
  const items = ['a', 'b', 'c'];
  provisionalOrder(items, 0, 3);
  assert.deepEqual(items, ['a', 'b', 'c']);
});

test('the provisional order matches the commit path for every valid slot', () => {
  const items = ['a', 'b', 'c', 'd', 'e'];
  // The commit path's own index formula, replicated so this fails the moment
  // the two diverge.
  const committedToIndex = (source, destination, count) =>
      [...Array(count).keys()].filter((i) => i !== source && i < destination).length;

  for (let source = 0; source < items.length; source++) {
    for (let destination = 0; destination <= items.length; destination++) {
      if (destination === source || destination === source + 1) {
        continue;
      }
      const provisional = provisionalOrder(items, source, destination);
      const committed = [...items];
      committed.splice(committedToIndex(source, destination, items.length), 0,
          committed.splice(source, 1)[0]);
      assert.deepEqual(provisional, committed,
          `source=${source} destination=${destination}`);
    }
  }
});
