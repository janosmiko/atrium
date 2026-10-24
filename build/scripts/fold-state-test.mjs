#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the sidebar's folder open/closed state.
//
//   make fold-test
//
// The interesting part is not the Set. It is that the snapshot is re-pushed on
// every tab title change, so a fold has to survive data arriving on top of it -
// which is exactly what a unit test can pin down and a person clicking cannot.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {createFoldState} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/sidebar.js'));

// What the browser pushes: `closed` reflects nothing the user did, because
// there is no producer for it yet.
const snapshot = (ids = ['work', 'reading']) => ({
  tabs: [{id: '1', title: 'One'}],
  folders: ids.map((id) => ({id, name: id, closed: false, tabs: []})),
});

const closedIds = (snap) =>
    (snap.folders || []).filter((f) => f.closed).map((f) => f.id);

test('nothing is folded until something is toggled', () => {
  const folds = createFoldState();
  assert.deepEqual(closedIds(folds.applyTo(snapshot())), []);
});

test('recording an intent folds the folder', () => {
  const folds = createFoldState();
  folds.record('work', true);
  assert.equal(folds.isClosed('work'), true);
  assert.deepEqual(closedIds(folds.applyTo(snapshot())), ['work']);
  folds.record('work', false);
  assert.deepEqual(closedIds(folds.applyTo(snapshot())), []);
});

/// The bug a Set could not express: opening a folder is an OPINION, and a push
/// that has not caught up must not undo it.
test('an explicit open survives a push that still says closed', () => {
  const folds = createFoldState();
  folds.record('work', false);
  const stale = snapshot();
  stale.folders[0].closed = true;  // the model has not persisted it yet
  assert.deepEqual(closedIds(folds.applyTo(stale)), [],
      'the folder must stay open until the push agrees');
});

test('a fold survives a snapshot re-push', () => {
  const folds = createFoldState();
  folds.record('reading', true);
  // The tab title changed; the browser pushes everything again, with
  // closed: false on every folder because it does not know any better.
  for (let i = 0; i < 5; i++) {
    assert.deepEqual(closedIds(folds.applyTo(snapshot())), ['reading'],
        'the push must not spring the folder back open');
  }
});

// The browser persists the fold, so the local set is an
// OPTIMISTIC overlay rather than the authority.

test('the model is believed when the surface has no opinion', () => {
  const folds = createFoldState();
  const pushed = snapshot();
  pushed.folders[0].closed = true;  // folded in the SwiftUI arm, say
  assert.deepEqual(closedIds(folds.applyTo(pushed)), ['work'],
      'a fold made elsewhere must reach this surface');
});

test('a local fold still wins while the push has not caught up', () => {
  const folds = createFoldState();
  folds.record('work', true);
  // The browser has not persisted it yet, so this push still says open.
  assert.deepEqual(closedIds(folds.applyTo(snapshot())), ['work'],
      'the optimistic fold must not flicker back open');
});

test('a local fold is dropped once the snapshot agrees with it', () => {
  const folds = createFoldState();
  folds.record('work', true);
  const agreed = snapshot();
  agreed.folders[0].closed = true;  // the push caught up
  assert.deepEqual(closedIds(folds.applyTo(agreed)), ['work']);
  assert.equal(folds.isClosed('work'), false,
      'the overlay has done its job and must stop masking the model');

  // Now the model says open again - unfolded in another window. With the
  // overlay still held, this surface would never see it.
  assert.deepEqual(closedIds(folds.applyTo(snapshot())), []);
});

test('applyTo does not mutate the snapshot it was given', () => {
  const folds = createFoldState();
  folds.record('work', true);
  const pushed = snapshot();
  const applied = folds.applyTo(pushed);
  assert.equal(pushed.folders[0].closed, false, 'the input is left alone');
  assert.equal(applied.folders[0].closed, true);
  assert.notEqual(applied.folders, pushed.folders);
});

test('a snapshot with no folders passes through untouched', () => {
  const folds = createFoldState();
  const bare = {tabs: [{id: '1'}]};
  const out = folds.applyTo(bare);
  // Structural, not reference, equality: applyTo copies because it also has
  // to reconcile the pinned section's flag, and identity was an
  // implementation detail rather than the contract. What matters is that
  // nothing is added and the input is not mutated.
  assert.deepEqual(out, bare);
  assert.deepEqual(bare, {tabs: [{id: '1'}]});
});

test('the pinned section folds through the same overlay', () => {
  const folds = createFoldState();
  const snap = {tabs: [], folders: [], pinnedCollapsed: false};
  folds.record('__pinned__', true);
  assert.equal(folds.applyTo(snap).pinnedCollapsed, true,
      'the optimistic fold applies before the push catches up');

  // Once the model agrees, the intent is dropped and the model leads again.
  assert.equal(folds.applyTo({...snap, pinnedCollapsed: true}).pinnedCollapsed, true);
  assert.equal(folds.applyTo(snap).pinnedCollapsed, false,
      'unfolded elsewhere must reach this surface');
});

test('the pinned sentinel is not pruned as a departed folder', () => {
  const folds = createFoldState();
  folds.record('__pinned__', true);
  folds.prune({tabs: [], folders: []});
  assert.equal(folds.applyTo({tabs: [], folders: [], pinnedCollapsed: false})
      .pinnedCollapsed, true);
});

test('folds for folders that no longer exist are dropped', () => {
  const folds = createFoldState();
  folds.record('work', true);
  folds.record('reading', true);
  folds.prune(snapshot(['work']));
  assert.equal(folds.isClosed('work'), true);
  assert.equal(folds.isClosed('reading'), false,
      'a departed folder must not keep a fold alive for the window\'s life');
});

test('folds are dropped when folders stop arriving at all', () => {
  const folds = createFoldState();
  folds.record('work', true);
  folds.prune({tabs: []});
  assert.equal(folds.isClosed('work'), false);
});
