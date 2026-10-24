// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// A pin or an unpin moves a row between the pinned section and the tab list,
// and renderSidebar rebuilds the whole tree - so the row that lands is a NEW
// element in a new slot, with nothing to animate away from.

// FLIP closes that gap: measure where each tab id sat before the rebuild, put
// the row that lands back there with a transform, then release it on the next
// frame and let CSS carry it home.

// Only a move ACROSS the pinned boundary starts a glide. A title arriving or a
// space switch rebuilds rows too, and animating those would make the list
// twitch on traffic nobody asked for.

import {REAL_TIMEOUT_CLOCK} from './clock.js';

const PINNED = 'pinned';
const TABS = 'tabs';

// Longer than the .2s the stylesheet gives the glide, so the fallback only
// ever fires for a transition that never ran.
const RELEASE_FALLBACK_MS = 300;

// Held at the old slot, transition off.
export const FLIP_HOLD_CLASS = 'rowflip';

// Released toward the new slot, transition on.
export const FLIP_RUN_CLASS = 'rowflipgo';

function sectionOf(row) {
  return row.closest('[data-dropzone]')?.dataset?.dropzone || '';
}

// Which section each tab id sits in, the rail's panes included - a pane holds
// its own lists, so a snapshot's top-level pair is only the active space.
function sectionsById(snapshot) {
  const sections = new Map();
  const add = (tabs, section) => {
    for (const tab of tabs || []) {
      if (tab.id && !sections.has(tab.id)) {
        sections.set(tab.id, section);
      }
    }
  };
  for (const scope of [snapshot, ...((snapshot && snapshot.spaces) || [])]) {
    add(scope && scope.pinned, PINNED);
    add(scope && scope.tabs, TABS);
    // A folder's rows count as loose ones: Cmd+D pins straight out of a
    // folder, and calling them TABS leaves the pinned line as the only
    // boundary a crossing can be measured against.
    for (const folder of (scope && scope.folders) || []) {
      add(folder && folder.tabs, TABS);
    }
  }
  return sections;
}

// Measuring every row costs a layout flush, and most pushes move nothing - a
// title, a favicon, a space switch that replaces every row on screen. Only a
// tab id that CHANGED section can produce a glide.
export function crossesPinnedLine(before, after) {
  if (!before) {
    return false;
  }
  const was = sectionsById(before);
  for (const [id, section] of sectionsById(after)) {
    if (was.has(id) && was.get(id) !== section) {
      return true;
    }
  }
  return false;
}

// Where every live row sits now, keyed by tab id. A pinned row with no tab has
// an empty id and cannot be matched across a rebuild, so it gets no slot. On a
// duplicated id the first row wins - it is the one the eye was tracking.
export function captureRowSlots(root) {
  const slots = new Map();
  for (const row of root.querySelectorAll('.row.tabrow')) {
    const id = row.dataset.tabId;
    if (!id || slots.has(id)) {
      continue;
    }
    const rect = row.getBoundingClientRect();
    slots.set(id, {left: rect.left, top: rect.top, section: sectionOf(row)});
  }
  return slots;
}

// Puts the rebuilt rows back where `slots` had them and releases them. Returns
// how many glided, so a caller can tell a skipped render from a still one.
export function playRowFlip(root, slots, options = {}) {
  const {reducedMotion, clock = REAL_TIMEOUT_CLOCK,
         requestFrame = (fn) => globalThis.requestAnimationFrame(fn)} = options;
  if (!slots || !slots.size) {
    return 0;
  }

  const moves = [];
  let crossed = false;
  for (const row of root.querySelectorAll('.row.tabrow')) {
    const was = slots.get(row.dataset.tabId);
    if (!was) {
      continue;
    }
    const section = sectionOf(row);
    if (section !== was.section && (section === PINNED || was.section === PINNED)) {
      crossed = true;
    }
    const rect = row.getBoundingClientRect();
    const dx = was.left - rect.left;
    const dy = was.top - rect.top;
    if (dx || dy) {
      moves.push({row, dx, dy});
    }
  }
  if (!crossed || !moves.length) {
    return 0;
  }

  const reduced = reducedMotion !== undefined
      ? reducedMotion
      : !!globalThis.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches;
  if (reduced) {
    return 0;
  }

  for (const {row, dx, dy} of moves) {
    row.classList.add(FLIP_HOLD_CLASS);
    row.style.transform = `translate(${dx}px, ${dy}px)`;
  }
  // The hold has to become a COMPUTED style before it is cleared, and reading
  // a rect back is what forces that. rAF runs ahead of the frame's own style
  // pass, so without this the row starts from none and snaps into place.
  for (const {row} of moves) {
    row.getBoundingClientRect();
  }

  // The run class and the cleared transform land in the SAME frame: the
  // transition the browser reads is the new one, so the row animates home
  // instead of jumping there.
  requestFrame(() => {
    for (const {row} of moves) {
      row.classList.remove(FLIP_HOLD_CLASS);
      row.classList.add(FLIP_RUN_CLASS);
      row.style.transform = '';
      releaseOnEnd(row, clock);
    }
  });
  return moves.length;
}

// Three ways out, because a row left in the run class is a row drawn away from
// the slot every click is aimed at: the transition ends, it is cancelled, or
// it never started and the timer says so.
function releaseOnEnd(row, clock) {
  let timer = null;
  const drop = () => {
    row.removeEventListener('transitionend', done);
    row.removeEventListener('transitioncancel', done);
    clock.clear(timer);
    row.classList.remove(FLIP_RUN_CLASS);
    row.style.transform = '';
  };
  // A row's own close button transitions its opacity and that bubbles, so the
  // listener has to name both the row and the property.
  const done = (event) => {
    if (event.target === row && event.propertyName === 'transform') {
      drop();
    }
  };
  row.addEventListener('transitionend', done);
  row.addEventListener('transitioncancel', done);
  timer = clock.set(RELEASE_FALLBACK_MS, drop);
}
