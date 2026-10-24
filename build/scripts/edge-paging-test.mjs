#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// edge paging for a drag in flight.
//
//   make paging-test
//
// Ported case for case from SidebarEdgePagingTests.swift rather than rewritten,
// so the two arms are held to the same behaviour while both exist. The only
// change is the unit: Swift's TimeInterval is seconds, this is milliseconds,
// because that is what a surface gets from performance.now().

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {
  EDGE_ZONE_WIDTH, EDGE_DWELL_MS, EDGE_REPEAT_COOLDOWN_MS,
  edgeAtX, directionFor, createEdgePaging,
} = await import(path.join(SRC, 'chrome/browser/resources/atrium/sidebar-paging.js'));

// MARK: edgeAtX

test('a point near minX is the leading edge', () => {
  assert.equal(edgeAtX(0, 300), 'leading');
  assert.equal(edgeAtX(EDGE_ZONE_WIDTH, 300), 'leading');
});

test('a point near maxX is the trailing edge', () => {
  assert.equal(edgeAtX(300, 300), 'trailing');
  assert.equal(edgeAtX(300 - EDGE_ZONE_WIDTH, 300), 'trailing');
});

test('a point in the middle is no edge', () => {
  assert.equal(edgeAtX(150, 300), null);
});

test('just outside the zone is no edge', () => {
  assert.equal(edgeAtX(EDGE_ZONE_WIDTH + 1, 300), null);
  assert.equal(edgeAtX(300 - EDGE_ZONE_WIDTH - 1, 300), null);
});

test('a zero width reports no edge', () => {
  assert.equal(edgeAtX(0, 0), null);
});

test('a sidebar too narrow for two zones resolves to leading', () => {
  // width < 2 * EDGE_ZONE_WIDTH: both conditions hold at once, and leading
  // wins because it is tested first. A documented tie-break, not an ambiguous
  // result that would flicker between the two.
  assert.equal(edgeAtX(EDGE_ZONE_WIDTH / 2, EDGE_ZONE_WIDTH), 'leading');
});

// MARK: directionFor

test('the leading edge pages to the previous space', () => {
  assert.equal(directionFor('leading'), 'previous');
});

test('the trailing edge pages to the next space', () => {
  assert.equal(directionFor('trailing'), 'next');
});

// MARK: dwell before the first page

test('nothing pages before the dwell elapses', () => {
  const paging = createEdgePaging();
  paging.setEdge('trailing', 0);
  assert.equal(paging.pageIfDue(EDGE_DWELL_MS - 50), null);
});

test('it pages once the dwell elapses', () => {
  const paging = createEdgePaging();
  paging.setEdge('trailing', 0);
  assert.equal(paging.pageIfDue(EDGE_DWELL_MS), 'trailing');
});

test('with no edge armed it never pages', () => {
  const paging = createEdgePaging();
  assert.equal(paging.pageIfDue(1000000), null);
});

// MARK: the accidental-page guard

test('a slow reorder near the edge never pages', () => {
  // The gesture this guard exists to survive: dragging a row to the bottom of
  // a list, drifting a few pixels sideways on the way, taking longer than the
  // dwell to do it. Each sideways step re-enters the zone, and re-entering
  // must restart the clock - otherwise time spent merely NEAR the edge adds up
  // into a page the user never asked for.
  const paging = createEdgePaging();
  let now = 0;
  for (let step = 0; step < 6; step++) {
    // Just inside the zone, then just outside it, as a hand would wander.
    paging.setEdge('leading', now);
    now += EDGE_DWELL_MS - 40;
    assert.equal(paging.pageIfDue(now), null, `paged on step ${step}`);
    paging.setEdge(null, now);
    now += 40;
  }
  // Six near-misses, well past the dwell in total elapsed time.
  assert.ok(now > EDGE_DWELL_MS * 4);
});

test('the zone is narrow enough that a row reorder clears it', () => {
  // The other half of the same guard, and the half that once weakened:
  // a wide zone put it
  // under a dragged row's own width. A drop aimed at the list, not the edge,
  // must resolve to no edge at all.
  const sidebarWidth = 260;
  assert.equal(edgeAtX(EDGE_ZONE_WIDTH + 1, sidebarWidth), null);
  assert.ok(EDGE_ZONE_WIDTH <= 12,
            'the lab pairs a NARROW zone with its dwell - widening one ' +
            'without the other weakens both');
});

// MARK: repeat paging

test('the second page waits the cooldown, not another dwell', () => {
  const paging = createEdgePaging();
  paging.setEdge('leading', 0);
  assert.equal(paging.pageIfDue(EDGE_DWELL_MS), 'leading');
  // Still inside the cooldown window straight after the first fire.
  assert.equal(paging.pageIfDue(EDGE_DWELL_MS + 50), null);
  // Measured from the FIRST FIRE, not from the original arm.
  assert.equal(paging.pageIfDue(EDGE_DWELL_MS + EDGE_REPEAT_COOLDOWN_MS), 'leading');
});

test('the repeat cooldown is longer than the initial dwell', () => {
  // Not a magic-number check: the state machine's "has fired" branch is only
  // meaningful if this ordering is deliberate. Holding at an edge has to page
  // more slowly than arriving at one, or it reads as a runaway auto-scroll.
  assert.ok(EDGE_REPEAT_COOLDOWN_MS > EDGE_DWELL_MS);
});

// MARK: edge changes reset the clock

test('changing edge resets the dwell clock', () => {
  const paging = createEdgePaging();
  paging.setEdge('leading', 0);
  // Almost dwelled long enough on leading...
  paging.setEdge('trailing', EDGE_DWELL_MS - 10);
  // ...but the old dwell must not carry over to the new edge.
  assert.equal(paging.pageIfDue(EDGE_DWELL_MS), null);
  assert.equal(paging.pageIfDue(EDGE_DWELL_MS - 10 + EDGE_DWELL_MS), 'trailing');
});

test('leaving the edge and coming back restarts the dwell', () => {
  const paging = createEdgePaging();
  paging.setEdge('trailing', 0);
  paging.setEdge(null, 100);
  paging.setEdge('trailing', 200);
  assert.equal(paging.pageIfDue(200 + EDGE_DWELL_MS - 10), null);
  assert.equal(paging.pageIfDue(200 + EDGE_DWELL_MS), 'trailing');
});

test('re-reporting the same edge does not restart an armed dwell', () => {
  const paging = createEdgePaging();
  paging.setEdge('leading', 0);
  // dragover fires continuously for the same edge while the pointer sits
  // still. If each report restarted the clock, a page could never fire at all.
  paging.setEdge('leading', 100);
  paging.setEdge('leading', 200);
  assert.equal(paging.pageIfDue(EDGE_DWELL_MS), 'leading');
});

test('the armed edge is readable while a drag is in flight', () => {
  const paging = createEdgePaging();
  assert.equal(paging.currentEdge, null);
  paging.setEdge('leading', 0);
  assert.equal(paging.currentEdge, 'leading');
  paging.setEdge(null, 10);
  assert.equal(paging.currentEdge, null);
});
