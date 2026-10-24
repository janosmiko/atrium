#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// drives the real installSkylight() against a real
// #atrium-root, like skylight-wiring-test.mjs does for its two siblings.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

test('skylight.js imports skylight-weather.js', () => {
  const src = fs.readFileSync(path.join(ATRIUM_DIR, 'skylight.js'), 'utf8');
  assert.ok(/from '\.\/skylight-weather\.js'/.test(src),
      'skylight.js must wire in skylight-weather.js the same way it already ' +
      'wires day-cycle and ambient');
});

const RAIN_STREAK =
    'repeating-linear-gradient(100deg, rgba(160,190,220,.07) 0 1px, transparent 1px 9px)';
const RAIN_WASH = 'linear-gradient(rgba(110,122,134,.22),rgba(110,122,134,.22))';

installDOM({pathname: '/sidebar'});
const root = document.createElement('div');
root.setAttribute('id', 'atrium-root');
document.body.append(root);

// installSkylight is the exact function atrium.js imports for /sidebar.
const {installSkylight} = await import(path.join(ATRIUM_DIR, 'skylight.js'));
const layer = installSkylight(root, {weather: 'rain'});

test('the weather wash reaches the real .bglayer atrium.js\'s own install path creates', () => {
  assert.equal(document.getElementById('atrium-root'), root,
      'must be the element atrium.html provides');
  assert.equal(root.querySelector('.bglayer'), layer,
      'installSkylight must return the same real .bglayer it installed onto');
  assert.ok(layer.style.background.includes(RAIN_STREAK),
      'Rain\'s diagonal streak must reach the real element, painted by ' +
      'skylight.js calling into skylight-weather.js - not by this test');
  assert.ok(layer.style.background.includes(RAIN_WASH));
});

test('weather, day cycle, and ambient all compose on that element', () => {
  assert.ok(layer.style.background.includes('rgba(20, 24, 29, 0)'),
      'day-cycle\'s base gradient must still be present under the weather wash');
  assert.ok(layer.style.background.includes(RAIN_WASH),
      'the weather wash must still be present, not overwritten by day-cycle\'s ' +
      'own repaint');
  assert.ok(layer.matches('.skyambient .bglayer'),
      'ambient\'s class toggle and the weather/day-cycle paint must land on ' +
      'the same ancestor/descendant pair skylight-ambient.css expects');
});

test.after(() => {
  clearInterval(document.getElementById('atrium-root')
      .querySelector('.bglayer')._skyClockHandle);
});
