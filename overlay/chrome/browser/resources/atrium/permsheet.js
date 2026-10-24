// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// The /permsheet route: the glass sheet a permission request
// shows over the web content. No innerHTML - the WebUI CSP registers no
// Trusted Types policy and the origin string is attacker-controlled.

export function installPermissionSheet(root) {
  root.replaceChildren();

  const scrim = document.createElement('div');
  scrim.className = 'psscrim ov-scrim';

  const card = document.createElement('div');
  card.className = 'pscard ov-card';

  const origin = document.createElement('div');
  origin.className = 'psorigin';

  const list = document.createElement('div');
  list.className = 'pslist';

  const actions = document.createElement('div');
  actions.className = 'psactions';

  const block = document.createElement('button');
  block.className = 'psbtn psblock ov-button';
  block.textContent = 'Block';
  block.addEventListener('click', () => chrome.send('permissionSheetBlock', []));

  const allow = document.createElement('button');
  allow.className = 'psbtn psallow ov-button ov-primary';
  allow.textContent = 'Allow';
  allow.addEventListener('click', () => chrome.send('permissionSheetAllow', []));

  actions.append(block, allow);
  card.append(origin, list, actions);
  scrim.appendChild(card);
  root.appendChild(scrim);

  // Escape blocks. Enter does NOT grant, and that asymmetry is deliberate
  //: the sheet takes keyboard focus the moment it appears, so a page
  // that tells the user to hold Enter and then calls getUserMedia had the
  // autorepeat land on a freshly focused sheet and grant camera and microphone
  // with no decision made. Chromium's own bubble carries an
  // InputEventActivationProtector against exactly that; this sheet had neither
  // that nor a reason to bind Enter.
  //
  // Blocking stays on the keyboard because it is the safe direction: the worst a
  // stray Escape does is deny a permission the user can ask for again.
  document.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') {
      e.preventDefault();
      chrome.send('permissionSheetBlock', []);
    }
  });

  window.atriumSetPermissionSheet = (request) => {
    origin.textContent = (request && request.origin) || '';
    list.replaceChildren();
    const permissions = (request && request.permissions) || [];
    for (const text of permissions) {
      const row = document.createElement('div');
      row.textContent = text;
      list.appendChild(row);
    }
  };

  chrome.send('permissionSheetReady', []);
}
