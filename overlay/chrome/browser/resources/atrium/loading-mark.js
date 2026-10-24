// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//
// a click on a row said nothing until the page arrived. The lab
// marks the row .loading and lets the comet sweep run on its ring until the
// load settles (swipe-lab.html:3818-3826).

// How long a mark survives without being renewed. The lab's own window: long
// enough that a slow page keeps its ring, short enough that a load which never
// reports back does not leave a row spinning for the session.
export const LOADING_TIMEOUT_MS = 1500;

// A ring that leaves before this reads as a flicker: a local page loads in
// under 100ms. One full turn of the comet (cometspin, 1.1s) is the least
// that reads as a sweep.
export const MIN_RING_MS = 1100;

// the timers, bound to the global that owns them.
//
// `{setTimeout, clearTimeout}` looks equivalent and is not. It copies the two
// globals as plain properties, so calling clock.setTimeout(...) passes
// this === clock, and a real browser refuses that with "Illegal invocation" -
// window.setTimeout has to be called on window. The throw landed inside
// markLoading, which sidebar.js calls BEFORE chrome.send, so every live-row
// click aborted before it sent anything and the row did nothing at all.
//
// It survived because the DOM shim's setTimeout is a plain function with no
// receiver check, so the tests were happy, and the VM scenarios that click rows
// all assert the COLD path - the one branch that does not call markLoading.
const PLATFORM_CLOCK = {
  setTimeout: (...args) => setTimeout(...args),
  clearTimeout: (...args) => clearTimeout(...args),
  now: () => Date.now(),
};

// Marks `el` loading and clears it after LOADING_TIMEOUT_MS unless something
// clears it sooner. Returns a function that settles it immediately, which is
// what a real load-complete signal calls.
export function markLoading(el, {clock = PLATFORM_CLOCK} = {}) {
  if (!el) {
    return () => {};
  }
  el.classList.add('loading');
  // A second click on the same row restarts the window rather than stacking a
  // second timer that would settle the first one's ring early.
  if (el._loadingTimer !== undefined) {
    clock.clearTimeout(el._loadingTimer);
  }
  const settle = () => {
    if (el._loadingTimer !== undefined) {
      clock.clearTimeout(el._loadingTimer);
      el._loadingTimer = undefined;
    }
    el.classList.remove('loading');
  };
  el._loadingTimer = clock.setTimeout(settle, LOADING_TIMEOUT_MS);
  return settle;
}

// every snapshot push rebuilds the rows, and the first one lands
// inside the window, so a mark on the element showed for one frame.
const loadingKeys = new Map();

// a cold row's click knows only its url, the same way a cold
// favourite's does (favId, above) - so a row has to answer to its url too, or
// a mark keyed by url never finds it.
function keysOf(el) {
  return [el.dataset.tabId, el.dataset.favTab, el.dataset.favId, el.dataset.url]
      .filter(Boolean);
}

function elementsFor(root, key) {
  return [...root.querySelectorAll('.row, .fav')].filter(
      el => keysOf(el).includes(key));
}

// the keys the focused tab answers to - its id, and the url its
// favourite tile is keyed by. A cold favourite click knows only the url, and
// the snapshot that follows knows the id, so both have to count as focus.
const activeKeys = new Set();

// Asked of the ELEMENT, not of the key the push carried: a tile answers to its
// url and to its tab id, and a load reports only one of the two. Empty means no
// snapshot has named a focused tab yet, and every ring runs.
function ringable(el) {
  if (activeKeys.size === 0) {
    return true;
  }
  return keysOf(el).some((key) => activeKeys.has(key));
}

// How long a click's claim on focus outlives a push that disagrees with it. The
// snapshot in flight when the click lands still names the tab being left, and
// adopting it would blank the ring the click just drew until the next push.
export const FOCUS_CLAIM_MS = 2000;

let claimUntil = 0;
// The selection a claim replaced. Only a push still naming THAT is the one in
// flight: a push naming some third tab is a focus move from elsewhere - a
// chord, another window - and has to land.
const supersededKeys = new Set();

// The focus change. Any ring on a row that is not the focused one goes at once,
// and a ring the focused row is owed comes back. `claimed` marks the caller as
// a click, which speaks for the user before any push has caught up.
export function setActiveKeys(keys, root,
                              {claimed = false, clock = PLATFORM_CLOCK} = {}) {
  const next = new Set();
  for (const key of keys || []) {
    if (key) {
      next.add(String(key));
    }
  }
  const stale = !claimed && clock.now() < claimUntil &&
      [...next].every((key) => supersededKeys.has(key));
  if (!stale) {
    if (claimed) {
      supersededKeys.clear();
      for (const key of activeKeys) {
        supersededKeys.add(key);
      }
    }
    activeKeys.clear();
    for (const key of next) {
      activeKeys.add(key);
    }
    claimUntil = claimed ? clock.now() + FOCUS_CLAIM_MS : 0;
  }
  if (root) {
    lastRoot = root;
  }
  if (lastRoot) {
    refreshMarks(lastRoot, clock, /*sweep=*/ true);
  }
}

export function markLoadingKey(key, root, {clock = PLATFORM_CLOCK} = {}) {
  if (!key || !root) {
    return;
  }
  const stale = loadingKeys.get(key);
  if (stale) {
    clock.clearTimeout(stale.timer);
  }
  lastRoot = root;
  const entry = {root};
  if (!ringShownAt.has(key)) {
    ringShownAt.set(key, clock.now());
  }
  for (const el of elementsFor(root, key).filter(ringable)) {
    el.classList.add('loading');
  }
  entry.timer = clock.setTimeout(() => {
    loadingKeys.delete(key);
    if (!liveKeys.has(key)) {
      ringShownAt.delete(key);
    }
    for (const el of elementsFor(entry.root, key)) {
      el.classList.remove('loading');
    }
  }, LOADING_TIMEOUT_MS);
  loadingKeys.set(key, entry);
}

// Keys the browser reports as loading, from DidStartLoading to DidStopLoading.
// No timer: the browser says when the load stops, whatever started it, so a
// Cmd+R reload and a load longer than the click window both show the ring.
const liveKeys = new Set();

// The fill angle each key's ring has reached, so a re-render can put it back.
// The class alone is not enough: the element is new after every snapshot.
const phaseByKey = new Map();

function applyPhase(el, entry) {
  if (entry && entry.phase === 'receiving') {
    el.classList.add('receiving');
    return;
  }
  el.classList.remove('receiving');
}

// The second phase of the ring. atriumTabLoading still owns whether a ring
// shows at all - this swaps its CSS from a pulse to a rotating sweep.
// `fraction` only feeds the docked field's own fill, in sidebar.js.
export function setLoadingPhase(key, root, phase, fraction,
                                {clock = PLATFORM_CLOCK} = {}) {
  if (!key || !root) {
    return;
  }
  lastRoot = root;
  // The ring outlives 'done' by the rest of MIN_RING_MS, and clearing the sweep
  // here spent that hold replaying the pulse, so a load ended on phase 1.
  // endKey owns the teardown.
  if (phase === 'done') {
    return;
  }
  const entry = phase === 'receiving' ? {phase} : null;
  if (entry) {
    phaseByKey.set(key, entry);
  } else {
    phaseByKey.delete(key);
  }
  for (const el of elementsFor(root, key).filter(ringable)) {
    applyPhase(el, entry);
  }
}

// When each key's ring first showed, so a stop can hold it to MIN_RING_MS.
const ringShownAt = new Map();
const holdTimers = new Map();
// The rows the last call saw: a held stop lands after a re-render.
let lastRoot = null;

function endKey(key, clock) {
  const root = lastRoot;
  liveKeys.delete(key);
  ringShownAt.delete(key);
  holdTimers.delete(key);
  phaseByKey.delete(key);
  // The click's own mark on the same key ends here too: the browser's stop
  // is the real signal the click window only stood in for.
  const clicked = loadingKeys.get(key);
  if (clicked) {
    clock.clearTimeout(clicked.timer);
    loadingKeys.delete(key);
  }
  for (const el of elementsFor(root, key)) {
    el.classList.remove('loading');
    applyPhase(el, null);
  }
}

export function setLoadingKey(key, root, loading, {clock = PLATFORM_CLOCK} = {}) {
  if (!key || !root) {
    return;
  }
  lastRoot = root;
  if (loading) {
    const held = holdTimers.get(key);
    if (held !== undefined) {
      clock.clearTimeout(held);
      holdTimers.delete(key);
    }
    liveKeys.add(key);
    if (!ringShownAt.has(key)) {
      ringShownAt.set(key, clock.now());
    }
    for (const el of elementsFor(root, key).filter(ringable)) {
      el.classList.add('loading');
    }
    return;
  }
  const shown = ringShownAt.get(key);
  const left = shown === undefined ? 0 : MIN_RING_MS - (clock.now() - shown);
  if (left > 0) {
    if (!holdTimers.has(key)) {
      holdTimers.set(key, clock.setTimeout(() => endKey(key, clock), left));
    }
    return;
  }
  endKey(key, clock);
}

// One pass over the rebuilt rows, keyed. The marks used to re-query the tree
// per key, and a snapshot carries as many keys as it has loading tabs.
function markIndex(root) {
  const elements = [...root.querySelectorAll('.row, .fav')];
  const byKey = new Map();
  for (const el of elements) {
    for (const key of keysOf(el)) {
      const found = byKey.get(key);
      if (found) {
        found.push(el);
      } else {
        byKey.set(key, [el]);
      }
    }
  }
  return {elements, byKey};
}

// A tab closed mid-load never reports its stop, so nothing else drains its key.
// The age gate is what separates that from a key whose row has not been
// rendered yet: a load push can arrive before the snapshot carrying its row.
function pruneDepartedKeys(byKey, clock) {
  for (const key of [...liveKeys]) {
    const shown = ringShownAt.get(key);
    if (byKey.has(key) || clock.now() - (shown ?? 0) < LOADING_TIMEOUT_MS) {
      continue;
    }
    liveKeys.delete(key);
    ringShownAt.delete(key);
    phaseByKey.delete(key);
    const held = holdTimers.get(key);
    if (held !== undefined) {
      clock.clearTimeout(held);
      holdTimers.delete(key);
    }
  }
}

function refreshMarks(root, clock, sweep) {
  lastRoot = root;
  const {elements, byKey} = markIndex(root);
  pruneDepartedKeys(byKey, clock);
  if (sweep) {
    for (const el of elements) {
      // No key at all means markLoading owns this one, with its own timer.
      if (keysOf(el).length && !ringable(el)) {
        el.classList.remove('loading');
        applyPhase(el, null);
      }
    }
  }
  for (const [key, entry] of loadingKeys) {
    entry.root = root;
    for (const el of (byKey.get(key) || []).filter(ringable)) {
      el.classList.add('loading');
    }
  }
  for (const key of liveKeys) {
    for (const el of (byKey.get(key) || []).filter(ringable)) {
      el.classList.add('loading');
      applyPhase(el, phaseByKey.get(key) || null);
    }
  }
}

// The render calls this with the rebuilt rows, which is where a live mark
// belongs now and where its timer clears it.
export function reapplyLoadingMarks(root, {clock = PLATFORM_CLOCK} = {}) {
  refreshMarks(root, clock, /*sweep=*/ false);
}

// Clears every mark under `root`. Called before a re-render replaces the rows,
// so a mark cannot outlive the element that carried it.
export function clearLoadingMarks(root, {clock = PLATFORM_CLOCK} = {}) {
  for (const el of root.querySelectorAll('.loading')) {
    if (el._loadingTimer !== undefined) {
      clock.clearTimeout(el._loadingTimer);
      el._loadingTimer = undefined;
    }
    el.classList.remove('loading');
  }
}
