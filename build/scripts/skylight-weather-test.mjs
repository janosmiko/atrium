#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// weather mode tints the sky gradient, ported from
// swipe-lab.html:2089-2098 (skyLayers weather washes and rain streak).

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

const {weatherLayers, spaceWeatherBackground, setSkylightWeather} =
    await import(path.join(ATRIUM_DIR, 'skylight-weather.js'));
const {dayCycleLayers} = await import(
    path.join(ATRIUM_DIR, 'skylight-day-cycle.js'));

const h = installDOM({pathname: '/sidebar'});

function makeHost() {
  h.reset();
  const host = document.createElement('div');
  host.setAttribute('id', 'atrium-root');
  document.body.append(host);
  return host;
}

const RAIN_STREAK =
    'repeating-linear-gradient(100deg, rgba(160,190,220,.07) 0 1px, transparent 1px 9px)';
const OVERCAST_WASH = 'linear-gradient(rgba(110,122,134,.16),rgba(110,122,134,.16))';
const RAIN_WASH = 'linear-gradient(rgba(110,122,134,.22),rgba(110,122,134,.22))';

test('Overcast and Rain add the lab\'s translucent wash layer at its fixed alpha', () => {
  assert.deepEqual(weatherLayers('overcast'), [OVERCAST_WASH]);
  assert.deepEqual(weatherLayers('rain'), [RAIN_STREAK, RAIN_WASH]);

  const base = 'linear-gradient(165deg, #0F2B46, #08192B)';
  const got = spaceWeatherBackground(base, {weather: 'overcast', hour: 13});
  assert.equal(
      got, [OVERCAST_WASH, ...dayCycleLayers(13)].join(',') + ',' + base,
      'wash must sit on top of the day-cycle warm/dark tint, matching the lab\'s push order');
});

test('Rain mode additionally renders the diagonal streak layer', () => {
  const layers = weatherLayers('rain');
  assert.equal(layers[0], RAIN_STREAK, 'the streak must be the lab\'s exact repeating-gradient');
  assert.equal(layers.length, 2, 'rain has both the streak and its own wash alpha');

  assert.ok(
      !weatherLayers('overcast').includes(RAIN_STREAK),
      'overcast must not render the rain-only streak');
});

test('Clear mode renders no weather wash, only the day-cycle gradient', () => {
  assert.deepEqual(weatherLayers('clear'), []);

  const base = 'linear-gradient(165deg, #0F2B46, #08192B)';
  const got = spaceWeatherBackground(base, {weather: 'clear', hour: 20.5});
  assert.equal(
      got, dayCycleLayers(20.5).join(',') + ',' + base,
      'clear must match the day-cycle module\'s own output byte for byte');
});

test('changing the weather mode re-renders the gradient with no page reload', () => {
  const host = makeHost();
  const base = 'linear-gradient(165deg, #0F2B46, #08192B)';

  setSkylightWeather(base, {host, weather: 'clear', hour: 13});
  const clearPaint = host.style.background;
  assert.equal(clearPaint, spaceWeatherBackground(base, {weather: 'clear', hour: 13}));

  setSkylightWeather(base, {host, weather: 'rain', hour: 13});
  const rainPaint = host.style.background;
  assert.notEqual(rainPaint, clearPaint, 'switching to Rain must repaint the same host in place');
  assert.equal(rainPaint, spaceWeatherBackground(base, {weather: 'rain', hour: 13}));

  setSkylightWeather(base, {host, weather: 'clear', hour: 13});
  assert.equal(host.style.background, clearPaint, 'switching back to Clear drops the wash again');
});

test('Night Shift off still suppresses the day-cycle tint under a weather wash', () => {
  const base = 'linear-gradient(165deg, #0F2B46, #08192B)';
  const got = spaceWeatherBackground(
      base, {weather: 'rain', nightShift: false, hour: 20.5});
  assert.equal(
      got, [...weatherLayers('rain')].join(',') + ',' + base,
      'nightShift:false must drop the day-cycle layers but keep the weather wash');
});
