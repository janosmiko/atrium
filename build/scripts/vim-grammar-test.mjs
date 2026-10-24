#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The grammar alone, with a verb table supplied by the test, so
// these cases belong to neither the injected page nor a WebUI surface.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');
const SOURCE = fs.readFileSync(path.join(ATRIUM_DIR, 'vim-grammar.js'), 'utf8');

// Evaluated as a string, the way the injected path loads it. A WebUI surface
// loads the same file as a classic script and reads the same global.
function loadGrammar() {
  installDOM();
  delete globalThis.__atriumVimGrammar;
  new Function(SOURCE)();
  return globalThis.__atriumVimGrammar;
}

function makeWith(verbs, options = {}) {
  const grammar = loadGrammar();
  const ran = [];
  const table = {};
  for (const key of verbs) {
    table[key] = () => ran.push(key);
  }
  return {
    machine: grammar.makeGrammar({verbs: table, ...options}),
    ran,
    grammar,
  };
}

test('a pair resolves to the name its second key gives it', () => {
  const {machine} = makeWith([]);
  assert.equal(machine.key('g'), 'handled');
  assert.equal(machine.key('t'), 'next-tab');
  assert.equal(machine.key('g'), 'handled');
  assert.equal(machine.key('T'), 'previous-tab');
});

// gj has to scroll. A prefix that swallowed whatever followed it would make
// every motion after a stray g disappear.
test('a prefix with no entry for the next key drops rather than eating it',
     () => {
       const {machine, ran} = makeWith(['j']);
       assert.equal(machine.key('g'), 'handled');
       assert.equal(machine.key('j'), 'handled');
       assert.deepEqual(ran, ['j']);
     });

test('a single browser key is named, not run', () => {
  const {machine, ran} = makeWith([]);
  assert.equal(machine.key('x'), 'close-tab');
  assert.equal(machine.key('o'), 'open-command-bar');
  assert.deepEqual(ran, []);
});

// The whole point of sharing: the page and a surface bring different verb
// tables and still get the same answer for a key neither of them runs.
test('two consumers with different verbs answer a browser key alike', () => {
  const page = makeWith(['j', 'k', 'd', 'u']);
  const surface = makeWith(['j', 'k']);
  assert.equal(page.machine.key(']'), 'handled');
  assert.equal(surface.machine.key(']'), 'handled');
  assert.equal(page.machine.key(']'), 'next-space');
  assert.equal(surface.machine.key(']'), 'next-space');
});

// Each consumer owns its own pending state. A shared one would make a g typed
// in the sidebar change what the next key means in the page.
test('two machines do not share a pending prefix', () => {
  const grammar = loadGrammar();
  const first = grammar.makeGrammar({verbs: {}});
  const second = grammar.makeGrammar({verbs: {}});
  assert.equal(first.key('g'), 'handled');
  assert.equal(second.key('t'), 'open-command-bar');
});

test('a verb the consumer supplies runs and reports handled', () => {
  const {machine, ran} = makeWith(['j']);
  assert.equal(machine.key('j'), 'handled');
  assert.deepEqual(ran, ['j']);
});

// Null rather than 'handled'. A surface listens to every key, so a key the
// grammar has no verb for has to stay available to whatever else claims it.
test('a key the grammar does not know is refused, not swallowed', () => {
  const {machine, ran} = makeWith(['j']);
  assert.equal(machine.key('q'), null);
  assert.equal(machine.key('`'), null);
  assert.deepEqual(ran, []);
});

test('isEditable knows a field from a heading', () => {
  const grammar = loadGrammar();
  const input = globalThis.document.createElement('input');
  const heading = globalThis.document.createElement('h1');
  assert.equal(grammar.isEditable(input), true);
  assert.equal(grammar.isEditable(heading), false);
  assert.equal(grammar.isEditable(null), false);
});

// A readonly field takes no typing, so vim keeps its verbs there.
test('isEditable refuses a readonly field', () => {
  const grammar = loadGrammar();
  const input = globalThis.document.createElement('input');
  input.setAttribute('readonly', '');
  assert.equal(grammar.isEditable(input), false);
});
