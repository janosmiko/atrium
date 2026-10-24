#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The pinned-extension strip under the address field. Pinned is the only
// gate now: the placement setting and the overflow tray are gone.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');
const css = fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar-ext-strip.css'), 'utf8');
const h = installDOM({pathname: '/sidebar'});
const {renderExtStrip, MAX_PINNED_TILES, stripRows, stripColumns} = await import(
    path.join(ATRIUM_DIR, 'sidebar-ext-strip.js'));
const {installStripExtensions, pinnedExtensionActions,
       setActiveExtensionProfile} = await import(
    path.join(ATRIUM_DIR, 'sidebar-ext-install.js'));

const {setIconScope} = await import(path.join(ATRIUM_DIR, 'icon-scope.js'));
const SCOPE = 'testscope';
setIconScope(SCOPE);

function makeHost() {
  h.reset();
  const container = document.createElement('div');
  container.className = 'exs-row';
  document.body.append(container);
  return container;
}

function action(id, overrides = {}) {
  return {id, name: id, actionTitle: id, visible: true, hasPopup: false,
          pinned: true, iconScope: SCOPE, ...overrides};
}

test('one chip per pinned extension, and none for an unpinned one', () => {
  const container = makeHost();
  renderExtStrip(container, {
    actions: [action('ublock'), action('onepassword'),
              action('unpinned', {pinned: false})],
  });
  const chips = container.querySelectorAll('.exs-chip');
  assert.equal(chips.length, 2);
  assert.deepEqual(chips.map((c) => c.dataset.extensionId),
                   ['ublock', 'onepassword']);
  assert.equal(chips[0].querySelector('.crx-icon').src,
               '/exticon/' + SCOPE + '/ublock.png');
});

test('a reloaded icon gets a new url, so the page fetches the new bytes', () => {
  const container = makeHost();
  renderExtStrip(container, {actions: [action('ublock', {iconRev: 2})]});
  assert.equal(container.querySelector('.crx-icon').src,
               '/exticon/' + SCOPE + '/ublock.png?r=2');
});

// The page's scope follows the window's target browser. The icon bytes live
// with the profile whose bridge pushed the pins, and that is not always the
// same profile, so a page-scope URL answered 404 and drew an empty tile.
test('a chip asks for its icon in the scope the push names', () => {
  const container = makeHost();
  renderExtStrip(container,
                 {actions: [action('ublock', {iconScope: 'bridgescope'})]});
  assert.equal(container.querySelector('.crx-icon').src,
               '/exticon/bridgescope/ublock.png');
});

test('a page scope change leaves a pushed scope alone', () => {
  const container = makeHost();
  renderExtStrip(container,
                 {actions: [action('ublock', {iconScope: 'bridgescope'})]});
  const loaded = container.querySelector('.crx-icon');
  loadIcons(container);
  setIconScope('otherscope');
  try {
    renderExtStrip(container,
                   {actions: [action('ublock', {iconScope: 'bridgescope'})]});
    assert.equal(container.querySelector('.crx-icon'), loaded);
  } finally {
    setIconScope(SCOPE);
  }
});

// A fresh <img> paints nothing for a few frames even from cache, so a redraw
// that rebuilt the icons blanked every chip on a space switch.
function loadIcons(container) {
  for (const img of container.querySelectorAll('.crx-icon')) {
    img.dispatchEvent(new Event('load'));
  }
}

test('a redraw keeps a loaded icon instead of blanking it', () => {
  const container = makeHost();
  renderExtStrip(container, {actions: [action('ublock')]});
  const loaded = container.querySelector('.crx-icon');
  loadIcons(container);
  renderExtStrip(container, {actions: [action('ublock')]});
  assert.equal(container.querySelector('.crx-icon'), loaded);
});

test('an icon that never loaded is not carried', () => {
  const container = makeHost();
  renderExtStrip(container, {actions: [action('ublock')]});
  const first = container.querySelector('.crx-icon');
  renderExtStrip(container, {actions: [action('ublock')]});
  assert.notEqual(container.querySelector('.crx-icon'), first);
});

test('a new icon rev shows the old icon until the new bytes load', () => {
  const container = makeHost();
  renderExtStrip(container, {actions: [action('ublock', {iconRev: 1})]});
  const old = container.querySelector('.crx-icon');
  loadIcons(container);
  const made = [];
  const create = document.createElement;
  document.createElement = function(tag) {
    const el = create.call(this, tag);
    if (tag === 'img') {
      made.push(el);
    }
    return el;
  };
  try {
    renderExtStrip(container, {actions: [action('ublock', {iconRev: 2})]});
  } finally {
    document.createElement = create;
  }
  assert.equal(container.querySelector('.crx-icon'), old);
  assert.equal(made.length, 1);
  const fresh = made[0];
  fresh.dispatchEvent(new Event('load'));
  assert.equal(container.querySelector('.crx-icon'), fresh);
  assert.equal(fresh.src, '/exticon/' + SCOPE + '/ublock.png?r=2');
});

test('another profile scope never carries the icon over', () => {
  const container = makeHost();
  renderExtStrip(container, {actions: [action('ublock')]});
  const old = container.querySelector('.crx-icon');
  loadIcons(container);
  renderExtStrip(container,
                 {actions: [action('ublock', {iconScope: 'otherscope'})]});
  assert.notEqual(container.querySelector('.crx-icon'), old);
});

test('a push with no scope draws no icon path', () => {
  const container = makeHost();
  renderExtStrip(container,
                 {actions: [action('ublock', {iconScope: undefined})]});
  assert.equal(container.querySelector('.crx-icon').src, '');
});

test('the strip hides itself when nothing is pinned', () => {
  const container = makeHost();
  renderExtStrip(container, {actions: [action('a', {pinned: false})]});
  assert.equal(container.childElementCount, 0);
  assert.equal(container.classList.contains('exs-hidden'), true);
});

test('a pinned chip un-hides the strip again', () => {
  const container = makeHost();
  renderExtStrip(container, {actions: [action('a', {pinned: false})]});
  renderExtStrip(container, {actions: [action('a')]});
  assert.equal(container.classList.contains('exs-hidden'), false);
});

test('the strip has no overflow button any more', () => {
  const container = makeHost();
  renderExtStrip(container, {actions: [action('a'), action('b')]});
  assert.equal(container.querySelector('.exs-more'), null,
               'the quick menu replaced the tray');
  assert.equal(container.querySelector('.crx-overflow-menu'), null);
});

test('clicking a chip reports its extension id', () => {
  const container = makeHost();
  let clicked = null;
  renderExtStrip(container, {
    actions: [action('ublock')],
    onChipClick: (id) => clicked = id,
  });
  container.querySelector('.exs-chip').click();
  assert.equal(clicked, 'ublock');
});

test('right-clicking a chip opens the native menu on its id and rect', () => {
  const container = makeHost();
  let seen = null;
  renderExtStrip(container, {
    actions: [action('ublock')],
    onChipContextMenu: (id, rect) => seen = id,
  });
  const event = new Event('contextmenu', {bubbles: true, cancelable: true});
  container.querySelector('.exs-chip').dispatchEvent(event);
  assert.equal(seen, 'ublock');
  assert.equal(event.defaultPrevented, true,
              'the native menu must not also open on the button');
});

test('an inert copy does not act on a right-click either', () => {
  const container = makeHost();
  let called = 0;
  renderExtStrip(container, {actions: [action('ublock')], inert: true,
                             onChipContextMenu: () => called++});
  container.querySelector('.exs-chip').dispatchEvent(
      new Event('contextmenu', {bubbles: true, cancelable: true}));
  assert.equal(called, 0, 'a copy must not open a menu');
});

test('an inert copy does not let a right-click bubble to the sidebar', () => {
  const container = makeHost();
  renderExtStrip(container, {actions: [action('ublock')], inert: true});
  let bubbled = false;
  document.addEventListener('contextmenu', () => bubbled = true);
  const event = new Event('contextmenu', {bubbles: true, cancelable: true});
  container.querySelector('.exs-chip').dispatchEvent(event);
  assert.equal(bubbled, false,
              'the click reached the sidebar and opened the empty-space menu');
  assert.equal(event.defaultPrevented, true);
});

// Four to a row, three rows. A thirteenth pin would take a fourth row out of
// the favourites under it.
test('the sidebar strip gives at most twelve pins a tile', () => {
  installed();
  window.atriumSetSidebarExtensions(
      Array.from({length: 40}, (_, i) => action(`ext${i}`)), 'home');
  const row = document.querySelector('body > .exs-row');
  const chips = row.querySelectorAll('.exs-chip');
  assert.equal(chips.length, 12);
  assert.equal(chips[0].dataset.extensionId, 'ext0');
  assert.equal(chips[11].dataset.extensionId, 'ext11');
  assert.equal(row.style.getPropertyValue('--exs-rows'), '3');
});

// The floating bar's row is bare chips that scroll, so the cap belongs to the
// callers that draw tiles, not to the renderer both of them share.
test('an uncapped call keeps every pin, the way the dev bar needs', () => {
  const container = makeHost();
  const actions = Array.from({length: 40}, (_, i) => action(`ext${i}`));
  renderExtStrip(container, {actions});
  assert.equal(container.querySelectorAll('.exs-chip').length, 40);
  renderExtStrip(container, {actions, max: MAX_PINNED_TILES});
  assert.equal(container.querySelectorAll('.exs-chip').length, 12);
});

// A copy in a rail pane is on screen only mid-swipe, and a duplicate of every
// chip in the tab order is what buildFavRoom's empty slots exist to avoid.
test('an inert copy is hidden from the reader and out of the tab order', () => {
  const container = makeHost();
  let clicked = 0;
  renderExtStrip(container, {actions: [action('ublock')], inert: true,
                             onChipClick: () => clicked++});
  assert.equal(container.getAttribute('aria-hidden'), 'true');
  const chip = container.querySelector('.exs-chip');
  assert.equal(chip.getAttribute('tabindex'), '-1');
  chip.click();
  assert.equal(clicked, 0, 'a copy must not act on a click');
});

test('the live strip stays reachable', () => {
  const container = makeHost();
  renderExtStrip(container, {actions: [action('ublock')]});
  assert.equal(container.getAttribute('aria-hidden'), null);
  assert.equal(container.querySelector('.exs-chip').getAttribute('tabindex'),
               null);
});

test('a tile is a shorter favourite: shared width, .fav fill and radius', () => {
  const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
  const rule = bare.match(/\.exs-row \.exs-chip \{([^}]+)\}/);
  assert.ok(rule, 'no .exs-row .exs-chip rule found');
  assert.match(rule[1], /width:\s*auto/, 'the tiles do not fill their column');
  assert.match(rule[1], /height:\s*var\(--exs-h\)/);
  assert.match(rule[1], /border-radius:\s*12px/, '.fav radius');
  assert.match(rule[1], /background:\s*var\(--fill,/, '.fav fill');
  const row = bare.match(/\.exs-row \{([^}]+)\}/);
  assert.match(row[1], /grid-template-columns:\s*repeat\(var\(--exs-cols, 4\), 1fr\)/,
      'the tiles do not share the row');
});

// The cached PNG is the extension's largest rep, so an unsized img draws at
// 32px or more inside a 22px chip.
test('the chip icon is sized', () => {
  const rule = css.match(/\.exs-chip\s+\.crx-icon\s*{([^}]+)}/);
  assert.ok(rule, 'no .exs-chip .crx-icon rule found');
  assert.match(rule[1], /width:\s*15px/);
  assert.match(rule[1], /height:\s*15px/);
});

test('installStripExtensions renders from a push and reflows the root', () => {
  h.reset();
  const root = document.createElement('div');
  root.setAttribute('id', 'atrium-root');
  root.setRect({width: 300});
  document.body.append(root);
  installStripExtensions(root);

  window.atriumSetSidebarExtensions([action('ublock')]);
  assert.equal(root.classList.contains('exs-reflow'), true,
               'the tiles below need their own lane once a chip is up');
  const chip = document.querySelector('.exs-chip');
  assert.ok(chip);
  // The popup anchors to this rect, so the click has to carry it in SCREEN
  // coordinates. A client-coordinate reading would send 40,12.
  chip.setRect({left: 40, top: 12, width: 24, height: 24});
  window.screenX = 200;
  window.screenY = 100;
  chip.click();
  assert.deepEqual(h.sent,
                   [['pillActionClicked', ['ublock', [240, 112, 24, 24]]]]);
});

test('a chip with no measurable box sends no rect at all', () => {
  h.reset();
  const root = document.createElement('div');
  root.setAttribute('id', 'atrium-root');
  root.setRect({width: 300});
  document.body.append(root);
  installStripExtensions(root);

  window.atriumSetSidebarExtensions([action('ublock')]);
  const chip = document.querySelector('.exs-chip');
  window.screenX = undefined;
  chip.click();
  window.screenX = 0;
  // Null, not a rect of NaNs: the window reads an unusable rect as no rect and
  // falls back to the surface, and NaN would not survive the trip anyway.
  assert.deepEqual(h.sent, [['pillActionClicked', ['ublock', null]]]);
});

test('unpinning the last extension drops the reflow lane again', () => {
  h.reset();
  const root = document.createElement('div');
  root.setAttribute('id', 'atrium-root');
  root.setRect({width: 300});
  document.body.append(root);
  installStripExtensions(root);

  window.atriumSetSidebarExtensions([action('ublock')]);
  window.atriumSetSidebarExtensions([action('ublock', {pinned: false})]);
  assert.equal(root.classList.contains('exs-reflow'), false);
  assert.equal(document.querySelector('.exs-chip'), null);
});

function installed() {
  h.reset();
  const root = document.createElement('div');
  root.setAttribute('id', 'atrium-root');
  root.setRect({width: 300});
  document.body.append(root);
  installStripExtensions(root);
  return root;
}

// A rail pane of another profile draws that profile's own pins, so one push
// per profile has to survive the next profile's push.
test('the pins are kept per profile', () => {
  installed();
  window.atriumSetSidebarExtensions([action('ublock')], 'home');
  window.atriumSetSidebarExtensions([action('onepassword')], 'work');
  assert.deepEqual(pinnedExtensionActions('home').map((a) => a.id), ['ublock']);
  assert.deepEqual(pinnedExtensionActions('work').map((a) => a.id),
                   ['onepassword']);
  assert.deepEqual(pinnedExtensionActions('never-loaded'), []);
});

// SelectSpace pushes the DESTINATION profile's actions before the glide even
// starts. Re-rendering the row on that push swaps it to the incoming pins
// while swipe.js is still carrying it off as the outgoing profile's row.
test('a push for another profile leaves the row it is drawn for alone', () => {
  installed();
  window.atriumSetSidebarExtensions([action('ublock')], 'home');
  window.atriumSetSidebarExtensions([action('onepassword')], 'work');
  const strip = document.querySelector('body > .exs-row');
  assert.deepEqual(
      strip.querySelectorAll('.exs-chip').map((c) => c.dataset.extensionId),
      ['ublock']);
  assert.equal(strip.dataset.profileId, 'home');
  assert.deepEqual(pinnedExtensionActions('work').map((a) => a.id),
                   ['onepassword'], 'the pane copies still get the new pins');
});

// The rebuild that stamps the pinned favourites grid is the one moment the row
// may change profile: the two are one block and must never disagree.
test('the snapshot rebuild is what moves the row to another profile', () => {
  installed();
  window.atriumSetSidebarExtensions([action('ublock')], 'home');
  window.atriumSetSidebarExtensions([action('onepassword')], 'work');
  setActiveExtensionProfile('work');
  const strip = document.querySelector('body > .exs-row');
  assert.deepEqual(
      strip.querySelectorAll('.exs-chip').map((c) => c.dataset.extensionId),
      ['onepassword']);
  assert.equal(strip.dataset.profileId, 'work');
});

// A push for the profile already showing is a pin, an unpin or a badge change
// on the row in front of the user.
test('a push for the profile on screen redraws it', () => {
  installed();
  window.atriumSetSidebarExtensions([action('ublock')], 'home');
  window.atriumSetSidebarExtensions(
      [action('ublock'), action('onepassword')], 'home');
  assert.equal(document.querySelector('body > .exs-row')
                   .querySelectorAll('.exs-chip').length, 2);
});

// The old single-argument shape, which the floating bar and the browser's
// pre-profile pushes still use.
test('a push with no profile means the active one', () => {
  installed();
  window.atriumSetSidebarExtensions([action('ublock')], 'home');
  window.atriumSetSidebarExtensions([action('onepassword')]);
  assert.deepEqual(pinnedExtensionActions('home').map((a) => a.id),
                   ['onepassword']);
});

// A second handshake replays everything the browser still holds, so entries it
// has forgotten must not linger.
test('a fresh install drops the profiles the last one held', () => {
  installed();
  window.atriumSetSidebarExtensions([action('ublock')], 'home');
  installed();
  assert.deepEqual(pinnedExtensionActions('home'), []);
});

// installStripExtensions inserts the real strip as #atrium-root's SIBLING, so
// a rule scoped under #atrium-root reaches the pane copies and never the row
// the user looks at. Cmd+Shift+D then drew the pins twice, once per bar.
test('the devbar hide rule reaches the strip beside the root', () => {
  const root = installed();
  window.atriumSetSidebarExtensions([action('ublock')], 'home');
  const strip = document.querySelector('body > .exs-row');
  assert.equal(strip.parentNode, root.parentNode,
               'the strip stopped being the root\'s sibling');
  const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
  assert.doesNotMatch(bare, /#atrium-root\.devbar-shown \.exs-row \{/,
      'a descendant of #atrium-root never matches the strip beside it');
  assert.match(bare, /html\.devbar-shown \.exs-row \{/);
});

// display: none cannot be transitioned, so the strip arrived and
// left in one frame while the rows under it eased over .2s.
test('the strip fades rather than popping, on the offsets own duration', () => {
  const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
  const row = bare.match(/\.exs-row \{([^}]+)\}/);
  const hidden = bare.match(/\.exs-row\.exs-hidden \{([^}]+)\}/);
  const devbar = bare.match(/html\.devbar-shown \.exs-row \{([^}]+)\}/);
  assert.ok(row && hidden && devbar, 'all three .exs-row rules exist');
  assert.match(row[1], /transition:[^;]*opacity \.2s/);
  for (const [name, rule] of [['hidden', hidden], ['devbar', devbar]]) {
    assert.doesNotMatch(rule[1], /display:\s*none/,
        `the ${name} state pops instead of fading`);
    assert.match(rule[1], /opacity:\s*0/);
    assert.match(rule[1], /pointer-events:\s*none/,
        `a faded ${name} strip still takes the clicks meant for the row`);
  }
});

// The pinned grid sits at 90 and a rail pane's rows at the same 90, so both
// bases take the one lane token. A pane padding smaller than the grid top puts
// the space header under the tiles.
test('the strip lane moves the pinned grid and the pane rows by one token', () => {
  const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
  const grid = bare.match(
      /#atrium-root\.exs-reflow\.sbroot:not\(\.devbar-shown\) > \.favgrid \{\s*top: calc\((\d+)px \+ var\(--exs-lane\)\)/);
  const pane = bare.match(
      /\.railpane \.sidebar\.exs-lane \{\s*padding-top: calc\((\d+)px \+ var\(--exs-lane\)\)/);
  assert.ok(grid && pane, 'both lane rules exist');
  assert.equal(pane[1], grid[1]);
});

// A profile with nothing pinned draws no strip, so holding the lane open for
// it would leave a blank band where its favourites should be.
test('the lane is per pane, not per root', () => {
  const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
  assert.doesNotMatch(
      bare,
      /#atrium-root\.exs-reflow\.sbroot:not\(\.devbar-shown\) \.sidebar \{/,
      'a root-wide padding pads every pane, pinned profile or not');
  assert.match(bare, /\.railpane \.sidebar\.exs-lane \{/);
});

// swipe.js writes this row's slide inline every frame.
test('the hidden states slide on translate, not on transform', () => {
  const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
  for (const rule of [bare.match(/\.exs-row\.exs-hidden \{([^}]+)\}/),
                      bare.match(/html\.devbar-shown \.exs-row \{([^}]+)\}/)]) {
    assert.ok(rule);
    assert.doesNotMatch(rule[1], /transform:/,
        'a transitioned transform here trails the swipe by its own duration');
    assert.match(rule[1], /translate:\s*0 -6px/);
  }
});

// The strip's own top lives here and the band it sits in comes from
// three tokens in sidebar.css, so a change to either file can push the strip
// off centre and nothing else notices.
test('the strip sits centred between the address row and the tiles', () => {
  const sidebar = fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar.css'), 'utf8');
  const strip = css.replace(/\/\*[\s\S]*?\*\//g, '');
  const sb = sidebar.replace(/\/\*[\s\S]*?\*\//g, '');
  const num = (re, text, what) => {
    const m = text.match(re);
    assert.ok(m, `${what} not found`);
    return Number(m[1]);
  };
  const rowTop = num(/#atrium-root\.sbroot > \.urlrow \{[^}]*?top: (\d+)px/s, sb,
      'address row top');
  const rowHeight = num(/\n\.urlbar \{[^}]*?height: (\d+)px/s, sb,
      'address field height');
  const stripTop = num(/\.exs-row \{[^}]*?top: (\d+)px/s, strip, 'strip top');
  const rowGap = num(
      /--exs-lane: calc\(var\(--exs-rows, 1\) \* \(var\(--exs-h\) \+ (\d+)px\)\)/,
      sb, 'lane token');
  const gridBase = num(
      /> \.favgrid \{\s*top: calc\((\d+)px \+ var\(--exs-lane\)\)/, strip,
      'pinned grid base');
  // Both the lane and the strip's own height are rows * (--exs-h + gap), so the
  // gap under the last drawn row is the same at one row and at three.
  const above = stripTop - (rowTop + rowHeight);
  const below = gridBase + rowGap - stripTop;
  assert.equal(above, below,
      `uneven band: ${above}px above the strip, ${below}px below`);
});

// Twelve tiles in one row would each fall under their hit target,
// and a lane held at one row paints the rest over the favourites.
test('twelve pins wrap into three rows and the lane follows the count', () => {
  const strip = css.replace(/\/\*[\s\S]*?\*\//g, '');
  assert.equal(MAX_PINNED_TILES, 12);
  assert.match(strip,
      /\.exs-row \{[^}]*?grid-template-columns: repeat\(var\(--exs-cols, 4\), 1fr\)/s);
  assert.match(strip,
      /\.exs-row \{[^}]*?height: calc\(var\(--exs-rows, 1\) \* \(var\(--exs-h\) \+ 6px\) - 6px\)/s,
      'the strip height must grow with the drawn row count');
  assert.deepEqual([0, 1, 4, 5, 8, 9, 12, 99].map(stripRows),
                   [1, 1, 1, 2, 2, 3, 3, 3]);
});

// A flat four columns left three pins three quarters of the way across the
// sidebar, and dropped the fifth alone under a full row of four.
test('the pins spread over their rows rather than filling four at a time', () => {
  assert.deepEqual([1, 2, 3, 4, 5, 6, 7, 8, 9, 12].map(stripColumns),
                   [1, 2, 3, 4, 3, 3, 4, 4, 3, 4]);
  const container = h.document.createElement('div');
  const actions = Array.from({length: 5}, (_, i) => (
      {id: `e${i}`, name: `e${i}`, pinned: true}));
  renderExtStrip(container, {actions, max: MAX_PINNED_TILES});
  assert.equal(container.style.getPropertyValue('--exs-cols'), '3',
               'five pins must read as 3 + 2, not 4 + 1');
  assert.equal(container.style.getPropertyValue('--exs-rows'), '2');
});

// The element that reserves the lane is never the strip, so it cannot read the
// row count off the DOM. Both callers have to write it themselves.
test('the strip writes its row count on its own container', () => {
  const container = h.document.createElement('div');
  const actions = Array.from({length: 9}, (_, i) => (
      {id: `e${i}`, name: `e${i}`, pinned: true}));
  renderExtStrip(container, {actions, max: MAX_PINNED_TILES});
  assert.equal(container.style.getPropertyValue('--exs-rows'), '3');
  renderExtStrip(container, {actions: actions.slice(0, 2), max: MAX_PINNED_TILES});
  assert.equal(container.style.getPropertyValue('--exs-rows'), '1');
});
