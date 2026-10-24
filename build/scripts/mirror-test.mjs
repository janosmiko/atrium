#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the Mirror View choice list.
//
//   make mirror-test
//
// Ported from the lab's menu build (docs/lab/swipe-lab.html:4426-4428 and
// :4470-4472), which offers "as <profile>" for every profile but the one you
// are in, then a separator and "Logged out".
//
// The lab reads a global profs array. Atrium has no such list: profiles are a
// property of spaces, so the choices are derived from the snapshot's chips -
// which is why slice 1 put profileId on them.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {mirrorChoices, LOGGED_OUT} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/mirror.js'));

const chip = (name, profileId) => ({name, profileId, tabs: [], pinned: [],
                                    folders: [], favorites: []});

const snapshot = {
  activeSpace: 0,
  spaces: [
    chip('Work', 'work'),
    chip('Work Side', 'work'),
    chip('Personal', 'personal'),
    chip('Ops', 'ops'),
  ],
};

test('every other profile is offered, and the current one is not', () => {
  const got = mirrorChoices(snapshot);
  assert.deepEqual(got.filter((c) => c.profileId !== LOGGED_OUT)
                       .map((c) => c.profileId),
                   ['personal', 'ops']);
});

test('a profile named by two spaces is offered once', () => {
  const got = mirrorChoices({...snapshot, activeSpace: 2});
  assert.deepEqual(got.filter((c) => c.profileId !== LOGGED_OUT)
                       .map((c) => c.profileId),
                   ['work', 'ops']);
});

test('profiles keep the order their spaces appear in', () => {
  const reordered = {activeSpace: 0, spaces: [
    chip('Work', 'work'), chip('Ops', 'ops'), chip('Personal', 'personal')]};
  assert.deepEqual(mirrorChoices(reordered).map((c) => c.profileId),
                   ['ops', 'personal', LOGGED_OUT]);
});

test('Logged out is always last, and always offered', () => {
  for (const at of [0, 1, 2, 3]) {
    const got = mirrorChoices({...snapshot, activeSpace: at});
    assert.equal(got.at(-1).profileId, LOGGED_OUT);
  }
});

test('a lone profile still offers Logged out and nothing else', () => {
  const only = {activeSpace: 0, spaces: [chip('Work', 'work')]};
  assert.deepEqual(mirrorChoices(only).map((c) => c.profileId), [LOGGED_OUT]);
});

test('a chip with no profileId is not offered as a choice', () => {
  const partial = {activeSpace: 0, spaces: [
    chip('Work', 'work'), chip('Broken', ''), chip('Ops', 'ops')]};
  assert.deepEqual(mirrorChoices(partial).map((c) => c.profileId),
                   ['ops', LOGGED_OUT]);
});

test('an absent or malformed snapshot offers nothing rather than throwing', () => {
  assert.deepEqual(mirrorChoices(null), []);
  assert.deepEqual(mirrorChoices({}), []);
  assert.deepEqual(mirrorChoices({spaces: []}), []);
});

test('an activeSpace outside the list is treated as no current profile', () => {
  const got = mirrorChoices({...snapshot, activeSpace: 99});
  assert.deepEqual(got.map((c) => c.profileId),
                   ['work', 'personal', 'ops', LOGGED_OUT]);
});

test('a chip with a profileName is labelled by it, and one without by its id', () => {
  const named = {activeSpace: 0, spaces: [
    chip('Work', 'work'),
    {...chip('Personal', 'Profile 7'), profileName: 'Personal'},
    chip('Ops', 'Profile 9')]};
  assert.deepEqual(mirrorChoices(named).map((c) => c.label),
                   ['Personal', 'Profile 9', 'Logged out']);
});

test('every choice carries a label the menu can draw', () => {
  for (const choice of mirrorChoices(snapshot)) {
    assert.equal(typeof choice.label, 'string');
    assert.ok(choice.label.length > 0);
  }
});

test('the module reaches nothing off the machine', async () => {
  const {readFile} = await import('node:fs/promises');
  const source = await readFile(
      path.join(SRC, 'chrome/browser/resources/atrium/mirror.js'), 'utf8');
  for (const reach of ['fetch(', 'XMLHttpRequest', 'sendBeacon', 'WebSocket',
                       'chrome.send', 'import(', 'localStorage',
                       'indexedDB']) {
    assert.ok(!source.includes(reach), `mirror.js must not use ${reach}`);
  }
});
