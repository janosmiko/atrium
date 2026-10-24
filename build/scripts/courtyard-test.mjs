#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the Courtyard's session - what a click, an Escape or a
// click-away does. The plane geometry has its own suite, canvas-model-test.mjs.
//
//   make courtyard-test

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {closedCourtyard, courtyardStep} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/courtyard.js'));

const snapshot = {activeSpace: 1, spaces: [{name: 'Work'}, {name: 'Home'}]};

test('opening remembers the space you came from', () => {
  const {state, effect} = courtyardStep(closedCourtyard(), {type: 'open', snapshot});
  assert.equal(state.open, true);
  assert.equal(state.priorSpace, 1);
  assert.equal(effect, null, 'opening does not leave the space you are in');
});

test('opening twice does not forget the space you came from', () => {
  const first = courtyardStep(closedCourtyard(), {type: 'open', snapshot}).state;
  const again = courtyardStep(first, {type: 'open', snapshot: {activeSpace: 0, spaces: []}});
  assert.equal(again.state.priorSpace, 1);
});

test('clicking a room closes the Courtyard and enters that space', () => {
  const open = courtyardStep(closedCourtyard(), {type: 'open', snapshot}).state;
  const {state, effect} = courtyardStep(open, {type: 'enterRoom', spaceIndex: 0});
  assert.equal(state.open, false);
  assert.deepEqual(effect, {type: 'enterSpace', spaceIndex: 0});
});

test('clicking the room you are already in just closes', () => {
  const open = courtyardStep(closedCourtyard(), {type: 'open', snapshot}).state;
  const {state, effect} = courtyardStep(open, {type: 'enterRoom', spaceIndex: 1});
  assert.equal(state.open, false);
  assert.equal(effect, null);
});

test('Escape and click-away close without a switch when nothing moved', () => {
  const open = courtyardStep(closedCourtyard(), {type: 'open', snapshot}).state;
  for (const action of [{type: 'dismiss', activeSpace: 1}, {type: 'dismiss'}]) {
    const {state, effect} = courtyardStep(open, action);
    assert.equal(state.open, false);
    assert.equal(effect, null);
  }
});

test('a dismiss returns to the prior space when the space moved underneath', () => {
  const open = courtyardStep(closedCourtyard(), {type: 'open', snapshot}).state;
  const {state, effect} = courtyardStep(open, {type: 'dismiss', activeSpace: 0});
  assert.equal(state.open, false);
  assert.deepEqual(effect, {type: 'enterSpace', spaceIndex: 1});
});

test('actions on a closed Courtyard do nothing', () => {
  const shut = closedCourtyard();
  for (const action of [{type: 'enterRoom', spaceIndex: 0}, {type: 'dismiss'},
                        {type: 'nonsense'}]) {
    const {state, effect} = courtyardStep(shut, action);
    assert.equal(state.open, false);
    assert.equal(effect, null);
  }
});

test('a step never mutates the state it was given', () => {
  const open = courtyardStep(closedCourtyard(), {type: 'open', snapshot}).state;
  const before = JSON.stringify(open);
  courtyardStep(open, {type: 'dismiss', activeSpace: 0});
  courtyardStep(open, {type: 'enterRoom', spaceIndex: 0});
  assert.equal(JSON.stringify(open), before);
});
