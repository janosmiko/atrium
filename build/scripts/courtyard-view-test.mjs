#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the Courtyard's rendered DOM - the plane, pan and zoom.
//
//   make courtyard-view-test (part of make webui-test)

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');

const h = installDOM({pathname: '/courtyard'});
const {installCourtyard} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/courtyard-view.js'));

// Pins the overlay-language conversion: courtyard.css reads colour, radius
// and shadow from overlay.css's --ov-* tokens, the way commandbar.css and
// hint.css do, rather than a literal that could drift back in.
const courtyardCss = fs.readFileSync(
    path.join(SRC, 'chrome/browser/resources/atrium/courtyard.css'), 'utf8');

test('courtyard.css carries no literal colour', () => {
  assert.doesNotMatch(courtyardCss, /rgba?\(|#[0-9a-fA-F]{3,8}\b/,
      'a literal colour belongs in overlay.css as a token, not here');
});

for (const token of ['--ov-ground', '--ov-radius-card', '--ov-radius-row',
                     '--ov-shadow', '--ov-font', '--ov-tile-ink',
                     '--ov-tile-shadow', '--ov-border', '--ov-sel']) {
  test(`courtyard.css reads ${token} from overlay.css`, () => {
    assert.match(courtyardCss, new RegExp('var\\(' + token + '\\)'));
  });
}

// courtyard-view.js has always put .sel on the active tab and courtyard.css had
// no rule for it, so the plane showed no active tab. A token named somewhere in
// the file is not the same as a token reaching this selector.
test('the active tab carries the shared selection wash', () => {
  const rule = /\.rtab\.sel\s*{([^}]*)}/.exec(courtyardCss);
  assert.ok(rule, '.rtab.sel has no rule at all');
  assert.match(rule[1], /background:\s*var\(--ov-sel\)/);
});

test('a room wears the shared card border', () => {
  const rule = /\n\.room\s*{([^}]*)}/.exec(courtyardCss);
  assert.ok(rule, '.room has no rule');
  assert.match(rule[1], /border:\s*1px\s+solid\s+var\(--ov-border\)/);
});

const fire = (node, type, init = {}) => {
  const event = new Event(type, {bubbles: true, ...init});
  node.dispatchEvent(event);
  return event;
};

const row = (id, title) =>
    ({id, title, color: '#888', selected: false, url: `https://${title}.test/`});

function snapshot(activeSpace = 0) {
  // Live ids reach the ACTIVE space alone (sidebar::snapshot consumes the
  // window's live pool once), so every row of the other room is cold and
  // carries an empty id, exactly as the real push does.
  const live = (space) => (id, title) => row(space === activeSpace ? id : '', title);
  const work = live(0);
  const home = live(1);
  return {
    activeSpace,
    spaces: [
      {
        name: 'Work',
        color: '#3355ff',
        icon: 'W',
        canvas: {x: 0, y: 0, w: 300, h: 240},
        pinned: [work('p1', 'Pin')],
        favorites: [],
        tabs: [work('t1', 'a'), work('t2', 'b')],
      },
      {
        name: 'Home',
        color: '#22aa66',
        icon: 'H',
        pinned: [],
        favorites: [],
        tabs: [home('t3', 'c')],
      },
    ],
  };
}

function open(activeSpace = 0) {
  h.reset();
  h.document.body.replaceChildren();
  const root = h.document.createElement('div');
  h.document.body.appendChild(root);
  installCourtyard(root);
  h.window.atriumSetCourtyard(snapshot(activeSpace));
  return root;
}

// h.sent entries are [name, args] pairs, not {name, args} objects - adapt to
// the shape the assertions below expect.
const sent = (name) => h.sent
    .filter(([n]) => n === name)
    .map(([n, args]) => ({name: n, args}));

test('installCourtyard sends courtyardReady with its push receiver ready', () => {
  h.reset();
  const root = h.document.createElement('div');
  h.document.body.appendChild(root);
  installCourtyard(root);
  assert.equal(typeof h.window.atriumSetCourtyard, 'function');
  assert.equal(sent('courtyardReady').length, 1);
});

test('every space draws as a room on one transformed plane', () => {
  const root = open();
  const plane = root.querySelector('.plane');
  assert.ok(plane, 'the grid is gone and a plane replaces it');
  assert.equal(plane.querySelectorAll('.room').length, 2);
  assert.equal(plane.querySelector('.room[data-room="0"] .rlane[data-section="tabs"] .rtab[data-index="1"]')
                   .textContent.includes('b'),
               true);
});

test('a room header draws a ph: icon as a Phosphor svg, not the literal string', () => {
  h.reset();
  h.document.body.replaceChildren();
  const root = h.document.createElement('div');
  h.document.body.appendChild(root);
  installCourtyard(root);
  h.window.atriumSetCourtyard({
    activeSpace: 0,
    spaces: [{name: 'Work', color: '#3355ff', icon: 'ph:star',
             canvas: {x: 0, y: 0, w: 300, h: 240},
             pinned: [], favorites: [], tabs: []}],
  });
  const svg = root.querySelector('.room[data-room="0"] .ric').firstChild;
  assert.equal(svg.tagName.toLowerCase(), 'svg');
  assert.equal(svg.dataset.icon, 'star');
});

test('a room is placed in plane units, not by a grid', () => {
  const root = open();
  const first = root.querySelector('.room[data-room="0"]');
  const second = root.querySelector('.room[data-room="1"]');
  assert.equal(first.style.left, '0px');
  assert.equal(first.style.top, '0px');
  assert.equal(second.style.left, '340px',
               'the unplaced room sits one gap right of the placed one');
});

test('a card sits at 34 plus 56 per index inside its lane', () => {
  const root = open();
  const lane = root.querySelector('.room[data-room="0"] .rlane[data-section="tabs"]');
  assert.equal(lane.querySelector('.rtab[data-index="0"]').style.top, '34px');
  assert.equal(lane.querySelector('.rtab[data-index="1"]').style.top, '90px');
});

test('a wheel pans the plane and never zooms it', () => {
  const root = open();
  const plane = root.querySelector('.plane');
  fire(root.querySelector('.courtwrap'), 'wheel',
       {deltaX: 30, deltaY: 20, ctrlKey: false});
  assert.equal(plane.style.transform,
               'translate(-30px, -20px) scale(1)');
});

test('ctrl+wheel zooms between the floor and the ceiling', () => {
  const root = open();
  const plane = root.querySelector('.plane');
  const wrap = root.querySelector('.courtwrap');
  for (let i = 0; i < 40; i++) {
    fire(wrap, 'wheel', {deltaX: 0, deltaY: 200, ctrlKey: true});
  }
  assert.ok(plane.style.transform.includes('scale(0.35)'),
            `the zoom stops at the floor, got ${plane.style.transform}`);
  for (let i = 0; i < 200; i++) {
    fire(wrap, 'wheel', {deltaX: 0, deltaY: -200, ctrlKey: true});
  }
  assert.ok(plane.style.transform.includes('scale(2)'),
            `the zoom stops at the ceiling, got ${plane.style.transform}`);
});

test('a ctrl+wheel is prevented, so Blink never runs its own page zoom', () => {
  const root = open();
  const event = fire(root.querySelector('.courtwrap'), 'wheel',
                      {deltaX: 0, deltaY: 200, ctrlKey: true});
  assert.equal(event.defaultPrevented, true);
});

test('the pinch entrypoint zooms the same plane C++ will drive', () => {
  const root = open();
  const plane = root.querySelector('.plane');
  assert.equal(typeof h.window.atriumCourtyardPinch, 'function');
  for (let i = 0; i < 40; i++) {
    h.window.atriumCourtyardPinch(200);
  }
  assert.ok(plane.style.transform.includes('scale(0.35)'));
  assert.ok(sent('courtyardZoom').length > 0,
            'the plane reports its zoom, which is how the close at the floor ' +
                'is decided');
  const last = sent('courtyardZoom').at(-1);
  assert.deepEqual(last.args, [0.35]);
});

test('a spread pinch zooms the plane in to the ceiling', () => {
  const root = open();
  const plane = root.querySelector('.plane');
  for (let i = 0; i < 40; i++) {
    h.window.atriumCourtyardPinch(-200);
  }
  assert.equal(plane.style.transform, 'translate(0px, 0px) scale(2)');
  assert.deepEqual(sent('courtyardZoom').at(-1).args, [2]);
});

test('clicking a room enters that space and closes', () => {
  const root = open();
  root.querySelector('.room[data-room="1"] .rhead').dispatchEvent(
      new Event('click', {bubbles: true}));
  assert.deepEqual(sent('sidebarSpaceSelected').at(-1).args, [1]);
  assert.equal(sent('courtyardDismiss').length, 1);
});

test('clicking the room you opened from closes with no space switch', () => {
  const root = open();
  root.querySelector('.room[data-room="0"] .rhead').dispatchEvent(
      new Event('click', {bubbles: true}));
  assert.equal(sent('sidebarSpaceSelected').length, 0);
  assert.equal(sent('courtyardDismiss').length, 1);
});

test('Escape and a click on bare plane both dismiss', () => {
  let root = open();
  fire(root.querySelector('.courtwrap'), 'keydown', {key: 'Escape'});
  assert.equal(sent('courtyardDismiss').length, 1);

  root = open();
  root.querySelector('.plane').dispatchEvent(
      new Event('click', {bubbles: true}));
  assert.equal(sent('courtyardDismiss').length, 1);
});

function drag(node, from, to) {
  fire(node, 'pointerdown', {pointerId: 1, clientX: from.x, clientY: from.y});
  fire(node, 'pointermove', {pointerId: 1, clientX: to.x, clientY: to.y});
  fire(node, 'pointerup', {pointerId: 1, clientX: to.x, clientY: to.y});
}

test('dragging a room title moves the room and persists the rect', () => {
  const root = open();
  const room = root.querySelector('.room[data-room="0"]');
  drag(room.querySelector('.rhead'), {x: 20, y: 20}, {x: 140, y: 90});
  assert.equal(room.style.left, '120px');
  assert.equal(room.style.top, '70px');
  assert.deepEqual(sent('courtyardRoomRect').at(-1).args, [0, 120, 70, 300, 240]);
});

test('dragging the corner grip resizes the room and persists the rect', () => {
  const root = open();
  const room = root.querySelector('.room[data-room="0"]');
  drag(room.querySelector('.rgrip'), {x: 300, y: 240}, {x: 400, y: 320});
  assert.deepEqual(sent('courtyardRoomRect').at(-1).args, [0, 0, 0, 400, 320]);
});

test('a resize below the tallest lane is refused, not persisted small', () => {
  const root = open();
  const room = root.querySelector('.room[data-room="0"]');
  drag(room.querySelector('.rgrip'), {x: 300, y: 240}, {x: 60, y: 10});
  const rect = sent('courtyardRoomRect').at(-1).args;
  assert.ok(rect[4] >= 90,
            `two cards need at least 90 units of height, got ${rect[4]}`);
});

test('a room drag never reorders the spaces', () => {
  const root = open();
  const room = root.querySelector('.room[data-room="1"]');
  drag(room.querySelector('.rhead'), {x: 360, y: 20}, {x: 10, y: 10});
  assert.equal(sent('sidebarSpaceReordered').length, 0);
  assert.deepEqual(
      [...root.querySelectorAll('.room')].map((r) => r.dataset.room),
      ['0', '1'],
      'the file order is the room order, whatever the rects say');
});

test('a room drag is not a click, so it never enters the space', () => {
  const root = open();
  const room = root.querySelector('.room[data-room="1"]');
  drag(room.querySelector('.rhead'), {x: 360, y: 20}, {x: 500, y: 90});
  assert.equal(sent('sidebarSpaceSelected').length, 0);
  assert.equal(sent('courtyardDismiss').length, 0);
});

test('a snapshot push during a drag cancels it and re-renders', () => {
  const root = open();
  const head = root.querySelector('.room[data-room="0"] .rhead');
  fire(head, 'pointerdown', {pointerId: 1, clientX: 20, clientY: 20});
  h.window.atriumSetCourtyard(snapshot(0));
  fire(head, 'pointermove', {pointerId: 1, clientX: 400, clientY: 400});
  fire(head, 'pointerup', {pointerId: 1, clientX: 400, clientY: 400});
  assert.equal(sent('courtyardRoomRect').length, 0,
               'the cancelled drag wrote no rect');
});

test('a press and release at the same point is not a drag', () => {
  const root = open();
  const head = root.querySelector('.room[data-room="0"] .rhead');
  fire(head, 'pointerdown', {pointerId: 1, clientX: 20, clientY: 20});
  fire(head, 'pointerup', {pointerId: 1, clientX: 20, clientY: 20});
  assert.equal(sent('courtyardRoomRect').length, 0);
});

test('pointercancel during a resize discards the rect, not commits it', () => {
  const root = open();
  const room = root.querySelector('.room[data-room="0"]');
  fire(room.querySelector('.rgrip'), 'pointerdown',
       {pointerId: 1, clientX: 300, clientY: 240});
  fire(room.querySelector('.rgrip'), 'pointermove',
       {pointerId: 1, clientX: 360, clientY: 300});
  fire(room.querySelector('.rgrip'), 'pointercancel',
       {pointerId: 1, clientX: 360, clientY: 300});
  assert.equal(sent('courtyardRoomRect').length, 0,
               'the cancelled resize wrote no rect');
  const rerendered = root.querySelector('.room[data-room="0"]');
  assert.equal(rerendered.style.width, '300px');
  assert.equal(rerendered.style.height, '240px');
});

test('a click after a pointercancel activates the card on the first try', () => {
  const root = open();
  const selector =
      '.room[data-room="0"] .rlane[data-section="tabs"] .rtab[data-index="0"]';
  const card = root.querySelector(selector);
  fire(card, 'pointerdown', {pointerId: 1, clientX: 10, clientY: 34});
  fire(card, 'pointermove', {pointerId: 1, clientX: 60, clientY: 34});
  fire(card, 'pointercancel', {pointerId: 1, clientX: 60, clientY: 34});
  // The cancel re-renders, so re-query rather than click the detached node.
  root.querySelector(selector).dispatchEvent(
      new Event('click', {bubbles: true}));
  assert.deepEqual(sent('courtyardCardActivated').at(-1).args, [0, 'tabs', 0]);
});

test('a vertical card drag sends the sidebar reorder for that room', () => {
  const root = open();
  const card = root.querySelector(
      '.room[data-room="0"] .rlane[data-section="tabs"] .rtab[data-index="0"]');
  drag(card, {x: 10, y: 34}, {x: 10, y: 34 + 112});
  assert.deepEqual(sent('sidebarTabReordered').at(-1).args, [0, 1, 0]);
});

test('a sideways card drag sends courtyardCardX and no reorder', () => {
  const root = open();
  const card = root.querySelector(
      '.room[data-room="0"] .rlane[data-section="tabs"] .rtab[data-index="0"]');
  drag(card, {x: 10, y: 34}, {x: 70, y: 34});
  assert.equal(sent('sidebarTabReordered').length, 0);
  assert.deepEqual(sent('courtyardCardX').at(-1).args, [0, 'tabs', 0, 60]);
});

test('a drop in the pinned lane of the same room pins at the slot', () => {
  const root = open();
  const card = root.querySelector(
      '.room[data-room="0"] .rlane[data-section="tabs"] .rtab[data-index="1"]');
  const pinLane = root.querySelector(
      '.room[data-room="0"] .rlane[data-section="pinned"]');
  drag(card, {x: 160, y: 90}, {x: 10, y: 34});
  assert.deepEqual(sent('sidebarTabPinned').at(-1).args, [1, 0, 0]);
  assert.ok(pinLane, 'the pinned lane is drawn, so it can be dropped into');
});

test('a drop in another room moves the tab to that space at the slot', () => {
  const root = open();
  const card = root.querySelector(
      '.room[data-room="0"] .rlane[data-section="tabs"] .rtab[data-index="1"]');
  drag(card, {x: 10, y: 90}, {x: 350, y: 34});
  assert.deepEqual(sent('sidebarTabMovedToSpace').at(-1).args,
                   [0, 1, 'tabs', 1, 0]);
});

test('a drop on bare plane is no move and no dismiss', () => {
  const root = open();
  const card = root.querySelector(
      '.room[data-room="0"] .rlane[data-section="tabs"] .rtab[data-index="0"]');
  drag(card, {x: 10, y: 34}, {x: -900, y: -900});
  assert.equal(sent('sidebarTabReordered').length, 0);
  assert.equal(sent('sidebarTabMovedToSpace').length, 0);
  assert.equal(sent('courtyardDismiss').length, 0,
               'a card that snapped back is not a click on empty plane');
});

test('a click on a card activates the tab rather than dragging it', () => {
  const root = open();
  const card = root.querySelector(
      '.room[data-room="0"] .rlane[data-section="tabs"] .rtab[data-index="0"]');
  drag(card, {x: 10, y: 34}, {x: 10, y: 34});
  card.dispatchEvent(new Event('click', {bubbles: true}));
  assert.deepEqual(sent('courtyardCardActivated').at(-1).args, [0, 'tabs', 0]);
  assert.equal(sent('courtyardDismiss').length, 1);
});

test('a card click in another room names that room, not a tab id', () => {
  const root = open();
  const card = root.querySelector(
      '.room[data-room="1"] .rlane[data-section="tabs"] .rtab[data-index="0"]');
  assert.equal(card.dataset.tabId, undefined,
               'a cold row has no live id, which is why the click sends none');
  card.dispatchEvent(new Event('click', {bubbles: true}));
  assert.deepEqual(sent('courtyardCardActivated').at(-1).args, [1, 'tabs', 0]);
  assert.equal(sent('sidebarTabClicked').length, 0);
  assert.equal(sent('courtyardDismiss').length, 1);
});

test('the close hook re-fits every room on the next open', () => {
  h.reset();
  h.document.body.replaceChildren();
  const root = h.document.createElement('div');
  h.document.body.appendChild(root);
  installCourtyard(root);
  const wrap = root.querySelector('.courtwrap');
  // The shim runs no layout engine, so the viewport fitAllRooms measures has
  // to be declared.
  wrap.clientWidth = 800;
  wrap.clientHeight = 600;
  h.window.atriumSetCourtyard(snapshot(0));

  const plane = root.querySelector('.plane');
  const fitted = plane.style.transform;
  // The 300x240 Work room and the unplaced 320x240 Home room bound a
  // 660x240 box. At zoom 1 in an 800x600 view its centre lands at (70, 180).
  assert.equal(fitted, 'translate(70px, 180px) scale(1)',
               'both rooms sit centred in an 800x600 view');

  for (let i = 0; i < 10; i++) {
    fire(wrap, 'wheel', {deltaX: 0, deltaY: 200, ctrlKey: true});
  }
  fire(wrap, 'wheel', {deltaX: 40, deltaY: 40, ctrlKey: false});
  assert.notEqual(plane.style.transform, fitted,
                  'the plane really was panned and zoomed away');

  // Mutation this pins: a Close() that only hides the widget leaves the
  // session open, so the next push re-renders at the old pan and zoom.
  h.window.atriumCourtyardClosed();
  h.window.atriumSetCourtyard(snapshot(0));
  assert.equal(plane.style.transform, fitted,
               'the next open starts fitted again');
});

test('a cancelled drag swallows the release, so nothing is clicked', () => {
  const root = open();
  const card = root.querySelector(
      '.room[data-room="0"] .rlane[data-section="tabs"] .rtab[data-index="0"]');
  fire(card, 'pointerdown', {pointerId: 1, clientX: 10, clientY: 34});
  h.window.atriumSetCourtyard(snapshot(0));
  fire(root.querySelector('.plane'), 'pointerup',
       {pointerId: 1, clientX: 10, clientY: 34});
  root.querySelector('.plane').dispatchEvent(
      new Event('click', {bubbles: true}));
  assert.equal(sent('courtyardDismiss').length, 0,
               'the release after a cancelled drag is not a click on the plane');
  assert.equal(sent('courtyardCardActivated').length, 0);
});

test('a drag on bare plane pans and is not a click', () => {
  const root = open();
  const plane = root.querySelector('.plane');
  drag(plane, {x: 200, y: 200}, {x: 260, y: 240});
  assert.equal(plane.style.transform, 'translate(60px, 40px) scale(1)');
  plane.dispatchEvent(new Event('click', {bubbles: true}));
  assert.equal(sent('courtyardDismiss').length, 0,
               'a pan that moved is not the click-away that dismisses');
});

test('a press and release on bare plane with no move still dismisses', () => {
  const root = open();
  const plane = root.querySelector('.plane');
  drag(plane, {x: 200, y: 200}, {x: 200, y: 200});
  plane.dispatchEvent(new Event('click', {bubbles: true}));
  assert.equal(sent('courtyardDismiss').length, 1);
});

test('a pinned folder flattens into a card per member tab', () => {
  h.reset();
  h.document.body.replaceChildren();
  const root = h.document.createElement('div');
  h.document.body.appendChild(root);
  installCourtyard(root);
  h.window.atriumSetCourtyard({
    activeSpace: 0,
    spaces: [{
      name: 'Work',
      color: '#3355ff',
      icon: 'W',
      canvas: {x: 0, y: 0, w: 300, h: 240},
      pinned: [{
        kind: 'folder',
        id: 'fold1',
        name: 'Folder',
        icon: 'F',
        closed: false,
        tabs: [row('f1', 'Nested1'), row('f2', 'Nested2')],
      }],
      favorites: [],
      tabs: [],
    }],
  });
  const lane =
      '.room[data-room="0"] .rlane[data-section="pinned"] .rtab[data-index="1"]';
  const card = root.querySelector(lane);
  assert.ok(card, 'the folder\'s second member draws as its own card');
  assert.equal(card.textContent.includes('Nested2'), true);
  card.dispatchEvent(new Event('click', {bubbles: true}));
  assert.deepEqual(sent('courtyardCardActivated').at(-1).args, [0, 'pinned', 1]);
});
