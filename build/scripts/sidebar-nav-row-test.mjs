#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the pinned chrome row, ported from the lab's #sbChrome.

//   node --test build/scripts/sidebar-nav-row-test.mjs

// A control wired to no message is dead, and one wired to a name
// kSurfaceMessages does not carry is FATAL in the browser process.

// The row installs OUTSIDE #atrium-root because every snapshot push calls
// root.replaceChildren(), which would take it along.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');
const h = installDOM({pathname: '/sidebar'});
const {buildNavRow, installSidebarNavRow, visibleNavButtons, layoutNavRow,
       NAV_METRICS, setNavGroupInRow, navRowForRoot, navPainterCount,
       createModifierTracker} =
    await import(path.join(ATRIUM_DIR, 'sidebar-nav-row.js'));
const {HOLD_THRESHOLD_MS} = await import(path.join(ATRIUM_DIR, 'hold-gesture.js'));
const {dismissMenus} = await import(path.join(ATRIUM_DIR, 'menu-dismiss.js'));
const {setBindings, installHintTriggers} =
    await import(path.join(ATRIUM_DIR, 'hint-triggers.js'));
const {currentHint} = await import(path.join(ATRIUM_DIR, 'hint.js'));
const {makeFakeClock} = await import('./fake-clock.mjs');

// Read from the header rather than copied, so a rename on either side shows up
// here instead of at the next launch.
const REGISTERED = new Set(
    fs.readFileSync(
          path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
          'utf8')
        .split('kSurfaceMessages[] = {')[1]
        .split('};')[0]
        .match(/"[^"]+"/g)
        .map((quoted) => quoted.slice(1, -1)));

const css = fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar-nav-row.css'), 'utf8');

function row() {
  h.reset();
  const sent = [];
  const el = buildNavRow({send: (name, args) => sent.push([name, args])});
  document.body.appendChild(el);
  return {el, sent};
}

// `mods` are the click's OWN modifier flags, which a real MouseEvent carries
// and the row's self-heal reads.
const click = (el, mods = {}) =>
    el.dispatchEvent(new Event('click', {bubbles: true, ...mods}));


test('the row carries the lab\'s controls, in the lab\'s order', () => {
  // Order IS the layout - the lab puts the dots first, then the toggle
  // (swipe-lab.html:54). Back carries first, the lab's own right-group
  // anchor (swipe-lab.html:3662), not the toggle.
  const {el} = row();
  assert.deepEqual(el.children.map((n) => n.className), [
    'sbtd', 'nb sbtoggle', 'nb first sbback', 'nb sbfwd', 'nb sbreload',
  ]);
});

test('back anchors the right group to the row\'s right edge', () => {
  const {el} = row();
  assert.ok(el.querySelector('.sbback').classList.contains('first'));
  for (const cls of ['.sbtoggle', '.sbfwd', '.sbreload']) {
    assert.equal(el.querySelector(cls).classList.contains('first'), false,
                 `${cls} must not also anchor the group`);
  }
  assert.match(css, /\.sbnav \.nb\.first\s*\{[^}]*margin-left:\s*auto/s);
});

// the reveal panel is a child window and draws above the parent's
// frame view, so the real close/minimise/zoom buttons are behind it. The page
// carries its own, and they are hidden until the native side says the sidebar
// is floating.
test('the window dots are the lab\'s three, each on its own message', () => {
  const {el, sent} = row();
  const dots = el.querySelector('.sbtd');
  assert.deepEqual(dots.children.map((n) => n.className), [
    'td close', 'td min', 'td zoom',
  ]);
  for (const [selector, expected] of [
         ['.td.close', 'sidebarWindowClose'],
         ['.td.min', 'sidebarWindowMinimize'],
         ['.td.zoom', 'sidebarWindowZoom'],
       ]) {
    sent.length = 0;
    click(el.querySelector(selector));
    assert.deepEqual(sent, [[expected, []]]);
    assert.ok(REGISTERED.has(expected),
              `${expected} is not in kSurfaceMessages, so it is fatal`);
  }
});

test('the dots stay hidden until the sidebar floats', () => {
  assert.match(css, /\.sbnav \.sbtd\s*\{[^}]*display:\s*none/);
  assert.match(
      css, /\.atrium-sidebar-floating \.sbnav \.sbtd\s*\{[^}]*display:\s*flex/);
  // Docked, the row keeps the clearance the real buttons sit in; floating,
  // the page's own dots take that space.
  assert.match(css, /\.atrium-sidebar-floating \.sbnav\s*\{[^}]*left:/);
});

test('the floating row carries dark ink, because white lands on its own wash',
     () => {
       // the row sits outside #atrium-root, so --fg falls back to
       // white and the icons vanished into the panel's rgb(219 222 229) wash.
       const ink = css.match(
           /html\.atrium-sidebar-floating:not\(\.atrium-native-chrome\) \.sbnav\s*\{([^}]*)\}/);
       assert.ok(ink, 'the floating row sets its own colour');
       assert.match(ink[1], /color:\s*rgb\(28 28 30/);
       assert.match(
           css,
           /html\.atrium-sidebar-floating:not\(\.atrium-native-chrome\) \.sbnav \.nb:hover\s*\{[^}]*background:\s*rgb\(28 28 30/);
       // Dark keeps white: its floating wash is rgb(42 42 44).
       const dark = css.slice(css.indexOf('@media (prefers-color-scheme: dark)'));
       assert.match(
           dark,
           /html\.atrium-sidebar-floating:not\(\.atrium-native-chrome\) \.sbnav\s*\{[^}]*color:\s*rgba\(255, 255, 255/);
     });

test('each control sends its own message and nothing else', () => {
  for (const [selector, expected] of [
         ['.sbtoggle', 'sidebarToggleRequested'],
         ['.sbfwd', 'sidebarForwardPressed'],
         ['.sbreload', 'sidebarReloadPressed'],
       ]) {
    const {el, sent} = row();
    click(el.querySelector(selector));
    assert.deepEqual(sent, [[expected, []]], selector);
  }
});

test('every name the row sends is registered in kSurfaceMessages', () => {
  // An unregistered name NOTREACHEDs in the browser process, which kills the
  // app rather than the click.
  const {el, sent} = row();
  for (const button of el.querySelectorAll('.nb')) {
    click(button);
  }
  // Back sends on pointerup, not click, so it contributes nothing here - its
  // own two names are covered by hold-gesture-test.mjs.
  assert.ok(sent.length >= 3, 'no control sent anything');
  for (const [name] of sent) {
    assert.ok(REGISTERED.has(name), `${name} is not in kSurfaceMessages`);
  }
});

test('the row installs outside the route root, so a re-render cannot take it',
     () => {
       h.reset();
       const root = document.createElement('div');
       root.id = 'atrium-root';
       document.body.appendChild(root);
       installSidebarNavRow(root, {send: () => {}});

       const el = document.body.querySelector('.sbnav');
       assert.ok(el, 'no row installed');
       assert.equal(root.contains(el), false,
                    'a snapshot push replaceChildren()s the root');
       root.replaceChildren();
       assert.equal(document.body.querySelectorAll('.sbnav').length, 1,
                    'the row must survive a re-render');
     });

test('a second install replaces the first rather than orphaning it', () => {
  h.reset();
  const root = document.createElement('div');
  document.body.appendChild(root);
  installSidebarNavRow(root, {send: () => {}});
  installSidebarNavRow(root, {send: () => {}});
  assert.equal(document.body.querySelectorAll('.sbnav').length, 1);
});

test('the row sits inside the window\'s one caption hole', () => {
  // AtriumClientView::NonClientHitTest carves x 82..196 under y 40 and nothing
  // else. A control outside it starts a window drag and never sees the click,
  // which is invisible from this side - hence the arithmetic here.
  const left = Number(css.match(/--atrium-traffic-light-clearance,\s*(\d+)px/)[1]);
  const top = Number(css.match(/--atrium-traffic-light-band-inset,\s*(\d+)px/)[1]);
  const width = Number(css.match(/\.sbnav \.nb \{[^}]*?width:\s*(\d+)px/s)[1]);
  const height = Number(css.match(/\.sbnav \.nb \{[^}]*?height:\s*(\d+)px/s)[1]);
  const gap = Number(css.match(/\.sbnav \{[^}]*?gap:\s*(\d+)px/s)[1]);

  const right = left + 4 * width + 3 * gap;
  assert.ok(left >= 82, `row starts at ${left}, left of the carve`);
  assert.ok(right <= 196, `row ends at ${right}, right of the carve`);
  assert.ok(top + height <= 40, `row ends at y ${top + height}, below the carve`);
});

// the lab's hover fade and click-driven glyph motion
// (swipe-lab.html:56-69).

const press = (el, mods = {}) => {
  el.dispatchEvent(new Event(
      'pointerdown', {timeStamp: 0, clientX: 0, clientY: 0, ...mods}));
  el.dispatchEvent(new Event('pointerup', {timeStamp: 10, ...mods}));
};

test('a back press adds nudgeL to its glyph', () => {
  const {el} = row();
  const glyph = el.querySelector('.sbback .g');
  assert.ok(glyph, 'back has no glyph wrapper');
  press(el.querySelector('.sbback'));
  assert.ok(glyph.classList.contains('nudgeL'));
});

test('clicking forward adds nudgeR to its glyph', () => {
  const {el} = row();
  click(el.querySelector('.sbfwd'));
  assert.ok(el.querySelector('.sbfwd .g').classList.contains('nudgeR'));
});

test('clicking reload adds spin to its glyph', () => {
  const {el} = row();
  click(el.querySelector('.sbreload'));
  assert.ok(el.querySelector('.sbreload .g').classList.contains('spin'));
});

test('animationend removes the class, and a second click re-adds it', () => {
  const {el} = row();
  const button = el.querySelector('.sbreload');
  const glyph = button.querySelector('.g');
  click(button);
  assert.ok(glyph.classList.contains('spin'));
  glyph.dispatchEvent(new Event('animationend'));
  assert.equal(glyph.classList.contains('spin'), false);
  click(button);
  assert.ok(glyph.classList.contains('spin'), 'second click must replay the animation');
});

test('animationcancel also clears the class', () => {
  const {el} = row();
  const button = el.querySelector('.sbreload');
  const glyph = button.querySelector('.g');
  click(button);
  assert.ok(glyph.classList.contains('spin'));
  glyph.dispatchEvent(new Event('animationcancel'));
  assert.equal(glyph.classList.contains('spin'), false);
});

test('a back press held past the hold threshold releases without nudgeL', async () => {
  const {el} = row();
  const button = el.querySelector('.sbback');
  const glyph = button.querySelector('.g');
  button.dispatchEvent(new Event('pointerdown', {timeStamp: 0, clientX: 0, clientY: 0}));
  await new Promise((resolve) => setTimeout(resolve, HOLD_THRESHOLD_MS + 50));
  button.dispatchEvent(new Event('pointerup', {timeStamp: HOLD_THRESHOLD_MS + 50}));
  assert.equal(glyph.classList.contains('nudgeL'), false,
               'the scrubber path releases as CANCELLED, not PRESS, so nothing should nudge');
});

test('prefers-reduced-motion skips the nudge and spin classes', () => {
  h.reset();
  const el = buildNavRow({send: () => {}, reducedMotion: true});
  document.body.appendChild(el);
  click(el.querySelector('.sbfwd'));
  click(el.querySelector('.sbreload'));
  press(el.querySelector('.sbback'));
  assert.equal(el.querySelector('.sbfwd .g').classList.contains('nudgeR'), false);
  assert.equal(el.querySelector('.sbreload .g').classList.contains('spin'), false);
  assert.equal(el.querySelector('.sbback .g').classList.contains('nudgeL'), false);
});

// A mouse click on macOS leaves focus on the button, and :focus-visible is
// what tells a click apart from a Tab press.

test('a plain click leaves no ring, but keyboard focus still gets one', () => {
  assert.match(css, /\.sbnav \.nb:focus\s*\{[^}]*outline:\s*none/s);
  assert.match(css, /\.sbnav \.nb:focus-visible\s*\{[^}]*outline:/s);
  const focusVisible =
      css.match(/\.sbnav \.nb:focus-visible\s*\{([^}]*)\}/s)[1];
  assert.equal(/outline:\s*none/.test(focusVisible), false,
               'the keyboard ring must not also be deleted');
  // The two rules carry equal specificity, so only source order makes the
  // keyboard ring win. Reordering them would delete it with every other
  // assertion here still passing.
  assert.ok(css.indexOf('.sbnav .nb:focus-visible') >
                css.indexOf('.sbnav .nb:focus {'),
            'the focus-visible rule must come after the focus rule');
});

test('clicking a nav button does not add a class that would outlive the click',
     () => {
       const {el} = row();
       for (const selector of ['.sbback', '.sbfwd', '.sbreload']) {
         const button = el.querySelector(selector);
         const before = button.className;
         click(button);
         assert.equal(button.className, before,
                      `${selector} kept a class the click added`);
       }
     });

test('the CSS carries the hover transition and the three keyframes', () => {
  assert.match(css, /\.sbnav \.nb\s*\{[^}]*transition:\s*background[^}]*\}/s);
  assert.match(css, /@keyframes navnudgeL\s*\{/);
  assert.match(css, /@keyframes navnudgeR\s*\{/);
  assert.match(css, /@keyframes reloadspin\s*\{/);
});

// the modifier ladder, the lab's reload modes (swipe-lab.html:4748)
// carried by all three controls. Option is the wider function, Cmd+Shift the
// widest, and glyph plus colour say which one a click will run.

// Every live tracker listens on window, so a chord left down would follow the
// next test into its own row.
const chordKey = (init) =>
    init.altKey ? 'Alt' : (init.metaKey ? 'Meta' : 'Shift');
const hold = (init) => window.dispatchEvent(
    new Event('keydown', {key: chordKey(init), ...init}));
const drop = () => window.dispatchEvent(new Event('keyup', {key: 'Alt'}));

// Every control draws, so a state is read off the drawing rather than off a
// character. Reload carries no data-nav, so it reads '' in all three states.
const navMode = (g) => g.querySelector('svg')?.dataset.nav ?? '';

const glyphs = (el) => ['.sbback', '.sbfwd', '.sbreload']
    .map((sel) => navMode(el.querySelector(`${sel} .g`)));

// Reload draws in every state, so its mode is read off the drawing rather
// than off a character - textContent is '' for all three.
const reloadMode = (el) => {
  const svg = el.querySelector('.sbreload svg');
  if (!svg) {
    return 'gone';
  }
  if (svg.querySelector('.rlbolt')) {
    return 'hard';
  }
  return svg.querySelector('.rlcrumb') ? 'nuke' : 'plain';
};

test('holding Option marks the row and swaps every nav glyph', () => {
  const {el} = row();
  const resting = glyphs(el);
  hold({key: 'Alt', altKey: true});
  assert.equal(el.dataset.mod, 'option');
  const held = glyphs(el);
  assert.notDeepEqual(held, resting, 'the glyphs did not change');
  assert.equal(held[0], 'end');
  drop();
});

test('releasing Option restores the glyphs and clears the mark', () => {
  const {el} = row();
  const resting = glyphs(el);
  hold({key: 'Alt', altKey: true});
  drop();
  assert.equal(el.dataset.mod, undefined);
  assert.deepEqual(glyphs(el), resting);
});

test('Cmd and Shift together are the third state', () => {
  const {el} = row();
  hold({key: 'Meta', metaKey: true, shiftKey: true});
  assert.equal(el.dataset.mod, 'cmdshift');
  assert.equal(navMode(el.querySelector('.sbback .g')), 'newtab');
  // Cmd alone claims nothing here, so it must read as no modifier at all.
  hold({key: 'Meta', metaKey: true});
  assert.equal(el.dataset.mod, undefined);
  drop();
});

// Chromium's own new-window modifier. Claiming it would take a click the user
// meant for a link out from under them.
test('Shift alone is not a modifier state', () => {
  const {el, sent} = row();
  hold({key: 'Shift', shiftKey: true});
  assert.equal(el.dataset.mod, undefined);
  assert.deepEqual(glyphs(el), ['plain', 'plain', '']);
  click(el.querySelector('.sbfwd'));
  drop();
  assert.deepEqual(sent, [['sidebarForwardPressed', []]]);
});

// An app switch can swallow the release, and the VM harness always does. The
// row would then offer the wider function to every later click.
test('a bare click heals a state whose release never arrived', () => {
  const {el, sent} = row();
  hold({key: 'Alt', altKey: true});
  assert.equal(el.dataset.mod, 'option');
  click(el.querySelector('.sbfwd'));
  assert.equal(el.dataset.mod, undefined, 'the stale state survived a click');
  assert.deepEqual(sent, [['sidebarForwardPressed', []]],
                   'the click ran the modifier function it no longer carried');
  assert.deepEqual(glyphs(el), ['plain', 'plain', '']);
});

test('a press heals before back decides what to send', () => {
  const {el, sent} = row();
  hold({key: 'Alt', altKey: true});
  press(el.querySelector('.sbback'));
  assert.deepEqual(sent, [['sidebarBackPressed', []]]);
  assert.equal(el.dataset.mod, undefined);
});

test('a click that still carries the modifier keeps the row armed', () => {
  const {el} = row();
  hold({key: 'Alt', altKey: true});
  click(el.querySelector('.sbfwd'), {altKey: true});
  assert.equal(el.dataset.mod, 'option', 'the heal disarmed a live modifier');
  drop();
});

test('an ordinary keydown heals a stale state', () => {
  const {el} = row();
  hold({key: 'Alt', altKey: true});
  window.dispatchEvent(new Event('keydown', {key: 'j'}));
  assert.equal(el.dataset.mod, undefined);
});

test('a window blur drops a held modifier', () => {
  const {el} = row();
  hold({key: 'Alt', altKey: true});
  window.dispatchEvent(new Event('blur'));
  assert.equal(el.dataset.mod, undefined,
               'a key released off-window never delivers its keyup');
  drop();
});

test('a hidden surface drops a held modifier too', () => {
  const {el} = row();
  hold({key: 'Alt', altKey: true});
  document.dispatchEvent(new Event('visibilitychange'));
  assert.equal(el.dataset.mod, undefined);
  drop();
});

// The native path. AtriumBrowserWindow::SetSidebarNavModifiers pushes here
// from the flagsChanged monitor, because the sidebar only receives key events
// while it holds the keyboard - which it does not while someone reads a page.
function installedRow() {
  h.reset();
  const sent = [];
  const root = document.createElement('div');
  root.id = 'atrium-root';
  document.body.appendChild(root);
  const el = installSidebarNavRow(
      root, {send: (name, args) => sent.push([name, args])});
  return {el, sent, root};
}

test('the native push arms and disarms the row', () => {
  const {el} = installedRow();
  assert.equal(typeof window.atriumSetNavModifiers, 'function',
               'installing the row must publish the receiver');
  window.atriumSetNavModifiers(false, false, true);
  assert.equal(el.dataset.mod, 'option');
  assert.equal(navMode(el.querySelector('.sbback .g')), 'end');
  window.atriumSetNavModifiers(true, true, false);
  assert.equal(el.dataset.mod, 'cmdshift');
  window.atriumSetNavModifiers(false, false, false);
  assert.equal(el.dataset.mod, undefined);
});

// The native push runs apply() directly with no DOM keydown at all.
test('an open hover hint follows the native push too, with no keydown at all', () => {
  const {el, sent} = installedRow();
  const clock = makeFakeClock();
  const {dispose} = installHintTriggers(document.body, {
    send: (name, args) => sent.push([name, args]), clock, nativeHint: false,
  });
  const reload = el.querySelector('.sbreload');
  reload.dispatchEvent(new Event('pointerover', {bubbles: true}));
  clock.advance(600);
  assert.equal(currentHint().querySelector('.hint-text').textContent, 'Reload');
  window.atriumSetNavModifiers(false, false, true);
  assert.equal(
      currentHint().querySelector('.hint-text').textContent,
      'Hard reload, ignoring the cache',
      'paint() ran from the native push alone, no keydown fired');
  assert.equal(currentHint().querySelector('.hint-keys'), null,
               'the option variant has no key of its own');
  window.atriumSetNavModifiers(false, false, false);
  assert.equal(currentHint().querySelector('.hint-text').textContent, 'Reload');
  dispose();
});

test('neither Cmd nor Shift alone over the native push is a state', () => {
  const {el} = installedRow();
  window.atriumSetNavModifiers(false, true, false);
  assert.equal(el.dataset.mod, undefined);
  window.atriumSetNavModifiers(true, false, false);
  assert.equal(el.dataset.mod, undefined);
});

test('a click after a native push runs the pushed function', () => {
  const {el, sent} = installedRow();
  window.atriumSetNavModifiers(false, false, true);
  click(el.querySelector('.sbfwd'), {altKey: true});
  window.atriumSetNavModifiers(false, false, false);
  assert.deepEqual(sent, [['sidebarForwardToEnd', []]]);
});

test('a reinstalled row takes the receiver with it', () => {
  const {el, root} = installedRow();
  const second = installSidebarNavRow(root, {send: () => {}});
  window.atriumSetNavModifiers(false, false, true);
  assert.equal(el.dataset.mod, undefined, 'the replaced row still answers');
  assert.equal(second.dataset.mod, 'option');
  window.atriumSetNavModifiers(false, false, false);
});

test('an ordinary key does not arm the row, even under Option', () => {
  const {el} = row();
  hold({key: 'K', altKey: true});
  assert.equal(el.dataset.mod, undefined,
               'typing an accented letter is not a request for the wider function');
  drop();
});

test('the glyph swap leaves the group anchor in place', () => {
  const {el} = row();
  const back = el.querySelector('.sbback');
  hold({key: 'Alt', altKey: true});
  assert.ok(back.classList.contains('first'),
            'back stopped anchoring the right group mid-swap');
  drop();
  assert.ok(back.classList.contains('first'));
});

test('a click while a modifier is held runs that state\'s function', () => {
  for (const [chord, expected] of [
         [{altKey: true}, ['sidebarForwardToEnd', 'sidebarReloadHard']],
         [{metaKey: true, shiftKey: true},
          ['sidebarForwardInNewTab', 'sidebarReloadClearingSite']],
       ]) {
    const {el, sent} = row();
    hold(chord);
    click(el.querySelector('.sbfwd'), chord);
    click(el.querySelector('.sbreload'), chord);
    drop();
    assert.deepEqual(sent.map(([name]) => name), expected);
  }
});

test('back sends its modifier function from the press path', () => {
  for (const [chord, expected] of [
         [{altKey: true}, 'sidebarBackToStart'],
         [{metaKey: true, shiftKey: true}, 'sidebarBackInNewTab'],
       ]) {
    const {el, sent} = row();
    hold(chord);
    press(el.querySelector('.sbback'), chord);
    drop();
    assert.deepEqual(sent, [[expected, []]]);
  }
});

test('a plain click still sends the plain message', () => {
  const {el, sent} = row();
  hold({altKey: true});
  drop();
  click(el.querySelector('.sbfwd'));
  assert.deepEqual(sent, [['sidebarForwardPressed', []]]);
});

test('the modifier glyph swap keeps the animation wrapper', () => {
  const {el} = row();
  hold({altKey: true});
  click(el.querySelector('.sbfwd'), {altKey: true});
  assert.ok(el.querySelector('.sbfwd .g').classList.contains('nudgeR'),
            'the swap must not replace .g, which owns the animations');
  drop();
});

test('every modifier message is registered in kSurfaceMessages', () => {
  const {el, sent} = row();
  for (const chord of [{altKey: true}, {metaKey: true, shiftKey: true}]) {
    hold(chord);
    for (const button of el.querySelectorAll('.nb')) {
      click(button, chord);
    }
    press(el.querySelector('.sbback'), chord);
    drop();
  }
  const names = new Set(sent.map(([name]) => name));
  for (const name of ['sidebarBackInNewTab', 'sidebarBackToStart',
                      'sidebarForwardInNewTab', 'sidebarForwardToEnd',
                      'sidebarReloadHard', 'sidebarReloadClearingSite']) {
    assert.ok(names.has(name), `${name} was never sent`);
    assert.ok(REGISTERED.has(name), `${name} is not in kSurfaceMessages`);
  }
  assert.equal(REGISTERED.has('sidebarReloadAllTabs'), false,
               'no state reaches reload-all any more, so its name is dead');
});

// a modifier swap no longer replaces the glyph in one frame -
// the outgoing glyph fades out through a ghost sibling while .g fades in.

test('a modifier press moves the outgoing glyph into a fading ghost', () => {
  const {el} = row();
  const fwd = el.querySelector('.sbfwd');
  hold({altKey: true});
  const ghost = fwd.querySelector('.gl.ghost');
  assert.ok(ghost, 'no ghost span left holding the outgoing glyph');
  assert.equal(navMode(ghost), 'plain', 'the ghost must carry the OLD glyph');
  assert.equal(ghost.classList.contains('out'), false,
               'the fade class must wait for its own frame, not land with the ghost');
  h.tick();
  assert.ok(ghost.classList.contains('out'));
  assert.equal(navMode(fwd.querySelector('.g')), 'end',
               '.g must already read the new glyph, ghost or not');
  drop();
});

test('reduced motion swaps the glyph with no ghost at all', () => {
  h.reset();
  const el = buildNavRow({send: () => {}, reducedMotion: true});
  document.body.appendChild(el);
  hold({altKey: true});
  assert.equal(el.querySelector('.sbfwd .gl.ghost'), null);
  assert.equal(navMode(el.querySelector('.sbfwd .g')), 'end');
  drop();
});

test('transitionend on the ghost clears it', () => {
  const {el} = row();
  hold({altKey: true});
  const ghost = el.querySelector('.sbfwd .gl.ghost');
  ghost.dispatchEvent(new Event('transitionend', {propertyName: 'opacity'}));
  assert.equal(el.querySelector('.sbfwd .gl.ghost'), null);
  drop();
});

test('a transitionend for another property leaves the ghost alone', () => {
  const {el} = row();
  hold({altKey: true});
  const ghost = el.querySelector('.sbfwd .gl.ghost');
  ghost.dispatchEvent(new Event('transitionend', {propertyName: 'transform'}));
  assert.ok(el.querySelector('.sbfwd .gl.ghost'),
            'only the opacity fade this ghost runs should clear it');
  drop();
});

test('a fade that never ends is cleared by the fallback timer', async () => {
  const {el} = row();
  hold({altKey: true});
  assert.ok(el.querySelector('.sbfwd .gl.ghost'));
  await new Promise((resolve) => setTimeout(resolve, 250));
  assert.equal(el.querySelector('.sbfwd .gl.ghost'), null,
               'the fallback must clear a ghost transitionend never reached');
  drop();
});

test('a second press before the first fade ends replaces the ghost', () => {
  const {el} = row();
  hold({altKey: true});
  hold({metaKey: true, shiftKey: true});
  assert.equal(el.querySelectorAll('.sbfwd .gl.ghost').length, 1,
               'a stale ghost must not stack up behind a new one');
  assert.equal(navMode(el.querySelector('.sbfwd .gl.ghost')), 'end',
               'the ghost now fading out is the OPTION glyph, not the plain one');
  assert.equal(navMode(el.querySelector('.sbfwd .g')), 'newtab');
  drop();
});

test('the CSS fades .g in and the ghost out over the plate\'s own .12s', () => {
  assert.match(css, /\.sbnav \.nb\s*\{[^}]*transition:\s*background-color \.12s,\s*color \.12s/s);
  assert.match(css,
      /\.sbnav \.nb \.g\.gswap,\s*\n\.sbnav \.nb \.gl\.ghost\s*\{[^}]*transition:\s*opacity \.12s/s);
  assert.match(css, /\.sbnav \.nb \.g\.gswap\.entering\s*\{[^}]*opacity:\s*0/s);
  assert.match(css, /\.sbnav \.nb \.gl\.ghost\.out\s*\{[^}]*opacity:\s*0/s);
});

test('prefers-reduced-motion drops the plate and glyph transitions', () => {
  const block = css.match(
      /@media \(prefers-reduced-motion: reduce\)\s*\{([^]*?)\n\}\n/)[1];
  assert.match(block, /\.sbnav \.nb\s*\{[^}]*transition:\s*none/s);
  assert.match(block, /\.sbnav \.nb \.g\.gswap,\s*\n\s*\.sbnav \.nb \.gl\.ghost\s*\{[^}]*transition:\s*none/s);
});

test('a replaced row stops listening for modifiers', () => {
  h.reset();
  const root = document.createElement('div');
  document.body.appendChild(root);
  const first = installSidebarNavRow(root, {send: () => {}});
  installSidebarNavRow(root, {send: () => {}});
  hold({altKey: true});
  assert.equal(first.dataset.mod, undefined,
               'the orphaned row still holds a window listener');
  assert.equal(document.body.querySelector('.sbnav').dataset.mod, 'option');
  drop();
});

// A minimal EventTarget that honours the real DOM contract: a capture-phase
// listener is a distinct registration from a bubble-phase one, and
// removeEventListener only drops the one whose capture flag matches.
function captureAwareTarget() {
  const listeners = new Map();
  return {
    dataset: {},
    addEventListener(type, fn, options) {
      const capture = options === true || !!(options && options.capture);
      const list = listeners.get(type) || [];
      list.push({fn, capture});
      listeners.set(type, list);
    },
    removeEventListener(type, fn, options) {
      const capture = options === true || !!(options && options.capture);
      const list = listeners.get(type) || [];
      listeners.set(
          type, list.filter((l) => l.fn !== fn || l.capture !== capture));
    },
    dispatch(type, event) {
      for (const {fn} of listeners.get(type) || []) {
        fn(event);
      }
    },
  };
}

test('dispose releases the row\'s captured pointerdown/click healers', () => {
  const target = captureAwareTarget();
  const tracker = createModifierTracker(target);
  hold({key: 'Alt', altKey: true});
  assert.equal(target.dataset.mod, 'option');
  tracker.dispose();
  target.dispatch('pointerdown', {});
  assert.equal(target.dataset.mod, 'option',
               'a disposed tracker must not still heal from a captured pointerdown');
});

test('the CSS gives each modifier state its own colour', () => {
  assert.match(css, /\.sbnav\[data-mod="option"\][^{]*\{[^}]*color:/s);
  assert.match(css, /\.sbnav\[data-mod="cmdshift"\][^{]*\{[^}]*color:/s);
  const option = css.match(/\.sbnav\[data-mod="option"\][^{]*\{([^}]*)\}/s)[1];
  const cmdshift =
      css.match(/\.sbnav\[data-mod="cmdshift"\][^{]*\{([^}]*)\}/s)[1];
  assert.notEqual(option, cmdshift, 'the two states must not look alike');
  assert.equal(css.includes('data-mod="shift"'), false,
               'a stale Shift rule would paint a state the row never enters');
});

// A translucent wash reads under 2:1 against a light sidebar tint, so the
// plate is opaque and the ink dark enough to clear 3:1 regardless of the
// tint underneath (measured in the modifier-glyphs-read-on-the-tint scenario).
test('the modifier plate is filled, not a translucent wash', () => {
  assert.match(css, /\.sbnav\[data-mod="option"\][^{]*\{[^}]*background:\s*#e2a648/s);
  assert.match(css, /\.sbnav\[data-mod="cmdshift"\][^{]*\{[^}]*background:\s*#ff5f57/s);
  assert.equal(/\.sbnav\[data-mod="option"\][^{]*\{[^}]*rgba\(/s.test(css), false,
               'a translucent wash lets the tint back in');
  assert.equal(/\.sbnav\[data-mod="cmdshift"\][^{]*\{[^}]*rgba\(/s.test(css), false,
               'a translucent wash lets the tint back in');
});

test('the modifier ink is dark enough to clear the plate', () => {
  assert.match(css, /\.sbnav\[data-mod="option"\][^{]*\{[^}]*color:\s*#6b4700/s);
  assert.match(css, /\.sbnav\[data-mod="cmdshift"\][^{]*\{[^}]*color:\s*#701400/s);
  assert.match(css, /\.sbnav\[data-mod="option"\][^{]*\{[^}]*font-weight:\s*600/s);
  assert.match(css, /\.sbnav\[data-mod="cmdshift"\][^{]*\{[^}]*font-weight:\s*600/s);
});

// the row collapses instead of clipping. The measurements are the
// row's own, so the C++ carve and this ladder must not drift apart.
const CARVE_CC = fs.readFileSync(
    path.join(SRC, 'chrome/browser/ui/atrium/atrium_nav_carve_decision.cc'),
    'utf8');

const carveConstant = (name) =>
    Number(CARVE_CC.match(new RegExp(`constexpr int ${name} = (\\d+);`))[1]);

// Widths the toggle plus the group occupy at each rung of the ladder.
const ALL_THREE = 94;
const TWO_AND_MENU = 88;
const ONE_AND_MENU = 64;
// Below TWO_AND_MENU the toggle yields its slot: back and forward stay up
// and Hide sidebar moves into the menu. A toggle and a nav button are both
// 22px, so the swap costs no width and the rung boundaries do not move.
const BACK_AND_MENU = 40;

const collapsed = (el, width) => {
  layoutNavRow(el, width);
  return el;
};

const shown = (el) => el.querySelectorAll('.nb')
    .filter((b) => !b.classList.contains('navgone'))
    .map((b) => b.className.split(' ').find((c) => c.startsWith('sb')));

test('every button stays visible while the group still fits', () => {
  assert.deepEqual(visibleNavButtons(ALL_THREE),
                   ['sbtoggle', 'sbback', 'sbfwd', 'sbreload']);
  assert.deepEqual(visibleNavButtons(ALL_THREE + 40),
                   ['sbtoggle', 'sbback', 'sbfwd', 'sbreload']);
});

// Reload is the least useful control to keep once the row runs out of room,
// then the toggle goes: back and forward are the pair a narrow row exists to
// keep, and Hide sidebar still has Cmd+S and a menu row.
test('the row sheds reload, then the toggle, then forward', () => {
  assert.deepEqual(visibleNavButtons(ALL_THREE - 1),
                   ['sbtoggle', 'sbback', 'sbfwd']);
  assert.deepEqual(visibleNavButtons(TWO_AND_MENU),
                   ['sbtoggle', 'sbback', 'sbfwd']);
  assert.deepEqual(visibleNavButtons(TWO_AND_MENU - 1), ['sbback', 'sbfwd']);
  assert.deepEqual(visibleNavButtons(ONE_AND_MENU), ['sbback', 'sbfwd']);
  assert.deepEqual(visibleNavButtons(ONE_AND_MENU - 1), ['sbback']);
  assert.deepEqual(visibleNavButtons(BACK_AND_MENU), ['sbback']);
  assert.deepEqual(visibleNavButtons(BACK_AND_MENU - 1), []);
  assert.deepEqual(visibleNavButtons(0), []);
});

test('every rung of the ladder keeps back, then adds forward, then reload',
     () => {
       assert.deepEqual(visibleNavButtons(BACK_AND_MENU), ['sbback']);
       assert.deepEqual(visibleNavButtons(ONE_AND_MENU), ['sbback', 'sbfwd']);
       assert.deepEqual(
           visibleNavButtons(ALL_THREE),
           ['sbtoggle', 'sbback', 'sbfwd', 'sbreload']);
     });

// The report was Hide sidebar sitting where forward belongs. kMinSidebarWidth
// is 160 (atrium_browser_window.cc:388), and the dots and insets leave the row
// no less than ONE_AND_MENU there.
test('both nav directions survive at the narrowest sidebar', () => {
  for (let width = ONE_AND_MENU; width <= 160; width++) {
    const visible = visibleNavButtons(width);
    assert.ok(visible.includes('sbback'), `back missing at ${width}`);
    assert.ok(visible.includes('sbfwd'), `forward missing at ${width}`);
  }
});

test('the ladder never asks for more room than it has', () => {
  const {button, gap, toggle, more, count} = NAV_METRICS;
  for (let width = 20; width <= 120; width++) {
    const visible = visibleNavButtons(width);
    if (visible.length === 0) {
      continue;
    }
    const hasToggle = visible.includes('sbtoggle');
    const nav = visible.length - (hasToggle ? 1 : 0);
    const full = hasToggle && nav === count;
    const items = visible.length + (full ? 0 : 1);
    const needed = (hasToggle ? toggle : 0) + nav * button +
        (full ? 0 : more) + (items - 1) * gap;
    assert.ok(needed <= width,
              `${visible.join('+')} needs ${needed} but only ${width} is free`);
  }
});

test('the native carve reads the same constants as this ladder', () => {
  assert.equal(carveConstant('kNavButtonWidth'), NAV_METRICS.button);
  assert.equal(carveConstant('kNavButtonGap'), NAV_METRICS.gap);
  assert.equal(carveConstant('kNavToggleWidth'), NAV_METRICS.toggle);
  assert.equal(carveConstant('kNavMoreButtonWidth'), NAV_METRICS.more);
  assert.equal(carveConstant('kNavGroupButtonCount'), NAV_METRICS.count);
});

// 14 and 16 draw the same 12px of ink, the lab's own relation between the two
// icons (12 and 14 in its 21px box, swipe-lab.html:2884, 2907). The reload box
// is 12 user units against the toggle's 16, so the two sizes cannot match.
test('the reload and toggle SVGs are drawn at one ink height', () => {
  const {el} = row();
  const reload = el.querySelector('.sbreload svg');
  assert.equal(reload.getAttribute('width'), '14');
  assert.equal(reload.getAttribute('height'), '14');
  const toggle = el.querySelector('.sbtoggle svg');
  assert.equal(toggle.getAttribute('width'), '16');
  assert.equal(toggle.getAttribute('height'), '16');
});

// back and forward were font characters, which sit by their face's
// metrics. Nothing else here would catch one falling back to text.
test('back and forward draw on the toggle\'s own box in every state', () => {
  const {el} = row();
  for (const state of ['plain', 'option', 'cmdshift']) {
    if (state !== 'plain') {
      hold(state === 'option' ? {altKey: true} : {metaKey: true, shiftKey: true});
    }
    for (const sel of ['.sbback', '.sbfwd']) {
      const svg = el.querySelector(`${sel} .g svg`);
      assert.ok(svg, `${sel} draws no SVG in the ${state} state`);
      assert.equal(svg.getAttribute('viewBox'), '0 0 16 16');
      assert.equal(svg.getAttribute('width'), '16');
      assert.equal(svg.getAttribute('height'), '16');
    }
  }
  drop();
});

// One drawing reflected, so a change to back reaches forward by construction.
test('forward is back reflected, not a second drawing', () => {
  const {el} = row();
  const paths = (sel) => [...el.querySelectorAll(`${sel} .g svg path`)]
      .map((node) => node.getAttribute('d'));
  assert.deepEqual(paths('.sbfwd'), paths('.sbback'));
  assert.equal(el.querySelector('.sbfwd .g svg g').getAttribute('transform'),
               'translate(16,0) scale(-1,1)');
  assert.equal(el.querySelector('.sbback .g svg g'), null);
});

// The arrowhead reaches higher than the arc's foot drops, so the drawing is
// not centred in a 0 0 12 12 box - it lands 0.66 units high, which put the
// arc a whole pixel above the text glyphs beside it.
test('the reload viewBox pays back the arc\'s own offset', () => {
  const {el} = row();
  assert.equal(el.querySelector('.sbreload svg').getAttribute('viewBox'),
               '0 -0.66 12 12');
});

// One line per child, attributes sorted so a reordered builder still reads the
// same. The shim carries no outerHTML, and its attribute map is the only
// complete account of what a node was given.
const drawing = (svg) => svg.children.map(
    (node) => [node.tagName.toLowerCase(),
               ...[...node._attrs].sort(([a], [b]) => (a < b ? -1 : 1))
                   .map(([k, v]) => `${k}="${v}"`)]
        .join(' '));

const RELOAD_ARC =
    'path d="M9.86 4.96A4 4 0 1 1 7.04 2.14" fill="none" ' +
    'stroke="currentColor" stroke-linecap="round" stroke-width="1.3"';
const RELOAD_HEAD =
    'path d="M5.25 2.79L7.04 2.14L5.82 0.68" fill="none" ' +
    'stroke="currentColor" stroke-linecap="round" stroke-linejoin="round" ' +
    'stroke-width="1.3"';

test('the plain reload draws the lab\'s arc and its arrowhead', () => {
  const {el} = row();
  assert.deepEqual(drawing(el.querySelector('.sbreload svg')),
                   [RELOAD_ARC, RELOAD_HEAD]);
});

// swipe-lab.html:2903. Severity is the glyph as much as the plate: a bolt
// inside the arc is what the lab's mode-hard draws.
test('Option marks the arc with the lab\'s bolt', () => {
  const {el} = row();
  hold({key: 'Alt', altKey: true});
  assert.deepEqual(drawing(el.querySelector('.sbreload svg')), [
    RELOAD_ARC, RELOAD_HEAD,
    'path class="rlbolt" d="M7.1 3.3L4.5 6.5H5.9L4.9 8.7L7.5 5.4H6.1Z" ' +
        'fill="currentColor"',
  ]);
  drop();
});

// swipe-lab.html:2904-2906 plus its .rldash. The arc doubles as the cookie the
// crumbs fall out of, and the break in it reads as continuity discarded.
test('Cmd+Shift dashes the arc and drops the lab\'s three crumbs', () => {
  const {el} = row();
  hold({key: 'Meta', metaKey: true, shiftKey: true});
  assert.deepEqual(drawing(el.querySelector('.sbreload svg')), [
    'path d="M9.86 4.96A4 4 0 1 1 7.04 2.14" fill="none" ' +
        'stroke="currentColor" stroke-dasharray="2.7 1.6" ' +
        'stroke-linecap="round" stroke-width="1.3"',
    RELOAD_HEAD,
    'circle class="rlcrumb" cx="4.95" cy="5.3" fill="currentColor" r=".85"',
    'circle class="rlcrumb" cx="7.15" cy="6.15" fill="currentColor" r=".78"',
    'circle class="rlcrumb" cx="5.7" cy="7.6" fill="currentColor" r=".7"',
  ]);
  drop();
});

// The lab draws its back and forward as characters and gives them no modifier
// mode of its own (swipe-lab.html:3662), so those two keep their own ladder.
test('back and forward rest in their plain state', () => {
  const {el} = row();
  assert.deepEqual(glyphs(el).slice(0, 2), ['plain', 'plain']);
});

test('the glyph wrapper carries no overflow clip', () => {
  for (const selector of ['\\.sbnav \\.nb\\b', '\\.sbnav \\.nb \\.g\\b',
                          '\\.sbnav \\.nb \\.g svg\\b']) {
    const rule = css.match(new RegExp(`${selector}\\s*\\{([^}]*)\\}`, 's'));
    assert.ok(rule, `${selector} has no rule of its own`);
    assert.equal(/overflow:/.test(rule[1]), false,
                 `${selector} clips the larger glyph`);
  }
});

// The carve derives the row's budget from sidebar_width alone, so the two
// insets it subtracts have to be the ones the CSS actually lays the row out
// with - the page measures itself and would never notice the disagreement.
test('the native carve subtracts the insets the CSS really uses', () => {
  const clearance = css.match(
      /left:\s*var\(--atrium-traffic-light-clearance,\s*(\d+)px\)/);
  const inset = css.match(/\.sbnav\s*\{[^}]*?right:\s*(\d+)px/s);
  assert.ok(clearance && inset, 'the row no longer states its own insets');
  assert.equal(carveConstant('kTrafficLightClearance'), Number(clearance[1]));
  assert.equal(carveConstant('kNavGroupRightInset'), Number(inset[1]));
});

test('the clearance fallback matches the token it stands in for', () => {
  const tokens =
      fs.readFileSync(path.join(ATRIUM_DIR, 'tokens.css'), 'utf8');
  const token =
      tokens.match(/--atrium-traffic-light-clearance:\s*(\d+)px/)[1];
  assert.equal(carveConstant('kTrafficLightClearance'), Number(token));
});

test('the dots gap the row measures with is the one the CSS leaves', () => {
  const gap = css.match(/\.sbnav \.sbtd\s*\{[^}]*?margin-right:\s*(\d+)px/s);
  assert.ok(gap, '.sbtd no longer states its gap to the toggle');
  assert.equal(Number(gap[1]), NAV_METRICS.dotsGap);
});

test('a layout at the full width leaves the row alone', () => {
  const {el} = row();
  collapsed(el, ALL_THREE);
  assert.deepEqual(shown(el), ['sbtoggle', 'sbback', 'sbfwd', 'sbreload']);
  assert.equal(el.querySelector('.sbmore'), null);
});

test('the menu button appears once a button is hidden', () => {
  const {el} = row();
  collapsed(el, TWO_AND_MENU);
  assert.deepEqual(shown(el), ['sbtoggle', 'sbback', 'sbfwd', 'sbmore']);
  const more = el.querySelector('.sbmore');
  assert.ok(more, 'nothing offers the hidden action');
  assert.equal(more.textContent.includes('…'), false,
               'the overflow glyph must not be an ellipsis character emoji');
});

test('the hidden button keeps its slot out of the flex row', () => {
  const {el} = row();
  collapsed(el, TWO_AND_MENU);
  assert.ok(el.querySelector('.sbreload').classList.contains('navgone'));
  assert.match(css, /\.sbnav \.nb\.navgone\s*\{[^}]*display:\s*none/s);
});

test('back keeps the group anchor as reload and forward drop out', () => {
  const {el} = row();
  collapsed(el, TWO_AND_MENU);
  assert.ok(el.querySelector('.sbback').classList.contains('first'),
            'back is still the leftmost visible control');
  collapsed(el, BACK_AND_MENU - 1);
  assert.ok(el.querySelector('.sbmore').classList.contains('first'),
            'with nothing visible, the menu button takes the anchor');
});

test('the anchor never lands on the toggle', () => {
  const {el} = row();
  collapsed(el, ONE_AND_MENU);
  assert.ok(el.querySelector('.sbback').classList.contains('first'),
            'back anchors the group once the toggle is shed');
  assert.equal(el.querySelector('.sbtoggle').classList.contains('first'),
               false, 'an anchored toggle would drag the group off its edge');
});

test('the menu lists the hidden actions under their plain labels', () => {
  const {el} = row();
  collapsed(el, ONE_AND_MENU);
  click(el.querySelector('.sbmore'));
  // The label span, not the whole item: a row also carries its glyph and its
  // keycaps now, and textContent would run all three together.
  const items = el.querySelectorAll('.navmenu .ctxitem');
  assert.deepEqual(items.map((n) => n.querySelector('.ctxlabel').textContent),
                   ['Reload', 'Hide sidebar']);
});

// .ctxmenu is layout only and says so. Without .ov-card this menu painted no
// background at all and read as unbordered text over the tab list.
test('the nav menu and its rows wear the shared overlay classes', () => {
  const {el} = row();
  collapsed(el, ONE_AND_MENU);
  click(el.querySelector('.sbmore'));
  assert.ok(el.querySelector('.navmenu').classList.contains('ov-card'),
            'the menu takes its surface, border, radius and shadow from ov-card');
  for (const item of el.querySelectorAll('.navmenu .ctxitem')) {
    assert.ok(item.classList.contains('ov-row'),
              'a row takes its padding, radius and hover from ov-row');
  }
});

// The address row is a z-index 5 sibling later in the tree, so at an equal
// z-index it painted over the open menu, and this bar's own z-index kept
// .ctxmenu's 50 from climbing out to beat it.
test('the bar outranks the address row only while a menu is open', () => {
  const css = fs.readFileSync(
      path.join(ATRIUM_DIR, 'sidebar-nav-row.css'), 'utf8')
      .replace(/\/\*[\s\S]*?\*\//g, '');
  const open = /\.sbnav:has\(\.navmenu\)\s*{([^}]*)}/.exec(css);
  assert.ok(open, 'nothing lifts the bar while its menu is open');
  const lifted = /z-index:\s*(\d+)/.exec(open[1]);
  assert.ok(lifted, 'the open-menu rule sets no z-index');
  const resting = /\.sbnav\s*{[^}]*?z-index:\s*(\d+)/s.exec(css);
  assert.ok(resting, 'the bar has no resting z-index');
  assert.ok(Number(lifted[1]) > Number(resting[1]),
            `the lift must outrank the resting bar, got ${lifted[1]} over ${resting[1]}`);
  // Scoped, or the band's resting order moves with it.
  assert.equal(Number(resting[1]), 5,
               'the resting bar has left the band the address row shares');
});

// The menu listed bare labels: no glyph, and no key beside an action that has
// one. Both halves matter, and the keys must come from the resolver the hover
// hint reads or the two can name different keys for one button.
test('a menu row carries its glyph and its key', () => {
  const {el} = row();
  setBindings({toggleSidebar: '⌘S'});
  collapsed(el, ONE_AND_MENU);
  click(el.querySelector('.sbmore'));
  const items = [...el.querySelectorAll('.navmenu .ctxitem')];
  assert.equal(items.length, 2);
  for (const item of items) {
    assert.ok(item.querySelector('.g'), 'a row with no glyph');
  }
  const keysOf = (item) => [...item.querySelectorAll('.ctxkey .ov-key')]
      .map((k) => k.textContent);
  // Reload's key is Chromium's own, the toggle's comes from the registry, so
  // one test covers both sources.
  assert.deepEqual(keysOf(items[0]), ['⌘', 'R']);
  assert.deepEqual(keysOf(items[1]), ['⌘', 'S']);
});

test('a menu row with no key of its own shows no keycap', () => {
  const {el} = row();
  // Nothing bound, so the toggle falls through to no key at all - and an empty
  // keycap would read as a broken glyph.
  setBindings({});
  collapsed(el, ONE_AND_MENU);
  click(el.querySelector('.sbmore'));
  const toggle = [...el.querySelectorAll('.navmenu .ctxitem')][1];
  assert.equal(toggle.querySelector('.ctxlabel').textContent, 'Hide sidebar');
  assert.equal(toggle.querySelector('.ctxkey'), null);
});

test('the shed toggle still hides the sidebar from the menu', () => {
  const {el, sent} = row();
  collapsed(el, ONE_AND_MENU);
  click(el.querySelector('.sbmore'));
  click(el.querySelectorAll('.navmenu .ctxitem')[1]);
  assert.deepEqual(sent, [['sidebarToggleRequested', []]]);
  assert.equal(el.querySelector('.navmenu'), null);
});

test('a menu action sends the plain message and closes the menu', () => {
  const {el, sent} = row();
  collapsed(el, ONE_AND_MENU);
  click(el.querySelector('.sbmore'));
  click(el.querySelectorAll('.navmenu .ctxitem')[0]);
  assert.deepEqual(sent, [['sidebarReloadPressed', []]]);
  assert.equal(el.querySelector('.navmenu'), null);
});

test('every menu message is registered in kSurfaceMessages', () => {
  const {el, sent} = row();
  collapsed(el, ONE_AND_MENU - 1);
  // Each pick closes the menu, so the next one needs a freshly built list.
  for (let at = 0; at < 3; at++) {
    click(el.querySelector('.sbmore'));
    click(el.querySelectorAll('.navmenu .ctxitem')[at]);
  }
  assert.equal(sent.length, 3);
  for (const [name] of sent) {
    assert.ok(REGISTERED.has(name), `${name} is not in kSurfaceMessages`);
  }
});

// the menu stands in for controls that change function under a
// modifier. A row that keeps saying Reload while Option is down names one
// action and runs another.
const menuRows = (el) => [...el.querySelectorAll('.navmenu .ctxitem')];
const menuLabels = (el) =>
    menuRows(el).map((n) => n.querySelector('.ctxlabel').textContent);
// Below BACK_AND_MENU nothing but the dots and the menu button survive, so
// the menu carries all four rows and back's own variants are reachable.
const openFullMenu = (el) => {
  collapsed(el, BACK_AND_MENU - 1);
  click(el.querySelector('.sbmore'));
  return el;
};

test('holding Option relabels every menu row that has an option variant', () => {
  const {el} = row();
  openFullMenu(el);
  const resting = menuLabels(el);
  hold({key: 'Alt', altKey: true});
  assert.deepEqual(menuLabels(el), [
    'Back to the first page of this tab',
    'Forward to the last page of this tab',
    'Hard reload, ignoring the cache',
    // The toggle has one function only, and must not fall off the list.
    'Hide sidebar',
  ]);
  drop();
  assert.deepEqual(menuLabels(el), resting, 'the release did not restore');
});

test('holding Option swaps the menu glyphs too', () => {
  const {el} = row();
  openFullMenu(el);
  const drawn = () => menuRows(el).map((n) => navMode(n.querySelector('.g')));
  const resting = drawn();
  hold({key: 'Alt', altKey: true});
  const held = drawn();
  assert.notDeepEqual(held, resting, 'the glyphs did not change');
  assert.deepEqual(held.slice(0, 2), ['end', 'end']);
  drop();
  assert.deepEqual(drawn(), resting);
});

test('holding Cmd+Shift relabels the menu to the widest variant', () => {
  const {el} = row();
  openFullMenu(el);
  hold({key: 'Meta', metaKey: true, shiftKey: true});
  assert.deepEqual(menuLabels(el).slice(0, 3), [
    'Back in a new tab',
    'Forward in a new tab',
    'Hard reload, clearing this site\'s cookies and data',
  ]);
  drop();
});

test('a menu click sends the variant on screen, not the plain one', () => {
  const {el, sent} = row();
  openFullMenu(el);
  hold({key: 'Alt', altKey: true});
  click(menuRows(el)[2], {altKey: true});
  assert.deepEqual(sent, [['sidebarReloadHard', []]]);
  assert.equal(el.querySelector('.navmenu'), null, 'the menu stayed open');
  drop();
});

test('a menu row in a modifier variant drops its keycap', () => {
  const {el} = row();
  openFullMenu(el);
  const reload = () => menuRows(el)[2];
  assert.ok(reload().querySelector('.ctxkey'), 'the plain row has no key');
  hold({key: 'Alt', altKey: true});
  // The hover hint already drops the key here: ⌘R runs the plain reload, and
  // a chip beside the hard one would name a key that does something else.
  assert.equal(reload().querySelector('.ctxkey'), null);
  drop();
  assert.ok(reload().querySelector('.ctxkey'), 'the key never came back');
});

// Every painter the tracker holds keeps its row alive, and a menu is built
// again on every open.
test('closing the menu releases its painters', () => {
  const {el} = row();
  openFullMenu(el);
  const stale = menuRows(el)[2];
  document.dispatchEvent(new Event('keydown', {key: 'Escape'}));
  hold({key: 'Alt', altKey: true});
  assert.equal(stale.querySelector('.ctxlabel').textContent, 'Reload',
               'a detached row is still being painted');
  drop();
});

test('Escape closes the menu', () => {
  const {el} = row();
  collapsed(el, ONE_AND_MENU);
  click(el.querySelector('.sbmore'));
  assert.ok(el.querySelector('.navmenu'));
  document.dispatchEvent(new Event('keydown', {key: 'Escape'}));
  assert.equal(el.querySelector('.navmenu'), null);
});

test('a press outside closes the menu', () => {
  const {el} = row();
  collapsed(el, ONE_AND_MENU);
  click(el.querySelector('.sbmore'));
  document.body.dispatchEvent(new Event('pointerdown', {bubbles: true}));
  assert.equal(el.querySelector('.navmenu'), null);
});

test('a collapse while the menu is open takes the menu with it', () => {
  const {el} = row();
  collapsed(el, ONE_AND_MENU);
  click(el.querySelector('.sbmore'));
  collapsed(el, ALL_THREE);
  assert.equal(el.querySelector('.navmenu'), null,
               'the menu outlived the button that owns it');
});

test('widening restores the buttons in order and drops the menu', () => {
  const {el} = row();
  collapsed(el, BACK_AND_MENU - 1);
  assert.deepEqual(shown(el), ['sbmore']);
  collapsed(el, BACK_AND_MENU);
  assert.deepEqual(shown(el), ['sbback', 'sbmore']);
  collapsed(el, ONE_AND_MENU);
  assert.deepEqual(shown(el), ['sbback', 'sbfwd', 'sbmore']);
  collapsed(el, TWO_AND_MENU);
  assert.deepEqual(shown(el), ['sbtoggle', 'sbback', 'sbfwd', 'sbmore']);
  collapsed(el, ALL_THREE);
  assert.deepEqual(shown(el), ['sbtoggle', 'sbback', 'sbfwd', 'sbreload']);
  assert.ok(el.querySelector('.sbback').classList.contains('first'));
});

test('the modifier variants still paint on the buttons that survive', () => {
  const {el, sent} = row();
  collapsed(el, ONE_AND_MENU);
  hold({key: 'Alt', altKey: true});
  assert.equal(el.dataset.mod, 'option');
  assert.equal(reloadMode(el), 'hard');
  click(el.querySelector('.sbreload'), {altKey: true});
  drop();
  assert.deepEqual(sent, [['sidebarReloadHard', []]]);
});

// A release lost to an app switch delivers no keyup, and the menu would offer
// the wider function for good. The click's own flags are the truth.
test('a menu click that no longer carries the modifier runs plain', () => {
  const {el, sent} = row();
  collapsed(el, ONE_AND_MENU);
  click(el.querySelector('.sbmore'));
  hold({key: 'Alt', altKey: true});
  // The label span, not the whole item: a row also carries its glyph and its
  // keycaps now, and textContent would run all three together.
  const items = el.querySelectorAll('.navmenu .ctxitem');
  assert.deepEqual(items.map((n) => n.querySelector('.ctxlabel').textContent),
                   ['Hard reload, ignoring the cache', 'Hide sidebar']);
  click(items[0]);
  drop();
  assert.deepEqual(sent, [['sidebarReloadPressed', []]]);
});

test('an unmeasured row keeps every button', () => {
  const {el} = row();
  layoutNavRow(el, 0);
  layoutNavRow(el);
  assert.deepEqual(shown(el), ['sbtoggle', 'sbback', 'sbfwd', 'sbreload'],
                   'a row with no layout yet must not collapse');
});

test('the row watches its own width', () => {
  const {el} = row();
  const live = h.resizeObservers.filter((o) => !o.disconnected);
  assert.equal(live.length, 1, 'nothing re-runs the ladder on a resize');
  assert.ok(live[0]._targets.has(el));
});

test('a replaced row stops watching', () => {
  h.reset();
  const root = document.createElement('div');
  document.body.appendChild(root);
  installSidebarNavRow(root, {send: () => {}});
  installSidebarNavRow(root, {send: () => {}});
  assert.equal(h.resizeObservers.filter((o) => !o.disconnected).length, 1,
               'the orphaned row still holds an observer');
});

test('the CSS gives the overflow button its narrower width', () => {
  const rule = css.match(/\.sbnav \.nb\.sbmore\s*\{([^}]*)\}/s);
  assert.ok(rule, 'the overflow button has no rule of its own');
  assert.match(rule[1], new RegExp(`width:\\s*${NAV_METRICS.more}px`));
});

test('the overflow button announces the menu it owns', () => {
  const {el} = row();
  collapsed(el, ONE_AND_MENU);
  const more = el.querySelector('.sbmore');
  assert.equal(more.getAttribute('aria-haspopup'), 'menu');
  assert.equal(more.getAttribute('aria-expanded'), 'false');
  click(more);
  assert.equal(more.getAttribute('aria-expanded'), 'true');
  click(more);
  assert.equal(more.getAttribute('aria-expanded'), 'false');
});

test('a pick leaves the overflow button reading closed', () => {
  const {el} = row();
  collapsed(el, ONE_AND_MENU);
  click(el.querySelector('.sbmore'));
  click(el.querySelectorAll('.navmenu .ctxitem')[0]);
  assert.equal(el.querySelector('.sbmore').getAttribute('aria-expanded'),
               'false');
});

test('the menu and its rows carry the menu roles', () => {
  const {el} = row();
  collapsed(el, ONE_AND_MENU);
  click(el.querySelector('.sbmore'));
  assert.equal(el.querySelector('.navmenu').getAttribute('role'), 'menu');
  const roles = el.querySelectorAll('.navmenu .ctxitem')
      .map((n) => n.getAttribute('role'));
  assert.deepEqual(roles, ['menuitem', 'menuitem']);
});

test('opening the menu moves focus onto its first row', () => {
  const {el} = row();
  collapsed(el, ONE_AND_MENU);
  click(el.querySelector('.sbmore'));
  assert.equal(document.activeElement,
               el.querySelectorAll('.navmenu .ctxitem')[0]);
});

test('Escape puts focus back on the overflow button', () => {
  const {el} = row();
  collapsed(el, ONE_AND_MENU);
  click(el.querySelector('.sbmore'));
  document.dispatchEvent(new Event('keydown', {key: 'Escape'}));
  assert.equal(document.activeElement, el.querySelector('.sbmore'),
               'the next Tab would start from the document');
});

test('Escape with no menu open leaves focus alone', () => {
  const {el} = row();
  collapsed(el, ONE_AND_MENU);
  el.querySelector('.sbreload').focus();
  document.dispatchEvent(new Event('keydown', {key: 'Escape'}));
  assert.equal(document.activeElement, el.querySelector('.sbreload'));
});

test('a window blur closes the menu', () => {
  const {el} = row();
  collapsed(el, ONE_AND_MENU);
  click(el.querySelector('.sbmore'));
  window.dispatchEvent(new Event('blur'));
  assert.equal(el.querySelector('.navmenu'), null);
});

test('a hidden surface closes the menu', () => {
  const {el} = row();
  collapsed(el, ONE_AND_MENU);
  click(el.querySelector('.sbmore'));
  document.dispatchEvent(new Event('visibilitychange'));
  assert.equal(el.querySelector('.navmenu'), null);
});

// A menu row overlapping the caption band is not merely awkward: the press
// starts a window drag and the row never gets its click.
test('the menu hangs clear of the window drag band', () => {
  const metrics = fs.readFileSync(
      path.join(SRC, 'chrome/browser/ui/atrium/atrium_window_metrics.h'),
      'utf8');
  const titlebar =
      Number(metrics.match(/kTitlebarHeight = (\d+)/)[1]);
  const rowTop = Number(
      css.match(/top:\s*var\(--atrium-traffic-light-band-inset,\s*(\d+)px\)/)[1]);
  const menuTop =
      Number(css.match(/\.sbnav \.navmenu\s*\{[^}]*?top:\s*(\d+)px/s)[1]);
  assert.ok(rowTop + menuTop > titlebar,
            `the menu starts at y ${rowTop + menuTop}, inside the caption`);
});

// with the floating address bar up, the devbar hosts the group and
// this row is left with the dots and the toggle.

const GROUP_SELECTORS = ['.sbback', '.sbfwd', '.sbreload'];

test('the floating address bar takes the group out of the row', () => {
  const {el} = row();
  setNavGroupInRow(el, false);
  for (const selector of GROUP_SELECTORS) {
    assert.equal(el.querySelector(selector), null, selector);
  }
});

test('the dots and the toggle stay in the row in both modes', () => {
  const {el} = row();
  setNavGroupInRow(el, false);
  assert.ok(el.querySelector('.sbtd'), 'the window dots left with the group');
  assert.ok(el.querySelector('.sbtoggle'), 'the toggle left with the group');
  setNavGroupInRow(el, true);
  assert.ok(el.querySelector('.sbtd'));
  assert.ok(el.querySelector('.sbtoggle'));
});

// a row narrow enough to have shed the toggle before the handover
// kept it shed forever - layoutNavRow, the only code that clears navgone,
// early-returns for as long as the group is away.
test('a toggle shed before the handover is not left hidden once the group leaves',
     () => {
       const {el} = row();
       collapsed(el, TWO_AND_MENU - 1);
       assert.ok(el.querySelector('.sbtoggle').classList.contains('navgone'),
                 'the fixture never shed the toggle');
       setNavGroupInRow(el, false);
       assert.ok(!el.querySelector('.sbtoggle').classList.contains('navgone'),
                 'the toggle stayed hidden after the group moved to the bar');
     });

test('turning the floating bar off returns the group in the lab\'s order',
     () => {
       const {el} = row();
       setNavGroupInRow(el, false);
       setNavGroupInRow(el, true);
       assert.deepEqual(el.children.map((n) => n.className), [
         'sbtd', 'nb sbtoggle', 'nb first sbback', 'nb sbfwd', 'nb sbreload',
       ]);
     });

test('the returned group still sends its own messages', () => {
  const {el, sent} = row();
  setNavGroupInRow(el, false);
  setNavGroupInRow(el, true);
  sent.length = 0;
  click(el.querySelector('.sbreload'));
  assert.deepEqual(sent, [['sidebarReloadPressed', []]]);
});

// The ladder exists to fit a group into a narrow row. With no group there is
// nothing to collapse, and an overflow button would open an empty menu.
test('a narrow row with the group away grows no overflow button', () => {
  const {el} = row();
  setNavGroupInRow(el, false);
  collapsed(el, ONE_AND_MENU);
  assert.equal(el.querySelector('.sbmore'), null);
});

test('a row that collapsed before the flip drops its overflow button too',
     () => {
       const {el} = row();
       collapsed(el, ONE_AND_MENU);
       assert.ok(el.querySelector('.sbmore'), 'the fixture never collapsed');
       setNavGroupInRow(el, false);
       assert.equal(el.querySelector('.sbmore'), null);
     });

test('the returned group answers a held modifier again', () => {
  const {el} = row();
  setNavGroupInRow(el, false);
  setNavGroupInRow(el, true);
  hold({key: 'Alt', altKey: true});
  assert.equal(el.dataset.mod, 'option');
  assert.equal(navMode(el.querySelector('.sbback .g')), 'end');
  assert.equal(reloadMode(el), 'hard');
  drop();
});

test('a repeat push of the same mode leaves the row alone', () => {
  const {el} = row();
  setNavGroupInRow(el, true);
  assert.equal(el.querySelectorAll('.sbback').length, 1);
  setNavGroupInRow(el, false);
  setNavGroupInRow(el, false);
  assert.equal(el.querySelectorAll('.sbtoggle').length, 1);
});

// sidebar.js owns window.atriumSetDevBarShown and reaches the row through
// this, rather than a second receiver racing that one for the same name.
test('navRowForRoot hands back the row installSidebarNavRow made', () => {
  h.reset();
  const root = document.createElement('div');
  document.body.appendChild(root);
  const installed = installSidebarNavRow(root, {send: () => {}});
  assert.equal(navRowForRoot(root), installed);
  setNavGroupInRow(navRowForRoot(root), false);
  assert.equal(installed.querySelector('.sbback'), null);
});

test('setNavGroupInRow tolerates a root that has no row yet', () => {
  setNavGroupInRow(navRowForRoot(document.createElement('div')), false);
});

// The tracker outlives the groups built against it, so a rebuild that keeps
// the old painters would call into detached nodes for the rest of the session
// - and nothing about the row's DOM shows it.
test('toggling the mode never grows the tracker\'s painter list', () => {
  const {el} = row();
  assert.equal(navPainterCount(el), 3, 'one painter per group button');
  for (let i = 0; i < 10; i++) {
    setNavGroupInRow(el, false);
    assert.equal(navPainterCount(el), 0, `cycle ${i} kept a dead painter`);
    setNavGroupInRow(el, true);
    assert.equal(navPainterCount(el), 3, `cycle ${i} left a painter behind`);
  }
});

// The release above is what the count test guards. This one guards the other
// half: releasing must not cost the LIVE group its painter.
test('the group still answers a modifier after ten toggles', () => {
  const {el} = row();
  for (let i = 0; i < 10; i++) {
    setNavGroupInRow(el, false);
    setNavGroupInRow(el, true);
  }
  hold({key: 'Alt', altKey: true});
  assert.deepEqual(glyphs(el), ['end', 'end', '']);
  assert.equal(reloadMode(el), 'hard');
  drop();
  assert.deepEqual(glyphs(el), ['plain', 'plain', '']);
  assert.equal(reloadMode(el), 'plain');
});

// --- the window-level dismiss ------------------------------------------------

// The state report goes over chrome.send, not the row's injected sender, so
// h.sent is where it lands.
test('the overflow menu reports itself open, and closed again', () => {
  const {el, sent} = row();
  collapsed(el, ONE_AND_MENU);
  click(el.querySelector('.sbmore'));
  assert.deepEqual(h.sent, [['sidebarMenuOpened', []]]);
  click(el.querySelector('.sbmore'));
  assert.deepEqual(h.sent, [['sidebarMenuOpened', []], ['sidebarMenuClosed', []]]);
  assert.deepEqual(sent, [], 'the row\'s own action log stays clean');
});

test('a layout pass with no menu open reports nothing', () => {
  const {el} = row();
  collapsed(el, ONE_AND_MENU);
  collapsed(el, ALL_THREE);
  assert.deepEqual(h.sent, []);
});

test('the native dismiss push closes the overflow menu', () => {
  const {el} = row();
  collapsed(el, ONE_AND_MENU);
  click(el.querySelector('.sbmore'));
  dismissMenus();
  assert.equal(el.querySelector('.navmenu'), null);
  assert.equal(el.querySelector('.sbmore').getAttribute('aria-expanded'),
               'false');
  assert.deepEqual(h.sent.at(-1), ['sidebarMenuClosed', []]);
});

test('both dismiss messages are registered, so neither aborts the browser', () => {
  assert.ok(REGISTERED.has('sidebarMenuOpened'));
  assert.ok(REGISTERED.has('sidebarMenuClosed'));
});

// --- greying a direction the tab cannot take ---------------------------------

// What the window pushes is the displayed tab's CanGoBack/CanGoForward. Until
// it pushes, both directions read live: a row greying itself on a guess would
// be wrong for as long as the first push takes to arrive.
const dimmed = (el) => ['.sbback', '.sbfwd']
    .map((selector) => {
      const button = el.querySelector(selector);
      return button ? (button.classList.contains('navdim') ? 'dim' : 'live')
                    : 'gone';
    })
    .join('|');

// installedRow() above is the fixture here too: the ability receiver is
// published by installSidebarNavRow, not by the bare row.

test('a row greys nothing until the window says what the tab can do', () => {
  const {el} = row();
  assert.equal(dimmed(el), 'live|live');
});

test('the ability push greys the direction the tab cannot take', () => {
  const {el} = installedRow();
  assert.equal(typeof window.atriumSetNavAbility, 'function',
               'the row publishes no receiver, so the push is lost');
  window.atriumSetNavAbility({back: false, forward: false});
  assert.equal(dimmed(el), 'dim|dim', 'a tab with no history either way');
  window.atriumSetNavAbility({back: true, forward: false});
  assert.equal(dimmed(el), 'live|dim', 'after one navigation');
  window.atriumSetNavAbility({back: false, forward: true});
  assert.equal(dimmed(el), 'dim|live', 'after going back to the first page');
});

test('a greyed back ignores a press', () => {
  const {el, sent} = installedRow();
  window.atriumSetNavAbility({back: false, forward: true});
  press(el.querySelector('.sbback'));
  assert.deepEqual(sent, []);
});

test('a greyed back ignores a hold, so the scrubber stays shut', async () => {
  const {el, sent} = installedRow();
  window.atriumSetNavAbility({back: false, forward: true});
  const button = el.querySelector('.sbback');
  button.dispatchEvent(
      new Event('pointerdown', {timeStamp: 0, clientX: 0, clientY: 0}));
  await new Promise((resolve) => setTimeout(resolve, HOLD_THRESHOLD_MS + 50));
  button.dispatchEvent(
      new Event('pointerup', {timeStamp: HOLD_THRESHOLD_MS + 50}));
  assert.deepEqual(sent, []);
});

test('a greyed forward ignores a modifier click', () => {
  const {el, sent} = installedRow();
  window.atriumSetNavAbility({back: true, forward: false});
  hold({key: 'Alt', altKey: true});
  click(el.querySelector('.sbfwd'), {altKey: true});
  drop();
  assert.deepEqual(sent, [],
                   'the wider function is no more reachable than the plain one');
});

test('greying one direction leaves the other controls live', () => {
  const {el, sent} = installedRow();
  window.atriumSetNavAbility({back: false, forward: true});
  click(el.querySelector('.sbfwd'));
  click(el.querySelector('.sbreload'));
  assert.deepEqual(
      sent, [['sidebarForwardPressed', []], ['sidebarReloadPressed', []]]);
});

test('the group comes back from the floating bar still greyed', () => {
  const {el} = installedRow();
  window.atriumSetNavAbility({back: false, forward: true});
  setNavGroupInRow(el, false);
  setNavGroupInRow(el, true);
  assert.equal(dimmed(el), 'dim|live');
});

test('the CSS greys a dead button and keeps a held modifier off it', () => {
  assert.match(css, /\.sbnav \.nb\.navdim[^{]*\{[^}]*opacity:\s*\.3/s);
  assert.match(css, /\.sbnav\[data-mod="option"\] \.nb\.navdim/);
  assert.match(css, /\.sbnav\[data-mod="cmdshift"\] \.nb\.navdim/);
});
