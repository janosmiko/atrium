// Copyright 2026 The Atrium Authors
//
// Edge paging for a drag in flight: holding a dragged tab at the sidebar's
// own edge pages to the previous or next space.
//
// Ported from swift/AtriumUI/Sources/AtriumUI/SidebarEdgePaging.swift rather
// than re-derived, so the two arms cannot drift while both exist. The Swift
// tests came with it - see build/edge-paging-test.mjs.
//
// One deliberate difference: Swift measures time in seconds because that is
// what TimeInterval is; this measures in MILLISECONDS, because that is what
// performance.now() and event timestamps give a surface. The durations are the
// same durations.

/**
 * How close to either physical edge counts as hovering it, in CSS pixels of
 * the sidebar's own width. Narrow on purpose: an ordinary drop near the pane's
 * border - a row reorder, a pin - must not arm a page.
 *
 * The lab's own value (swipe-lab.html:4571). It had been
 * doubled to 24 with no reason recorded, which is half of a guard the lab
 * builds from two halves: a narrow zone AND a long dwell, tied together in its
 * comment at 4564-4566. The dwell below is deliberately shorter than the lab's,
 * so widening the zone as well left neither half doing its job.
 */
export const EDGE_ZONE_WIDTH = 12;

/**
 * The pause before the FIRST page at a newly entered edge. Shorter than the
 * lab's 550ms on purpose: it matches the space-card hover, so the
 * two dwell-to-act gestures in this surface feel the same. That is why the zone
 * above stays at the lab's width rather than being widened to compensate.
 */
export const EDGE_DWELL_MS = 350;

/**
 * The gap between successive pages while the drag stays at the same edge.
 * Longer than the dwell so holding still pages repeatedly without reading as a
 * runaway auto-scroll, short enough that paging across several spaces to find
 * a drop target is not sluggish.
 */
export const EDGE_REPEAT_COOLDOWN_MS = 700;

/**
 * Which physical edge local x falls in, or null for neither.
 *
 * Always geometric: 'leading' is minX and 'trailing' is maxX, with no
 * mirroring when the sidebar docks right. That is the same invariant the Swift
 * arm holds (SidebarRowDrag.swift's D4), so a dock side never enters this.
 */
export function edgeAtX(x, width) {
  if (!(width > 0)) {
    return null;
  }
  // Leading is tested first, so a sidebar narrow enough for the two zones to
  // overlap resolves to one edge rather than flickering between both.
  if (x <= EDGE_ZONE_WIDTH) {
    return 'leading';
  }
  if (x >= width - EDGE_ZONE_WIDTH) {
    return 'trailing';
  }
  return null;
}

/** The space-order direction an edge pages toward. */
export function directionFor(edge) {
  return edge === 'trailing' ? 'next' : 'previous';
}

/**
 * Decides WHEN a page fires while an edge is armed. Pure and time-driven: it
 * owns no timer, and every call is given `now`, which is what makes it
 * testable without waiting in real time.
 */
export function createEdgePaging() {
  let currentEdge = null;
  // When the edge was armed, or last paged - whichever is later.
  let armedAt = null;
  let hasFired = false;

  return {
    get currentEdge() {
      return currentEdge;
    },

    /**
     * Sets the hovered edge (null having left the zone). ANY change - to null,
     * or straight to the other edge - restarts the dwell, so a drag wobbling
     * across the boundary cannot accumulate dwell toward a page it never held.
     */
    setEdge(edge, now) {
      if (edge === currentEdge) {
        return;
      }
      currentEdge = edge;
      armedAt = edge === null ? null : now;
      hasFired = false;
    },

    /**
     * Returns the edge to page toward if one is due right now, else null.
     * Firing re-anchors the clock, so the next page waits the shorter repeat
     * cooldown rather than a full dwell again.
     */
    pageIfDue(now) {
      if (currentEdge === null || armedAt === null) {
        return null;
      }
      const wait = hasFired ? EDGE_REPEAT_COOLDOWN_MS : EDGE_DWELL_MS;
      if (now - armedAt < wait) {
        return null;
      }
      armedAt = now;
      hasFired = true;
      return currentEdge;
    },
  };
}
