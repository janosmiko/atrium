#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The sidebar media controller block. One controller per record AtriumMediaHub
// pushes as mediaSessionsChanged, newest at the bottom.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');
const css = fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar-media.css'), 'utf8');
const source = fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar-media.js'), 'utf8');
const h = installDOM({pathname: '/sidebar'});
const {installSidebarMedia, renderMediaBlock, MAX_SHOWN_CONTROLLERS} =
    await import(path.join(ATRIUM_DIR, 'sidebar-media.js'));
const {setIconScope} = await import(path.join(ATRIUM_DIR, 'icon-scope.js'));

const SCOPE = 'testscope';
setIconScope(SCOPE);

function record(id, overrides = {}) {
  return {
    id, title: 'Track ' + id, faviconURL: '', host: 'example.com',
    playing: true, canPrev: true, canNext: true,
    canSeek: false, pictureInPicture: false, volume: 1, ...overrides,
  };
}

function makeHost() {
  h.reset();
  const container = document.createElement('div');
  container.className = 'sbm-block';
  document.body.append(container);
  return container;
}

function installed() {
  h.reset();
  const root = document.createElement('div');
  root.setAttribute('id', 'atrium-root');
  document.body.append(root);
  installSidebarMedia(root);
  return root;
}

// The volume button is a .sbm-btn too, but it toggles the slider locally
// and carries no data-command of its own.
const commandsIn = (item) =>
    item.querySelectorAll('.sbm-btn[data-command]').map((b) => b.dataset.command);

test('one controller per record', () => {
  const container = makeHost();
  renderMediaBlock(container, {records: [record('7'), record('9')]});
  const items = container.querySelectorAll('.sbm-item');
  assert.equal(items.length, 2);
  assert.deepEqual(items.map((i) => i.dataset.tabId), ['7', '9']);
});

// The hub sends the list in order and the block stacks it top to bottom, so
// the last record is the one nearest the tab list.
test('the newest record is the controller at the bottom', () => {
  const container = makeHost();
  renderMediaBlock(container, {
    records: [record('1', {title: 'older'}), record('2', {title: 'newer'})],
  });
  const titles = container.querySelectorAll('.sbm-title').map((t) => t.textContent);
  assert.deepEqual(titles, ['older', 'newer']);
});

test('the title is the record\'s own text', () => {
  const container = makeHost();
  renderMediaBlock(container, {
    records: [record('3', {title: 'Nocturne', host: 'radio.example'})],
  });
  assert.equal(container.querySelector('.sbm-title').textContent, 'Nocturne');
});

// The host line is gone: the title is the only text drawn per controller.
test('the block draws no host line', () => {
  const container = makeHost();
  renderMediaBlock(container, {records: [record('1')]});
  assert.equal(container.querySelector('.sbm-host'), null);
  const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
  assert.doesNotMatch(bare, /\.sbm-host/);
});

// The favicon is read from the scoped cache by tab id. A record's own
// faviconURL is page-supplied, and never reaches an src.
test('the favicon comes from the scoped path, not from the record', () => {
  const container = makeHost();
  renderMediaBlock(container, {
    records: [record('5', {faviconURL: 'javascript:alert(1)'})],
  });
  const img = container.querySelector('.sbm-fv img');
  assert.equal(img.src, '/favicon/' + SCOPE + '/5.png');
});

test('an empty list hides the block', () => {
  const container = makeHost();
  renderMediaBlock(container, {records: [record('1')]});
  renderMediaBlock(container, {records: []});
  assert.equal(container.childElementCount, 0);
  assert.equal(container.classList.contains('sbm-hidden'), true);
});

// Three controllers of room, and the rest reached by scrolling - a fourth
// would take the tab list's own height.
test('the block holds three controllers and scrolls the rest', () => {
  const container = makeHost();
  assert.equal(MAX_SHOWN_CONTROLLERS, 3);
  renderMediaBlock(container, {
    records: [record('1'), record('2'), record('3'), record('4'), record('5')],
  });
  assert.equal(container.querySelectorAll('.sbm-item').length, 5,
               'every record keeps its controller');
  assert.equal(container.style.getPropertyValue('--sbm-rows'), '3',
               'the block must not grow past three');
  const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
  const block = bare.match(/\.sbm-block \{([^}]+)\}/);
  assert.ok(block, 'no .sbm-block rule found');
  assert.match(block[1], /overflow-y:\s*auto/);
  assert.match(block[1],
      /height: calc\(\(var\(--sbm-rows, 0\) - 1\) \* 42px \+ var\(--sbm-h, 36px\)\)/);
});

test('a controller carries the five instrumented controls', () => {
  const container = makeHost();
  renderMediaBlock(container, {records: [record('7')]});
  assert.deepEqual(commandsIn(container.querySelector('.sbm-item')),
                   ['mediaPrev', 'mediaPlayPause', 'mediaNext',
                    'mediaPictureInPicture', 'mediaClosed']);
});

test('a control the page does not support keeps its place, disabled', () => {
  const container = makeHost();
  renderMediaBlock(container,
                   {records: [record('7', {canPrev: false, canNext: false})]});
  const item = container.querySelector('.sbm-item');
  assert.deepEqual(commandsIn(item),
                   ['mediaPrev', 'mediaPlayPause', 'mediaNext',
                    'mediaPictureInPicture', 'mediaClosed']);
  assert.equal(item.querySelector('.sbm-prev').disabled, true);
  assert.equal(item.querySelector('.sbm-next').disabled, true);
  assert.ok(!item.querySelector('.sbm-play').disabled);
});

test('each instrumented button sends its own command with the record\'s tab id',
     () => {
  const container = makeHost();
  const sent = [];
  renderMediaBlock(container, {
    records: [record('4'), record('11')],
    onCommand: (command, tabId) => sent.push([command, tabId]),
  });
  const second = container.querySelectorAll('.sbm-item')[1];
  for (const button of second.querySelectorAll('.sbm-btn[data-command]')) {
    button.click();
  }
  assert.deepEqual(sent, [
    ['mediaPrev', '11'], ['mediaPlayPause', '11'], ['mediaNext', '11'],
    ['mediaPictureInPicture', '11'], ['mediaClosed', '11'],
  ]);
});

// The title is the back-to-tab target now - there is no dedicated button.
test('the title sends mediaBackToTab with the record\'s tab id', () => {
  const container = makeHost();
  const sent = [];
  renderMediaBlock(container, {
    records: [record('9')],
    onCommand: (command, tabId) => sent.push([command, tabId]),
  });
  container.querySelector('.sbm-title').click();
  assert.deepEqual(sent, [['mediaBackToTab', '9']]);
});

test('the picture-in-picture button reflects the record\'s own state', () => {
  const container = makeHost();
  renderMediaBlock(container, {records: [record('1', {pictureInPicture: false})]});
  const pip = container.querySelector('.sbm-btn[data-command="mediaPictureInPicture"]');
  assert.equal(pip.getAttribute('aria-label'), 'Picture-in-Picture');
  assert.equal(pip.getAttribute('aria-pressed'), 'false');
  renderMediaBlock(container, {records: [record('1', {pictureInPicture: true})]});
  const pipOn = container.querySelector('.sbm-btn[data-command="mediaPictureInPicture"]');
  assert.equal(pipOn.getAttribute('aria-label'), 'Exit Picture-in-Picture');
  assert.equal(pipOn.getAttribute('aria-pressed'), 'true');
});

// The mute toggle is gone: the volume button only opens the slider band, and
// the slider itself is what tells the browser a level.
test('the volume slider sends mediaVolume with the record\'s tab id and level',
     () => {
  const container = makeHost();
  const sent = [];
  renderMediaBlock(container, {
    records: [record('8', {volume: 0.4})],
    onCommand: (command, tabId, level) => sent.push([command, tabId, level]),
  });
  const range = container.querySelector('.sbm-vol-range');
  assert.equal(range.value, '0.4');
  range.value = '0.75';
  range.dispatchEvent(new Event('input'));
  assert.deepEqual(sent, [['mediaVolume', '8', 0.75]]);
});

test('the volume button carries no command of its own', () => {
  const container = makeHost();
  renderMediaBlock(container, {records: [record('1')]});
  const volBtn = container.querySelector('.sbm-vol-btn');
  assert.ok(volBtn);
  assert.equal(volBtn.dataset.command, undefined);
});

test('the play button reads the record\'s own state', () => {
  const container = makeHost();
  renderMediaBlock(container, {records: [record('1', {playing: false})]});
  const play = container.querySelector('.sbm-btn[data-command="mediaPlayPause"]');
  assert.equal(play.getAttribute('aria-label'), 'Play');
  renderMediaBlock(container, {records: [record('1', {playing: true})]});
  assert.equal(
      container.querySelector('.sbm-btn[data-command="mediaPlayPause"]')
          .getAttribute('aria-label'), 'Pause');
});

// A record with no id cannot be commanded, so a controller for it would be a
// row of dead buttons.
test('a record with no tab id draws nothing', () => {
  const container = makeHost();
  renderMediaBlock(container, {records: [record(''), record('2')]});
  assert.deepEqual(
      container.querySelectorAll('.sbm-item').map((i) => i.dataset.tabId), ['2']);
});

test('a push installs the block beside the root and draws it', () => {
  const root = installed();
  window.atriumMediaSessionsChanged([record('8')]);
  const block = document.querySelector('body > .sbm-block');
  assert.ok(block);
  assert.equal(block.parentNode, root.parentNode,
               'a child of the root would not survive its rebuilds');
  assert.equal(block.querySelectorAll('.sbm-item').length, 1);
  assert.equal(root.classList.contains('sbm-reflow'), true,
               'the tab list has to give the block its room');
});

test('a record that goes takes its controller with it', () => {
  const root = installed();
  window.atriumMediaSessionsChanged([record('8'), record('9')]);
  window.atriumMediaSessionsChanged([record('9')]);
  const block = document.querySelector('body > .sbm-block');
  assert.deepEqual(block.querySelectorAll('.sbm-item').map((i) => i.dataset.tabId),
                   ['9']);
  window.atriumMediaSessionsChanged([]);
  assert.equal(block.querySelectorAll('.sbm-item').length, 0);
  assert.equal(root.classList.contains('sbm-reflow'), false,
               'an empty block must give the room back');
});

test('a button on the installed block reaches the browser', () => {
  installed();
  window.atriumMediaSessionsChanged([record('8')]);
  document.querySelector('.sbm-btn[data-command="mediaClosed"]').click();
  assert.deepEqual(h.sent, [['mediaClosed', ['8']]]);
});

// A private window's hub answers with its own records only, so the block for
// one starts empty and stays empty until that profile plays something.
test('a window with no records holds no room', () => {
  const root = installed();
  assert.equal(root.classList.contains('sbm-reflow'), false);
  assert.equal(document.querySelector('.sbm-item'), null);
});

// A title and a host are page-supplied. innerHTML anywhere in this file would
// make one of them markup.
test('the block never builds a node from a string', () => {
  const bare = source.replace(/\/\/.*$/gm, '');
  assert.doesNotMatch(bare, /innerHTML|outerHTML|insertAdjacentHTML/);
});

// The block is the root's SIBLING, so it inherits none of the sidebar's ink.
// Left out, its text and fills ignore the backdrop the window measured.
test('the block reads the same ink tokens as the strip beside it', () => {
  const sidebar = fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar.css'), 'utf8');
  assert.match(sidebar,
      /html:not\(\.atrium-native-chrome\) body > \.sbm-block,/,
      'the block is not in the ink block\'s selector list');
  const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
  assert.doesNotMatch(bare, /rgba?\(|#[0-9a-fA-F]{3,8}\b/,
      'a literal colour here cannot follow the theme');
});

test('nothing in the block moves under reduced motion', () => {
  const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
  const rule = bare.match(
      /@media \(prefers-reduced-motion: reduce\) \{([\s\S]+?)\n\}/);
  assert.ok(rule, 'no reduced-motion block');
  assert.match(rule[1], /transition:\s*none/);
});

// The rows themselves must not move: 38 scenarios click them by pixel.
test('a controller rests at exactly one 36px row', () => {
  const container = makeHost();
  renderMediaBlock(container, {records: [record('1')]});
  assert.equal(container.style.getPropertyValue('--sbm-h'), '36px');
  const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
  assert.match(bare, /\.sbm-item \{[^}]*?height: 36px/s);
  const sidebar = fs.readFileSync(path.join(ATRIUM_DIR, 'sidebar.css'), 'utf8');
  assert.match(sidebar, /\n\.row \{[^}]*?height: 36px/s);
});

// Only one controller can be expanded at a time, so a single --sbm-h
// describes the room the block and the tab list need.
test('hovering a controller grows it to 72px and reports the height', () => {
  const container = makeHost();
  const heights = [];
  renderMediaBlock(container, {
    records: [record('1')],
    onHeightChange: (h) => heights.push(h),
  });
  const item = container.querySelector('.sbm-item');
  item.dispatchEvent(new Event('mouseenter'));
  assert.equal(container.style.getPropertyValue('--sbm-h'), '72px');
  item.dispatchEvent(new Event('mouseleave'));
  assert.equal(container.style.getPropertyValue('--sbm-h'), '36px');
  assert.deepEqual(heights, [36, 72, 36]);
});

test('opening the volume band grows the controller to 108px', () => {
  const container = makeHost();
  renderMediaBlock(container, {records: [record('1'), record('2')]});
  const [first, second] = container.querySelectorAll('.sbm-item');
  first.querySelector('.sbm-vol-btn').click();
  assert.equal(first.classList.contains('sbm-vol-open'), true);
  assert.equal(container.style.getPropertyValue('--sbm-h'), '108px');
  // Opening the second controller's band closes the first: only one row can
  // be expanded, which is what keeps the height math a single number.
  second.querySelector('.sbm-vol-btn').click();
  assert.equal(first.classList.contains('sbm-vol-open'), false);
  assert.equal(second.classList.contains('sbm-vol-open'), true);
});

// The transport IS the resting row: favicon, previous, play/pause, next,
// volume. Hover opens a title row above it for the name and the window
// controls, which are the only parts that belong to the expanded state.
test('the transport rests in the row and the title row opens above it', () => {
  const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
  const rest = bare.match(/\.sbm-item \{([^}]+)\}/);
  // The transport is the last track, and the 1fr above it is the title row the
  // resting state leaves empty. Fixed tracks overflowed the animating box and
  // slid the transport past the block's clipped bottom edge.
  // minmax(0, ...) because a bare 1fr floors at its content, and the title's
  // own line box then pushed the transport down while the row was short.
  assert.match(rest[1], /grid-template-rows:\s*minmax\(0, 1fr\) 36px/);
  assert.match(rest[1], /"fav\s+\.\s+prev\s+play\s+next\s+\.\s+vol"/);
  assert.match(bare,
      /\.sbm-item \.sbm-title,[\s\S]*?\.sbm-item \.sbm-volband \{[^}]*?display:\s*none/);
  assert.match(bare,
      /:is\(:hover, \.sbm-vol-open\) \.sbm-pip-btn,[\s\S]*?\{[^}]*?display:\s*flex/);
  // A bare .sbm-pip-btn ties with .sbm-btn's own display and loses on order,
  // so every hidden-at-rest selector has to carry .sbm-item.
  for (const name of ['title', 'pip-btn', 'close-btn']) {
    assert.doesNotMatch(bare, new RegExp('^\\.sbm-' + name + ',?$', 'm'),
                        `.sbm-${name} is hidden without .sbm-item to outrank .sbm-btn`);
  }
});

// Below the narrow-sidebar breakpoint the favicon drops, the same pattern
// .row.tabrow.splitrow uses for its own title-hiding query.
test('the favicon hides below the narrow-sidebar breakpoint', () => {
  const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
  // On the block: a row that carries the container cannot be styled by its
  // own query, which left every rule but the favicon's dead.
  assert.match(bare, /\.sbm-block \{[^}]*?container:\s*sbmitem \/ inline-size/s);
  const query = bare.match(/@container sbmitem \(max-width: \d+px\) \{([^}]+)\}/);
  assert.ok(query, 'no sbmitem container query found');
  assert.match(query[1], /\.sbm-fv \{[^}]*?display:\s*none/s);
});

// Every command comes back as a fresh push. A rebuild on each one tore the
// hovered row, and an open volume band, out from under the pointer.
test('a push that changes no row keeps the nodes it drew', () => {
  const root = installed();
  window.atriumMediaSessionsChanged([record('4'), record('5')]);
  const block = document.querySelector('body > .sbm-block');
  const before = [...block.querySelectorAll('.sbm-item')];
  before[1].classList.add('sbm-vol-open');
  before[1].hovered = true;

  window.atriumMediaSessionsChanged(
      [record('4'), record('5', {playing: false, volume: 0})]);

  const after = [...block.querySelectorAll('.sbm-item')];
  assert.equal(after[0], before[0], 'row 4 was rebuilt');
  assert.equal(after[1], before[1], 'row 5 was rebuilt');
  assert.equal(after[1].classList.contains('sbm-vol-open'), true);
  assert.equal(after[1].hovered, true);
  assert.equal(after[1].querySelector('.sbm-play').title, 'Play',
               'the patched row kept the old play glyph');
  assert.equal(after[1].querySelector('.sbm-vol-range').value, '0');
});

// Each drag step sends a command and the hub answers with a push. That answer
// lags the thumb, so writing it back mid-drag drags the thumb backwards.
test('a push leaves the volume thumb alone while it is held', () => {
  const root = installed();
  window.atriumMediaSessionsChanged([record('4', {volume: 1})]);
  const range = document.querySelector('body > .sbm-block .sbm-vol-range');

  range.dispatchEvent(new Event('pointerdown', {bubbles: true}));
  range.value = '0.2';
  window.atriumMediaSessionsChanged([record('4', {volume: 1})]);
  assert.equal(range.value, '0.2', 'the lagging push moved the held thumb');

  range.dispatchEvent(new Event('pointerup', {bubbles: true}));
  window.atriumMediaSessionsChanged([record('4', {volume: 0.2})]);
  assert.equal(range.value, '0.2');
});

test('a push that changes the rows rebuilds them', () => {
  const root = installed();
  window.atriumMediaSessionsChanged([record('4'), record('5')]);
  const block = document.querySelector('body > .sbm-block');
  const before = [...block.querySelectorAll('.sbm-item')];
  window.atriumMediaSessionsChanged([record('5')]);
  const after = [...block.querySelectorAll('.sbm-item')];
  assert.equal(after.length, 1);
  assert.notEqual(after[0], before[0]);
});

// A row gap counts against the 36px tracks the item's height is the sum of,
// so an 8px one made the two-row grid need 80px in a 72px box.
test('the item grid gaps its columns only', () => {
  const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
  const item = bare.match(/\.sbm-item \{([^}]+)\}/);
  assert.ok(item, 'no .sbm-item rule found');
  assert.match(item[1], /row-gap:\s*0/);
  assert.doesNotMatch(item[1], /^\s*gap:/m);
});

// The block grows upward from its own bottom edge, so a snap here reads as
// the card jumping while the row inside it eases.
test('the block eases its own height', () => {
  const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
  const block = bare.match(/\.sbm-block \{([^}]+)\}/);
  assert.match(block[1], /transition:[^;]*height \.2s/);
});

// The block is #atrium-root's sibling, so .sidebar's own font never reaches
// it and the title drew in the UA serif default.
test('the block names its own sans font', () => {
  const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
  const block = bare.match(/\.sbm-block \{([^}]+)\}/);
  assert.ok(block, 'no .sbm-block rule found');
  assert.match(block[1], /font:[^;]*var\(--sans/);
});

// Even 1fr tracks spread the three transport buttons apart as the sidebar
// widens. They belong together between the favicon and the volume button.
test('the transport keeps its three buttons together', () => {
  const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
  const rest = bare.match(/\.sbm-item \{([^}]+)\}/);
  assert.ok(rest, 'no .sbm-item rule found');
  assert.match(rest[1],
               /grid-template-columns:\s*auto minmax\(0, 1fr\) auto auto auto minmax\(0, 1fr\) auto/);
  assert.match(rest[1], /"fav\s+\.\s+prev\s+play\s+next\s+\.\s+vol"/);
});

// Four 24px buttons and their gaps come before the favicon: the four controls
// are what the row is for.
test('the favicon drops when the transport row runs out of room', () => {
  const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
  const queries = [...bare.matchAll(/@container sbmitem \(max-width: (\d+)px\)/g)];
  assert.equal(queries.length, 1, 'one breakpoint, not one per state');
  assert.ok(Number(queries[0][1]) >= 185,
            'the breakpoint must clear four 24px buttons, their gaps and the padding');
});

// A column gap survives the hidden favicon's track, so the leading edge spent
// two gaps against the trailing edge's one and the transport sat 17px right.
test('the narrow row spaces its buttons evenly against both edges', () => {
  const bare = css.replace(/\/\*[\s\S]*?\*\//g, '');
  const start = bare.indexOf('@container sbmitem');
  assert.ok(start >= 0, 'no sbmitem container query found');
  let depth = 0;
  let end = start;
  for (let i = bare.indexOf('{', start); i < bare.length; i++) {
    if (bare[i] === '{') depth++;
    if (bare[i] === '}' && --depth === 0) {
      end = i;
      break;
    }
  }
  const body = bare.slice(start, end);
  assert.match(body, /\.sbm-item \{[^}]*?column-gap:\s*0/s,
               'a gap of its own would not count in the even distribution');
  assert.match(body, /\.sbm-item \{[^}]*?justify-content:\s*space-evenly/s,
               'space-evenly is what makes the edges match the gaps');
  assert.match(body, /\.sbm-item \{[^}]*?padding:\s*0;/s,
               'padding sits outside the distribution and unbalances it');
  assert.match(body, /"prev\s+play\s+next\s+vol"/,
               'four tracks, or the spare room lands in a spacer again');
  assert.match(body, /grid-template-columns:\s*repeat\(4, 24px\)/,
               'an auto track grows past its button and reopens the gaps');
});

// The band is a grown-in row, not a popover, but Esc and a click outside
// still reach it: AtriumBrowserWindow::PushDismissMenus calls this.
test('the shared dismiss path closes an open volume band', () => {
  const root = installed();
  window.atriumMediaSessionsChanged([record('8')]);
  const block = document.querySelector('body > .sbm-block');
  block.querySelector('.sbm-vol-btn').click();
  assert.equal(block.querySelectorAll('.sbm-vol-open').length, 1);
  window.atriumDismissMenus();
  assert.equal(block.querySelectorAll('.sbm-vol-open').length, 0);
  assert.equal(block.style.getPropertyValue('--sbm-h'), '36px',
               'the tab list keeps the band\'s room until the height follows');
});

// A second handshake must not leave the first block's entry in the registry,
// where its close() would reach a container no longer in the document.
test('a second install releases the first block\'s dismiss entry', () => {
  const root = installed();
  installSidebarMedia(root);
  window.atriumMediaSessionsChanged([record('8')]);
  const blocks = document.querySelectorAll('body > .sbm-block');
  assert.equal(blocks.length, 1, 'the prior block must be gone');
  blocks[0].querySelector('.sbm-vol-btn').click();
  window.atriumDismissMenus();
  assert.equal(blocks[0].querySelectorAll('.sbm-vol-open').length, 0);
});
