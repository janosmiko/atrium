#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// a click said nothing until the page arrived.
//
//   node --test build/scripts/loading-mark-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

const h = installDOM({pathname: '/sidebar'});

const {markLoading, clearLoadingMarks, LOADING_TIMEOUT_MS} =
    await import(path.join(ATRIUM_DIR, 'loading-mark.js'));

// A clock the test drives, so nothing here waits on a real 1.5s timer.
function fakeClock() {
  let now = 0;
  const timers = new Map();
  let next = 1;
  return {
    setTimeout: (fn, ms) => {
      timers.set(next, {fn, at: now + ms});
      return next++;
    },
    clearTimeout: (id) => timers.delete(id),
    advance(ms) {
      now += ms;
      for (const [id, t] of [...timers]) {
        if (t.at <= now) {
          timers.delete(id);
          t.fn();
        }
      }
    },
    pending: () => timers.size,
    now: () => now,
  };
}

const row = () => {
  const el = h.document.createElement('div');
  el.className = 'row tabrow';
  return el;
};

test('marking a row adds the class the sweep keys off', () => {
  const el = row();
  markLoading(el, {clock: fakeClock()});
  assert.equal(el.classList.contains('loading'), true);
});

test('the mark settles on its own so a load that never reports back clears', () => {
  const clock = fakeClock();
  const el = row();
  markLoading(el, {clock});

  clock.advance(LOADING_TIMEOUT_MS - 1);
  assert.equal(el.classList.contains('loading'), true, 'settled early');

  clock.advance(1);
  assert.equal(el.classList.contains('loading'), false, 'never settled');
});

test('the returned settle clears it immediately, for a real load signal', () => {
  const clock = fakeClock();
  const el = row();
  const settle = markLoading(el, {clock});
  settle();
  assert.equal(el.classList.contains('loading'), false);
  assert.equal(clock.pending(), 0, 'the timer outlived the settle');
});

test('a second click restarts the window rather than settling the first early', () => {
  // Two stacked timers would clear the ring LOADING_TIMEOUT_MS after the first
  // click, cutting the second click's feedback short.
  const clock = fakeClock();
  const el = row();
  markLoading(el, {clock});
  clock.advance(LOADING_TIMEOUT_MS - 100);
  markLoading(el, {clock});

  clock.advance(100);
  assert.equal(el.classList.contains('loading'), true,
               'the first click\'s timer settled the second click\'s ring');
  clock.advance(LOADING_TIMEOUT_MS);
  assert.equal(el.classList.contains('loading'), false);
});

test('clearing sweeps every mark and its timer, for a re-render', () => {
  // A mark must not outlive the element that carried it.
  const clock = fakeClock();
  const root = h.document.createElement('div');
  const a = row();
  const b = row();
  root.append(a, b);
  markLoading(a, {clock});
  markLoading(b, {clock});

  clearLoadingMarks(root, {clock});
  assert.equal(root.querySelectorAll('.loading').length, 0);
  assert.equal(clock.pending(), 0, 'timers survived the clear');
});

test('reduced motion swaps each phase for a static, distinct ring treatment', () => {
  // Stopping the animation alone left both phases at the idle ring's
  // own .9 opacity - indistinguishable from a plain selected row. Each phase
  // now gets its own static shape instead.
  const css = fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar.css'), 'utf8');
  const reduced = css.match(/@media \(prefers-reduced-motion: reduce\) {[\s\S]*?\n}/g) || [];
  const ring = reduced.find((block) => block.includes('.loading:not(.receiving)'));
  assert.ok(ring, 'the ring\'s reduced-motion block is gone');

  const phase1 = /\.loading:not\(\.receiving\) > \.brdr::after\s*{([^}]*)}/.exec(ring);
  assert.ok(phase1, 'phase 1 has no reduced-motion rule');
  assert.match(phase1[1], /animation:\s*none/);
  assert.match(phase1[1], /border-style:\s*dashed/);

  const phase2Before = /\.receiving > \.brdr::before\s*{([^}]*)}/.exec(ring);
  assert.ok(phase2Before, 'phase 2\'s arc has no reduced-motion rule');
  assert.match(phase2Before[1], /animation:\s*none/);
  assert.match(phase2Before[1], /opacity:\s*0/,
               'a frozen partial arc must not show');

  const phase2After = /\.receiving > \.brdr::after\s*{([^}]*)}/.exec(ring);
  assert.ok(phase2After, 'phase 2 has no static ring to stand in for the sweep');
  assert.match(phase2After[1], /animation:\s*none/);
  assert.match(phase2After[1], /border-width:\s*3px/,
               'phase 2 must not share idle\'s ring width');
});

test('the sweep spins only on a receiving ring', () => {
  // On the base .brdr::before it ran on every row's hidden ring, and the
  // --sweep repaint pinned an idle window's renderer at full CPU.
  const css = fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar.css'), 'utf8');
  const spins = css.match(/animation:\s*cometspin[^;]*;/g) || [];
  assert.equal(spins.length, 1, 'cometspin is declared once');
  assert.match(css, /\.receiving\s*>\s*\.brdr::before\s*{[^}]*animation:\s*cometspin/,
               'the one declaration sits on the receiving ring');
});

test('loading-mark.js is registered in atrium_resources.grd', () => {
  // An unregistered module is a blank surface at runtime, not a build error.
  const grd = fs.readFileSync(path.join(ATRIUM_DIR, 'atrium_resources.grd'), 'utf8');
  assert.match(grd, /file="loading-mark\.js"/);
});

// the default clock must survive a receiver check.
//
// The bug this pins: the default was `{setTimeout, clearTimeout}`, an object
// literal holding the globals as plain properties, so clock.setTimeout(...) ran
// with this === clock. A real browser rejects that - window.setTimeout has to be
// called on window - and threw "Illegal invocation" inside markLoading, which
// sidebar.js calls BEFORE chrome.send. Every live-row click aborted before
// sending anything.
//
// The shim cannot catch it: its setTimeout is a plain function that ignores its
// receiver, which is exactly why the suite stayed green. So this test installs a
// timer that DOES check, the way the platform does, and calls markLoading with
// no clock at all so the default is what gets exercised.
test('the default clock calls the platform timers with a receiver they accept', () => {
  const realSetTimeout = globalThis.setTimeout;
  const realClearTimeout = globalThis.clearTimeout;
  let calls = 0;
  // Mirrors what the platform actually rejects, which is narrower than "not
  // the global". A bare setTimeout(...) call from a module passes
  // this === undefined and browsers accept it - the WebIDL binding resolves an
  // absent receiver to the global. What they refuse is a DIFFERENT object,
  // which is precisely what `{setTimeout, clearTimeout}` handed them.
  const illegal = (self) => self !== undefined && self !== globalThis;
  globalThis.setTimeout = function(fn, ms) {
    if (illegal(this)) {
      throw new TypeError('Illegal invocation');
    }
    calls++;
    return realSetTimeout(fn, ms);
  };
  globalThis.clearTimeout = function(id) {
    if (illegal(this)) {
      throw new TypeError('Illegal invocation');
    }
    return realClearTimeout(id);
  };
  try {
    const el = document.createElement('div');
    const settle = markLoading(el);
    assert.equal(calls, 1, 'the default clock has to reach the real timer');
    assert.equal(el.classList.contains('loading'), true);
    settle();
    assert.equal(el.classList.contains('loading'), false);
  } finally {
    globalThis.setTimeout = realSetTimeout;
    globalThis.clearTimeout = realClearTimeout;
  }
});

// --- a mark keyed by tab survives the re-render ---------------------

const {markLoadingKey, reapplyLoadingMarks, setLoadingKey, setLoadingPhase,
       MIN_RING_MS} =
    await import(path.join(ATRIUM_DIR, 'loading-mark.js'));

function keyedRoot(tabId) {
  const root = h.document.createElement('div');
  const r = row();
  r.dataset.tabId = tabId;
  const fav = h.document.createElement('div');
  fav.className = 'fav';
  fav.dataset.favId = 'https://x.example/';
  fav.dataset.favTab = tabId;
  root.append(r, fav);
  return root;
}

test('a keyed mark lands on the row and the tile that share the tab', () => {
  const clock = fakeClock();
  const root = keyedRoot('t9');
  markLoadingKey('t9', root, {clock});
  assert.ok(root.querySelector('.row').classList.contains('loading'));
  assert.ok(root.querySelector('.fav').classList.contains('loading'));
  clock.advance(LOADING_TIMEOUT_MS);
  assert.equal(root.querySelectorAll('.loading').length, 0);
});

test('a re-render within the window gets the mark back, after it does not', () => {
  const clock = fakeClock();
  const first = keyedRoot('t9');
  markLoadingKey('t9', first, {clock});
  const rebuilt = keyedRoot('t9');
  reapplyLoadingMarks(rebuilt);
  assert.ok(rebuilt.querySelector('.row').classList.contains('loading'));
  clock.advance(LOADING_TIMEOUT_MS);
  assert.equal(rebuilt.querySelectorAll('.loading').length, 0);
  const late = keyedRoot('t9');
  reapplyLoadingMarks(late);
  assert.equal(late.querySelectorAll('.loading').length, 0);
});

// the browser's own loading state, from DidStartLoading to
// DidStopLoading, with no click and no timer between them.

test('a live mark lands on the row and the tile and holds without a timer', () => {
  const clock = fakeClock();
  const root = keyedRoot('t4');
  setLoadingKey('t4', root, true, {clock});
  assert.equal(root.querySelectorAll('.loading').length, 2);
  clock.advance(LOADING_TIMEOUT_MS * 4);
  assert.equal(root.querySelectorAll('.loading').length, 2,
               'a long load lost its ring');
  setLoadingKey('t4', root, false, {clock});
  assert.equal(root.querySelectorAll('.loading').length, 0);
});

test('the stop ends a click mark on the same key too', () => {
  const clock = fakeClock();
  const root = keyedRoot('t5');
  markLoadingKey('t5', root, {clock});
  clock.advance(MIN_RING_MS);
  setLoadingKey('t5', root, false, {clock});
  assert.equal(root.querySelectorAll('.loading').length, 0);
  assert.equal(clock.pending(), 0, 'the click timer outlived the stop');
});

test('a live mark survives a re-render until the stop', () => {
  const clock = fakeClock();
  const root = keyedRoot('t6');
  setLoadingKey('t6', root, true, {clock});
  const rebuilt = keyedRoot('t6');
  reapplyLoadingMarks(rebuilt);
  assert.equal(rebuilt.querySelectorAll('.loading').length, 2);
  clock.advance(MIN_RING_MS);
  setLoadingKey('t6', rebuilt, false, {clock});
  const late = keyedRoot('t6');
  reapplyLoadingMarks(late);
  assert.equal(late.querySelectorAll('.loading').length, 0);
});

test('a stop within the minimum holds the ring, then ends it', () => {
  // A local page loads in under 100ms. The ring still has to be seen.
  const clock = fakeClock();
  const root = keyedRoot('t7');
  setLoadingKey('t7', root, true, {clock});
  clock.advance(80);
  setLoadingKey('t7', root, false, {clock});
  assert.equal(root.querySelectorAll('.loading').length, 2, 'flickered');
  clock.advance(MIN_RING_MS - 80 - 1);
  assert.equal(root.querySelectorAll('.loading').length, 2, 'ended early');
  clock.advance(1);
  assert.equal(root.querySelectorAll('.loading').length, 0);
});

test('a held stop ends on the rows a re-render put in place', () => {
  const clock = fakeClock();
  const root = keyedRoot('t8');
  setLoadingKey('t8', root, true, {clock});
  setLoadingKey('t8', root, false, {clock});
  const rebuilt = keyedRoot('t8');
  reapplyLoadingMarks(rebuilt);
  assert.equal(rebuilt.querySelectorAll('.loading').length, 2);
  clock.advance(MIN_RING_MS);
  assert.equal(rebuilt.querySelectorAll('.loading').length, 0,
               'the hold ended on the old rows');
});

test('a new start during the hold keeps the ring running', () => {
  const clock = fakeClock();
  const root = keyedRoot('t9b');
  setLoadingKey('t9b', root, true, {clock});
  setLoadingKey('t9b', root, false, {clock});
  setLoadingKey('t9b', root, true, {clock});
  clock.advance(MIN_RING_MS * 3);
  assert.equal(root.querySelectorAll('.loading').length, 2,
               'the old hold ended the new load');
});

// --- the ring belongs to the focused tab alone ----------------------

const {setActiveKeys, FOCUS_CLAIM_MS} =
    await import(path.join(ATRIUM_DIR, 'loading-mark.js'));

// Two rows and two tiles, one pair per tab, which is what the report needs: a
// load running on one while the other takes focus.
function twoTabRoot(a, b) {
  const root = h.document.createElement('div');
  for (const id of [a, b]) {
    const r = row();
    r.dataset.tabId = id;
    const fav = h.document.createElement('div');
    fav.className = 'fav';
    fav.dataset.favId = `https://${id}.example/`;
    fav.dataset.favTab = id;
    root.append(r, fav);
  }
  return root;
}

const lit = (root, id) =>
    root.querySelectorAll('.loading').filter(
        (el) => el.dataset.tabId === id || el.dataset.favTab === id).length;

test('a load in a background tab draws no ring at all', () => {
  const clock = fakeClock();
  const root = twoTabRoot('f1', 'f2');
  setActiveKeys(['f1'], root);
  setLoadingKey('f2', root, true, {clock});
  assert.equal(root.querySelectorAll('.loading').length, 0);
  setActiveKeys([], root);
});

test('the ring runs on the tab that has focus', () => {
  const clock = fakeClock();
  const root = twoTabRoot('f3', 'f4');
  setActiveKeys(['f3'], root);
  setLoadingKey('f3', root, true, {clock});
  assert.equal(lit(root, 'f3'), 2, 'the focused row and its tile');
  assert.equal(lit(root, 'f4'), 0);
  setActiveKeys([], root);
});

test('a focus switch mid-load takes the ring off the row that lost it', () => {
  const clock = fakeClock();
  const root = twoTabRoot('f5', 'f6');
  setActiveKeys(['f5'], root);
  setLoadingKey('f5', root, true, {clock});
  assert.equal(lit(root, 'f5'), 2, 'the ring never started');

  setActiveKeys(['f6'], root);
  assert.equal(lit(root, 'f5'), 0, 'the old row kept its ring');
  setActiveKeys([], root);
});

test('a load-start after the switch draws the new active tab a ring', () => {
  const clock = fakeClock();
  const root = twoTabRoot('f7', 'f8');
  setActiveKeys(['f7'], root);
  setLoadingKey('f7', root, true, {clock});
  setActiveKeys(['f8'], root);
  setLoadingKey('f8', root, true, {clock});
  assert.equal(lit(root, 'f8'), 2);
  assert.equal(lit(root, 'f7'), 0);
  setActiveKeys([], root);
});

test('a re-render does not bring a background ring back', () => {
  // reapplyLoadingMarks re-adds every live key, and the browser goes on
  // reporting a background load - so the focus rule has to hold here too.
  const clock = fakeClock();
  const root = twoTabRoot('f9', 'f10');
  setActiveKeys(['f9'], root);
  setLoadingKey('f10', root, true, {clock});
  const rebuilt = twoTabRoot('f9', 'f10');
  reapplyLoadingMarks(rebuilt);
  assert.equal(rebuilt.querySelectorAll('.loading').length, 0);
  setActiveKeys([], root);
});

test('focus returning to a tab still loading brings its ring back', () => {
  const clock = fakeClock();
  const root = twoTabRoot('f11', 'f12');
  setActiveKeys(['f11'], root);
  setLoadingKey('f11', root, true, {clock});
  setActiveKeys(['f12'], root);
  setActiveKeys(['f11'], root);
  assert.equal(lit(root, 'f11'), 2, 'the load is still running');
  setLoadingKey('f11', root, false, {clock});
  clock.advance(MIN_RING_MS);
  setActiveKeys([], root);
});

test('a click on a row that is not the focused one marks nothing', () => {
  const clock = fakeClock();
  const root = twoTabRoot('f13', 'f14');
  setActiveKeys(['f13'], root);
  markLoadingKey('f14', root, {clock});
  assert.equal(root.querySelectorAll('.loading').length, 0);
  clock.advance(LOADING_TIMEOUT_MS);
  setActiveKeys([], root);
});

test('with no focused tab named yet, every ring still runs', () => {
  // The sidebar learns the active tab from a snapshot. Before the first one
  // lands there is nothing to compare against, and a ring is the lesser defect.
  const clock = fakeClock();
  const root = twoTabRoot('f15', 'f16');
  setActiveKeys([], root);
  setLoadingKey('f15', root, true, {clock});
  assert.equal(lit(root, 'f15'), 2);
  setLoadingKey('f15', root, false, {clock});
  clock.advance(MIN_RING_MS);
});

test('focus is read off the element, not off the key the load carried', () => {
  // A cold favourite click knows only the url, and the load that follows
  // reports a tab id. The tile answers to both, so its ring survives the gap
  // between the click and the snapshot that names the tab.
  const clock = fakeClock();
  const root = twoTabRoot('f17', 'f18');
  setActiveKeys(['https://f17.example/'], root);
  setLoadingKey('f17', root, true, {clock});
  assert.equal(root.querySelectorAll('.fav.loading').length, 1,
               'the tile shares the url that has focus');
  setActiveKeys(['f17', 'https://f17.example/'], root);
  assert.equal(lit(root, 'f17'), 2, 'the row joined once the snapshot landed');
  setLoadingKey('f17', root, false, {clock});
  clock.advance(MIN_RING_MS);
  setActiveKeys([], root);
});

// A click claims focus before any push agrees, and the push that lands next
// may still be carrying the selection the click just replaced.

test('a push that has not caught up cannot blank the ring the click drew', () => {
  const clock = fakeClock();
  const root = twoTabRoot('f19', 'f20');
  setActiveKeys(['f19'], root, {clock});
  setLoadingKey('f19', root, true, {clock});

  setActiveKeys(['f20'], root, {claimed: true, clock});
  setLoadingKey('f20', root, true, {clock});
  assert.equal(lit(root, 'f20'), 2, 'the click never drew a ring');

  setActiveKeys(['f19'], root, {clock});
  assert.equal(lit(root, 'f20'), 2, 'a stale push blanked the clicked row');
  assert.equal(lit(root, 'f19'), 0);
  setActiveKeys([], root, {clock});
});

test('the claim expires, so a push that still disagrees wins in the end', () => {
  const clock = fakeClock();
  const root = twoTabRoot('f21', 'f22');
  setActiveKeys(['f21'], root, {clock});
  setActiveKeys(['f22'], root, {claimed: true, clock});
  setLoadingKey('f22', root, true, {clock});
  setActiveKeys(['f21'], root, {clock});
  assert.equal(lit(root, 'f22'), 2, 'premise: the claim held it');

  clock.advance(FOCUS_CLAIM_MS);
  setActiveKeys(['f21'], root, {clock});
  assert.equal(lit(root, 'f22'), 0, 'the claim outlived its window');
  setActiveKeys([], root, {clock});
});

test('a focus move to some third tab lands even inside the claim', () => {
  // The claim only speaks for the tab the click LEFT. A chord or another
  // window moving focus somewhere else is news, not a push in flight.
  const clock = fakeClock();
  const root = twoTabRoot('f33', 'f34');
  setActiveKeys(['f33'], root, {clock});
  setActiveKeys(['f34'], root, {claimed: true, clock});
  setLoadingKey('f34', root, true, {clock});

  setActiveKeys(['f35'], root, {clock});
  assert.equal(lit(root, 'f34'), 0, 'a real focus move was swallowed');
  setLoadingKey('f34', root, false, {clock});
  clock.advance(MIN_RING_MS);
  setActiveKeys([], root, {clock});
});

test('a push that agrees with the click ends the claim there and then', () => {
  const clock = fakeClock();
  const root = twoTabRoot('f23', 'f24');
  setActiveKeys(['f24'], root, {claimed: true, clock});
  setLoadingKey('f24', root, true, {clock});
  // The snapshot catching up carries the tab id AND its url.
  setActiveKeys(['f24', 'https://f24.example/'], root, {clock});
  // A real focus move now, still inside the claim window, must take effect.
  setActiveKeys(['f23'], root, {clock});
  assert.equal(lit(root, 'f24'), 0, 'the spent claim held a second push off');
  setActiveKeys([], root, {clock});
});

// A tab closed mid-load never reports a stop, so nothing else drains its key.

test('a key no rendered row carries any more stops holding a ring', () => {
  const clock = fakeClock();
  const root = twoTabRoot('f25', 'f26');
  setActiveKeys(['f25'], root, {clock});
  setLoadingKey('f25', root, true, {clock});

  clock.advance(LOADING_TIMEOUT_MS);
  reapplyLoadingMarks(twoTabRoot('f26', 'f27'), {clock});

  const back = twoTabRoot('f25', 'f26');
  setActiveKeys(['f25'], back, {clock});
  assert.equal(lit(back, 'f25'), 0, 'the closed tab kept its key');
  setActiveKeys([], back, {clock});
});

test('a key whose row has not been rendered yet is kept', () => {
  // The load push can beat the snapshot that carries the new row, and dropping
  // the key there would lose the ring for the whole load.
  const clock = fakeClock();
  const root = twoTabRoot('f28', 'f29');
  setActiveKeys([], root, {clock});
  setLoadingKey('f30', root, true, {clock});
  reapplyLoadingMarks(root, {clock});

  const arrived = twoTabRoot('f30', 'f29');
  reapplyLoadingMarks(arrived, {clock});
  assert.equal(lit(arrived, 'f30'), 2, 'the key was dropped before its row');
  setLoadingKey('f30', arrived, false, {clock});
  clock.advance(MIN_RING_MS);
});

test('a re-render walks the tree once, not once per marked key', () => {
  const clock = fakeClock();
  const root = twoTabRoot('f31', 'f32');
  setActiveKeys([], root, {clock});
  setLoadingKey('f31', root, true, {clock});
  setLoadingKey('f32', root, true, {clock});
  markLoadingKey('f31', root, {clock});

  let queries = 0;
  const real = root.querySelectorAll.bind(root);
  root.querySelectorAll = (selector) => {
    queries++;
    return real(selector);
  };
  reapplyLoadingMarks(root, {clock});
  assert.equal(queries, 1, `${queries} passes over the tree for 3 marks`);

  root.querySelectorAll = real;
  setLoadingKey('f31', root, false, {clock});
  setLoadingKey('f32', root, false, {clock});
  clock.advance(Math.max(MIN_RING_MS, LOADING_TIMEOUT_MS));
});

// --- the sweep has to read at a glance ------------------------------

test('the sweep is at least 2px thick', () => {
  const css = fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar.css'), 'utf8');
  const sweep = /\.brdr::before\s*{([^}]*)}/.exec(css);
  assert.ok(sweep, 'the sweep rule is gone');
  // The sweep reads --selring so it cannot drift from the ring it draws on,
  // which is why the number has to be resolved rather than read off this rule.
  assert.match(sweep[1], /padding:\s*var\(--selring\)/,
               'the sweep no longer reads the ring token');
  const token = /--selring:\s*(\d+(?:\.\d+)?)px/.exec(css);
  assert.ok(token, '--selring is gone');
  assert.ok(Number(token[1]) >= 2, `the sweep is ${token[1]}px`);
});

test('the ring falls back to the contrast-corrected tint, not a raw white mix', () => {
  // The row and the tile each redeclare --ringb, so a value set on the sidebar
  // alone would lose to them. --ringfb is the hook that reaches through.
  const css = fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar.css'), 'utf8');
  const declarations = css.match(/--ringb:\s*var\(--ringfb,[^;]*;/g) || [];
  assert.equal(declarations.length, 3,
               'the row, the tile and the drag ghost row all need it');
});

// --- the ring's second phase --------------------------------------------------

test('receiving starts the comet, with no fill angle to track', () => {
  const clock = fakeClock();
  const root = twoTabRoot('f40', 'f41');
  setActiveKeys([], root, {clock});
  setLoadingKey('f40', root, true, {clock});
  setLoadingPhase('f40', root, 'receiving', 0.25, {clock});

  const el = root.querySelector('[data-tab-id="f40"]');
  assert.equal(el.classList.contains('receiving'), true);
  assert.equal(el.style.getPropertyValue('--fillang'), '',
               'phase 2 spins the ring - it never sets a fill angle');
  assert.equal(el.classList.contains('loading'), true,
               'the ring itself still belongs to atriumTabLoading');

  setLoadingKey('f40', root, false, {clock});
  clock.advance(MIN_RING_MS);
});

test('waiting keeps the comet, with no fill angle', () => {
  const clock = fakeClock();
  const root = twoTabRoot('f42', 'f43');
  setActiveKeys([], root, {clock});
  setLoadingKey('f42', root, true, {clock});
  setLoadingPhase('f42', root, 'waiting', 0, {clock});

  const el = root.querySelector('[data-tab-id="f42"]');
  assert.equal(el.classList.contains('receiving'), false);
  assert.equal(el.style.getPropertyValue('--fillang'), '');

  setLoadingKey('f42', root, false, {clock});
  clock.advance(MIN_RING_MS);
});

// The ring outlives 'done' by the rest of MIN_RING_MS. Dropping the sweep on
// that push spent the hold replaying phase 1, so a load ended on the pulse and
// the two phases read as swapped.
test('done leaves the sweep running, and the stop is what ends the ring', () => {
  const clock = fakeClock();
  const root = twoTabRoot('f44', 'f45');
  setActiveKeys([], root, {clock});
  setLoadingKey('f44', root, true, {clock});
  setLoadingPhase('f44', root, 'receiving', 0.5, {clock});
  setLoadingPhase('f44', root, 'done', 1, {clock});

  const el = root.querySelector('[data-tab-id="f44"]');
  assert.equal(el.classList.contains('receiving'), true,
               'done put the ring back on phase 1');
  assert.equal(el.style.getPropertyValue('--fillang'), '');

  setLoadingKey('f44', root, false, {clock});
  clock.advance(MIN_RING_MS);
  assert.equal(el.classList.contains('receiving'), false,
               'the ring never let go of the sweep');
  assert.equal(el.classList.contains('loading'), false);
});

test('a fresh load on the same row starts over from the pulse', () => {
  const clock = fakeClock();
  const root = twoTabRoot('f48', 'f49');
  setActiveKeys([], root, {clock});
  setLoadingKey('f48', root, true, {clock});
  setLoadingPhase('f48', root, 'receiving', 0.5, {clock});
  setLoadingPhase('f48', root, 'waiting', 0, {clock});

  const el = root.querySelector('[data-tab-id="f48"]');
  assert.equal(el.classList.contains('receiving'), false,
               'a new load has to begin on phase 1');
  assert.equal(el.classList.contains('loading'), true);

  setLoadingKey('f48', root, false, {clock});
  clock.advance(MIN_RING_MS);
});

test('a re-render keeps the phase the run had reached', () => {
  // Every snapshot push rebuilds the rows - a re-render is a new element
  // with no memory of its own, so the 'receiving' class has to come back
  // from phaseByKey, not from anything the fresh element carries in.
  const clock = fakeClock();
  const root = twoTabRoot('f46', 'f47');
  setActiveKeys([], root, {clock});
  setLoadingKey('f46', root, true, {clock});
  setLoadingPhase('f46', root, 'receiving', 0.75, {clock});

  const rebuilt = twoTabRoot('f46', 'f47');
  reapplyLoadingMarks(rebuilt, {clock});
  const el = rebuilt.querySelector('[data-tab-id="f46"]');
  assert.equal(el.classList.contains('receiving'), true);
  assert.equal(el.style.getPropertyValue('--fillang'), '');

  setLoadingKey('f46', rebuilt, false, {clock});
  clock.advance(MIN_RING_MS);
});

test('sidebar.css spins the ring for phase 2, never fills it', () => {
  const css = fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar.css'), 'utf8');
  const sweep = /\.receiving > \.brdr::before\s*{([^}]*)}/.exec(css);
  assert.ok(sweep, 'the receiving ring rule is gone');
  assert.match(sweep[1], /animation:\s*cometspin/);
  assert.doesNotMatch(sweep[1], /--fillang/);
  assert.doesNotMatch(sweep[1], /conic-gradient\(from -90deg/);
  assert.doesNotMatch(css, /@property --fillang/);
});
