// Copyright 2026 The Atrium Authors
//
// edgeGlow(), ported from docs/lab/swipe-lab.html's ambient
// edge signal (idea #23). Timings and geometry live in edge-glow.css; this
// file only owns the DOM lifecycle - one glow at a time, and self-removal.

import {REAL_TIMEOUT_CLOCK} from './clock.js';

// Matches wakering/efpulse/efsweepfade in edge-glow.css. Used only under
// reduced motion, where CSS drops the animation and animationend would
// otherwise never fire, leaving the glow stuck.

const DURATION_MS = {ring: 2200, pulse: 4000, sweep: 8000};

/**
 * Lights the window frame in `color` for one of three modes: "ring",
 * "pulse", or "sweep". A second call replaces the first rather than
 * stacking, matching the lab.
 */
export function edgeGlow(color, mode, {host, reducedMotion, clock = REAL_TIMEOUT_CLOCK} = {}) {
  const win = host || document.getElementById('atrium-root');
  if (!win) {
    return null;
  }
  const outgoing = win.querySelector('.edgefx');
  if (outgoing) {
    if (outgoing._clockHandle !== undefined) {
      clock.clear(outgoing._clockHandle);
    }
    outgoing.remove();
  }

  const reduced = reducedMotion !== undefined
      ? reducedMotion
      : !!globalThis.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches;

  const el = document.createElement('div');
  el.className = `edgefx ${mode}`;
  el.style.setProperty('--efc', color);
  win.appendChild(el);

  if (reduced) {
    el._clockHandle = clock.set(DURATION_MS[mode] || 0, () => el.remove());
    return el;
  }
  el.addEventListener('animationend', (e) => {
    if (e.target === el) {
      el.remove();
    }
  });
  return el;
}
