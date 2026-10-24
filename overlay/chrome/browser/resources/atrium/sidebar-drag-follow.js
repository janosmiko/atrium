// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The native drag image is fixed at dragstart, and a favourite dropped on a
// list becomes a tab, a tab dropped on the grid a favourite. Only inside the
// sidebar: nothing outside takes one.

// Half a row, so the pointer holds the row at its middle.
const ROW_GRIP_Y = 18;

// A copy that no lookup of the real rows and tiles finds: those go by
// .tabrow, a tab id or a favourite id.
export function asDragCopy(el, className) {
  el.className = className;
  for (const node of [el, ...el.querySelectorAll('[data-tab-id], [data-fav-id]')]) {
    node.removeAttribute('data-tab-id');
    node.removeAttribute('data-fav-id');
  }
  el.draggable = false;
  return el;
}

export function createDragFollower(root) {
  // A drag image of nothing. Blink paints the element it is given, so it has
  // to be laid out, but it may sit off screen.
  const blank = document.createElement('div');
  blank.className = 'dragblank';
  let el = null;
  let tileLook = null;
  let rowLook = null;
  let own = null;
  let grip = {x: 0, y: 0};
  let width = 0;
  let tileBox = null;
  // dragover fires many times a frame, and each read would force a layout.
  let zoneBox = null;

  function show(look, name) {
    if (el.firstChild !== look) {
      el.replaceChildren(look);
    }
    if (!el.isConnected) {
      root.appendChild(el);
    }
    el.hidden = false;
    el.dataset.look = name;
  }

  function sizeTile(box) {
    if (!box?.width) {
      return;
    }
    tileBox = {width: box.width, height: box.height};
    tileLook.style.width = box.width + 'px';
    tileLook.style.height = box.height + 'px';
  }

  const place = (x, y) => {
    el.style.transform = `translate(${x}px, ${y}px)`;
  };

  return {
    // `looks` holds the tile and the row the source shows as. The one that
    // is the source's own shape keeps the pointer where it was picked up.
    start(source, event, looks) {
      this.stop();
      if (!blank.isConnected) {
        root.appendChild(blank);
      }
      event.dataTransfer?.setDragImage?.(blank, 0, 0);
      const box = source.getBoundingClientRect();
      grip = {x: event.clientX - box.left, y: event.clientY - box.top};
      width = box.width;
      ({tile: tileLook, row: rowLook} = looks);
      own = source.matches('.fav') ? 'tile' : 'row';
      tileBox = null;
      if (own === 'tile') {
        sizeTile(box);
      }
      el = document.createElement('div');
      el.className = 'dragfollow';
      el.hidden = true;
    },

    // `zone` is the zone the drop would land in, or null. Over a list the row
    // snaps to the list's own column. Over the grid a row turns into a tile.
    move(x, y, zone) {
      if (!el) {
        return;
      }
      const grid = zone?.dataset?.dropzone === 'favorites';
      if (zone && !grid) {
        if (zoneBox?.zone !== zone) {
          zoneBox = {zone, box: zone.getBoundingClientRect()};
          requestAnimationFrame(() => {
            zoneBox = null;
          });
        }
        const {box} = zoneBox;
        // On the holder, not the row: a row's width leaves out its padding.
        el.style.width = box.width + 'px';
        show(rowLook, 'row');
        place(box.left, y - ROW_GRIP_Y);
        return;
      }
      if (grid && own === 'row') {
        if (!tileBox) {
          // The grid's own tile, or the gap the preview opened for this one.
          sizeTile(zone.querySelector('.fav')?.getBoundingClientRect());
        }
        el.style.width = '';
        show(tileLook, 'tile');
        place(x - (tileBox?.width ?? 0) / 2, y - (tileBox?.height ?? 0) / 2);
        return;
      }
      el.style.width = own === 'row' ? width + 'px' : '';
      show(own === 'row' ? rowLook : tileLook, own);
      place(x - grip.x, y - grip.y);
    },

    hide() {
      if (el) {
        el.hidden = true;
      }
    },

    stop() {
      // Blink copies the drag image at dragstart, so the blank can go.
      blank.remove();
      el?.remove();
      el = null;
      tileLook = null;
      rowLook = null;
    },
  };
}
