// Copyright 2026 The Atrium Authors
//
// a second drop target for sidebar-drag.js's row drag.

const ZONE_CLASS = 'shelfzone';
const HOT_CLASS = 'hot';
const ON_RIGHT_CLASS = 'sbright';

// How far outside the zone's own box the drag still counts as "approaching"
// it - the same tolerance the lab's own proximity check uses.
const HOT_MARGIN_X = 70;
const HOT_MARGIN_Y = 60;

// `send(tabId)` fires once, on a real drop - the tab itself is never closed.
export function installShelfDropZone(root, {send} = {}) {
  let zone = null;
  let dragTabId = null;

  function ensureZone() {
    if (zone) {
      return zone;
    }
    zone = document.createElement('div');
    zone.className = ZONE_CLASS;
    zone.hidden = true;
    const label = document.createElement('span');
    label.textContent = 'Shelf';
    zone.appendChild(label);
    zone.addEventListener('dragover', (e) => {
      e.preventDefault();
      if (e.dataTransfer) {
        e.dataTransfer.dropEffect = 'move';
      }
    });
    zone.addEventListener('drop', (e) => {
      e.preventDefault();
      e.stopPropagation();
      const tabId = dragTabId;
      hide();
      if (tabId) {
        send?.(tabId);
      }
    });
    root.appendChild(zone);
    return zone;
  }

  function hide() {
    if (zone) {
      zone.hidden = true;
      zone.classList.remove(HOT_CLASS);
    }
    dragTabId = null;
  }

  root.addEventListener('dragstart', (e) => {
    const row = e.target.closest?.('.tabrow');
    const tabId = row?.dataset?.tabId;
    if (!row || !tabId) {
      return;
    }
    dragTabId = tabId;
    ensureZone().hidden = false;
  });

  root.addEventListener('dragover', (e) => {
    if (!zone || zone.hidden) {
      return;
    }
    const zr = zone.getBoundingClientRect();
    zone.classList.toggle(HOT_CLASS,
        e.clientX > zr.left - HOT_MARGIN_X && e.clientX < zr.right + HOT_MARGIN_X &&
        e.clientY > zr.top - HOT_MARGIN_Y && e.clientY < zr.bottom + HOT_MARGIN_Y);
  });

  // dragend always fires, dropped on the zone or not - the cleanup path.
  root.addEventListener('dragend', hide);

  return {
    // Same edge the sidebar itself docks on - the zone sits inside it.
    setOnRight(onRight) {
      ensureZone().classList.toggle(ON_RIGHT_CLASS, !!onRight);
    },
  };
}
