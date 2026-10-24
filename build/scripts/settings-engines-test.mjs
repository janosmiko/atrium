#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// A keyword the command bar cannot reach - a verb, or another engine's - must
// be refused, not silently unreachable.
//
//   node --test build/scripts/settings-engines-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');

installDOM({pathname: '/settings'});

const {renderEnginesPane, shippableEngines} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/settings-engines.js'));

function engine(overrides) {
  return {id: 'e1', name: 'Example', keyword: 'ex',
          url: 'https://example.com/?q=%s', enabled: true, builtin: false,
          ...overrides};
}

// Mounts the pane with its own send loop, so a click's `rerenderOnly` push
// re-renders the same way the real settings route does.
function mount(engines) {
  let current = {searchEngines: engines};
  const body = document.createElement('div');
  const sent = [];
  const rerender = () => {
    body.replaceChildren();
    renderEnginesPane(body, current, send);
  };
  function send(next) {
    sent.push(next);
    current = next;
    rerender();
  }
  rerender();
  return {
    body,
    sent,
    get current() {
      return current;
    },
    select(index) {
      body.querySelectorAll('.engine-row')[index]
          .dispatchEvent(new Event('click', {bubbles: true}));
      sent.length = 0;
    },
    setField(name, value) {
      const input = body.querySelector(`.engine-detail input[name="${name}"]`);
      input.value = value;
      input.dispatchEvent(new Event('blur', {bubbles: false}));
    },
    error() {
      return body.querySelector('.engine-error').textContent;
    },
  };
}

test('a keyword equal to a command verb is refused with a message naming it',
     () => {
       const m = mount([engine()]);
       m.select(0);
       m.setField('keyword', 'mv');
       assert.match(m.error(), /"mv" is a command/);
       assert.deepEqual(m.sent, []);
     });

test('a verb keyword is refused whatever its case', () => {
  const m = mount([engine()]);
  m.select(0);
  m.setField('keyword', 'MV');
  assert.match(m.error(), /"MV" is a command/);
  assert.deepEqual(m.sent, []);
});

test('a keyword equal to another engine\'s keyword is refused', () => {
  const m = mount([engine({id: 'e1', keyword: 'ex'}),
                    engine({id: 'e2', name: 'Other', keyword: 'other'})]);
  m.select(1);
  m.setField('keyword', 'ex');
  assert.match(m.error(), /"ex" is already used by another engine/);
  assert.deepEqual(m.sent, []);
});

test('a duplicate keyword is refused whatever its case', () => {
  const m = mount([engine({id: 'e1', keyword: 'ex'}),
                    engine({id: 'e2', name: 'Other', keyword: 'other'})]);
  m.select(1);
  m.setField('keyword', 'EX');
  assert.match(m.error(), /already used by another engine/);
  assert.deepEqual(m.sent, []);
});

test('a free keyword is accepted', () => {
  const m = mount([engine({id: 'e1', keyword: 'ex'}),
                    engine({id: 'e2', name: 'Other', keyword: 'other'})]);
  m.select(1);
  m.setField('keyword', 'newkw');
  assert.equal(m.error(), '');
  assert.equal(m.sent.length, 1);
  assert.equal(
      m.sent[0].searchEngines.find((e) => e.id === 'e2').keyword, 'newkw');
});

test('editing an engine and keeping its own keyword is accepted, ' +
     'not flagged as a duplicate of itself', () => {
  const m = mount([engine({id: 'e1', keyword: 'ex'}),
                    engine({id: 'e2', name: 'Other', keyword: 'other'})]);
  m.select(0);
  m.setField('keyword', 'ex');
  assert.equal(m.error(), '');
  assert.equal(m.sent.length, 1);
  assert.equal(
      m.sent[0].searchEngines.find((e) => e.id === 'e1').keyword, 'ex');
});

test('shippableEngines drops an engine whose keyword is a command verb',
     () => {
       assert.deepEqual(shippableEngines([engine({keyword: 'close'})]), []);
     });

test('shippableEngines drops engines that share a keyword', () => {
  const clean = engine({id: 'e3', name: 'Clean', keyword: 'clean'});
  const dupeA = engine({id: 'e1', keyword: 'dupe'});
  const dupeB = engine({id: 'e2', name: 'Other', keyword: 'dupe'});
  const kept = shippableEngines([dupeA, dupeB, clean]);
  assert.deepEqual(kept, [clean]);
});
