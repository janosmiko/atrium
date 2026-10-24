// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// SVG marks for overlay rows and buttons. Built node by node: the WebUI CSP
// registers no Trusted Types policy, and a glyph font
// would drift between platforms.

import {FLOCK_ARMS, FLOCK_SPREAD, FLOCK_TONES, FLOCK_BIRDS} from './mark-flock.js';

const NS = 'http://www.w3.org/2000/svg';

// Each entry is a list of [tag, attributes]. 24x24 viewBox, stroked, no fill.
const SHAPES = {
  url: [['path', {d: 'M7 17L17 7M9 7h8v8'}]],
  search: [['circle', {cx: '11', cy: '11', r: '6'}],
           ['path', {d: 'M20 20l-4.5-4.5'}]],
  favorite: [['path', {d: 'M12 4l2.4 5 5.6.8-4 3.9.9 5.5-4.9-2.6-4.9 2.6.9-5.5-4-3.9 5.6-.8z'}]],
  action: [['path', {d: 'M13 3L5 14h6l-1 7 8-11h-6z'}]],
  tab: [['rect', {x: '3', y: '5', width: '18', height: '14', rx: '2'}],
        ['path', {d: 'M3 10h18'}]],
  space: [['rect', {x: '3', y: '3', width: '8', height: '8', rx: '1.5'}],
          ['rect', {x: '13', y: '13', width: '8', height: '8', rx: '1.5'}],
          ['path', {d: 'M13 7h4v4M11 17H7v-4'}]],
  close: [['path', {d: 'M6 6l12 12M18 6L6 18'}]],
  up: [['path', {d: 'M6 15l6-6 6 6'}]],
  down: [['path', {d: 'M6 9l6 6 6-6'}]],
  // The lab's confirmation tick (swipe-lab.html:4918), at this file's scale.
  check: [['path', {d: 'M5 13l4.5 4.5L19 7'}]],
  popout: [['path', {d: 'M9 3H3v6M15 21h6v-6M3 3l7 7M21 21l-7-7'}]],
  clock: [['circle', {cx: '12', cy: '12', r: '9'}],
          ['path', {d: 'M12 8v4l3 2'}]],
  pin: [['path', {d: 'M8.6 3h6.8l-.8 6.2 4 4.2H5.4l4-4.2z'}],
        ['path', {d: 'M12 13.8v7.2'}]],
  send: [['path', {d: 'M9 6l6 6-6 6'}]],
  summarize: [['path', {d: 'M5 7h14M5 12h14M5 17h8'}]],
  bullets: [['path', {d: 'M10 7h9M10 12h9M10 17h9'}],
            ['circle', {cx: '6', cy: '7', r: '1'}],
            ['circle', {cx: '6', cy: '12', r: '1'}],
            ['circle', {cx: '6', cy: '17', r: '1'}]],
  trash: [['path', {d: 'M5 7h14M10 7V5h4v2M7 7l1 12h8l1-12'}]],
  zoom: [['circle', {cx: '10', cy: '10', r: '7'}],
         ['path', {d: 'M10 7v6M7 10h6'}],
         ['path', {d: 'M15 15l6 6'}]],
};

export const ICON_NAMES = Object.keys(SHAPES);

export function icon(name) {
  const key = Object.hasOwn(SHAPES, name) ? name : 'url';
  const svg = document.createElementNS(NS, 'svg');
  svg.setAttribute('viewBox', '0 0 24 24');
  svg.setAttribute('aria-hidden', 'true');
  svg.classList.add('ov-icon');
  svg.dataset.icon = key;
  for (const [tag, attrs] of SHAPES[key]) {
    const node = document.createElementNS(NS, tag);
    for (const [k, v] of Object.entries(attrs)) {
      node.setAttribute(k, v);
    }
    svg.appendChild(node);
  }
  return svg;
}

// Geometry is generated, so the loader cannot drift from the app icons. A bird
// is shoved out while it dims, and one element cannot carry both animations.
export function markFlock() {
  const svg = document.createElementNS(NS, 'svg');
  svg.setAttribute('viewBox', '0 0 48 48');
  svg.setAttribute('aria-hidden', 'true');
  svg.classList.add('ov-flock');
  const spin = document.createElementNS(NS, 'g');
  spin.classList.add('ov-flock-spin');
  for (const [arm, band, cx, cy, rx, ry, deg, op] of FLOCK_BIRDS) {
    const dx = cx - 24;
    const dy = cy - 24;
    const len = Math.hypot(dx, dy);
    // A bird sitting on the centre has no radius to be shoved along, so it goes
    // straight up rather than staying behind while the rest of its arm leaves.
    const nx = len ? dx / len : 0;
    const ny = len ? dy / len : -1;
    const phase = -(arm * FLOCK_SPREAD + band * FLOCK_SPREAD / 8);
    const carrier = document.createElementNS(NS, 'g');
    carrier.classList.add('ov-flock-push');
    carrier.style.setProperty('--flock-phase', phase.toFixed(3));
    carrier.style.setProperty('--flock-nx', nx.toFixed(3));
    carrier.style.setProperty('--flock-ny', ny.toFixed(3));
    const bird = document.createElementNS(NS, 'ellipse');
    bird.setAttribute('cx', cx);
    bird.setAttribute('cy', cy);
    bird.setAttribute('rx', rx);
    bird.setAttribute('ry', ry);
    bird.setAttribute('transform', `rotate(${deg} ${cx} ${cy})`);
    bird.setAttribute('fill', FLOCK_TONES[arm % FLOCK_ARMS]);
    bird.setAttribute('opacity', op);
    bird.classList.add(`ov-flock-b${band}`);
    bird.style.setProperty('--flock-op', op);
    bird.style.setProperty('--flock-phase', phase.toFixed(3));
    carrier.appendChild(bird);
    spin.appendChild(carrier);
  }
  svg.appendChild(spin);
  return svg;
}
