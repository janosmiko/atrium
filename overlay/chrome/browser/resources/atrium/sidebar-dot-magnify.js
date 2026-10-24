// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Ported from docs/lab/swipe-lab.html:4668-4713.

import {spaceGlyph} from './space-glyph.js';

// How much closer the cursor must get to the next dot before the chip moves.
// Without it the pick flips on every sub-pixel move at the midpoint.
export const HYSTERESIS_PX = 5;

/** Index of the dot centre nearest `x`, or null. `current` keeps its pick
    until another dot is HYSTERESIS_PX closer. */
export function nearestDot(x, centres, current = null) {
  if (!centres.length) {
    return null;
  }
  let best = 0;
  let bestDistance = Infinity;
  centres.forEach((centre, i) => {
    const distance = Math.abs(x - centre);
    if (distance < bestDistance) {
      bestDistance = distance;
      best = i;
    }
  });
  const held = Number.isInteger(current) && current >= 0 &&
      current < centres.length;
  if (held && best !== current &&
      Math.abs(x - centres[current]) - bestDistance < HYSTERESIS_PX) {
    return current;
  }
  return best;
}

// The last cursor position, for re-deriving the chip after a rebuild: fresh
// elements carry no hover state. One listener for the document, not one per
// bar - the bar is replaced on every snapshot.
let pointer = {x: null, y: null};
let tracking = false;

function trackPointer() {
  if (tracking || !document?.addEventListener) {
    return;
  }
  tracking = true;
  document.addEventListener('mousemove', (e) => {
    pointer = {x: e.clientX, y: e.clientY};
  }, true);
}

function reducedMotion() {
  return !!globalThis.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches;
}

// The dots, not every child: placeDotBloom appends .dotbloom to this same bar
//. It has no .d glyph, so a pick that landed on it threw and left
// the bar dead until the cursor left.
function dotsOf(bar) {
  return [...bar.querySelectorAll('.sdot')];
}

// offsetLeft, not a rect: the shove moves the dots and a measured centre
// would chase the chip. Overview and + sit outside the scroller,
// so each dot needs its own origin.
function originOf(bar, dot) {
  const scroller = dot.closest('.dotscroll') || bar;
  return scroller.getBoundingClientRect().left - scroller.scrollLeft;
}

// .sdot.mag .d grows to 20px in a 12px slot - 4px either side of centre a
// magnified dot needs clear of the scroller's own clip to not overhang it.
const MAG_OVERHANG_PX = 4;

// A dot within MAG_OVERHANG_PX of the scroller's edge keeps a real offsetLeft
// (so its centre could still win the pick), but magnifying it
// would clip the chip or push it under the z-index:1 Overview/+ button.
function centresOf(bar) {
  return dotsOf(bar).map((dot) => {
    const centre = originOf(bar, dot) + dot.offsetLeft + dot.offsetWidth / 2;
    const scroller = dot.closest('.dotscroll');
    if (!scroller) {
      return centre;
    }
    const {left, right} = scroller.getBoundingClientRect();
    return centre < left + MAG_OVERHANG_PX || centre > right - MAG_OVERHANG_PX
        ? Infinity : centre;
  });
}

// Half the dots' 20px pitch: a click past this from every centre is padding
// or a flex gap, not a miss on the dot the shift moved.
const DOT_HIT_RADIUS_PX = 10;

// A click can land mid-glide of the bloom's sibling-shift transform, missing
// the painted box even though centresOf's layout position is dead on target.
export function dotAt(bar, x) {
  const dots = dotsOf(bar);
  const centres = centresOf(bar);
  const index = nearestDot(x, centres);
  if (index === null || Math.abs(x - centres[index]) > DOT_HIT_RADIUS_PX) {
    return null;
  }
  return dots[index];
}

/** Magnifies the dot nearest the cursor in `bar`. Returns the refresh a
    rebuild must call, since fresh elements carry no hover. */
export function installDotMagnify(bar) {
  trackPointer();
  let magnified = null;

  function paint(index, animate) {
    if (index === magnified) {
      return;
    }
    const dots = dotsOf(bar);
    const previous = magnified === null ? null : dots[magnified];
    if (previous) {
      previous.classList.remove('mag');
      const glyph = previous.firstChild;
      glyph.style.background = '';
      // The active dot is a bare inked glyph, not a chip.
      if (previous.classList.contains('active')) {
        glyph.style.color = previous.dataset.ink || '';
      } else if (!previous.classList.contains('ovdot') &&
                 !previous.classList.contains('addws')) {
        // Overview and + are permanent controls, not per-space dots, and
        // keep their glyph even when nothing is hovering them.
        glyph.textContent = '';
      }
    }
    magnified = index;
    const dot = index === null ? null : dots[index];
    if (!dot) {
      return;
    }
    dot.classList.add('mag');
    const glyph = dot.firstChild;
    glyph.replaceChildren(dot.dataset.spaceIcon === undefined
        ? document.createTextNode(dot.dataset.init || '')
        : spaceGlyph(dot.dataset.spaceIcon, dot.dataset.spaceName || ''));
    glyph.style.color = '';
    if (dot.dataset.tint) {
      glyph.style.background = dot.dataset.tint;
    }
    if (animate && !reducedMotion()) {
      glyph.style.animation = 'none';
      // Reading the box commits the reset, or the same animation never
      // restarts on a second dot.
      void glyph.offsetWidth;
      glyph.style.animation = 'dotpop .3s cubic-bezier(.34, 1.56, .64, 1)';
      glyph.addEventListener('animationend', () => {
        glyph.style.animation = '';
      }, {once: true});
    }
  }

  function pickAt(x, animate) {
    paint(nearestDot(x, centresOf(bar), magnified), animate);
  }

  bar.addEventListener('mousemove', (e) => {
    pointer = {x: e.clientX, y: e.clientY};
    pickAt(e.clientX, true);
  });
  bar.addEventListener('mouseleave', () => {
    paint(null, false);
  });
  // Silent: a scrollIntoView after a far jump slides many dots under a still
  // cursor, and popping each one reads as a stutter.
  // Capture: the scroll fires on .dotscroll and does not bubble.
  bar.addEventListener('scroll', () => {
    if (pointer.x !== null) {
      pickAt(pointer.x, false);
    }
  }, true);

  return function refresh() {
    magnified = null;
    if (pointer.x === null) {
      return;
    }
    const under = document.elementFromPoint?.(pointer.x, pointer.y);
    // Only while the cursor is over this bar: a stale position over the rows
    // above would magnify the dot nearest that column.
    if (under && bar.contains(under)) {
      pickAt(pointer.x, false);
    }
  };
}
