#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the pushed step, not the 'keep' fallback, must decide.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');

const h = installDOM({pathname: '/sidebar'});
const {installSidebar} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/sidebar.js'));

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

function fresh() {
  h.reset();
  const root = document.createElement('div');
  h.body.appendChild(root);
  installSidebar(root);
  return root;
}

const TAB = {id: '42', title: 'Quarterly Report', color: '#7FB3D5'};

test('atriumSetNotificationTimeout lands on window under the name C++ calls', () => {
  fresh();
  // PushNotificationTimeoutToSidebar executes the literal
  // "window.atriumSetNotificationTimeout(...)" - renaming this breaks the
  // push with no error on either side.
  assert.equal(typeof h.window.atriumSetNotificationTimeout, 'function');
});

test('a step pushed before any snooze return is what a later return obeys', () => {
  const root = fresh();
  h.window.atriumSetNotificationTimeout('off');
  // No notificationTimeoutStep in opts - the pushed value must be the only
  // thing deciding this, not a hand-passed one.
  h.window.atriumTabSnoozeReturned(TAB, {});
  assert.ok(root.querySelector('.toast'),
            'the retired off migrates to oneSecond, which still shows a toast');
  assert.ok(root.querySelector('.edgefx'), 'the edge glow fires too');
});

test('a threeSeconds push makes the toast auto-dismiss at 3000ms, not the keep default', () => {
  const root = fresh();
  const clock = fakeClock();
  h.window.atriumSetNotificationTimeout('threeSeconds');
  h.window.atriumTabSnoozeReturned(TAB, {clock});
  clock.advance(2999);
  assert.equal(root.querySelectorAll('.toast').length, 1, 'not yet - one ms early');
  clock.advance(1);
  clock.advance(260);  // the .bye removal delay
  assert.equal(root.querySelector('.toasts'), null,
               'the pushed step must control dismissal, not the keep default ' +
               'atriumTabSnoozeReturned falls back to with no push at all');
});

test('a caller-supplied notificationTimeoutStep still overrides the pushed one', () => {
  // Preserves the existing test harness seam (snoozed-tab-return-test.mjs
  // hand-passes the step directly) alongside the new production path.
  const root = fresh();
  h.window.atriumSetNotificationTimeout('off');
  h.window.atriumTabSnoozeReturned(TAB, {notificationTimeoutStep: 'keep'});
  assert.ok(root.querySelector('.toast'),
            'an explicit opts value must win over the stored push');
});
