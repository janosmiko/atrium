// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//
// the reveal panel's width handle, drawn by the page.
//
// The in-window seam cannot serve the panel. The panel is a child window, so
// it takes every event over its own frame and a band the parent puts under it
// is dead; a band placed just outside it reads as a handle floating away from
// the sidebar. The lab has the same split - a .resizer between the docked pane
// and the page, and a separate .ovresize inside the sidebar itself while it
// floats (swipe-lab.html:42-45).

const LIVE = 'sidebarOverlayWidthLive';
const COMMIT = 'sidebarOverlayWidthCommit';

// Keyed by root so a second install tears down its own prior handle rather
// than orphaning it - sidebar-nav-row.js's lesson.
const handlesByRoot = new WeakMap();

function dockedOnRight() {
  return document.documentElement.classList.contains('atrium-sidebar-on-right');
}

export function buildOverlayResizer(
    {send = (name, args) => chrome.send(name, args)} = {}) {
  const handle = document.createElement('div');
  handle.className = 'ovresize';
  handle.title = 'Drag to resize';

  let startScreenX = 0;
  let startWidth = 0;

  // screenX, not clientX: the panel's own frame moves under the pointer as it
  // grows, so a client-space delta measures the drag against a ruler the drag
  // is moving.
  const widthFor = (event) => {
    const dx = event.screenX - startScreenX;
    return startWidth + (dockedOnRight() ? -dx : dx);
  };

  handle.addEventListener('pointerdown', (event) => {
    if (event.button !== 0) {
      return;
    }
    startScreenX = event.screenX;
    startWidth = document.documentElement.clientWidth;
    handle.setPointerCapture(event.pointerId);
    handle.classList.add('on');
    event.preventDefault();
  });

  handle.addEventListener('pointermove', (event) => {
    if (!handle.hasPointerCapture(event.pointerId)) {
      return;
    }
    send(LIVE, [widthFor(event)]);
  });

  const end = (event) => {
    if (!handle.hasPointerCapture(event.pointerId)) {
      return;
    }
    handle.releasePointerCapture(event.pointerId);
    handle.classList.remove('on');
    send(COMMIT, [widthFor(event)]);
  };
  handle.addEventListener('pointerup', end);
  handle.addEventListener('pointercancel', end);

  return handle;
}

// Outside #atrium-root, like the nav row: every snapshot push calls
// root.replaceChildren(), and a handle taken mid-drag loses its capture.
export function installOverlayResizer(root, options = {}) {
  handlesByRoot.get(root)?.remove();
  const handle = buildOverlayResizer(options);
  (root.parentNode || document.body).insertBefore(handle, root);
  handlesByRoot.set(root, handle);
  return handle;
}
