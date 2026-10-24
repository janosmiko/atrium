// Copyright 2026 The Atrium Authors
//
// the workspace swipe, ported from docs/lab/swipe-lab.html.
//
// The lab is the design reference, not a starting point. Momentum, the commit
// threshold, the rubber band and the settle spring took 122+ design
// rounds there and 3-5 days of re-derivation on the SwiftUI side. This is a
// transcription of that machine, and where it deviates the deviation is
// commented.
//
// The machine holds no DOM. That is what lets build/swipe-machine-test.mjs
// answer "does this commit?" without a browser - the question that could only
// be answered by hand, one gesture at a time, on the SwiftUI arm.
//
// Deviations from the lab, all forced by this surface having only a trackpad:
//   - No `holding` state. The lab keeps one because it also supports a pointer
//     drag, where a finger may rest mid-gesture. A wheel gesture's end can
//     only ever be inferred from silence, and that inference fires at
//     wheelEndGap (0.09s), long before holdSilenceGap (0.5s) could - so the
//     state was unreachable.
//   - Panes are a full rail rather than the lab's three-pane window. A jump
//     still settles one pane: the controller lifts the panes in between off
//     the rail, so the lab's jumpOverride is not needed.
import {clamp} from './clamp.js';

export const S = {
  idle: 'idle',
  armed: 'armed',
  ignoring: 'ignoring',
  tracking: 'tracking',
  overshoot: 'overshoot',
  settlingCommit: 'settlingCommit',
  settlingCancel: 'settlingCancel',
};

export const DEFAULT_TUNING = {
  // Fraction of the sidebar width a gesture must cross to commit, at any
  // speed, while the fingers are still moving.
  commitThreshold: 0.15,
  // How far past a neighbour the rail may be dragged, asymptotically.
  overshootCap: 0.40,
  settleDur: 0.28,
  springDamp: 0.86,
  // Display follower time constant: invisible at reading speed, turns a
  // violent flick into a glide instead of a teleport.
  followTau: 0.15,
  axisLockPx: 6,
  // Wheel silence that means the fingers left the trackpad.
  wheelEndGap: 0.09,
  // How long the input must be QUIET before a committed gesture stops
  // swallowing its momentum tail. Deliberately longer than
  // wheelEndGap: the silence that ends a gesture is the finger lift, and the
  // tail arrives just after it - clearing on the same threshold meant the
  // commit armed suppression and the very same silence disarmed it before the
  // first tail event landed. Long enough to outlast the gap before a tail,
  // short enough that a deliberate second flick is never blocked (a human
  // needs ~180ms to re-swipe, and breaksThroughTail can still let one in).
  tailQuietGap: 0.25,
  // How long a pause counts as a FINGER LIFT rather than a seam inside one
  // gesture. macOS ends the finger phase and then begins the
  // momentum phase after a pause of its own - measured at 102ms and 89ms on a
  // real trackpad - and it restarts momentum at high velocity, so the first
  // momentum event is a large spike after a gap. At the old 60ms that pair
  // read as "the user lifted and swiped again", and one flick paged twice.
  // Above the momentum-onset gap, and still well under the ~200ms a human
  // needs to deliberately re-swipe.
  tailLiftGap: 0.15,

  // The rail end resists from the first pixel - there is nothing to reveal.
  railEndCap: 0.22,
  // The backdrop blend toward the neighbour completes at blendFactor x
  // commitThreshold of the width (the lab's blendWeight): 0.9, so the colour
  // arrives with the pane rather than at the commit line.
  blendFactor: 6.0,
};

// Asymptotic resistance past an edge: unbounded input, bounded travel. Pulled
// out of createSwipeMachine so the vertical overscroll on the tab list can use
// the SAME curve rather than a copy of it - "feels like one
// surface" is not something two independently tuned formulas can promise.
export function resistPast(excess, cap) {
  return cap <= 0 ? 0 : cap * (1 - 1 / (1 + excess / cap));
}

// A jump lifts the panes it skips off the rail, so the rail counts SLOTS while
// `lifted` (ascending pane indices) is set: a pane past a lifted one sits
// earlier.
const slotOf = (k, lifted) =>
  !lifted ? k : k - lifted.filter((x) => x < k).length;
function paneOf(s, lifted) {
  let k = s;
  for (const x of lifted || []) {
    if (x <= k) {
      k++;
    }
  }
  return k;
}

// Creates the machine. `width` and `spaceCount` are read through callbacks
// because both change under it: the sidebar is resizable and the snapshot is
// re-pushed whenever the tab strip moves.
// `quickPanes` leading positions get the short settle: the Overview is a
// whole surface arriving, and it reads slow at the ordinary spring.
export function createSwipeMachine({spaceCount, width, tuning, quickPanes = 0} = {}) {
  const tune = {...DEFAULT_TUNING, ...(tuning || {})};
  const countOf =
      typeof spaceCount === 'function' ? spaceCount : () => spaceCount || 1;
  const widthOf = typeof width === 'function' ? width : () => width || 260;

  const resist = (excess) => resistPast(excess, tune.overshootCap);

  const m = {
    tuning: tune,
    state: S.idle,
    // The committed space. Display position is spaceIndex + displayP.
    spaceIndex: 0,
    displayP: 0,
    accumX: 0,
    accumY: 0,
    lastEvent: 0,
    // Progress at which this gesture began: 0, or the rebased offset a
    // takeover inherits from the settle it interrupted.
    startP: 0,
    sessionOpen: false,
    // True while a jumpTo settle runs with no finger behind it.
    gliding: false,
    // The panes a jump took off the rail. A swipe that takes the jump over
    // keeps them off until the rail lands, so its neighbours are the panes
    // on screen.
    lifted: null,
    // The pane on screen a jump settles on first, when its target lies
    // between the two panes shown. See jumpTo.
    via: null,
    glideBurst: false,
    // Momentum tail suppression after a mid-gesture commit.
    swallowing: false,
    swallowSign: 0,
    swallowLastMag: 0,
    swallowAccel: 0,
    swallowLastT: 0,
    swallowSawGap: false,
    swallowSawMomentum: false,
    swallowStartT: 0,
    swallowEMA: 0,
    settleFrom: 0,
    // Where the current settle started, relative to spaceIndex. settleFrom
    // moves with the spring, this does not.
    settleOrigin: 0,
    settleTarget: 0,
    settleVel: 0,
    settleTargetIndex: 0,
    settleFast: false,
    settleStretch: 1,
    settleSpan: 0,

    width: widthOf,
    spaceCount: countOf,

    neighbor(dir) {
      const i = paneOf(slotOf(this.spaceIndex, this.lifted) + dir, this.lifted);
      return i >= 0 && i < countOf() ? i : null;
    },

    begin(now) {
      this.accumX = 0;
      this.accumY = 0;
      this.startP = 0;
      this.lastEvent = now;
      this.swallowing = false;
      this.state = S.armed;
    },

    // The DISPLAY caps via resist(); the accumulator must cap too, just past
    // the asymptote, or a long swipe banks invisible distance that has to be
    // paid back before a reversal or release responds. Returns whether the
    // accumulator is pinned at a limit.
    clampAccumulator() {
      const w = widthOf();
      let raw = -this.accumX / w;
      const lim = (dir) =>
          this.neighbor(dir) !== null ? 1 + 3 * tune.overshootCap : 0.45;
      const lo = -lim(-1);
      const hi = lim(1);
      const pinned = raw <= lo || raw >= hi;
      raw = clamp(raw, lo, hi);
      this.accumX = -raw * w;
      return pinned;
    },

    rawProgress() {
      return -this.accumX / widthOf();
    },

    // Progress in pane units, relative to the committed space. Past a
    // neighbour it resists; with no neighbour at all it resists from zero.
    progress() {
      const raw = this.rawProgress();
      const dir = raw > 0 ? 1 : -1;
      const sign = Math.sign(raw) || 1;
      if (this.neighbor(dir) === null) {
        const cap = tune.railEndCap;
        const a = Math.abs(raw);
        return sign * cap * (1 - 1 / (1 + a / cap));
      }
      const a = Math.abs(raw);
      return a <= 1 ? raw : sign * (1 + resist(a - 1));
    },

    // A wheel delta. `dx`/`dy` are already sign-flipped by the caller (the
    // lab passes -deltaX), so dx < 0 moves toward the next space.
    // `isMomentum` is WheelEvent.momentum - macOS's own answer to "is this the
    // fingers or the coast", surfaced by Blink and stable since forever. Every
    // heuristic below it exists because the HTML port did not have it; the
    // SwiftUI arm read NSEvent.momentumPhase directly (SidebarScrub.swift).
    //
    // Passed alongside the heuristics rather than replacing them yet: the heuristics
    // are a shipped fix for a bug reported twice, and they do not get reopened
    // on the strength of one flag being present.
    delta(dx, dy, now, isMomentum) {
      // step()'s quiet rule, applied here too: the loop parks at rest before
      // tailQuietGap passes, and a swipe from rest then never got unswallowed.
      if (this.swallowing && now - this.lastEvent > tune.tailQuietGap) {
        this.swallowing = false;
      }
      this.lastEvent = now;

      // A FINGER event during suppression is a new gesture - but only if the
      // fingers actually LIFTED first. Without that qualifier the rest of one
      // long swipe clears the suppression that exists to stop it tunnelling,
      // and a single gesture pages several spaces.
      //
      // The lift is a FACT here, not an inference: a momentum event cannot be
      // produced while a finger is down, so once one has arrived during this
      // suppression the fingers have demonstrably left, and the next
      // fingers-on event is a new gesture no matter how soon it follows.
      //
      // This used to wait for a quiet stretch to infer the same thing, and that
      // was wrong twice over. It compared `now` against a field
      // assigned `now` a few lines earlier, so it never fired at all. And the
      // gesture it most needs to catch leaves no quiet stretch to find: a
      // re-swipe into a still-running tail arrives 49ms after the last
      // momentum event, inside any threshold that a continuous stream is not.
      //
      // A stream with no momentum labels at all - a mouse wheel, or synthetic
      // input - is left to breaksThroughTail below, which is what it is for.
      // Reviving the silence rule for that case paged 17 spaces on a recording
      // that contains 3 deliberate flicks.
      if (this.swallowing && isMomentum === true) {
        this.swallowSawMomentum = true;
      }
      if (this.swallowing && isMomentum === false && this.swallowSawMomentum) {
        this.swallowing = false;
      }
      // Momentum can never START a gesture. Not merely while suppression is
      // active - from idle too: coasting is the continuation of a flick that
      // already resolved, so if it did not commit on the fingers it does not
      // get to commit on the glide.
      if (isMomentum === true && this.state === S.idle && !this.sessionOpen) {
        return;
      }
      // When the flag is absent - a synthetic event, or a mouse wheel - fall
      // back to the heuristic.
      if (this.swallowing &&
          (isMomentum === true || !this.breaksThroughTail(dx, now))) {
        return;
      }

      // begin always wins: a new gesture during a settle takes over seamlessly
      // - the pending switch finalizes NOW and the gesture chains from where
      // the panes visually are (no dropped second swipe, no visual jump).
      if (this.state === S.settlingCommit || this.state === S.settlingCancel) {
        this.takeoverFromSettle(now);
      }
      if (this.state === S.idle) {
        this.begin(now);
      }
      if (this.state === S.armed) {
        this.accumX += dx;
        this.accumY += dy;
        if (Math.hypot(this.accumX, this.accumY) >= tune.axisLockPx) {
          if (Math.abs(this.accumX) >= Math.abs(this.accumY)) {
            this.sessionOpen = true;
            this.gliding = false;
            this.state = S.tracking;
          } else {
            this.state = S.ignoring;
          }
        }
        return;
      }
      if (this.state === S.ignoring) {
        return;
      }
      if (this.state !== S.tracking && this.state !== S.overshoot) {
        return;
      }

      const w = widthOf();
      const rawBefore = -this.accumX / w;
      this.accumX += dx;
      const pinned = this.clampAccumulator();
      const rawAfter = -this.accumX / w;
      const p = this.progress();

      // Commits during the movement: a lift-time decision made a slow drag
      // trade the pane last. Measured from startP because a takeover starts
      // with a raw offset that is already past the threshold.
      const moved = p - this.startP;
      const dir = moved > 0 ? 1 : -1;
      if (Math.abs(moved) >= tune.commitThreshold &&
          this.neighbor(dir) !== null) {
        this.startSwallow(dx, now);
        this.settleFrom = this.displayP;
        this.settleOrigin = this.settleFrom;
        this.settleTarget = dir;
        this.settleTargetIndex = this.neighbor(dir);
        this.settleVel = 0;
        // A pane away at most, so the guard runs at its tightest. Left unset
        // it kept whatever span the last jumpTo wrote, and a long jump earlier
        // in the session slackened the guard for this settle.
        this.settleSpan = Math.abs(this.settleFrom - this.settleTarget);
        this.settleFast = this.spaceIndex < quickPanes ||
            this.settleTargetIndex < quickPanes;
        this.settleStretch = this.settleFast
            ? 1 : clamp(Math.sqrt(this.settleSpan / 0.35), 1, 2.2);
        this.state = S.settlingCommit;
        return;
      }

      // Rail-end saturation: pinned and still pushing carries no intent.
      if (pinned && Math.abs(rawAfter - rawBefore) < 1e-6 &&
          Math.sign(-dx) === Math.sign(rawAfter || 1)) {
        this.startSwallow(dx, now);
        this.resolveEnd();
        return;
      }

      this.state = Math.abs(p) > 1 ? S.overshoot : S.tracking;
    },

    startSwallow(dx, now) {
      this.swallowing = true;
      this.swallowSign = Math.sign(dx);
      this.swallowLastMag = Math.abs(dx);
      this.swallowAccel = 0;
      this.swallowLastT = now;
      this.swallowSawGap = false;
      this.swallowSawMomentum = false;
      this.swallowStartT = now;
      this.swallowEMA = Math.abs(dx);
    },

    // A genuine NEW gesture must win over a decaying momentum tail. Physics:
    // a tail can only DECELERATE, so sustained acceleration is proof of a
    // finger - but a single spike is just tail noise, and acceleration alone
    // would double-switch one long swipe. A real re-swipe has a micro-pause
    // (finger lift and retouch) before its ramp; a continuous gesture streams
    // events every ~10ms with no gap at all.
    breaksThroughTail(dx, now) {
      const mag = Math.abs(dx);
      const flip = Math.sign(dx) !== 0 && Math.sign(dx) !== this.swallowSign;
      this.swallowAccel = mag > this.swallowLastMag * 1.05 ? this.swallowAccel + 1 : 0;
      this.swallowLastMag = mag;
      const gap = now - (this.swallowLastT || now);
      this.swallowLastT = now;
      if (gap >= tune.tailLiftGap) {
        this.swallowSawGap = true;
      }
      const ema = this.swallowEMA || mag;
      // Humans cannot intentionally re-swipe within 180ms of a commit.
      const past = now - (this.swallowStartT || 0) >= 0.18 && this.swallowSawGap;
      const jump = mag >= Math.max(6, 3 * ema);
      const ramp = this.swallowAccel >= 2 && mag >= Math.max(6, 1.5 * ema);
      this.swallowEMA = 0.75 * ema + 0.25 * mag;
      if (flip || (past && (jump || ramp))) {
        this.swallowing = false;
        return true;
      }
      return false;
    },

    takeoverFromSettle(now) {
      const committing = this.state === S.settlingCommit;
      // Display progress relative to the space being settled on.
      const rel = this.settleFrom - this.settleTarget;
      if (committing) {
        // settleTarget is the via pane's slot until the rail lifts again.
        this.spaceIndex = this.via ?? this.settleTargetIndex;
      }
      this.via = null;
      this.begin(now);
      this.sessionOpen = true;
      this.gliding = false;
      // The burst hold is a jumpTo rule. This is a gesture now, and its
      // commit announces at the commit.
      this.glideBurst = false;
      this.state = S.tracking;
      this.accumX = -rel * widthOf();
      // Through the resist curve, like every later reading: a raw rel panes
      // away from the target read as movement before the fingers moved.
      this.startP = this.progress();
      this.displayP = rel;
    },

    // An explicit release, or an interruption (a click landing mid-gesture)
    // that must resolve like one rather than leaving the rail part-way.
    end() {
      this.swallowing = false;
      if (this.state === S.armed || this.state === S.ignoring) {
        this.state = S.idle;
        return;
      }
      if (this.state === S.tracking || this.state === S.overshoot) {
        this.resolveEnd();
      }
    },

    resolveEnd() {
      const p = this.progress();
      const dir = p > 0 ? 1 : -1;
      const n = this.neighbor(dir);
      // Always a rollback: delta() commits at the threshold before the lift.
      // The settle starts from where the DISPLAY is - the follower may lag a
      // violent gesture - so the handoff is visually continuous.
      this.settleFrom = this.displayP;
      this.settleOrigin = this.settleFrom;
      this.settleVel = 0;
      // A rail-end bounce is stiffer and critically damped: it snaps back.
      this.settleFast = n === null;
      const dist = Math.abs(this.settleFrom);
      this.settleStretch =
          this.settleFast ? 1 : clamp(Math.sqrt(dist / 0.35), 1, 2.2);
      this.settleSpan = dist;
      this.settleTarget = 0;
      this.settleTargetIndex = this.spaceIndex;
      this.state = S.settlingCancel;
    },

    // Animates to any space, and returns the one it will land on. With a full
    // rail this is the same settle a swipe uses, just over a longer distance.
    jumpTo(target) {
      const i = clamp(Math.round(target), 0, countOf() - 1);
      if (i === this.spaceIndex && this.state === S.idle) {
        return i;
      }
      // spaceIndex is not rebased, unlike takeoverFromSettle above: the
      // controller reads a move of it as an arrival and displayP is measured
      // from it, so rebasing switched to a space the rail was passing through.
      // The panes on screen keep their places. Lifting from spaceIndex alone
      // swapped a half-shown pane for the target in one frame.
      const was = this.lifted;
      const at = slotOf(this.spaceIndex, was) + (this.sessionOpen ? this.displayP : 0);
      const last = countOf() - 1;
      const a = clamp(paneOf(Math.floor(at), was), 0, last);
      const b = clamp(paneOf(Math.ceil(at), was), 0, last);
      this.via = null;
      if (i > a && i < b) {
        // The target cannot sit next to both. The rail finishes on the nearer
        // pane first, and settleStep lifts again once the other is off screen.
        this.via = at - Math.floor(at) < 0.5 ? a : b;
        this.settleFrom = at - slotOf(this.spaceIndex, was);
        this.settleTarget = slotOf(this.via, was) - slotOf(this.spaceIndex, was);
        this.settleOrigin = this.settleFrom;
        this.spanSettle();
      } else {
        this.relift(i, at, a === b ? [a] : [a, b]);
      }
      this.settleTargetIndex = i;
      this.settleVel = 0;
      this.settleFast = false;
      this.accumX = 0;
      this.sessionOpen = true;
      // A burst is a jumpTo that interrupts a settling glide.
      this.glideBurst = this.gliding;
      this.gliding = true;
      this.swallowing = false;
      this.state = S.settlingCommit;
      return i;
    },

    // Lifts every pane between `keep` and the target, and aims the settle at
    // the target without moving what is on screen. `at` is the rail slot now.
    relift(i, at, keep) {
      const was = this.lifted;
      const shown = [...keep, i];
      const lifted = [];
      for (let k = Math.min(...shown) + 1; k < Math.max(...shown); k++) {
        if (!shown.includes(k)) {
          lifted.push(k);
        }
      }
      this.lifted = lifted.length ? lifted : null;
      const base = slotOf(this.spaceIndex, this.lifted);
      this.settleFrom = slotOf(keep[0], this.lifted) +
          (at - slotOf(keep[0], was)) - base;
      this.settleOrigin = this.settleFrom;
      this.settleTarget = slotOf(i, this.lifted) - base;
      this.spanSettle();
    },

    spanSettle() {
      this.settleSpan = Math.abs(this.settleTarget - this.settleFrom);
      this.settleStretch = clamp(Math.sqrt(this.settleSpan / 0.35), 1, 2.2);
    },

    // Advances the spring, or the display follower when a gesture is live.
    // Returns the display progress to render, in pane units relative to
    // spaceIndex. Callers drive this once per animation frame.
    step(dt, now) {
      // A trackpad gives no release event. Silence is the only signal there
      // is, and inferring one is what keeps an abandoned gesture from
      // freezing the rail part-way.
      const quiet = now - this.lastEvent;
      if ((this.state === S.tracking || this.state === S.overshoot) &&
          quiet > tune.wheelEndGap && quiet < 1) {
        this.end();
      } else if ((this.state === S.armed || this.state === S.ignoring) &&
                 quiet > tune.wheelEndGap) {
        // Silence has to resolve a gesture from EVERY state it can be found
        // in, not just the two that move the rail. `ignoring` was terminal
        // without this: delta() drops every event while it is set, and nothing
        // else ever called end() for it - so one diagonal scroll that the axis
        // lock resolved vertical killed swiping until the surface was rebuilt.
        // Found on a live stuck sidebar reading state:'ignoring', running:true
        //.
        //
        // No upper bound here, unlike above: these two carry no rail offset to
        // resolve, so ending late costs nothing, and staying stuck costs
        // everything. The 1s bound exists to stop a long-abandoned TRACKING
        // gesture snapping the rail after the fact.
        this.end();
      }
      if (this.swallowing && now - this.lastEvent > tune.tailQuietGap) {
        this.swallowing = false;
      }

      const settled = this.settleStep(dt);
      if (settled !== null) {
        this.displayP = settled;
      } else if (this.sessionOpen && this.state !== S.idle) {
        const target = this.progress();
        const tau = Math.max(0.001, tune.followTau);
        this.displayP += (target - this.displayP) * (1 - Math.exp(-dt / tau));
      } else {
        this.displayP = 0;
      }
      return this.displayP;
    },

    settleStep(dt) {
      if (this.state !== S.settlingCommit && this.state !== S.settlingCancel) {
        return null;
      }
      // 0.4: a critically damped spring reads as landed at about a third of
      // this, which is the ~100ms pane trade the swipe is tuned to.
      const dur = this.settleFast
          ? Math.max(0.06, tune.settleDur * 0.4)
          : Math.max(0.08, tune.settleDur * (this.settleStretch || 1));
      const dampR = this.settleFast ? 1.0 : tune.springDamp;
      const stiff = (2 * Math.PI / dur) ** 2;
      const damp = 2 * Math.sqrt(stiff) * dampR;
      // Substep: a stiff spring explodes under explicit integration whenever a
      // frame runs long.
      const steps = Math.max(1, Math.ceil(dt / 0.004));
      const h = dt / steps;
      for (let i = 0; i < steps; i++) {
        const x = this.settleFrom - this.settleTarget;
        this.settleVel += (-stiff * x - damp * this.settleVel) * h;
        this.settleFrom += this.settleVel * h;
      }
      // Checked before the landing test below, which would otherwise land a
      // spring that creeps up on the via pane on the target instead.
      if (this.via !== null &&
          (this.settleFrom - this.settleTarget) *
              Math.sign(this.settleTarget - this.settleOrigin) >= -0.002) {
        const via = this.via;
        this.via = null;
        this.relift(this.settleTargetIndex,
                    slotOf(this.spaceIndex, this.lifted) + this.settleFrom, [via]);
      }
      // Scaled by the span this settle set out to cover, not by |settleTarget|:
      // a press reversing a glide leaves the target near zero with the rail
      // still panes away, which read as a diverging spring and snapped home.
      if (!Number.isFinite(this.settleFrom) ||
          Math.abs(this.settleFrom - this.settleTarget) > 3 + (this.settleSpan || 0)) {
        this.settleFrom = this.settleTarget;  // never diverge
        this.settleVel = 0;
      }
      if (Math.abs(this.settleFrom - this.settleTarget) < 0.002 &&
          Math.abs(this.settleVel) < 0.02) {
        this.spaceIndex = this.settleTargetIndex;
        this.lifted = null;
        this.via = null;
        this.settleFrom = this.settleTarget;
        this.accumX = 0;
        this.sessionOpen = false;
        this.gliding = false;
        this.glideBurst = false;
        this.state = S.idle;
        return 0;
      }
      return this.settleFrom;
    },
  };
  // Mutates the shared object rather than replacing it -
  // every read above is `tune.xxx` at call time, so a live preset switch
  // takes effect on the in-flight gesture, not just the next one.
  m.setTuning = (next) => Object.assign(tune, next);
  return m;
}


// The DOM half: wheel events in, a rail transform out.
//
// Installed once per route, not once per snapshot. The snapshot is re-pushed
// whenever a tab title changes, and adding a wheel listener each time would
// leave one per push attached to the same element.
// this file counts in RAIL positions. `leadingPanes` is how
// many panes precede the first space (the Overview pane is one), and only the
// announcement below converts, so the two numberings meet in one place.
// Settings > Swipe haptics. Strength names are what the browser maps to an
// actuation. `detents` splits the commit distance, `settleStep` is in panes,
// and 0 turns that kind of tap off.
export const HAPTIC_PATTERNS = {
  off: null,
  crisp: {detents: 3, detent: 'soft', commit: 'plain', land: 'plain'},
  smooth: {detents: 3, detent: 'soft', commit: 'plain', settleStep: 0.125,
           settle: 'soft', land: 'soft'},
};

export function createSwipeController(
    root, {onIdle, onActiveIndex, tuning, leadingPanes = 0, onLeadingPane,
           onRailPaint} = {}) {
  const now = () => performance.now() / 1000;
  let spaceCount = 1;
  let rail = null;
  let running = false;
  let prevT = 0;
  let announced = 0;
  let pressHoldFrom = null;
  // The index the surface announced and the model has not confirmed yet.
  let awaitingAck = null;
  let marks = [];
  // Last index handed to onActiveIndex. The space dots bar is one element
  // outside the rail, so nothing moves it into place - it has to be
  // told where the swipe is going.
  let reportedActive = null;

  const machine = createSwipeMachine({
    spaceCount: () => spaceCount,
    // The native side owns the width (AtriumRootView::sidebar_slot), and the
    // user can drag it - so it is read per frame rather than captured.
    width: () => root.clientWidth || 260,
    tuning,
    quickPanes: leadingPanes,
  });

  // The backdrop scrub, reported through the settle so the colour lands with
  // the pane. Gesture-driven only: a jumpTo gets the browser's own cross-fade.
  // scrubFrom outlives the commit that flips machine.spaceIndex - the origin
  // space's theme is the layer underneath.
  let scrubFrom = null;
  let scrubTarget = null;
  let scrubWeight = -1;
  const tune = {...DEFAULT_TUNING, ...(tuning || {})};
  // In pane units: about 3px of a 260px sidebar.
  const SCRUB_DEAD_ZONE = 0.01;
  // Seconds a first dot press waits for a second one. Presses from a fast
  // click burst land inside it, so only the space the rail ends on announces.
  const PRESS_HOLD = 0.25;

  // Trackpad taps at fixed steps of travel, so a faster swipe taps faster.
  // Only a finger swipe taps. A jumpTo or a rollback settle has no hand on
  // the trackpad to feel it.
  const LAND_TAP_GAP = 0.01;
  let haptics = HAPTIC_PATTERNS.crisp;
  let dragTaps = 0;
  // The space a committed swipe settles on, or null when the settle must not
  // tap.
  let tapTarget = null;
  let settleTaps = 0;

  function tap(strength) {
    if (strength) {
      chrome.send('sidebarSwipeHaptic', [strength]);
    }
  }

  // The last detent is the commit, which taps on its own.
  function tapDragDetents() {
    if (!haptics?.detents) {
      return;
    }
    const step = machine.tuning.commitThreshold / haptics.detents;
    const n = Math.floor(Math.abs(machine.progress() - machine.startP) / step);
    if (n > dragTaps && n < haptics.detents) {
      dragTaps = n;
      tap(haptics.detent);
    }
  }

  function tapSettle() {
    if (tapTarget === null) {
      return;
    }
    const settling = machine.state === S.settlingCommit && !machine.gliding &&
        machine.settleTargetIndex === tapTarget;
    // The machine goes idle on the frame that lands, so the landing has to be
    // read from either state.
    const landed = settling
        ? Math.abs(machine.settleFrom - machine.settleTarget) < LAND_TAP_GAP
        : machine.state === S.idle && machine.spaceIndex === tapTarget;
    if (landed || !settling) {
      tapTarget = null;
      if (landed) {
        tap(haptics?.land);
      }
      return;
    }
    if (!haptics?.settleStep) {
      return;
    }
    const n = Math.floor(
        Math.abs(machine.settleFrom - machine.settleOrigin) / haptics.settleStep);
    if (n > settleTaps) {
      settleTaps = n;
      tap(haptics.settle);
    }
  }

  function endScrub(commit) {
    if (scrubTarget !== null) {
      chrome.send('sidebarSpaceScrubEnded', [commit]);
    }
    scrubFrom = null;
    scrubTarget = null;
    scrubWeight = -1;
  }

  // The staged side is promoted only once the machine has landed on it.
  // Anything else that takes the rail over - a jump, a rebuild, rest - calls
  // this so the browser never keeps a layer for a space the rail left.
  function settleScrub() {
    endScrub(scrubTarget !== null && machine.spaceIndex === scrubTarget);
  }

  // `at` is a rail slot, so a lifted gap maps the spaces to slots first.
  function reportScrub(at, gap) {
    if (scrubFrom === null) {
      return;
    }
    // The rail lands and drops its gap while the scrub runs on through the
    // momentum tail, so a pair across the gap keeps it. `at` is a whole pane.
    if (!gap && scrubTarget !== null && Math.abs(scrubTarget - scrubFrom) > 1) {
      gap = [];
      for (let k = Math.min(scrubFrom, scrubTarget) + 1;
           k < Math.max(scrubFrom, scrubTarget); k++) {
        gap.push(k);
      }
      at = slotOf(at, gap);
    }
    // A second flick past a committed space promotes that layer and stages
    // the next. A settle's own overshoot must not: the spring comes back.
    const passing =
        machine.state === S.tracking || machine.state === S.overshoot ||
        (machine.state === S.settlingCommit &&
         machine.settleTargetIndex !== scrubTarget);
    if (passing && scrubTarget !== null &&
        machine.spaceIndex === scrubTarget) {
      const targetSlot = slotOf(scrubTarget, gap);
      if (Math.sign(at - targetSlot) ===
              Math.sign(targetSlot - slotOf(scrubFrom, gap)) &&
          Math.abs(at - targetSlot) >= SCRUB_DEAD_ZONE) {
        chrome.send('sidebarSpaceScrubEnded', [true]);
        scrubFrom = scrubTarget;
        scrubTarget = null;
        scrubWeight = -1;
      }
    }
    const fromSlot = slotOf(scrubFrom, gap);
    const off = at - fromSlot;
    // A restage rasters the neighbour's theme in the browser, so a finger
    // wobbling on the origin keeps the side it has rather than flipping.
    let want = Math.abs(off) < SCRUB_DEAD_ZONE
        ? scrubTarget : paneOf(fromSlot + Math.sign(off), gap);
    // A leading pane is not a space and has no theme to blend toward.
    if (want !== null && (want < leadingPanes || want >= spaceCount)) {
      want = null;
    }
    if (want !== scrubTarget) {
      // Crossing back over the origin: the staged side's weight is already
      // at zero, so dropping it is invisible.
      if (scrubTarget !== null) {
        chrome.send('sidebarSpaceScrubEnded', [false]);
      }
      scrubTarget = want;
      scrubWeight = -1;
      if (want === null) {
        return;
      }
      chrome.send('sidebarSpaceScrubBegan', [want - leadingPanes]);
    }
    if (scrubTarget === null) {
      return;
    }
    const full = tune.blendFactor * tune.commitThreshold;
    const weight = full <= 0 ? 1 : clamp(Math.abs(off) / full, 0, 1);
    // Each push is a browser-process message, so a frame that moved less
    // than a colour step stays quiet.
    if (Math.abs(weight - scrubWeight) >= 0.002) {
      scrubWeight = weight;
      chrome.send('sidebarSpaceScrubBlend', [Number(weight.toFixed(4))]);
    }
  }

  // The pinned copy shows only while it was drawn for every profile on screen.
  // At the settle the rail lands before the rebuild does, so a stale copy
  // shown then flashes the outgoing profile's tiles.
  // `at` and `drawAt` are rail slots, so a lifted gap maps each end back to
  // its pane.
  function markProfileSpan(at, drawAt, gap) {
    if (!rail) {
      return;
    }
    const panes = rail.querySelectorAll('.railpane');
    // Empty on the Overview pane, which has no profile and draws no grid.
    const profileAt = (k) => panes[k]?.dataset?.profileId || '';
    // The settle spring is underdamped, so it sweeps PAST the space it lands
    // on into the next, either direction. Reading that as a profile change
    // flicked the moving pane grids on mid-settle: the reported bounce.
    const settling = machine.state === S.settlingCommit ||
        machine.state === S.settlingCancel;
    // The settle's real start, not spaceIndex: a takeover or a reversed jump
    // settles onto spaceIndex from up to a pane away, and a clamp that starts
    // there glued a same-profile grid to the incoming pane.
    const from = slotOf(machine.spaceIndex, gap) + (machine.settleOrigin || 0);
    const to = slotOf(machine.settleTargetIndex, gap);
    const held = settling
        ? Math.min(Math.max(at, Math.min(from, to)), Math.max(from, to))
        : at;
    // The Overview's empty id is kept. Dropping it made a glide onto the
    // Overview read as a same-profile span, which parked the pinned grid at
    // the sidebar edge while the Overview slid in under it.
    const spanned = [profileAt(paneOf(Math.floor(held), gap)),
                     profileAt(paneOf(Math.ceil(held), gap))];
    const pinned =
        [...root.children].find((el) => el.classList.contains('favgrid'));
    const drawnFor = pinned?.dataset?.profileId || '';
    const pinnedFits =
        spanned.length > 0 && spanned.every((id) => id === drawnFor);
    root.classList.toggle('xprofile', !pinnedFits);
    // Stamped per pane, because a pane of the pinned grid's own profile holds
    // empty slots rather than tiles. One root-wide class showed every grid at
    // once, which blanked the outgoing half of a cross-profile swipe.
    for (let k = 0; k < panes.length; k++) {
      panes[k].classList.toggle('otherprofile',
                                (panes[k].dataset?.profileId || '') !== drawnFor);
    }
    // The pinned copy belongs to a pane that is still on screen. Once it is
    // not, it holds the outgoing profile's tiles over the incoming pane until
    // the rebuild lands, which is the flash it used to be hidden for.
    const pinnedGone = !spanned.includes(drawnFor);
    root.classList.toggle('pinnedoff', pinnedGone);
    // The pinned extension strip is the root's SIBLING, so it takes the class
    // on itself - CSS cannot reach back past the root to it.
    const strip = extStrip();
    strip?.classList.toggle('pinnedoff', pinnedGone);
    // sidebar.css anchors it at the sidebar's full width, so left alone it
    // covers the incoming pane the whole transit. Mid-span it now travels
    // with its own pane instead, the way a railpane's transform would.
    if (pinned || strip) {
      // The clamp on `held` lets the spring sweep past the landing pane, so a
      // copy that matches its span still has to follow the pane's overshoot.
      if (pinnedFits && held === drawAt) {
        setPinnedTransform(pinned, strip, '');
      } else {
        // The spanned edge that matches, not the first same-profile pane in
        // the rail - another space earlier in the DOM can share the profile,
        // and tracking THAT one flew the copy off past the real outgoing pane.
        const loIdx = Math.floor(held), hiIdx = Math.ceil(held);
        const paneIndex = profileAt(paneOf(loIdx, gap)) === drawnFor ? loIdx :
            (profileAt(paneOf(hiIdx, gap)) === drawnFor ? hiIdx : -1);
        // drawAt, not at: on at the grid kept sliding while the rail stood
        // still through the settle overshoot clamp.
        const width = root.clientWidth || 260;
        const raw = paneIndex < 0 ? 0 : (paneIndex - drawAt) * width;
        // CSS scale wraps transform, so the browser computes
        // scale(s) * translateX(x) = translateX(s*x). Divide by s
        // to keep the grid at the right offset.
        const share = clamp(leadingPanes - drawAt, 0, 1);
        const s = 1 - .08 * share;
        const offset = s > 0 ? raw / s : raw;
        setPinnedTransform(pinned, strip, `translateX(${offset.toFixed(2)}px)`);
      }
    }
  }

  // The grid and the strip are one block over the pane they were drawn for, so
  // they travel together or they separate mid-swipe.
  function setPinnedTransform(grid, strip, value) {
    if (grid) {
      grid.style.transform = value;
    }
    if (strip) {
      strip.style.transform = value;
    }
  }

  const jumpGap = () => machine.lifted;
  let gapLifted = false;
  // Every frame, not once: a rebuild mid-glide swaps in panes without the class.
  function liftGap(gap) {
    if (!gap && !gapLifted) {
      return;
    }
    const panes = rail.querySelectorAll('.railpane');
    for (let k = 0; k < panes.length; k++) {
      panes[k].classList.toggle('jumped', !!gap && gap.includes(k));
    }
    gapLifted = !!gap;
  }

  function paint(p) {
    if (!rail) {
      return;
    }
    const gap = jumpGap();
    liftGap(gap);
    const at = slotOf(machine.spaceIndex, gap) + p;
    // Overshoot between spaces is wanted. Past the first lies the Overview, a
    // whole surface, and a spring dipping into it reads as the Overview
    // arriving. Settling only - dragging into it is how the user opens it.
    // Only a settle that started on the space side can overshoot into the
    // Overview. One leaving it travels those positions for real, and clamping
    // them held the chrome at a flat 0 for the whole glide.
    const settlingOntoASpace =
        (machine.state === S.settlingCommit ||
         machine.state === S.settlingCancel) &&
        machine.settleTargetIndex >= leadingPanes &&
        machine.spaceIndex >= leadingPanes;
    const drawAt = settlingOntoASpace ? Math.max(at, leadingPanes) : at;
    const x = -drawAt * (root.clientWidth || 260);
    rail.style.transform = `translateX(${x.toFixed(2)}px)`;
    markProfileSpan(at, drawAt, gap);
    // drawAt, not at: the chrome over the rail has to agree with where the
    // rail was painted, overshoot clamp included.
    onRailPaint?.(drawAt);
    reportScrub(at, gap);
  }

  // A gesture is seconds long, but nothing stops someone holding one for
  // minutes, and the samples are only ever read as a summary.
  const MAX_MARKS = 600;

  // Scrolling and swiping compete for the same wheel events. Once the tab list
  // starts a native scroll, the rest of that gesture keeps going to it and the
  // swipe never begins - which is the "sometimes I cannot swipe" bug report.
  // The lab locks its scroll zone for the duration; this is that lock.
  //
  // HORIZONTAL states only. `armed` still belongs to the axis lock and
  // `ignoring` IS the vertical branch, so locking on either would freeze the
  // very scroll the user is asking for. The lab's own comment says the same.
  //
  // Toggled on the ROOT rather than a `.sidebar`: buildRail renders one pane
  // per space, so there are several, and the class has to cover all of them.
  function syncLock() {
    const live = machine.state === S.tracking ||
        machine.state === S.overshoot ||
        machine.state === S.settlingCommit ||
        machine.state === S.settlingCancel;
    root.classList.toggle('swipelock', live);
    // The strip is the root's sibling and is never rebuilt, so a class it
    // takes mid-glide plays as a drop from above unless it is locked too.
    extStrip()?.classList.toggle('swipelock', live);
  }

  function extStrip() {
    return [...(root.parentNode?.children || [])]
        .find((el) => el.classList.contains('exs-row'));
  }

  // While a commit settles the dot belongs to the space being entered, not the
  // one being left - the same rule the lab's updateDots applies.
  function reportActive() {
    const active = machine.state === S.settlingCommit ? machine.settleTargetIndex
                                                      : machine.spaceIndex;
    if (active === reportedActive || !Number.isInteger(active)) {
      return;
    }
    reportedActive = active;
    onActiveIndex?.(active);
  }

  function frame() {
    const t = now();
    const dt = Math.min(0.05, t - prevT);
    prevT = t;
    if (marks.length < MAX_MARKS) {
      marks.push(dt * 1000);
    }
    paint(machine.step(dt, t));
    tapSettle();
    reportActive();
    // The DECIDED space, not the arrived one. C++ swaps the web card the
    // moment this lands, so announcing on arrival kept the old page drawing
    // for the whole settle and held the incoming tab back with it.
    // A burst holds until the rail lands. Mid-burst spaceIndex is the space
    // being left, and announcing it switched the browser back.
    let committed = machine.spaceIndex;
    if (machine.state === S.settlingCommit) {
      if (machine.gliding && !machine.glideBurst) {
        pressHoldFrom ??= t;
      }
      const holding = machine.glideBurst ||
          (pressHoldFrom !== null && t - pressHoldFrom < PRESS_HOLD);
      committed = holding ? announced : machine.settleTargetIndex;
    } else {
      pressHoldFrom = null;
    }
    if (committed !== announced) {
      announced = committed;
      // Held until a snapshot comes back agreeing. The browser switches spaces
      // asynchronously, so the NEXT push can still carry the old active space -
      // and attach() adopting it would yank the rail back to the space the user
      // just left, then forward again when the switch lands. That is the
      // creeping and overshoot bug.
      awaitingAck = announced;
      // A leading pane is not a space, so there is nothing to select. Saying
      // otherwise would switch the browser to space 0 whenever the rail
      // reached Overview.
      if (announced >= leadingPanes) {
        chrome.send('sidebarSpaceSelected', [announced - leadingPanes]);
      }
    }
    syncLock();
    // `swallowing` is cleared by step() after tailQuietGap of quiet, and step()
    // only runs while this loop does - so exiting at idle with it still set
    // stranded it ON, and delta() silently dropped every wheel event of the
    // NEXT gesture. That is the "sometimes I cannot swipe any more" in
    // one commit could disable swiping until the surface rebuilt.
    //
    // Kept alive rather than cleared here: the momentum tail arrives AFTER the
    // settle ends, so clearing on idle would let the tail start a fresh gesture
    // and page a second space, which is exactly that bug.
    if (machine.state === S.idle && !machine.sessionOpen && !machine.swallowing) {
      running = false;
      root.classList.remove('swipelock');
      extStrip()?.classList.remove('swipelock');
      publishStats();
      // Reported from REST, not from the announce above. A gesture can pass
      // through a leading pane and settle back, so announcing on the way past
      // marks a pane the rail never stayed on.
      if (machine.spaceIndex < leadingPanes) {
        onLeadingPane?.(machine.spaceIndex);
      }
      settleScrub();
      onIdle?.();
      return;
    }
    requestAnimationFrame(frame);
  }

  // Frame intervals for the gesture that just ended. The old stats could only
  // measure a programmatic transition; this is the real finger, which is the
  // number the SwiftUI comparison actually needs. Read from the debug port
  // (build/swipe-bench.mjs) or the console.
  function publishStats() {
    // The first interval is measured from start(), not from a previous frame.
    const sorted = marks.slice(1).sort((a, b) => a - b);
    marks = [];
    if (sorted.length < 4) {
      return;
    }
    window.__swipeStats = {
      frames: sorted.length,
      median: sorted[Math.floor(sorted.length / 2)],
      p95: sorted[Math.floor(sorted.length * 0.95)],
      worst: sorted[sorted.length - 1],
    };
  }

  // The loop runs only while a gesture or a settle is live. This surface is on
  // screen for the whole session, so an always-on rAF would be a permanent
  // wakeup for nothing.
  function start() {
    if (running) {
      return;
    }
    running = true;
    prevT = now();
    requestAnimationFrame(frame);
  }

  root.addEventListener('wheel', (e) => {
    if (!rail || spaceCount < 2) {
      return;
    }
    const inGesture = machine.state === S.tracking ||
        machine.state === S.overshoot || machine.swallowing;
    if (!inGesture) {
      // Vertical intent belongs to the tab list. A live gesture still
      // finishes: the cursor does not move during a swipe, so whatever is
      // under it is not what the fingers meant.
      if (Math.abs(e.deltaY) > Math.abs(e.deltaX) * 1.2) {
        return;
      }
      // The dot bar keeps its native scroll only when it HAS one.
      // only .dotscroll scrolls now, not the whole .spacedots row.
      const dots = e.target.closest?.('.dotscroll');
      if (dots && dots.scrollWidth > dots.clientWidth) {
        return;
      }
    }
    e.preventDefault();
    // e.momentum is WheelEvent.momentum. Undefined on a synthetic
    // event, which is why the machine treats only an explicit false as "the
    // fingers are on the trackpad".
    //
    // deltaX IS negated, so a rightward scrub reaches the PREVIOUS
    // space. Safari and Arc both make a two-finger swipe right go back, and
    // the user overturned the opposite choice on real hardware 2026-08-17.
    // The 2026-08-10 attempt to match Cmd+Shift+Right dropped this negation on
    // the strength of swipe-controller-test.mjs, but that test's deltaX fixture
    // was calibrated by swipe-direction-bisect.mjs, so both share one
    // assumption about which sign a rightward finger produces and cannot catch
    // it being wrong. Cmd+Shift+Right still moves FORWARD, so the two
    // disagree - that is an open question.
    const live = () => machine.state === S.tracking || machine.state === S.overshoot;
    const was = machine.state;
    const settleWas = machine.settleOrigin;
    machine.delta(-e.deltaX, -e.deltaY, now(),
                  typeof e.momentum === 'boolean' ? e.momentum : undefined);
    // Momentum still moves a live drag, but no finger is down to feel a tap.
    // A takeover can commit in the event that started it, so a new settle
    // shows as a new origin.
    const fingers = e.momentum !== true;
    if (fingers && machine.state === S.settlingCommit &&
        (was !== S.settlingCommit || machine.settleOrigin !== settleWas)) {
      tap(haptics?.commit);
      tapTarget = machine.settleTargetIndex;
      settleTaps = 0;
    }
    if (live()) {
      if (was !== S.tracking && was !== S.overshoot) {
        dragTaps = 0;
      }
      if (fingers) {
        tapDragDetents();
      }
    }
    // The previous swipe's scrub stays open through its momentum tail. A new
    // gesture measured from that stale origin blends toward the wrong space.
    // A takeover mid-settle continues the same rail motion and keeps its scrub.
    if (live() && (was === S.idle || was === S.armed)) {
      settleScrub();
    }
    if (scrubFrom === null && live()) {
      scrubFrom = machine.spaceIndex;
    }
    // Before start(), so the lock lands on the event that entered tracking
    // rather than one frame later - a frame is long enough for the scroll
    // container to claim the gesture.
    syncLock();
    start();
  }, {passive: false});

  // A press landing mid-gesture resolves it like a release rather than
  // leaving the rail parked between two spaces.
  root.addEventListener('pointerdown', () => {
    if (machine.state === S.tracking || machine.state === S.overshoot) {
      machine.end();
      start();
    }
  });

  window.addEventListener('resize', () => paint(machine.displayP));
  return {
    // Called after the rail is rebuilt.
    //
    // `activeIndex` is the space the MODEL says is active, and it wins. This
    // used to be remembered locally instead, because when the rail was written
    // there was no space producer and the browser genuinely could not say which
    // space was showing. A producer has landed since, and keeping the
    // local guess after that let the two drift: the rail would sit on a pane
    // the model did not consider active, which made an ordinary drag resolve
    // its source and destination to different spaces and read as a cross-space
    // move.
    //
    // Omitted, it falls back to the remembered index, so a caller that has no
    // active space to offer behaves exactly as before.
    //
    // While an announcement is unacknowledged the LOCAL index wins. The browser
    // switches asynchronously, so a push that predates the switch still names
    // the old space, and adopting it would drag the rail back to the space the
    // user just left and then forward again when the switch lands - visible as
    // creeping, an overshoot, or a jump one space too far. Same
    // optimistic-overlay shape the folds use: believe the model, except where
    // the surface has said something the model has not answered yet.
    attach(count, activeIndex) {
      settleScrub();
      const sameGeometry = Math.max(1, count) === spaceCount;
      spaceCount = Math.max(1, count);
      rail = root.querySelector('.rail');
      // A rebuild during a jumpTo glide over the same panes keeps the glide:
      // no finger holds the old nodes, and the new rail has the same widths.
      if (sameGeometry && this.isGliding()) {
        if (Number.isInteger(activeIndex) && activeIndex === awaitingAck) {
          awaitingAck = null;
        }
        paint(machine.displayP);
        return;
      }
      // A commit still settling has already told the browser where it is going,
      // so THAT is the local index the hold below defends. spaceIndex is the
      // space being left until the spring arrives.
      const settling = machine.state === S.settlingCommit ||
          machine.state === S.settlingCancel;
      let wanted = machine.state === S.settlingCommit ? machine.settleTargetIndex
                                                      : machine.spaceIndex;
      if (Number.isInteger(activeIndex)) {
        if (awaitingAck === null || activeIndex === awaitingAck) {
          wanted = activeIndex;
          awaitingAck = null;
        }
      }
      machine.spaceIndex = Math.min(Math.max(wanted, 0), spaceCount - 1);
      machine.lifted = null;
      machine.via = null;
      announced = machine.spaceIndex;
      // Land at rest. A rebuild replaces the rail's nodes, so any in-flight
      // offset describes geometry that no longer exists - repainting it is what
      // leaves the rail parked between two panes. The spring has to be stopped
      // too: zeroing the offset alone leaves it to resume next frame from where
      // it was, and its arrival then writes the pre-rebuild index back.
      if (settling) {
        machine.state = S.idle;
        machine.sessionOpen = false;
        machine.settleFrom = 0;
        machine.settleTarget = 0;
        machine.settleVel = 0;
      }
      machine.displayP = 0;
      // The machine only sets this once a gesture commits, so at rest it would
      // otherwise still read 0 and put the dot on the wrong space.
      machine.settleTargetIndex = machine.spaceIndex;
      paint(0);
      reportActive();
    },
    detach() {
      settleScrub();
      spaceCount = 1;
      rail = null;
      gapLifted = false;
    },
    // A rebuild mid-gesture would throw away the very nodes the wheel events
    // are being delivered to, and reset the tab list's scroll position with
    // them. Callers defer their re-render until this goes false.
    // Read-only window into the machine, for build/wheel-capture.mjs. The wheel
    // path can drop a delta for reasons that look identical from outside - the
    // vertical-intent guard, the axis lock resolving to `ignoring`, or the
    // momentum-tail suppression - and a capture that cannot tell them apart
    // sends the next hour in the wrong direction.
    debug() {
      const d = {
        state: machine.state,
        swallowing: !!machine.swallowing,
        sessionOpen: !!machine.sessionOpen,
        spaceIndex: machine.spaceIndex,
        displayP: Number((machine.displayP || 0).toFixed(3)),
        running,
        awaitingAck,
        spaceCount,
      };
      // While the tail is suppressing, carry the inputs breaksThroughTail
      // decides on. A swallowed re-swipe is otherwise indistinguishable from
      // the tail that swallowed it, and knowing WHICH term failed - jump,
      // ramp, flip or the `past` gate - is the whole diagnosis.
      if (machine.swallowing) {
        d.tail = {
          ema: Number((machine.swallowEMA || 0).toFixed(1)),
          accel: machine.swallowAccel || 0,
          sawGap: !!machine.swallowSawGap,
          sign: machine.swallowSign,
          // Seconds since the commit that started the suppression; the `past`
          // gate needs this over 0.18 before jump or ramp are even consulted.
          age: Number(((now() - (machine.swallowStartT || 0))).toFixed(3)),
          lastMag: Number((machine.swallowLastMag || 0).toFixed(1)),
        };
      }
      return d;
    },
    setHaptics(name) {
      haptics = Object.hasOwn(HAPTIC_PATTERNS, name) ? HAPTIC_PATTERNS[name]
                                                     : HAPTIC_PATTERNS.crisp;
    },
    isBusy() {
      return running;
    },
    // A jumpTo settle in flight, as opposed to a gesture or its momentum tail.
    isGliding() {
      return running && machine.gliding &&
          machine.state === S.settlingCommit;
    },
    // The committed space. Not the one on screen mid-slide - the one the
    // machine considers current, which is what a relative jump steps from.
    spaceIndex() {
      return machine.spaceIndex;
    },
    // Where the rail is headed. spaceIndex does not move until it arrives, so
    // anything deciding what a NEXT press should do reads this instead - a
    // second press inside the glide otherwise answers about the space it left.
    targetIndex() {
      return machine.state === S.settlingCommit ? machine.settleTargetIndex
                                                : machine.spaceIndex;
    },
    spaceCount() {
      return spaceCount;
    },
    // Returns the space it will land on, which is not the current one until
    // the slide finishes.
    jumpTo(i) {
      // SelectSpace echoes a glide back at the space the rail announced, and
      // that now lands mid-settle. Restarting the settle would clear the
      // momentum-tail suppression and end the scrub uncommitted.
      if (machine.state === S.settlingCommit &&
          machine.settleTargetIndex === i) {
        return i;
      }
      // The browser cross-fades a jump itself. A drag's scrub still in
      // flight would ride the jump's settle toward a space the jump is not
      // going to.
      settleScrub();
      const target = machine.jumpTo(i);
      start();
      return target;
    },
    // the settings surface pushes a preset into a sidebar it
    // does not share a page with, so applying it live has to go through this
    // rather than a constructor argument.
    setTuning(next) {
      machine.setTuning(next);
      Object.assign(tune, next);
    },
  };
}
