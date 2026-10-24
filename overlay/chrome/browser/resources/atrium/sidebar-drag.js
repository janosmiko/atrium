import {createEdgePaging, directionFor, edgeAtX} from './sidebar-paging.js';
import {clearFlip, clearTileTransforms, flipMove, nearestSlot, reflowTiles}
    from './sidebar-grid-drop.js';
import {asDragCopy, createDragFollower} from './sidebar-drag-follow.js';

// Copyright 2026 The Atrium Authors
//
// dragging a sidebar row to reorder it.
//
// The hit-testing lives here rather than in Swift, and here is why:
// chrome.send is one-way async IPC, so asking the browser where a drop would
// land would cost two IPC hops per pointer move and the indicator would trail
// the cursor. The renderer decides where the row goes; Swift is told once, on
// drop, and owns what that means.
//
// The index maths is separated from the DOM for the same reason the swipe
// machine is: "which slot does this Y land in" is a question worth answering
// in a test rather than by dragging things around by hand.

/**
 * The insertion slot for a pointer at `y`, given each row's vertical box.
 *
 * Slots are BETWEEN rows: 0 is above the first row, boxes.length is below the
 * last. A row is claimed by its own midpoint, which is what makes the indicator
 * flip when the cursor crosses the middle rather than the edge - dragging onto
 * the top half of a row means "before it", the bottom half "after it".
 *
 * @param {number} y pointer position, same coordinate space as the boxes
 * @param {!Array<{top: number, bottom: number}>} boxes in render order
 * @return {number} 0..boxes.length
 */
/**
 * The order a list should be LAID OUT in mid-drag, with the dragged item shown
 * at the slot the pointer currently resolves to.
 *
 * Ported from FavoritesReflowMath.swift, which exists to mirror the commit
 * path's index arithmetic exactly - so the provisional order shown while
 * dragging IS the order committed on drop, rather than a preview that happens
 * to agree most of the time.
 *
 * `destination` is an insert-BEFORE slot in 0...items.length. Returns `items`
 * unchanged for anything out of range, and for the two destinations that
 * resolve to no move at all - immediately before or after the item's own
 * position - so a stray hover never flickers the grid.
 */
export function provisionalOrder(items, source, destination) {
  if (!Array.isArray(items) ||
      !Number.isInteger(source) || source < 0 || source >= items.length ||
      !Number.isInteger(destination) ||
      destination < 0 || destination > items.length ||
      destination === source || destination === source + 1) {
    return items;
  }
  const reordered = [...items];
  const [dragged] = reordered.splice(source, 1);
  // Removing `source` shifts every later index down by one.
  reordered.splice(destination > source ? destination - 1 : destination, 0, dragged);
  return reordered;
}

// How often the armed edge is checked while a drag is in flight. Well under
// the dwell, so the page fires within a frame or two of becoming due.
const EDGE_POLL_MS = 50;

const DRAGGING_CLASS = 'dragging';
// Marks a live drag on the route root, so the CSS can reveal drop targets that
// are invisible at rest.
const ACTIVE_CLASS = 'dragactive';
// Marks the row a split would land against. data-split-side names which half.
const SPLIT_CLASS = 'dropsplit';

// The lists where a drop target is a ghost row rather than the line. A folder
// body and the folder list keep the line. The space name is a pinned zone too,
// but it is one row, not a list.
const GHOST_LISTS = '.rows[data-dropzone="tabs"], .rows[data-dropzone="pinned"], ' +
    '.foldin[data-dropzone="pinned"]';
const GHOST_CLASS = 'rowghost';
// A committed drop leaves its preview up for the rebuild to replace, so the
// list does not flash back first. This puts it back if no rebuild comes.
const HELD_PREVIEW_MS = 1500;
const OPEN_GRID_CLASS = 'dropopen';

// atrium_core refuses a favourite past this many per profile:
// MAX_FAVORITES_PER_PROFILE in rust/atrium-core/src/mutate/favorites.rs.
const FAVORITES_MAX = 12;

const SPLIT_EDGE_FRACTION = 0.25;

// The outer fifth at each end of a row is where the pointer sits when the user
// aims BETWEEN two rows. Giving those bands to the split read as
// the split and the reorder being inverted. It vetoes the side
// edges too, or a drop aimed between two rows splits on drifting near a margin.
const REORDER_BAND_FRACTION = 0.2;

// Left and right are tested before the halves, so a corner resolves to the side
// ChooseSplitOrientation (atrium_split_orientation_decision.h) picks for the
// same point dropped on a page.
export function splitEdgeFor(x, y, box) {
  const width = box.right - box.left;
  const height = box.bottom - box.top;
  if (!(width > 0) || !(height > 0)) {
    return null;
  }
  // A row still receives events for a point outside its own box, and a missing
  // coordinate fails every comparison below rather than landing in a band.
  if (!Number.isFinite(x) || !Number.isFinite(y) ||
      x < box.left || x > box.right || y < box.top || y > box.bottom) {
    return null;
  }
  const down = (y - box.top) / height;
  if (down < REORDER_BAND_FRACTION || down > 1 - REORDER_BAND_FRACTION) {
    return null;
  }
  if (x - box.left < width * SPLIT_EDGE_FRACTION) {
    return 'left';
  }
  if (box.right - x < width * SPLIT_EDGE_FRACTION) {
    return 'right';
  }
  return down < 0.5 ? 'top' : 'bottom';
}

export function insertionIndexFor(y, boxes) {
  for (let i = 0; i < boxes.length; i++) {
    const box = boxes[i];
    if (y < (box.top + box.bottom) / 2) {
      return i;
    }
  }
  return boxes.length;
}

/**
 * Where the dragged row ends up, given the slot the pointer is in.
 *
 * A slot index is not a row index: removing the dragged row first shifts every
 * slot after it down by one. Without this, dragging a row DOWN always lands it
 * one place short - the classic off-by-one in every reorder implementation,
 * and invisible when dragging up.
 *
 * Returns null when the move is a no-op, so the caller sends nothing rather
 * than a message that reorders a list into itself.
 *
 * @param {number} from the dragged row's index
 * @param {number} slot the insertion slot under the pointer
 * @return {?number} the destination index, or null
 */
export function destinationFor(from, slot) {
  const to = slot > from ? slot - 1 : slot;
  return to === from ? null : to;
}

/**
 * Wires row dragging onto `root`.
 *
 * `onReorder(from, to)` is called once, on a real move. Rows are addressed by
 * their position among the DRAGGABLE rows, which is what the browser side
 * translates back through the persisted array.
 */
/**
 * Resolves a finished drag into the one thing the browser should be asked to
 * do, or null when it should be asked for nothing.
 *
 * `from.index` is a position within its own section as RENDERED; `to.slot` is
 * an insertion slot in the destination section, counting the gaps between its
 * rows.
 *
 * The distinction that matters: a same-section drop needs the remove-then-
 * insert adjustment (destinationFor), because the row vacates a slot in the
 * very list it is being inserted into. A cross-section drop does not - the
 * destination list never shifts, so adjusting there lands the row one slot
 * short on every downward drop.
 *
 * `from.space` and `to.space` are space indices, present once a drag can page
 * between spaces while in flight. When they differ the whole drop
 * is a cross-space move, and the source index is meaningless without the space
 * it was resolved against - by then the browser's active space is the
 * DESTINATION.
 */
export function dropDecision({from, to} = {}) {
  if (!from || !to || !Number.isInteger(from.index)) {
    return null;
  }
  // A split's panes need not be adjacent, so no one-slot kind can move them.
  if (from.split) {
    if (to.section !== 'tabs' || !Number.isInteger(to.slot) ||
        !Number.isInteger(from.space) || !Number.isInteger(to.space)) {
      return null;
    }
    const sorted = [...from.slots].sort((a, b) => a - b);
    const contiguous =
        sorted.every((slot, i) => slot === sorted[0] + i);
    if (from.space === to.space && contiguous && to.slot >= sorted[0] &&
        to.slot <= sorted[sorted.length - 1] + 1) {
      return null;
    }
    return {kind: 'moveRun', slots: from.slots, fromSpace: from.space,
            toSpace: to.space, to: to.slot};
  }
  // Both ends need a live tab id, since a cold row and a folder head have no
  // page for a pane to show. Only the two flat lists split: inside a folder
  // body the same quarters already mean "join at this member's slot".
  if (to.edge && to.tabId && from.tabId && to.tabId !== from.tabId &&
      !from.isFolder && from.section !== 'favorites' &&
      (to.section === 'tabs' || to.section === 'pinned')) {
    if (Number.isInteger(from.space) && Number.isInteger(to.space) &&
        from.space !== to.space) {
      return null;
    }
    return {kind: 'split', side: to.edge, draggedTabId: from.tabId,
            targetTabId: to.tabId};
  }
  // A folder body declares itself as `folder:<group id>`. Dropping a row there
  // puts the tab in that group - membership is a back-reference on
  // the tab, so there is no slot to carry. A folder can never join another.
  if (typeof to.section === 'string' && to.section.startsWith('folder:')) {
    const groupID = to.section.slice('folder:'.length);
    // A member over its own folder: the same remove-then-insert adjustment
    // as any other same-list move.
    if (groupID && to.section === from.section && Number.isInteger(to.slot)) {
      const dest = destinationFor(from.index, to.slot);
      return dest === null
          ? null
          : {kind: 'reorderMember', groupID, from: from.index, to: dest};
    }
    // a favourite joins a folder the same way a row does, except it
    // is named by its url - it has no index into a list it was never part of.
    if (from.section === 'favorites') {
      if (!groupID || !from.url) {
        return null;
      }
      const known = Number.isInteger(to.slot) && Array.isArray(to.boxes) &&
          to.slot < to.boxes.length;
      return {kind: 'favoriteToFolder', url: from.url, groupID,
              memberSlot: known ? to.slot : -1};
    }
    if (!groupID || from.isFolder ||
        (from.section !== 'tabs' && from.section !== 'pinned')) {
      return null;
    }
    if (Number.isInteger(from.space) && Number.isInteger(to.space) &&
        from.space !== to.space) {
      return null;
    }
    const decision = {kind: 'group', from: from.index, groupID, section: from.section};
    // A head-only target (collapsed, or no body to measure) has no boxes, and
    // an append slot equals boxes.length - both must still land on the last
    // member, not be read as a real position among rows never seen.
    if (Number.isInteger(to.slot) && Array.isArray(to.boxes) &&
        to.slot < to.boxes.length) {
      decision.memberSlot = to.slot;
    }
    return decision;
  }
  // The dragged row's own section names the group it left. `from.index` is
  // its place among the folder's own members, not a destination slot.
  if (typeof from.section === 'string' && from.section.startsWith('folder:')) {
    const groupID = from.section.slice('folder:'.length);
    if (!groupID || (to.section !== 'pinned' && to.section !== 'tabs') ||
        !Number.isInteger(to.slot)) {
      return null;
    }
    return {kind: 'ungroup', groupID, member: from.index,
            toSection: to.section, to: to.slot};
  }
  // A regular folder head dropped into the pinned run pins the whole group -
  // named by its id, since a position among `folders` means nothing there.
  if (from.section === 'folders' && to.section === 'pinned') {
    if (!from.folderId || !Number.isInteger(to.slot)) {
      return null;
    }
    return {kind: 'pinFolder', groupID: from.folderId, to: to.slot};
  }
  if (to.section === 'favorites') {
    if (from.section !== 'tabs' && from.section !== 'pinned') {
      return null;
    }
    // null, not absent, when the grid could not name a slot - an
    // empty grid has no tile to be nearest to. The browser appends on null.
    return {kind: 'favorite', from: from.index, fromSection: from.section,
            to: Number.isInteger(to.slot) ? to.slot : null};
  }
  // out of the grid. The url is the favourite's whole identity, so a
  // tile without one names neither a favourite to remove nor a page to place.
  // A drop back on the grid never reaches here - that is the reorder gesture,
  // and sidebar-fav-drag commits it.
  if (from.section === 'favorites') {
    if (!from.url || (to.section !== 'tabs' && to.section !== 'pinned')) {
      return null;
    }
    if (!Number.isInteger(to.slot)) {
      return null;
    }
    return {
      kind: 'unfavorite',
      url: from.url,
      toSection: to.section,
      to: to.slot,
    };
  }
  if (!Number.isInteger(to.slot)) {
    return null;
  }
  if (Number.isInteger(from.space) && Number.isInteger(to.space) &&
      from.space !== to.space) {
    if (from.section !== 'tabs' && from.section !== 'pinned') {
      return null;
    }
    if (to.section !== 'tabs' && to.section !== 'pinned') {
      return null;
    }
    // The slot travels now. It deliberately did not until each pane
    // rendered its own space - before that there was no target list
    // to resolve one against - and moveTabAcrossSpaces has a destination
    // index since.
    //
    // NOT adjusted for the row leaving its old place: it vacates a slot in the
    // SOURCE space, and the destination list never shifts. The same trap as a
    // cross-section drop.
    return {
      kind: 'moveToSpace',
      from: from.index,
      fromSection: from.section,
      fromSpace: from.space,
      toSpace: to.space,
      to: to.slot,
    };
  }
  if (from.section === 'tabs' && to.section === 'tabs') {
    const dest = destinationFor(from.index, to.slot);
    return dest === null ? null : {kind: 'reorder', from: from.index, to: dest};
  }
  if (from.section === 'pinned' && to.section === 'pinned') {
    const dest = destinationFor(from.index, to.slot);
    return dest === null
        ? null
        : {kind: 'reorderPinned', from: from.index, to: dest};
  }
  if (from.section === 'folders' && to.section === 'folders') {
    const dest = destinationFor(from.index, to.slot);
    return dest === null
        ? null
        : {kind: 'reorderFolder', from: from.index, to: dest};
  }
  if (from.section === 'tabs' && to.section === 'pinned') {
    return {kind: 'pin', from: from.index, to: to.slot};
  }
  if (from.section === 'pinned' && to.section === 'tabs') {
    return {kind: 'unpin', from: from.index, to: to.slot};
  }
  return null;
}

/**
 * Wires dragging in the sidebar.
 *
 * A row belongs to whichever `[data-dropzone]` element contains it, so a
 * section is declared once by the container rather than repeated on every row.
 * `onDrop` receives the resolved dropDecision - one call, on drop, never per
 * pointer move: chrome.send is one-way, so there would be nothing to wait for.
 *
 * `onDragMove(tabId, screenX, screenY)` is the pointer's screen
 * position while the drag is in flight, for another window's live edge
 * highlight. Called with three nulls when the drag ends, however it ends, so
 * the highlight cannot outlive the gesture.
 *
 * `onRelease(tabId, screenX, screenY)` is called once, on a
 * dragend whose release never resolved to a drop at all (see dragend below).
 * Screen coordinates, not client ones - the classifier this feeds
 * hit-tests OTHER Atrium windows, for which a point relative to this sidebar's
 * own viewport is meaningless.
 */
export function installRowDrag(root, {onDrop, onEdgePage, onRelease, onDragMove,
                                      rowSelector = '.tabrow',
                                      splitActive = () => false,
                                      // What a favourite looks like as a tab
                                      // row, and a tab row as a tile.
                                      rowLook = () => document.createElement('div'),
                                      tileLook = () => document.createElement('div')} = {}) {
  let from = null;
  let indicator = null;
  const paging = createEdgePaging();
  let pagePoll = null;
  const follower = createDragFollower(root);

  const zoneOf = (el) => el?.closest?.('[data-dropzone]') ?? null;

  const spaceIndexFrom = (el, attribute, selector) => {
    const holder = el?.closest?.(selector);
    const index = holder ? Number(holder.dataset[attribute]) : NaN;
    if (Number.isInteger(index)) {
      return index;
    }
    // The favourites grid sits BESIDE the rail, so a tile has no pane to name
    // a space. Favourites belong to the profile, and the space showing is the
    // one a drag onto or off a tile means.
    const rail = el?.closest?.('.sbroot')?.querySelector?.('.rail');
    const active = rail ? Number(rail.dataset.activeSpace) : NaN;
    return Number.isInteger(active) ? active : undefined;
  };

  /**
   * The space a dragged row belongs to: the pane it is drawn in.
   *
   * This used to read the rail's active space instead, because every pane
   * rendered the ACTIVE space's rows retinted and a pane index would have named
   * whichever pane was on screen rather than where the row came from. Panes
   * carry their own contents, so the pane IS the answer - and
   * the two agreeing is what stops a plain reorder being read as a cross-space
   * move when the rail and the model disagree.
   */
  const sourceSpace = (el) => spaceIndexFrom(el, 'spaceIndex', '.railpane');

  /** The space a drop landed in - here the pane IS the answer. */
  const targetSpace = (el) => spaceIndexFrom(el, 'spaceIndex', '.railpane');
  const rowsIn = (zone) =>
      zone ? [...zone.children].filter((el) => el.matches(rowSelector)) : [];
  // A split draws several tabs as one row, so DOM position stops counting the
  // slots the core keeps. buildRows stamps the real slot on the row.
  const slotsIn = (row) => Math.max(1, row.querySelectorAll('.sseg').length);
  const slotOf = (zone, row) => {
    const stamped = row?.dataset?.slot;
    return stamped === undefined || stamped === '' ? rowsIn(zone).indexOf(row)
                                                   : Number(stamped);
  };
  const slotAtRow = (zone, rowIndex) => rowsIn(zone).slice(0, rowIndex).reduce(
      (n, row) => n + slotsIn(row), 0);

  // getBoundingClientRect forces layout, and dragover can fire many times a
  // frame. Cached until the next rAF. The cheap rowsIn(zone)
  // recheck still catches a live DOM edit (a fold) immediately, since
  // sidebar.js defers a rail rebuild until the drag ends.
  let frameRaf = null;
  let rootBoxCache = null;
  let zoneBoxCache = null; // {zone, rows, boxes}

  function clearFrameCache() {
    if (frameRaf !== null) {
      cancelAnimationFrame(frameRaf);
      frameRaf = null;
    }
    rootBoxCache = null;
    zoneBoxCache = null;
  }

  function armFrameCache() {
    if (frameRaf === null) {
      frameRaf = requestAnimationFrame(() => {
        frameRaf = null;
        rootBoxCache = null;
        zoneBoxCache = null;
      });
    }
  }

  function cachedRootBox() {
    armFrameCache();
    if (rootBoxCache === null) {
      rootBoxCache = root.getBoundingClientRect();
    }
    return rootBoxCache;
  }

  const sameRows = (a, b) =>
      a.length === b.length && a.every((el, i) => el === b[i]);

  function cachedZoneBoxes(zone) {
    armFrameCache();
    const rows = rowsIn(zone);
    if (zoneBoxCache && zoneBoxCache.zone === zone &&
        sameRows(zoneBoxCache.rows, rows)) {
      return zoneBoxCache.boxes;
    }
    const boxes = rows.map((r) => r.getBoundingClientRect());
    zoneBoxCache = {zone, rows, boxes};
    return boxes;
  }

  // The grid the pointer is over, measured once on arrival. Re-measuring would
  // resolve the same pointer to a different slot once a preview has moved the
  // tiles, and the drop would commit a slot the user never saw.
  let gridPreview = null;

  const scrollZones = () => [...root.querySelectorAll('.railpane .scrollzone')];

  // The pinned grid is absolute, and each pane holds its place with an empty
  // room grid. Without a slot there too, a ghost tile on a new line covers the
  // space name instead of pushing it down. An empty grid takes no room at rest,
  // so it opens, rooms and all, to show its hint. Returns the undo.
  function makeRoom(zone) {
    const rooms = zone.closest('.railpane') ? []
        : [...root.querySelectorAll('.railpane .favgrid[aria-hidden="true"]')];
    const grids = zone.classList.contains('empty') ? [zone, ...rooms] : [];
    const slots = grids.length ? [] : rooms.map(() => {
      const slot = document.createElement('div');
      slot.className = 'favslot';
      return slot;
    });
    flipMove(scrollZones(), () => {
      grids.forEach((grid) => grid.classList.add(OPEN_GRID_CLASS));
      slots.forEach((slot, i) => rooms[i].appendChild(slot));
    });
    return () => flipMove(scrollZones(), () => {
      grids.forEach((grid) => grid.classList.remove(OPEN_GRID_CLASS));
      slots.forEach((slot) => slot.remove());
    });
  }

  function undoGrid({zone, order, ghost, unroom}) {
    ghost?.remove();
    for (const tile of order) {
      zone.appendChild(tile);
    }
    clearTileTransforms(zone);
    unroom();
  }

  function clearGridPreview() {
    if (gridPreview) {
      const preview = gridPreview;
      gridPreview = null;
      undoGrid(preview);
    }
  }

  // A gap rather than a line: the grid wraps, so there is no space between two
  // tiles for a line to sit in.
  function previewGrid(zone, e) {
    if (!gridPreview || gridPreview.zone !== zone) {
      clearGridPreview();
      if (zone.classList.contains('empty')) {
        gridPreview = {zone, order: [], boxes: [], slot: 0, ghost: null,
                       unroom: makeRoom(zone)};
        return 0;
      }
      const tiles = [...zone.querySelectorAll('.fav')];
      // A folder has no page to become a tile of.
      const ghost = asDragCopy(from.isFolder ? document.createElement('div')
                                             : tileLook(from.el), 'fav favghost');
      gridPreview = {
        zone,
        order: tiles,
        boxes: tiles.map((t) => t.getBoundingClientRect()),
        slot: null,
        ghost,
        unroom: makeRoom(zone),
      };
    }
    const slot = nearestSlot({x: e.clientX, y: e.clientY}, gridPreview.boxes,
                             gridPreview.slot);
    if (slot !== gridPreview.slot) {
      gridPreview.slot = slot;
      const order = [...gridPreview.order];
      order.splice(slot, 0, gridPreview.ghost);
      reflowTiles(zone, order);
    }
    return slot;
  }

  // The list the ghost row is in. Tops count from the zone's own top, so a
  // scroll moves nothing.
  let listPreview = null;
  let heldPreview = null;
  let heldTimer = null;
  let heldGrid = null;


  function measureList(zone) {
    const zoneTop = zone.getBoundingClientRect().top;
    // Per frame, not per event: a pointer in a split band shows no preview.
    const live = cachedZoneBoxes(zone);
    const rows = zoneBoxCache.rows;
    // A same-list drag moves the row itself. A split member stays put: it is
    // one pane of a row. A folder head takes its body along.
    const row = from?.row;
    const own = row && !from.member && rows.includes(row) ? row : null;
    const body = own?.matches('.frow') &&
        own.nextElementSibling?.matches('.fold') ? own.nextElementSibling : null;
    const boxes = live.map((b) => ({top: b.top - zoneTop, bottom: b.bottom - zoneTop,
                                    left: b.left, right: b.right}));
    return {
      zone,
      rows,
      boxes,
      // Converts offsetTop to the same zone-relative tops as `boxes`.
      base: rows.length ? rows[0].offsetTop - boxes[0].top : 0,
      own,
      body,
      home: own ? (body || own).nextSibling : null,
      ghost: null,
      shown: null,
      // Read on the next hit test after each preview change.
      view: null,
    };
  }

  // Rest boxes would put a split under a pointer that sits on the moved row.
  // offsetTop leaves out the FLIP transforms: a box is where its row ends up.
  function layoutView(list) {
    const rows = new Set(list.rows);
    return [...list.zone.children]
        .filter((el) => rows.has(el) || el === list.ghost)
        .map((el) => {
          const top = el.offsetTop - list.base;
          return {el, top, bottom: top + el.offsetHeight};
        });
  }

  const listFor = (zone) =>
      listPreview?.zone === zone ? listPreview : measureList(zone);

  function ghostRow() {
    const ghost = from.tile ? rowLook(from.tile) : from.el.cloneNode(true);
    return asDragCopy(ghost, (from.isFolder ? 'row ' : 'row tablook ') + GHOST_CLASS);
  }

  // Shows the dragged item before row `index` of the measured list.
  function showList(list, index) {
    if (listPreview !== list) {
      clearListPreview();
      listPreview = list;
    }
    if (list.shown === index) {
      return;
    }
    list.shown = index;
    const before = list.rows[index] ?? list.anchor ?? null;
    // All children, not only rows: a pinned folder's body follows its head.
    flipMove([...list.zone.children], () => {
      if (list.own) {
        list.zone.insertBefore(list.own, before);
        if (list.body) {
          list.zone.insertBefore(list.body, before);
        }
        return;
      }
      list.ghost ??= ghostRow();
      list.zone.insertBefore(list.ghost, before);
    });
    list.view = null;
  }

  function undoList(list) {
    list.ghost?.remove();
    if (list.own) {
      list.zone.insertBefore(list.own, list.home);
      if (list.body) {
        list.zone.insertBefore(list.body, list.home);
      }
    }
    clearFlip(list.zone.children);
  }

  function clearListPreview() {
    if (listPreview) {
      const list = listPreview;
      listPreview = null;
      undoList(list);
    }
  }

  function releaseHeldPreview() {
    clearTimeout(heldTimer);
    heldTimer = null;
    if (heldPreview?.zone.isConnected) {
      undoList(heldPreview);
    }
    heldPreview = null;
    if (heldGrid?.zone.isConnected) {
      undoGrid(heldGrid);
    }
    heldGrid = null;
  }

  // A committed drop: the rebuild replaces the whole list, preview and all.
  function holdListPreview() {
    releaseHeldPreview();
    heldPreview = listPreview;
    listPreview = null;
    heldGrid = gridPreview;
    gridPreview = null;
    if (heldPreview || heldGrid) {
      heldTimer = setTimeout(releaseHeldPreview, HELD_PREVIEW_MS);
    }
  }

  // The space name pins a drop at the top of the pinned run, so the ghost goes
  // where that row lands: first in an open pinned body, else under the name.
  function nameTarget(name, space) {
    const fold = name.nextElementSibling;
    const body = fold?.matches('.fold') && !fold.classList.contains('closed')
        ? fold.querySelector('.foldin[data-dropzone="pinned"]')
        : null;
    let list;
    if (body) {
      list = listFor(body);
    } else if (listPreview?.name === name) {
      list = listPreview;
    } else {
      list = {zone: name.parentElement, name, anchor: name.nextSibling, rows: [],
              boxes: [], base: 0, own: null, body: null, home: null,
              ghost: null, shown: null, view: null};
    }
    return {section: 'pinned', zone: body ?? name, space, list, index: 0, slot: 0};
  }

  function listTarget(zone, section, space, e) {
    const list = listFor(zone);
    const y = e.clientY - zone.getBoundingClientRect().top;
    const preview = list.own ?? list.ghost;
    list.view ??= layoutView(list);
    const hit = list.view.find((b) => y >= b.top && y <= b.bottom);
    let index;
    if (hit && hit.el === preview) {
      // On the dragged row wherever it stands: the drop leaves it there.
      index = list.shown ?? list.rows.indexOf(preview);
    } else {
      const after = list.view.find(
          (b) => b.el !== preview && y < (b.top + b.bottom) / 2);
      index = after ? list.rows.indexOf(after.el) : list.rows.length;
    }
    const to = {section, zone, space, list, index,
                slot: list.rows.slice(0, index).reduce(
                    (n, row) => n + slotsIn(row), 0)};
    if (splitActive() || !hit || hit.el === preview) {
      return to;
    }
    const row = hit.el;
    if (row.dataset.tabId) {
      const {left, right} = list.boxes[list.rows.indexOf(row)];
      const edge = splitEdgeFor(e.clientX, y, {top: hit.top, bottom: hit.bottom,
                                               left, right});
      if (edge) {
        to.edge = edge;
        to.tabId = row.dataset.tabId;
        to.row = row;
      }
    }
    return to;
  }

  function ensureIndicator() {
    if (!indicator) {
      indicator = document.createElement('div');
      indicator.className = 'dropline';
    }
    // The rail rebuild after a push replaces every child of root, the line
    // included, so a cached node can be detached by the next drag.
    if (!indicator.isConnected) {
      root.appendChild(indicator);
    }
    return indicator;
  }

  function showLine(slot, boxes) {
    const line = ensureIndicator();
    const rootBox = cachedRootBox();
    const y = slot === 0
        ? boxes[0]?.top ?? rootBox.top
        : boxes[Math.min(slot, boxes.length) - 1].bottom;
    line.style.transform = `translateY(${(y - rootBox.top).toFixed(1)}px)`;
    line.hidden = false;
  }

  function hideLine() {
    if (indicator) {
      indicator.hidden = true;
    }
  }

  function clearZoneHighlights() {
    for (const zone of root.querySelectorAll('[data-dropzone].dropinto')) {
      zone.classList.remove('dropinto');
    }
  }

  function clearSplitHints() {
    for (const row of root.querySelectorAll('.' + SPLIT_CLASS)) {
      row.classList.remove(SPLIT_CLASS);
      delete row.dataset.splitSide;
    }
  }

  function startPaging() {
    // Polled rather than driven by dragover: holding still at an edge is the
    // whole gesture, and dragover stops firing the moment the pointer stops.
    if (onEdgePage && pagePoll === null) {
      pagePoll = setInterval(() => {
        const edge = paging.pageIfDue(performance.now());
        if (edge) {
          onEdgePage(directionFor(edge));
        }
      }, EDGE_POLL_MS);
    }
  }

  function stopPaging() {
    // The poll is the only thing this module leaves running between events, so
    // it has to be cleared on every path out of a drag - drop and dragend both
    // land here.
    if (pagePoll !== null) {
      clearInterval(pagePoll);
      pagePoll = null;
    }
    paging.setEdge(null, 0);
  }

  function clear() {
    stopPaging();
    clearFrameCache();
    clearGridPreview();
    clearListPreview();
    follower.stop();
    for (const row of root.querySelectorAll('.' + DRAGGING_CLASS)) {
      row.classList.remove(DRAGGING_CLASS);
    }
    // The empty favourites grid dims in only while a drag is in flight, so
    // that has to end with it - the hover highlight on a zone under the
    // pointer is cleared separately, below.
    root.classList.remove(ACTIVE_CLASS);
    clearZoneHighlights();
    clearSplitHints();
    hideLine();
    from = null;
  }

  // `member` is a split row's pane (.sseg), which owns its own slot and tab.
  function beginFrom(row, member = null) {
    // A favourite tile is a source too. It is not a `rowSelector`
    // row and never will be - it carries a url rather than a position - so it
    // is matched separately and hands `dropDecision` that url.
    const tile = row?.matches?.('.fav') ? row : null;
    const zone = row?.parentElement;
    const section = zone?.dataset?.dropzone;
    if (!row || !section || !root.contains(row)) {
      return false;
    }
    // The source index below is read from the DOM, which must be at rest.
    releaseHeldPreview();
    from = {
      section,
      url: tile ? tile.dataset.favId : undefined,
      index: tile ? [...zone.children].indexOf(tile)
                  : slotOf(zone, member || row),
      space: sourceSpace(row),
      // Carried only for the release message - nothing above this
      // reads it, since every existing drop kind addresses rows by index.
      tabId: (member || row).dataset.tabId,
      // A folder head dragged among pinned items or top-level folders - never
      // a target to group INTO, whichever section it started in.
      isFolder: row.matches('.frow'),
      folderId: row.dataset.folderId || undefined,
      // What the list preview moves or copies.
      el: member || row,
      row,
      member,
      tile,
    };
    // The address bar grip names one pane, and pulls only that pane out.
    if (!member && row.matches('.splitrow')) {
      from.split = true;
      from.slots = [...row.querySelectorAll('.sseg')].map(
          (seg) => Number(seg.dataset.slot));
    }
    (member || row).classList.add(DRAGGING_CLASS);
    root.classList.add(ACTIVE_CLASS);
    startPaging();
    return true;
  }

  root.addEventListener('dragstart', (e) => {
    const row = e.target.closest?.('.fav') || e.target.closest?.(rowSelector);
    if (!beginFrom(row)) {
      return;
    }
    // Required for a drop to be allowed at all. The payload is unused, and a
    // type of our own keeps a page's text field from taking the row as text.
    e.dataTransfer?.setData('application/x-atrium-tab', String(from.index));
    if (e.dataTransfer) {
      e.dataTransfer.effectAllowed = 'move';
    }
    // The drawn copy shows only inside the sidebar, so a tear-off drags no
    // image once it leaves. A split row keeps the native one: no tile or list
    // row stands for several tabs.
    if (from.tile) {
      follower.start(from.tile, e, {
        tile: asDragCopy(from.tile.cloneNode(true), 'fav'),
        row: asDragCopy(rowLook(from.tile), 'row tablook'),
      });
    } else if (row.matches('.tabrow') && !row.matches('.splitrow')) {
      follower.start(row, e, {
        tile: asDragCopy(tileLook(row), 'fav'),
        row: asDragCopy(row.cloneNode(true), 'row tablook'),
      });
    }
  });

  // Resolves the pointer to a destination, and paints the matching affordance.
  function targetFor(e) {
    let zone = zoneOf(e.target);
    let section = zone?.dataset?.dropzone;
    // A folder head is also its own 'folder:<id>' drop target, for a TAB
    // joining the group. A dragged FOLDER belongs one level up instead - its
    // own folders list or the pinned run, never another folder's zone.
    if (from?.isFolder && typeof section === 'string' &&
        section.startsWith('folder:')) {
      zone = zone.closest('[data-dropzone="folders"], [data-dropzone="pinned"]');
      section = zone?.dataset?.dropzone;
    }
    if (!section) {
      return null;
    }
    // A member over its own folder's head: the head is where a row JOINS the
    // group, and it has no rows to resolve a slot against. Only the body
    // reorders members.
    if (section === from?.section && zone.matches(rowSelector)) {
      return null;
    }
    const space = targetSpace(zone);
    // A tile's own reorder belongs to sidebar-fav-drag, which moves the tile
    // itself. A gap here would put the tile back in its old slot.
    if (section === 'favorites') {
      if (from?.section === 'favorites') {
        return {section, slot: null, zone, space};
      }
      const tiles = [...zone.querySelectorAll('.fav')]
          .filter((tile) => tile !== gridPreview?.ghost);
      return tiles.length >= FAVORITES_MAX
          ? null
          : {section, slot: previewGrid(zone, e), zone, space};
    }
    if (zone.matches('.wsname')) {
      return nameTarget(zone, space);
    }
    if (zone.matches(GHOST_LISTS)) {
      return listTarget(zone, section, space, e);
    }
    const boxes = cachedZoneBoxes(zone);
    // A split row occupies one box and several slots, so the row the pointer
    // stopped above does not count the slots before it.
    const to = {section,
                slot: slotAtRow(zone, insertionIndexFor(e.clientY, boxes)),
                zone, boxes, space};
    const row = splitActive() ? null : e.target?.closest?.(rowSelector);
    // cachedZoneBoxes just refreshed this, and its rows line up with `boxes`.
    const rowIndex = row ? zoneBoxCache.rows.indexOf(row) : -1;
    if (rowIndex >= 0 && row.dataset.tabId) {
      const edge = splitEdgeFor(e.clientX, e.clientY, boxes[rowIndex]);
      if (edge) {
        to.edge = edge;
        to.tabId = row.dataset.tabId;
        to.row = row;
      }
    }
    return to;
  }

  root.addEventListener('dragover', (e) => {
    if (!from) {
      return;
    }
    // dragleave stops the poll on exit. A dragover here means the pointer
    // came back, so the poll must resume.
    startPaging();
    // Without preventDefault the drop event never fires at all.
    e.preventDefault();
    if (e.dataTransfer) {
      e.dataTransfer.dropEffect = 'move';
    }
    // Measured against the sidebar's own viewport, not the rail: the rail is
    // one pane per space and many times wider, so its edges are off screen.
    const rootBox = cachedRootBox();
    paging.setEdge(edgeAtX(e.clientX - rootBox.left, rootBox.width),
                   performance.now());

    const to = targetFor(e);
    // Every path off the grid, including a pointer over no zone at all: the
    // tiles must close the gap they opened as soon as the drop stops aiming
    // at them.
    if (to?.section !== 'favorites') {
      clearGridPreview();
    }
    clearZoneHighlights();
    clearSplitHints();
    const decision = to ? dropDecision({from, to}) : null;
    follower.move(e.clientX, e.clientY, decision ? to.zone : null);
    // A split keeps a preview in its own list: undoing it would move another
    // row under the still pointer.
    if (!decision || !to.list ||
        (decision.kind === 'split' && to.list !== listPreview)) {
      clearListPreview();
    }
    if (listPreview?.ghost) {
      // A split shows no ghost row, but its place stays for the same reason.
      listPreview.ghost.style.visibility = decision?.kind === 'split' ? 'hidden' : '';
    }
    if (!decision) {
      hideLine();
      return;
    }
    if (decision.kind === 'split') {
      // The line and the side band answer the same question, so only one of
      // them is ever up.
      hideLine();
      // It sits on the row the band marks.
      follower.hide();
      to.row.classList.add(SPLIT_CLASS);
      to.row.dataset.splitSide = decision.side;
      return;
    }
    if (to.list) {
      hideLine();
      showList(to.list, to.index);
      return;
    }
    // The ghost tile marks a grid drop.
    if (to.section === 'favorites') {
      hideLine();
      return;
    }
    // A folder head has no place to show a ghost row.
    if (!to.boxes?.length) {
      hideLine();
      to.zone.classList.add('dropinto');
      return;
    }
    showLine(to.slot, to.boxes);
  });

  // Blink takes the drop operation from dragenter, not dragover, on the update
  // where the element under the pointer changes. A tile that reflows under a
  // still pointer changes it, and the release then drops nothing.
  root.addEventListener('dragenter', (e) => {
    if (from) {
      e.preventDefault();
    }
  });

  // dragover stops firing once the pointer leaves this WebContents, so it
  // cannot disarm the poll. dragleave still does, relatedTarget
  // outside root (null, once off this surface entirely).
  root.addEventListener('dragleave', (e) => {
    if (from && !root.contains(e.relatedTarget)) {
      stopPaging();
      follower.hide();
      clearListPreview();
      clearGridPreview();
    }
  });

  root.addEventListener('drop', (e) => {
    if (!from) {
      return;
    }
    e.preventDefault();
    // A drop is a one-off, not a stream, and it commits to whatever is on
    // screen right now - the frame cache exists to skip re-reading layout
    // between dragover ticks, not to answer this with a stale measurement.
    clearFrameCache();
    const to = targetFor(e);
    const decision = dropDecision({from, to});
    const shown = to?.list ? to.list === listPreview : to?.section === 'favorites';
    if (decision && decision.kind !== 'split' && shown) {
      holdListPreview();
    }
    clear();
    if (decision) {
      placed = true;
      onDrop?.(decision);
    }
  });

  // dragend fires even when the drag is released outside every target, which
  // is the signal SwiftUI's .onDrag never had - see SidebarRowDrag.swift's
  // 887 lines of AppKit written to work around exactly that.
  //
  // the 'drop' listener above always clears `from` when IT fires -
  // whether or not the pointer was over a recognized dropzone, whether or not
  // the drop resolved to a decision. A release that lands anywhere inside this
  // surface's own DOM (even a gap between zones) reaches 'drop' by bubbling, so
  // what survives to see `from` still set here is exactly a release outside the
  // sidebar entirely - the one case 'drop' never gets a chance to see. That is
  // also the only way the point can be outside the bounding rect, so checking
  // `from` here stands in for checking dropDecision was null: any resolved or
  // rejected drop already nulled it above.
  // the live preview's position feed. `drag` fires on the SOURCE
  // throughout the gesture, including while the pointer is over a different OS
  // window - which `dragover` never sees, because that is a different document.
  // It is the same coordinate source `dragend` below already relies on.
  //
  // Sent only when the point actually moved: a stationary drag fires this
  // continuously, and each message classifies against every open window.
  let lastMove = '';
  root.addEventListener('drag', (e) => {
    if (!from || !onDragMove) {
      return;
    }
    const key = `${e.screenX},${e.screenY}`;
    if (key === lastMove) {
      return;
    }
    lastMove = key;
    onDragMove(from.tabId, e.screenX, e.screenY);
  });

  root.addEventListener('dragend', (e) => {
    lastMove = '';
    // Clears whatever this drag put up, on EVERY ending - a drop inside the
    // sidebar and an Escape both land here, and neither reaches the release
    // path that would otherwise do the clearing.
    if (onDragMove) {
      onDragMove(null, null, null);
    }
    const dragged = from;
    // A tear-off or a split names one tab, and a split row is several.
    if (dragged && !dragged.split && onRelease) {
      const rect = root.getBoundingClientRect();
      const outside = e.clientX < rect.left || e.clientX > rect.right ||
          e.clientY < rect.top || e.clientY > rect.bottom;
      if (outside && dragged.tabId) {
        onRelease(dragged.tabId, e.screenX, e.screenY);
      } else if (outside) {
        // data-tab-id is what release routing addresses a row by,
        // and no row type renders without it today. A row type that did would
        // lose tear-off, cross-window moves and splits with nothing to read.
        console.warn(
            'sidebar-drag: release ignored, row has no tabId - section ' +
            `${dragged.section}, index ${dragged.index}, space ${dragged.space}`);
      }
    }
    clear();
  });

  // the space dots live outside `root`, so their drop handler
  // cannot reach the drag through an event on a row. `drop` fires before
  // `dragend` clears this, so a dot reading it still sees the source.
  // the address bar's grip drags in its own document, so no
  // dragstart or dragend reaches this one and the browser brackets the drag.
  // Whether this sidebar placed the row, so the browser only then pulls a
  // split pane out of its split.
  let placed = false;
  const escapeId = (id) => String(id).replace(/["\\]/g, '\\$&');
  const beginExternal = (tabId) => {
    clear();
    placed = false;
    if (!tabId) {
      return false;
    }
    const named = [...root.querySelectorAll(
        `[data-tab-id="${escapeId(tabId)}"]`)]
        .filter((el) => !el.closest('.ovrailpane'));
    const member = named.find((el) => el.matches('.sseg')) || null;
    const row = member ? member.closest(rowSelector)
                       : named.find((el) => el.matches('.tabrow'));
    return beginFrom(row, member);
  };
  const endExternal = () => {
    const wasPlaced = placed;
    placed = false;
    clear();
    return wasPlaced;
  };
  return {dragSource: () => from, beginExternal, endExternal};
}
