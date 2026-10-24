#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Structural coverage only - the dom-shim has no layout engine.
//
//   node --test build/scripts/toast-surface-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';
import {makeFakeClock} from './fake-clock.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const h = installDOM({pathname: '/toast'});
const {installToastSurface} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/toast-surface.js'));

const STACK_CSS = fs.readFileSync(
    path.join(SRC, 'chrome/browser/resources/atrium/toast-stack.css'), 'utf8');

function makeRoot() {
  h.reset();
  const root = document.createElement('div');
  root.setAttribute('id', 'atrium-root');
  document.body.append(root);
  return root;
}

test('installToastSurface marks the root so toastroot-scoped CSS applies', () => {
  const root = makeRoot();
  installToastSurface(root);
  assert.equal(root.classList.contains('toastroot'), true);
});

test('a confirmation card carries .cti and .ctcl; a hover card carries neither', () => {
  const root = makeRoot();
  installToastSurface(root, {fitHoverLabel: (label, text) => label.textContent = text});
  window.atriumConfirmToast('Link copied', '', true);
  window.atriumHoverUrl('https://example.com/one');
  const cards = root.querySelectorAll('.toast');
  assert.equal(cards.length, 2);
  const confirm = [...cards].find((c) => c.querySelector('.ctl').textContent === 'Link copied');
  const hover = [...cards].find((c) => c.querySelector('.ctl').textContent === 'https://example.com/one');
  assert.ok(confirm.querySelector('.cti'), 'a confirmation must keep its icon slot');
  assert.ok(confirm.querySelector('.ctcl'), 'a confirmation must keep its dismiss control');
  assert.equal(hover.querySelector('.cti'), null, 'a hover card must have no icon slot');
  assert.equal(hover.querySelector('.ctcl'), null, 'a hover card must have no dismiss control');
});

test('atriumHoverUrl runs the label through fitHoverLabel, not the raw text', () => {
  const root = makeRoot();
  const calls = [];
  installToastSurface(root, {
    fitHoverLabel: (label, text) => {
      calls.push(text);
      label.textContent = 'FITTED:' + text;
    },
  });
  window.atriumHoverUrl('https://example.com/long/path');
  assert.deepEqual(calls, ['https://example.com/long/path']);
  assert.equal(root.querySelector('.ctl').textContent,
               'FITTED:https://example.com/long/path');
});

test('hovering a second link retargets the same card in place', () => {
  const root = makeRoot();
  installToastSurface(root, {fitHoverLabel: (label, text) => label.textContent = text});
  window.atriumHoverUrl('https://first.example/one');
  const firstCard = root.querySelector('.toast');
  window.atriumHoverUrl('https://second.example/two');
  assert.equal(root.querySelectorAll('.toast').length, 1, 'must not stack a second card');
  assert.equal(root.querySelector('.toast'), firstCard, 'must reuse the same element');
  assert.equal(root.querySelector('.ctl').textContent, 'https://second.example/two');
});

test('unhovering (empty text) clears the hover card', () => {
  const root = makeRoot();
  installToastSurface(root, {fitHoverLabel: (label, text) => label.textContent = text});
  window.atriumHoverUrl('https://example.com/one');
  window.atriumHoverUrl('');
  assert.equal(root.querySelector('.toast'), null);
});

test('atriumConfirmToast passes an iconName through to the card\'s icon', () => {
  const root = makeRoot();
  installToastSurface(root);
  window.atriumConfirmToast('90%', '', true, 'zoom');
  assert.equal(root.querySelector('.toast .cti svg').dataset.icon, 'zoom');
});

test('atriumConfirmToast with a repeated key updates the one card instead of stacking', () => {
  const root = makeRoot();
  installToastSurface(root);
  window.atriumConfirmToast('90%', '', true, 'zoom', 'zoom');
  window.atriumConfirmToast('110%', '', true, 'zoom', 'zoom');
  const cards = root.querySelectorAll('.toast');
  assert.equal(cards.length, 1, 'a repeated zoom key must not stack a second card');
  assert.equal(cards[0].querySelector('.ctl').textContent, '110%');
});

test('toastReady is sent once the surface installs', () => {
  const root = makeRoot();
  installToastSurface(root);
  assert.deepEqual(h.sent, [['toastReady', []]]);
});

test('reportSize measures the CARD, not the viewport-clamped .toasts box, already on the FIRST hover', () => {
  const root = makeRoot();
  installToastSurface(root, {
    fitHoverLabel: (label, text) => {
      label.textContent = text;
      // .toasts shrink-to-fits against the widget's own (narrow, on a
      // first hover) viewport. The card, a non-stretched flex item, renders wider.
      root.querySelector('.toasts').setRect({width: 40, height: 30});
      label.closest('.toast').setRect({width: 300, height: 30});
    },
  });
  window.atriumHoverUrl('https://example.com/a/very/long/path/that/needs/eliding');
  const sized = h.sent.filter(([name]) => name === 'toastStackSized');
  assert.equal(sized.length, 1, 'exactly one size report for the single hover call');
  assert.deepEqual(sized[0][1], [300, 30, 0],
      'must report the card width the fit produced, not the clamped container width');
});

test('the reported size covers the ring a card paints outside its box', () => {
  // A window clips at its frame. Sized to the card exactly, the widest card
  // loses its 1px accent ring and its rounded corners on both sides - the
  // flat edges the 2026-09-02 report photographed.
  const root = makeRoot();
  installToastSurface(root, {
    fitHoverLabel: (label, text) => {
      label.textContent = text;
      const box = root.querySelector('.toasts');
      box.style.setProperty('--ct-ring', '1px');
      box.setRect({width: 300, height: 48});
      label.closest('.toast').setRect({width: 300, height: 48});
    },
  });
  window.atriumHoverUrl('https://example.com/one');
  const sized = h.sent.filter(([name]) => name === 'toastStackSized');
  assert.deepEqual(sized[0][1], [302, 50, 1],
      'the widget must be a ring wider and taller than the card on every side, ' +
      'and must say how much of that is outset so the card keeps its corner inset');
});

test('a card mid-entry pushes the box it settles into, not the box it paints as', () => {
  // atrium-toast-peekin scales a new card to .93 and drops it 10px on the very
  // insertion that pushes, and nothing pushes again once it settles.
  const root = makeRoot();
  installToastSurface(root, {fitHoverLabel: (label, text) => label.textContent = text});
  const card = window.atriumConfirmToast('Link copied', '', true);
  const box = root.querySelector('.toasts');
  box.setRect({left: 2, top: 1, width: 166, height: 48});
  card.setRect({left: 8, top: 12, width: 154, height: 45});
  card.setLayoutBox({left: 0, top: 0, width: 166, height: 48});
  document.querySelector('.shadow-probe').style.boxShadow =
      'rgba(34, 16, 22, 0.3) 0px 5px 14px';
  h.sent.length = 0;
  window.atriumHoverUrl('');
  const pushed = h.sent.filter(([name]) => name === 'toastCardShadows');
  const shadow = JSON.parse(pushed[pushed.length - 1][1][0])[0];
  assert.deepEqual(
      {left: shadow.left, top: shadow.top,
       width: shadow.width, height: shadow.height},
      {left: 2, top: 1, width: 166, height: 48},
      'the shadow must take the card\'s layout box, not its animated one');
});

test('a dismissed card is pushed as fading, so its shadow leaves with it', () => {
  // .bye is a class, and the card keeps its box for the whole 250ms fade. A
  // shadow painted outside this document stays solid over a card nobody can
  // see any more - the card-shaped block with no card in it.
  const {clock, timers, card, push} = armEnteringCard(1000);
  push();
  clock.t = 1220;
  timers.advance(220);
  clock.t = 1360;
  card.style.animationDuration = '0.25s';
  card.classList.add('bye');
  const cards = push();
  assert.equal(cards.length, 1);
  assert.equal(cards[0].fading, true);
  assert.equal(cards[0].fadeMs, 250, 'the fade length is the CSS animation\'s own');
});

test('a card dismissed before its shadow arrived is not pushed as fading', () => {
  // There is nothing to fade out. A fade-out registers the ring solid for the
  // first frame, which puts the outline back on screen around a card halfway
  // through its own entry.
  const {clock, card, push} = armEnteringCard(1000);
  push();
  clock.t = 1100;
  card.style.animationDuration = '0.25s';
  card.classList.add('bye');
  const cards = push();
  assert.equal(cards[0].fading, false);
  assert.equal(cards[0].entering, true, 'it stays away rather than fading out');
  assert.equal(cards[0].enterMs, 0);
});

// A stamped card, its shadow probe armed, and a clock the test moves by hand.
// The first push resolves no shadow and returns early, which is what arms the
// probe, so every reading below comes from the second push onwards.
function armEnteringCard(atMs) {
  const clock = {t: atMs};
  const timers = makeFakeClock();
  const root = makeRoot();
  installToastSurface(root, {
    fitHoverLabel: (label, text) => label.textContent = text,
    clock: timers,
    now: () => clock.t,
  });
  const card = window.atriumConfirmToast('Link copied', '', true);
  card.style.animationDuration = '0.22s';
  document.querySelector('.shadow-probe').style.boxShadow =
      'rgba(34, 16, 22, 0.3) 0px 5px 14px';
  const read = () => {
    const sent = h.sent.filter(([name]) => name === 'toastCardShadows');
    assert.ok(sent.length, 'nothing pushed a shadow to read');
    return JSON.parse(sent[sent.length - 1][1][0]);
  };
  return {clock, timers, root, card, read, push: () => {
    h.sent.length = 0;
    window.atriumHoverUrl('');
    return read();
  }};
}

test('an arriving card carries no shadow at all while its keyframe runs', () => {
  // atrium-toast-peekin holds the card at scale(.93) 10px below the box the
  // ring is drawn around, so a shadow shown during those 220ms is an outline
  // beside the card rather than under it.
  const {push} = armEnteringCard(1000);
  const cards = push();
  assert.equal(cards.length, 1);
  assert.equal(cards[0].entering, true);
  assert.equal(cards[0].fading, false, 'a card cannot arrive and leave at once');
  assert.equal(cards[0].enterMs, 0, 'entering with a fade is a shadow on screen');
});

test('a re-push inside the keyframe still carries no shadow', () => {
  // An insertion provokes two pushes, and every push tears down all ten shadow
  // slots, so a card reported entering only once comes back solid microseconds
  // later under a card at 2% opacity.
  const {clock, push} = armEnteringCard(1000);
  push();
  clock.t = 1005;
  const cards = push();
  assert.equal(cards[0].entering, true);
  assert.equal(cards[0].enterMs, 0);
});

test('the shadow arrives on a push of its own, since nothing else pushes again', () => {
  // The card's box is the same before and after its keyframe, so neither the
  // mutation observer nor the resize observer fires at the end of it.
  const {clock, timers, push, read} = armEnteringCard(1000);
  push();
  h.sent.length = 0;
  clock.t = 1220;
  timers.advance(220);
  const cards = read();
  assert.equal(cards[0].entering, false, 'it is no longer held away');
  assert.equal(cards[0].enterMs, 140, 'and it is asked to fade in');
});

test('a re-push inside the fade does not ask for the fade again', () => {
  // Every push re-registers the ring. Asking a second time restarts the fade
  // from nothing, so the shadow drops out for a frame and climbs again.
  const {clock, timers, push, read} = armEnteringCard(1000);
  push();
  clock.t = 1220;
  timers.advance(220);
  clock.t = 1290;
  const cards = push();
  assert.equal(cards[0].entering, false);
  assert.equal(cards[0].enterMs, 0);
});

test('two cards that arrive apart keep their own entry windows', () => {
  // One timestamp shared by the stack would hand the second card the first
  // card's deadline, and its shadow would be on screen during its keyframe.
  const {clock, timers, push} = armEnteringCard(1000);
  push();
  // The stack prepends, so the newest card is cards[0]. Blanking the probe
  // makes the insertion's own push bail before it anchors anything, which is
  // the only way to hand a card its animation duration first.
  const probe = document.querySelector('.shadow-probe');
  probe.style.boxShadow = '';
  const second = window.atriumConfirmToast('Copied again', '', true);
  second.style.animationDuration = '0.22s';
  probe.style.boxShadow = 'rgba(34, 16, 22, 0.3) 0px 5px 14px';
  clock.t = 1220;
  timers.advance(220);
  const cards = push();
  assert.equal(cards.length, 2);
  assert.equal(cards[1].entering, false, 'the first card has settled');
  assert.equal(cards[0].entering, true, 'the newest is inside its own keyframe');
  assert.equal(cards[0].enterMs, 0);
});

test('a card keeps one id, and the card that takes its slot has another', () => {
  // The native side keys a shadow by slot. Without an id it cannot tell a slot
  // that changed hands from the same card pushed again, and the newcomer
  // inherits the fade the last occupant left on that slot.
  const {clock, timers, push} = armEnteringCard(1000);
  const first = push();
  clock.t = 1220;
  timers.advance(220);
  assert.equal(push()[0].id, first[0].id, 'the same card changed id');

  const probe = document.querySelector('.shadow-probe');
  probe.style.boxShadow = '';
  window.atriumConfirmToast('Copied again', '', true);
  probe.style.boxShadow = 'rgba(34, 16, 22, 0.3) 0px 5px 14px';
  const cards = push();
  assert.equal(cards.length, 2);
  assert.notEqual(cards[0].id, cards[1].id,
                  'the new card took slot 0 under the old card\'s id');
});

test('a settled card is pushed solid, so its shadow cannot blink out', () => {
  const {clock, timers, push} = armEnteringCard(1000);
  push();
  clock.t = 1220;
  timers.advance(220);
  clock.t = 1360;
  const cards = push();
  assert.equal(cards[0].entering, false);
  assert.equal(cards[0].enterMs, 0);
});

test('atriumSetNotificationTimeout lands on window under the name C++ calls', () => {
  const root = makeRoot();
  installToastSurface(root);
  // PushNotificationTimeoutToToastOverlay executes the literal
  // "window.atriumSetNotificationTimeout(...)" - renaming this breaks the
  // push with no error on either side.
  assert.equal(typeof window.atriumSetNotificationTimeout, 'function');
});

test('the pushed step controls the confirmation dismissal, not the CONFIRM_MS default', () => {
  const root = makeRoot();
  const clock = makeFakeClock();
  installToastSurface(root, {clock});
  window.atriumSetNotificationTimeout('threeSeconds');
  window.atriumConfirmToast('Link copied', '', true);
  clock.advance(1400);  // CONFIRM_MS - must not have dismissed yet
  assert.equal(root.querySelectorAll('.toast').length, 1,
               'the old 1400ms fallback must not still be driving this');
  clock.advance(1600);
  clock.advance(260);  // the .bye removal delay
  assert.equal(root.querySelector('.toasts'), null,
               'the pushed threeSeconds step must be the one that dismissed it');
});

test('a second, different pushed step changes the dismissal time in the same session', () => {
  const root = makeRoot();
  const clock = makeFakeClock();
  installToastSurface(root, {clock});
  window.atriumSetNotificationTimeout('threeSeconds');
  window.atriumConfirmToast('First', '', true);
  clock.advance(3000);
  clock.advance(260);
  assert.equal(root.querySelector('.toasts'), null, 'the first confirmation must be gone');

  window.atriumSetNotificationTimeout('tenSeconds');
  window.atriumConfirmToast('Second', '', true);
  clock.advance(3000);
  assert.equal(root.querySelectorAll('.toast').length, 1,
               'the new tenSeconds step must still be showing at the old threeSeconds mark');
  clock.advance(7000);
  clock.advance(260);
  assert.equal(root.querySelector('.toasts'), null);
});

test('the keep step leaves the confirmation up with no auto-dismiss timer', () => {
  const root = makeRoot();
  let scheduled = false;
  const clock = {
    set: (ms, fn) => {
      scheduled = true;
      return 1;
    },
    clear: () => {},
  };
  installToastSurface(root, {clock});
  window.atriumSetNotificationTimeout('keep');
  window.atriumConfirmToast('Link copied', '', true);
  assert.equal(scheduled, false, 'keep must not schedule a timer at all');
  assert.equal(root.querySelectorAll('.toast').length, 1);
});

test('a retired off step still shows the confirmation toast, migrated to oneSecond', () => {
  const root = makeRoot();
  installToastSurface(root);
  window.atriumSetNotificationTimeout('off');
  const card = window.atriumConfirmToast('Link copied', '', true);
  assert.ok(card);
  assert.ok(root.querySelector('.toast'));
});

// Records the width budget each fit was handed, which is the whole question
// for the pane-width work below.
function installWithBudgetSpy(root, extra = {}) {
  const budgets = [];
  installToastSurface(root, {
    fitHoverLabel: (label, text, maxWidth) => {
      budgets.push(maxWidth);
      label.textContent = text;
      if (extra.onFit) {
        extra.onFit();
      }
    },
  });
  return budgets;
}

test('atriumSetPaneWidth lands on window under the name C++ calls', () => {
  const root = makeRoot();
  installToastSurface(root);
  // ToastOverlayWidget::PushPaneWidth executes the literal
  // "window.atriumSetPaneWidth(...)" - renaming this breaks the push silently.
  assert.equal(typeof window.atriumSetPaneWidth, 'function');
});

test('the fit budget is a share of the pushed pane width, not of the screen', () => {
  const root = makeRoot();
  const budgets = installWithBudgetSpy(root);
  // Not 1200: the shim's own screen reads that wide, so the old proxy would
  // have produced this same number and the test would not discriminate.
  window.atriumSetPaneWidth(1500);
  window.atriumHoverUrl('https://example.com/one');
  assert.deepEqual(budgets, [600], '0.4 of the 1500px pane the widget pushed');
});

test('a wider pane gives the card a wider budget at the same screen size', () => {
  const root = makeRoot();
  const budgets = installWithBudgetSpy(root);
  window.atriumSetPaneWidth(1200);
  window.atriumHoverUrl('https://example.com/one');
  window.atriumHoverUrl('');
  window.atriumSetPaneWidth(1500);
  window.atriumHoverUrl('https://example.com/one');
  assert.deepEqual(budgets, [480, 600],
      'the second hover must fit against the wider pane');
});

test('a very wide pane is capped, so the card never spans the page', () => {
  const root = makeRoot();
  const budgets = installWithBudgetSpy(root);
  window.atriumSetPaneWidth(4000);
  window.atriumHoverUrl('https://example.com/one');
  assert.deepEqual(budgets, [640],
      '0.4 of 4000 is 1600, and a status line that wide stops being scannable');
});

test('a narrow pane still gets the floor, so the card never collapses', () => {
  const root = makeRoot();
  const budgets = installWithBudgetSpy(root);
  window.atriumSetPaneWidth(300);
  window.atriumHoverUrl('https://example.com/one');
  assert.deepEqual(budgets, [240], '0.4 of 300 is under the 240px floor');
});

test('a pane width change re-fits the card in place, with no re-hover', () => {
  const root = makeRoot();
  const budgets = installWithBudgetSpy(root);
  window.atriumSetPaneWidth(1200);
  window.atriumHoverUrl('https://example.com/one');
  // What a sidebar drag does: the pane narrows under a card already up.
  window.atriumSetPaneWidth(800);
  assert.deepEqual(budgets, [480, 320],
      'the narrower pane must re-fit the standing card');
});

test('an unchanged pane width re-fits nothing, so a size report cannot loop', () => {
  const root = makeRoot();
  const budgets = installWithBudgetSpy(root);
  window.atriumSetPaneWidth(1200);
  window.atriumHoverUrl('https://example.com/one');
  window.atriumSetPaneWidth(1200);
  window.atriumSetPaneWidth(1200);
  assert.equal(budgets.length, 1, 'only the hover itself may fit');
});

test('a width arriving mid-fit re-fits once the fit settles, without re-entering it', () => {
  // reportSize runs inside the fit through the MutationObserver, and the size
  // report is what makes the widget push a width back. That width must reach
  // the standing card, but only after the fit it arrived under has finished.
  const root = makeRoot();
  let fits = 0;
  const budgets = installWithBudgetSpy(root, {
    onFit: () => {
      fits += 1;
      if (fits === 1) {
        window.atriumSetPaneWidth(1500);
      }
    },
  });
  window.atriumSetPaneWidth(1200);
  window.atriumHoverUrl('https://example.com/one');
  assert.deepEqual(budgets, [480, 600],
      'the second pass runs after the first, not inside it');
  // A third pass would mean the deferral loops rather than settling.
  assert.equal(fits, 2);
});

function hintText(root) {
  const el = root.querySelector('.toast .cthint');
  return el ? el.textContent : null;
}

test('atriumSetNavModifiers lands on window under the name C++ calls', () => {
  const root = makeRoot();
  installToastSurface(root);
  // AtriumBrowserWindow::SetSidebarNavModifiers executes the literal
  // "window.atriumSetNavModifiers(...)" - renaming this breaks the push
  // silently on both sides.
  assert.equal(typeof window.atriumSetNavModifiers, 'function');
});

test('each modifier names what mac dispositions actually do with the link', () => {
  const root = makeRoot();
  installToastSurface(root, {fitHoverLabel: (label, text) => label.textContent = text});
  window.atriumHoverUrl('https://example.com/one');
  const cases = [
    // shift, cmd, option, label
    [true, false, false, 'Peek'],
    [false, true, false, 'Background tab'],
    [true, true, false, 'New tab'],
    [false, true, true, 'Little window'],
    [false, false, true, 'Peek'],
    // NavigationPolicyFromEventModifiers takes the new-tab branch before the
    // split one when shift is down too, so this is a focused tab.
    [true, true, true, 'New tab'],
    // Shift and Option both land on NEW_WINDOW, which the peek intercept takes.
    [true, false, true, 'Peek'],
  ];
  for (const [shift, cmd, option, want] of cases) {
    window.atriumSetNavModifiers(shift, cmd, option);
    assert.equal(hintText(root), want,
                 `shift=${shift} cmd=${cmd} option=${option}`);
  }
});

test('the hint appears and disappears while the pointer stays on the link', () => {
  const root = makeRoot();
  installToastSurface(root, {fitHoverLabel: (label, text) => label.textContent = text});
  window.atriumHoverUrl('https://example.com/one');
  assert.equal(hintText(root), null, 'no modifier, no hint');
  window.atriumSetNavModifiers(true, false, false);
  assert.equal(hintText(root), 'Peek');
  // No second atriumHoverUrl anywhere above or below: releasing alone clears it.
  window.atriumSetNavModifiers(false, false, false);
  assert.equal(hintText(root), '');
  assert.equal(root.querySelector('.toast .ctl').textContent,
               'https://example.com/one', 'the url itself is untouched');
});

test('a held modifier survives a retarget to another link', () => {
  const root = makeRoot();
  installToastSurface(root, {fitHoverLabel: (label, text) => label.textContent = text});
  window.atriumSetNavModifiers(false, true, false);
  window.atriumHoverUrl('https://first.example/one');
  assert.equal(hintText(root), 'Background tab');
  window.atriumHoverUrl('https://second.example/two');
  assert.equal(hintText(root), 'Background tab',
               'the key is still down, so the new link carries the same hint');
});

test('the hint takes its width out of the url budget, not out of the pane', () => {
  const root = makeRoot();
  const budgets = [];
  installToastSurface(root, {
    fitHoverLabel: (label, text, maxWidth) => {
      budgets.push(maxWidth);
      label.textContent = text;
      const hint = label.closest('.toast').querySelector('.cthint');
      if (hint) {
        hint.setRect({width: hint.textContent ? 60 : 0});
      }
    },
  });
  window.atriumSetPaneWidth(1500);
  window.atriumHoverUrl('https://example.com/one');
  assert.deepEqual(budgets, [600], 'no hint yet');
  // The shim has no layout engine, so the hint's box only exists once a fit
  // has run with the text in place. A real engine measures it on the spot.
  window.atriumSetNavModifiers(true, false, false);
  assert.equal(budgets.length, 2, 'the hint must force a re-fit');
  window.atriumHoverUrl('https://example.com/two');
  assert.equal(budgets[2], 540, 'the 60px hint comes out of the url\'s room');
});

// the widget is placed against one edge of the pane and resizes a
// round trip after the page lays out. A stack anchored to the other edge slides
// the card inside its own surface for that frame, and a card wider than the
// stale viewport reports an offset left of the widget entirely.
test('the stack anchors to the same edge of the widget in both variants', () => {
  const plain = STACK_CSS.match(/\n\.toastroot \.toasts\s*{([\s\S]*?)\n}/);
  const left = STACK_CSS.match(/\n\.toastroot \.toasts\.left\s*{([\s\S]*?)\n}/);
  assert.ok(plain, 'the stack has no rule of its own inside the widget');
  assert.ok(left, 'nothing anchors the stack on the leading side');
  assert.match(plain[1], /right:\s*var\(--ct-ring\)/);
  assert.match(plain[1], /left:\s*auto/);
  assert.match(left[1], /left:\s*var\(--ct-ring\)/,
               'the left variant must pin the leading edge, not inherit right');
  assert.match(left[1], /right:\s*auto/,
               'both edges pinned makes the stack stretch instead of anchor');
});

test('a side pushed before the first card still reaches it', () => {
  // The native side comes with the show request, and showToast does not build
  // .toasts until that request produces a card.
  const root = makeRoot();
  installToastSurface(root, {fitHoverLabel: (label, text) => label.textContent = text});

  window.atriumToastSide('left');
  window.atriumHoverUrl('https://example.com/one');
  assert.equal(root.querySelector('.toasts').classList.contains('left'), true,
               'a widget hung off the leading edge must anchor its card there');

  window.atriumToastSide('right');
  assert.equal(root.querySelector('.toasts').classList.contains('left'), false);
});
