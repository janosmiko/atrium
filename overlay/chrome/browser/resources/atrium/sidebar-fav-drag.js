// Copyright 2026 The Atrium Authors
//
// Dragging a favourite tile to reorder the grid, with the live reflow the
// tiles animate through on the way.
//
// Separate from installRowDrag on purpose: that one claims .tabrow and this one
// .fav, so both can listen on the same root without either seeing the other's
// drags. The two pieces of maths this needs were ported from the Swift arm
// rather than re-derived - see sidebar-grid-drop.js and provisionalOrder.

import {destinationFor, provisionalOrder} from './sidebar-drag.js';
import {clearTileTransforms, nearestSlot, reflowTiles}
    from './sidebar-grid-drop.js';

const DRAGGING_CLASS = 'favdragging';

/**
 * Wires favourite-tile dragging under `root`.
 *
 * `onReorder(from, to)` is called once on drop, with `to` already adjusted for
 * the tile leaving its old slot - the same convention SpacesStore.moveFavorite
 * expects, so no caller has to re-derive the shift.
 */
export function installFavoriteDrag(root, {onReorder} = {}) {
  let grid = null;
  let source = null;
  // The order the grid had when the drag started. Every preview is computed
  // from THIS, not from the last preview - applying a move to an already-moved
  // order compounds it and the tiles wander.
  let baseOrder = null;
  // The tile geometry as it was when the drag started, and NOT re-measured
  // afterwards. Each preview physically moves the tiles, so measuring the live
  // layout means the same pointer position resolves to a different slot once
  // the grid has reflowed under it - the preview shows one slot and the drop
  // commits another. That divergence is the bug this whole file is careful
  // about, and it is what the Swift arm's "static order the commit resolves
  // against" exists to prevent.
  let baseBoxes = null;
  // The slot the preview is currently showing, fed back into nearestSlot as
  // the hysteresis anchor.
  let sticky = null;

  const tilesOf = (el) => [...el.querySelectorAll('.fav')];

  // `restore` puts the tiles back in the order they had before the drag.
  // reflowTiles() physically reorders the DOM and the transform is only the
  // animation laid over it, so clearing the transforms does not undo a preview.
  // Without this a drag that ends WITHOUT a drop - Esc, or released outside -
  // leaves the grid showing a move that never reached the model, and it stays
  // wrong until some unrelated snapshot rebuilds the rail.
  function finish({restore = false} = {}) {
    if (grid) {
      if (restore && baseOrder) {
        for (const tile of baseOrder) {
          grid.appendChild(tile);
        }
      }
      clearTileTransforms(grid);
      for (const tile of grid.querySelectorAll('.' + DRAGGING_CLASS)) {
        tile.classList.remove(DRAGGING_CLASS);
      }
    }
    grid = null;
    source = null;
    baseOrder = null;
    baseBoxes = null;
    sticky = null;
  }

  function slotAt(event) {
    return nearestSlot({x: event.clientX, y: event.clientY}, baseBoxes, sticky);
  }

  root.addEventListener('dragstart', (e) => {
    const tile = e.target.closest?.('.fav');
    if (!tile || !root.contains(tile)) {
      return;
    }
    grid = tile.closest('[data-dropzone="favorites"]');
    if (!grid) {
      grid = null;
      return;
    }
    baseOrder = tilesOf(grid);
    baseBoxes = baseOrder.map((t) => t.getBoundingClientRect());
    source = baseOrder.indexOf(tile);
    sticky = null;
    tile.classList.add(DRAGGING_CLASS);
    e.dataTransfer?.setData('text/plain', String(source));
    if (e.dataTransfer) {
      e.dataTransfer.effectAllowed = 'move';
    }
    e.stopPropagation();
  });

  root.addEventListener('dragover', (e) => {
    if (!grid) {
      return;
    }
    // Only over the grid the drag STARTED in. A favourite dragged out into the
    // tab list has nowhere to go yet, and previewing a reorder there would
    // promise a move that never happens.
    //
    // Identity, not just "some favourites zone": every rail pane draws its own
    // grid, and edge paging slides another pane under the
    // pointer mid-drag by design. Comparing only the selector meant
    // hovering a neighbouring space's grid reflowed THIS one against slot
    // geometry measured where the pointer no longer is.
    if (e.target.closest?.('[data-dropzone="favorites"]') !== grid) {
      return;
    }
    e.preventDefault();
    e.stopPropagation();
    if (e.dataTransfer) {
      e.dataTransfer.dropEffect = 'move';
    }
    const slot = slotAt(e);
    if (slot === sticky) {
      return;
    }
    sticky = slot;
    reflowTiles(grid, provisionalOrder(baseOrder, source, slot));
  });

  root.addEventListener('drop', (e) => {
    // Same identity check as dragover: a drop on another space's grid must not
    // commit a reorder into this one. Moving a favourite ACROSS spaces is a
    // separate feature that does not exist yet.
    if (!grid || e.target.closest?.('[data-dropzone="favorites"]') !== grid) {
      return;
    }
    e.preventDefault();
    e.stopPropagation();
    // The SAME sticky slot the preview last showed. Recomputing bare here
    // would commit a slot the user never saw whenever the pointer is released
    // inside the dead zone - the exact silent bug the Swift arm shipped once
    // (see sidebar-grid-drop.js).
    const to = destinationFor(source, slotAt(e));
    const from = source;
    finish();
    if (to !== null) {
      onReorder?.(from, to);
    }
  });

  // A drop commits and the browser pushes a fresh snapshot, so the drop path
  // deliberately does NOT restore. dragend fires after drop too, but drop has
  // already cleared `grid` by then, so this is a no-op on that path.
  root.addEventListener('dragend', () => finish({restore: true}));
}
