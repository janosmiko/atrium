#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// overlay.css carries the brand book's themes. The book is the
// source; these read it and compare, so a theme edited in one place and not
// the other fails here rather than shipping half-applied.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const REPO = path.resolve(import.meta.dirname, '../..');
const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');

const brandBook = fs.readFileSync(
    path.join(REPO, 'docs/brand/brand-book.html'), 'utf8');
const overlay = fs.readFileSync(
    path.join(SRC, 'chrome/browser/resources/atrium/overlay.css'), 'utf8');

test('the brand book and product share the 16px large-surface radius', () => {
  assert.match(overlay, /--ov-radius-card:\s*16px;/);
  assert.match(overlay, /--ov-radius-compact:\s*10px;/);
  assert.match(brandBook, /Large cards and notification toasts round at 16/);
  assert.match(brandBook, /floating address bar, mouse hints, and hovered-URL status — use 10/);
  assert.doesNotMatch(brandBook, /border-radius:\s*18px|ovRadius\(ov,\s*18/);
});

// Matches the generator: fold accents, then anything not alphanumeric becomes
// a hyphen. 'Rosé Pine' has to reach 'rose-pine' or the class never matches.
const slug = (name) => name.normalize('NFKD').replace(/[̀-ͯ]/g, '')
    .toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '');

const triplet = (hex) => {
  const h = hex.replace('#', '');
  return [0, 2, 4].map((i) => parseInt(h.slice(i, i + 2), 16)).join(' ');
};

function bookThemes() {
  const block = /const themes = \{([\s\S]*?)\n\};/.exec(brandBook);
  assert.ok(block, 'the brand book has no themes object');
  const out = new Map();
  const row = /'?([A-Za-zé][A-Za-zé ]*)'?\s*:\s*\{\s*ink:\s*'(#[0-9A-Fa-f]{6})',\s*paper:\s*'(#[0-9A-Fa-f]{6})',\s*accent:\s*'(#[0-9A-Fa-f]{6})',\s*soft:\s*'(#[0-9A-Fa-f]{6})'\s*\}/g;
  for (const m of block[1].matchAll(row)) {
    out.set(m[1].trim(), {ink: m[2], paper: m[3], accent: m[4], soft: m[5]});
  }
  return out;
}

function cssThemes() {
  const out = new Map();
  const rule = /html\.atrium-theme-([a-z0-9-]+)\s*\{([^}]*)\}/g;
  for (const m of overlay.matchAll(rule)) {
    const body = m[2];
    const read = (prop) => {
      const hit = new RegExp(`--ov-${prop}:\\s*([^;]+);`).exec(body);
      return hit ? hit[1].trim() : null;
    };
    out.set(m[1], {
      accent: read('accent'), ink: read('ink'),
      paper: read('paper'), soft: read('soft'),
    });
  }
  return out;
}

test('every brand book theme has a class in overlay.css', () => {
  const book = bookThemes();
  assert.equal(book.size, 19, `the book should carry 19 themes, found ${book.size}`);
  const css = cssThemes();
  const missing = [...book.keys()].filter((n) => !css.has(slug(n)));
  assert.deepEqual(missing, [], `no class for: ${missing.join(', ')}`);
});

test('overlay.css invents no theme the book does not have', () => {
  const known = new Set([...bookThemes().keys()].map(slug));
  const extra = [...cssThemes().keys()].filter((s) => !known.has(s));
  assert.deepEqual(extra, [], `not in the brand book: ${extra.join(', ')}`);
});

// The values, not just the names. A class that exists with the wrong accent is
// the failure this pair is really for - the names would pass on their own.
for (const [name, want] of bookThemes()) {
  test(`${name} carries the book's own four values`, () => {
    const got = cssThemes().get(slug(name));
    assert.ok(got, `no class html.atrium-theme-${slug(name)}`);
    assert.equal(got.accent.toUpperCase(), want.accent.toUpperCase());
    assert.equal(got.paper.toUpperCase(), want.paper.toUpperCase());
    assert.equal(got.soft.toUpperCase(), want.soft.toUpperCase());
    // --ov-ink is consumed as rgb(var(--ov-ink) / alpha), so it is a triplet
    // where the other three are colours. A hex here would break every rule
    // that reads it without failing to parse.
    assert.equal(got.ink, triplet(want.ink),
        `${name}'s ink must be the triplet for ${want.ink}`);
  });
}

// The System accent is not a brand book entry: it reads the live macOS colour,
// so its class maps pushed properties instead of carrying literals. It must
// stay out of the atrium-theme- namespace or the two tests above claim it.
test('the System accent maps every token the themes carry as literals', () => {
  const rule =
      /html\.atrium-overlay-accent-system\s*\{([^}]*)\}/.exec(overlay);
  assert.ok(rule, 'overlay.css has no html.atrium-overlay-accent-system');
  for (const prop of ['accent', 'accent-ink', 'ink', 'paper', 'soft']) {
    const hit = new RegExp(`--ov-${prop}:\\s*var\\(--atrium-system-`)
                    .exec(rule[1]);
    assert.ok(hit, `--ov-${prop} does not read a pushed --atrium-system- value`);
  }
});

test('the System accent is not a brand book theme class', () => {
  assert.equal(cssThemes().has('system'), false,
      'html.atrium-theme-system would be a theme with no book entry');
});

test('a theme sets all four tokens, never a subset', () => {
  for (const [slugName, got] of cssThemes()) {
    for (const prop of ['accent', 'ink', 'paper', 'soft']) {
      assert.ok(got[prop],
          `html.atrium-theme-${slugName} sets no --ov-${prop}, so it would ` +
          'inherit half a palette from whatever came before it');
    }
  }
});

// The confirm-to-quit HUD and the peek card are native views with no CSS to
// read, so atrium_overlay_themes.cc carries the same table. Pinned here the
// way overlay.css is, or a theme edited in the book ships wrong on them.
function cppThemes() {
  const cpp = fs.readFileSync(
      path.join(SRC, 'chrome/browser/ui/atrium/atrium_overlay_themes.cc'),
      'utf8');
  const out = new Map();
  const row =
      /\{"([a-z0-9-]+)",\s*0x([0-9A-Fa-f]{6}),\s*0x([0-9A-Fa-f]{6}),\s*0x([0-9A-Fa-f]{6}),\s*0x([0-9A-Fa-f]{6})\}/g;
  for (const m of cpp.matchAll(row)) {
    out.set(m[1], {accent: '#' + m[2], ink: '#' + m[3],
                   paper: '#' + m[4], soft: '#' + m[5]});
  }
  return out;
}

test('the C++ theme table carries every brand book theme and nothing else', () => {
  const book = new Map([...bookThemes()].map(([n, v]) => [slug(n), v]));
  const cpp = cppThemes();
  assert.deepEqual([...cpp.keys()].sort(), [...book.keys()].sort());
  for (const [name, want] of book) {
    const got = cpp.get(name);
    for (const prop of ['accent', 'ink', 'paper', 'soft']) {
      assert.equal(got[prop].toUpperCase(), want[prop].toUpperCase(),
          `${name}'s ${prop} differs between the book and the C++ table`);
    }
  }
});
