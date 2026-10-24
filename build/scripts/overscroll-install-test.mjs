#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the rubber band's wiring onto a scroll zone.
//
//   make webui-test
//
// The model has its own suite; what is asserted here is only what the model
// cannot see - which events get claimed, what lands in the style, and whether
// teardown really lets go. Each of those fails quietly in its own way: a band
// that claims a horizontal delta drags the tab list during a swipe, a band that
// writes scrollTop instead of transform relayouts every row and reopens the
// page-scroll bug, and a band left installed keeps a rebuilt
// zone alive and animating.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';
import {makeFakeClock} from './fake-clock.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const h = installDOM({pathname: '/sidebar'});
const {installOverscroll} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/overscroll-install.js'));

// A zone with NO overflow - the case the task is about.
function makeZone({scrollHeight = 200, clientHeight = 400, scrollTop = 0} = {}) {
  const zone = document.createElement('div');
  zone.className = 'scrollzone';
  const inner = document.createElement('div');
  inner.className = 'scrollinner';
  zone.append(inner);
  zone.inner = inner;
  Object.defineProperty(zone, 'scrollHeight', {value: scrollHeight, configurable: true});
  Object.defineProperty(zone, 'clientHeight', {value: clientHeight, configurable: true});
  zone.scrollTop = scrollTop;
  document.body.append(zone);
  return zone;
}

// dom-shim events do not carry arbitrary fields through dispatchEvent
// reliably, so the listener is invoked directly with a real wheel-shaped
// object. That is the contract under test - what installOverscroll does with
// an event - not how the browser delivers it.
function fire(zone, {deltaY = 0, deltaX = 0, momentum} = {}) {
  let prevented = false;
  const listeners = zone.__wheelListeners || [];
  for (const fn of listeners) {
    fn({deltaX, deltaY, momentum, preventDefault: () => {
      prevented = true;
    }});
  }
  return prevented;
}

// Capture the listener installOverscroll registers, since the shim's
// dispatchEvent will not carry deltaY.
function installCapturing(zone, opts) {
  zone.__wheelListeners = [];
  const realAdd = zone.addEventListener.bind(zone);
  zone.addEventListener = (type, fn, o) => {
    if (type === 'wheel') {
      zone.__wheelListeners.push(fn);
    }
    return realAdd(type, fn, o);
  };
  const teardown = installOverscroll(zone, {reducedMotion: true, requestFrame: () => {}, ...opts});
  return teardown;
}

test('omitting reducedMotion reads globalThis.matchMedia for the fallback', () => {
  const zone = makeZone();
  let queried = null;
  h.setMatchMedia((query) => {
    queried = query;
    return true;
  });
  installCapturing(zone, {reducedMotion: undefined});
  assert.equal(queried, '(prefers-reduced-motion: reduce)');
});

test('a vertical wheel on a list with nothing to scroll is claimed', () => {
  const zone = makeZone();
  installCapturing(zone);
  const prevented = fire(zone, {deltaY: -60});
  assert.equal(prevented, true, 'the band must take the gesture');
  assert.match(zone.inner.style.transform, /translateY\(/,
               `expected a transform, got ${JSON.stringify(zone.inner.style.transform)}`);
});

// The constraint from the task: transform, never scrollTop. A scrollTop-based
// band relayouts the rows and reopens the page-scroll bug.
test('the band paints with transform and never touches scrollTop', () => {
  const zone = makeZone();
  installCapturing(zone);
  fire(zone, {deltaY: -60});
  assert.equal(zone.scrollTop, 0, 'scrollTop must be left alone');
});

// The distinction the whole design turns on. Moving the ZONE moves the
// viewport, and a viewport carries its clip box - the list would slide down
// over the space dots instead of revealing space above it.
test('the content moves and the zone itself stays put', () => {
  const zone = makeZone();
  installCapturing(zone);
  fire(zone, {deltaY: -60});
  assert.match(zone.inner.style.transform, /translateY\(/);
  assert.equal(zone.style.transform || '', '', 'the viewport must not move');
});

test('a horizontal wheel is left for the rail', () => {
  const zone = makeZone();
  installCapturing(zone);
  const prevented = fire(zone, {deltaX: -60, deltaY: 0});
  assert.equal(prevented, false, 'a swipe must pass straight through');
  assert.equal(zone.inner.style.transform || '', '');
});

// A diagonal gesture with more horizontal than vertical is still a swipe. The
// 1.2 ratio matches the rail's own axis test, so neither surface claims what
// the other wants.
test('a mostly-horizontal diagonal is left for the rail', () => {
  const zone = makeZone();
  installCapturing(zone);
  assert.equal(fire(zone, {deltaX: -60, deltaY: -20}), false);
});

// Scrolled here, not left native: with a non-passive listener on the zone,
// whether a phased gesture natively scrolls is a per-gesture race - the VM
// probe read 0px from 300px gestures whose every event arrived.
test('a mid-list wheel scrolls the zone right here', () => {
  const zone = makeZone({scrollHeight: 1200, clientHeight: 400, scrollTop: 300});
  installCapturing(zone);
  assert.equal(fire(zone, {deltaY: 60}), true, 'the zone owns the scroll now');
  assert.equal(zone.scrollTop, 360);
  assert.equal(fire(zone, {deltaY: -120}), true);
  assert.equal(zone.scrollTop, 240);
  assert.equal(zone.inner.style.transform || '', '', 'a scroll is not the band');
});

test('the scroll clamps at the edges, so the band sees a true edge', () => {
  const zone = makeZone({scrollHeight: 1200, clientHeight: 400, scrollTop: 20});
  installCapturing(zone);
  fire(zone, {deltaY: -60});
  assert.equal(zone.scrollTop, 0);
  zone.scrollTop = 790;
  fire(zone, {deltaY: 60});
  assert.equal(zone.scrollTop, 800);
});

test('a down-scroll from the top scrolls, a pull past the top bands', () => {
  const zone = makeZone({scrollHeight: 1200, clientHeight: 400, scrollTop: 0});
  installCapturing(zone);
  fire(zone, {deltaY: 60});
  assert.equal(zone.scrollTop, 60);
  assert.equal(zone.inner.style.transform || '', '');
  zone.scrollTop = 0;
  fire(zone, {deltaY: -60});
  assert.match(zone.inner.style.transform, /translateY\(/,
               'pulling past the top is still the band');
  assert.equal(zone.scrollTop, 0);
});

test('teardown removes the listener and clears the transform', () => {
  const zone = makeZone();
  const teardown = installCapturing(zone);
  fire(zone, {deltaY: -60});
  assert.match(zone.inner.style.transform, /translateY\(/);
  teardown();
  assert.equal(zone.inner.style.transform, '', 'a rebuilt zone must not inherit a band');
});

test('installing on nothing is a no-op rather than a throw', () => {
  const teardown = installOverscroll(null);
  assert.equal(typeof teardown, 'function');
  teardown();
});

// every event used to reset the same silence timer, finger or
// momentum, so the spring could not start until the whole tail fell quiet.
test('the spring starts on finger lift, not after the whole momentum tail', () => {
  const zone = makeZone();
  const clock = makeFakeClock();
  let frameCb = null;
  const requestFrame = (cb) => {
    frameCb = cb;
  };
  installCapturing(zone, {reducedMotion: false, clock, requestFrame});

  fire(zone, {deltaY: -20, momentum: false});
  fire(zone, {deltaY: -20, momentum: false});
  fire(zone, {deltaY: -20, momentum: false});
  assert.match(zone.inner.style.transform, /translateY\(/, 'the band engaged');

  for (let i = 0; i < 10; i++) {
    fire(zone, {deltaY: -5, momentum: true});
    clock.advance(20);
  }

  assert.ok(frameCb,
      'the spring loop must start as soon as a momentum event proves the ' +
      'fingers lifted, not only after the whole tail falls silent');

  let t = 0;
  for (let i = 0; i < 60 && zone.inner.style.transform; i++) {
    t += 16;
    frameCb(t);
  }
  assert.equal(zone.inner.style.transform, '', 'the band must reach rest');
  assert.ok(t < 500,
      `the return must finish close to the spring settle duration, not ` +
      `the length of the momentum tail, took ${t}ms of frames`);
});
