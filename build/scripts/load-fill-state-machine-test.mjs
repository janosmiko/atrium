#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');

const h = installDOM({pathname: '/sidebar'});

const {installSidebar} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/sidebar.js'));

const SNAPSHOT = {
  spaceName: 'Work',
  spaceTint: '#4488cc',
  favorites: [],
  pinned: [],
  folders: [],
  tabs:
      [{id: 't1', title: 'First', url: 'https://one.example/', selected: true}],
};

function fresh() {
  h.reset();
  const root = h.document.createElement('div');
  h.body.appendChild(root);
  installSidebar(root);
  return root;
}

function mount(snapshot = SNAPSHOT) {
  const root = fresh();
  h.window.atriumSetSidebar(snapshot);
  h.sent.length = 0;
  return root;
}

const fillIn = (root) => root.querySelector('.urlrow .loadfill');

test('the fill advances monotonically and never jumps backwards', () => {
  const root = mount();
  h.window.atriumLoadPhase('t1', 'waiting', 0);
  h.window.atriumLoadPhase('t1', 'receiving', 0.2);
  assert.equal(fillIn(root).style.width, '20%');
  h.window.atriumLoadPhase('t1', 'receiving', 0.55);
  assert.equal(fillIn(root).style.width, '55%');
  // Chromium resets its own progress tracker mid-load, so a real report can
  // arrive lower than the high-water mark already shown.
  h.window.atriumLoadPhase('t1', 'receiving', 0.1);
  assert.equal(fillIn(root).style.width, '55%',
      'a lower fraction mid-load must not shrink the fill');
  h.window.atriumLoadPhase('t1', 'receiving', 0.9);
  assert.equal(fillIn(root).style.width, '90%');
});

test('a fresh waiting phase starts a new high-water mark, not just a new element',
     () => {
       const root = mount();
       h.window.atriumLoadPhase('t1', 'waiting', 0);
       h.window.atriumLoadPhase('t1', 'receiving', 0.8);
       h.window.atriumLoadPhase('t1', 'done', 1);
       h.window.atriumLoadPhase('t1', 'waiting', 0);
       h.window.atriumLoadPhase('t1', 'receiving', 0.1);
       assert.equal(fillIn(root).style.width, '10%',
           'the previous run\'s 80% must not carry into a genuinely new load');
     });

test('the fill stops and clears once the page finishes loading', () => {
  const root = mount();
  h.window.atriumLoadPhase('t1', 'waiting', 0);
  h.window.atriumLoadPhase('t1', 'receiving', 0.4);
  h.window.atriumLoadPhase('t1', 'done', 1);
  const fill = fillIn(root);
  assert.equal(fill.classList.contains('done'), true);
  assert.equal(fill.classList.contains('receiving'), false);
  assert.equal(fill.classList.contains('waiting'), false);
});

test('a re-render after done never replays the fade', () => {
  // buildUrlRow rebuilds a fresh .loadfill on every snapshot push. sidebar.js
  // only replays a phase onto it when that phase is not 'done'.
  const root = mount();
  h.window.atriumLoadPhase('t1', 'waiting', 0);
  h.window.atriumLoadPhase('t1', 'receiving', 0.4);
  h.window.atriumLoadPhase('t1', 'done', 1);
  h.window.atriumSetSidebar(SNAPSHOT);
  const fill = fillIn(root);
  assert.equal(fill.classList.contains('done'), false,
      'a completed run must not replay its fade on the next render');
  assert.equal(fill.classList.contains('waiting'), false);
  assert.equal(fill.classList.contains('receiving'), false);
});

test('a live phase does carry over a re-render, so a mid-load rebuild keeps its fill',
     () => {
       const root = mount();
       h.window.atriumLoadPhase('t1', 'waiting', 0);
       h.window.atriumLoadPhase('t1', 'receiving', 0.6);
       h.window.atriumSetSidebar(SNAPSHOT);
       const fill = fillIn(root);
       assert.equal(fill.classList.contains('receiving'), true);
       assert.equal(fill.style.width, '60%');
     });

// That case is not pinned here: atriumLoadPhase carries no same-document bit, so
// its waiting/done pair matches a real fast load too. Fix belongs in C++'s
// LoadProgressObserver::DidStartLoading.
