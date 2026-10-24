// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

import {PHOSPHOR_SHAPES} from './phosphor/fill.js';

const NS = 'http://www.w3.org/2000/svg';

// Built without innerHTML - the WebUI CSP registers no Trusted Types policy.
// Falls back to the first letter, matching sidebar.js's own header glyph.
export function spaceGlyph(value, name) {
  const picked = typeof value === 'string' ? value : '';
  if (picked.startsWith('ph:')) {
    const iconName = picked.slice(3);
    if (Object.hasOwn(PHOSPHOR_SHAPES, iconName)) {
      return phosphorNode(iconName);
    }
    return firstLetterNode(name);
  }
  return picked ? document.createTextNode(picked) : firstLetterNode(name);
}

function phosphorNode(name) {
  const svg = document.createElementNS(NS, 'svg');
  svg.setAttribute('viewBox', '0 0 256 256');
  svg.setAttribute('fill', 'currentColor');
  svg.setAttribute('aria-hidden', 'true');
  svg.dataset.icon = name;
  for (const [tag, attrs] of PHOSPHOR_SHAPES[name]) {
    const node = document.createElementNS(NS, tag);
    for (const [key, val] of Object.entries(attrs)) {
      node.setAttribute(key, val);
    }
    svg.appendChild(node);
  }
  return svg;
}

function firstLetterNode(name) {
  return document.createTextNode((name || '')[0] || '');
}
