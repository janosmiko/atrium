#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the Courtyard canvas model - items from the snapshot, lane
// rects, card y from index, the drop hit test and the zoom clamp.
//
//   make webui-test
//
// Pure, like courtyard-test.mjs: no DOM shim, because nothing here touches a
// document. A card's y is its sidebar index and nothing else, so the whole
// layout is a function of the snapshot the surface already receives.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {
  CARD_TOP,
  CARD_STEP,
  ROOM_DEFAULT_W,
  ROOM_GAP,
  ZOOM_MIN,
  ZOOM_MAX,
  cardY,
  clampZoom,
  destinationFor,
  dropDecision,
  dropIndex,
  fitView,
  itemsFrom,
  laneAt,
} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/canvas-model.js'));

const row = (id, title, canvasX) => {
  const out = {id, title, color: '#888', selected: false, url: `https://${id}/`};
  if (canvasX !== undefined) {
    out.canvasX = canvasX;
  }
  return out;
};

function snapshot() {
  return {
    activeSpace: 0,
    spaces: [
      {
        name: 'Work',
        color: '#3355ff',
        icon: 'W',
        canvas: {x: 0, y: 0, w: 300, h: 240},
        pinned: [row('p1', 'Pin')],
        favorites: [row('f1', 'Fav')],
        tabs: [row('t1', 'A', 12), row('t2', 'B'), row('t3', 'C')],
      },
      {
        name: 'Home',
        color: '#22aa66',
        icon: 'H',
        pinned: [],
        favorites: [],
        tabs: [row('t4', 'D')],
      },
    ],
  };
}

const rooms = (items) => items.filter((i) => i.kind === 'room');
const cards = (items) => items.filter((i) => i.kind === 'card');

test('a card y is its sidebar index and nothing else', () => {
  assert.equal(cardY(0), 34);
  assert.equal(cardY(1), 90);
  assert.equal(cardY(4), CARD_TOP + 4 * CARD_STEP);
});

test('the zoom clamps to the range the pinch can reach', () => {
  assert.equal(clampZoom(1), 1);
  assert.equal(clampZoom(0.1), ZOOM_MIN);
  assert.equal(clampZoom(9), ZOOM_MAX);
  assert.equal(clampZoom(Number.NaN), 1, 'a broken zoom falls back to 1');
});

test('fitView centres two rooms side by side at zoom 1 when they already fit', () => {
  const rects = [{x: 0, y: 0, w: 200, h: 150}, {x: 250, y: 0, w: 200, h: 150}];
  assert.deepEqual(fitView(rects, 1000, 800), {x: 275, y: 325, zoom: 1});
});

test('fitView shrinks a layout too big for the viewport and keeps every rect inside it', () => {
  const rects = [
    {x: 0, y: 0, w: 300, h: 240},
    {x: 2000, y: 0, w: 300, h: 240},
    {x: 0, y: 1000, w: 300, h: 240},
  ];
  const viewportW = 800;
  const viewportH = 600;
  const view = fitView(rects, viewportW, viewportH);
  assert.ok(view.zoom < 1, `a layout this big must shrink, got zoom ${view.zoom}`);
  for (const r of rects) {
    const left = r.x * view.zoom + view.x;
    const top = r.y * view.zoom + view.y;
    const right = (r.x + r.w) * view.zoom + view.x;
    const bottom = (r.y + r.h) * view.zoom + view.y;
    assert.ok(left >= 0 && top >= 0 && right <= viewportW && bottom <= viewportH,
               `rect ${JSON.stringify(r)} landed outside the viewport`);
  }
});

test('every room and every card arrives in one items list', () => {
  const items = itemsFrom(snapshot());
  assert.deepEqual(rooms(items).map((r) => r.spaceIndex), [0, 1]);
  assert.deepEqual(
      cards(items).map((c) => `${c.spaceIndex}:${c.section}:${c.index}`),
      ['0:pinned:0', '0:favorites:0', '0:tabs:0', '0:tabs:1', '0:tabs:2',
       '1:tabs:0']);
});

test('lanes are pinned, favourites then tabs, and empty sections get none', () => {
  const items = itemsFrom(snapshot());
  assert.deepEqual(rooms(items)[0].lanes.map((l) => l.section),
                   ['pinned', 'favorites', 'tabs']);
  assert.deepEqual(rooms(items)[1].lanes.map((l) => l.section), ['tabs']);
});

test('a card sits at its lane x plus its own offset, and y from its index', () => {
  const items = itemsFrom(snapshot());
  const room = rooms(items)[0];
  const lane = room.lanes.find((l) => l.section === 'tabs');
  const [a, b] = cards(items).filter((c) => c.section === 'tabs' &&
                                            c.spaceIndex === 0);
  assert.equal(a.x, lane.x + 12, 'the stored offset moves the card sideways');
  assert.equal(a.y, lane.y + cardY(0));
  assert.equal(b.x, lane.x, 'a card with no offset sits at x 0 of its lane');
  assert.equal(b.y, lane.y + cardY(1));
});

test('a card carries its lane origin, so the view can place it inside the lane', () => {
  const items = itemsFrom(snapshot());
  const room = rooms(items)[0];
  const lane = room.lanes.find((l) => l.section === 'tabs');
  const card = cards(items).find((c) => c.section === 'tabs' && c.index === 0);
  assert.equal(card.lanePlaneX, lane.x);
  assert.equal(card.lanePlaneY, lane.y);
});

test('an unplaced room lands right of the rightmost placed one, every time', () => {
  const once = rooms(itemsFrom(snapshot()))[1].rect;
  const again = rooms(itemsFrom(snapshot()))[1].rect;
  assert.deepEqual(once, again, 'the same snapshot gives the same rect');
  assert.equal(once.x, 300 + ROOM_GAP);
  assert.equal(once.y, 0);
  assert.equal(once.w, ROOM_DEFAULT_W);
});

test('a room grows in height when a lane needs it', () => {
  const snap = snapshot();
  snap.spaces[0].tabs = Array.from({length: 8}, (unused, i) => row(`t${i}`, 'x'));
  const room = rooms(itemsFrom(snap))[0];
  assert.ok(room.rect.h > 240,
            `eight cards need more than the stored 240, got ${room.rect.h}`);
});

test('the drop index is the rounded lane offset, clamped to the section', () => {
  const items = itemsFrom(snapshot());
  const lane = rooms(items)[0].lanes.find((l) => l.section === 'tabs');
  assert.equal(dropIndex(lane, lane.y + 34), 0);
  assert.equal(dropIndex(lane, lane.y + 34 + 56), 1);
  assert.equal(dropIndex(lane, lane.y - 500), 0, 'above the lane clamps to 0');
  assert.equal(dropIndex(lane, lane.y + 9999), 3, 'below clamps to the length');
});

test('a point outside every lane hits nothing', () => {
  const items = itemsFrom(snapshot());
  assert.equal(laneAt(items, -900, -900), null);
});

test('a vertical drag in the tabs lane reorders that section', () => {
  const items = itemsFrom(snapshot());
  const lane = rooms(items)[0].lanes.find((l) => l.section === 'tabs');
  const drag = {spaceIndex: 0, section: 'tabs', index: 0, url: 'https://t1/'};
  const at = {x: lane.x + 4, y: lane.y + cardY(2)};
  assert.deepEqual(dropDecision(drag, at, items, 0),
                   {send: 'sidebarTabReordered', args: [0, 1, 0]});
});

test('a sideways drag in the same lane sets canvasX and changes no order', () => {
  const items = itemsFrom(snapshot());
  const lane = rooms(items)[0].lanes.find((l) => l.section === 'tabs');
  const drag = {spaceIndex: 0, section: 'tabs', index: 0, url: 'https://t1/'};
  const at = {x: lane.x + 40, y: lane.y + cardY(0)};
  assert.deepEqual(dropDecision(drag, at, items, 0),
                   {send: 'courtyardCardX', args: [0, 'tabs', 0, 40]});
});

test('the pinned and favourites lanes reorder through their own messages', () => {
  const items = itemsFrom(snapshot());
  const snap = snapshot();
  snap.spaces[0].pinned = [row('p1', 'Pin'), row('p2', 'Pin2')];
  snap.spaces[0].favorites = [row('f1', 'Fav'), row('f2', 'Fav2')];
  const grown = itemsFrom(snap);
  const pinLane = rooms(grown)[0].lanes.find((l) => l.section === 'pinned');
  const favLane = rooms(grown)[0].lanes.find((l) => l.section === 'favorites');
  assert.deepEqual(
      dropDecision({spaceIndex: 0, section: 'pinned', index: 0, url: 'u'},
                   {x: pinLane.x, y: pinLane.y + cardY(2)}, grown, 0),
      {send: 'sidebarPinnedReordered', args: [0, 1, 0]});
  assert.deepEqual(
      dropDecision({spaceIndex: 0, section: 'favorites', index: 0, url: 'u'},
                   {x: favLane.x, y: favLane.y + cardY(2)}, grown, 0),
      {send: 'sidebarFavoriteReordered', args: [0, 1, 0]});
  assert.ok(items, 'the unmodified snapshot is still readable');
});

test('a cross-lane drop pins, unpins, favourites or unfavourites at the slot', () => {
  const items = itemsFrom(snapshot());
  const room = rooms(items)[0];
  const pinLane = room.lanes.find((l) => l.section === 'pinned');
  const favLane = room.lanes.find((l) => l.section === 'favorites');
  const tabLane = room.lanes.find((l) => l.section === 'tabs');

  assert.deepEqual(
      dropDecision({spaceIndex: 0, section: 'tabs', index: 1, url: 'https://t2/'},
                   {x: pinLane.x, y: pinLane.y + cardY(1)}, items, 0),
      {send: 'sidebarTabPinned', args: [1, 1, 0]});
  assert.deepEqual(
      dropDecision({spaceIndex: 0, section: 'pinned', index: 0, url: 'https://p1/'},
                   {x: tabLane.x, y: tabLane.y + cardY(2)}, items, 0),
      {send: 'sidebarTabUnpinned', args: [0, 2, 0]});
  assert.deepEqual(
      dropDecision({spaceIndex: 0, section: 'tabs', index: 2, url: 'https://t3/'},
                   {x: favLane.x, y: favLane.y + cardY(0)}, items, 0),
      {send: 'sidebarRowFavorited', args: [2, 'tabs', 0]});
  assert.deepEqual(
      dropDecision({spaceIndex: 0, section: 'favorites', index: 0,
                    url: 'https://f1/'},
                   {x: tabLane.x, y: tabLane.y + cardY(1)}, items, 0),
      {send: 'sidebarFavoriteMovedOut', args: ['https://f1/', 'tabs', 1]});
});

test('a sideways drag inside the favourites lane is refused, not stored as x', () => {
  const items = itemsFrom(snapshot());
  const lane = rooms(items)[0].lanes.find((l) => l.section === 'favorites');
  const drag = {spaceIndex: 0, section: 'favorites', index: 0, url: 'https://f1/'};
  const at = {x: lane.x + 40, y: lane.y + cardY(0)};
  assert.equal(dropDecision(drag, at, items, 0), null,
               'favourites have nowhere to keep a stored x offset');
});

test('a drop in another room is a move to that space at the drop index', () => {
  const items = itemsFrom(snapshot());
  const lane = rooms(items)[1].lanes.find((l) => l.section === 'tabs');
  assert.deepEqual(
      dropDecision({spaceIndex: 0, section: 'tabs', index: 1, url: 'https://t2/'},
                   {x: lane.x, y: lane.y + cardY(1)}, items, 0),
      {send: 'sidebarTabMovedToSpace', args: [0, 1, 'tabs', 1, 1]});
});

test('a cross-room drop only lands in the target tabs lane', () => {
  const snap = snapshot();
  snap.spaces[1].favorites = [row('f9', 'Fav')];
  snap.spaces[1].pinned = [row('p9', 'Pin')];
  const items = itemsFrom(snap);
  const room1 = rooms(items)[1];
  const tabLane = room1.lanes.find((l) => l.section === 'tabs');
  const favLane = room1.lanes.find((l) => l.section === 'favorites');
  const pinLane = room1.lanes.find((l) => l.section === 'pinned');

  assert.equal(
      dropDecision({spaceIndex: 0, section: 'favorites', index: 0,
                    url: 'https://f1/'},
                   {x: tabLane.x, y: tabLane.y + cardY(0)}, items, 0),
      null, 'a favourite has no regular list in another room to join');
  assert.equal(
      dropDecision({spaceIndex: 0, section: 'tabs', index: 0, url: 'https://t1/'},
                   {x: favLane.x, y: favLane.y + cardY(0)}, items, 0),
      null, 'favouriting is per profile, not addressed by a cross-room drop');
  assert.equal(
      dropDecision({spaceIndex: 0, section: 'tabs', index: 0, url: 'https://t1/'},
                   {x: pinLane.x, y: pinLane.y + cardY(0)}, items, 0),
      null, 'the move export takes no target section, so a pinned lane snaps back');
  assert.equal(
      dropDecision({spaceIndex: 0, section: 'pinned', index: 0,
                    url: 'https://p1/'},
                   {x: pinLane.x, y: pinLane.y + cardY(0)}, items, 0),
      null, 'a pinned row lands in another room the same way, or not at all');
});

test('an unfavourite drop outside the active room is refused', () => {
  const snap = snapshot();
  snap.spaces[1].favorites = [row('f9', 'Fav')];
  const items = itemsFrom(snap);
  const lane = rooms(items)[1].lanes.find((l) => l.section === 'tabs');
  assert.equal(
      dropDecision({spaceIndex: 1, section: 'favorites', index: 0,
                    url: 'https://f9/'},
                   {x: lane.x, y: lane.y + cardY(0)}, items, 0),
      null,
      'the handler opens or activates a tab in the current browser, so it ' +
          'only means anything in the active room');
});

test('a drop outside every lane is no move at all', () => {
  const items = itemsFrom(snapshot());
  assert.equal(
      dropDecision({spaceIndex: 0, section: 'tabs', index: 0, url: 'u'},
                   {x: -900, y: -900}, items, 0),
      null);
});

test('destinationFor keeps the sidebar remove-then-insert adjustment', () => {
  assert.equal(destinationFor(0, 2), 1);
  assert.equal(destinationFor(2, 0), 0);
  assert.equal(destinationFor(1, 1), null);
});

// Cards M1, M2, P1, P2 are 0..3, but the core counts pinned ITEMS: the folder
// is item 0, P1 item 1, P2 item 2.
function folderSnapshot() {
  const snap = snapshot();
  snap.spaces[0].pinned = [
    {kind: 'folder', id: 'G', tabs: [row('m1', 'M1'), row('m2', 'M2')]},
    row('p1', 'P1'),
    row('p2', 'P2'),
  ];
  snap.spaces[1].pinned = [row('p9', 'Pin')];
  return snap;
}

function folderLanes() {
  const items = itemsFrom(folderSnapshot());
  const lane = (room, section) =>
      rooms(items)[room].lanes.find((l) => l.section === section);
  return {items, lane};
}

// Below the last card's middle, still inside the lane.
const END = cardY(3) + CARD_STEP / 2 + 2;

const P1 = {spaceIndex: 0, section: 'pinned', index: 2, url: 'https://p1/'};
const M2 = {spaceIndex: 0, section: 'pinned', index: 1, url: 'https://m2/'};

test('a pin after a folder reorders by its own item slot', () => {
  const {items, lane} = folderLanes();
  const pin = lane(0, 'pinned');
  assert.deepEqual(
      dropDecision(P1, {x: pin.x, y: pin.y + cardY(0)}, items, 0),
      {send: 'sidebarPinnedReordered', args: [1, 0, 0]});
  assert.deepEqual(
      dropDecision(P1, {x: pin.x, y: pin.y + END}, items, 0),
      {send: 'sidebarPinnedReordered', args: [1, 2, 0]});
});

test('a pin after a folder unpins, favourites and moves space as its own item', () => {
  const {items, lane} = folderLanes();
  assert.deepEqual(
      dropDecision(P1, {x: lane(0, 'tabs').x, y: lane(0, 'tabs').y + cardY(1)},
                   items, 0),
      {send: 'sidebarTabUnpinned', args: [1, 1, 0]});
  assert.deepEqual(
      dropDecision(P1,
                   {x: lane(0, 'favorites').x,
                    y: lane(0, 'favorites').y + cardY(0)},
                   items, 0),
      {send: 'sidebarRowFavorited', args: [1, 'pinned', 0]});
  assert.deepEqual(
      dropDecision(P1, {x: lane(1, 'tabs').x, y: lane(1, 'tabs').y + cardY(0)},
                   items, 0),
      {send: 'sidebarTabMovedToSpace', args: [0, 1, 'pinned', 1, 0]});
});

test('a row pinned below a folder lands at the item slot the card names', () => {
  const {items, lane} = folderLanes();
  const pin = lane(0, 'pinned');
  const t1 = {spaceIndex: 0, section: 'tabs', index: 0, url: 'https://t1/'};
  assert.deepEqual(
      dropDecision(t1, {x: pin.x, y: pin.y + cardY(3)}, items, 0),
      {send: 'sidebarTabPinned', args: [0, 2, 0]}, 'before P2 is item 2');
  assert.deepEqual(
      dropDecision(t1, {x: pin.x, y: pin.y + cardY(1)}, items, 0),
      {send: 'sidebarTabPinned', args: [0, 0, 0]},
      'inside the folder lands before it, never splitting it');
});

test('a folder member leaves its folder through the member call', () => {
  const {items, lane} = folderLanes();
  assert.deepEqual(
      dropDecision(M2, {x: lane(0, 'tabs').x, y: lane(0, 'tabs').y + cardY(0)},
                   items, 0),
      {send: 'sidebarTabUngrouped', args: ['G', 1, 'tabs', 0, 0]});
  const pin = lane(0, 'pinned');
  assert.deepEqual(
      dropDecision(M2, {x: pin.x, y: pin.y + END}, items, 0),
      {send: 'sidebarTabUngrouped', args: ['G', 1, 'pinned', 3, 0]});
});

test('a folder member drop the member call cannot express is refused', () => {
  const {items, lane} = folderLanes();
  const pin = lane(0, 'pinned');
  assert.equal(
      dropDecision(M2, {x: pin.x + 40, y: pin.y + cardY(1)}, items, 0), null,
      'inside its own folder, and a member has no stored x');
  assert.equal(
      dropDecision(M2,
                   {x: lane(0, 'favorites').x,
                    y: lane(0, 'favorites').y + cardY(0)},
                   items, 0),
      null, 'the favourite call counts pinned items, a member is none');
  assert.equal(
      dropDecision(M2, {x: lane(1, 'tabs').x, y: lane(1, 'tabs').y + cardY(0)},
                   items, 0),
      null, 'the move call counts pinned items, a member is none');
});
