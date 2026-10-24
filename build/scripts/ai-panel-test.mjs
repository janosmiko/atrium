#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// route-test.mjs covers the backtick-to-render reaching path.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';
import {waitFor} from './wait-for.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

const h = installDOM({pathname: '/ai-panel'});

const {installAiPanel, clampWidth, STUB_AI_BACKEND} =
    await import(path.join(ATRIUM_DIR, 'ai-panel.js'));
const {currentHint, hideHint} = await import(path.join(ATRIUM_DIR, 'hint.js'));
const {makeFakeClock} = await import('./fake-clock.mjs');

function hintText() {
  return currentHint()?.querySelector('.hint-text')?.textContent ?? null;
}

const REGISTERED = new Set(
    fs.readFileSync(
          path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
          'utf8')
        .split('kSurfaceMessages[] = {')[1]
        .split('};')[0]
        .match(/"[^"]+"/g)
        .map((quoted) => quoted.slice(1, -1)));

// Width clamps to [220, 400]. The 45%-of-window ceiling is the browser's,
// which is the only side that knows how wide the window is.
test('clampWidth enforces the floor', () => {
  assert.equal(clampWidth(100), 220);
});

test('clampWidth enforces the 400px cap', () => {
  assert.equal(clampWidth(1000), 400);
});

// this document IS the panel, so taking its innerWidth for the
// window width clamped a 280px panel to 45% of itself.
test('clampWidth does not shrink a width to a fraction of the panel itself', () => {
  window.innerWidth = 280;
  assert.equal(clampWidth(280), 280);
});

test('every name ai-panel.js sends is registered in kSurfaceMessages', () => {
  assert.ok(REGISTERED.has('aiPanelReady'));
  assert.ok(REGISTERED.has('aiPanelToggled'));
  assert.ok(REGISTERED.has('aiPanelWidthChanged'));
  // An unregistered name is a FATAL in the browser process, not a no-op.
  assert.ok(REGISTERED.has('aiPanelRowMark'));
});

// the port kept drag-resize and dropped the double-click reset,
// which the lab pairs with it on the same handle (swipe-lab.html:1525-1527).
function openPanel() {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {getContext: () => ({pageTitle: 'Doc'})});
  h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
  return root;
}

test('double-clicking the resize handle returns the panel to its default width', () => {
  const root = openPanel();
  const panel = root.querySelector('.aipanel');
  const handle = root.querySelector('.aipresize');

  // A real drag first, because the panel opens AT the default - a reset from
  // the default is a no-op and would make this pass without proving anything.
  handle.dispatchEvent(new Event('pointerdown', {bubbles: true, screenX: 500}));
  h.window.dispatchEvent(new Event('pointermove', {bubbles: true, screenX: 560}));
  h.window.dispatchEvent(new Event('pointerup', {bubbles: true}));
  const widened = h.sent.filter((m) => m[0] === 'aiPanelWidthChanged').pop();
  assert.notDeepEqual(widened, ['aiPanelWidthChanged', [280]],
                      'the drag did not move the width, so the reset below is vacuous');

  h.sent.length = 0;
  handle.dispatchEvent(new Event('dblclick', {bubbles: true}));

  // Committed the same way a drag commits on pointerup, so a reset survives a
  // reopen exactly as a dragged width does.
  assert.deepEqual(h.sent.filter((m) => m[0] === 'aiPanelWidthChanged'),
                   [['aiPanelWidthChanged', [280]]]);
  h.reset();
});

test('opening the panel via the hotkey focuses the prompt input', () => {
  const root = openPanel();
  assert.equal(document.activeElement, root.querySelector('.aichatinput'),
               'the hotkey must focus the prompt so typing needs no click');
  h.reset();
});

test('closing then reopening the panel via the hotkey focuses the prompt again',
     () => {
       const root = openPanel();
       root.querySelector('[data-ai-panel-close]').click();
       document.activeElement = document.body;

       h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
       assert.equal(document.activeElement, root.querySelector('.aichatinput'));
       h.reset();
     });

test('the handle names both gestures, since the reset is not discoverable', () => {
  const root = openPanel();
  assert.equal(root.querySelector('.aipresize').title,
               'Drag to resize \u00b7 double-click resets');
  h.reset();
});

// The panel's own chrome must read as the app's chrome: the same .ov-btn
// glyph buttons find.js/peek-controls.js/devbar.js use, and the same
// .ovresize hairline the docked sidebar's resize handle wears.
test('pin, close and send are .ov-btn glyph buttons with a tooltip each', () => {
  const root = openPanel();
  for (const selector of ['.aippin', '.aipclose', '.aichatsend']) {
    const button = root.querySelector(selector);
    assert.equal(button.tagName, 'BUTTON', selector);
    assert.equal(button.classList.contains('ov-btn'), true, selector);
    assert.ok(button.title, `${selector} has no tooltip`);
    assert.ok(button.querySelector('svg.ov-icon'), `${selector} has no glyph`);
  }
  h.reset();
});

test('the pin button reports its pressed state through aria-pressed', () => {
  const root = openPanel();
  const pinButton = root.querySelector('.aippin');
  assert.equal(pinButton.getAttribute('aria-pressed'), 'false');
  pinButton.click();
  assert.equal(pinButton.getAttribute('aria-pressed'), 'true');
  h.reset();
});

test('the resize handle wears the same class as the docked sidebar\'s own handle',
     () => {
       const root = openPanel();
       assert.equal(
           root.querySelector('.aipresize').classList.contains('ovresize'),
           true);
       h.reset();
     });

test('the injected backend, not the stub, renders the summary', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  const seen = [];
  installAiPanel(root, {
    backend: {
      summarize(context) {
        seen.push(context);
        return 'injected summary';
      },
      chat: STUB_AI_BACKEND.chat,
    },
    getContext: () => ({pageTitle: 'Injected Page'}),
  });

  h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));

  const summaryText = root.querySelector('.aisum p').textContent;
  assert.equal(summaryText, 'injected summary',
               'the stub must be swappable - a real backend has to win over it');
  assert.deepEqual(seen, [{pageTitle: 'Injected Page'}]);
  h.reset();
});

test('the stub backend answers with "(mock)" when no backend is injected',
     async () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {getContext: () => ({pageTitle: 'Doc'})});

       h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
       const input = root.querySelector('.aichatinput');
       input.value = 'what is this about';
       root.querySelector('[data-ai-panel-send]').click();

       await waitFor(() => root.querySelector('.aibubble-ai'));
       const reply = root.querySelector('.aibubble-ai');
       assert.ok(reply, 'the chat stub never replied');
       assert.match(reply.textContent, /^\(mock\)/,
                    'the stub must be named as a stub in its own output');
       h.reset();
     });

test('the panel paints no background of its own', () => {
  const cssPath = path.join(ATRIUM_DIR, 'ai-panel.css');
  const css = fs.readFileSync(cssPath, 'utf8');
  const panelRule = css.match(/\.aipanel\s*{[^}]*}/)[0];
  assert.match(panelRule, /background:\s*none/,
               'transparent chrome means no background paint here at all');
});

// the ring duplicated the caret as a focus signal.
test('the prompt field shows no focus ring', () => {
  const css = fs.readFileSync(path.join(ATRIUM_DIR, 'ai-panel.css'), 'utf8');
  const rule = css.match(/\.aichatinput:focus-visible\s*{[^}]*}/)[0];
  assert.doesNotMatch(rule, /outline:\s*\d/);
});

// .aipanel clips overflow, so a handle straddling the
// seam with a negative offset had half its hit area, and the hover accent
// riding that half, clipped away - hovering the edge showed nothing.
test('the resize handle sits flush inside the panel, not straddling the clipped seam',
     () => {
       const css =
           fs.readFileSync(path.join(ATRIUM_DIR, 'ai-panel.css'), 'utf8');
       assert.doesNotMatch(
           css.match(/#atrium-root\.aionleft \.aipresize\s*{[^}]*}/)[0],
           /-\d/);
       assert.doesNotMatch(
           css.match(/#atrium-root\.aionright \.aipresize\s*{[^}]*}/)[0],
           /-\d/);
     });

// the header, body and prompt field sit on a raised card rather
// than straight on the transparent backdrop.
test('buildAiSurface wraps the panel body in a card element', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {getContext: () => ({pageTitle: null})});

  const card = root.querySelector('.aipanel > .aipcard');
  assert.ok(card, 'buildAiSurface must build a .aipcard surface');
  h.reset();
});

// The panel sits on the sidebar's backdrop. It used to take the backdrop tint
// itself as its text colour, which is the one colour that cannot read on it.
test('the panel paints with the sidebar ink tokens, not the tint', () => {
  const css = fs.readFileSync(path.join(ATRIUM_DIR, 'ai-panel.css'), 'utf8');
  assert.doesNotMatch(css, /--aip-ink/);
  assert.match(css.match(/\.aipanel\s*{[^}]*}/)[0], /color:\s*var\(--fg\)/);
  assert.match(css.match(/\.aipcard\s*{[^}]*}/)[0],
               /background:\s*var\(--fill\)/);
  const sidebar =
      fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar.css'), 'utf8');
  const glass = sidebar.match(
      /html:not\(\.atrium-native-chrome\)([^{]*)\{[^}]*--atrium-glass-ink/);
  assert.ok(glass, 'sidebar.css lost its glass ink rule');
  assert.match(glass[1], /#atrium-root\.aipanelroot/,
               'the glass ink tokens never reach the AI panel root');
});

// The buttons, the prompt field and the native card read ov-* tokens. A
// theme's paper outranks the native class in overlay.css, so without these
// the panel wore the theme's paper beside a neutral sidebar.
test('the panel maps the overlay tokens to the sidebar, glass and native', () => {
  const css = fs.readFileSync(path.join(ATRIUM_DIR, 'ai-panel.css'), 'utf8');
  const root = css.match(/\n#atrium-root\.aipanelroot\s*{[^}]*--ov-[^}]*}/);
  assert.ok(root, 'ai-panel.css maps no overlay token on the panel root');
  assert.match(root[0], /--ov-paper:\s*var\(--fill\)/);
  assert.match(root[0], /--ov-fg:\s*var\(--fg\)/);
  const native = css.match(
      /html\.atrium-native-chrome\s+#atrium-root\.aipanelroot\s*{[^}]*}/);
  assert.ok(native, 'ai-panel.css has no native rule on the panel root');
  assert.match(native[0], /--ov-paper:\s*var\(--atrium-surround-color/);
});

// Drag the handle and confirm the reported width is clamped and sent.
test('dragging the resize handle clamps and reports the width', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  window.innerWidth = 1200;
  installAiPanel(root, {getContext: () => ({pageTitle: null})});

  const dragTo = (screenX) => {
    root.querySelector('[data-ai-panel-resize]')
        .dispatchEvent(new Event('pointerdown', {screenX: 280}));
    window.dispatchEvent(new Event('pointermove', {screenX}));
    window.dispatchEvent(new Event('pointerup', {}));
  };

  h.sent.length = 0;
  dragTo(0);  // dragging far past the floor
  const widthSent = h.sent.find((m) => m[0] === 'aiPanelWidthChanged');
  assert.ok(widthSent, 'no width was reported to the browser');
  assert.equal(widthSent[1][0], 220, 'must clamp to the 220px floor');

  h.sent.length = 0;
  dragTo(2000);  // dragging far past the cap
  const capped = h.sent.find((m) => m[0] === 'aiPanelWidthChanged');
  assert.equal(capped[1][0], 400, 'must clamp to the 400px cap at this width');
  h.reset();
});

// The panel document IS its own WebView, so clientX is relative to the
// panel's left edge - which SetAiPanelWidth moves on every committed width.
// Right-docked, the right edge stays put, so panelLeft = windowWidth - width.
test('a one-way drag tracks the pointer, not the panel\'s own shifting edge',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       window.innerWidth = 1200;
       installAiPanel(root, {getContext: () => ({pageTitle: null})});
       window.atriumSetAiPanelOnRight(true);

       const WINDOW_WIDTH = 1200;
       const START_WIDTH = 280;
       let panelLeft = WINDOW_WIDTH - START_WIDTH;
       const screenStart = 900;
       const handle = root.querySelector('[data-ai-panel-resize]');

       h.sent.length = 0;
       handle.dispatchEvent(new Event(
           'pointerdown',
           {screenX: screenStart, clientX: screenStart - panelLeft}));

       const committed = [];
       for (let screenX = screenStart - 1; screenX >= screenStart - 60;
            screenX--) {
         h.sent.length = 0;
         window.dispatchEvent(new Event(
             'pointermove', {screenX, clientX: screenX - panelLeft}));
         const sent = h.sent.find((m) => m[0] === 'aiPanelWidthChanged');
         if (!sent) {
           continue;
         }
         committed.push(sent[1][0]);
         panelLeft = WINDOW_WIDTH - sent[1][0];
       }
       window.dispatchEvent(new Event('pointerup', {}));

       assert.ok(committed.length > 10,
                 'the drag never reported enough width changes');
       for (let i = 1; i < committed.length; i++) {
         assert.ok(
             committed[i] >= committed[i - 1],
             `width moved backward mid-drag: ${committed[i - 1]} -> ${
                 committed[i]}`);
       }
       // 60px of one-way travel must land the panel exactly 60px wider.
       assert.equal(committed[committed.length - 1] - START_WIDTH, 60);
       h.reset();
     });

test('install announces the renderer is live', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  h.sent.length = 0;
  installAiPanel(root, {getContext: () => ({pageTitle: null})});
  assert.deepEqual(h.sent, [['aiPanelReady', []]]);
  h.reset();
});

// --- attach-to-tab lifecycle (pin, follow, re-target) --------

const open = () =>
    h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
const pin = (root) => root.querySelector('[data-ai-panel-pin]').click();

test('unpinned - switching tab or space closes the panel', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {getContext: () => ({pageTitle: null})});

  window.atriumSetAiPanelTarget({tabId: '1', spaceIndex: 0, pageTitle: 'A'});
  open();
  assert.equal(root.classList.contains('aipopen'), true);

  window.atriumSetAiPanelTarget({tabId: '2', spaceIndex: 0, pageTitle: 'B'});
  assert.equal(root.classList.contains('aipopen'), false,
               'unpinned panel must close on a tab switch');
  h.reset();
});

// TabId is a process-wide counter on the WebContents, so a tab moved to
// another space is the same tab and keeps its panel.
test('a tab moved to another space keeps its open panel and conversation',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {
         backend: {summarize: () => 'sum', chat: () => () => {}},
         getContext: () => ({pageTitle: null}),
       });

       window.atriumSetAiPanelTarget(
           {tabId: 1, spaceIndex: 0, pageTitle: 'A'});
       open();
       root.querySelector('.aichatinput').value = 'question on A';
       root.querySelector('[data-ai-panel-send]').click();

       window.atriumSetAiPanelTarget(
           {tabId: 1, spaceIndex: 1, pageTitle: 'A'});
       assert.equal(root.classList.contains('aipopen'), true,
                    'moving the tab to another space must not hide its panel');
       assert.equal(
           root.querySelectorAll('.aichatlog .aibubble-me, .aichatlog .aibubble-ai')
               .length,
           1, 'moving the tab to another space must keep its chat');
       h.reset();
     });

test('pinned - switching tab or space keeps the panel open and re-attaches',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {getContext: () => ({pageTitle: null})});

       window.atriumSetAiPanelTarget(
           {tabId: '1', spaceIndex: 0, pageTitle: 'A'});
       open();
       pin(root);
       window.atriumSetAiPanelTarget(
           {tabId: '2', spaceIndex: 0, pageTitle: 'B'});

       assert.equal(root.classList.contains('aipopen'), true,
                    'pinned panel must stay open across the switch');
       assert.equal(root.querySelector('.aictx').textContent, 'About: B',
                    'pinned panel must re-attach to the new tab');
       h.reset();
     });

// A pinned panel carries one thread rather than swapping each tab's in.
// Asked unpinned, where the swap is still the behaviour.
test('unpinned re-attachment switches the summary and chat log, not just the context line',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {
         backend: {
           summarize: (ctx) => `summary for ${ctx.pageTitle}`,
           chat: STUB_AI_BACKEND.chat,
         },
         getContext: () => ({pageTitle: null}),
       });

       window.atriumSetAiPanelTarget(
           {tabId: '1', spaceIndex: 0, pageTitle: 'A'});
       open();
       assert.equal(root.querySelector('.aisum p').textContent,
                    'summary for A');
       const input = root.querySelector('.aichatinput');
       input.value = 'hi';
       root.querySelector('[data-ai-panel-send]').click();
       assert.equal(
           root.querySelectorAll('.aichatlog .aibubble-me, .aichatlog .aibubble-ai')
               .length,
           1, 'the chat message did not land before re-targeting');

       window.atriumSetAiPanelTarget(
           {tabId: '2', spaceIndex: 0, pageTitle: 'B'});
       assert.equal(
           root.querySelector('.aisum').children.length, 0,
           'a stale summary from the old tab must not survive re-attachment');
       assert.equal(
           root.querySelectorAll('.aichatlog .aibubble').length, 0,
           'a stale chat log from the old tab must not survive re-attachment');
       h.reset();
     });

test('chat acts on whichever tab the panel is currently attached to',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       const seenContexts = [];
       installAiPanel(root, {
         backend: {
           summarize: STUB_AI_BACKEND.summarize,
           chat: (ctx, query, respond) => {
             seenContexts.push(ctx);
             return STUB_AI_BACKEND.chat(ctx, query, respond);
           },
         },
         getContext: () => ({pageTitle: null}),
       });

       window.atriumSetAiPanelTarget(
           {tabId: '1', spaceIndex: 0, pageTitle: 'A'});
       open();
       pin(root);
       window.atriumSetAiPanelTarget(
           {tabId: '2', spaceIndex: 0, pageTitle: 'B'});
       const input = root.querySelector('.aichatinput');
       input.value = 'q';
       root.querySelector('[data-ai-panel-send]').click();

       // extraTabIds: the tab on screen is still what chat
       // acts on, and a pinned thread also carries the pages it travelled.
       assert.deepEqual(
           seenContexts,
           [{tabId: '2', spaceIndex: 0, pageTitle: 'B', extraTabIds: ['1']}],
           'chat must act on the re-attached tab, not the one the panel opened on');
       h.reset();
     });

// The pinned half is in the test below. Unpinned, the
// log really is swapped for the new tab's, so the reply still has to be
// dropped - that is the case this guards.
test('re-targeting cancels a pending chat reply so it cannot land in the new tab\'s log',
     async () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {getContext: () => ({pageTitle: null})});

       window.atriumSetAiPanelTarget(
           {tabId: '1', spaceIndex: 0, pageTitle: 'A'});
       open();
       const input = root.querySelector('.aichatinput');
       input.value = 'question';
       root.querySelector('[data-ai-panel-send]').click();
       window.atriumSetAiPanelTarget(
           {tabId: '2', spaceIndex: 0, pageTitle: 'B'});  // re-attach mid-flight

       await new Promise((resolve) => setTimeout(resolve, 500));
       assert.equal(
           root.querySelectorAll('.aichatlog .aibubble-ai').length, 0,
           'a reply owed to tab A must not land in tab B\'s log after re-attachment');
       h.reset();
     });

test('a reply owed when a pinned panel changes tab still lands, because the thread did not change',
     async () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {getContext: () => ({pageTitle: null})});

       window.atriumSetAiPanelTarget(
           {tabId: '1', spaceIndex: 0, pageTitle: 'A'});
       open();
       pin(root);
       const input = root.querySelector('.aichatinput');
       input.value = 'question';
       root.querySelector('[data-ai-panel-send]').click();
       window.atriumSetAiPanelTarget(
           {tabId: '2', spaceIndex: 0, pageTitle: 'B'});

       await new Promise((resolve) => setTimeout(resolve, 500));
       assert.equal(
           root.querySelectorAll('.aichatlog .aibubble-ai').length, 1,
           'dropping this reply would lose the answer to a question the user can still see');
       h.reset();
     });

test('unpinning drops a reply owed to the roaming thread',
     async () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {getContext: () => ({pageTitle: null})});

       window.atriumSetAiPanelTarget(
           {tabId: '1', spaceIndex: 0, pageTitle: 'A'});
       open();
       pin(root);
       const input = root.querySelector('.aichatinput');
       input.value = 'question';
       root.querySelector('[data-ai-panel-send]').click();
       pin(root);  // unpin swaps the roaming thread out for tab A's own

       await new Promise((resolve) => setTimeout(resolve, 500));
       assert.equal(
           root.querySelectorAll('.aichatlog .aibubble-ai').length, 0,
           'a reply owed to the roaming thread must not land in the tab\'s own log');
       h.reset();
     });

// The panel belongs to its tab: an unpinned switch hides it, the switch back
// shows the same conversation, and another tab never sees that history.
function installPerTabPanel() {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {
    backend: {
      summarize: (ctx) => `summary for ${ctx.pageTitle}`,
      chat: () => () => {},
    },
    getContext: () => ({pageTitle: null}),
  });
  return root;
}

function say(root, text) {
  root.querySelector('.aichatinput').value = text;
  root.querySelector('[data-ai-panel-send]').click();
}

const TAB_A = {tabId: 1, spaceIndex: 0, pageTitle: 'A'};
const TAB_B = {tabId: 2, spaceIndex: 0, pageTitle: 'B'};

test('unpinned - switching back to a tab shows its panel with the same conversation',
     () => {
       const root = installPerTabPanel();
       window.atriumSetAiPanelTarget(TAB_A);
       open();
       say(root, 'question on A');

       window.atriumSetAiPanelTarget(TAB_B);
       assert.equal(root.classList.contains('aipopen'), false,
                    'the panel must hide on a tab switch');

       h.sent.length = 0;
       window.atriumSetAiPanelTarget(TAB_A);
       assert.equal(root.classList.contains('aipopen'), true,
                    'the panel must come back on the tab it was open on');
       assert.deepEqual(h.sent.filter((m) => m[0] === 'aiPanelToggled'),
                        [['aiPanelToggled', [true]]],
                        'the browser must widen the container again');
       assert.equal(root.querySelector('.aisum p').textContent, 'summary for A');
       assert.deepEqual(
           [...root.querySelectorAll('.aichatlog .aibubble')].map(
               (b) => b.textContent),
           ['question on A']);
       assert.equal(root.querySelector('.aictx').textContent, 'About: A');
       h.reset();
     });

// The panel comes back because the tab was switched to, not because the
// user asked to open it - it must not steal focus from whatever the page had.
test('switching back to a tab with an open panel does not steal focus',
     () => {
       const root = installPerTabPanel();
       window.atriumSetAiPanelTarget(TAB_A);
       open();
       root.querySelector('.aichatinput').focus();

       window.atriumSetAiPanelTarget(TAB_B);
       document.activeElement = document.body;

       window.atriumSetAiPanelTarget(TAB_A);
       assert.equal(root.classList.contains('aipopen'), true,
                    'the panel must come back on the tab it was open on');
       assert.notEqual(document.activeElement, root.querySelector('.aichatinput'),
                       'an implicit reopen from a tab switch must not focus the prompt');
       h.reset();
     });

test('opening the panel on another tab shows none of the first tab\'s history',
     () => {
       const root = installPerTabPanel();
       window.atriumSetAiPanelTarget(TAB_A);
       open();
       say(root, 'question on A');

       window.atriumSetAiPanelTarget(TAB_B);
       open();
       assert.equal(root.classList.contains('aipopen'), true);
       assert.equal(root.querySelectorAll('.aichatlog .aibubble').length, 0,
                    'tab B must not show tab A\'s chat');
       assert.equal(root.querySelector('.aisum p').textContent, 'summary for B');

       say(root, 'question on B');
       window.atriumSetAiPanelTarget(TAB_A);
       assert.deepEqual(
           [...root.querySelectorAll('.aichatlog .aibubble')].map(
               (b) => b.textContent),
           ['question on A'], 'tab A must not show tab B\'s chat');
       h.reset();
     });

test('a tab that closed the panel stays closed when switched back to', () => {
  const root = installPerTabPanel();
  window.atriumSetAiPanelTarget(TAB_A);
  open();
  root.querySelector('[data-ai-panel-close]').click();
  window.atriumSetAiPanelTarget(TAB_B);
  window.atriumSetAiPanelTarget(TAB_A);
  assert.equal(root.classList.contains('aipopen'), false);
  h.reset();
});

// A pinned panel carries one thread rather than swapping the tab's own
// in and out.
test('a pinned thread survives a tab switch and comes back to it',
     () => {
       const root = installPerTabPanel();
       window.atriumSetAiPanelTarget(TAB_A);
       open();
       pin(root);
       say(root, 'question on A');

       window.atriumSetAiPanelTarget(TAB_B);
       assert.equal(root.classList.contains('aipopen'), true);
       assert.equal(root.querySelectorAll('.aichatlog .aibubble-me').length, 1,
                    'the pinned thread must follow the user to tab B');
       say(root, 'question on B');

       // Back on A the thread must hold both questions. Counting only A's
       // would also pass at 1, which is why B's question is asked first.
       window.atriumSetAiPanelTarget(TAB_A);
       assert.equal(root.querySelectorAll('.aichatlog .aibubble-me').length, 2,
                    'returning to A must not swap in A\'s own thread');
       assert.equal(
           [...root.querySelectorAll('.aichatlog .aibubble-me')]
               .map((b) => b.textContent)
               .join('|'),
           'question on A|question on B',
           'the pinned thread must be one conversation in the order it was said');
       h.reset();
     });

test('pinning leaves the tab\'s own thread untouched and unpinning hands it back',
     () => {
       const root = installPerTabPanel();
       window.atriumSetAiPanelTarget(TAB_A);
       open();
       say(root, 'A\'s own question');

       pin(root);
       assert.equal(root.querySelectorAll('.aichatlog .aibubble-me').length, 0,
                    'the roaming thread starts empty, it does not adopt A\'s');
       say(root, 'roaming question');
       window.atriumSetAiPanelTarget(TAB_B);
       window.atriumSetAiPanelTarget(TAB_A);

       pin(root);  // unpin, back on A
       assert.equal(root.querySelectorAll('.aichatlog .aibubble-me').length, 1,
                    'unpinning must hand back A\'s own thread, not the roaming one');
       assert.equal(
           root.querySelector('.aichatlog .aibubble-me').textContent,
           'A\'s own question',
           'A\'s thread must be exactly as it was before the pinned session');

       pin(root);  // re-pin
       assert.equal(root.querySelectorAll('.aichatlog .aibubble-me').length, 1,
                    're-pinning must resume the roaming thread, not empty it');
       assert.equal(root.querySelector('.aichatlog .aibubble-me').textContent,
                    'roaming question');
       h.reset();
     });

// --- a streamed answer grows one bubble, it does not queue them ---

// A backend that hands its respond() straight back, so the test drives the
// stream itself rather than racing a timer.
function installStreamingPanel() {
  const root = document.createElement('div');
  document.body.appendChild(root);
  let reply = null;
  installAiPanel(root, {
    backend: {
      summarize: () => '',
      chat: (ctx, query, respond) => {
        reply = respond;
        return () => { reply = null; };
      },
    },
    getContext: () => ({pageTitle: 'A', tabId: 1, spaceIndex: 0}),
  });
  return {root, push: (...args) => reply && reply(...args)};
}

test('a streamed answer grows one bubble, not one per chunk',
     () => {
       const {root, push} = installStreamingPanel();
       open();
       say(root, 'what is this page');
       assert.ok(root.querySelector('.aistatus .ailoader'),
                 'the loader stands above the prompt from the moment we ask');

       push('The page', false);
       const bubbles = () => root.querySelectorAll('.aichatlog .aibubble-ai');
       assert.equal(bubbles().length, 1);
       assert.equal(bubbles()[0].textContent, 'The page');
       assert.equal(root.querySelector('.aichatlog .ailoader'), null,
                    'the loader must not be a message in the log');

       push('The page opens with a filmography.', true);
       assert.equal(bubbles().length, 1,
                    'a second chunk must grow the bubble, not add one');
       assert.equal(bubbles()[0].textContent,
                    'The page opens with a filmography.');
       assert.equal(root.querySelector('.aistatus .ailoader'), null,
                    'the finished reply must take the loader down');
       h.reset();
     });

// A reasoning model streams its reasoning long before any answer. The
// loader must not come down on that first chunk, or the panel looks idle for
// the whole think.
test('the loader stays up while only reasoning streams', () => {
  const {root, push} = installStreamingPanel();
  open();
  say(root, 'what is this page');
  push('', false, false, 'Let me look at the headings first.');

  assert.ok(root.querySelector('.aithink'), 'setup: the fold must be up');
  assert.ok(root.querySelector('.aistatus .ailoader'),
            'the loader came down while the model was still thinking');

  push('The page opens with a filmography.', true);
  assert.equal(root.querySelector('.aistatus .ailoader'), null);
  h.reset();
});

// createNativeAiBackend sends every chunk as `reasoning || ''`, so a
// chunk carrying only answer text arrives with an empty reasoning. Taking that
// as the new reasoning erased the fold at exactly the moment the answer landed.
test('an answer chunk with no reasoning keeps the fold', () => {
  const {root, push} = installStreamingPanel();
  open();
  say(root, 'what is this page');
  push('', false, false, 'Check the headings first.');
  assert.ok(root.querySelector('.aithink'), 'setup: the fold must be up');

  push('The page opens with a filmography.', false, false, '');

  const fold = root.querySelector('.aithink');
  assert.ok(fold, 'the answer erased the reasoning the model had already sent');
  assert.equal(fold.querySelector('.aithink-body').textContent,
               'Check the headings first.');
  h.reset();
});

// The fold body is its own scroller, capped at 220px. The shim has
// no layout, so the growth is modelled: writing the next chunk of reasoning is
// what pushes the bottom down, which is why the position has to be read first.
function stageOverflowingFold(root, {scrollTop = 380} = {}) {
  const body = root.querySelector('.aithink-body');
  body.scrollHeight = 600;
  body.clientHeight = 220;
  body.scrollTop = scrollTop;
  const text = Object.getOwnPropertyDescriptor(
      Object.getPrototypeOf(body), 'textContent');
  Object.defineProperty(body, 'textContent', {
    configurable: true,
    get: () => text.get.call(body),
    set: (value) => {
      text.set.call(body, value);
      body.scrollHeight = 900;
    },
  });
  return body;
}

test('the fold follows the reasoning as it streams', () => {
  const {root, push} = installStreamingPanel();
  open();
  say(root, 'what is this page');
  push('', false, false, 'Check the headings first.');
  const body = stageOverflowingFold(root);

  push('', false, false, 'Check the headings first. Then the infobox.');

  assert.equal(body.scrollTop, 900,
               'the reader had to scroll down by hand to follow the thinking');
  h.reset();
});

test('a reader who scrolled up inside the fold is left alone',
     () => {
       const {root, push} = installStreamingPanel();
       open();
       say(root, 'what is this page');
       push('', false, false, 'Check the headings first.');
       const body = stageOverflowingFold(root, {scrollTop: 50});

       push('', false, false, 'Check the headings first. Then the infobox.');

       assert.equal(body.scrollTop, 50,
                    'the next chunk yanked the reader back to the bottom');
       h.reset();
     });

// From the other end: a stream that dies mid-answer is finished
// work, and a loader left spinning over it says the opposite.
test('a failed stream takes the loader down', () => {
  const {root, push} = installStreamingPanel();
  open();
  say(root, 'what is this page');
  push('The page opens with', false);
  push('Ollama stopped responding.', true, true);

  assert.equal(root.querySelector('.aistatus .ailoader'), null);
  h.reset();
});

test('a chat stream that fails keeps its text and says why',
     () => {
       const {root, push} = installStreamingPanel();
       open();
       say(root, 'what is this page');
       push('The page opens with', false);
       push('Ollama stopped responding.', true, true);

       assert.equal(
           root.querySelector('.aichatlog .aibubble-ai').textContent,
           'The page opens with',
           'the half answer already read must stay');
       assert.equal(root.querySelector('.aichatlog .aierrnote').textContent,
                    'Ollama stopped responding.');
       h.reset();
     });

// The on-device path never streams: one call, done, and the bubble it
// makes has to be the same one the streamed path ends on.
test('a single non-streamed answer still renders once', () => {
  const {root, push} = installStreamingPanel();
  open();
  say(root, 'what is this page');
  push('the whole answer at once');

  assert.equal(root.querySelectorAll('.aichatlog .aibubble-ai').length, 1);
  assert.equal(root.querySelector('.aichatlog .aibubble-ai').textContent,
               'the whole answer at once');
  assert.equal(root.querySelector('.aistatus .ailoader'), null);
  h.reset();
});

// The chat thread folds the same way the summary pane does - the
// reply is a model reply either way.
test('a chat answer with reasoning folds it behind the answer',
     () => {
       const {root, push} = installStreamingPanel();
       open();
       say(root, 'who directed it');
       push('Frank Oz.', false, false, 'The user wants the director.');

       const bubble = root.querySelector('.aichatlog .aibubble-ai');
       assert.equal(bubble.querySelector('.aibubbletext').textContent,
                    'Frank Oz.');
       const fold = bubble.querySelector('.aithink');
       assert.equal(fold.open, false);
       assert.equal(fold.querySelector('.aithink-body').textContent,
                    'The user wants the director.');
       h.reset();
     });

test('a chat answer with no reasoning grows no disclosure',
     () => {
       const {root, push} = installStreamingPanel();
       open();
       say(root, 'who directed it');
       push('Frank Oz.');

       assert.equal(root.querySelector('.aichatlog .aithink'), null);
       h.reset();
     });

// The pinned thread is one conversation across pages, so without a
// mark an answer written for one page sat under a context line naming the next.
const marks = (root) =>
    [...root.querySelectorAll('.aichatlog .aipagemark')].map((m) => m.textContent);

test('a pinned move marks the thread with the new page',
     () => {
       const root = installPerTabPanel();
       window.atriumSetAiPanelTarget(TAB_A);
       open();
       pin(root);
       say(root, 'question on A');

       window.atriumSetAiPanelTarget(TAB_B);
       assert.deepEqual(marks(root), ['Now on: B'],
                        'the move must name the page the thread is now about');
       // the mark comes AFTER the answer it protects, or the old answer
       // still reads as being about the new page.
       const roles = [...root.querySelectorAll('.aichatlog > *')]
                         .map((el) => el.className.split(' ')[0]);
       assert.equal(roles[roles.length - 1], 'aipagemark',
                    'the mark must close the old page, not open the new one');
       assert.ok(roles.includes('aibubble'),
                 'the answer written for A is still in the thread');
       h.reset();
     });

test('a same-tab refresh adds no mark', () => {
  const root = installPerTabPanel();
  window.atriumSetAiPanelTarget(TAB_A);
  open();
  pin(root);
  say(root, 'question on A');

  // Same tab id, new title and space - the refresh path.
  window.atriumSetAiPanelTarget({tabId: TAB_A.tabId, spaceIndex: 3,
                                 pageTitle: 'A renamed'});
  assert.deepEqual(marks(root), [],
                   'the page did not change, so nothing may mark it');
  h.reset();
});

test('an unpinned panel is unchanged', () => {
  const root = installPerTabPanel();
  window.atriumSetAiPanelTarget(TAB_A);
  open();
  say(root, 'question on A');

  window.atriumSetAiPanelTarget(TAB_B);
  window.atriumSetAiPanelTarget(TAB_A);
  assert.deepEqual(marks(root), [],
                   'the unpinned panel swaps threads per tab and needs no mark');
  h.reset();
});

// A mark on an empty thread announces a move away from a page nothing was
// said about, which is the state a pinned panel sits in most of the time.
test('an empty pinned thread takes no mark', () => {
  const root = installPerTabPanel();
  window.atriumSetAiPanelTarget(TAB_A);
  open();
  pin(root);

  window.atriumSetAiPanelTarget(TAB_B);
  assert.deepEqual(marks(root), []);
  h.reset();
});

// The shim has no layout, so a container that overflows is staged
// by hand: 600px of content in a 200px window, scrolled to the bottom.
function stageOverflowingLog(root, {scrollTop = 400} = {}) {
  const chatLog = root.querySelector('.aichatlog');
  chatLog.scrollHeight = 600;
  chatLog.clientHeight = 200;
  chatLog.scrollTop = scrollTop;
  return chatLog;
}

test('an answer scrolls the newest text into view', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {getContext: () => ({pageTitle: null})});
  open();
  const chatLog = stageOverflowingLog(root);

  // The log grows when the answer lands, which is the moment the reader
  // would otherwise be left looking at old text.
  chatLog.scrollHeight = 900;
  say(root, 'a question');

  assert.equal(chatLog.scrollTop, 900,
               'the panel must follow the newest text');
  h.reset();
});

test('scrolling up stops the following and the view stays put',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {getContext: () => ({pageTitle: null})});
       open();
       const chatLog = stageOverflowingLog(root);

       // The reader scrolls back through the answer.
       chatLog.scrollTop = 50;
       chatLog.dispatchEvent(new Event('scroll', {bubbles: false}));

       chatLog.scrollHeight = 900;
       say(root, 'a question');

       assert.equal(chatLog.scrollTop, 50,
                    'a reader who scrolled up must not be yanked to the bottom');
       h.reset();
     });

test('returning to the bottom resumes the following', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {getContext: () => ({pageTitle: null})});
  open();
  const chatLog = stageOverflowingLog(root);

  chatLog.scrollTop = 50;
  chatLog.dispatchEvent(new Event('scroll', {bubbles: false}));
  // Back to the bottom: 600 - 400 - 200 is 0, within the slack.
  chatLog.scrollTop = 400;
  chatLog.dispatchEvent(new Event('scroll', {bubbles: false}));

  chatLog.scrollHeight = 900;
  say(root, 'a question');

  assert.equal(chatLog.scrollTop, 900,
               'coming back to the newest text must re-arm the following');
  h.reset();
});

// Installs a spy on chat so the pages the question carries can
// be read off the context, which is the only place the cap is observable.
function installChatSpy() {
  const root = document.createElement('div');
  document.body.appendChild(root);
  const seen = [];
  installAiPanel(root, {
    backend: {
      summarize: STUB_AI_BACKEND.summarize,
      chat: (ctx, query, respond) => {
        seen.push(ctx);
        return STUB_AI_BACKEND.chat(ctx, query, respond);
      },
    },
    getContext: () => ({pageTitle: null}),
  });
  return {root, seen};
}

test('a pinned thread carries the last pages only, oldest dropped',
     () => {
       const {root, seen} = installChatSpy();
       window.atriumSetAiPanelTarget({tabId: '1', spaceIndex: 0, pageTitle: '1'});
       open();
       pin(root);
       for (const id of ['2', '3', '4', '5', '6', '7']) {
         window.atriumSetAiPanelTarget({tabId: id, spaceIndex: 0, pageTitle: id});
       }
       root.querySelector('.aichatinput').value = 'q';
       root.querySelector('[data-ai-panel-send]').click();

       // MAX_ROAMING_PAGES is 5 and counts the page on screen, so four travel
       // with it. Tabs 1 and 2 are the ones that must have fallen off.
       assert.deepEqual(seen.at(-1).extraTabIds, ['3', '4', '5', '6'],
                        'the oldest pages must drop, not the newest');
       assert.equal(seen.at(-1).tabId, '7',
                    'the page on screen must not be repeated in the carried list');
       h.reset();
     });

test('revisiting a page moves it to newest instead of listing it twice',
     () => {
       const {root, seen} = installChatSpy();
       window.atriumSetAiPanelTarget({tabId: '1', spaceIndex: 0, pageTitle: '1'});
       open();
       pin(root);
       for (const id of ['2', '3', '1', '4']) {
         window.atriumSetAiPanelTarget({tabId: id, spaceIndex: 0, pageTitle: id});
       }
       root.querySelector('.aichatinput').value = 'q';
       root.querySelector('[data-ai-panel-send]').click();

       // Without the dedupe, '1' appears twice and eats a slot the cap could
       // have spent on a page the user has not already given the model.
       assert.deepEqual(seen.at(-1).extraTabIds, ['2', '3', '1']);
       h.reset();
     });

test('a closed tab drops its panel state', () => {
  const root = installPerTabPanel();
  window.atriumSetAiPanelTarget(TAB_A);
  open();
  say(root, 'question on A');
  window.atriumSetAiPanelTarget(TAB_B);

  window.atriumForgetAiPanelTab(TAB_A.tabId);
  window.atriumSetAiPanelTarget(TAB_A);
  assert.equal(root.classList.contains('aipopen'), false,
               'a forgotten tab must not reopen the panel');
  assert.equal(root.querySelectorAll('.aichatlog .aibubble').length, 0,
               'a forgotten tab must not keep its chat');
  h.reset();
});

test('closing the attached tab before the next target push leaves no stale chat',
     () => {
       const root = installPerTabPanel();
       window.atriumSetAiPanelTarget(TAB_A);
       open();
       say(root, 'question on A');

       window.atriumForgetAiPanelTab(TAB_A.tabId);
       assert.equal(root.classList.contains('aipopen'), false,
                    'closing the tab under an unpinned panel must hide it');
       assert.equal(root.querySelectorAll('.aichatlog .aibubble').length, 0);

       window.atriumSetAiPanelTarget(TAB_B);
       window.atriumSetAiPanelTarget(TAB_A);
       assert.equal(root.classList.contains('aipopen'), false,
                    'the closed tab\'s state must not come back from a late stash');
       assert.equal(root.querySelectorAll('.aichatlog .aibubble').length, 0);
       h.reset();
     });

test('repeat attach does not stack a second panel or duplicate a keydown handler',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {getContext: () => ({pageTitle: null})});
       installAiPanel(root, {getContext: () => ({pageTitle: null})});

       assert.equal(root.querySelectorAll('.aipanel').length, 1,
                    'a repeat attach must not orphan a second panel subtree');
       h.sent.length = 0;
       open();
       assert.equal(
           h.sent.filter((m) => m[0] === 'aiPanelToggled').length, 1,
           'a duplicated keydown listener would toggle open/close twice');
       h.reset();
     });

// --- teardown/rebuild contract --------------------------------

test('two kill-switch off/on cycles leave exactly one click listener on root - one send invokes sendChat exactly once',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {getContext: () => ({pageTitle: 'Doc'})});

       window.atriumSetAiPanelEnabled(false);
       window.atriumSetAiPanelEnabled(true);
       window.atriumSetAiPanelEnabled(false);
       window.atriumSetAiPanelEnabled(true);

       open();
       const panel = root.querySelector('.aipanel');
       // A bubble/network-call count would stay at 1 no matter how many
       // listeners leaked, because the first invocation clears the input
       // before the rest run - counting query reads survives that masking.
       let sendChatEntries = 0;
       const realQuerySelector = panel.querySelector.bind(panel);
       panel.querySelector = (selector) => {
         if (selector === '.aichatinput') {
           sendChatEntries++;
         }
         return realQuerySelector(selector);
       };

       const input = panel.querySelector('.aichatinput');
       sendChatEntries = 0;
       input.value = 'hi';
       root.querySelector('[data-ai-panel-send]').click();

       assert.equal(
           sendChatEntries, 1,
           'a listener leaked from a prior enable re-runs sendChat once per leftover listener');
       h.reset();
     });

test('the pin button reflects the real pinned state after a kill-switch rebuild',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {getContext: () => ({pageTitle: 'Doc'})});

       open();
       pin(root);
       assert.equal(root.querySelector('.aippin').classList.contains('on'),
                    true);

       window.atriumSetAiPanelEnabled(false);
       window.atriumSetAiPanelEnabled(true);

       assert.equal(
           root.querySelector('.aippin').classList.contains('on'), true,
           'the rebuilt pin button must show the surviving pinned state, not reset to unpinned');
       h.reset();
     });

test('per-space memory is capped and evicts the oldest entry', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  const panel = installAiPanel(root, {getContext: () => ({pageTitle: 'Doc'})});

  window.atriumSetAiPanelTarget({tabId: '1', spaceIndex: 7, pageTitle: 'Doc'});
  open();
  pin(root);
  const input = root.querySelector('.aichatinput');
  for (let i = 0; i < 210; i++) {
    input.value = `msg${i}`;
    root.querySelector('[data-ai-panel-send]').click();
  }

  const entries = panel.getMemory(7);
  assert.ok(entries.length <= 200,
             `per-space memory must be bounded, saw ${entries.length}`);
  assert.ok(entries.every((e) => e.text !== 'msg0'),
             'the oldest entry must be evicted once the cap is reached');
  h.reset();
});

// --- kill switch and per-space memory -------------------------

test('kill switch off leaves no panel node and no summarize/chat controls',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {getContext: () => ({pageTitle: 'Doc'})});

       window.atriumSetAiPanelEnabled(false);

       assert.equal(root.querySelector('.aipanel'), null,
                    'no panel node may exist while AI is off');
       assert.equal(root.querySelectorAll('[data-ai-panel-send]').length, 0);
       assert.equal(root.querySelectorAll('.aichatinput').length, 0);
       h.reset();
     });

test('kill switch off - backtick no-ops, no listener or timer survives',
     async () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       const seen = [];
       installAiPanel(root, {
         backend: {
           summarize: (ctx) => (seen.push('summarize'), 'x'),
           chat: (ctx, query, respond) => (seen.push('chat'), () => {}),
         },
         getContext: () => ({pageTitle: 'Doc'}),
       });

       window.atriumSetAiPanelEnabled(false);
       assert.equal(document.body._listeners.get('keydown')?.length ?? 0, 0,
                    'no keydown listener may remain while AI is off');

       h.sent.length = 0;
       h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
       assert.deepEqual(h.sent, [],
                         'backtick must do nothing while the kill switch is on');
       assert.equal(root.querySelector('.aipanel'), null);

       await new Promise((resolve) => setTimeout(resolve, 500));
       assert.deepEqual(seen, [], 'no summarize/chat/timer may run while AI is off');
       h.reset();
     });

test('summarizing writes a space-scoped memory entry', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  const panel = installAiPanel(root, {getContext: () => ({pageTitle: 'Doc'})});

  window.atriumSetAiPanelTarget({tabId: '1', spaceIndex: 3, pageTitle: 'Doc'});
  open();  // backtick opens and summarizes

  const entries = panel.getMemory(3);
  assert.equal(entries.length, 1);
  assert.equal(entries[0].kind, 'summary');
  h.reset();
});

test('chatting writes a space-scoped memory entry distinct from other spaces',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       const panel =
           installAiPanel(root, {getContext: () => ({pageTitle: 'Doc'})});

       window.atriumSetAiPanelTarget(
           {tabId: '1', spaceIndex: 0, pageTitle: 'A'});
       open();
       pin(root);
       const input = root.querySelector('.aichatinput');
       input.value = 'hello';
       root.querySelector('[data-ai-panel-send]').click();

       window.atriumSetAiPanelTarget(
           {tabId: '2', spaceIndex: 1, pageTitle: 'B'});

       const space0 = panel.getMemory(0);
       const space1 = panel.getMemory(1);
       assert.ok(space0.some((e) => e.kind === 'chat' && e.text === 'hello'),
                  'the chat must be recorded under the space it happened in');
       assert.equal(space1.length, 0,
                    'a different space must not see another space\'s memory');
       h.reset();
     });

// A privacy control clears the data it governs, so the kill switch wipes
// memory instead of preserving it across an off cycle.
test('disabling clears the per-space memory the kill switch governs',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       const panel =
           installAiPanel(root, {getContext: () => ({pageTitle: 'Doc'})});

       window.atriumSetAiPanelTarget(
           {tabId: '1', spaceIndex: 5, pageTitle: 'Doc'});
       open();
       assert.equal(panel.getMemory(5).length, 1);

       window.atriumSetAiPanelEnabled(false);
       assert.equal(panel.getMemory(5).length, 0,
                    'the kill switch must clear the memory it governs');

       h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
       assert.equal(panel.getMemory(5).length, 0,
                    'no new entry may be recorded while AI is off');
       h.reset();
     });

// the loader's label wears the panel's own ink token, and every
// moving part of the mark stops under prefers-reduced-motion, matching every
// other animated indicator in the app (sidebar.css's .loadfill.waiting).
// The birds themselves carry the four brand tones, not var(--fg) - a flock in
// one flat ink is not the mark.
test('the AI loader reads the panel theme tokens and stops every part ' +
     'of its animation under prefers-reduced-motion',
     () => {
       const css = fs.readFileSync(
           path.join(ATRIUM_DIR, 'ai-panel.css'), 'utf8');
       assert.match(css.match(/\.ailoader\s*{[^}]*}/)[0], /var\(--fg\)/);
       const reduced = css.match(
           /@media \(prefers-reduced-motion: reduce\)\s*{\s*\.ailoader[^{]*{[^}]*}/);
       assert.ok(reduced, 'no reduced-motion override for the loader mark');
       for (const part of ['ov-flock-spin', 'ov-flock-push', 'ov-flock-b']) {
         assert.ok(reduced[0].includes(part), `${part} still animates`);
       }
       assert.match(reduced[0], /animation:\s*none/);
     });

// The shove and the dim are one gesture split across two elements, so they run
// off the same beat. Give them separate durations and a bird drifts out of its
// own dark window, which looks like the mark coming apart rather than breathing.
test('the loader shove and dim share one beat', () => {
  const css = fs.readFileSync(path.join(ATRIUM_DIR, 'ai-panel.css'), 'utf8');
  for (const name of ['aiflockpush', 'aiflockb1']) {
    assert.match(
        css, new RegExp(`animation: ${name} var\\(--flock-beat\\)`),
        `${name} does not run off --flock-beat`);
  }
  // The crest of the shove sits after the dim has let go, which is the whole
  // point of the motion: a bird lights up while it is still travelling out.
  const crest = Number(css.match(/@keyframes aiflockpush[\s\S]*?\n}/)[0]
                           .match(/(\d+)% {\s*\n?\s*transform: translate\(calc/)[1]);
  const lit = Number(css.match(/@keyframes aiflockb1[\s\S]*?\n}/)[0]
                         .match(/\d+%, (\d+)%/)[1]);
  assert.ok(crest > lit, `shove crests at ${crest}%, dim lets go at ${lit}%`);
});

// At 18px beside a 12px label the mark read as a smudge, which is
// half of why it moved out of the log. The empty strip has to collapse, or
// sending a question pushes the prompt down under the cursor that just clicked.
test('the loader mark reads, and the strip is free at rest',
     () => {
       const css = fs.readFileSync(
           path.join(ATRIUM_DIR, 'ai-panel.css'), 'utf8');
       const size = Number(
           css.match(/\.ailoader \.ov-flock\s*{[^}]*width: (\d+)px/)[1]);
       assert.ok(size >= 28, `the loader mark is only ${size}px`);
       assert.match(css, /\.aistatus:empty\s*{\s*display: none;\s*}/);
     });

// The glide must not be a whole multiple of the beat. When it is, the front and
// the turn line up every few seconds and the eye reads the repeat as a stutter.
test('the loader glide runs a whole revolution off the front', () => {
  const css = fs.readFileSync(path.join(ATRIUM_DIR, 'ai-panel.css'), 'utf8');
  const turn = Number(css.match(/animation: aiflockturn ([\d.]+)s/)[1]);
  const beat = Number(css.match(/--flock-beat: ([\d.]+)s/)[1]);
  assert.notEqual(turn % beat, 0, `glide ${turn}s is a whole ${beat}s beats`);
  assert.match(
      css.match(/@keyframes aiflockturn[\s\S]*?\n}/)[0], /rotate\(-360deg\)/);
});

// --- Auto summarize setting -------------------------------------

test('auto summarize defaults on - opening the panel starts a summary',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       const seen = [];
       installAiPanel(root, {
         backend: {
           summarize: (ctx) => (seen.push('summarize'), 'sum'),
           chat: STUB_AI_BACKEND.chat,
         },
         getContext: () => ({pageTitle: 'Doc'}),
       });

       open();
       assert.deepEqual(seen, ['summarize']);
       assert.equal(root.querySelector('.aisum p').textContent, 'sum');
       h.reset();
     });

test('auto summarize off - opening the panel calls the backend for ' +
     'neither summarize nor chat, and leaves the summary empty',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       const seen = [];
       installAiPanel(root, {
         backend: {
           summarize: (ctx) => (seen.push('summarize'), 'sum'),
           chat: (ctx, query, respond) => (seen.push('chat'), () => {}),
         },
         getContext: () => ({pageTitle: 'Doc'}),
       });

       window.atriumSetAiPanelAutoSummarize(false);
       open();

       assert.deepEqual(seen, [], 'no model request while auto summarize is off');
       assert.equal(root.querySelector('.aisum').children.length, 0,
                    'the conversation must open empty, not mid-render');
       h.reset();
     });

test('auto summarize off still opens the panel with the prompt ready',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {getContext: () => ({pageTitle: 'Doc'})});

       window.atriumSetAiPanelAutoSummarize(false);
       open();

       assert.equal(root.classList.contains('aipopen'), true);
       assert.equal(document.activeElement,
                    root.querySelector('.aichatinput'),
                    'the prompt must still be ready for the user to type');
       h.reset();
     });

test('flipping auto summarize back on takes effect on the next open, ' +
     'no restart needed',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       const seen = [];
       installAiPanel(root, {
         backend: {
           summarize: (ctx) => (seen.push('summarize'), 'sum'),
           chat: STUB_AI_BACKEND.chat,
         },
         getContext: () => ({pageTitle: 'Doc'}),
       });

       window.atriumSetAiPanelAutoSummarize(false);
       open();
       assert.deepEqual(seen, []);
       root.querySelector('[data-ai-panel-close]').click();

       window.atriumSetAiPanelAutoSummarize(true);
       open();
       assert.deepEqual(seen, ['summarize']);
       h.reset();
     });

// --- the quick actions and the language picker -------

function quickPanel(overrides = {}) {
  const root = document.createElement('div');
  document.body.appendChild(root);
  const asked = [];
  installAiPanel(root, {
    backend: {
      summarize: () => 'sum',
      chat: (ctx, query, respond) => {
        asked.push(query);
        return () => {};
      },
    },
    getContext: () => ({pageTitle: 'Doc', tabId: '7'}),
    ...overrides,
  });
  open();
  return {root, asked};
}

const quickButton = (root, id) =>
    root.querySelector(`[data-ai-panel-quick="${id}"]`);

const languageSelect = (root) => root.querySelector('[data-ai-panel-language]');

test('the panel offers the two actions above the prompt', () => {
  const {root} = quickPanel();
  const row = root.querySelector('.aiquick');
  assert.ok(row, 'no quick action row');
  // the label moved to the hover hint so the picker fits the line.
  assert.deepEqual(
      [...row.querySelectorAll('.aiquickbtn')].map((b) => b.textContent), ['', '']);
  assert.deepEqual(
      [...row.querySelectorAll('.aiquickbtn')].map(
          (b) => b.getAttribute('aria-label')),
      ['Summarize', 'Bullet points']);
  assert.equal(quickButton(root, 'translate'), null,
               'the language picker replaced the translate action');
  // .ov-btn is the 26x26 icon square every other glyph button in the app wears.
  assert.equal(row.querySelectorAll('.ov-btn').length, 2);
  assert.equal(row.querySelector('.ov-button'), null,
               'the label variant would leave a gap where the text was');
  // A row below the prompt would read as a footer, not as an action bar.
  assert.equal(row.nextElementSibling, root.querySelector('.aichatrow'));
  // the thinking strip sits above the actions, under the log.
  assert.equal(root.querySelector('.aistatus').nextElementSibling, row);
  assert.equal(root.querySelector('.aistatus').previousElementSibling,
               root.querySelector('.aichatlog'));
  h.reset();
});

test('each action wears its own glyph', () => {
  const {root} = quickPanel();
  for (const id of ['summarize', 'bullets']) {
    const svg = quickButton(root, id).querySelector('svg.ov-icon');
    assert.ok(svg, `${id} has no glyph`);
    // A mark the fallback produced would be the link arrow on both buttons,
    // which tells the reader nothing about which is which.
    assert.notEqual(svg.dataset.icon, 'url', `${id} fell back to the url mark`);
  }
  h.reset();
});

// --- one line, so the labels move to the hint ---------------------

test('hovering a glyph raises the shared hint carrying its label',
     () => {
       const clock = makeFakeClock();
       const {root} = quickPanel({clock});
       quickButton(root, 'bullets')
           .dispatchEvent(new Event('pointerover', {bubbles: true}));
       clock.advance(600);
       assert.equal(hintText(), 'Bullet points');
       hideHint();
       h.reset();
     });

test('picking a language renames the hint, which is the only ' +
     'place the name still appears',
     () => {
       const clock = makeFakeClock();
       const {root} = quickPanel({clock});
       window.atriumSetAiPanelTranslateLanguage('hu');
       const select = languageSelect(root);
       select.value = 'fr';
       select.dispatchEvent(new Event('change', {bubbles: true}));
       select.dispatchEvent(new Event('pointerover', {bubbles: true}));
       clock.advance(600);
       assert.equal(hintText(), 'Summary language: French');
       hideHint();
       h.reset();
     });

test('the picker shows a code, and its hint names the language',
     () => {
       const clock = makeFakeClock();
       const {root} = quickPanel({clock});
       window.atriumSetAiPanelTranslateLanguage('hu');
       const select = languageSelect(root);
       assert.deepEqual(
           [...select.querySelectorAll('option')].map((o) => o.textContent)
               .slice(0, 4),
           ['EN-US', 'EN', 'ES', 'FR']);
       select.dispatchEvent(new Event('pointerover', {bubbles: true}));
       clock.advance(600);
       assert.equal(hintText(), 'Summary language: Hungarian');
       hideHint();
       h.reset();
     });

test('each button sends one request for the page, and it lands in the ' +
     'conversation',
     () => {
       for (const [id, prompt] of [
                ['summarize', 'Summarize this page in Hungarian.'],
                ['bullets',
                 'Summarize this page as bullet points in Hungarian.'],
           ]) {
         const {root, asked} = quickPanel();
         window.atriumSetAiPanelTranslateLanguage('hu');
         quickButton(root, id).click();
         assert.deepEqual(asked, [prompt]);
         assert.equal(
             root.querySelectorAll('.aichatlog .aibubble-me').length, 1,
             `${id} did not put its request in the conversation`);
         assert.equal(root.querySelectorAll('.aistatus .ailoader').length,
                      1, `${id} left the panel with nothing to show`);
         h.reset();
       }
     });

test('the picker sits in the action row and starts on the settings ' +
     'language',
     () => {
       const {root} = quickPanel();
       window.atriumSetAiPanelTranslateLanguage('hu');
       const select = languageSelect(root);
       assert.ok(select, 'no language picker');
       assert.equal(select.closest('.aiquick'), root.querySelector('.aiquick'));
       assert.equal(select.value, 'hu');
       assert.ok([...select.querySelectorAll('option')].some(
                     (o) => o.value === ''),
                 'the system language must stay pickable');
       h.reset();
     });

test('with no language stored the action follows the panel locale, ' +
     'which is the macOS preferred language',
     () => {
       const {root, asked} = quickPanel();
       window.atriumSetAiPanelTranslateLanguage('');
       quickButton(root, 'summarize').click();
       // Spelt out, not recomputed: running the implementation's own formula
       // here would pass whatever that formula produced.
       assert.equal(navigator.language, 'en-US');
       assert.deepEqual(asked, ['Summarize this page in American English.']);
       h.reset();
     });

test('a settings change applies to the next action with no reopen', () => {
  const {root, asked} = quickPanel();
  window.atriumSetAiPanelTranslateLanguage('hu');
  quickButton(root, 'summarize').click();
  window.atriumSetAiPanelTranslateLanguage('fr');
  quickButton(root, 'summarize').click();
  assert.deepEqual(asked, [
    'Summarize this page in Hungarian.',
    'Summarize this page in French.',
  ]);
  h.reset();
});

test('picking a language in the panel answers both actions in it, ' +
     'and sends nothing to settings',
     () => {
       const {root, asked} = quickPanel();
       window.atriumSetAiPanelTranslateLanguage('hu');
       const select = languageSelect(root);
       select.value = 'fr';
       h.sent.length = 0;
       select.dispatchEvent(new Event('change', {bubbles: true}));
       // On the fly means for this thread: any message here would be the
       // panel rewriting the setting it only borrowed a starting value from.
       assert.deepEqual(h.sent, [], 'the picker must not write the setting');
       quickButton(root, 'summarize').click();
       quickButton(root, 'bullets').click();
       assert.deepEqual(asked, [
         'Summarize this page in French.',
         'Summarize this page as bullet points in French.',
       ]);
       h.reset();
     });

// --- clear conversation -----------------------------------------

test('the header clear button empties the summary and the chat log', () => {
  const {root} = quickPanel();
  quickButton(root, 'summarize').click();
  assert.ok(root.querySelector('.aichatlog .aibubble-me'), 'setup');
  assert.ok(root.querySelector('.aisum').children.length, 'setup');

  root.querySelector('[data-ai-panel-clear]').click();

  assert.equal(root.querySelectorAll('.aichatlog .aibubble').length, 0);
  assert.equal(root.querySelector('.aisum').children.length, 0);
  assert.equal(root.classList.contains('aipopen'), true,
               'clearing the thread must not close the panel');
  h.reset();
});

test('a reply still in flight cannot land in the cleared thread', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  let answer = null;
  installAiPanel(root, {
    backend: {
      summarize: () => 'sum',
      chat: (ctx, query, respond) => {
        answer = respond;
        return () => {};
      },
    },
    getContext: () => ({pageTitle: 'Doc', tabId: '7'}),
  });
  open();
  quickButton(root, 'summarize').click();
  assert.ok(root.querySelector('.aistatus .ailoader'), 'setup');

  root.querySelector('[data-ai-panel-clear]').click();
  answer('late');

  assert.equal(root.querySelectorAll('.aichatlog .aibubble').length, 0,
               'the cancelled reply landed anyway');
  h.reset();
});

test('the clear button is an icon square in the header, ahead of pin ' +
     'and close',
     () => {
       const {root} = quickPanel();
       const clear = root.querySelector('[data-ai-panel-clear]');
       assert.equal(clear.closest('.aiphead'), root.querySelector('.aiphead'));
       assert.equal(clear.classList.contains('ov-btn'), true);
       assert.ok(clear.querySelector('svg.ov-icon'), 'no glyph');
       assert.equal(clear.getAttribute('aria-label'), 'Clear conversation');
       // Close is the rightmost of the three, as it is on every other surface.
       assert.equal(clear.nextElementSibling,
                    root.querySelector('[data-ai-panel-pin]'));
       h.reset();
     });

test('clearing a pinned thread drops the pages it had travelled', () => {
  const {root, seen} = installChatSpy();
  window.atriumSetAiPanelTarget({tabId: '1', spaceIndex: 0, pageTitle: '1'});
  open();
  pin(root);
  for (const id of ['2', '3']) {
    window.atriumSetAiPanelTarget({tabId: id, spaceIndex: 0, pageTitle: id});
  }
  root.querySelector('[data-ai-panel-clear]').click();
  quickButton(root, 'summarize').click();

  // An emptied thread that still names the pages behind it asks the model
  // about reading the user just threw away, and they cannot see that it does.
  assert.deepEqual(seen.at(-1).extraTabIds, []);
  assert.equal(seen.at(-1).tabId, '3');
  h.reset();
});

test('every window.atrium receiver the panel adds is registered as a ' +
     'surface push',
     () => {
       const source = fs.readFileSync(
           path.join(ATRIUM_DIR, 'ai-panel.js'), 'utf8');
       assert.match(source, /window\.atriumSetAiPanelTranslateLanguage/);
     });

// Per-tab memory reopens a panel the user never asked for in the
// space they just arrived in. Leaving the space is a stronger signal than
// "this tab had it open once", so the memory loses.
test('unpinned - a space change closes the panel even on a tab that ' +
     'remembers it open',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {getContext: () => ({pageTitle: null})});

       window.atriumSetAiPanelTarget({tabId: '2', spaceIndex: 1, pageTitle: 'B'});
       open();
       assert.equal(root.classList.contains('aipopen'), true, 'setup');

       window.atriumSetAiPanelTarget({tabId: '1', spaceIndex: 0, pageTitle: 'A'});
       open();
       assert.equal(root.classList.contains('aipopen'), true, 'setup');

       window.atriumSetAiPanelTarget({tabId: '2', spaceIndex: 1, pageTitle: 'B'});
       assert.equal(root.classList.contains('aipopen'), false,
                    'the space change must close it, memory or not');
       h.reset();
     });

test('changing back does not bring a closed panel back on its own', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {getContext: () => ({pageTitle: null})});

  window.atriumSetAiPanelTarget({tabId: '1', spaceIndex: 0, pageTitle: 'A'});
  open();
  window.atriumSetAiPanelTarget({tabId: '2', spaceIndex: 1, pageTitle: 'B'});
  assert.equal(root.classList.contains('aipopen'), false, 'setup');

  window.atriumSetAiPanelTarget({tabId: '1', spaceIndex: 0, pageTitle: 'A'});
  assert.equal(root.classList.contains('aipopen'), false,
               'coming back must not reopen it by itself');
  h.reset();
});

// A tab switch inside one space keeps the memory it always had - only the
// space change overrides it.
test('a tab switch inside one space still restores what that tab had',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {getContext: () => ({pageTitle: null})});

       window.atriumSetAiPanelTarget({tabId: '1', spaceIndex: 0, pageTitle: 'A'});
       open();
       window.atriumSetAiPanelTarget({tabId: '2', spaceIndex: 0, pageTitle: 'B'});
       assert.equal(root.classList.contains('aipopen'), false, 'setup');

       window.atriumSetAiPanelTarget({tabId: '1', spaceIndex: 0, pageTitle: 'A'});
       assert.equal(root.classList.contains('aipopen'), true,
                    'within one space the tab keeps its own panel state');
       h.reset();
     });

// The sidebar is a different WebContents, so the panel cannot mark
// its own row - it reports the tab and the browser pushes it across.
const lastMark = () => h.sent.filter((m) => m[0] === 'aiPanelRowMark').pop();

function panelOnTab(tabId) {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {getContext: () => ({pageTitle: 'Doc'})});
  window.atriumSetAiPanelTarget({tabId, spaceIndex: 0, pageTitle: 'Doc'});
  open();
  return root;
}

test('the panel reports the tab it is open for', () => {
  panelOnTab('7');
  assert.deepEqual(lastMark(), ['aiPanelRowMark', ['7']]);
  h.reset();
});

test('closing the panel reports no row', () => {
  const root = panelOnTab('7');
  root.querySelector('[data-ai-panel-close]').click();
  assert.deepEqual(lastMark(), ['aiPanelRowMark', ['']]);
  h.reset();
});

test('leaving the tab reports no row, since the panel left with it', () => {
  panelOnTab('7');
  window.atriumSetAiPanelTarget({tabId: '8', spaceIndex: 0, pageTitle: 'Two'});
  assert.deepEqual(lastMark(), ['aiPanelRowMark', ['']]);
  h.reset();
});

test('the mark follows a move between two tabs that both had it open', () => {
  const root = panelOnTab('7');
  window.atriumSetAiPanelTarget({tabId: '8', spaceIndex: 0, pageTitle: 'Two'});
  open();
  assert.deepEqual(lastMark(), ['aiPanelRowMark', ['8']], 'setup');

  // The panel never closes across this move, so nothing in setOpen fires.
  window.atriumSetAiPanelTarget({tabId: '7', spaceIndex: 0, pageTitle: 'Doc'});
  assert.equal(root.classList.contains('aipopen'), true, 'setup: still open');
  assert.deepEqual(lastMark(), ['aiPanelRowMark', ['7']]);
  h.reset();
});

test('a pinned panel marks no row', () => {
  const root = panelOnTab('7');
  pin(root);
  assert.deepEqual(lastMark(), ['aiPanelRowMark', ['']],
                   'a pinned thread belongs to no single tab');
  h.reset();
});

test('unpinning marks the tab the panel is back on', () => {
  const root = panelOnTab('7');
  pin(root);
  pin(root);
  assert.deepEqual(lastMark(), ['aiPanelRowMark', ['7']]);
  h.reset();
});

test('atriumCloseAiPanel clears the sidebar row mark', () => {
  panelOnTab('7');
  assert.deepEqual(lastMark(), ['aiPanelRowMark', ['7']], 'setup');
  window.atriumCloseAiPanel();
  assert.deepEqual(lastMark(), ['aiPanelRowMark', ['']]);
  h.reset();
});

test('atriumCloseAiPanel closes an open panel and reports it once', () => {
  const root = openPanel();
  assert.equal(root.classList.contains('aipopen'), true, 'setup: the panel did not open');
  h.sent.length = 0;

  window.atriumCloseAiPanel();
  window.atriumCloseAiPanel();

  assert.equal(root.classList.contains('aipopen'), false);
  assert.deepEqual(h.sent.filter((m) => m[0] === 'aiPanelToggled'),
                   [['aiPanelToggled', [false]]],
                   'the second call on a closed panel must send nothing');
  h.reset();
});
