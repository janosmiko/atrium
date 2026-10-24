// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The /quickmenu route: share, the site row, and every extension as a tile.
//
// Every DOM write uses createElement/textContent, never innerHTML - the WebUI
// CSP sets require-trusted-types-for 'script' with an EMPTY allowlist, and an
// extension name is attacker-influenced text rendered at browser privilege.

import {extIconPath, setIconScope} from './icon-scope.js';
import {screenRectOf} from './sidebar-ext-shared.js';

function buildShareRow(send) {
  const row = document.createElement('div');
  row.className = 'qm-share';

  const share = document.createElement('button');
  share.className = 'qm-share-btn ov-row';
  share.type = 'button';
  share.textContent = 'Share';
  // The shim's click() does not model the native no-op on a disabled
  // control, so the guard is explicit here rather than left to the DOM.
  share.addEventListener('click', () => {
    if (!share.disabled) {
      send('quickMenuShare', []);
    }
  });

  const copy = document.createElement('button');
  copy.className = 'qm-copy-btn ov-row';
  copy.type = 'button';
  copy.textContent = 'Copy link';
  copy.addEventListener('click', () => {
    if (!copy.disabled) {
      send('quickMenuCopy', []);
    }
  });

  row.append(share, copy);
  return row;
}

const SVG_NS = 'http://www.w3.org/2000/svg';

// How long a pointer must rest on a tile before it counts as the pin toggle.
const HOLD_MS = 500;

// The same two glyphs the floating address bar draws, inline rather than
// imported: devbar.js is a whole route, and this is nine lines of path data.
function buildLockGlyph(broken) {
  const svg = document.createElementNS(SVG_NS, 'svg');
  svg.setAttribute('viewBox', '0 0 12 12');
  svg.setAttribute('width', '12');
  svg.setAttribute('height', '12');
  svg.setAttribute('aria-hidden', 'true');
  const body = document.createElementNS(SVG_NS, 'rect');
  body.setAttribute('x', '2.5');
  body.setAttribute('y', '5');
  body.setAttribute('width', '7');
  body.setAttribute('height', '5');
  body.setAttribute('rx', '1.2');
  body.setAttribute('fill', 'none');
  body.setAttribute('stroke', 'currentColor');
  body.setAttribute('stroke-width', '1.2');
  svg.appendChild(body);
  // Shut: one shackle. Broken: the same shackle snapped on the right.
  const paths = broken ? ['M4 5V3.8a2 2 0 0 1 2-2', 'M8 5V4.3']
                       : ['M4 5V3.8a2 2 0 0 1 4 0V5'];
  for (const d of paths) {
    const path = document.createElementNS(SVG_NS, 'path');
    path.setAttribute('d', d);
    path.setAttribute('fill', 'none');
    path.setAttribute('stroke', 'currentColor');
    path.setAttribute('stroke-width', '1.2');
    path.setAttribute('stroke-linecap', 'round');
    svg.appendChild(path);
  }
  return svg;
}

function buildSiteRow(send) {
  const row = document.createElement('button');
  row.className = 'qm-site ov-row';
  row.type = 'button';

  const glyph = document.createElement('span');
  glyph.className = 'qm-site-glyph';
  glyph.appendChild(buildLockGlyph(false));

  const text = document.createElement('span');
  text.className = 'qm-site-text';
  const host = document.createElement('span');
  host.className = 'qm-site-host';
  const state = document.createElement('span');
  state.className = 'qm-site-state';
  text.append(host, state);

  row.append(glyph, text);
  row.addEventListener('click', () => send('quickMenuSiteInfo', []));
  return row;
}

// Tracks every tile's pending long-press timer so a grid rebuild can cancel
// them all - Blink does not synthesize pointercancel on script-driven DOM
// removal, so a bare per-tile clear leaves a detached tile's timer running.
const pendingHoldTimers = new Set();

function buildTile(action, send) {
  const tile = document.createElement('button');
  tile.className = 'qm-tile';
  tile.type = 'button';
  tile.dataset.extensionId = action.id;
  const name = action.actionTitle || action.name || action.id;
  tile.title = action.pinned ? name + ' - pinned' : name;
  if (action.pinned) {
    tile.classList.add('qm-pinned');
  }

  const icon = document.createElement('img');
  // Served by the browser process from bytes it already holds. A network
  // fetch here would run on the chrome context's profile.
  icon.src = extIconPath(action.id, action.iconRev, action.iconScope);
  icon.alt = '';
  icon.className = 'qm-tile-icon';
  tile.appendChild(icon);

  // A long press is the pointer's way to the same toggle, for a trackpad
  // user with no secondary click. The timer is cleared on any release or
  // leave, and `held` stops the click that follows from running the action.
  let holdTimer = null;
  let held = false;
  const cancelHold = () => {
    clearTimeout(holdTimer);
    pendingHoldTimers.delete(holdTimer);
    holdTimer = null;
  };
  tile.addEventListener('pointerdown', () => {
    held = false;
    holdTimer = setTimeout(() => {
      held = true;
      pendingHoldTimers.delete(holdTimer);
      send('extensionPinToggled', [action.id]);
    }, HOLD_MS);
    pendingHoldTimers.add(holdTimer);
  });
  for (const eventName of ['pointerup', 'pointerleave', 'pointercancel']) {
    tile.addEventListener(eventName, cancelHold);
  }

  tile.addEventListener('click', () => {
    if (held) {
      held = false;
      return;
    }
    send('pillActionClicked', [action.id, screenRectOf(tile)]);
  });
  // A touch long-press fires its own contextmenu after the hold timer - guard
  // it the same way click is guarded, or the pin toggle also opens the menu.
  tile.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    if (held) {
      held = false;
      return;
    }
    send('extensionChipContextMenu', [action.id, screenRectOf(tile)]);
  });
  return tile;
}

export function installQuickMenu(root) {
  const send = (name, args) => chrome.send(name, args);

  const card = document.createElement('div');
  card.className = 'qm-card ov-card';

  const shareRow = buildShareRow(send);
  const siteRow = buildSiteRow(send);

  const grid = document.createElement('div');
  grid.className = 'qm-grid';

  const foot = document.createElement('div');
  foot.className = 'qm-foot';
  const manage = document.createElement('button');
  manage.className = 'qm-manage';
  manage.type = 'button';
  manage.textContent = 'Manage';
  manage.addEventListener('click', () => send('quickMenuManage', []));
  foot.appendChild(manage);

  card.append(shareRow, siteRow, grid, foot);
  root.appendChild(card);

  window.atriumSetQuickMenuIconScope = (token) => {
    setIconScope(token);
  };

  window.atriumSetQuickMenuActions = (actions) => {
    const list = actions || [];
    for (const timer of pendingHoldTimers) {
      clearTimeout(timer);
    }
    pendingHoldTimers.clear();
    grid.replaceChildren();
    for (const action of list) {
      grid.appendChild(buildTile(action, send));
    }
    // Only offered when there is nothing to manage yet - a user with
    // extensions reaches the store through Manage.
    const existing = foot.querySelector('.qm-store');
    if (list.length) {
      existing?.remove();
      return;
    }
    if (!existing) {
      // A button, not a link: this widget is no WebContents delegate for new
      // windows, so a target=_blank navigation from here goes nowhere.
      const store = document.createElement('button');
      store.className = 'qm-store';
      store.type = 'button';
      store.textContent = 'Get extensions';
      store.addEventListener('click', () => send('quickMenuStore', []));
      foot.appendChild(store);
    }
  };

  window.atriumSetQuickMenuSite = (site) => {
    const s = site || {};
    siteRow.querySelector('.qm-site-glyph')
        .replaceChildren(buildLockGlyph(!s.secure));
    siteRow.querySelector('.qm-site-host').textContent = s.host || '';
    siteRow.querySelector('.qm-site-state').textContent =
        s.secure ? 'Secure' : 'Not secure';
    siteRow.classList.toggle('qm-insecure', !s.secure);
    // A chrome:// or file:// page has nothing the share sheet can take.
    shareRow.querySelector('.qm-share-btn').disabled = !s.shareable;
    shareRow.querySelector('.qm-copy-btn').disabled = !s.shareable;
  };

  chrome.send('quickMenuReady', []);
  return card;
}
