#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// what a cross-section drop in the HTML sidebar resolves to.
//
//   make drop-test
//
// The trap this pins down: a drop that CROSSES sections must not apply the
// remove-then-insert adjustment. destinationFor() exists because a row leaving
// its old slot shifts everything after it - but that only happens when the row
// leaves the same list it is being inserted into. Pinning a tab removes it from
// `tabs` and inserts it into `pinnedTabs`, so the pinned list never shifts, and
// adjusting there silently drops the row one slot short on every downward drop.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {dropDecision, splitEdgeFor} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/sidebar-drag.js'));

const drop = (fromSection, index, toSection, slot) =>
    dropDecision({from: {section: fromSection, index}, to: {section: toSection, slot}});

// Same call with the space each end belongs to. A drag that pages to another
// space while in flight starts in one space and lands in another.
const across = (fromSpace, fromSection, index, toSpace, toSection, slot) =>
    dropDecision({
      from: {section: fromSection, index, space: fromSpace},
      to: {section: toSection, slot, space: toSpace},
    });

test('a drop inside the tab list is a reorder', () => {
  assert.deepEqual(drop('tabs', 0, 'tabs', 3),
      {kind: 'reorder', from: 0, to: 2});
  assert.deepEqual(drop('tabs', 3, 'tabs', 0),
      {kind: 'reorder', from: 3, to: 0});
});

test('a drop inside the pinned list is a pinned reorder', () => {
  // A different kind, because it is a different array in the model. Sending
  // this as a plain reorder would move a row in the regular list instead.
  assert.deepEqual(drop('pinned', 2, 'pinned', 0),
      {kind: 'reorderPinned', from: 2, to: 0});
});

test('a drop inside the folders list is a folder reorder', () => {
  // a folder head reorders among folders, its own kind for the same
  // reason a pinned reorder is not a plain reorder - a different list in the
  // model.
  assert.deepEqual(drop('folders', 0, 'folders', 2),
      {kind: 'reorderFolder', from: 0, to: 1});
});

test('a drop onto a row\'s own position is nothing at all', () => {
  assert.equal(drop('tabs', 2, 'tabs', 2), null);
  assert.equal(drop('tabs', 2, 'tabs', 3), null);
  assert.equal(drop('pinned', 1, 'pinned', 1), null);
  assert.equal(drop('pinned', 1, 'pinned', 2), null);
  assert.equal(drop('folders', 1, 'folders', 1), null);
  assert.equal(drop('folders', 1, 'folders', 2), null);
});

// THE cross-section trap.
test('crossing into the pinned list keeps the slot unadjusted', () => {
  // Tab 0 dropped at the END of a 3-row pinned list belongs at pinned index 3.
  // With the same-list adjustment it would land at 2 - one short, every time,
  // and only when dragging into a section rather than within one.
  assert.deepEqual(drop('tabs', 0, 'pinned', 3),
      {kind: 'pin', from: 0, to: 3});
  assert.deepEqual(drop('tabs', 5, 'pinned', 0),
      {kind: 'pin', from: 5, to: 0});
});

test('crossing out of the pinned list keeps the slot unadjusted', () => {
  assert.deepEqual(drop('pinned', 0, 'tabs', 4),
      {kind: 'unpin', from: 0, to: 4});
  assert.deepEqual(drop('pinned', 2, 'tabs', 0),
      {kind: 'unpin', from: 2, to: 0});
});

test('a cross-section drop at the same numeric index is still a real move', () => {
  // Index 1 in `tabs` and index 1 in `pinnedTabs` are different rows in
  // different arrays. The no-op check must not fire across sections.
  assert.deepEqual(drop('tabs', 1, 'pinned', 1), {kind: 'pin', from: 1, to: 1});
  assert.deepEqual(drop('pinned', 1, 'tabs', 1), {kind: 'unpin', from: 1, to: 1});
});

test('a drop on the favourites grid promotes the row', () => {
  // the slot travels. The grid used to append whatever landed on
  // it, so the tile appeared somewhere the pointer had never been.
  assert.deepEqual(drop('tabs', 2, 'favorites', 0),
      {kind: 'favorite', from: 2, fromSection: 'tabs', to: 0});
  assert.deepEqual(drop('pinned', 0, 'favorites', 3),
      {kind: 'favorite', from: 0, fromSection: 'pinned', to: 3});
});

test('a promoted row with no resolvable grid slot still appends', () => {
  // A grid with no laid-out tile has nothing to be nearest to, and nearestSlot
  // answers with its length - zero. An absent slot is the older shape and must
  // not turn the whole drop into nothing.
  assert.deepEqual(
      dropDecision({from: {section: 'tabs', index: 1},
                    to: {section: 'favorites'}}),
      {kind: 'favorite', from: 1, fromSection: 'tabs', to: null});
});

test('a drag that starts on a favourite tile is not handled yet', () => {
  // Favourite tiles are not draggable in this subtask. Returning null rather
  // than guessing keeps an unhandled drag inert instead of sending the browser
  // a message built from a section the ABI has no case for.
  assert.equal(drop('favorites', 0, 'tabs', 1), null);
  assert.equal(drop('favorites', 0, 'favorites', 1), null);
});

test('an unknown section resolves to nothing', () => {
  assert.equal(drop('tabs', 0, 'folder', 1), null);
  assert.equal(drop(undefined, 0, 'tabs', 1), null);
});

test('a missing index resolves to nothing', () => {
  assert.equal(dropDecision({from: {section: 'tabs'}, to: {section: 'pinned', slot: 0}}), null);
  assert.equal(dropDecision({from: {section: 'tabs', index: 0}, to: {section: 'pinned'}}), null);
});

// drops that land in a different space than they started in.

test('a drop in the space it started in is unaffected by space numbers', () => {
  assert.deepEqual(across(2, 'tabs', 0, 2, 'tabs', 3),
      {kind: 'reorder', from: 0, to: 2});
  assert.deepEqual(across(0, 'tabs', 1, 0, 'pinned', 1),
      {kind: 'pin', from: 1, to: 1});
});

test('a drop in another space is a cross-space move', () => {
  // fromSpace is not decoration: paging has already switched the browser's
  // ACTIVE space by the time the drop lands, so the source index means nothing
  // without the space it was resolved against.
  assert.deepEqual(across(0, 'tabs', 2, 1, 'tabs', 0), {
    kind: 'moveToSpace',
    from: 2,
    fromSection: 'tabs',
    fromSpace: 0,
    toSpace: 1,
    to: 0,
  });
});

test('a cross-space move carries the slot it was aimed at', () => {
  // The surface is given the target space's rows, so there is a slot to
  // resolve, and moveTabAcrossSpaces takes a
  // destination, so the slot the indicator showed is the slot committed.
  assert.equal(across(0, 'tabs', 2, 1, 'tabs', 3).to, 3);
  assert.equal(across(0, 'tabs', 2, 1, 'tabs', 0).to, 0);
});

test('a cross-space slot is NOT adjusted for the row leaving', () => {
  // The row vacates a slot in the SOURCE space; the destination list never
  // shifts, so the same-list remove-then-insert adjustment must not apply -
  // the identical trap as a cross-section drop.
  assert.equal(across(0, 'tabs', 0, 1, 'tabs', 3).to, 3);
});

test('a pinned row can be dragged into another space', () => {
  assert.deepEqual(across(3, 'pinned', 1, 0, 'tabs', 2), {
    kind: 'moveToSpace',
    from: 1,
    fromSection: 'pinned',
    fromSpace: 3,
    toSpace: 0,
    to: 2,
  });
});

test('a cross-space no-op check does not fire on matching indices', () => {
  // Index 1 in one space and index 1 in another are different tabs. The
  // same-position guard must not swallow this the way it does within a list.
  assert.deepEqual(across(0, 'tabs', 1, 1, 'tabs', 1), {
    kind: 'moveToSpace',
    from: 1,
    fromSection: 'tabs',
    fromSpace: 0,
    toSpace: 1,
    to: 1,
  });
});

test('favouriting is unchanged by which space it lands in', () => {
  // Favourites are keyed by profile and URL, not by space, so the grid a row
  // is dropped on does not change what happens - only whose profile it is.
  assert.deepEqual(across(0, 'tabs', 2, 1, 'favorites', 0),
      {kind: 'favorite', from: 2, fromSection: 'tabs', to: 0});
});

// dropping a row onto a folder puts the tab in that group.
// Membership was always persistable - SpaceTab.groupID round-trips - but no
// surface offered a way to set it, so every folder a user made stayed empty.

test('dropping on a folder groups the tab', () => {
  assert.deepEqual(
      dropDecision({from: {section: 'tabs', index: 2},
                    to: {section: 'folder:ABC-123', slot: 0}}),
      {kind: 'group', from: 2, groupID: 'ABC-123', section: 'tabs'});
});

test('a pinned tab can join a folder too', () => {
  // Pinned items share the folder vocabulary now - the section rides along so
  // the browser knows which array to remove the tab from.
  assert.deepEqual(
      dropDecision({from: {section: 'pinned', index: 0},
                    to: {section: 'folder:G', slot: 0}}),
      {kind: 'group', from: 0, groupID: 'G', section: 'pinned'});
});

test('a pinned tab dropped above the only member carries slot 0', () => {
  // Real member geometry resolves a slot the core can land the pin at
  // directly - one message, no follow-up reorder to flash a half-applied join.
  assert.deepEqual(
      dropDecision({from: {section: 'pinned', index: 3},
                    to: {section: 'folder:G', slot: 0, boxes: [{top: 0, bottom: 10}]}}),
      {kind: 'group', from: 3, groupID: 'G', section: 'pinned', memberSlot: 0});
});

test('a pinned tab dropped below the only member carries no slot', () => {
  // Slot 1 IS the append position for a one-member box list, so it is the
  // same "beside the last member" default as carrying no slot at all.
  assert.deepEqual(
      dropDecision({from: {section: 'pinned', index: 3},
                    to: {section: 'folder:G', slot: 1, boxes: [{top: 0, bottom: 10}]}}),
      {kind: 'group', from: 3, groupID: 'G', section: 'pinned'});
});

test('a collapsed folder head has no measurable rows, so it appends', () => {
  // boxes: [] mirrors a head-only target - slot 0 is what insertionIndexFor
  // returns with nothing to measure, and must not be read as "prepend".
  assert.deepEqual(
      dropDecision({from: {section: 'pinned', index: 3},
                    to: {section: 'folder:G', slot: 0, boxes: []}}),
      {kind: 'group', from: 3, groupID: 'G', section: 'pinned'});
});

test('a folder drop carries no slot', () => {
  // Membership is a back-reference on the tab, not a position in the folder -
  // assignToGroup takes a group id and nothing else, so a slot would be a
  // number nothing honours.
  const decision = dropDecision({from: {section: 'tabs', index: 0},
                                 to: {section: 'folder:G', slot: 7}});
  assert.equal('to' in decision, false);
});

test('a folder cannot be dropped into another folder', () => {
  // No nesting - `isFolder` marks a dragged head regardless of which section
  // it started in, and the join branch has to refuse it directly, not only
  // rely on targetFor redirecting it away first.
  assert.equal(
      dropDecision({from: {section: 'pinned', index: 0, isFolder: true},
                    to: {section: 'folder:G2', slot: 0}}), null);
  assert.equal(
      dropDecision({from: {section: 'folders', index: 0, isFolder: true},
                    to: {section: 'folder:G2', slot: 0}}), null);
});

test('a folder drop in another space is not a group move', () => {
  // Crossing spaces is its own operation; grouping into a folder that belongs
  // to a different space would need the tab moved first.
  assert.equal(dropDecision({from: {section: 'tabs', index: 0, space: 0},
                             to: {section: 'folder:G', slot: 0, space: 1}}), null);
});

test('a folder id is required', () => {
  assert.equal(dropDecision({from: {section: 'tabs', index: 0},
                             to: {section: 'folder:', slot: 0}}), null);
});

// A row inside a folder leaves it, either into the pinned run or the tab list.

test('a row inside a folder can be dragged out to the pinned run', () => {
  assert.deepEqual(
      dropDecision({from: {section: 'folder:G1', index: 1},
                    to: {section: 'pinned', slot: 2}}),
      {kind: 'ungroup', groupID: 'G1', member: 1, toSection: 'pinned', to: 2});
});

test('a row inside a folder can be dragged out to the tab list', () => {
  assert.deepEqual(
      dropDecision({from: {section: 'folder:G1', index: 0},
                    to: {section: 'tabs', slot: 3}}),
      {kind: 'ungroup', groupID: 'G1', member: 0, toSection: 'tabs', to: 3});
});

test('a member row cannot be dropped anywhere but pinned or tabs', () => {
  assert.equal(
      dropDecision({from: {section: 'folder:G1', index: 0},
                    to: {section: 'folders', slot: 0}}), null);
  assert.equal(
      dropDecision({from: {section: 'folder:G1', index: 0},
                    to: {section: 'favorites', slot: 0}}), null);
});

// A regular folder pins as a whole group, named by its id.

test('dropping a regular folder head into the pinned run pins the group', () => {
  assert.deepEqual(
      dropDecision({
        from: {section: 'folders', index: 1, isFolder: true, folderId: 'G2'},
        to: {section: 'pinned', slot: 0},
      }),
      {kind: 'pinFolder', groupID: 'G2', to: 0});
});

test('pinning a folder needs its id', () => {
  assert.equal(
      dropDecision({from: {section: 'folders', index: 0, isFolder: true},
                    to: {section: 'pinned', slot: 0}}), null);
});

// A pinned folder head reorders among pinned items the same way a pinned tab
// does - the ordinary pinned/pinned branch, since it is just one more item.

test('a pinned folder head reorders among pinned items', () => {
  assert.deepEqual(
      dropDecision({
        from: {section: 'pinned', index: 0, isFolder: true, folderId: 'G1'},
        to: {section: 'pinned', slot: 3},
      }),
      {kind: 'reorderPinned', from: 0, to: 2});
});

// a favourite could be dragged IN and never out, so the grid was a
// one-way door and a favourite had no removal route at all.
const out = (toSection, slot) => dropDecision({
  from: {section: 'favorites', index: 0, url: 'https://fav.test/'},
  to: {section: toSection, slot},
});

test('a favourite dragged into the tab list leaves the grid', () => {
  assert.deepEqual(out('tabs', 2),
      {kind: 'unfavorite', url: 'https://fav.test/', toSection: 'tabs', to: 2});
});

test('a favourite dragged into the pinned strip leaves the grid too', () => {
  assert.deepEqual(out('pinned', 0),
      {kind: 'unfavorite', url: 'https://fav.test/', toSection: 'pinned', to: 0});
});

test('a favourite with no url resolves to nothing', () => {
  // The url IS the favourite's identity, so a tile without one names no
  // favourite to remove and no page to put anywhere.
  assert.equal(dropDecision({from: {section: 'favorites', index: 0},
                             to: {section: 'tabs', slot: 0}}), null);
});

test('a favourite dropped back on the grid is not an unfavorite', () => {
  // Reordering inside the grid is sidebar-fav-drag's gesture, and it commits
  // its own message. Answering here as well would remove the favourite the
  // user was only moving.
  assert.equal(dropDecision({from: {section: 'favorites', index: 0,
                                    url: 'https://fav.test/'},
                             to: {section: 'favorites', slot: 1}}), null);
});

test('a member row dragged within its own folder reorders among the members', () => {
  assert.deepEqual(
      dropDecision({from: {section: 'folder:G1', index: 0},
                    to: {section: 'folder:G1', slot: 3}}),
      {kind: 'reorderMember', groupID: 'G1', from: 0, to: 2});
  assert.deepEqual(
      dropDecision({from: {section: 'folder:G1', index: 2},
                    to: {section: 'folder:G1', slot: 0}}),
      {kind: 'reorderMember', groupID: 'G1', from: 2, to: 0});
});

test('a member row dropped back on its own slot is not a move', () => {
  assert.equal(
      dropDecision({from: {section: 'folder:G1', index: 1},
                    to: {section: 'folder:G1', slot: 1}}), null);
  assert.equal(
      dropDecision({from: {section: 'folder:G1', index: 1},
                    to: {section: 'folder:G1', slot: 2}}), null);
});

test('a member row cannot be dropped into a different folder', () => {
  assert.equal(
      dropDecision({from: {section: 'folder:G1', index: 0},
                    to: {section: 'folder:G2', slot: 0}}), null);
});

test('a favourite dropped in a folder joins it at the pointed slot', () => {
  // This used to resolve to nothing, which is why a favourite
  // dragged onto a folder looked like it had been dropped into a hole.
  assert.deepEqual(
      dropDecision({from: {section: 'favorites', index: 0,
                           url: 'https://fav.test/'},
                    to: {section: 'folder:G', slot: 1,
                         boxes: [{top: 0, bottom: 10}, {top: 10, bottom: 20}]}}),
      {kind: 'favoriteToFolder', url: 'https://fav.test/', groupID: 'G',
       memberSlot: 1});
});

test('a favourite dropped on a collapsed folder becomes its last member', () => {
  // Same rule a tab already follows: a head with no measurable body, and an
  // append slot past the last box, both mean "last", not a position among
  // rows nobody has seen.
  assert.deepEqual(
      dropDecision({from: {section: 'favorites', index: 0,
                           url: 'https://fav.test/'},
                    to: {section: 'folder:G', slot: 0, boxes: []}}),
      {kind: 'favoriteToFolder', url: 'https://fav.test/', groupID: 'G',
       memberSlot: -1});
  assert.deepEqual(
      dropDecision({from: {section: 'favorites', index: 0,
                           url: 'https://fav.test/'},
                    to: {section: 'folder:G', slot: 2,
                         boxes: [{top: 0, bottom: 10}, {top: 10, bottom: 20}]}}),
      {kind: 'favoriteToFolder', url: 'https://fav.test/', groupID: 'G',
       memberSlot: -1});
});

test('a favourite with no url names no page to put in a folder', () => {
  assert.equal(dropDecision({from: {section: 'favorites', index: 0},
                             to: {section: 'folder:G', slot: 0}}), null);
});

test('a folder needs an id to take a favourite', () => {
  assert.equal(dropDecision({from: {section: 'favorites', index: 0,
                                    url: 'https://fav.test/'},
                             to: {section: 'folder:', slot: 0}}), null);
});

// --- Split bands of a tab row: a split rather than a reorder ----------------

const ROW_BOX = {left: 0, right: 200, top: 0, bottom: 40};

// The pointer resolved against a row, the shape targetFor hands over once the
// point falls in one of that row's split bands.
const onto = (side, extra = {}) => dropDecision({
  from: {section: 'tabs', index: 0, tabId: 'T1', ...extra.from},
  to: {section: 'tabs', slot: 2, edge: side, tabId: 'T2', ...extra.to},
});

test('each split band of a tab row names the side the split takes', () => {
  for (const side of ['left', 'right', 'top', 'bottom']) {
    assert.deepEqual(onto(side),
        {kind: 'split', side, draggedTabId: 'T1', targetTabId: 'T2'});
  }
});

test('the middle band of a row still reorders', () => {
  assert.deepEqual(
      dropDecision({from: {section: 'tabs', index: 0, tabId: 'T1'},
                    to: {section: 'tabs', slot: 2, tabId: 'T2'}}),
      {kind: 'reorder', from: 0, to: 1});
});

test('a pinned row splits the same way a regular row does', () => {
  assert.deepEqual(
      onto('right', {from: {section: 'pinned'}, to: {section: 'pinned'}}),
      {kind: 'split', side: 'right', draggedTabId: 'T1', targetTabId: 'T2'});
});

test('a folder head carries no tab, so its edges are not a split', () => {
  // A folder row has no data-tab-id, so targetFor never resolves an edge for
  // it - and the drop stays the group join it already was.
  assert.deepEqual(
      dropDecision({from: {section: 'tabs', index: 0, tabId: 'T1'},
                    to: {section: 'folder:G1', slot: 0}}),
      {kind: 'group', from: 0, groupID: 'G1', section: 'tabs'});
});

test('a dragged folder never splits', () => {
  assert.equal(onto('left', {from: {section: 'folders', isFolder: true}}), null);
});

test('a folder member dropped on a tab row edge splits, not ungroups', () => {
  // A member IS a tab row, so the same gesture has to mean the same thing on
  // it. Only the middle band still takes the row out of the group.
  assert.deepEqual(onto('right', {from: {section: 'folder:G1'}}),
      {kind: 'split', side: 'right', draggedTabId: 'T1', targetTabId: 'T2'});
  assert.deepEqual(
      dropDecision({from: {section: 'folder:G1', index: 0, tabId: 'T1'},
                    to: {section: 'tabs', slot: 2, tabId: 'T2'}}),
      {kind: 'ungroup', groupID: 'G1', member: 0, toSection: 'tabs', to: 2});
});

test('a folder body keeps its split bands for the member slot', () => {
  assert.deepEqual(
      dropDecision({from: {section: 'tabs', index: 0, tabId: 'T1'},
                    to: {section: 'folder:G1', slot: 0, edge: 'top',
                         tabId: 'T2', boxes: [{}, {}]}}),
      {kind: 'group', from: 0, groupID: 'G1', section: 'tabs', memberSlot: 0});
});

test('a row dropped on its own edge is nothing at all', () => {
  assert.equal(onto('left', {to: {slot: 1, tabId: 'T1'}}), null);
});

test('a cold row has no tab to split with, so the edge still reorders', () => {
  const reorder = {kind: 'reorder', from: 0, to: 1};
  assert.deepEqual(onto('left', {from: {tabId: ''}}), reorder);
  assert.deepEqual(onto('left', {to: {tabId: ''}}), reorder);
});

test('a split band in another space is not a split', () => {
  // Both panes are on screen mid-paging, but a split needs two tabs of the
  // same window's strip.
  assert.equal(onto('left', {from: {space: 0}, to: {space: 1}}), null);
});

// the outer band is where one aims BETWEEN two rows, so quarters
// read as inverted. ROW_BOX is 40 tall, so the bands fall at 8/20/32.
test('splitEdgeFor splits the middle three fifths and reorders the ends', () => {
  assert.equal(splitEdgeFor(100, 4, ROW_BOX), null, 'top fifth reorders');
  assert.equal(splitEdgeFor(100, 12, ROW_BOX), 'top');
  assert.equal(splitEdgeFor(100, 28, ROW_BOX), 'bottom');
  assert.equal(splitEdgeFor(100, 36, ROW_BOX), null, 'bottom fifth reorders');
});

test('splitEdgeFor turns over at the band edges, not near them', () => {
  assert.equal(splitEdgeFor(100, 7.9, ROW_BOX), null);
  assert.equal(splitEdgeFor(100, 8.1, ROW_BOX), 'top');
  // The midline belongs to the lower half, so no point between the two reorder
  // bands answers null.
  assert.equal(splitEdgeFor(100, 19.9, ROW_BOX), 'top');
  assert.equal(splitEdgeFor(100, 20, ROW_BOX), 'bottom');
  assert.equal(splitEdgeFor(100, 31.9, ROW_BOX), 'bottom');
  assert.equal(splitEdgeFor(100, 32.1, ROW_BOX), null);
});

test('splitEdgeFor lets left or right win a corner inside the split bands',
     () => {
       assert.equal(splitEdgeFor(10, 12, ROW_BOX), 'left');
       assert.equal(splitEdgeFor(190, 28, ROW_BOX), 'right');
       // The same holds at the margins: a drop aimed between two rows
       // reorders even when it drifts into a side quarter.
       assert.equal(splitEdgeFor(4, 2, ROW_BOX), null);
       assert.equal(splitEdgeFor(196, 38, ROW_BOX), null);
     });

// A split row stands for several tabs, which need not be adjacent, so it moves
// as one run rather than through the one-slot kinds.
const splitFrom = (slots, space = 0) => ({section: 'tabs', index: slots[0],
                                         slots, split: true, space,
                                         tabId: '7'});

test('a split row dropped in its own list is a run move', () => {
  assert.deepEqual(
      dropDecision({from: splitFrom([1, 2]), to: {section: 'tabs', slot: 5, space: 0}}),
      {kind: 'moveRun', slots: [1, 2], fromSpace: 0, toSpace: 0, to: 5});
  assert.deepEqual(
      dropDecision({from: splitFrom([3, 4]), to: {section: 'tabs', slot: 0, space: 0}}),
      {kind: 'moveRun', slots: [3, 4], fromSpace: 0, toSpace: 0, to: 0});
});

test('the run slot is the raw insertion slot, the core adjusts it', () => {
  assert.equal(
      dropDecision({from: splitFrom([0, 3]), to: {section: 'tabs', slot: 2, space: 0}}).to,
      2);
});

test('a contiguous split row dropped on itself is nothing', () => {
  for (const slot of [1, 2, 3]) {
    assert.equal(
        dropDecision({from: splitFrom([1, 2]), to: {section: 'tabs', slot, space: 0}}),
        null, `slot ${slot}`);
  }
});

test('a split row dropped in another space moves the run there', () => {
  assert.deepEqual(
      dropDecision({from: splitFrom([1, 3], 0), to: {section: 'tabs', slot: 2, space: 1}}),
      {kind: 'moveRun', slots: [1, 3], fromSpace: 0, toSpace: 1, to: 2});
});

test('a split row dropped on a space dot appends there', () => {
  assert.deepEqual(
      dropDecision({from: splitFrom([1, 3], 0), to: {section: 'tabs', slot: -1, space: 2}}),
      {kind: 'moveRun', slots: [1, 3], fromSpace: 0, toSpace: 2, to: -1});
});

test('a split row never pins, favourites or joins a folder', () => {
  const from = splitFrom([1, 2]);
  for (const to of [
    {section: 'pinned', slot: 0, space: 0},
    {section: 'pinned', slot: 0, space: 1},
    {section: 'favorites', slot: 0, space: 0},
    {section: 'folder:G', slot: 0, space: 0},
    {section: 'folders', slot: 0, space: 0},
  ]) {
    assert.equal(dropDecision({from, to}), null, to.section);
  }
});

test('a split row over another row\'s edge still moves, it never splits', () => {
  assert.deepEqual(
      dropDecision({from: splitFrom([1, 2]),
                    to: {section: 'tabs', slot: 0, space: 0, edge: 'left', tabId: '9'}}),
      {kind: 'moveRun', slots: [1, 2], fromSpace: 0, toSpace: 0, to: 0});
});

test('splitEdgeFor answers nothing for a row with no box', () => {
  assert.equal(splitEdgeFor(0, 0, {left: 0, right: 0, top: 0, bottom: 0}), null);
});

test('splitEdgeFor answers nothing for a point outside the row', () => {
  assert.equal(splitEdgeFor(100, -5, ROW_BOX), null);
  assert.equal(splitEdgeFor(100, 45, ROW_BOX), null);
  assert.equal(splitEdgeFor(-5, 20, ROW_BOX), null);
  assert.equal(splitEdgeFor(205, 20, ROW_BOX), null);
});

test('splitEdgeFor rejects a coordinate that is not a number', () => {
  // Every comparison against undefined or NaN is false, so without a check of
  // its own a missing clientX walks past the left and right bands into 'top'.
  assert.equal(splitEdgeFor(undefined, 4, ROW_BOX), null);
  assert.equal(splitEdgeFor(NaN, 4, ROW_BOX), null);
  assert.equal(splitEdgeFor(10, undefined, ROW_BOX), null);
  assert.equal(splitEdgeFor(10, NaN, ROW_BOX), null);
});
