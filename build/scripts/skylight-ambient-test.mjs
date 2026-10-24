#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// ambient breathing loop, ported from swipe-lab.html:958-961.
// No CSS engine here, so timing and reduced-motion are read off the stylesheet text.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

const css = fs.readFileSync(path.join(ATRIUM_DIR, 'skylight-ambient.css'), 'utf8');

const {setSkylightAmbient} = await import(
    path.join(ATRIUM_DIR, 'skylight-ambient.js'));

const h = installDOM({pathname: '/sidebar'});

function makeHost() {
  h.reset();
  const host = document.createElement('div');
  host.setAttribute('id', 'atrium-root');
  document.body.append(host);
  return host;
}

// Collapses whitespace and trailing semicolons so a rule can be formatted
// for readability without the assertion drifting from the value it checks.
const norm = (s) => s.replace(/\s+/g, ' ').replace(/;\s*$/, '').trim();

test('Ambient on drifts the background on the lab\'s 26s ease-in-out alternate loop', () => {
  const rule = css.match(/\.skyambient \.bglayer\s*{\s*animation:\s*([^;]+);?\s*}/);
  assert.ok(rule, 'no .skyambient .bglayer animation rule found');
  assert.equal(norm(rule[1]), 'ambdrift 26s ease-in-out infinite alternate');

  const keyframes = css.match(/@keyframes ambdrift\s*{\s*from\s*{([^}]+)}\s*to\s*{([^}]+)}\s*}/);
  assert.ok(keyframes, 'no @keyframes ambdrift found');
  assert.equal(norm(keyframes[1]), 'transform: scale(1.04) translate(-0.6%,-0.5%)');
  assert.equal(norm(keyframes[2]), 'transform: scale(1.07) translate(0.7%,0.6%)');

  const host = makeHost();
  setSkylightAmbient(true, {host});
  assert.ok(host.classList.contains('skyambient'),
      'setSkylightAmbient(true) must apply the class the animation keys off');
});

test('prefers-reduced-motion drops the animation', () => {
  const media = css.match(
      /@media \(prefers-reduced-motion:\s*reduce\)\s*{\s*\.skyambient \.bglayer\s*{\s*animation:\s*([^;]+);?\s*}\s*}/);
  assert.ok(media, 'no reduced-motion override for .skyambient .bglayer found');
  assert.equal(norm(media[1]), 'none');
});

test('Ambient off leaves the background static', () => {
  const host = makeHost();
  assert.ok(!host.classList.contains('skyambient'),
      'no class means the .skyambient .bglayer rule cannot match');

  setSkylightAmbient(true, {host});
  setSkylightAmbient(false, {host});
  assert.ok(!host.classList.contains('skyambient'),
      'turning Ambient off must remove the class again');
});
