// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//
// the Overview's Spaces section. One card per space, tinted
// with the space's own colour, jumping to it on click - the lab's ovspaces
// (swipe-lab.html:3003-3007).
//
// Cards rather than the row list every other section here uses: a space has no
// second line to caption, and the lab makes the tint the whole point of the
// card, which a 30px row cannot carry.

export function renderSpacesSection() {
  const wrap = document.createElement('div');
  wrap.className = 'ovspaces';

  let spaces = [];
  // The card a drag started on, as a filtered position - the same numbering the
  // click sends, so the two cannot disagree about which room is which.
  let dragFrom = null;

  function buildCard(space, index) {
    const card = document.createElement('div');
    card.className = 'ovcard';
    card.dataset.spaceIndex = String(index);
    // The lab's own two-stop gradient, so a card reads as the space rather
    // than as a flat swatch.
    const tint = space.color || '#7f8f9f';
    card.style.background =
        `linear-gradient(160deg, ${tint}EE, ${tint}99)`;
    // textContent: a space name is user-entered.
    card.textContent = space.name || 'Space';
    // Drag to reorder. HTML5 drag rather than pointer maths: the cards
    // are a grid, so a drop lands on a sibling and that sibling's index IS the
    // destination - no geometry to compute.
    card.draggable = true;
    card.addEventListener('dragstart', (e) => {
      dragFrom = index;
      e.dataTransfer?.setData?.('text/plain', String(index));
    });
    card.addEventListener('dragover', (e) => e.preventDefault());
    card.addEventListener('drop', (e) => {
      e.preventDefault();
      if (dragFrom === null || dragFrom === index) {
        return;
      }
      chrome.send('sidebarSpaceReordered', [dragFrom, index]);
      dragFrom = null;
    });
    card.addEventListener('dragend', () => {
      dragFrom = null;
    });
    card.addEventListener('click', () => {
      // The same verb the sidebar's dots and the swipe both land on, so the
      // Overview cannot switch spaces by a route of its own.
      chrome.send('sidebarSpaceSelected', [index]);
    });
    return card;
  }

  function render() {
    wrap.replaceChildren();
    if (!spaces.length) {
      const empty = document.createElement('div');
      empty.className = 'ovempty';
      empty.textContent = 'No spaces yet.';
      wrap.appendChild(empty);
      return;
    }
    spaces.forEach((space, index) => wrap.appendChild(buildCard(space, index)));
  }

  // The Overview is its own WebContents, so it cannot read the sidebar's
  // snapshot - the browser pushes the same list here.
  window.atriumSetOverviewSpaces = (next) => {
    // The Overview space is not one of the rooms you can jump to, and the
    // model may or may not carry it depending on the arm - filtered either way.
    spaces = (Array.isArray(next) ? next : []).filter((s) => !s.overview);
    render();
  };

  render();
  chrome.send('spacesListReady', []);

  function dispose() {
    delete window.atriumSetOverviewSpaces;
  }

  return {root: wrap, dispose};
}
