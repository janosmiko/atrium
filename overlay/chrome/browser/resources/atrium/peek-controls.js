// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

import {icon} from './icons.js';

function button(kind, label, iconName) {
  const el = document.createElement('button');
  el.className = `pkc-${kind} ov-btn`;
  el.setAttribute('aria-label', label);
  el.appendChild(icon(iconName));
  el.addEventListener('click', () => chrome.send('peekControl', [kind]));
  return el;
}

export function installPeekControls(root) {
  root.replaceChildren();

  const cluster = document.createElement('div');
  cluster.className = 'pkc';
  cluster.append(
      button('close', 'Close peek', 'close'),
      button('popout', 'Open as a tab', 'popout'));
  root.appendChild(cluster);

  chrome.send('peekControlsReady', []);
}
