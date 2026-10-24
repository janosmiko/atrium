#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the /devbar route - structure, the url push, and the handshake.
//
//   node --test build/scripts/devbar-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event, makeDataTransfer} from './dom-shim.mjs';

const h = installDOM({pathname: '/devbar'});

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {buildDevBar, installDevBar} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/devbar.js'));
const {currentHint, hideHint} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/hint.js'));
const {makeFakeClock} = await import('./fake-clock.mjs');

function fresh() {
  h.reset();
  const root = document.createElement('div');
  h.body.appendChild(root);
  installDevBar(root);
  return root;
}

// The hover-hint delay is real time by default (installHover's own
// REAL_TIMEOUT_CLOCK); a fake clock lets the hint tests below skip the wait.
function freshWithClock() {
  h.reset();
  const root = document.createElement('div');
  h.body.appendChild(root);
  const clock = makeFakeClock();
  installDevBar(root, {clock});
  return {root, clock};
}

test('buildDevBar renders a lock, an extension slot, the url span, and copy', () => {
  const bar = buildDevBar();
  assert.equal(bar.className, 'devbar ov-card');
  assert.ok(bar.querySelector('.dvlock svg'));
  assert.ok(bar.querySelector('.dvexts'));
  assert.ok(bar.querySelector('.dvurl'));
  assert.equal(bar.querySelector('.dvcopy').title, 'Copy URL');
});

test('installDevBar announces itself once mounted', () => {
  h.reset();
  const root = document.createElement('div');
  h.body.appendChild(root);
  installDevBar(root);
  assert.deepEqual(h.sent, [['devBarReady', []]]);
});

test('the copy control hands the url to the window, and nothing when empty', () => {
  // navigator.clipboard refuses an unfocused WebUI, so the
  // window writes the clipboard.
  const root = fresh();
  h.sent.length = 0;
  root.querySelector('.dvcopy').click();
  assert.deepEqual(h.sent, []);
  window.atriumSetDevBarUrl('https://example.com/path');
  root.querySelector('.dvcopy').click();
  assert.deepEqual(h.sent, [['devBarCopyUrl', ['https://example.com/path']]]);
});

test('atriumSetDevBarAway slides the bar out and back', () => {
  const root = fresh();
  const bar = root.querySelector('.devbar');
  window.atriumSetDevBarAway(true);
  assert.ok(bar.classList.contains('away'));
  window.atriumSetDevBarAway(false);
  assert.ok(!bar.classList.contains('away'));
});

test('the shadow push carries the away slide, so the native shadow can leave with the bar', () => {
  // The shadow is painted outside this document, from a rect that never moves
  // when .away slides the bar. Without the slide the window has nothing to
  // move it by, and the shadow stays on a page with no bar.
  const root = fresh();
  const bar = root.querySelector('.devbar');
  bar.style.boxShadow = 'rgba(34, 16, 22, 0.3) 0px 5px 14px';
  bar.style.borderTopLeftRadius = '16px';
  bar.style.setProperty('--dv-away-shift', '-60px');
  bar.style.transitionDuration = '0.2s, 0.2s';
  h.sent.length = 0;
  window.__atriumPushOverlayShadowSpec();
  assert.deepEqual(h.sent, [[
    'devBarShadowSpec', [0, 5, 14, 16, 34, 16, 22, 0.3, -60, 200],
  ]]);
});

test('atriumSetDevBarUrl fills the url span, empty until pushed', () => {
  const root = fresh();
  const urlSpan = root.querySelector('.dvurl');
  assert.equal(urlSpan.textContent, '');
  window.atriumSetDevBarUrl('https://example.com/path');
  assert.equal(urlSpan.textContent, 'https://example.com/path');
});

test('a second push replaces the first rather than appending', () => {
  fresh();
  window.atriumSetDevBarUrl('https://one.example/');
  window.atriumSetDevBarUrl('https://two.example/');
  assert.equal(document.querySelector('.dvurl').textContent, 'https://two.example/');
});

// --- the focus ring -------------------------------------------

test('atriumSetDevBarFocused(true) adds .focused and sets --dv-ring', () => {
  const root = fresh();
  const bar = root.querySelector('.devbar');
  assert.equal(bar.classList.contains('focused'), false);
  window.atriumSetDevBarFocused(true, '#b03a48eb');
  assert.equal(bar.classList.contains('focused'), true);
  assert.equal(bar.style.getPropertyValue('--dv-ring'), '#b03a48eb');
});

test('atriumSetDevBarFocused(false) drops .focused', () => {
  const root = fresh();
  const bar = root.querySelector('.devbar');
  window.atriumSetDevBarFocused(true, '#b03a48eb');
  window.atriumSetDevBarFocused(false, '');
  assert.equal(bar.classList.contains('focused'), false);
});

// --- the two phase load fill ------------------------------------------------

test('the capsule carries a fill layer, first so the text paints over it', () => {
  const bar = buildDevBar();
  assert.equal(bar.childNodes[0].className, 'loadfill');
});

test('atriumLoadPhase drives the capsule through the three phases', () => {
  const root = fresh();
  const fill = root.querySelector('.loadfill');
  window.atriumLoadPhase('7', 'waiting', 0);
  assert.equal(fill.classList.contains('waiting'), true);
  assert.equal(fill.style.width, '100%');
  window.atriumLoadPhase('7', 'receiving', 0.45);
  assert.equal(fill.classList.contains('receiving'), true);
  assert.equal(fill.style.width, '45%');
  window.atriumLoadPhase('7', 'done', 1);
  assert.equal(fill.classList.contains('done'), true);
  assert.equal(fill.style.width, '100%');
});

test('devbar.css inks the fill from the overlay set', () => {
  const css = devbarCss();
  assert.match(css, /\.devbar \.loadfill\s*{[^}]*background:\s*var\(--ov-fg\)/);
  assert.match(css, /\.devbar \.loadfill\.receiving\s*{[^}]*opacity:\s*\.2/);
  assert.match(css,
      /\.devbar \.loadfill\.receiving\s*{[^}]*transition:\s*width \.25s ease-out/);
  assert.match(css, /\.devbar \.loadfill\.done\s*{[^}]*transition:\s*opacity \.26s/);
});

// atrium.html loads every route's sheet into ONE document, so a @keyframes name
// defined in two of them is not two animations - the last sheet wins for every
// surface. sidebar.css and devbar.css each carried loadbreathe, and devbar.css
// loads second, so editing the sidebar's copy would have changed nothing while
// looking like a fix.
const atriumCss = (name) => fs.readFileSync(
    path.join(SRC, `chrome/browser/resources/atrium/${name}`), 'utf8');

test('loadbreathe is defined once, in the shared sheet', () => {
  const sheets = {
    'overlay.css': atriumCss('overlay.css'),
    'devbar.css': atriumCss('devbar.css'),
    'sidebar.css': atriumCss('sidebar.css'),
  };
  const owners = Object.entries(sheets)
      .filter(([, css]) => /@keyframes\s+loadbreathe\b/.test(css))
      .map(([name]) => name);
  assert.deepEqual(owners, ['overlay.css'],
      `loadbreathe must live only in overlay.css, found in: ${owners.join(', ')}`);
});

// The trough is what a glance during the wait usually gets. At .08 it moved the
// address field by 8 levels out of 255, measured against the same patch with the
// fill hidden (101.9 against 93.9).
test('the waiting breath never dips below the readable floor', () => {
  const frames = /@keyframes\s+loadbreathe\s*{([\s\S]*?)\n}/.exec(atriumCss('overlay.css'));
  assert.ok(frames, 'no loadbreathe block in overlay.css');
  const stops = [...frames[1].matchAll(/opacity:\s*([0-9.]+)/g)].map((m) => Number(m[1]));
  assert.equal(stops.length, 2, 'expected a trough and a peak');
  assert.ok(Math.min(...stops) >= 0.14,
      `the trough is ${Math.min(...stops)}, which reads as almost nothing`);
  assert.ok(Math.max(...stops) > Math.min(...stops), 'the breath must actually move');
});

test('the floating bar uses the compact floating-surface radius', () => {
  const bar = /\n\.devbar\s*{([^}]*)}/.exec(devbarCss());
  assert.ok(bar, 'the .devbar rule is gone');
  assert.match(bar[1], /border-radius:\s*var\(--ov-radius-compact\)/);

  const overlay = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/overlay.css'), 'utf8');
  assert.match(overlay, /--ov-radius-compact:\s*10px/);
});

test('the capsule clips the fill to its own rounded box', () => {
  const css = devbarCss();
  const bar = /\n\.devbar\s*{([^}]*)}/.exec(css);
  assert.ok(bar, 'the .devbar rule is gone');
  assert.match(bar[1], /position:\s*relative/);
  assert.match(bar[1], /overflow:\s*hidden/);
});

// --- insecure-origin treatment ---------------------------------

test('a plain-http origin marks the bar insecure with the broken lock and Not secure', () => {
  const root = fresh();
  window.atriumSetDevBarUrl('http://example.com/');
  assert.equal(root.querySelector('.devbar').classList.contains('insecure'), true);
  assert.equal(
      root.querySelector('.dvlock svg').getAttribute('aria-label'),
      'Not secure');
  assert.equal(root.querySelector('.dvwarn').textContent, 'Not secure');
  assert.equal(root.querySelector('.dvurl').textContent, 'http://example.com/');
});

test('https clears the insecure mark and restores the plain lock', () => {
  const root = fresh();
  window.atriumSetDevBarUrl('http://example.com/');
  window.atriumSetDevBarUrl('https://example.com/');
  assert.equal(root.querySelector('.devbar').classList.contains('insecure'), false);
  assert.equal(root.querySelector('.dvlock svg').getAttribute('aria-label'), null);
});

test('http://localhost is treated as secure', () => {
  const root = fresh();
  window.atriumSetDevBarUrl('http://localhost:3000/dashboard');
  assert.equal(root.querySelector('.devbar').classList.contains('insecure'), false);
});

test('a loopback IP literal is treated as secure like every other browser', () => {
  const root = fresh();
  window.atriumSetDevBarUrl('http://127.0.0.1:8080/');
  assert.equal(root.querySelector('.devbar').classList.contains('insecure'), false);
  window.atriumSetDevBarUrl('http://[::1]:8080/');
  assert.equal(root.querySelector('.devbar').classList.contains('insecure'), false);
});

test('a host merely prefixed with localhost is not treated as secure', () => {
  const root = fresh();
  window.atriumSetDevBarUrl('http://localhost.evil.com/');
  assert.equal(root.querySelector('.devbar').classList.contains('insecure'), true);
});

test('devbar.css carries the insecure ground, ink, and ring', () => {
  const css = devbarCss();
  assert.match(css,
      /\.devbar\.insecure\s*{[^}]*background:\s*var\(--ov-danger-paper\)/);
  assert.match(css, /\.devbar\.insecure\s*{[^}]*color:\s*var\(--ov-danger\)/);
  assert.match(css, /\.devbar\.insecure\s*{[^}]*border-color:[^;]*--ov-danger/);
});

// The warning must not follow the space tint, or a green space turns it green.
test('the danger pair is defined outside the accent, in both schemes', () => {
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/overlay.css'), 'utf8');
  const light = css.slice(css.indexOf(':root {'), css.indexOf('@media'));
  assert.match(light, /--ov-danger:\s*rgb\(/);
  assert.match(light, /--ov-danger-paper:\s*rgb\(/);
  assert.doesNotMatch(light, /--ov-danger[^;]*--ov-accent/);
  const dark = css.slice(css.indexOf('@media (prefers-color-scheme: dark)'));
  assert.match(dark, /--ov-danger:\s*rgb\(/);
  assert.match(dark, /--ov-danger-paper:\s*rgb\(/);
});

// --- the shadow room --------------------------------------------

function devbarCss() {
  return fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/devbar.css'), 'utf8');
}

// The room the container keeps for the shadow. The WebView no longer covers
// it, so C++ paints it and these numbers bound it.
function shadowPadding() {
  const cc = fs.readFileSync(
      path.join(SRC, 'chrome/browser/ui/atrium/atrium_devbar_layout.h'),
      'utf8');
  const constant = (name) => {
    const m = cc.match(new RegExp(`constexpr int ${name} = (\\d+);`));
    assert.ok(m, `${name} is a constant in atrium_devbar_layout.h`);
    return Number(m[1]);
  };
  return {
    top: constant('kDevBarShadowPadTop'),
    x: constant('kDevBarShadowPadX'),
    bottom: constant('kDevBarShadowPadBottom'),
  };
}

// A padding here puts the WebView back over the reserved room, where AppKit
// hit-tests it and a click near the bar vanishes into the renderer.
test('devbar.css leaves the shadow room to the window', () => {
  const css = devbarCss();
  assert.ok(!/body\s*{[^}]*padding:\s*\d+px\s+\d+px\s+\d+px/.test(css),
            'body must not pad itself back into the reserved room');
  assert.match(css, /body\s*{[^}]*margin:\s*0/,
               'the body rule still exists, so the check above means something');
});

// kDevBarShadowPadX's own comment: "0 5px 14px, so 7px of reach" - a blur
// radius reaches about half its length outward.
test('every --ov-shadow-tight variant fits inside the room devbar.css reserves', () => {
  const pad = shadowPadding();
  const overlay = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/overlay.css'), 'utf8');
  const defs = [...overlay.matchAll(
      /--ov-shadow-tight:\s*(-?\d+)(?:px)?\s+(-?\d+)px\s+(\d+)px/g)];
  assert.ok(defs.length >= 3,
            `expected light/dark/native-chrome variants, found ${defs.length}`);
  for (const [, , dyStr, blurStr] of defs) {
    const dy = Number(dyStr);
    const reach = Number(blurStr) / 2;
    assert.ok(dy >= 0, `the shadow falls downward or flat, dy=${dy}`);
    assert.ok(reach - dy <= pad.top, `${reach - dy}px above <= ${pad.top}`);
    assert.ok(dy + reach <= pad.bottom, `${dy + reach}px below <= ${pad.bottom}`);
    assert.ok(reach <= pad.x, `${reach}px sideways <= ${pad.x}`);
  }
});

test("the bar's shadow values come from overlay.css, not a C++ constant", () => {
  const css = devbarCss();
  assert.match(css, /\n\.devbar\s*{[^}]*box-shadow:\s*var\(--ov-shadow-tight\)/,
               '.devbar must declare the shadow so devbar.js can resolve it');
});

test('DevBarContainerView no longer paints its own shadow', () => {
  const cc = fs.readFileSync(
      path.join(SRC, 'chrome/browser/ui/atrium/atrium_browser_window.cc'),
      'utf8');
  assert.ok(!cc.includes('kDevBarShadowColor'),
            'the C++ shadow colour constant must be gone');
});

// --- the pin toggle -----------------------------------------------

test('buildDevBar draws a pin control, off, beside the copy button', () => {
  const bar = buildDevBar();
  const pin = bar.querySelector('.dvpin');
  assert.ok(pin, 'the bar carries a pin');
  assert.equal(pin.getAttribute('aria-pressed'), 'false');
  assert.equal(pin.getAttribute('aria-label'), 'Pin the address bar');
  assert.ok(pin.querySelector('svg'), 'the pin draws a glyph, not an emoji');
  const controls = [...bar.children].map((child) => child.className);
  assert.deepEqual(controls.slice(-2), ['dvcopy ov-btn', 'dvpin ov-btn']);
});

test('the pin control reports each flip to the window and shows it at once', () => {
  const root = fresh();
  const pin = root.querySelector('.dvpin');
  h.sent.length = 0;
  pin.click();
  assert.deepEqual(h.sent, [['devBarPinToggled', [true]]]);
  assert.equal(pin.getAttribute('aria-pressed'), 'true');
  pin.click();
  assert.deepEqual(h.sent.at(-1), ['devBarPinToggled', [false]]);
  assert.equal(pin.getAttribute('aria-pressed'), 'false');
});

// The window is the owner: it restores the saved pin at the handshake, and a
// second window on the same profile is told about a flip it did not make.
test('atriumSetDevBarPinned shows the pushed state and reports nothing back', () => {
  const root = fresh();
  h.sent.length = 0;
  window.atriumSetDevBarPinned(true);
  assert.equal(root.querySelector('.dvpin').getAttribute('aria-pressed'), 'true');
  assert.deepEqual(h.sent, []);
});

test('a push overrides what the click guessed, so the next click follows it', () => {
  const root = fresh();
  const pin = root.querySelector('.dvpin');
  pin.click();
  window.atriumSetDevBarPinned(false);
  h.sent.length = 0;
  pin.click();
  assert.deepEqual(h.sent, [['devBarPinToggled', [true]]]);
});

// The label reads off aria-label, not title: this bar draws its own hints, so
// installHover parks every title it finds to keep a tooltip off the surface.
test('the pin label names the action the next click performs', () => {
  const root = fresh();
  const pin = root.querySelector('.dvpin');
  assert.equal(pin.getAttribute('aria-label'), 'Pin the address bar');
  assert.equal(pin.dataset.hintTitle, 'Pin the address bar');
  window.atriumSetDevBarPinned(true);
  assert.equal(pin.getAttribute('aria-label'), 'Unpin the address bar');
});

test('devbar.css gives the pressed pin a ground the unpressed one has not', () => {
  const css = devbarCss();
  const pressed =
      /\.devbar \.dvpin\[aria-pressed="true"\]\s*{([^}]*)}/.exec(css);
  assert.ok(pressed, 'the pressed pin carries no rule');
  assert.match(pressed[1], /background:/);
  const hover =
      /\.devbar \.dvcopy:hover,\s*\.devbar \.dvpin:hover\s*{([^}]*)}/.exec(css);
  assert.ok(hover, 'the hover rule is gone');
  // Louder than hover, or a pinned bar reads as one merely under the cursor.
  assert.notEqual(pressed[1].trim(), hover[1].trim());
});

// --- the nav group the sidebar row hands over ------------------

// A name kSurfaceMessages does not carry NOTREACHEDs in the BROWSER process,
// so a wrong one here kills the app rather than the click.
const REGISTERED = new Set(
    fs.readFileSync(
          path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
          'utf8')
        .split('kSurfaceMessages[] = {')[1]
        .split('};')[0]
        .match(/"[^"]+"/g)
        .map((quoted) => quoted.slice(1, -1)));

test('the bar leads with back, forward and reload, in that order', () => {
  const bar = buildDevBar();
  const group = bar.querySelector('.dvnav');
  assert.ok(group, 'the bar carries no nav group');
  assert.deepEqual(group.children.map((n) => n.className), [
    'nb sbback', 'nb sbfwd', 'nb sbreload',
  ]);
  // children[0] is the loadfill layer, first so it paints under everything,
  // and the drag grip is the bar's left edge.
  assert.equal(bar.children[2].className, 'dvnav');
});

test('the bar keeps its own controls beside the group', () => {
  const bar = buildDevBar();
  const controls = bar.children.map((child) => child.className);
  assert.deepEqual(controls, [
    'loadfill', 'dvgrip', 'dvnav', 'dvlock', 'dvwarn', 'dvexts', 'dvurl',
    'dvcopy ov-btn', 'dvpin ov-btn',
  ]);
});

test('a nav click sends the plain message the sidebar row sends', () => {
  for (const [selector, expected] of [
         ['.sbfwd', 'sidebarForwardPressed'],
         ['.sbreload', 'sidebarReloadPressed'],
       ]) {
    const root = fresh();
    h.sent.length = 0;
    root.querySelector(selector).click();
    assert.deepEqual(h.sent, [[expected, []]], selector);
    assert.ok(REGISTERED.has(expected), `${expected} is fatal, not registered`);
  }
});

// Back navigates on pointerup, since a hold opens the history scrubber.
test('a back press navigates and a hold reaches the scrubber', () => {
  const root = fresh();
  const back = root.querySelector('.sbback');
  h.sent.length = 0;
  back.dispatchEvent(
      new Event('pointerdown', {timeStamp: 0, clientX: 0, clientY: 0}));
  back.dispatchEvent(new Event('pointerup', {timeStamp: 10}));
  assert.deepEqual(h.sent, [['sidebarBackPressed', []]]);
  assert.ok(REGISTERED.has('sidebarBackHeld'));
});

test('the native modifier push paints the bar\'s group', () => {
  const root = fresh();
  const group = root.querySelector('.dvnav');
  assert.equal(typeof window.atriumSetNavModifiers, 'function',
               'the bar publishes no receiver, so the push is lost');
  window.atriumSetNavModifiers(false, false, true);
  assert.equal(group.dataset.mod, 'option');
  assert.equal(root.querySelector('.sbback .g svg').dataset.nav, 'end');
  window.atriumSetNavModifiers(true, true, false);
  assert.equal(group.dataset.mod, 'cmdshift');
  assert.ok(root.querySelector('.sbreload svg .rlcrumb'),
            'the bar\'s reload kept the plain arc under Cmd+Shift');
  window.atriumSetNavModifiers(true, false, false);
  assert.equal(group.dataset.mod, undefined, 'Shift alone is not a state');
  window.atriumSetNavModifiers(false, false, false);
  assert.equal(group.dataset.mod, undefined);
});

const modClick = (el, mods) =>
    el.dispatchEvent(new Event('click', {bubbles: true, ...mods}));

test('a held modifier changes what a click sends', () => {
  const root = fresh();
  window.atriumSetNavModifiers(false, false, true);
  h.sent.length = 0;
  modClick(root.querySelector('.sbreload'), {altKey: true});
  assert.deepEqual(h.sent, [['sidebarReloadHard', []]]);
  window.atriumSetNavModifiers(true, true, false);
  h.sent.length = 0;
  modClick(root.querySelector('.sbreload'), {metaKey: true, shiftKey: true});
  assert.deepEqual(h.sent, [['sidebarReloadClearingSite', []]]);
  assert.ok(REGISTERED.has('sidebarReloadClearingSite'),
            'sidebarReloadClearingSite is fatal, not registered');
  window.atriumSetNavModifiers(false, false, false);
});

// The bar hosts the group while it is up, so it needs the same self-heal the
// row has - a push whose release is lost would arm it for good.
test('a bare click heals a state the bar was left holding', () => {
  const root = fresh();
  const group = root.querySelector('.dvnav');
  window.atriumSetNavModifiers(false, false, true);
  h.sent.length = 0;
  root.querySelector('.sbreload').click();
  assert.equal(group.dataset.mod, undefined);
  assert.deepEqual(h.sent, [['sidebarReloadPressed', []]]);
});

test('devbar.css paints the group and both modifier variants', () => {
  const css = devbarCss();
  assert.match(css, /\.devbar \.nb\s*{[^}]*width:\s*22px/);
  assert.match(css, /\.devbar \.dvnav\[data-mod="option"\][^{]*{[^}]*color:\s*#6b4700/s);
  assert.match(css, /\.devbar \.dvnav\[data-mod="cmdshift"\][^{]*{[^}]*color:\s*#701400/s);
  assert.equal(/\.devbar \.dvnav\[data-mod="option"\][^{]*{[^}]*rgba\(/s.test(css), false,
               'a translucent wash lets the tint back in');
});

// The bar's group is the SAME buildNavGroup as the row's, so a modifier push
// fades it through the same ghost span - only the CSS geometry needs its own
// copy here.
test('a modifier push on the bar fades through a ghost too', () => {
  const root = fresh();
  window.atriumSetNavModifiers(false, false, true);
  const ghost = root.querySelector('.sbreload .gl.ghost');
  assert.ok(ghost, 'the bar\'s reload left no ghost holding the old glyph');
  h.tick();
  assert.ok(ghost.classList.contains('out'));
  window.atriumSetNavModifiers(false, false, false);
});

test('devbar.css fades .g in and the ghost out over the plate\'s own .12s', () => {
  const css = devbarCss();
  assert.match(css,
      /\.devbar \.nb\s*{[^}]*transition:\s*background-color \.12s,\s*color \.12s/s);
  assert.match(css,
      /\.devbar \.nb \.g\.gswap,\s*\n\.devbar \.nb \.gl\.ghost\s*{[^}]*transition:\s*opacity \.12s/s);
  assert.match(css, /\.devbar \.nb \.g\.gswap\.entering\s*{[^}]*opacity:\s*0/s);
  assert.match(css, /\.devbar \.nb \.gl\.ghost\.out\s*{[^}]*opacity:\s*0/s);
});

test('devbar.css drops the plate and glyph transitions under reduced motion', () => {
  const css = devbarCss();
  const blocks = [...css.matchAll(
      /@media \(prefers-reduced-motion: reduce\)\s*{([^]*?)\n}\n/g)]
      .map((m) => m[1]);
  const nbBlock = blocks.find((b) => /\.devbar \.nb\s*{[^}]*transition:\s*none/s.test(b));
  assert.ok(nbBlock, 'no reduced-motion block turns off the plate transition');
  assert.match(nbBlock,
      /\.devbar \.nb \.g\.gswap,\s*\n\s*\.devbar \.nb \.gl\.ghost\s*{[^}]*transition:\s*none/s);
});

// devbar.css keeps its own copy of .sbnav .nb's font rule for the
// same buttons (buildNavGroup, hosted here) - a size changed
// on one side and left behind on the other is only caught by reading both.
test('the devbar glyphs stay the same size as the sidebar row', () => {
  const devbarFont =
      devbarCss().match(/\.devbar \.nb\s*{[^}]*font:\s*([\d.]+px\/1)/s);
  const sidebarCss = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar-nav-row.css'),
      'utf8');
  const sidebarFont =
      sidebarCss.match(/\.sbnav \.nb\s*{[^}]*font:\s*([\d.]+px\/1)/s);
  assert.ok(devbarFont, '.devbar .nb has no font rule');
  assert.ok(sidebarFont, '.sbnav .nb has no font rule');
  assert.equal(devbarFont[1], '17px/1');
  assert.equal(devbarFont[1], sidebarFont[1]);

  const root = fresh();
  const reload = root.querySelector('.sbreload svg');
  assert.equal(reload.getAttribute('width'), '14');
  assert.equal(reload.getAttribute('height'), '14');
});

// A wash tuned faint enough for one plate and dark enough for the other
// would drift the two surfaces apart, so the declarations are compared
// value by value rather than trusted to stay in sync by hand.
test('the devbar modifier plate matches the sidebar row', () => {
  const devbarCssText = devbarCss();
  const sidebarCssText = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/sidebar-nav-row.css'),
      'utf8');
  for (const mode of ['option', 'cmdshift']) {
    const devbarRule = devbarCssText.match(new RegExp(
        `\\.devbar \\.dvnav\\[data-mod="${mode}"\\] \\.nb \\{([^}]*)\\}`,
        's'))[1];
    const sidebarRule = sidebarCssText.match(new RegExp(
        `\\.sbnav\\[data-mod="${mode}"\\] \\.nb\\.sbreload \\{([^}]*)\\}`,
        's'))[1];
    for (const prop of ['color', 'background', 'font-weight']) {
      const devbarValue =
          devbarRule.match(new RegExp(`${prop}:\\s*([^;]+);`))[1];
      const sidebarValue =
          sidebarRule.match(new RegExp(`${prop}:\\s*([^;]+);`))[1];
      assert.equal(devbarValue, sidebarValue,
                   `${mode} ${prop} drifted between the bar and the row`);
    }
  }
});

// The group is this surface's while the bar is up, so the window's ability
// push has to reach it here too - the sidebar's receiver by the same name
// lives in the other document.
test('the ability push greys the bar\'s back and forward', () => {
  const root = fresh();
  assert.equal(typeof window.atriumSetNavAbility, 'function',
               'the bar publishes no receiver, so the push is lost');
  window.atriumSetNavAbility({back: false, forward: false});
  assert.ok(root.querySelector('.sbback').classList.contains('navdim'));
  assert.ok(root.querySelector('.sbfwd').classList.contains('navdim'));
  window.atriumSetNavAbility({back: true, forward: false});
  assert.equal(root.querySelector('.sbback').classList.contains('navdim'),
               false);
  assert.ok(root.querySelector('.sbfwd').classList.contains('navdim'));
});

test('a greyed control in the bar sends nothing, plain or held', () => {
  const root = fresh();
  window.atriumSetNavAbility({back: false, forward: false});
  h.sent.length = 0;
  const back = root.querySelector('.sbback');
  back.dispatchEvent(
      new Event('pointerdown', {timeStamp: 0, clientX: 0, clientY: 0}));
  back.dispatchEvent(new Event('pointerup', {timeStamp: 10}));
  root.querySelector('.sbfwd').click();
  window.atriumSetNavModifiers(false, false, true);
  modClick(root.querySelector('.sbfwd'), {altKey: true});
  window.atriumSetNavModifiers(false, false, false);
  assert.deepEqual(h.sent, []);
  root.querySelector('.sbreload').click();
  assert.deepEqual(h.sent, [['sidebarReloadPressed', []]],
                  'reload has nowhere to run out of, so it never greys');
});

test('devbar.css greys a dead button on its own plate', () => {
  const css = devbarCss();
  assert.match(css, /\.devbar \.nb\.navdim[^{]*\{[^}]*opacity:/s);
  assert.match(css, /\.devbar \.dvnav\[data-mod="option"\] \.nb\.navdim/);
  assert.match(css, /\.devbar \.dvnav\[data-mod="cmdshift"\] \.nb\.navdim/);
});

// --- the url opens the prompt ------------------------------------------------

test('a click on the url hands its box to the window', () => {
  const root = fresh();
  const url = root.querySelector('.dvurl');
  url.setRect({left: 240, top: 44, width: 380, height: 26});
  window.atriumSetDevBarUrl('https://example.com/path');
  h.sent.length = 0;
  url.click();
  assert.deepEqual(h.sent, [['devBarUrlClicked', [240, 44, 380, 26]]]);
});

test('the url reports its box even before a push has filled it', () => {
  // The bar is built before the first push, and a click in that gap must
  // still open the prompt rather than do nothing.
  const root = fresh();
  const url = root.querySelector('.dvurl');
  url.setRect({left: 240, top: 44, width: 380, height: 26});
  h.sent.length = 0;
  url.click();
  assert.deepEqual(h.sent, [['devBarUrlClicked', [240, 44, 380, 26]]]);
});

// --- the shared hint bubble replaces the native title tooltip --------------

// the bar's own document clips a bubble at the capsule edge,
// so every hint here leaves for HintOverlayWidget instead of rendering in it.
function shownHint() {
  const last = h.sent.at(-1);
  assert.equal(last?.[0], 'devBarHintShow', 'the hint must leave the bar');
  return last[1];
}

test('hovering a nav button hints through the overlay, not a native tooltip', () => {
  const {root, clock} = freshWithClock();
  const back = root.querySelector('.sbback');
  back.setRect({left: 12, top: 6, width: 22, height: 22});
  back.dispatchEvent(new Event('pointerover', {bubbles: true}));
  assert.equal(back.title, '', 'the native tooltip must be parked while armed');
  clock.advance(600);
  assert.equal(document.querySelectorAll('.hint').length, 0);
  assert.deepEqual(shownHint().slice(2, 6), [12, 6, 22, 22]);
  hideHint();
  assert.deepEqual(h.sent.at(-1), ['devBarHintHide', []]);
});

test('the copy and pin buttons hint too', () => {
  const {root, clock} = freshWithClock();
  root.querySelector('.dvcopy').dispatchEvent(
      new Event('pointerover', {bubbles: true}));
  clock.advance(600);
  assert.equal(shownHint()[0], 'Copy URL');
  hideHint();
  root.querySelector('.dvpin').dispatchEvent(
      new Event('pointerover', {bubbles: true}));
  clock.advance(600);
  assert.equal(shownHint()[0], 'Pin the address bar');
  hideHint();
});

test('the lock hints nothing on a secure page, names the origin on an insecure one', () => {
  const {root, clock} = freshWithClock();
  const lock = root.querySelector('.dvlock');
  const before = h.sent.length;
  lock.dispatchEvent(new Event('pointerover', {bubbles: true}));
  clock.advance(600);
  assert.equal(currentHint(), null, 'a secure page has nothing to name');
  assert.equal(h.sent.length, before, 'and nothing reaches the overlay');
  window.atriumSetDevBarUrl('http://example.com/');
  lock.dispatchEvent(
      new Event('pointerout', {bubbles: true, relatedTarget: document.body}));
  lock.dispatchEvent(new Event('pointerover', {bubbles: true}));
  clock.advance(600);
  assert.match(shownHint()[0], /^Not secure:/);
  hideHint();
});

test('the hint messages are registered with the browser', () => {
  const registered = new Set(
      fs.readFileSync(
            path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
            'utf8')
          .split('kSurfaceMessages[] = {')[1]
          .split('};')[0]
          .match(/"[^"]+"/g)
          .map((quoted) => quoted.slice(1, -1)));
  for (const name of ['devBarHintShow', 'devBarHintHide']) {
    assert.ok(registered.has(name), `${name} is fatal, not registered`);
  }
});

test('devBarUrlClicked is registered with the browser', () => {
  const registered = new Set(
      fs.readFileSync(
            path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
            'utf8')
          .split('kSurfaceMessages[] = {')[1]
          .split('};')[0]
          .match(/"[^"]+"/g)
          .map((quoted) => quoted.slice(1, -1)));
  assert.ok(registered.has('devBarUrlClicked'));
});

// the window makes the pressed bar's pane active, so the press
// has to reach it before the click that acts on the active pane.
test('a press on a split bar reports itself before the click', () => {
  const root = fresh();
  const url = root.querySelector('.dvurl');
  window.atriumSetDevBarTab('8');
  h.sent.length = 0;
  url.dispatchEvent(new Event('pointerdown', {bubbles: true}));
  url.click();
  assert.deepEqual(h.sent.map(([name]) => name),
                   ['devBarPointerDown', 'devBarUrlClicked']);
});

test('a press on the single bar reports nothing extra', () => {
  const root = fresh();
  const url = root.querySelector('.dvurl');
  h.sent.length = 0;
  url.dispatchEvent(new Event('pointerdown', {bubbles: true}));
  assert.deepEqual(h.sent, []);
});

test('devBarPointerDown is registered with the browser', () => {
  const registered = new Set(
      fs.readFileSync(
            path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
            'utf8')
          .split('kSurfaceMessages[] = {')[1]
          .split('};')[0]
          .match(/"[^"]+"/g)
          .map((quoted) => quoted.slice(1, -1)));
  assert.ok(registered.has('devBarPointerDown'),
            'an unregistered name crashes the browser process');
});

test('a split bar fills only for its own pane\'s tab', () => {
  const root = fresh();
  const fill = root.querySelector('.loadfill');
  window.atriumSetDevBarTab('8');
  window.atriumLoadPhase('7', 'waiting', 0);
  assert.equal(fill.classList.contains('waiting'), false);
  window.atriumLoadPhase('8', 'waiting', 0);
  assert.equal(fill.classList.contains('waiting'), true);
});

test('an empty tab id puts the single bar back on every phase', () => {
  const root = fresh();
  const fill = root.querySelector('.loadfill');
  window.atriumSetDevBarTab('8');
  window.atriumSetDevBarTab('');
  window.atriumLoadPhase('7', 'waiting', 0);
  assert.equal(fill.classList.contains('waiting'), true);
});

test('extensionPinToggled is registered with the browser', () => {
  const registered = new Set(
      fs.readFileSync(
            path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
            'utf8')
          .split('kSurfaceMessages[] = {')[1]
          .split('};')[0]
          .match(/"[^"]+"/g)
          .map((quoted) => quoted.slice(1, -1)));
  assert.ok(registered.has('extensionPinToggled'),
            'an unregistered name crashes the browser process');
});

test('.dvexts fills from the same extensions push the sidebar takes', () => {
  const root = fresh();
  window.atriumSetSidebarExtensions([
    {id: 'ublock', name: 'uBlock', actionTitle: 'uBlock', visible: true,
     pinned: true},
    {id: 'other', name: 'Other', actionTitle: 'Other', visible: true,
     pinned: false},
  ]);
  const chips = root.querySelectorAll('.dvexts .exs-chip');
  assert.equal(chips.length, 1, 'only the pinned one');
  assert.equal(chips[0].dataset.extensionId, 'ublock');
});

// The sidebar takes its scope off the snapshot, which never reaches this
// route. Without a scope extIconPath() returns '' and every chip draws the
// browser's broken-image glyph instead of the extension's own icon.
// The page scope opens no extension icon, so the chip reads the push's own.
test('a devbar chip draws its icon in the scope the push names', () => {
  const root = fresh();
  const pin = {id: 'ublock', name: 'uBlock', actionTitle: 'uBlock',
               visible: true, pinned: true};
  window.atriumSetIconScope('testscope');
  window.atriumSetSidebarExtensions([pin]);
  assert.equal(root.querySelector('.dvexts .crx-icon').src, '',
               'the control: the page scope alone gives no icon path');
  window.atriumSetSidebarExtensions([{...pin, iconScope: 'extscope'}]);
  assert.equal(root.querySelector('.dvexts .crx-icon').src,
               '/exticon/extscope/ublock.png');
});

// The native View appears in one frame, so without this the bar pops into
// place while the sidebar row and its strip slide away above it.
test('the bar descends when the window says it just came up', () => {
  const root = fresh();
  const bar = root.querySelector('.devbar');
  assert.equal(bar.classList.contains('descending'), false);
  window.atriumDevBarEnter();
  assert.equal(bar.classList.contains('descending'), true);
});

test('a devbar chip click sends the same message the sidebar strip sends', () => {
  const root = fresh();
  window.atriumSetSidebarExtensions([
    {id: 'ublock', name: 'uBlock', actionTitle: 'uBlock', visible: true,
     pinned: true},
  ]);
  h.sent.length = 0;
  const chip = root.querySelector('.dvexts .exs-chip');
  // The bar's chips carry their own screen rect too, or a popup opened from
  // here anchors to whatever the sidebar reported last.
  chip.setRect({left: 60, top: 8, width: 24, height: 24});
  window.screenX = 200;
  window.screenY = 100;
  chip.click();
  assert.deepEqual(h.sent,
                   [['pillActionClicked', ['ublock', [260, 108, 24, 24]]]]);
});

test('the floating bar carries the quick menu button at the field\'s end', () => {
  const root = fresh();
  const button = root.querySelector('.dvexts .qm-open');
  assert.ok(button, 'no puzzle button in the extension slot');
  assert.equal(button.getAttribute('aria-label'), 'Extensions and site');
});

test('the button hands the browser its own box', () => {
  const root = fresh();
  const button = root.querySelector('.dvexts .qm-open');
  button.setRect({left: 210, top: 12, width: 20, height: 20});
  h.sent.length = 0;
  button.click();
  assert.deepEqual(h.sent, [['quickMenuToggled', [210, 12, 20, 20]]]);
});

test('the pinned chips stay put when the strip re-renders around the button', () => {
  const root = fresh();
  window.atriumSetSidebarExtensions([
    {id: 'ublock', name: 'uBlock', actionTitle: 'uBlock', visible: true,
     pinned: true},
  ]);
  const slot = root.querySelector('.dvexts');
  assert.equal(slot.querySelectorAll('.exs-chip').length, 1);
  assert.ok(slot.querySelector('.qm-open'),
            'a re-render must not drop the button');
});

// --- the drag grip --------------------------------------------

function registeredNames() {
  return new Set(
      fs.readFileSync(
            path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
            'utf8')
          .split('kSurfaceMessages[] = {')[1]
          .split('};')[0]
          .match(/"[^"]+"/g)
          .map((quoted) => quoted.slice(1, -1)));
}

test('the grip leads the bar and names what it does', () => {
  const root = fresh();
  const grip = root.querySelector('.dvgrip');
  assert.ok(grip, 'no grip');
  assert.equal(grip.draggable, true);
  assert.equal(grip.getAttribute('aria-label'), 'Drag to move this tab');
  const controls = [...root.querySelector('.devbar').children]
      .filter((el) => !el.classList.contains('loadfill'));
  assert.equal(controls[0], grip, 'the grip is the bar\'s left edge');
});

test('a plain press and click on the grip sends nothing, split or not', () => {
  for (const tabId of ['', '8']) {
    const root = fresh();
    window.atriumSetDevBarTab(tabId);
    const grip = root.querySelector('.dvgrip');
    h.sent.length = 0;
    grip.dispatchEvent(new Event('pointerdown', {bubbles: true}));
    grip.click();
    assert.deepEqual(h.sent, [], `tab '${tabId}'`);
  }
});

test('a drag carries only the tab type, so a page drop cannot navigate', () => {
  const root = fresh();
  window.atriumSetDevBarUrl('https://example.com/');
  const grip = root.querySelector('.dvgrip');
  const dataTransfer = makeDataTransfer();
  let image = null;
  dataTransfer.setDragImage = (el, x, y) => {
    image = {el, x, y};
  };
  h.sent.length = 0;
  grip.dispatchEvent(new Event('dragstart', {bubbles: true, dataTransfer}));
  assert.deepEqual(dataTransfer.types, ['application/x-atrium-tab']);
  assert.equal(dataTransfer.effectAllowed, 'move');
  assert.deepEqual(h.sent, [['devBarTabDragStarted', []]]);
  assert.ok(image?.el.matches('.row.tabrow'), 'the ghost is row-shaped');
  assert.ok(image.el.querySelector('.fv') && image.el.querySelector('.tl'));
  assert.equal(image.el.querySelector('.tl').textContent, 'example.com');
});

test('the drag reports each new point once, and never the end-of-drag 0,0', () => {
  const root = fresh();
  const grip = root.querySelector('.dvgrip');
  grip.dispatchEvent(new Event('dragstart',
                               {bubbles: true, dataTransfer: makeDataTransfer()}));
  h.sent.length = 0;
  for (const [screenX, screenY] of [[300, 60], [300, 60], [310, 70], [0, 0]]) {
    grip.dispatchEvent(new Event('drag', {bubbles: true, screenX, screenY}));
  }
  assert.deepEqual(h.sent, [['devBarTabDragMoved', [300, 60]],
                            ['devBarTabDragMoved', [310, 70]]]);
});

test('dragend hands the release point to the window', () => {
  const root = fresh();
  const grip = root.querySelector('.dvgrip');
  grip.dispatchEvent(new Event('dragstart',
                               {bubbles: true, dataTransfer: makeDataTransfer()}));
  h.sent.length = 0;
  grip.dispatchEvent(new Event('dragend',
                               {bubbles: true, screenX: 113, screenY: 262}));
  assert.deepEqual(h.sent, [['devBarTabDragReleased', [113, 262]]]);
  assert.equal(document.querySelector('.dvghost'), null, 'the ghost is gone');
});

test('the grip messages are registered with the browser', () => {
  const registered = registeredNames();
  for (const name of ['devBarTabDragStarted', 'devBarTabDragMoved',
                      'devBarTabDragReleased']) {
    assert.ok(registered.has(name), `${name} is unregistered, a FATAL crash`);
  }
});
