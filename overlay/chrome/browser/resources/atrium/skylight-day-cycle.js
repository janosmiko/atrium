// Copyright 2026 The Atrium Authors
//
// Ported from swipe-lab.html:2078-2100 and :2593-2598.

import {REAL_INTERVAL_CLOCK} from './clock.js';

const KEYFRAMES = [
  [0, -.15, .72], [4, -.2, .78], [6, -.55, .38], [9, -.12, .06],
  [13, 0, 0], [16, .12, .02], [18.5, .68, .12], [20.5, .3, .42],
  [22.5, -.05, .66], [24, -.15, .72],
];

/** Piecewise-linear day cycle at `hour` (wraps to 0-24). */
export function skyCurve(hour) {
  const h = ((hour % 24) + 24) % 24;
  let a = KEYFRAMES[0];
  let b = KEYFRAMES[KEYFRAMES.length - 1];
  for (let i = 0; i < KEYFRAMES.length - 1; i++) {
    if (h >= KEYFRAMES[i][0] && h <= KEYFRAMES[i + 1][0]) {
      a = KEYFRAMES[i];
      b = KEYFRAMES[i + 1];
      break;
    }
  }
  const f = (h - a[0]) / ((b[0] - a[0]) || 1);
  return {warm: a[1] + (b[1] - a[1]) * f, dark: a[2] + (b[2] - a[2]) * f};
}

// Floor of 0.02 matches the lab - below it the layer would paint but not
// read as visible, so it is omitted rather than pushed at a dead alpha.
export function dayCycleLayers(hour) {
  const {warm, dark} = skyCurve(hour);
  const layers = [];
  if (Math.abs(warm) > 0.02) {
    const wc = warm > 0 ? '255,140,60' : '90,150,255';
    const wa = (Math.abs(warm) * 0.16).toFixed(3);
    layers.push(`linear-gradient(rgba(${wc},${wa}),rgba(${wc},${wa}))`);
  }
  if (dark > 0.02) {
    const da = (dark * 0.52).toFixed(3);
    layers.push(`linear-gradient(rgba(5,9,15,${da}),rgba(5,9,15,${da}))`);
  }
  return layers;
}

function liveHour() {
  const now = new Date();
  return now.getHours() + now.getMinutes() / 60;
}

// Ported from spaceBG(): sky layers paint in front of `base`. Night Shift
// defaults on, matching the lab's `??true`.
export function spaceDayCycleBackground(base, {nightShift = true, hour} = {}) {
  if (!nightShift) {
    return base;
  }
  const sky = dayCycleLayers(hour ?? liveHour());
  return sky.length ? sky.join(',') + ',' + base : base;
}

// Repaints every 60s off the real clock, matching the lab's own
// setInterval creep - a demo `hour` freezes it at one reading instead.
export function setSkylightDayCycle(base, {host, nightShift = true, hour,
    clock = REAL_INTERVAL_CLOCK, now = () => new Date(),
    background = spaceDayCycleBackground} = {}) {
  const el = host || document.getElementById('atrium-root');
  if (!el) {
    return null;
  }
  if (el._skyClockHandle !== undefined) {
    clock.clear(el._skyClockHandle);
    delete el._skyClockHandle;
  }

  const currentHour = () => {
    const d = now();
    return d.getHours() + d.getMinutes() / 60;
  };
  const paint = (h) => {
    el.style.background = background(base, {nightShift, hour: h});
  };

  if (hour !== undefined || !nightShift) {
    paint(hour);
    return el;
  }
  paint(currentHour());
  el._skyClockHandle = clock.set(60000, () => paint(currentHour()));
  return el;
}
