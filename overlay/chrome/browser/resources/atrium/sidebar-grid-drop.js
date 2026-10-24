// Copyright 2026 The Atrium Authors
//
// Nearest-slot geometry for a drop in the favourites GRID.
//
// Ported from swift/AtriumUI/Sources/AtriumUI/FavoritesGridDrop.swift with its
// test cases, rather than re-derived. The hysteresis below was earned across
// 228 and 273 ("icons constantly jumping around") and is not the
// kind of rule that survives being rewritten from the description.

/**
 * How far past a tile's horizontal centre the pointer must travel before the
 * live preview flips to the other side of it.
 *
 * Well under half a tile, so a deliberate move still reads instantly, but
 * comfortably above the few pixels of jitter a held-still pointer produces.
 */
export const GRID_BOUNDARY_HYSTERESIS = 6;

/** How long a tile takes to slide to its new place during the reflow. */
const REFLOW_MS = 180;

// Runs `move`, then animates each of `els` from where it was before (FLIP).
// Neither a grid nor a list animates its own reflow, so a DOM move teleports.
export function flipMove(els, move) {
  if (globalThis.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches) {
    move();
    return;
  }
  const before = new Map(els.map((el) => [el, el.getBoundingClientRect()]));
  move();
  for (const el of els) {
    const from = before.get(el);
    const to = el.getBoundingClientRect();
    const dx = from.left - to.left;
    const dy = from.top - to.top;
    if (!dx && !dy) {
      continue;
    }
    el.style.transition = 'none';
    el.style.transform = `translate(${dx}px, ${dy}px)`;
    // Read back so the browser treats the next write as a change to animate
    // rather than folding both into one style recalculation.
    void el.offsetWidth;
    el.style.transition = `transform ${REFLOW_MS}ms ease`;
    el.style.transform = '';
  }
}

// Moves the tiles into `order`.
export function reflowTiles(grid, order) {
  flipMove([...grid.querySelectorAll('.fav')], () => {
    for (const tile of order) {
      grid.appendChild(tile);
    }
  });
}

export function clearFlip(els) {
  for (const el of els) {
    el.style.transition = '';
    el.style.transform = '';
  }
}

export function clearTileTransforms(grid) {
  clearFlip(grid.querySelectorAll('.fav'));
}

/**
 * The insertion slot (0...boxes.length, insert BEFORE the tile at that index,
 * length meaning append) nearest `point`.
 *
 * `boxes` is parallel to the rendered order; an entry that is null - a tile not
 * laid out yet - is skipped, and a grid with no laid-out tile at all appends,
 * because there is nothing to be nearest to.
 *
 * Picks the tile whose CENTRE is closest, which is correct for any column count
 * without hardcoding one and handles a drop landing in a different row the same
 * way as one in the same row. Before/after is then decided by comparing x to
 * that tile's own centre, which is also what makes a point exactly on the
 * boundary resolve deterministically rather than tie.
 *
 * `stickyIndex` is the slot the preview is currently showing. Passing it adds
 * inertia at a tile boundary.
 *
 * BOTH the live preview and the drop commit must pass it. An earlier round of
 * the Swift version passed it only on the preview, and a release inside the
 * dead zone then committed one slot past what was on screen - silently, since
 * nothing in the UI showed the change. Do not "simplify" the commit to the bare
 * threshold.
 */
export function nearestSlot(point, boxes, stickyIndex = null) {
  let nearestIndex = null;
  let nearestBox = null;
  let nearestDistanceSquared = Infinity;

  for (let i = 0; i < boxes.length; i++) {
    const box = boxes[i];
    if (!box) {
      continue;
    }
    const dx = point.x - (box.left + box.right) / 2;
    const dy = point.y - (box.top + box.bottom) / 2;
    const distanceSquared = dx * dx + dy * dy;
    if (distanceSquared < nearestDistanceSquared) {
      nearestDistanceSquared = distanceSquared;
      nearestIndex = i;
      nearestBox = box;
    }
  }

  if (nearestIndex === null) {
    return boxes.length;
  }

  const midX = (nearestBox.left + nearestBox.right) / 2;
  const resolved = point.x >= midX ? nearestIndex + 1 : nearestIndex;

  // Confined to the two slots either side of the nearest tile's own centre.
  // Moving onto a DIFFERENT tile is a real target change and is honoured
  // immediately, as is a stale sticky value the current geometry cannot
  // support.
  if (!Number.isInteger(stickyIndex) || stickyIndex === resolved ||
      (stickyIndex !== nearestIndex && stickyIndex !== nearestIndex + 1)) {
    return resolved;
  }
  const crossedDecisively = resolved > stickyIndex
      ? point.x >= midX + GRID_BOUNDARY_HYSTERESIS
      : point.x <= midX - GRID_BOUNDARY_HYSTERESIS;
  return crossedDecisively ? resolved : stickyIndex;
}
