// Copyright 2026 The Atrium Authors
//
// the tab list's vertical rubber band.
//
// The list only responded to a vertical gesture when its content overflowed.
// With few tabs, or none, it was inert - where every native macOS list still
// takes the gesture, moves with resistance and springs back. Native
// `overflow-y:auto` bounces on macOS ONLY when the content overflows, so this
// is new behaviour rather than something the lab already had.
//
// Holds no DOM, like the swipe machine and for the same reason: "does it spring
// back, and with what damping" is otherwise only answerable by feel, one
// gesture at a time (build/overscroll-test.mjs).
//
// The resistance curve and the spring constants come from swipe.js rather than
// being restated here. The rail and the list have to feel like one surface, and
// two independently tuned formulas cannot promise that - a change to
// springDamp must move both or neither.

import {DEFAULT_TUNING, resistPast} from './swipe.js';

export const DEFAULT_OVERSCROLL_TUNING = {
  // Travel in PIXELS, unlike the rail's cap which is in pane units. A list has
  // no natural unit to be a fraction of, and a hard pixel ceiling is what keeps
  // the band from swallowing the sidebar on a long pull.
  overscrollCap: 84,
  // How many pixels of input reach the asymptote's knee. Lower feels stiffer.
  // Not the rail's width, because a vertical pull on a short list has no
  // "distance to the next thing" to scale against.
  overscrollScale: 240,
  settleDur: DEFAULT_TUNING.settleDur,
  springDamp: DEFAULT_TUNING.springDamp,
};

/**
 * A vertical overscroll band for a scroll zone.
 *
 * `reducedMotion` skips the spring and returns to rest immediately, matching
 * what the lab does for its own springs under prefers-reduced-motion.
 */
export function createOverscrollModel({tuning, reducedMotion = false} = {}) {
  const tune = {...DEFAULT_OVERSCROLL_TUNING, ...(tuning || {})};

  // Signed input distance, before resistance. Kept separately from `offset`
  // because the curve is not invertible cheaply, and a reversal has to pay back
  // the input it banked rather than the travel it produced - the same reason
  // the rail keeps accumX apart from its displayed progress.
  let accum = 0;
  let vel = 0;

  const travelFor = (input) => {
    const sign = Math.sign(input);
    const scaled = Math.abs(input) / tune.overscrollScale;
    return sign * resistPast(scaled * tune.overscrollCap, tune.overscrollCap);
  };

  const m = {
    tuning: tune,
    reducedMotion,
    /** Current translateY in pixels. Positive pulls the content down. */
    offset: 0,
    /** True between the first taken delta and end(). */
    dragging: false,

    /**
     * A wheel delta, already sign-flipped by the caller like the rail's.
     * `geom` is the zone's own {scrollTop, scrollHeight, clientHeight}.
     * Returns whether the band took the gesture; false means let it through to
     * native scrolling untouched.
     */
    delta(dy, now, geom) {
      if (!dy) {
        return false;
      }
      const {scrollTop = 0, scrollHeight = 0, clientHeight = 0} = geom || {};
      const overflows = scrollHeight > clientHeight;
      // Pulling DOWN (dy > 0) is only ours at the top; pulling up, at the
      // bottom. Anywhere else native scrolling can still consume it, and taking
      // it there would replace ordinary scrolling with a rubber band.
      const atTop = scrollTop <= 0;
      const atBottom = scrollTop + clientHeight >= scrollHeight;
      if (overflows && accum === 0) {
        if (dy > 0 ? !atTop : !atBottom) {
          return false;
        }
      }
      // Unwinding past zero means the gesture is heading back into the list.
      // Held instead, every later delta took travelFor's damped curve, and a
      // trackpad flick never pauses long enough for end() to reset accum.
      if (accum !== 0 && Math.sign(dy) !== Math.sign(accum) &&
          Math.abs(dy) >= Math.abs(accum)) {
        accum = 0;
        this.offset = 0;
        this.dragging = false;
        vel = 0;
        return false;
      }
      accum += dy;
      this.offset = travelFor(accum);
      this.dragging = true;
      vel = 0;
      return true;
    },

    /** The gesture ended - the caller infers this from wheel silence. */
    end() {
      this.dragging = false;
    },

    /**
     * Advance the spring by `dt` seconds. No-op while dragging, so a gesture
     * that keeps arriving is never fought by the return.
     */
    step(dt) {
      if (this.dragging || this.offset === 0) {
        return this.offset;
      }
      if (this.reducedMotion) {
        accum = 0;
        vel = 0;
        this.offset = 0;
        return 0;
      }
      // Same critically damped form the rail settles with, so a change to
      // springDamp or settleDur moves both surfaces together.
      const dur = Math.max(0.08, tune.settleDur);
      const stiff = (2 * Math.PI / dur) ** 2;
      const damp = 2 * Math.sqrt(stiff) * tune.springDamp;
      // Substep for the same reason swipe.js does: a stiff spring integrated
      // explicitly explodes whenever a frame runs long.
      const steps = Math.max(1, Math.ceil(dt / 0.004));
      const h = dt / steps;
      for (let i = 0; i < steps; i++) {
        vel += (-stiff * this.offset - damp * vel) * h;
        this.offset += vel * h;
      }
      // Settle exactly, and re-seat the accumulator with it. Leaving accum
      // behind would make the next pull start from banked input and jump.
      if (!Number.isFinite(this.offset) ||
          (Math.abs(this.offset) < 0.5 && Math.abs(vel) < 12)) {
        this.offset = 0;
        accum = 0;
        vel = 0;
      } else {
        accum = this.offset === 0 ? 0 : inputFor(this.offset);
      }
      return this.offset;
    },
  };

  // The curve inverted, so a spring mid-return can hand the accumulator back a
  // value consistent with where the list actually is. Without it, a second pull
  // during the return resumes from the original input and jumps.
  function inputFor(travel) {
    const sign = Math.sign(travel);
    const t = Math.min(Math.abs(travel), tune.overscrollCap * 0.999);
    const scaled = tune.overscrollCap * (t / (tune.overscrollCap - t));
    return sign * scaled * tune.overscrollScale / tune.overscrollCap;
  }

  return m;
}
