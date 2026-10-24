// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// press versus hold on one control.
//
// Holding Back opens the history scrubber and a short press still navigates,
// so the rule that keeps them apart is the part worth testing on its own. It
// lives here rather than in C++ because the control lives in this surface, so
// the timer does too - a second copy of these rules on the other side of the
// pipe would be one more thing to keep in step.

// The lab's own holdSilenceGap (docs/lab/swipe-lab.html:2390), so a hold feels
// the same length here as it does there rather than being guessed twice.
export const HOLD_THRESHOLD_MS = 500;

// How far a pointer may drift and still count as held. No lab constant behind
// this one, unlike the threshold: the lab's drag commit is a fraction of the
// sidebar width and does not convert to a pointer radius. Tunable.
export const HOLD_SLOP_PX = 6;

export const PENDING = 'pending';
export const PRESS = 'press';
export const HOLD = 'hold';
export const CANCELLED = 'cancelled';

// One control's press. `down: false` means no press is live.
export function newHoldState() {
  return {down: false, fired: false, downAt: 0, originX: 0, originY: 0};
}

// A fresh press, even if the last one was never released - a lost pointerup
// must not leave a hold armed for the next press to inherit.
export function onPointerDown(state, atMs, x, y) {
  state.down = true;
  state.fired = false;
  state.downAt = atMs;
  state.originX = x;
  state.originY = y;
  return PENDING;
}

export function onPointerMove(state, x, y) {
  if (!state.down) {
    return PENDING;
  }
  const dx = x - state.originX;
  const dy = y - state.originY;
  if (Math.hypot(dx, dy) <= HOLD_SLOP_PX) {
    return PENDING;
  }
  // A drag is neither gesture. Ending the press here is what stops a swipe
  // that happens to start on this control from opening the scrubber.
  state.down = false;
  return CANCELLED;
}

export function onHoldTimerTick(state, atMs) {
  if (!state.down || state.fired) {
    return PENDING;
  }
  if (atMs - state.downAt < HOLD_THRESHOLD_MS) {
    return PENDING;
  }
  // Latched, not cleared: the press stays live so the pointer keeps being
  // tracked, but this can never fire twice.
  state.fired = true;
  return HOLD;
}

export function onPointerUp(state) {
  const wasDown = state.down;
  const hadFired = state.fired;
  state.down = false;
  state.fired = false;
  if (!wasDown) {
    return CANCELLED;
  }
  if (hadFired) {
    // The scrubber is open. Releasing commits whatever it has selected, and
    // navigating back as well would move the page twice.
    return CANCELLED;
  }
  // Deliberately not checking the elapsed time here. A held press whose timer
  // never ran is still a press: a dropped timer must lose the scrubber, not
  // the back button.
  return PRESS;
}
