// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// One hint bubble per document, on the shared overlay layer.

import {REAL_TIMEOUT_CLOCK} from './clock.js';
import {keycaps} from './keycaps.js';

const GAP_PX = 8;

// Written out per surface rather than built from the key: an unregistered
// chrome.send name is a FATAL browser crash, so these must stay greppable
// against kSurfaceMessages in atrium_surface_host.h.
const NATIVE_MESSAGES = {
  sidebar: ['sidebarHintShow', 'sidebarHintHide'],
  devBar: ['devBarHintShow', 'devBarHintHide'],
};

let current = null;
let timer = null;
let timerClock = null;

export function currentHint() {
  return current;
}

export function hideHint() {
  if (timer !== null && timerClock) {
    timerClock.clear(timer);
  }
  timer = null;
  timerClock = null;
  if (current) {
    // A native hint lives in HintOverlayWidget's own document, so there is no
    // node here to remove - see showHint's native branch below.
    if (current.native) {
      chrome.send(NATIVE_MESSAGES[current.native][1], []);
    } else {
      current.remove();
    }
  }
  current = null;
}

// 'above' is for an anchor whose neighbour below must stay readable - the AI
// panel's action row sits directly on the prompt field. Either side gives way
// to the other rather than leave the viewport.
export function hintTop(r, height, viewportHeight, placement = 'below') {
  const below = r.top + r.height + GAP_PX;
  const above = r.top - GAP_PX - height;
  const fits = (top) => top >= GAP_PX && top + height <= viewportHeight - GAP_PX;
  const [first, second] =
      placement === 'above' ? [above, below] : [below, above];
  if (fits(first)) {
    return first;
  }
  if (fits(second)) {
    return second;
  }
  return Math.max(GAP_PX, Math.min(below, viewportHeight - height - GAP_PX));
}

function place(el, anchor, placement) {
  if (typeof anchor === 'string') {
    el.dataset.hintAnchor = anchor;
    el.classList.add('hint-' + anchor);
    return;
  }
  el.dataset.hintAnchor = 'element';
  // An Element (getBoundingClientRect) locally, or a plain {left,top,width,
  // height} already resolved by HintOverlayWidget on the /hint route.
  const r = typeof anchor.getBoundingClientRect === 'function'
      ? anchor.getBoundingClientRect()
      : anchor;
  const width = el.offsetWidth || 0;
  const height = el.offsetHeight || 0;
  const left = Math.max(GAP_PX, Math.min(r.left, window.innerWidth - width - GAP_PX));
  const top = hintTop(r, height, window.innerHeight, placement);
  el.style.setProperty('--hint-left', `${left}px`);
  el.style.setProperty('--hint-top', `${top}px`);
  el.classList.add('hint-at');
}

let nativeSeq = 0;

// The bubble sits beside #atrium-root, which a snapshot push replaces wholesale.
// `native` names the sender in NATIVE_MESSAGES: an element anchor overhangs
// and the surface's own view clips it, a corner anchor cannot and stays local.
export function showHint({text, keys = '', anchor = 'bottom-center', ttlMs = 2500,
                          placement = 'below'},
                          {clock = REAL_TIMEOUT_CLOCK, host = document.body,
                           native = false} = {}) {
  hideHint();
  const messages = native ? NATIVE_MESSAGES[native] : undefined;
  if (messages && typeof anchor !== 'string') {
    const r = anchor.getBoundingClientRect();
    chrome.send(messages[0], [
      text, keys,
      Math.round(r.left), Math.round(r.top),
      Math.round(r.width), Math.round(r.height),
      ttlMs,
    ]);
    current = {native, seq: ++nativeSeq};
    return current;
  }
  const el = document.createElement('div');
  el.className = 'hint ov-card ov-hint';
  const label = document.createElement('span');
  label.className = 'hint-text';
  label.textContent = text;
  el.appendChild(label);
  if (keys) {
    const caps = keycaps(keys);
    caps.classList.add('hint-keys');
    el.appendChild(caps);
  }
  // The document's body, not `host`: the AI panel delegates its hovers from
  // #atrium-root itself, and a bubble mounted in there reads the panel's own
  // glass paper and ink instead of the overlay card every other hint wears.
  const mount = (host.ownerDocument || document).body || host;
  const root = mount.querySelector('#atrium-root');
  if (root) {
    mount.insertBefore(el, root);
  } else {
    mount.appendChild(el);
  }
  place(el, anchor || 'bottom-center', placement);
  current = el;
  timerClock = clock;
  timer = clock.set(ttlMs, () => {
    timer = null;
    if (current === el) {
      hideHint();
    }
  });
  return el;
}

// The /hint route: a bare canvas HintOverlayWidget sizes to the parent
// window, so showHint's own local rendering (native defaults false) is
// exactly what belongs here.
export function installHintOverlay() {
  window.atriumShowHint = (opts) => showHint(opts || {});
  window.atriumHideHint = () => hideHint();
  chrome.send('hintReady', []);
}
