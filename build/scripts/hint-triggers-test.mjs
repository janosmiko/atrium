#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// hint-triggers.js: hold, modifier, post-action and hover, on a fake clock.
//
//   node --test build/scripts/hint-triggers-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');
const h = installDOM({pathname: '/sidebar'});
const {currentHint, hideHint} = await import(path.join(ATRIUM_DIR, 'hint.js'));
const {HOLD_THRESHOLD_MS} = await import(path.join(ATRIUM_DIR, 'hold-gesture.js'));
const {installHintTriggers, setBindings, bindingFor, refreshHoverHint} =
    await import(path.join(ATRIUM_DIR, 'hint-triggers.js'));
const {makeFakeClock} = await import('./fake-clock.mjs');

function hintKeys() {
  const el = currentHint();
  if (!el) {
    return null;
  }
  return [...el.querySelectorAll('.hint-keys .ov-key')].map((c) => c.textContent);
}

function hintText() {
  return currentHint()?.querySelector('.hint-text')?.textContent ?? null;
}

// The shim's pathname is /sidebar, so every call below passes nativeHint:
// false to keep exercising the local bubble - the native routing path (no
// element to render, a chrome.send instead) gets its own tests further down.

// Two tab rows and the toggle button, the surfaces the triggers delegate on.
function fresh() {
  hideHint();
  h.reset();
  setBindings({});
  // h.reset() rebuilds the body tree but leaves documentElement alone, and
  // the shim's dataset proxy throws deleting a key that was never set.
  if (document.documentElement.dataset.gestureHints !== undefined) {
    delete document.documentElement.dataset.gestureHints;
  }
  const root = document.createElement('div');
  root.id = 'atrium-root';
  document.body.appendChild(root);
  const row1 = document.createElement('div');
  row1.className = 'row tabrow';
  const row2 = document.createElement('div');
  row2.className = 'row tabrow';
  document.body.append(row1, row2);
  const nav = document.createElement('div');
  nav.className = 'sbnav';
  const toggle = document.createElement('button');
  toggle.className = 'nb sbtoggle';
  toggle.title = 'Hide sidebar';
  toggle.setAttribute('aria-label', 'Hide sidebar');
  nav.appendChild(toggle);
  document.body.appendChild(nav);
  const sent = [];
  const send = (name, args) => sent.push([name, args]);
  const clock = makeFakeClock();
  return {row1, row2, toggle, sent, send, clock};
}

test('hold: showing the hint at the threshold, hiding on release', () => {
  const {row1, sent, send, clock} = fresh();
  const {dispose} =
      installHintTriggers(document.body, {send, clock, gestureHintsLive: true, nativeHint: false});
  row1.dispatchEvent(new Event(
      'pointerdown', {timeStamp: 0, clientX: 0, clientY: 0}));
  clock.advance(HOLD_THRESHOLD_MS);
  assert.equal(hintText(), 'Hold to open in a peek');
  assert.equal(currentHint().dataset.hintAnchor, 'element');
  row1.dispatchEvent(new Event('pointerup', {}));
  assert.equal(currentHint(), null);
  assert.deepEqual(sent, []);
  dispose();
});

test('hold: drift past the slop cancels it before the threshold fires', () => {
  const {row1, send, clock} = fresh();
  const {dispose} =
      installHintTriggers(document.body, {send, clock, gestureHintsLive: true, nativeHint: false});
  row1.dispatchEvent(new Event(
      'pointerdown', {timeStamp: 0, clientX: 0, clientY: 0}));
  row1.dispatchEvent(new Event('pointermove', {clientX: 10, clientY: 0}));
  clock.advance(HOLD_THRESHOLD_MS);
  assert.equal(currentHint(), null);
  dispose();
});

test('hold: a cancel after the hint shows hides it too', () => {
  const {row1, send, clock} = fresh();
  const {dispose} =
      installHintTriggers(document.body, {send, clock, gestureHintsLive: true, nativeHint: false});
  row1.dispatchEvent(new Event(
      'pointerdown', {timeStamp: 0, clientX: 0, clientY: 0}));
  clock.advance(HOLD_THRESHOLD_MS);
  assert.ok(currentHint());
  row1.dispatchEvent(new Event('pointermove', {clientX: 10, clientY: 0}));
  assert.equal(currentHint(), null);
  dispose();
});

test('gesture hints stay off by default, a hover still shows', () => {
  const {row1, toggle, send, clock} = fresh();
  const {dispose} = installHintTriggers(document.body, {send, clock, nativeHint: false});
  row1.dispatchEvent(new Event(
      'pointerdown', {timeStamp: 0, clientX: 0, clientY: 0}));
  clock.advance(HOLD_THRESHOLD_MS);
  assert.equal(currentHint(), null, 'the hold gesture does not exist yet');
  toggle.dispatchEvent(new Event('pointerover', {bubbles: true}));
  clock.advance(600);
  assert.equal(hintText(), 'Hide sidebar');
  dispose();
});

test('the gestureHints dataset flag turns the gate on with no install option', () => {
  const {row1, send, clock} = fresh();
  document.documentElement.dataset.gestureHints = 'live';
  const {dispose} = installHintTriggers(document.body, {send, clock, nativeHint: false});
  row1.dispatchEvent(new Event(
      'pointerdown', {timeStamp: 0, clientX: 0, clientY: 0}));
  clock.advance(HOLD_THRESHOLD_MS);
  assert.equal(hintText(), 'Hold to open in a peek');
  dispose();
  delete document.documentElement.dataset.gestureHints;
});

test('modifier: Shift over a row shows the split hint, its own keyup hides it', () => {
  const {row2, send, clock} = fresh();
  const {dispose} =
      installHintTriggers(document.body, {send, clock, gestureHintsLive: true, nativeHint: false});
  window.dispatchEvent(new Event('keydown', {key: 'Shift', shiftKey: true}));
  row2.dispatchEvent(new Event('pointermove', {}));
  assert.equal(hintText(), 'Open in a split');
  assert.deepEqual(hintKeys(), ['⇧', 'click']);
  window.dispatchEvent(new Event('keyup', {key: 'Shift'}));
  assert.equal(currentHint(), null);
  dispose();
});

test('modifier: Option over a row shows the peek hint', () => {
  const {row2, send, clock} = fresh();
  const {dispose} =
      installHintTriggers(document.body, {send, clock, gestureHintsLive: true, nativeHint: false});
  window.dispatchEvent(new Event('keydown', {key: 'Alt', altKey: true}));
  row2.dispatchEvent(new Event('pointermove', {}));
  assert.equal(hintText(), 'Open in a peek');
  assert.deepEqual(hintKeys(), ['⌥', 'click']);
  window.dispatchEvent(new Event('keyup', {key: 'Alt'}));
  assert.equal(currentHint(), null);
  dispose();
});

test('modifier: the pointer leaving every row hides it too', () => {
  const {row2, send, clock} = fresh();
  const {dispose} =
      installHintTriggers(document.body, {send, clock, gestureHintsLive: true, nativeHint: false});
  window.dispatchEvent(new Event('keydown', {key: 'Shift', shiftKey: true}));
  row2.dispatchEvent(new Event('pointermove', {}));
  assert.ok(currentHint());
  document.body.dispatchEvent(new Event('pointermove', {}));
  assert.equal(currentHint(), null);
  window.dispatchEvent(new Event('keyup', {key: 'Shift'}));
  dispose();
});

test('modifier: a hidden surface clears the state too', () => {
  const {row2, send, clock} = fresh();
  const {dispose} =
      installHintTriggers(document.body, {send, clock, gestureHintsLive: true, nativeHint: false});
  window.dispatchEvent(new Event('keydown', {key: 'Shift', shiftKey: true}));
  document.dispatchEvent(new Event('visibilitychange'));
  row2.dispatchEvent(new Event('pointermove', {}));
  assert.equal(currentHint(), null);
  dispose();
});

test('modifier: extra pointermoves over the same row keep the same bubble', () => {
  const {row2, send, clock} = fresh();
  const {dispose} =
      installHintTriggers(document.body, {send, clock, gestureHintsLive: true, nativeHint: false});
  window.dispatchEvent(new Event('keydown', {key: 'Shift', shiftKey: true}));
  row2.dispatchEvent(new Event('pointermove', {}));
  const shown = currentHint();
  row2.dispatchEvent(new Event('pointermove', {}));
  row2.dispatchEvent(new Event('pointermove', {}));
  assert.equal(currentHint(), shown);
  window.dispatchEvent(new Event('keyup', {key: 'Shift'}));
  dispose();
});

test('post-action: a bound action shows once, the underlying send always runs', () => {
  const {sent, send, clock} = fresh();
  setBindings({toggleSidebar: '⌘S'});
  const {send: hintSend, dispose} = installHintTriggers(
      document.body, {send, clock, gestureHintsLive: true, nativeHint: false});
  hintSend('sidebarToggleRequested', [], {fromClick: true});
  assert.equal(hintText(), 'Next time: Toggle sidebar');
  assert.deepEqual(hintKeys(), ['⌘', 'S']);
  assert.deepEqual(sent, [['sidebarToggleRequested', []]]);
  const shown = currentHint();
  hintSend('sidebarToggleRequested', [], {fromClick: true});
  assert.equal(currentHint(), shown, 'a second call must not show a new hint');
  assert.deepEqual(sent, [
    ['sidebarToggleRequested', []], ['sidebarToggleRequested', []],
  ]);
  dispose();
});

test('post-action: with no binding, or not from a click, nothing shows', () => {
  const {sent, send, clock} = fresh();
  const {send: hintSend, dispose} = installHintTriggers(
      document.body, {send, clock, gestureHintsLive: true, nativeHint: false});
  hintSend('sidebarNewTab', []);
  assert.equal(currentHint(), null, 'sidebarNewTab has no bound action');
  setBindings({toggleSidebar: '⌘S'});
  hintSend('sidebarToggleRequested', []);
  assert.equal(currentHint(), null, 'a call with no fromClick must stay quiet');
  assert.deepEqual(sent, [['sidebarNewTab', []], ['sidebarToggleRequested', []]]);
  dispose();
});

test('hover: the button label and binding show after the delay, not before', () => {
  const {toggle, send, clock} = fresh();
  setBindings({toggleSidebar: '⌘S'});
  const {dispose} = installHintTriggers(document.body, {send, clock, nativeHint: false});
  toggle.dispatchEvent(new Event('pointerover', {bubbles: true}));
  clock.advance(599);
  assert.equal(currentHint(), null);
  clock.advance(1);
  assert.equal(hintText(), 'Hide sidebar');
  assert.deepEqual(hintKeys(), ['⌘', 'S']);
  toggle.dispatchEvent(new Event(
      'pointerout', {bubbles: true, relatedTarget: document.body}));
  assert.equal(currentHint(), null);
  dispose();
});

// Back, forward and reload have no id in the hotkey registry, so the hint had
// no key to name and showed the label alone.
test('hover: a nav button names the key Chromium owns for it', () => {
  const {clock, send} = fresh();
  const nav = document.querySelector('.sbnav');
  const cases = [['sbback', 'Back', ['⌘', '[']],
                 ['sbfwd', 'Forward', ['⌘', ']']],
                 ['sbreload', 'Reload', ['⌘', 'R']]];
  const {dispose} = installHintTriggers(document.body, {send, clock, nativeHint: false});
  for (const [cls, label, keys] of cases) {
    const button = document.createElement('button');
    button.className = 'nb ' + cls;
    button.title = label;
    button.setAttribute('aria-label', label);
    nav.appendChild(button);
    button.dispatchEvent(new Event('pointerover', {bubbles: true}));
    clock.advance(600);
    assert.equal(hintText(), label);
    assert.deepEqual(hintKeys(), keys, `${cls} named the wrong key`);
    button.dispatchEvent(new Event(
        'pointerout', {bubbles: true, relatedTarget: document.body}));
  }
  dispose();
});

// sidebar-nav-row.js's paint() repaints aria-label/title live and calls
// refreshHoverHint() - simulated here directly. sidebar-nav-row-test.mjs
// exercises paint() itself end to end.
test('hover: a modifier held over an open nav hint updates its text and key, releasing restores both', () => {
  const {clock, send} = fresh();
  const nav = document.querySelector('.sbnav');
  const button = document.createElement('button');
  button.className = 'nb sbreload';
  button.title = 'Reload';
  button.setAttribute('aria-label', 'Reload');
  nav.appendChild(button);
  const {dispose} = installHintTriggers(document.body, {send, clock, nativeHint: false});
  button.dispatchEvent(new Event('pointerover', {bubbles: true}));
  clock.advance(600);
  assert.equal(hintText(), 'Reload');
  assert.deepEqual(hintKeys(), ['⌘', 'R']);
  button.setAttribute('aria-label', 'Hard reload, ignoring the cache');
  button.title = 'Hard reload, ignoring the cache';
  nav.dataset.mod = 'option';
  refreshHoverHint();
  assert.equal(hintText(), 'Hard reload, ignoring the cache',
      'the pointer never moved, but the label must follow the variant');
  assert.deepEqual(hintKeys(), [],
      'the option variant has no key of its own - never the plain reload key');
  button.setAttribute('aria-label', 'Reload');
  button.title = 'Reload';
  delete nav.dataset.mod;
  refreshHoverHint();
  assert.equal(hintText(), 'Reload');
  assert.deepEqual(hintKeys(), ['⌘', 'R'],
      'releasing the modifier must restore the plain key, pointer still still');
  dispose();
});

// The floating bar's copy of the same group, built through buildNavGroup
// onto .dvnav rather than .sbnav.
test('hover: the devbar nav row drops the key for a modifier variant too', () => {
  const {clock, send} = fresh();
  const dvnav = document.createElement('span');
  dvnav.className = 'dvnav';
  document.body.appendChild(dvnav);
  const button = document.createElement('button');
  button.className = 'nb sbback';
  button.title = 'Back';
  button.setAttribute('aria-label', 'Back');
  dvnav.appendChild(button);
  const {dispose} = installHintTriggers(document.body, {send, clock, nativeHint: false});
  button.dispatchEvent(new Event('pointerover', {bubbles: true}));
  clock.advance(600);
  assert.deepEqual(hintKeys(), ['⌘', '[']);
  button.setAttribute('aria-label', 'Back in a new tab');
  dvnav.dataset.mod = 'cmdshift';
  refreshHoverHint();
  assert.equal(hintText(), 'Back in a new tab');
  assert.deepEqual(hintKeys(), []);
  dispose();
});

// A refresh with no open hint (still armed, timer pending, or nothing
// hovered) must not conjure one - the timer's own fire is what shows it.
test('hover: refreshHoverHint before a hint has shown does nothing', () => {
  const {clock, send} = fresh();
  const nav = document.querySelector('.sbnav');
  const button = document.createElement('button');
  button.className = 'nb sbreload';
  button.title = 'Reload';
  button.setAttribute('aria-label', 'Reload');
  nav.appendChild(button);
  const {dispose} = installHintTriggers(document.body, {send, clock, nativeHint: false});
  button.dispatchEvent(new Event('pointerover', {bubbles: true}));
  clock.advance(300);
  refreshHoverHint();
  assert.equal(currentHint(), null, 'the 600ms delay has not elapsed yet');
  dispose();
  refreshHoverHint();
  assert.equal(currentHint(), null, 'a disposed trigger must not still answer');
});

// Copy link is a hardcoded Cmd+Shift+C match in
// atrium_command_dispatcher_delegate.mm's IsCopyLinkKeyEquivalent, never
// registered with NativeHotkeyMap, so it has no action id to bind through.
test('hover: the copy link button names its native hotkey', () => {
  const {clock, send} = fresh();
  const button = document.createElement('button');
  button.className = 'dvcopy ov-btn';
  button.title = 'Copy URL';
  document.body.appendChild(button);
  const {dispose} = installHintTriggers(document.body, {send, clock, nativeHint: false});
  button.dispatchEvent(new Event('pointerover', {bubbles: true}));
  clock.advance(600);
  assert.equal(hintText(), 'Copy URL');
  assert.deepEqual(hintKeys(), ['⌘', '⇧', 'C'], 'dvcopy named the wrong key');
  dispose();
});

// Real ones: the pinned chip and the quick menu button carry neither a
// registry id nor a Chromium accelerator, and an empty keycap would read as a
// broken glyph.
for (const cls of ['exs-chip', 'qm-open']) {
  test(`hover: a .${cls} with no key of its own shows the label and no keycap`,
       () => {
    const {clock, send} = fresh();
    const nav = document.querySelector('.sbnav');
    const button = document.createElement('button');
    button.className = cls;
    button.title = 'uBlock Origin';
    button.setAttribute('aria-label', 'uBlock Origin');
    nav.appendChild(button);
    const {dispose} = installHintTriggers(document.body, {send, clock, nativeHint: false});
    button.dispatchEvent(new Event('pointerover', {bubbles: true}));
    clock.advance(600);
    assert.equal(hintText(), 'uBlock Origin');
    assert.deepEqual(hintKeys(), []);
    dispose();
  });
}

// The Clear icon in the tab divider carries no visible text, so the hover hint
// is the only place its label and chord are spelled out.
test('hover: the Clear icon names its label and the clearTabs chord', () => {
  const {clock, send} = fresh();
  const header = document.createElement('div');
  header.className = 'sechdr';
  const button = document.createElement('button');
  button.className = 'sechdrclear';
  button.setAttribute('aria-label', 'Clear tabs');
  header.appendChild(button);
  document.body.appendChild(header);
  setBindings({clearTabs: '⇧⌘K'});
  const {dispose} = installHintTriggers(document.body, {send, clock, nativeHint: false});
  button.dispatchEvent(new Event('pointerover', {bubbles: true}));
  clock.advance(600);
  assert.equal(hintText(), 'Clear tabs');
  assert.deepEqual(hintKeys(), ['⇧', '⌘', 'K']);
  dispose();
});

// A Clear takes its button away with the rows, so no pointerout ever fires.
// Left armed, the hint re-anchored at the document's origin on the next
// refresh and sat there for the rest of its time.
test('hover: a click on the armed Clear icon drops the hint for good', () => {
  const {clock, send} = fresh();
  const header = document.createElement('div');
  header.className = 'sechdr';
  const button = document.createElement('button');
  button.className = 'sechdrclear';
  button.setAttribute('aria-label', 'Clear tabs');
  header.appendChild(button);
  document.body.appendChild(header);
  setBindings({clearTabs: '⇧⌘K'});
  const {dispose} = installHintTriggers(document.body, {send, clock, nativeHint: false});
  button.dispatchEvent(new Event('pointerover', {bubbles: true}));
  clock.advance(600);
  assert.equal(hintText(), 'Clear tabs');
  button.dispatchEvent(new Event('click', {bubbles: true}));
  assert.equal(currentHint(), null, 'the click left the hint up');
  header.remove();
  refreshHoverHint();
  clock.advance(1000);
  assert.equal(currentHint(), null, 'a refresh re-showed the hint for a removed button');
  dispose();
});

// A rebindable key has to beat a literal, or the hint teaches a key the user
// has moved. No shipped button carries both today, so this one is synthetic -
// it exists to hold the precedence for whenever a nav action gains an id.
test('hover: a registry binding beats the fallback key', () => {
  const {clock, send} = fresh();
  const nav = document.querySelector('.sbnav');
  const button = document.createElement('button');
  button.className = 'nb sbtoggle sbback';
  button.title = 'Back';
  button.setAttribute('aria-label', 'Back');
  nav.appendChild(button);
  setBindings({toggleSidebar: '⌥⌘B'});
  const {dispose} = installHintTriggers(document.body, {send, clock, nativeHint: false});
  button.dispatchEvent(new Event('pointerover', {bubbles: true}));
  clock.advance(600);
  assert.deepEqual(hintKeys(), ['⌥', '⌘', 'B'],
                   'the literal won over a binding the user can move');
  dispose();
});

// Reload gained a rebindable action id, so ⌘R is now only the answer until
// the map arrives. A hint naming a key the user has moved teaches the wrong one.
test('hover: a rebound reload names the new chord, not the fallback', () => {
  const {send, clock} = fresh();
  const nav = document.querySelector('.sbnav');
  const button = document.createElement('button');
  button.className = 'nb sbreload';
  button.title = 'Reload';
  button.setAttribute('aria-label', 'Reload');
  nav.appendChild(button);
  setBindings({reload: '⌃⌘P'});
  const {dispose} = installHintTriggers(document.body, {send, clock, nativeHint: false});
  button.dispatchEvent(new Event('pointerover', {bubbles: true}));
  clock.advance(600);
  assert.deepEqual(hintKeys(), ['⌃', '⌘', 'P'],
                   'the hint did not follow reload to its rebound chord');
  dispose();
});

test('hover: leaving before the delay shows nothing', () => {
  const {toggle, send, clock} = fresh();
  const {dispose} = installHintTriggers(document.body, {send, clock, nativeHint: false});
  toggle.dispatchEvent(new Event('pointerover', {bubbles: true}));
  clock.advance(300);
  toggle.dispatchEvent(new Event(
      'pointerout', {bubbles: true, relatedTarget: document.body}));
  clock.advance(600);
  assert.equal(currentHint(), null);
  dispose();
});

test('hover: pointerout to a child of the same button does not cancel', () => {
  const {toggle, send, clock} = fresh();
  setBindings({toggleSidebar: '⌘S'});
  const glyph = document.createElement('span');
  toggle.appendChild(glyph);
  const {dispose} = installHintTriggers(document.body, {send, clock, nativeHint: false});
  toggle.dispatchEvent(new Event('pointerover', {bubbles: true}));
  clock.advance(300);
  toggle.dispatchEvent(
      new Event('pointerout', {bubbles: true, relatedTarget: glyph}));
  clock.advance(300);
  assert.equal(hintText(), 'Hide sidebar',
              'a move within the button must not cancel the pending hint');
  dispose();
});

// the tooltip used to come back on leave, so the next hover raced
// the bubble against it. A surface that draws its own hints carries no title.
test('hover: the native title moves aside and stays there', () => {
  const {toggle, send, clock} = fresh();
  const {dispose} = installHintTriggers(document.body, {send, clock, nativeHint: false});
  assert.equal(toggle.title, '', 'the install sweep left a title behind');
  assert.equal(toggle.dataset.hintTitle, 'Hide sidebar');
  toggle.dispatchEvent(new Event('pointerover', {bubbles: true}));
  assert.equal(toggle.title, '');
  toggle.dispatchEvent(new Event(
      'pointerout', {bubbles: true, relatedTarget: document.body}));
  assert.equal(toggle.title, '', 'the button took its tooltip back on leave');
  assert.equal(toggle.dataset.hintTitle, 'Hide sidebar');
  dispose();
});

// Blink kills a WebUI renderer that puts a name on a role-less div, which is
// what the floating sidebar's resize handle did on every relaunch.
test('hover: a parked title names a button but never a plain div', () => {
  const {toggle, send, clock} = fresh();
  const handle = document.createElement('div');
  handle.title = 'Drag to resize';
  document.body.appendChild(handle);
  const {dispose} = installHintTriggers(document.body, {send, clock, nativeHint: false});
  assert.equal(handle.dataset.hintTitle, 'Drag to resize');
  assert.equal(handle.getAttribute('aria-label'), null);
  assert.equal(toggle.getAttribute('aria-label'), 'Hide sidebar');
  dispose();
});

// The reported pair: the bubble for the button, and the address row's own
// tooltip beside it. sidebar.js sets that title long after the install.
test('hover: a title set on an ancestor after the install is parked too', () => {
  const {toggle, send, clock} = fresh();
  const nav = toggle.parentNode;
  const {dispose} = installHintTriggers(document.body, {send, clock, nativeHint: false});
  nav.title = 'https://example.com';
  toggle.dispatchEvent(new Event('pointerover', {bubbles: true}));
  clock.advance(600);
  assert.equal(nav.title, '', 'an ancestor still shows a tooltip beside the bubble');
  assert.equal(nav.dataset.hintTitle, 'https://example.com');
  assert.equal(hintText(), 'Hide sidebar', 'the designed bubble is the one hint');
  dispose();
});

test('dispose releases every listener: a later pointerdown shows nothing', () => {
  const {row1, send, clock} = fresh();
  const {dispose} = installHintTriggers(
      document.body, {send, clock, gestureHintsLive: true, nativeHint: false});
  dispose();
  row1.dispatchEvent(new Event(
      'pointerdown', {timeStamp: 0, clientX: 0, clientY: 0}));
  clock.advance(HOLD_THRESHOLD_MS);
  assert.equal(currentHint(), null);
});

test('bindingFor answers undefined for an id with no binding', () => {
  setBindings({toggleSidebar: '⌘S'});
  assert.equal(bindingFor('toggleSidebar'), '⌘S');
  assert.equal(bindingFor('back'), undefined);
  assert.equal(bindingFor(undefined), undefined);
});

test('native: a hold hint on /sidebar sends sidebarHintShow instead of a local bubble', () => {
  const {row1, send, clock} = fresh();
  const {dispose} =
      installHintTriggers(document.body, {send, clock, gestureHintsLive: true});
  row1.setRect({left: 10, top: 20, width: 100, height: 30});
  row1.dispatchEvent(new Event(
      'pointerdown', {timeStamp: 0, clientX: 0, clientY: 0}));
  clock.advance(HOLD_THRESHOLD_MS);
  assert.equal(document.querySelectorAll('.hint').length, 0);
  assert.deepEqual(h.sent.at(-1),
      ['sidebarHintShow', ['Hold to open in a peek', 'hold', 10, 20, 100, 30, 2500]]);
  row1.dispatchEvent(new Event('pointerup', {}));
  assert.deepEqual(h.sent.at(-1), ['sidebarHintHide', []]);
  dispose();
});

test('native: a modifier hint sends its own row rect', () => {
  const {row2, send, clock} = fresh();
  const {dispose} =
      installHintTriggers(document.body, {send, clock, gestureHintsLive: true});
  row2.setRect({left: 5, top: 15, width: 80, height: 24});
  window.dispatchEvent(new Event('keydown', {key: 'Shift', shiftKey: true}));
  row2.dispatchEvent(new Event('pointermove', {}));
  assert.deepEqual(h.sent.at(-1),
      ['sidebarHintShow', ['Open in a split', '⇧click', 5, 15, 80, 24, 2500]]);
  window.dispatchEvent(new Event('keyup', {key: 'Shift'}));
  assert.deepEqual(h.sent.at(-1), ['sidebarHintHide', []]);
  dispose();
});

test('native: a corner-anchored post-action hint still renders locally', () => {
  const {send, clock} = fresh();
  setBindings({toggleSidebar: '⌘S'});
  const {send: hintSend, dispose} = installHintTriggers(
      document.body, {send, clock, gestureHintsLive: true});
  const before = h.sent.length;
  hintSend('sidebarToggleRequested', [], {fromClick: true});
  assert.equal(hintText(), 'Next time: Toggle sidebar');
  assert.equal(h.sent.length, before, 'a corner anchor must not go native');
  dispose();
});

test('native: hover sends sidebarHintShow for the toggle button too', () => {
  const {toggle, send, clock} = fresh();
  toggle.setRect({left: 1, top: 2, width: 22, height: 22});
  const {dispose} = installHintTriggers(document.body, {send, clock});
  toggle.dispatchEvent(new Event('pointerover', {bubbles: true}));
  clock.advance(600);
  const [name, args] = h.sent.at(-1);
  assert.equal(name, 'sidebarHintShow');
  assert.equal(args[0], 'Hide sidebar');
  dispose();
});

test('native: the /devbar route goes to the overlay, not the local bubble',
     () => {
       // the bar drew its hints in its own document, where its
       // capsule bounded them. The route decides the host, so a hint here must
       // leave as devBarHintShow rather than becoming an element.
       const {toggle, send, clock} = fresh();
       toggle.setRect({left: 1, top: 2, width: 22, height: 22});
       const was = location.pathname;
       location.pathname = '/devbar';
       try {
         const {dispose} = installHintTriggers(document.body, {send, clock});
         toggle.dispatchEvent(new Event('pointerover', {bubbles: true}));
         clock.advance(600);
         assert.equal(h.sent.at(-1)[0], 'devBarHintShow');
         // A native hint keeps a marker instead of a node, so the absence of
         // an element is what says it never rendered here.
         assert.equal(currentHint().native, 'devBar');
         assert.equal(document.querySelector('.hint'), null);
         dispose();
       } finally {
         location.pathname = was;
       }
     });

// measured geometry, so "below" fits the viewport and only the
// asked-for side lifts the bubble off the prompt field beneath the row.
test('the AI panel action row asks for a bubble above it, the sidebar does not',
     () => {
       const {toggle, send, clock} = fresh();
       window.innerHeight = 680;
       const quick = document.createElement('button');
       quick.className = 'aiquickbtn ov-btn';
       quick.setAttribute('aria-label', 'Bullet points');
       quick.setRect({left: 40, top: 600, width: 26, height: 26});
       document.body.appendChild(quick);
       const {dispose} = installHintTriggers(document.body, {send, clock, nativeHint: false});

       quick.dispatchEvent(new Event('pointerover', {bubbles: true}));
       clock.advance(600);
       // The shim measures the bubble it did not lay out at 0 tall, so this
       // pins the side only - hint-test.mjs holds hintTop's arithmetic.
       assert.equal(currentHint().style['--hint-top'], '592px',
                    'the glyph hint must sit above its own button');

       toggle.setRect({left: 1, top: 2, width: 22, height: 22});
       toggle.dispatchEvent(new Event('pointerover', {bubbles: true}));
       clock.advance(600);
       assert.equal(currentHint().style['--hint-top'], '32px',
                    'a sidebar button keeps the default side');
       dispose();
     });

// the panel's own installHover parks every title under it, so a
// button the allowlist misses shows neither a hint nor the tooltip it lost.
test('every glyph button in the AI panel raises a hint, not only the action row',
     () => {
       const {send, clock} = fresh();
       const buttons = [
         ['aippin', 'Pin - keep open across tabs'],
         ['aipclear', 'Clear conversation'],
         ['aipclose', 'Close'],
         ['aichatsend', 'Send'],
       ].map(([cls, label]) => {
         const button = document.createElement('button');
         button.className = `${cls} ov-btn`;
         button.title = label;
         button.setRect({left: 40, top: 60, width: 26, height: 26});
         document.body.appendChild(button);
         return [button, label];
       });
       const {dispose} =
           installHintTriggers(document.body, {send, clock, nativeHint: false});

       for (const [button, label] of buttons) {
         button.dispatchEvent(new Event('pointerover', {bubbles: true}));
         clock.advance(600);
         assert.equal(hintText(), label,
                      `.${button.className.split(' ')[0]} must raise its hint`);
         button.dispatchEvent(new Event('pointerout', {bubbles: true}));
       }
       dispose();
     });
