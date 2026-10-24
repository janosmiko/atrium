// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The pinned-extension strip under the address field. Chromium's own pin
// state decides what is here. There is no Atrium-side placement any more.

import {extIconPath} from './icon-scope.js';
import {buildIcon, screenRectOf} from './sidebar-ext-shared.js';

// A fifth tile in one row is below its own hit target, so the strip wraps
// instead of narrowing. The floating bar's row scrolls and passes no cap.
export const STRIP_COLUMNS = 4;
export const MAX_PINNED_TILES = STRIP_COLUMNS * 3;

// The lane the tiles below give up is this many rows tall, so the element that
// carries the lane class needs the same number the strip drew with.
export function stripRows(pinnedCount) {
  return Math.max(1, Math.ceil(Math.min(pinnedCount, MAX_PINNED_TILES) /
                               STRIP_COLUMNS));
}

// Spread over the rows stripRows() gives rather than filling each one to four:
// a flat four columns leaves three pins three quarters of the way across the
// sidebar, and puts the fifth alone under a full row. Five becomes 3 + 2.
export function stripColumns(pinnedCount) {
  const count = Math.min(pinnedCount, MAX_PINNED_TILES);
  return Math.max(1, Math.ceil(count / stripRows(count)));
}

// A fresh <img> paints nothing for a few frames even from cache, so a redraw
// that rebuilt every icon blanked the strip on a space switch. The key leaves
// out the rev but keeps the profile scope, so no icon crosses profiles.
const loadedPathOf = new WeakMap();

function iconCarryKey(path) {
  return path.replace(/\?.*$/, '');
}

function takeLoadedIcons(container) {
  const icons = new Map();
  for (const img of container.querySelectorAll('.crx-icon')) {
    const path = loadedPathOf.get(img);
    if (path) {
      icons.set(iconCarryKey(path), img);
    }
  }
  return icons;
}

// A new rev keeps the old icon on screen until the new bytes arrive. If a
// later redraw took the old icon to another chip first, this swap is stale.
function attachStripIcon(chip, action, carried) {
  const path = extIconPath(action.id, action.iconRev, action.iconScope);
  const old = path ? carried.get(iconCarryKey(path)) : null;
  if (old && loadedPathOf.get(old) === path) {
    chip.appendChild(old);
    return;
  }
  const icon = buildIcon(action);
  icon.addEventListener('load', () => {
    loadedPathOf.set(icon, path);
    if (old?.parentNode === chip) {
      old.replaceWith(icon);
    }
  }, {once: true});
  chip.appendChild(old || icon);
}

function buildStripChip(action, onChipClick, onChipContextMenu, inert,
                        carried) {
  const chip = document.createElement('button');
  chip.className = 'exs-chip';
  chip.dataset.extensionId = action.id;
  // title, not a rendered node: an extension name is untrusted text.
  chip.title = action.actionTitle || action.name || action.id;
  attachStripIcon(chip, action, carried);
  if (inert) {
    chip.setAttribute('tabindex', '-1');
    // Otherwise the click bubbles past this copy to the sidebar's own
    // delegated listener and opens the empty-space menu instead.
    chip.addEventListener('contextmenu', (e) => {
      e.preventDefault();
      e.stopPropagation();
    });
    return chip;
  }
  chip.addEventListener('click', (e) => {
    e.stopPropagation();
    onChipClick(action.id, screenRectOf(chip));
  });
  chip.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    e.stopPropagation();
    onChipContextMenu(action.id, screenRectOf(chip));
  });
  return chip;
}

export function renderExtStrip(container, {
  actions = [],
  onChipClick = () => {},
  onChipContextMenu = () => {},
  max = Infinity,
  // A copy in a rail pane: on screen only mid-swipe, and a second set of the
  // same chips in the tab order is what buildFavRoom's empty slots avoid.
  inert = false,
} = {}) {
  const carried = takeLoadedIcons(container);
  container.replaceChildren();
  const pinned = actions.filter((a) => a.pinned).slice(0, max);
  container.classList.toggle('exs-hidden', pinned.length === 0);
  container.style.setProperty('--exs-rows', String(stripRows(pinned.length)));
  container.style.setProperty('--exs-cols', String(stripColumns(pinned.length)));
  if (inert) {
    container.setAttribute('aria-hidden', 'true');
  }
  for (const action of pinned) {
    container.appendChild(
        buildStripChip(action, onChipClick, onChipContextMenu, inert, carried));
  }
  return pinned.length;
}
