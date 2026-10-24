#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The Notification timeout row's five steps, wired to toast-stack.js's
// dismissAfterMs seam.
//
//   node --test build/scripts/notification-timeout-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
installDOM({pathname: '/sidebar'});

const {showToast} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/toast-stack.js'));
const {edgeGlow} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/edge-glow.js'));
const {NOTIFICATION_TIMEOUT_STEPS, shouldShowToast, dismissAfterMsForTimeout,
       migrateNotificationTimeoutStep} =
    await import(path.join(
        SRC, 'chrome/browser/resources/atrium/notification-timeout.js'));

// A clock that only moves when the test says so, matching toast-stack-test's
// own fake clock - the 260ms "bye" removal must not make the suite wait it out.
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
  };
}

function makeHost() {
  const host = document.createElement('div');
  host.setAttribute('id', 'atrium-root');
  document.body.append(host);
  return host;
}

test('the five-step order is preserved: 1s, 3s, 5s, 10s, Keep', () => {
  assert.deepEqual(NOTIFICATION_TIMEOUT_STEPS,
      ['oneSecond', 'threeSeconds', 'fiveSeconds', 'tenSeconds', 'keep']);
});

test('a stored off or thirtySeconds migrates to the nearest step left', () => {
  assert.equal(migrateNotificationTimeoutStep('off'), 'oneSecond');
  assert.equal(migrateNotificationTimeoutStep('thirtySeconds'), 'tenSeconds');
  for (const step of NOTIFICATION_TIMEOUT_STEPS) {
    assert.equal(migrateNotificationTimeoutStep(step), step,
                 `${step} is already a current step`);
  }
});

test('every current step shows a toast, retired off included', () => {
  for (const step of [...NOTIFICATION_TIMEOUT_STEPS, 'off']) {
    assert.equal(shouldShowToast(step), true, `${step} shows a toast`);
  }
});

test('edge glow fires regardless of the notification timeout setting', () => {
  const host = makeHost();
  const el = edgeGlow('#ff0000', 'ring', {host});
  assert.ok(host.querySelector('.edgefx'), 'the glow rendered');
  assert.equal(el.className, 'edgefx ring');
});

// Setting is 1s, 3s, 5s, or 10s - each toast auto-dismisses that many
// seconds after it appears. A stored off or thirtySeconds resolves to 1s or
// 10s, the step it migrated to.

for (const [step, seconds] of
     [['oneSecond', 1], ['threeSeconds', 3], ['fiveSeconds', 5], ['tenSeconds', 10],
      ['off', 1], ['thirtySeconds', 10]]) {
  test(`${step} auto-dismisses ${seconds}s after the toast appears`, () => {
    const host = makeHost();
    const clock = fakeClock();
    const ms = dismissAfterMsForTimeout(step);
    assert.equal(ms, seconds * 1000);
    showToast({color: '#ff0000', title: 'Signal'}, {host, clock, dismissAfterMs: ms});
    clock.advance(ms - 1);
    assert.equal(host.querySelectorAll('.toast').length, 1,
                 'not yet - one ms early');
    clock.advance(1);
    clock.advance(260);  // the .bye removal delay
    assert.equal(host.querySelector('.toasts'), null,
                 'the toast is gone once its own duration elapses');
  });
}

// Setting is Keep - a toast stays on screen until the user dismisses it
// with the close button.

test('keep never schedules an automatic close', () => {
  const host = makeHost();
  const clock = fakeClock();
  const ms = dismissAfterMsForTimeout('keep');
  assert.equal(ms, undefined);
  const card = showToast(
      {color: '#ff0000', icon: document.createElement('span'), title: 'Signal'},
      {host, clock, dismissAfterMs: ms});
  clock.advance(10 * 60 * 1000);  // ten minutes - nothing to advance
  assert.ok(host.contains(card), 'the card is still there');
  card.querySelector('.ctcl').click();
  clock.advance(260);
  assert.equal(host.querySelector('.toasts'), null,
               'the close button is still what dismisses it');
});
