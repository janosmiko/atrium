#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// edgeGlow(), ported from swipe-lab.html's ambient edge signal.
//
// Sweep's ::before pseudo-element spins forever underneath the outer fade, so
// only the host element's own animationend may remove it - the guard below.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const h = installDOM({pathname: '/sidebar'});
const {edgeGlow} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/edge-glow.js'));

// A clock that only moves when the test says so, matching pill-fade's own
// fake clock - a suite that waited out the real 2.2s/4s/8s durations would be
// both slow and at the mercy of scheduler jitter.
function fakeClock() {
  let next = 1;
  const timers = new Map();
  return {
    set(ms, fn) {
      const id = next++;
      timers.set(id, {at: ms, fn});
      return id;
    },
    clear(id) {
      timers.delete(id);
    },
    advance(ms) {
      for (const [id, t] of [...timers.entries()]) {
        t.at -= ms;
        if (t.at <= 0) {
          timers.delete(id);
          t.fn();
        }
      }
    },
    get pending() {
      return timers.size;
    },
  };
}

// Each test gets a clean body - #atrium-root's id would otherwise collide
// with the one a previous test left behind and querySelector would return
// the wrong element.
function makeHost() {
  h.reset();
  const host = document.createElement('div');
  host.setAttribute('id', 'atrium-root');
  document.body.append(host);
  return host;
}

for (const mode of ['ring', 'pulse', 'sweep']) {
  test(`${mode} renders one .edgefx with the caller's color and mode class`, () => {
    const host = makeHost();
    const el = edgeGlow('#ff0000', mode, {host});
    assert.equal(host.querySelectorAll('.edgefx').length, 1);
    assert.equal(el.className, `edgefx ${mode}`);
    assert.equal(el.style['--efc'], '#ff0000');
  });
}

test('the glow steals neither pointer nor keyboard focus', () => {
  const host = makeHost();
  const el = edgeGlow('#ff0000', 'ring', {host});
  assert.equal(el.hasAttribute('tabindex'), false);
  assert.equal(document.activeElement, document.body,
               'appending the glow must not move focus onto it');
});

test('animationend on the glow itself removes it from the DOM', () => {
  const host = makeHost();
  const el = edgeGlow('#ff0000', 'pulse', {host});
  el.dispatchEvent(new Event('animationend'));
  assert.equal(host.querySelectorAll('.edgefx').length, 0);
});

test('an animationend that bubbled from something else is ignored', () => {
  const host = makeHost();
  const el = edgeGlow('#ff0000', 'sweep', {host});
  const other = document.createElement('div');
  el.append(other);
  const bubbled = new Event('animationend');
  bubbled.target = other;
  el.dispatchEvent(bubbled);
  assert.equal(host.querySelectorAll('.edgefx').length, 1,
               'only the host element\'s own animationend may clear it, ' +
               'since sweep\'s spinning pseudo-element never stops on its own');
});

test('a second call replaces the first glow rather than stacking two', () => {
  const host = makeHost();
  edgeGlow('#ff0000', 'ring', {host});
  const second = edgeGlow('#00ff00', 'pulse', {host});
  const glows = host.querySelectorAll('.edgefx');
  assert.equal(glows.length, 1, 'the first glow must be gone, not just covered');
  assert.equal(glows[0], second);
  assert.equal(glows[0].style['--efc'], '#00ff00');
});

test('reducedMotion:true still shows a glow and clears it on a timer', () => {
  const host = makeHost();
  const clock = fakeClock();
  const el = edgeGlow('#ff0000', 'ring', {host, reducedMotion: true, clock});
  assert.equal(host.querySelectorAll('.edgefx').length, 1,
               'a glow must still show under reduced motion');
  assert.equal(el.className, 'edgefx ring',
               'no animated-transition modifier is added to the class');
  clock.advance(2199);
  assert.equal(host.querySelectorAll('.edgefx').length, 1, 'not yet - one ms early');
  clock.advance(1);
  assert.equal(host.querySelectorAll('.edgefx').length, 0,
               'it must clear itself once the mode\'s own duration elapses');
});

test('reducedMotion clears pulse and sweep on their own durations too', () => {
  const host = makeHost();
  const clock = fakeClock();
  edgeGlow('#ff0000', 'pulse', {host, reducedMotion: true, clock});
  clock.advance(3999);
  assert.equal(host.querySelectorAll('.edgefx').length, 1);
  clock.advance(1);
  assert.equal(host.querySelectorAll('.edgefx').length, 0);

  const clock2 = fakeClock();
  edgeGlow('#ff0000', 'sweep', {host, reducedMotion: true, clock: clock2});
  clock2.advance(7999);
  assert.equal(host.querySelectorAll('.edgefx').length, 1);
  clock2.advance(1);
  assert.equal(host.querySelectorAll('.edgefx').length, 0);
});

test('reducedMotion:false (default) never schedules the fallback timer', () => {
  const host = makeHost();
  let scheduled = false;
  const clock = {
    set: (ms, fn) => {
      scheduled = true;
      return 1;
    },
    clear: () => {},
  };
  edgeGlow('#ff0000', 'ring', {host, reducedMotion: false, clock});
  assert.equal(scheduled, false,
               'the animated path relies on animationend, not a timer');
});

test('omitting reducedMotion falls back to matchMedia, and honours it', () => {
  const host = makeHost();
  h.setMatchMedia(() => true);
  const clock = fakeClock();
  const el = edgeGlow('#ff0000', 'ring', {host, clock});
  assert.equal(el.className, 'edgefx ring',
               'matchMedia() reporting reduced motion must take the same path '
                   + 'reducedMotion:true does');
  clock.advance(2200);
  assert.equal(host.querySelectorAll('.edgefx').length, 0,
               'the fallback path must still self-clear on the mode\'s timer');
});

test('omitting reducedMotion with no reduced-motion preference never schedules a timer',
     () => {
       const host = makeHost();
       let scheduled = false;
       const clock = {
         set: (ms, fn) => {
           scheduled = true;
           return 1;
         },
         clear: () => {},
       };
       edgeGlow('#ff0000', 'ring', {host, clock});
       assert.equal(scheduled, false,
                    'matchMedia() reporting no preference must behave like '
                        + 'reducedMotion:false');
     });

test('a rapid reducedMotion retrigger cancels the outgoing glow\'s pending timer', () => {
  const host = makeHost();
  const clock = fakeClock();
  edgeGlow('#ff0000', 'sweep', {host, reducedMotion: true, clock});
  assert.equal(clock.pending, 1, 'the first glow schedules its own clear timer');
  edgeGlow('#00ff00', 'ring', {host, reducedMotion: true, clock});
  assert.equal(clock.pending, 1,
               'replacing the glow must cancel the outgoing timer, not leave it running');
  clock.advance(2200);
  assert.equal(host.querySelectorAll('.edgefx').length, 0,
               'the surviving timer must still clear the second glow\'s own node');
});

test('falls back to #atrium-root when no host is given', () => {
  const host = makeHost();
  const el = edgeGlow('#ff0000', 'ring', {});
  assert.equal(el.parentNode, host);
});

test('missing host entirely is a no-op rather than a throw', () => {
  h.reset();
  const el = edgeGlow('#ff0000', 'ring', {});
  assert.equal(el, null);
});
