#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// The cases atrium_command_bar_match_text_unittest.cc pins, in the page's
// copy of the rule. Two implementations drift the moment one is unasserted.
//   node --test build/scripts/command-match-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

const {commandMatchScore} =
    await import(path.join(ATRIUM_DIR, 'command-match.js'));

const hits = (query, fields) => commandMatchScore(query, fields) !== null;

// --- the tokenizer -----------------------------------------------------------

test('every token must land somewhere', () => {
  assert.equal(hits('gh iss', ['GitHub Issues']), true);
  assert.equal(hits('gh nope', ['GitHub Issues']), false);
});

test('tokens match out of order', () => {
  assert.equal(hits('iss gh', ['GitHub Issues']), true);
});

test('tokens may land on different fields', () => {
  assert.equal(hits('issues github', ['Issues', 'github.com']), true);
});

test('runs of whitespace are one separator', () => {
  assert.equal(commandMatchScore('gh   iss', ['GitHub Issues']),
               commandMatchScore('gh iss', ['GitHub Issues']));
  assert.equal(commandMatchScore('  gh iss  ', ['GitHub Issues']),
               commandMatchScore('gh iss', ['GitHub Issues']));
});

test('a typed host splits into the words it has to land on', () => {
  // One token carrying the dots would land on no word at all, and typing a
  // host is how a tab with a useless title gets found.
  assert.equal(hits('weather.example', ['Weather', 'weather.example']), true);
  assert.equal(hits('weather.example', ['Weather', 'other.site']), false);
});

test('an empty query has no tokens and scores zero', () => {
  // Each caller keeps its own empty-query behaviour, so this only has to stay
  // a match rather than a filter.
  assert.equal(commandMatchScore('', ['GitHub Issues']), 0);
  assert.equal(commandMatchScore('   ', ['GitHub Issues']), 0);
  assert.equal(commandMatchScore('...', ['GitHub Issues']), 0);
});

test('no field is no match', () => {
  assert.equal(hits('gh', []), false);
  assert.equal(hits('gh', ['']), false);
});

test('a missing or non-string field is skipped, not a crash', () => {
  // An action row's aliases arrive from the browser, and a catalog row that
  // shipped without them must not take the palette down.
  assert.equal(hits('gh', undefined), false);
  assert.equal(hits('gh', [null, 'GitHub']), true);
});

// --- what counts as a hit ----------------------------------------------------

test('a prefix of any word hits', () => {
  assert.equal(hits('iss', ['GitHub Issues']), true);
  assert.equal(hits('git', ['GitHub Issues']), true);
});

test('a subsequence of one word hits', () => {
  assert.equal(hits('gthb', ['GitHub Issues']), true);
});

test('a subsequence must start the word', () => {
  // Without the shared first character every short query hits every row.
  assert.equal(hits('tub', ['GitHub Issues']), false);
});

test('a subsequence does not span two words', () => {
  assert.equal(hits('gis', ['GitHub Issues']), false);
});

test('a miss stays a miss', () => {
  assert.equal(hits('xyz', ['GitHub Issues']), false);
  assert.equal(hits('zebra', ['GitHub Issues', 'github.com']), false);
});

test('punctuation separates words', () => {
  assert.equal(hits('git', ['github.com']), true);
  assert.equal(hits('com', ['github.com']), true);
  assert.equal(hits('new', ['docs.google.com/new-doc']), true);
});

test('case is folded both ways', () => {
  assert.equal(hits('GH ISS', ['github issues']), true);
  assert.equal(hits('gh iss', ['GITHUB ISSUES']), true);
});

// --- ranking -----------------------------------------------------------------

test('prefix outranks subsequence', () => {
  assert.ok(commandMatchScore('git', ['GitHub']) >
            commandMatchScore('gthb', ['GitHub']));
});

test('an earlier word outranks a later one', () => {
  assert.ok(commandMatchScore('iss', ['Issues on GitHub']) >
            commandMatchScore('iss', ['GitHub Issues']));
});

test('the best word in the field is the one scored', () => {
  assert.equal(commandMatchScore('iss', ['GitHub Issues']),
               commandMatchScore('iss', ['zzzzzz Issues']));
});

test('the best field is the one scored', () => {
  assert.equal(commandMatchScore('git', ['Something', 'github.com']),
               commandMatchScore('git', ['github.com']));
});

test('scores are ordered, not absolute', () => {
  // A long field must not out-penalise a hit into a miss, which is what a
  // position penalty with no floor would do.
  const long = 'aaaa bbbb cccc dddd eeee ffff gggg hhhh iiii jjjj kkkk llll ' +
      'mmmm nnnn oooo pppp qqqq rrrr ssss tttt uuuu vvvv wwww xxxx yyyy ' +
      'zzzz target';
  assert.ok(commandMatchScore('target', [long]) > 0);
});

// --- the two implementations agree -------------------------------------------

test('the C++ suite pins these same cases', () => {
  // A guard on the pairing itself: the file below is the other half of this
  // rule, and a rename that leaves this suite alone is the drift to catch.
  const twin = path.join(
      SRC, 'chrome/browser/ui/atrium/atrium_command_bar_match_text_unittest.cc');
  assert.ok(fs.existsSync(twin), `${twin} is the C++ half of this matcher`);
});
