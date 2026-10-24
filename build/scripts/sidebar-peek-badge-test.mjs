#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// A favourite tile wears the peeked page's favicon while its Peek is open.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');

const h = installDOM({pathname: '/sidebar'});
const {installSidebar} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/sidebar.js'));

function fresh() {
  h.reset();
  const root = document.createElement('div');
  h.body.appendChild(root);
  installSidebar(root);
  return root;
}

const SNAPSHOT = {
  iconScope: 'testscope',
  tabs: [{id: '11', title: 'Row', url: 'https://row.test/'}],
  pinned: [],
  folders: [],
  favorites: [{id: 'https://fav.test/', title: 'Fav', tabId: '7'},
              {id: 'https://cold.test/', title: 'Cold', tabId: ''}],
};

const badged = (root) => [...root.querySelectorAll('.fav > .peekbadge')].map(
    (b) => b.parentNode.dataset.favTab);

test('the receiver lands on window under the name C++ calls', () => {
  fresh();
  assert.equal(typeof h.window.atriumSetPeekBadge, 'function');
});

test('the favourite that owns the peek wears its favicon', () => {
  const root = fresh();
  h.window.atriumSetSidebar(SNAPSHOT);
  assert.deepEqual(badged(root), []);

  h.window.atriumSetPeekBadge(7, 42);
  assert.deepEqual(badged(root), ['7']);
  assert.equal(root.querySelector('.peekbadge img').src,
               '/favicon/testscope/42.png');
});

test('a new favicon revision swaps the img src', () => {
  const root = fresh();
  h.window.atriumSetSidebar(SNAPSHOT);
  h.window.atriumSetPeekBadge(7, 42, 1);
  assert.equal(root.querySelector('.peekbadge img').src,
               '/favicon/testscope/42.png?r=1');

  // Same path, same bytes to Blink: only the revision makes it fetch again.
  h.window.atriumSetPeekBadge(7, 42, 2);
  assert.equal(root.querySelector('.peekbadge img').src,
               '/favicon/testscope/42.png?r=2');
  assert.equal(root.querySelectorAll('.peekbadge').length, 1);
});

test('the badge survives a rebuild', () => {
  const root = fresh();
  h.window.atriumSetSidebar(SNAPSHOT);
  h.window.atriumSetPeekBadge(7, 42);

  h.window.atriumSetSidebar(SNAPSHOT);
  assert.deepEqual(badged(root), ['7']);
});

test('closing the peek clears the badge, on the live tree and the next one',
     () => {
  const root = fresh();
  h.window.atriumSetSidebar(SNAPSHOT);
  h.window.atriumSetPeekBadge(7, 42);

  // SetSidebarPeekBadge(0, 0) is the browser's clear.
  h.window.atriumSetPeekBadge(0, 0);
  assert.deepEqual(badged(root), []);
  h.window.atriumSetSidebar(SNAPSHOT);
  assert.deepEqual(badged(root), []);
});

test('a peek from a plain tab row badges nothing', () => {
  const root = fresh();
  h.window.atriumSetSidebar(SNAPSHOT);
  h.window.atriumSetPeekBadge(11, 42);
  assert.deepEqual(badged(root), []);
  assert.equal(root.querySelectorAll('.peekbadge').length, 0);
});

test('the badge is a white plate that takes no room', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');
  const rule = /\.fav > \.peekbadge\s*{([^}]*)}/.exec(css);
  assert.ok(rule, 'no rule paints the badge');
  assert.match(rule[1], /position:\s*absolute/);
  assert.match(rule[1], /background:\s*#fff/);
  assert.match(rule[1], /box-shadow/);
});
