// Copyright 2026 The Atrium Authors
// Split out so sidebar-ext-install.js and sidebar-ext-strip.js can share these
// without importing each other.

import {extIconPath} from './icon-scope.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

export function buildIcon(action) {
  const icon = document.createElement('img');
  icon.src = extIconPath(action.id, action.iconRev, action.iconScope);
  icon.alt = '';
  icon.className = 'crx-icon';
  return icon;
}

// The one puzzle glyph both address fields use, so the two cannot drift.
function buildPuzzleGlyph() {
  const svg = document.createElementNS(SVG_NS, 'svg');
  svg.setAttribute('viewBox', '0 0 16 16');
  svg.setAttribute('width', '16');
  svg.setAttribute('height', '16');
  svg.setAttribute('aria-hidden', 'true');
  const body = document.createElementNS(SVG_NS, 'path');
  body.setAttribute(
      'd',
      'M6.4 2.2a1.6 1.6 0 0 1 3.2 0v.9h2.3c.5 0 .9.4.9.9v2.3h-.9a1.6 1.6 0 0 0 ' +
          '0 3.2h.9v2.3c0 .5-.4.9-.9.9H9.6v-.9a1.6 1.6 0 0 0-3.2 0v.9H4.1a.9.9 ' +
          '0 0 1-.9-.9V9.5h.9a1.6 1.6 0 0 0 0-3.2h-.9V4a.9.9 0 0 1 .9-.9h2.3z');
  body.setAttribute('fill', 'none');
  body.setAttribute('stroke', 'currentColor');
  body.setAttribute('stroke-width', '1.2');
  body.setAttribute('stroke-linejoin', 'round');
  svg.appendChild(body);
  return svg;
}

// An element's box in SCREEN coordinates, or null when it has no measurable
// one. Screen, so the window can place an extension popup's anchor without
// knowing which surface sent it.
export function screenRectOf(el) {
  const r = el?.getBoundingClientRect?.();
  const parts = [r?.left + window.screenX, r?.top + window.screenY,
                 r?.width, r?.height];
  return parts.every(Number.isFinite) ? parts.map(Math.round) : null;
}

// `send` takes (name, args) so the sidebar and the floating bar can each
// route through their own sender.
export function buildQuickMenuButton(send) {
  const button = document.createElement('button');
  button.className = 'qm-open';
  button.type = 'button';
  button.title = 'Extensions and site';
  button.setAttribute('aria-label', button.title);
  button.appendChild(buildPuzzleGlyph());
  button.addEventListener('click', () => {
    const box = button.getBoundingClientRect();
    send('quickMenuToggled',
         [Math.round(box.left), Math.round(box.top),
          Math.round(box.width), Math.round(box.height)]);
  });
  return button;
}
