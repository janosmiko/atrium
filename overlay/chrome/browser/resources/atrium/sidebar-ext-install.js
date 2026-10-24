// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Installs the pinned-extension strip beside #atrium-root. Inserted as the
// root's previous sibling, not a child: the tab list's replaceChildren()
// rebuilds must never touch it.

import {MAX_PINNED_TILES, renderExtStrip, stripRows} from './sidebar-ext-strip.js';

// Keyed by root so a second install on the same root (a second sidebarReady
// handshake) tears down its own prior container instead of orphaning it.
const installsByRoot = new WeakMap();

// One entry per profile that has pushed, so a rail pane of another profile can
// draw that profile's own pins the way it draws its own favourites. Bounded by
// the number of loaded profiles.
const actionsByProfile = new Map();
// Null until the first push or the first snapshot names one: an empty string is
// itself a profile id here, the one a window with no controller pushes under.
let activeProfileId = null;
// Set by the module that owns the strip element, so a getter called before the
// install (or after a route change) has somewhere to read from.
let redrawStrip = () => {};

// The pins a rail pane of `profileId` shows, or the ones on the strip itself
// when called with no id. Empty for a profile whose window has never pushed -
// it has no controller yet, so nothing knows its pins.
export function pinnedExtensionActions(profileId) {
  const id = profileId === undefined ? activeProfileId : profileId;
  return actionsByProfile.get(id) || [];
}

// Only the rebuild that stamps the pinned favourites grid moves the strip to
// another profile. SelectSpace pushes the DESTINATION profile's pins before the
// glide starts, while swipe.js still carries this row off as the outgoing one.
export function setActiveExtensionProfile(profileId) {
  if (profileId === activeProfileId) {
    return;
  }
  activeProfileId = profileId;
  redrawStrip();
}

export function installStripExtensions(root) {
  const prior = installsByRoot.get(root);
  if (prior) {
    prior.stripContainer.remove();
  }

  const stripContainer = document.createElement('div');
  stripContainer.className = 'exs-row exs-hidden';
  (root.parentNode || document.body).insertBefore(stripContainer, root);
  // A fresh handshake replays every profile it still holds, so keeping the old
  // entries would leave a profile the browser has since forgotten on screen.
  actionsByProfile.clear();
  activeProfileId = null;

  function update() {
    const shown = renderExtStrip(stripContainer, {
      actions: pinnedExtensionActions(activeProfileId),
      max: MAX_PINNED_TILES,
      onChipClick: (id, rect) => chrome.send('pillActionClicked', [id, rect]),
      onChipContextMenu: (id, rect) =>
          chrome.send('extensionChipContextMenu', [id, rect]),
    });
    stripContainer.dataset.profileId = activeProfileId ?? '';
    root.classList.toggle('exs-reflow', shown > 0);
    // The strip is the root's sibling, so the grid under the root cannot read
    // the row count off it.
    root.style.setProperty('--exs-rows', String(stripRows(shown)));
  }
  redrawStrip = update;

  // Pushed on the sidebarReady handshake and on every change - a push before
  // that handshake lands in a document with no listener yet. The replay sends
  // the active profile last, so the first push owns the row.
  // The snapshot goes out before this replay
  // (atrium_browser_window_surface_routes.cc:532). Swap those two and every
  // replayed push reads a null active profile.
  window.atriumSetSidebarExtensions = (a, profileId) => {
    const id = profileId === undefined || profileId === null
        ? (activeProfileId ?? '')
        : profileId;
    actionsByProfile.set(id, a || []);
    if (activeProfileId === null) {
      activeProfileId = id;
    }
    if (id === activeProfileId) {
      update();
    }
  };

  update();
  installsByRoot.set(root, {stripContainer});
  return stripContainer;
}
