// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The /theme route: the theme studio in its own child window. The window is
// moved by the browser, not by CSS - the header only reports pointer deltas.

import {mountThemeStudio} from './theme-studio.js';

function installHeaderDrag(head, send) {
  let last = null;
  head.addEventListener('pointerdown', (event) => {
    // The close button is a click, and a drag that begins on it would move
    // the window out from under the release.
    if (event.button !== 0 || event.target.closest('.tw-close')) {
      return;
    }
    // Screen coordinates, not client: the browser moves this window under
    // the pointer, so client deltas reverse on the next move and the window
    // bounces between two spots.
    last = {x: event.screenX, y: event.screenY};
    head.setPointerCapture?.(event.pointerId);
    event.preventDefault();
  });
  head.addEventListener('pointermove', (event) => {
    if (!last) {
      return;
    }
    // Integers, because a fractional remainder that never reaches the widget
    // is lost. The remainder is carried to the next move instead.
    const dx = Math.round(event.screenX - last.x);
    const dy = Math.round(event.screenY - last.y);
    if (dx === 0 && dy === 0) {
      return;
    }
    last = {x: last.x + dx, y: last.y + dy};
    send('themeStudioDragBy', [dx, dy]);
  });
  const release = () => {
    last = null;
  };
  head.addEventListener('pointerup', release);
  head.addEventListener('pointercancel', release);
}

export function installThemeWindow(root, {send = (n, a) => chrome.send(n, a)} = {}) {
  root.replaceChildren();
  root.classList.add('themeroot');

  const card = document.createElement('div');
  card.className = 'tw ov-card';

  const head = document.createElement('div');
  head.className = 'tw-head';
  const name = document.createElement('span');
  name.className = 'tw-name';
  const close = document.createElement('button');
  close.className = 'tw-close';
  close.type = 'button';
  close.title = 'Close';
  close.textContent = '×';
  close.addEventListener('click', () => send('themeStudioClose', []));
  head.append(name, close);
  installHeaderDrag(head, send);

  const body = document.createElement('div');
  body.className = 'tw-body';
  card.append(head, body);
  root.appendChild(card);

  document.addEventListener('keydown', (event) => {
    if (event.key === 'Escape') {
      send('themeStudioClose', []);
    }
  });

  let studio = null;
  window.atriumSetThemeStudio = (payload) => {
    if (!payload?.spaceId) {
      return 'no space';
    }
    studio?.destroy();
    body.replaceChildren();
    name.textContent = payload.spaceName || '';
    const spaceId = payload.spaceId;
    studio = mountThemeStudio(body, {
      spec: payload.spec || null,
      // The header names the space. The module's own header would repeat it.
      spaceName: '',
      accent: payload.accent || null,
      onPreview: (next) => send('settingsThemePreview', [spaceId, next]),
      onCommit: (next) => send('settingsThemeChanged', [spaceId, next]),
      onRemove: () => send('settingsThemeChanged', [spaceId, null]),
    });
    return 'studio set';
  };

  send('themeStudioReady', []);
}
