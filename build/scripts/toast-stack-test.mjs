#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// showToast(), ported from swipe-lab.html's toast stack.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';
import {makeFakeClock} from './fake-clock.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const h = installDOM({pathname: '/sidebar'});
const {showToast, showConfirmToast, elideMiddle} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/toast-stack.js'));

// Each test gets a clean body - a stray .toasts box from a previous test
// would make querySelectorAll counts lie.
function makeHost() {
  h.reset();
  const host = document.createElement('div');
  host.setAttribute('id', 'atrium-root');
  document.body.append(host);
  return host;
}

// A dismiss control now only exists on an iconed (confirmation-style) card
//, so any test of the close button itself needs one.
const dummyIcon = () => document.createElement('span');

test('toasts stack top-right by default, newest on top', () => {
  const host = makeHost();
  showToast({color: '#ff0000', title: 'One', sub: 'sub one'}, {host});
  showToast({color: '#00ff00', title: 'Two', sub: 'sub two'}, {host});
  const box = host.querySelector('.toasts');
  assert.ok(box, 'a .toasts container must exist');
  assert.equal(box.classList.contains('left'), false,
               'default corner is top-right, so no .left modifier');
  const cards = box.querySelectorAll('.toast');
  assert.equal(cards.length, 2);
  assert.equal(cards[0].querySelector('.ctl').textContent, 'Two',
               'the most recent card must be first (prepend), not appended');
});

test('side:"right" (sidebar on the right) flips the stack to top-left', () => {
  const host = makeHost();
  showToast({color: '#ff0000', title: 'One'}, {host, side: 'right'});
  const box = host.querySelector('.toasts');
  assert.equal(box.classList.contains('left'), true,
               'the corner opposite a right-hand sidebar is top-left');
});

// The count is written out rather than read from STACK_CAP: importing the
// constant would make this test agree with any value the code happens to hold.
test('ten toasts all stay up, and the 11th drops the oldest', () => {
  const host = makeHost();
  for (let i = 1; i <= 10; i++) {
    showToast({color: '#ff0000', title: `T${i}`}, {host});
  }
  const box = host.querySelector('.toasts');
  assert.equal(box.querySelectorAll('.toast').length, 10,
               'a tenth card must not evict anything');

  showToast({color: '#ff0000', title: 'T11'}, {host});
  const titles = [...box.querySelectorAll('.toast .ctl')].map((n) => n.textContent);
  assert.deepEqual(
      titles, ['T11', 'T10', 'T9', 'T8', 'T7', 'T6', 'T5', 'T4', 'T3', 'T2'],
      'T1 (the oldest) must be the one dropped');
});

test('each card renders the themed color, icon, title, and subtitle', () => {
  const host = makeHost();
  const icon = document.createElement('span');
  icon.className = 'probe-icon';
  showToast({color: '#7FB3D5', icon, title: 'Rewound', sub: 'Space A'},
             {host});
  const card = host.querySelector('.toast');
  assert.equal(card.style['--ctc'], '#7FB3D5');
  assert.equal(card.querySelector('.cti .probe-icon'), icon);
  assert.equal(card.querySelector('.ctl').textContent, 'Rewound');
  assert.equal(card.querySelector('.ctu').textContent, 'Space A');
});

test('the close button dismisses only its own card', () => {
  const host = makeHost();
  const clock = makeFakeClock();
  showToast({color: '#ff0000', icon: dummyIcon(), title: 'Keep'}, {host, clock});
  showToast({color: '#00ff00', icon: dummyIcon(), title: 'Close me'}, {host, clock});
  const box = host.querySelector('.toasts');
  const closeMe = [...box.querySelectorAll('.toast')]
      .find((t) => t.querySelector('.ctl').textContent === 'Close me');
  closeMe.querySelector('.ctcl').click();
  clock.advance(260);
  const remaining = [...box.querySelectorAll('.toast')].map(
      (t) => t.querySelector('.ctl').textContent);
  assert.deepEqual(remaining, ['Keep']);
});

test('closing the last card removes the empty .toasts container too', () => {
  const host = makeHost();
  const clock = makeFakeClock();
  showToast({color: '#ff0000', icon: dummyIcon(), title: 'Only'}, {host, clock});
  host.querySelector('.ctcl').click();
  clock.advance(260);
  assert.equal(host.querySelector('.toasts'), null);
});

test('clicking the close button does not trigger the card click action', () => {
  const host = makeHost();
  const clock = makeFakeClock();
  let clicked = false;
  showToast({color: '#ff0000', icon: dummyIcon(), title: 'Actionable', onClick: () => clicked = true},
             {host, clock});
  host.querySelector('.ctcl').click();
  assert.equal(clicked, false,
               'the close button must stop propagation before the card sees the click');
});

test('a card with an onClick action dismisses itself and runs the action on click', () => {
  const host = makeHost();
  const clock = makeFakeClock();
  let clicked = false;
  const card = showToast(
      {color: '#ff0000', title: 'Actionable', onClick: () => clicked = true},
      {host, clock});
  card.click();
  assert.equal(clicked, true, 'the action must run');
  assert.equal(host.querySelector('.toasts'), null,
               'clicking an actionable card must dismiss it immediately, no fade wait');
});

test('a card with no onClick is not clickable and has no pointer cursor', () => {
  const host = makeHost();
  const card = showToast({color: '#ff0000', title: 'Plain'}, {host});
  assert.equal(card.style.cursor, undefined);
  card.click();
  assert.equal(host.querySelector('.toasts') === null, false,
               'a plain card must not be dismissed by a click');
});

test('dismissAfterMs schedules an automatic close', () => {
  const host = makeHost();
  const clock = makeFakeClock();
  showToast({color: '#ff0000', title: 'Timed'}, {host, clock, dismissAfterMs: 3000});
  clock.advance(2999);
  assert.equal(host.querySelectorAll('.toast').length, 1, 'not yet - one ms early');
  clock.advance(1);
  clock.advance(260);
  assert.equal(host.querySelector('.toasts'), null,
               'the timed toast must be gone once its own duration elapses');
});

test('a rapid double dismiss schedules only one removal timer', () => {
  const host = makeHost();
  const clock = makeFakeClock();
  showToast({color: '#ff0000', icon: dummyIcon(), title: 'Card'}, {host, clock});
  const closeBtn = host.querySelector('.ctcl');
  closeBtn.click();
  assert.equal(clock.pending, 1, 'the first click schedules the bye timer');
  closeBtn.click();
  assert.equal(clock.pending, 1,
               'a second click before BYE_MS elapses must not add a second timer');
  clock.advance(260);
  assert.equal(host.querySelector('.toasts'), null);
});

test('a close click racing the auto-dismiss timer schedules only one removal', () => {
  const host = makeHost();
  const clock = makeFakeClock();
  showToast({color: '#ff0000', icon: dummyIcon(), title: 'Timed'}, {host, clock, dismissAfterMs: 100});
  host.querySelector('.ctcl').click();
  assert.equal(clock.pending, 2,
               'the dismissAfterMs timer and the manual bye timer are both pending');
  clock.advance(100);
  assert.equal(clock.pending, 1,
               'the auto-dismiss timer firing bye() again must cancel the earlier ' +
               'bye timer, not add a second one alongside it');
  clock.advance(260);
  assert.equal(host.querySelector('.toasts'), null);
});

test('omitting dismissAfterMs never schedules an automatic close', () => {
  const host = makeHost();
  let scheduled = false;
  const clock = {
    set: (ms, fn) => {
      scheduled = true;
      return 1;
    },
    clear: () => {},
  };
  showToast({color: '#ff0000', title: 'Manual'}, {host, clock});
  assert.equal(scheduled, false,
               'without dismissAfterMs, only the close button or a click dismisses');
});

test('a toast card and its close button wear the shared classes', () => {
  const card = showToast({color: '#7FB3D5', icon: dummyIcon(), title: 'Shelved'},
                         {host: makeHost()});
  assert.equal(card.classList.contains('ov-card'), true);
  assert.equal(card.querySelector('.ctcl').classList.contains('ov-btn'), true);
});

test('the hover URL status strip uses the compact radius without changing notifications', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/toast-stack.css'), 'utf8');
  assert.match(css,
      /\.toastroot \.toast:not\(:has\(\.cti\)\)\s*\{[^}]*border-radius:\s*var\(--ov-radius-compact\)/);
  assert.match(css,
      /\.toast\s*\{[^}]*border-radius:\s*var\(--ov-radius-card\)/);
});

test('a toast with keys shows one cap per key after its text', () => {
  const card = showToast({color: '#7FB3D5', title: 'Back from snooze', keys: '⌘Z'},
                         {host: makeHost()});
  const caps = card.querySelectorAll('.ctk .ov-key');
  assert.equal(caps.length, 2);
  assert.equal(card.querySelector('.ctk').textContent, '⌘Z');
  assert.equal(card.querySelector('.ctk').previousElementSibling.querySelector('.ctl').textContent,
      'Back from snooze');
});

test('a toast without keys has no cap element at all', () => {
  const card = showToast({color: '#7FB3D5', title: 'Plain'}, {host: makeHost()});
  assert.equal(card.querySelector('.ctk'), null);
});

test('a hover card has no .cti, and opts out of its .ctcl', () => {
  const card = showToast({title: 'https://example.com/one', closable: false},
                         {host: makeHost()});
  assert.equal(card.querySelector('.cti'), null);
  assert.equal(card.querySelector('.ctcl'), null,
               'a hover card clears itself on unhover, so a dismiss control is dead weight');
});

// The 'keep' timeout passes no dismissAfterMs, so a card that also lost its
// button could only leave by eviction - and the stack holds ten now.
test('an icon-less card that does not opt out keeps a way to be dismissed', () => {
  const host = makeHost();
  const card = showToast({title: 'Rewound'}, {host});
  assert.equal(card.querySelector('.cti'), null, 'still no icon slot');
  assert.ok(card.querySelector('.ctcl'),
            'no icon and no auto-dismiss must still leave a close button');
});

test('a toast pushed with a key already in the stack replaces that card instead of stacking', () => {
  const host = makeHost();
  const clock = makeFakeClock();
  showToast({color: '#ff0000', title: 'Zoom', sub: '90%', key: 'zoom'}, {host, clock});
  showToast({color: '#ff0000', title: 'Zoom', sub: '110%', key: 'zoom'}, {host, clock});
  const box = host.querySelector('.toasts');
  assert.equal(box.querySelectorAll('.toast').length, 1,
               'a repeated key must update the existing card, not stack a second one');
  assert.equal(box.querySelector('.ctu').textContent, '110%');
});

test('a keyed toast restarts its dismiss timer instead of firing the first one', () => {
  const host = makeHost();
  const clock = makeFakeClock();
  showToast({color: '#ff0000', title: 'Zoom', sub: '90%', key: 'zoom'},
             {host, clock, dismissAfterMs: 1000});
  clock.advance(900);
  showToast({color: '#ff0000', title: 'Zoom', sub: '110%', key: 'zoom'},
             {host, clock, dismissAfterMs: 1000});
  clock.advance(900);
  assert.equal(host.querySelectorAll('.toast').length, 1,
               'the restart must have pushed the dismiss past the first deadline');
  clock.advance(100);
  clock.advance(260);
  assert.equal(host.querySelector('.toasts'), null,
               'the restarted timer must still fire eventually');
});

test('a keyed toast pushed during its fade-out stays instead of vanishing', () => {
  const host = makeHost();
  const clock = makeFakeClock();
  showToast({color: '#ff0000', title: 'Zoom', sub: '90%', key: 'zoom'},
             {host, clock, dismissAfterMs: 1000});
  clock.advance(1100);
  const card = showToast({color: '#ff0000', title: 'Zoom', sub: '110%', key: 'zoom'},
                         {host, clock, dismissAfterMs: 1000});
  clock.advance(500);
  assert.equal(host.querySelectorAll('.toast').length, 1,
               'the replace must cancel the pending removal');
  assert.ok(!card.classList.contains('bye'), 'the card must come back from its fade');
});

test('a toast with no key always stacks, even with identical content', () => {
  const host = makeHost();
  showToast({color: '#ff0000', title: 'Zoom', sub: '90%'}, {host});
  showToast({color: '#ff0000', title: 'Zoom', sub: '110%'}, {host});
  const box = host.querySelector('.toasts');
  assert.equal(box.querySelectorAll('.toast').length, 2,
               'no key means no dedup, matching the pre-existing behavior');
});

test('a confirmation card keeps both its .cti and its .ctcl', () => {
  const card = showConfirmToast({title: 'Link copied'}, {host: makeHost()});
  assert.ok(card.querySelector('.cti'), 'a confirmation must still show its icon slot');
  assert.ok(card.querySelector('.ctcl'),
            'a confirmation sits for CONFIRM_MS, so a user may want it gone sooner');
});

test('showConfirmToast honors an iconName override instead of the ok/close default', () => {
  const card = showConfirmToast({title: 'Zoomed', ok: true, iconName: 'zoom'},
                                {host: makeHost()});
  assert.equal(card.querySelector('.cti svg').dataset.icon, 'zoom');
});

test('showConfirmToast forwards its key to the stack\'s replace-in-place path', () => {
  const host = makeHost();
  showConfirmToast({title: '90%', iconName: 'zoom', key: 'zoom'}, {host});
  showConfirmToast({title: '110%', iconName: 'zoom', key: 'zoom'}, {host});
  assert.equal(host.querySelectorAll('.toast').length, 1);
});

// One unit per character, so a boundary can be built exactly rather than
// guessed against real font metrics.
const charWidth = (s) => s.length;

test('elideMiddle: a short URL that fits is returned whole, no ellipsis', () => {
  const url = 'https://a.example/x';
  assert.equal(elideMiddle(url, 40, charWidth), url);
});

test('elideMiddle: a URL at the width boundary keeps the exact widest fit', () => {
  // maxWidth 11 fits 5+1(ellipsis)+5=11 exactly. A < instead of <= search
  // would settle one character short on each side (4+1+4=9).
  const text = 'abcdefghijklmnopqrst';
  assert.equal(elideMiddle(text, 11, charWidth), 'abcde…pqrst');
});

test('elideMiddle: a long URL keeps its start and its end, ellipsis between', () => {
  const url = 'https://telex.hu/g7/vallalat/2026/09/01/nemet-autogyartok-slug';
  const elided = elideMiddle(url, 30, charWidth);
  assert.ok(elided.includes('…'), 'must carry an ellipsis');
  assert.ok(url.startsWith(elided.split('…')[0]), 'kept text is a real prefix');
  assert.ok(url.endsWith(elided.split('…')[1]), 'kept text is a real suffix');
  assert.ok(elided.length < url.length, 'must actually be shorter');
});

test('elideMiddle: a maxWidth too small even for the ellipsis returns just it', () => {
  assert.equal(elideMiddle('https://a.example/very/long/path', 0, charWidth), '…');
});

// The card was freed from the 220px cap but the LABEL inside it was not, so a
// fit wider than 220px was still clipped by .ctl's own box.
test('the hover label is uncapped, while a confirmation label keeps its cap', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/toast-stack.css'), 'utf8');
  assert.match(
      css, /\.toastroot \.toast:not\(:has\(\.cti\)\) \.ctl\s*{[^}]*max-width:\s*none/,
      'the hover label must not carry a width cap');
  assert.match(css, /\n\.toast \.ctl\s*{[^}]*max-width:\s*220px/,
               'a confirmation label keeps its cap, so the rule above means something');
});

// the hovered URL card is a status line, and it wore the accent
// ring every confirmation card wears - a hard light outline no other overlay
// surface has. --ct-ring is not only paint: toast-surface.js reads it off the
// box and pads the widget by it, so dropping one without the other opens a gap
// at the pane corner.
test('a status card drops the accent ring, and its box drops the outset with it', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/toast-stack.css'), 'utf8');
  assert.match(
      css,
      /\.toast:not\(:has\(\.cti\)\):not\(\.shadow-probe\)\s*{[^}]*box-shadow:\s*none/,
      'a status card must paint no ring');
  assert.match(css, /\.toasts:not\(:has\(\.cti\)\)\s*{[^}]*--ct-ring:\s*0/,
               'a box with no confirmation card must report no outset');
  assert.match(
      css,
      /\.toastroot\s+\.toast:not\(:has\(\.cti\)\)\s*{[^}]*border-color:\s*transparent/,
      'a status strip hides the .ov-card border so no rim is visible');
  assert.match(css, /\n\.toast\s*{[^}]*box-shadow: 0 0 0 var\(--ct-ring\)/,
               'a confirmation card keeps its ring, so the rule above means something');
  assert.match(css, /\.toast\.shadow-probe\s*{[^}]*box-shadow:\s*var\(--ov-shadow-tight\)/,
               'the off-screen probe still resolves the shared card shadow');
});
