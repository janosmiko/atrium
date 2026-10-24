// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//
// The fill layer behind the address text. Chromium reports no progress
// before the first byte, so the wait is its own phase rather than the fake
// 6 percent the old native bar opened on.
import {clamp} from './clamp.js';

const PHASES = ['waiting', 'receiving', 'done'];

// Inserts a fill layer as `host`'s first child and returns the handle the
// push drives. The layer animates, so it composites on its own and the host's
// rounded clip does not reach it - each host rounds .loadfill itself.
export function createLoadFill(host) {
  const element = document.createElement('div');
  element.className = 'loadfill';
  host.insertBefore(element, host.firstChild);
  let max = 0;

  const setPhase = (phase, fraction) => {
    if (!PHASES.includes(phase)) {
      return;
    }
    element.classList.remove(...PHASES);
    element.classList.add(phase);
    if (phase === 'waiting') {
      max = 0;
      element.style.width = '100%';
      return;
    }
    if (phase === 'done') {
      max = 0;
      element.style.width = '100%';
      return;
    }
    max = Math.max(max, clamp(fraction));
    element.style.width = `${Number((max * 100).toFixed(1))}%`;
  };

  return {element, setPhase};
}
