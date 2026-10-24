// Copyright 2026 The Atrium Authors

import {REAL_TIMEOUT_CLOCK} from './clock.js';

// Ported from the lab's flyRow/flySnooze (swipe-lab.html:2247-2273). CSS
// keyframes plus animationend, not Element.animate() - dom-shim has no Web
// Animations API.

const STAGGER_MS = 30;
const STAGGER_CAP_MS = 150;

function flightTarget(host) {
  return host.querySelector('.sdot.ovdot') || host.querySelector('#spaceDots');
}

// Flies a clone of `row`. The real row is never touched here, so the
// sidebar's own data-driven rebuild is still what removes it.
export function flyRowToOverview(row, opts = {}) {
  const {host = document, reducedMotion, clock = REAL_TIMEOUT_CLOCK, delayMs = 0} = opts;
  if (!row || !row.isConnected) {
    return;
  }
  const reduced = reducedMotion !== undefined
      ? reducedMotion
      : !!globalThis.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches;
  if (reduced) {
    return;
  }

  clock.set(delayMs, () => {
    if (!row.isConnected) {
      return;
    }
    // No Overview dot (e.g. Overview was never opened) leaves nothing real to
    // aim at, so the clone shrinks toward its own center instead of a point
    // off in space.
    const target = flightTarget(host);
    const r0 = row.getBoundingClientRect();
    const r1 = target ? target.getBoundingClientRect() : r0;
    const dx = (r1.left + r1.width / 2) - (r0.left + r0.width / 2);
    const dy = (r1.top + r1.height / 2) - (r0.top + r0.height / 2);

    const clone = row.cloneNode(true);
    clone.querySelectorAll('.x,.us').forEach((b) => b.remove());
    clone.className = 'archivefly';
    Object.assign(clone.style, {
      position: 'fixed',
      left: r0.left + 'px',
      top: r0.top + 'px',
      width: r0.width + 'px',
      height: r0.height + 'px',
    });
    clone.style.setProperty('--dx', dx + 'px');
    clone.style.setProperty('--dy', dy + 'px');
    document.body.appendChild(clone);
    clone.addEventListener('animationend', (e) => {
      if (e.target === clone) {
        clone.remove();
      }
    });
  });
}

// A batch staggers each flight 30ms behind the last, capped at 150ms - the
// lab's "Archive Tabs Below" batching (swipe-lab.html:4504-4507).
export function flyRowsToOverview(rows, opts = {}) {
  rows.forEach((row, ix) => {
    flyRowToOverview(row, {...opts, delayMs: Math.min(ix * STAGGER_MS, STAGGER_CAP_MS)});
  });
}
