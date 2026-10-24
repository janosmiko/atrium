// Copyright 2026 The Atrium Authors
//
// binds the vertical rubber band to a scroll zone.
//
// Split from overscroll.js for the reason the swipe machine is split from its
// controller: the model can then be driven headlessly, and this file stays
// small enough to read in one go.
//
// Two constraints from the task, both load-bearing:
//
//   The band paints with transform, never scrollTop. A transform is
//   compositor-only, so the rows do not relayout, and it leaves installSidebar's
//   html/body overflow:hidden intact - a scrollTop-based band would not.
//
//   It must not fight the swipe. The rail's own wheel listener already returns
//   early on vertical intent (swipe.js), and this only ever claims a vertical
//   delta, so the two never want the same event.

import {createOverscrollModel} from './overscroll.js';
import {REAL_TIMEOUT_CLOCK} from './clock.js';

// The wheel silence that means the fingers left the trackpad, in ms. Same
// meaning as the rail's wheelEndGap; stated here in the units a timer wants.
const WHEEL_END_MS = 90;

/**
 * Installs the band on `zone`. Returns a teardown that removes the listener,
 * cancels any pending timer and clears the transform - a scroll zone is rebuilt
 * whenever the snapshot changes, and a band left running would keep a stale
 * element alive and animating.
 */
export function installOverscroll(
    zone, {reducedMotion, requestFrame, clock = REAL_TIMEOUT_CLOCK} = {}) {
  if (!zone) {
    return () => {};
  }
  const raf = requestFrame ||
      ((cb) => (globalThis.requestAnimationFrame || setTimeout)(cb, 16));
  const reduced = reducedMotion !== undefined
      ? reducedMotion
      : !!globalThis.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches;
  const model = createOverscrollModel({reducedMotion: reduced});

  let endTimer = null;
  let running = false;
  let prev = 0;
  // Set once a momentum event proves the fingers lifted mid-drag, so the rest
  // of that tail is not read as a second pull.
  let swallowingTail = false;

  // The CONTENT moves, never the zone. The zone is the viewport, and a
  // translate on a viewport carries its clip box along - which slides the whole
  // list down over the space dots instead of revealing space above it. Falls
  // back to the zone only so a caller passing a bare element still gets
  // something rather than silently nothing.
  const surface = () => zone.querySelector?.('.scrollinner') || zone;

  const paint = () => {
    surface().style.transform =
        model.offset ? `translateY(${model.offset.toFixed(2)}px)` : '';
  };

  const frame = (t) => {
    const dt = Math.min(0.05, Math.max(0, (t - prev) / 1000)) || 1 / 60;
    prev = t;
    model.step(dt);
    paint();
    if (model.offset !== 0 || model.dragging) {
      raf(frame);
    } else {
      running = false;
    }
  };

  const start = () => {
    if (running) {
      return;
    }
    running = true;
    prev = (globalThis.performance?.now?.() ?? 0);
    raf(frame);
  };

  const onWheel = (e) => {
    // Horizontal intent belongs to the rail. Checked before anything else so a
    // swipe is never even considered here.
    if (Math.abs(e.deltaY) <= Math.abs(e.deltaX) * 1.2) {
      return;
    }
    // e.momentum (swipe.js) cannot be true while a finger is down,
    // so it proves the lift mid-drag instead of waiting on silence.
    const isMomentum = typeof e.momentum === 'boolean' ? e.momentum : undefined;
    if (isMomentum === false) {
      swallowingTail = false;
    } else if (isMomentum === true) {
      if (model.dragging) {
        if (endTimer !== null) {
          clock.clear(endTimer);
          endTimer = null;
        }
        model.end();
        start();
        swallowingTail = true;
      }
      if (swallowingTail) {
        e.preventDefault();
        return;
      }
    }
    const geom = {
      scrollTop: zone.scrollTop,
      scrollHeight: zone.scrollHeight,
      clientHeight: zone.clientHeight,
    };
    // Sign-flipped to match the rail's convention, so both surfaces describe a
    // gesture the same way.
    if (!model.delta(-e.deltaY, 0, geom)) {
      // Scrolled here, not left native: with a non-passive listener on the
      // zone, whether a phased gesture natively scrolls is a per-gesture race.
      // Clamped so the hand-off to the band sees a true edge.
      const max = Math.max(0, geom.scrollHeight - geom.clientHeight);
      zone.scrollTop = Math.max(0, Math.min(max, zone.scrollTop + e.deltaY));
      e.preventDefault();
      return;
    }
    e.preventDefault();
    paint();
    if (endTimer !== null) {
      clock.clear(endTimer);
    }
    // A wheel gesture has no end event, and this is the fallback for when
    // the browser reports no momentum phase at all (a mouse, or a synthetic
    // event) - silence is the only signal left.
    endTimer = clock.set(WHEEL_END_MS, () => {
      endTimer = null;
      model.end();
      start();
    });
  };

  zone.addEventListener('wheel', onWheel, {passive: false});

  return () => {
    zone.removeEventListener('wheel', onWheel);
    if (endTimer !== null) {
      clock.clear(endTimer);
      endTimer = null;
    }
    running = false;
    surface().style.transform = '';
  };
}
