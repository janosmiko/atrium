// Copyright 2026 The Atrium Authors
//
// The Courtyard canvas model. Pure: a sidebar snapshot in,
// plane geometry and one drop decision out. No DOM, no chrome.send - the view
// above it does both.
//
// A card has no stored y. Its vertical place is its index in its own section,
// so the sidebar order is the one source of truth and a canvas drag reorders
// the sidebar rather than shadowing it. Only x is stored, and only for a tab
// or a pinned row: a favourite is keyed by (profile, url) and is not a tab, so
// the core has nowhere to keep an offset for it.
//
// Every kind of plane item - room, card, and later a note or a stroke - comes
// out of one `items[]` list, so a second kind needs no second traversal.

export const CARD_TOP = 34;
export const CARD_STEP = 56;
export const CARD_HEIGHT = 44;
export const ROOM_PAD = 12;
export const ROOM_DEFAULT_W = 320;
export const ROOM_DEFAULT_H = 240;
export const ROOM_GAP = 40;
export const ZOOM_MIN = 0.35;
export const ZOOM_MAX = 2;
export const FIT_MARGIN = 48;

/** The lanes a room draws, in this order. Empty sections draw none. */
export const LANE_ORDER = ['pinned', 'favorites', 'tabs'];

/** A card's top inside its lane. The spec's one layout rule. */
export function cardY(index) {
  return CARD_TOP + index * CARD_STEP;
}

/** The plane's zoom range. A broken value falls back to 1 rather than to 0. */
export function clampZoom(zoom) {
  if (!Number.isFinite(zoom)) {
    return 1;
  }
  return Math.min(ZOOM_MAX, Math.max(ZOOM_MIN, zoom));
}

// The pan and zoom that fits and centres the bounding box of `rects` in the
// viewport. Capped at 1: a small layout sits at its own size, never blown up.
export function fitView(rects, viewportW, viewportH) {
  if (!rects || !rects.length) {
    return {x: 0, y: 0, zoom: 1};
  }
  const minX = Math.min(...rects.map((r) => r.x));
  const minY = Math.min(...rects.map((r) => r.y));
  const maxX = Math.max(...rects.map((r) => r.x + r.w));
  const maxY = Math.max(...rects.map((r) => r.y + r.h));
  const boxW = maxX - minX;
  const boxH = maxY - minY;

  const zoom = Math.min(
      1, viewportW / (boxW + 2 * FIT_MARGIN),
      viewportH / (boxH + 2 * FIT_MARGIN));
  const centerX = minX + boxW / 2;
  const centerY = minY + boxH / 2;
  return {
    x: viewportW / 2 - centerX * zoom,
    y: viewportH / 2 - centerY * zoom,
    zoom,
  };
}

/** The height a lane of `count` cards needs, including the bottom pad. */
export function laneHeight(count) {
  if (count <= 0) {
    return CARD_TOP;
  }
  return cardY(count - 1) + CARD_HEIGHT + ROOM_PAD;
}

/**
 * The destination index for a same-section drop.
 *
 * The row vacates a slot in the very list it is inserted into, so a downward
 * drop lands one short without this. Same rule as sidebar-drag.js, repeated
 * rather than imported: this module must stay free of the sidebar's DOM code.
 */
export function destinationFor(from, slot) {
  const to = slot > from ? slot - 1 : slot;
  return to === from ? null : to;
}

// A pinned folder (kind: 'folder', see sidebar.js's own buildPinnedFold) has
// no title/color/url of its own - flatten it to its member tabs, the same
// rule sidebar.js's findActiveTab already applies to this list.
function pinnedCards(space) {
  // `item` is what the core counts, and a folder is one item.
  return (space.pinned || []).flatMap((p, item) =>
      (p && p.kind === 'folder') ?
          (p.tabs || []).map((row, member) => ({row, item, folder: p.id, member})) :
          [{row: p, item}]);
}

function sectionRows(space, section) {
  if (section === 'pinned') {
    return pinnedCards(space).map((card) => card.row);
  }
  if (section === 'favorites') {
    return space.favorites || [];
  }
  return space.tabs || [];
}

/** The rects of a room's lanes, splitting its width evenly. */
export function laneRects(rect, sections) {
  const width = sections.length ? rect.w / sections.length : rect.w;
  return sections.map((section, i) => ({
    section,
    x: rect.x + i * width,
    y: rect.y,
    w: width,
    h: rect.h,
  }));
}

function validRect(rect) {
  return !!rect && [rect.x, rect.y, rect.w, rect.h].every(Number.isFinite) &&
      rect.w > 0 && rect.h > 0;
}

/**
 * Every plane item the snapshot describes, rooms before their own cards.
 *
 * `spaceIndex` is the index in the SNAPSHOT, so a click enters the right space
 * and a drop addresses the right room. `index` on a card is its position in
 * its own section, because that is what the move ABI addresses a row by.
 *
 * A room with no stored rect goes right of the rightmost room placed so far,
 * at row zero. That walk is left to right in snapshot order, so the same
 * snapshot gives the same rect every time.
 */
export function itemsFrom(snapshot) {
  const spaces = (snapshot && snapshot.spaces) || [];
  const activeSpace = snapshot ? snapshot.activeSpace : -1;
  const items = [];
  let nextX = 0;

  for (let spaceIndex = 0; spaceIndex < spaces.length; spaceIndex++) {
    const space = spaces[spaceIndex];
    if (space.overview) {
      continue;
    }
    const sections = LANE_ORDER.filter(
        (section) => sectionRows(space, section).length > 0);
    const needed = sections.reduce(
        (tallest, section) =>
            Math.max(tallest, laneHeight(sectionRows(space, section).length)),
        CARD_TOP);

    const stored = validRect(space.canvas) ? space.canvas : null;
    const rect = {
      x: stored ? stored.x : nextX,
      y: stored ? stored.y : 0,
      w: stored ? stored.w : ROOM_DEFAULT_W,
      // A hand resize sets the persisted MINIMUM. A lane that outgrows it
      // pushes the room taller rather than clipping its own cards.
      h: Math.max(stored ? stored.h : ROOM_DEFAULT_H, needed),
    };
    nextX = Math.max(nextX, rect.x + rect.w + ROOM_GAP);

    const pinned = pinnedCards(space);
    const lanes = laneRects(rect, sections).map((lane) => ({
      ...lane,
      count: sectionRows(space, lane.section).length,
      spaceIndex,
      // The pinned item a drop at each card slot lands before.
      ...(lane.section === 'pinned' && {
        itemSlots: [...pinned.map((c) => c.item), (space.pinned || []).length],
      }),
    }));

    items.push({
      kind: 'room',
      spaceIndex,
      name: space.name,
      color: space.color,
      icon: space.icon || (space.name || '')[0] || '',
      current: spaceIndex === activeSpace,
      placed: !!stored,
      rect,
      lanes,
    });

    for (const lane of lanes) {
      const rows = sectionRows(space, lane.section);
      for (let index = 0; index < rows.length; index++) {
        const row = rows[index];
        items.push({
          kind: 'card',
          spaceIndex,
          section: lane.section,
          index,
          ...(lane.section === 'pinned' && {
            item: pinned[index].item,
            folder: pinned[index].folder,
            member: pinned[index].member,
          }),
          id: row.id,
          title: row.title,
          color: row.color,
          url: row.url,
          selected: !!row.selected,
          x: lane.x + (Number.isFinite(row.canvasX) ? row.canvasX : 0),
          y: lane.y + cardY(index),
          w: lane.w,
          h: CARD_HEIGHT,
          lanePlaneX: lane.x,
          lanePlaneY: lane.y,
        });
      }
    }
  }
  return items;
}

/** The lane under a plane point, or null when the point is on bare plane. */
export function laneAt(items, x, y) {
  for (const item of items) {
    if (item.kind !== 'room') {
      continue;
    }
    for (const lane of item.lanes) {
      if (x >= lane.x && x < lane.x + lane.w && y >= lane.y &&
          y < lane.y + lane.h) {
        return lane;
      }
    }
  }
  return null;
}

/** The insertion slot a plane y names inside `lane`, clamped to the section. */
export function dropIndex(lane, y) {
  const raw = Math.round((y - lane.y - CARD_TOP) / CARD_STEP);
  return Math.min(Math.max(raw, 0), lane.count);
}

/**
 * The one thing a finished card drag asks the browser to do, as
 * `{send, args}`, or null when it asks for nothing.
 *
 * `send` is the chrome.send name and `args` is its payload, so the view sends
 * without a second switch of its own. The section moves reuse the
 * sidebar's own messages, because the browser window already handles every
 * one of them.
 *
 * The four same-room messages carry a THIRD argument the sidebar never sends:
 * the room's space index. The sidebar can leave it out because its rows always
 * belong to the active space, and the handler then passes -1, which means "the
 * document's own activeSpaceID decides". The Courtyard shows every room at
 * once, so it must say which.
 *
 * `sidebarFavoriteMovedOut` gets no such argument. Its handler activates or
 * opens a tab in the CURRENT browser, so it only means anything in the active
 * room, and a drop out of another room's favourites lane is refused.
 */
export function dropDecision(drag, point, items, activeSpace) {
  if (!drag || !point || !Number.isInteger(drag.index)) {
    return null;
  }
  const lane = laneAt(items, point.x, point.y);
  if (!lane) {
    return null;
  }
  const toSpace = lane.spaceIndex;
  const to = lane.section;
  const from = drag.section;
  const cardSlot = dropIndex(lane, point.y);
  const slot = lane.itemSlots ? lane.itemSlots[cardSlot] : cardSlot;
  const card = from === 'pinned' ?
      items.find((i) => i.kind === 'card' && i.section === 'pinned' &&
                     i.spaceIndex === drag.spaceIndex && i.index === drag.index) :
      null;
  if (from === 'pinned' && !card) {
    return null;
  }
  if (card && card.folder !== undefined) {
    return memberDropDecision(card, to, toSpace, cardSlot, slot);
  }
  const index = card ? card.item : drag.index;

  if (toSpace !== drag.spaceIndex) {
    // The move export names a SOURCE section and no target one, so any lane
    // but the target's tabs lane is refused and the card snaps back. A
    // favourite is per profile rather than per space, so it cannot be a source.
    if (from === 'favorites' || to !== 'tabs') {
      return null;
    }
    return {
      send: 'sidebarTabMovedToSpace',
      args: [drag.spaceIndex, index, from, toSpace, slot],
    };
  }

  if (from === to) {
    const dest = destinationFor(index, slot);
    if (dest === null) {
      if (from === 'favorites') {
        return null;
      }
      return {
        send: 'courtyardCardX',
        args: [toSpace, from, drag.index, Math.round(point.x - lane.x)],
      };
    }
    if (from === 'tabs') {
      return {send: 'sidebarTabReordered', args: [index, dest, toSpace]};
    }
    if (from === 'pinned') {
      return {send: 'sidebarPinnedReordered', args: [index, dest, toSpace]};
    }
    return {send: 'sidebarFavoriteReordered', args: [index, dest, toSpace]};
  }

  if (to === 'favorites') {
    return {send: 'sidebarRowFavorited', args: [index, from, toSpace]};
  }
  if (from === 'favorites') {
    if (!drag.url || toSpace !== activeSpace) {
      return null;
    }
    return {send: 'sidebarFavoriteMovedOut', args: [drag.url, to, slot]};
  }
  if (from === 'tabs') {
    return {send: 'sidebarTabPinned', args: [index, slot, toSpace]};
  }
  return {send: 'sidebarTabUnpinned', args: [index, slot, toSpace]};
}

// Every item-counted call would name the folder, so a member moves only
// through the ungroup call. A drop strictly inside its own folder is refused.
function memberDropDecision(card, to, toSpace, cardSlot, slot) {
  const firstMember = card.index - card.member;
  if (toSpace !== card.spaceIndex || to === 'favorites' ||
      (to === 'pinned' && slot === card.item && cardSlot !== firstMember)) {
    return null;
  }
  return {
    send: 'sidebarTabUngrouped',
    args: [card.folder, card.member, to, slot, toSpace],
  };
}
