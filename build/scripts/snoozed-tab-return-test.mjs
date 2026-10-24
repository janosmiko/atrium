#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// handleSnoozedTabReturn(). The wake event is
// not built, so this seam has no real caller yet.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const h = installDOM({pathname: '/sidebar'});
const {handleSnoozedTabReturn} = await import(path.join(
    SRC, 'chrome/browser/resources/atrium/snoozed-tab-return.js'));

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
  h.reset();
  const host = document.createElement('div');
  host.setAttribute('id', 'atrium-root');
  document.body.append(host);
  return host;
}

const TAB = {id: '42', title: 'Quarterly Report', color: '#7FB3D5'};

// the return shows an edge ring in the tab's space accent color.

test('a snoozed tab\'s return rings the edge in its space accent color', () => {
  const host = makeHost();
  handleSnoozedTabReturn(TAB, {host, notificationTimeoutStep: 'keep'});
  const glow = host.querySelector('.edgefx');
  assert.ok(glow, 'an edge glow must render');
  assert.equal(glow.className, 'edgefx ring');
  assert.equal(glow.style['--efc'], TAB.color);
});

test('a retired off step still rings the edge and now shows a toast too', () => {
  const host = makeHost();
  handleSnoozedTabReturn(TAB, {host, notificationTimeoutStep: 'off'});
  assert.ok(host.querySelector('.edgefx'), 'the glow renders');
  assert.ok(host.querySelector('.toast'), 'off migrates to oneSecond, which shows a toast');
});

// the same return raises a themed toast naming the tab, obeying the
// notification timeout setting.

test('the return also raises a toast naming the returned tab', () => {
  const host = makeHost();
  handleSnoozedTabReturn(TAB, {host, notificationTimeoutStep: 'keep'});
  const card = host.querySelector('.toast');
  assert.ok(card, 'a toast must render alongside the glow');
  assert.equal(card.style['--ctc'], TAB.color);
  assert.equal(card.querySelector('.ctl').textContent, TAB.title);
});

for (const [step, seconds] of
     [['oneSecond', 1], ['threeSeconds', 3], ['fiveSeconds', 5], ['tenSeconds', 10],
      ['off', 1], ['thirtySeconds', 10]]) {
  test(`${step} auto-dismisses the toast ${seconds}s later`, () => {
    const host = makeHost();
    const clock = fakeClock();
    handleSnoozedTabReturn(
        TAB, {host, clock, notificationTimeoutStep: step});
    clock.advance(seconds * 1000 - 1);
    assert.equal(host.querySelectorAll('.toast').length, 1, 'not yet - one ms early');
    clock.advance(1);
    clock.advance(260);  // the .bye removal delay
    assert.equal(host.querySelector('.toasts'), null,
                 'the toast is gone once its own timeout elapses');
  });
}

test('Keep never auto-dismisses the toast', () => {
  const host = makeHost();
  const clock = fakeClock();
  handleSnoozedTabReturn(TAB, {host, clock, notificationTimeoutStep: 'keep'});
  clock.advance(10 * 60 * 1000);
  assert.ok(host.querySelector('.toast'), 'the card is still there');
});

// clicking the toast focuses the returned tab.

test('clicking the toast sends the same focus message a sidebar click sends', () => {
  const host = makeHost();
  handleSnoozedTabReturn(TAB, {host, notificationTimeoutStep: 'keep'});
  host.querySelector('.toast').click();
  assert.deepEqual(h.sent, [['sidebarTabClicked', [TAB.id]]]);
});

test('a caller-supplied focusTab overrides the default chrome.send', () => {
  const host = makeHost();
  let focused;
  handleSnoozedTabReturn(
      TAB, {host, notificationTimeoutStep: 'keep', focusTab: (id) => focused = id});
  host.querySelector('.toast').click();
  assert.equal(focused, TAB.id);
  assert.deepEqual(h.sent, [], 'the override replaces the default, not adds to it');
});

test('a retired off step still renders a toast to click, migrated to oneSecond', () => {
  const host = makeHost();
  handleSnoozedTabReturn(TAB, {host, notificationTimeoutStep: 'off'});
  assert.ok(host.querySelector('.toast'));
});
