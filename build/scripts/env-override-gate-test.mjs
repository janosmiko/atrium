#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the ATRIUM_*_FILE path overrides are test seams, and a shipped build
// must ignore them.
//
//   node --test build/scripts/env-override-gate-test.mjs
//
// The settings path is a WRITE target as well as a read one, so anything that can
// set the environment could redirect a 0600 write to a file of its choosing.
//
// Read from the source rather than built, because proving it by compiling would
// need an is_official_build of Chromium - hours, and branding requirements this
// checkout does not carry. What is checkable here is the shape: every read of one
// of these vars in PRODUCTION code sits behind an OFFICIAL_BUILD gate. That also
// catches the regression this is really guarding against, which is a fifth
// override added later without one.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM = path.join(SRC, 'chrome/browser/ui/atrium');

// Tests and unit tests SET these vars deliberately; only production reads matter.
const isProduction = (name) =>
    (name.endsWith('.cc') || name.endsWith('.mm')) &&
    !name.includes('unittest') && !name.includes('browsertest');

function productionReads() {
  const hits = [];
  for (const entry of fs.readdirSync(ATRIUM).filter(isProduction)) {
    const text = fs.readFileSync(path.join(ATRIUM, entry), 'utf8');
    for (const match of text.matchAll(/ATRIUM_[A-Z_]*FILE/g)) {
      // getenv counts as a read exactly like GetVar - one site used it, and that
      // is how it survived the first sweep.
      const lineStart = text.lastIndexOf('\n', match.index) + 1;
      const line = text.slice(lineStart, text.indexOf('\n', match.index));
      // A comment naming the variable is not a read of it. Skipped by looking at
      // the line rather than by trusting the name to appear only in code - the
      // doc comments here name all four deliberately.
      if (line.trimStart().startsWith('//')) {
        continue;
      }
      hits.push({file: entry, name: match[0], at: match.index, text, line});
    }
  }
  return hits;
}

test('every path override read in production is gated, directly or by the helper', () => {
  const reads = productionReads();
  // Load-bearing: with no reads found the loop below is vacuous and would pass on
  // a checkout where the whole seam had been renamed.
  assert.notEqual(reads.length, 0, 'no ATRIUM_*_FILE reads found at all');

  for (const {file, name, at, text, line} of reads) {
    const before = text.slice(0, at);
    const lastIf = before.lastIndexOf('#if');
    const inConditional = lastIf > before.lastIndexOf('#endif');
    const gate = inConditional
        ? before.slice(lastIf, before.indexOf('\n', lastIf))
        : '';
    // Two acceptable shapes. Either the read itself is inside an OFFICIAL_BUILD
    // conditional, or it is handed to DevOnlyPathOverride, whose body carries the
    // gate for every caller - the next test pins that so the helper cannot lose
    // it and quietly re-open all four.
    const viaHelper = line.includes('DevOnlyPathOverride');
    assert.ok((inConditional && gate.includes('OFFICIAL_BUILD')) || viaHelper,
              `${file} reads ${name} with no OFFICIAL_BUILD gate and not through ` +
              `DevOnlyPathOverride` + (gate ? ` (nearest: ${gate.trim()})` : ''));
  }
});

test('the helper that gates for its callers is itself gated', () => {
  const text = fs.readFileSync(
      path.join(ATRIUM, 'atrium_settings_source.cc'), 'utf8');
  const at = text.indexOf('std::string DevOnlyPathOverride');
  assert.notEqual(at, -1, 'DevOnlyPathOverride is gone; every caller is ungated');
  const body = text.slice(at, text.indexOf('\n}', at));
  assert.ok(body.includes('OFFICIAL_BUILD'),
            'DevOnlyPathOverride no longer gates on OFFICIAL_BUILD, so every ' +
            'override it serves is live in a shipped build');
  // And it returns nothing on that branch, rather than gating and reading anyway.
  assert.match(body, /defined\(OFFICIAL_BUILD\)\s*\n\s*return std::string\(\);/);
});

test('the four known overrides are all still gated', () => {
  // Named so that dropping one from the source is a failure rather than a
  // silently smaller sweep - the test above passes trivially on an empty set for
  // any single var.
  const found = new Set(productionReads().map((r) => r.name));
  for (const name of ['ATRIUM_SPACES_FILE', 'ATRIUM_SETTINGS_FILE',
                      'ATRIUM_HOTKEYS_FILE', 'ATRIUM_ROUTING_FILE']) {
    assert.ok(found.has(name), `${name} is no longer read in production code`);
  }
});
