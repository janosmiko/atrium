// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The four hint triggers: a hold, a modifier held over a row, a click on an
// action that has a hotkey, and a hover over a nav or ext button.

import {showHint, hideHint, currentHint} from './hint.js';
import {
  HOLD, HOLD_THRESHOLD_MS,
  newHoldState, onHoldTimerTick, onPointerDown, onPointerMove, onPointerUp,
} from './hold-gesture.js';
import {REAL_TIMEOUT_CLOCK} from './clock.js';

const HOVER_DELAY_MS = 600;

// The hold, modifier and post-action hints name gestures the sidebar cannot
// do yet (peek from a hold, split from Shift-click), and until those land the
// hints would teach nothing. Hover is unaffected and always installs live.
export const GESTURE_HINTS_LIVE = false;

// `option` is a per-call override (tests, mainly). Otherwise a scenario can
// flip the page-wide dataset flag without an install-time option to pass.
function gestureHintsLive(option) {
  if (option !== undefined) {
    return option;
  }
  if (typeof document !== 'undefined' &&
      document.documentElement?.dataset.gestureHints === 'live') {
    return true;
  }
  return GESTURE_HINTS_LIVE;
}

// Button class -> hotkey action id, from atrium_native_hotkey_defaults.cc.
// Anything absent here has no rebindable action and falls back to FALLBACK_KEYS.
const ACTION_BY_CLASS = {
  sbtoggle: 'toggleSidebar',
  sbreload: 'reload',
  sechdrclear: 'clearTabs',
};

// sbreload holds only until setBindings arrives - bindingFor wins above. The
// rest are accelerators_cocoa.mm's and IsCopyLinkKeyEquivalent's, unrebindable.
// Cmd+arrow runs back and forward too but is kept out of the main menu.
const FALLBACK_KEYS = {
  sbback: '⌘[',
  sbfwd: '⌘]',
  sbreload: '⌘R',
  dvcopy: '⌘⇧C',
};

// chrome.send name -> the action id its hotkey would run and the label the
// post-action hint's text names. An entry with no action id documents intent
// only, until the core names its hotkey.
const POST_ACTION_TABLE = {
  sidebarToggleRequested: {action: 'toggleSidebar', label: 'Toggle sidebar'},
  sidebarNewTab: {action: undefined, label: 'New Tab'},
  sidebarBackPressed: {action: undefined, label: 'Back'},
  sidebarForwardPressed: {action: undefined, label: 'Forward'},
  sidebarReloadPressed: {action: 'reload', label: 'Reload'},
  sidebarSpaceCreated: {action: undefined, label: 'New Space'},
};

let bindings = {};

export function setBindings(map) {
  bindings = map || {};
}

export function bindingFor(actionId) {
  return actionId ? bindings[actionId] : undefined;
}

function classesOf(button) {
  return String(button?.className || '').split(/\s+/);
}

// A modifier variant (Option, Cmd+Shift) has no key of its own, and the
// class stays fixed across the swap - a plain allowlist, not FALLBACK_KEYS.
const NAV_MODIFIER_CLASSES = new Set(['sbback', 'sbfwd', 'sbreload']);

// True while `button` sits in a row whose modifier tracker (sidebar-nav-row.js
// createModifierTracker) has armed a non-plain variant - .sbnav for the row,
// .dvnav for the floating bar's copy of it, both set by the same tracker.
function inModifierVariant(button) {
  return classesOf(button).some((cls) => NAV_MODIFIER_CLASSES.has(cls)) &&
      Boolean(button.closest?.('.sbnav, .dvnav')?.dataset.mod);
}

// The registry first, so a rebound key wins over a literal. Shared with the nav
// overflow menu, which must never name a different key than the hover hint.
export function keysForButton(button) {
  for (const cls of classesOf(button)) {
    const bound = bindingFor(ACTION_BY_CLASS[cls]);
    if (bound) {
      return bound;
    }
  }
  for (const cls of classesOf(button)) {
    if (FALLBACK_KEYS[cls]) {
      return FALLBACK_KEYS[cls];
    }
  }
  return '';
}

// sidebar-nav-row.js's paint() calls this instead of an import back here -
// it is the one funnel both a real keydown and the native flagsChanged push
// (which fires no DOM event at all) both run through.
let activeHoverRefresh = null;

export function refreshHoverHint() {
  activeHoverRefresh?.();
}

// Every surface whose own view clips a bubble at its edge, mapped to its
// hint.js NATIVE_MESSAGES key. The window translates the anchor rect out of
// the sending document's client space, so the two cannot share one name.
const NATIVE_HINT_ROUTES = {
  '/sidebar': 'sidebar',
  '/devbar': 'devBar',
};

// `option` is a per-call override (tests). Otherwise the route decides, and
// a hint on one of them goes through HintOverlayWidget rather than rendering
// locally - see hint.js's showHint native branch.
function nativeHintHost(option) {
  if (option !== undefined) {
    return option;
  }
  if (typeof location === 'undefined') {
    return false;
  }
  return NATIVE_HINT_ROUTES[location.pathname] || false;
}

// One trigger's own hint, so hiding it never claims a bubble a different
// trigger (or a caller outside this module) put up in the meantime.
function ownedHint(clock, host, native) {
  let el = null;
  return {
    show(options) {
      el = showHint(options, {clock, host, native});
    },
    hide() {
      if (el && currentHint() === el) {
        hideHint();
      }
      el = null;
    },
  };
}

// Hold: a tab row pressed past HOLD_THRESHOLD_MS without enough drift to
// cancel it, using the same state machine the Back button's scrubber does.
function installHold(host, clock, gestureHintsLiveOption, nativeHintOption) {
  const owner = ownedHint(clock, host, nativeHintHost(nativeHintOption));
  const state = newHoldState();
  let row = null;
  let timer = null;

  const stopTimer = () => {
    if (timer !== null) {
      clock.clear(timer);
      timer = null;
    }
  };

  const onDown = (event) => {
    const target = event.target?.closest?.('.row.tabrow');
    if (!target) {
      return;
    }
    row = target;
    const downAt = event.timeStamp ?? 0;
    onPointerDown(state, downAt, event.clientX ?? 0, event.clientY ?? 0);
    stopTimer();
    // One shot: the hold latches in onHoldTimerTick, so a second tick could
    // only ever report PENDING.
    timer = clock.set(HOLD_THRESHOLD_MS, () => {
      timer = null;
      // The timer itself already gates the elapsed time, so this timestamp
      // only satisfies onHoldTimerTick's signature, it never gates twice.
      if (onHoldTimerTick(state, downAt + HOLD_THRESHOLD_MS) === HOLD &&
          gestureHintsLive(gestureHintsLiveOption)) {
        owner.show(
            {text: 'Hold to open in a peek', keys: 'hold', anchor: row});
      }
    });
  };
  const onMove = (event) => {
    if (!state.down) {
      return;
    }
    onPointerMove(state, event.clientX ?? 0, event.clientY ?? 0);
    if (!state.down) {
      stopTimer();
      owner.hide();
    }
  };
  const onRelease = () => {
    stopTimer();
    onPointerUp(state);
    owner.hide();
    row = null;
  };

  host.addEventListener('pointerdown', onDown);
  host.addEventListener('pointermove', onMove);
  host.addEventListener('pointerup', onRelease);
  host.addEventListener('pointercancel', onRelease);

  return () => {
    stopTimer();
    host.removeEventListener('pointerdown', onDown);
    host.removeEventListener('pointermove', onMove);
    host.removeEventListener('pointerup', onRelease);
    host.removeEventListener('pointercancel', onRelease);
  };
}

const MODIFIER_HINTS = {
  shift: {text: 'Open in a split', keys: '⇧click'},
  option: {text: 'Open in a peek', keys: '⌥click'},
};

// Modifier: Shift or Option held while the pointer sits over a row. The keys
// arrive on window, same as sidebar-nav-row.js's own modifier tracker,
// because a chord held before the pointer enters a row must still be seen.
function installModifier(host, clock, gestureHintsLiveOption, nativeHintOption) {
  const owner = ownedHint(clock, host, nativeHintHost(nativeHintOption));
  let shiftDown = false;
  let optionDown = false;
  let hoveredRow = null;
  let shown = null;

  const update = () => {
    const mode = !hoveredRow
        ? null
        : shiftDown ? 'shift' : optionDown ? 'option' : null;
    if (!mode) {
      shown = null;
      owner.hide();
      return;
    }
    // A pointermove fires on every pixel of drift, so a rebuild here for the
    // same row and mode would churn the bubble on each one.
    if (shown && shown.mode === mode && shown.row === hoveredRow) {
      return;
    }
    shown = {mode, row: hoveredRow};
    if (!gestureHintsLive(gestureHintsLiveOption)) {
      owner.hide();
      return;
    }
    const {text, keys} = MODIFIER_HINTS[mode];
    owner.show({text, keys, anchor: hoveredRow});
  };

  const onKeyDown = (event) => {
    if (event.key === 'Shift') {
      shiftDown = true;
    } else if (event.key === 'Alt') {
      optionDown = true;
    } else {
      return;
    }
    update();
  };
  const onKeyUp = (event) => {
    if (event.key === 'Shift') {
      shiftDown = false;
    } else if (event.key === 'Alt') {
      optionDown = false;
    } else {
      return;
    }
    update();
  };
  // A key released off-window or while the surface is hidden delivers no
  // keyup, and the hint would otherwise outlive the chord.
  const onBlur = () => {
    shiftDown = false;
    optionDown = false;
    update();
  };
  const onMove = (event) => {
    hoveredRow = event.target?.closest?.('.row.tabrow') || null;
    update();
  };

  window.addEventListener('keydown', onKeyDown);
  window.addEventListener('keyup', onKeyUp);
  window.addEventListener('blur', onBlur);
  document.addEventListener('visibilitychange', onBlur);
  host.addEventListener('pointermove', onMove);

  return () => {
    window.removeEventListener('keydown', onKeyDown);
    window.removeEventListener('keyup', onKeyUp);
    window.removeEventListener('blur', onBlur);
    document.removeEventListener('visibilitychange', onBlur);
    host.removeEventListener('pointermove', onMove);
  };
}

// `send` is wrapped rather than read off the DOM event, because the six call
// sites span two modules that each already take a sender callback.
function installPostAction(host, clock, send, gestureHintsLiveOption, nativeHintOption) {
  const owner = ownedHint(clock, host, nativeHintHost(nativeHintOption));
  const shown = new Set();

  const wrapped = (name, args, {fromClick = false} = {}) => {
    const result = send(name, args);
    const entry = POST_ACTION_TABLE[name];
    if (fromClick && entry && !shown.has(name) &&
        gestureHintsLive(gestureHintsLiveOption)) {
      const binding = bindingFor(entry.action);
      if (binding) {
        shown.add(name);
        owner.show({
          text: `Next time: ${entry.label}`,
          keys: binding,
          anchor: 'top-right',
          ttlMs: 3000,
        });
      }
    }
    return result;
  };

  return {send: wrapped};
}

// a surface that draws its own hints shows no native tooltip, from
// a button or from anything above it. The label is not lost - labelFor reads
// the parked copy, and the address row keeps its origin in dataset.origin.
const NAMEABLE_TAGS = new Set(['A', 'BUTTON', 'IMG', 'INPUT', 'SELECT', 'TEXTAREA']);
const UNNAMEABLE_ROLES = new Set(['generic', 'none', 'presentation']);

// Blink DCHECKs, and so kills the WebUI renderer, on an aria-label over an
// element whose role cannot carry a name - a plain div or span.
function mayCarryName(el) {
  const role = el.getAttribute('role');
  return role ? !UNNAMEABLE_ROLES.has(role)
              : NAMEABLE_TAGS.has(el.tagName.toUpperCase());
}

function parkTitle(el) {
  if (el.nodeType === 1 && el.title) {
    el.dataset.hintTitle = el.title;
    // `title` is the accessible name of a control whose only content is a
    // glyph, so taking it away silently unnames it for a screen reader.
    if (!el.getAttribute('aria-label') && mayCarryName(el)) {
      el.setAttribute('aria-label', el.title);
    }
    el.title = '';
  }
}

function parkTitlesUnder(host) {
  parkTitle(host);
  for (const el of host.querySelectorAll('[title]')) {
    parkTitle(el);
  }
}

// The pointer's own path, walked on every pointerover. macOS shows the nearest
// title at or above the target, and this runs long before the tooltip's own
// delay, so it covers a title the observer below has not reached yet.
function parkTitlesOnPath(target, host) {
  const stop = host.parentNode;
  for (let el = target; el && el !== stop; el = el.parentNode) {
    parkTitle(el);
  }
}

// The surface sets titles long after the install - the address row's origin on
// every navigation, the pin's label on every toggle - so the sweep has to keep
// running rather than happen once.
function watchTitles(host) {
  if (typeof MutationObserver !== 'function') {
    return () => {};
  }
  const observer = new MutationObserver((records) => {
    for (const record of records) {
      if (record.type === 'attributes') {
        parkTitle(record.target);
        continue;
      }
      for (const added of record.addedNodes) {
        if (added.nodeType === 1) {
          parkTitlesUnder(added);
        }
      }
    }
  });
  observer.observe(host, {
    subtree: true, childList: true, attributes: true, attributeFilter: ['title'],
  });
  return () => observer.disconnect();
}

// pointerover/pointerout, not pointerenter/pointerleave: the latter do not
// bubble, so a listener on `host` would never see one from a descendant.
//
// Shared by the sidebar route and the /devbar route (called directly), and
// nativeHintHost lifts both off their own document: the floating bar draws
// above the card but is still bounded by its capsule.
export function installHover(host, clock, nativeHintOption) {
  const owner = ownedHint(clock, host, nativeHintHost(nativeHintOption));
  parkTitlesUnder(host);
  const unwatchTitles = watchTitles(host);
  let armedButton = null;
  let timer = null;
  // The button an actual hint is showing for, and what it showed - so a
  // keydown/keyup while merely armed (timer still pending) does nothing, and
  // a repeat with the same content skips the rebuild.
  let shownButton = null;
  let lastShown = null;

  // `dataset.hintTitle` is where parkTitle put the button's own `title`, and
  // `title` still answers for a button this surface has not reached yet.
  const labelFor = (button) =>
      button.getAttribute('aria-label') || button.dataset.hintTitle ||
      button.title || '';

  const actionFor = (button) => {
    for (const cls of (button.className || '').split(/\s+/)) {
      if (ACTION_BY_CLASS[cls]) {
        return ACTION_BY_CLASS[cls];
      }
    }
    return undefined;
  };


  // .dvnav/.dvlock/.dvcopy/.dvpin: the floating address bar's back/forward/
  // reload, site info, copy-url and pin buttons, so its hint stops being a
  // plain native tooltip and reads like every other hover hint.
  // .aippin/.aipclear/.aipclose/.aichatsend: the AI panel's header and chat
  // buttons. parkTitlesUnder takes the title off everything under the panel,
  // so a glyph button this list misses names itself nowhere at all.
  const targetOf = (event) =>
      event.target?.closest?.(
          '.sbnav .nb, .exs-chip, .qm-open, .dvnav .nb, .dvlock, .dvcopy, ' +
          '.dvpin, .sechdrclear, .aiquickbtn, .aiplang, .aippin, .aipclear, ' +
          '.aipclose, .aichatsend') ||
      null;

  // The AI panel's action row sits directly on the prompt field, which a
  // bubble below the row would cover while the reader is aiming at it.
  const ABOVE_TARGETS = '.aiquickbtn, .aiplang';

  const clearTimer = () => {
    if (timer !== null) {
      clock.clear(timer);
      timer = null;
    }
  };

  // A modifier variant has no key of its own, so the hint drops it rather
  // than naming the plain reload/back/forward key.
  const showFor = (button) => {
    const text = labelFor(button);
    const keys = inModifierVariant(button) ? '' : keysForButton(button);
    if (!text) {
      owner.hide();
      shownButton = null;
      lastShown = null;
      return;
    }
    if (shownButton === button && lastShown &&
        lastShown.text === text && lastShown.keys === keys) {
      return;
    }
    owner.show({
      text, keys, anchor: button, ttlMs: 4000,
      placement: button.matches?.(ABOVE_TARGETS) ? 'above' : 'below',
    });
    shownButton = button;
    lastShown = {text, keys};
  };

  const disarm = () => {
    clearTimer();
    owner.hide();
    armedButton = null;
    shownButton = null;
    lastShown = null;
  };

  const refresh = () => {
    // A button re-rendered away fires no pointerout, and a hint anchored to a
    // detached element lands at the document's origin.
    if (armedButton && !armedButton.isConnected) {
      disarm();
      return;
    }
    if (armedButton && shownButton === armedButton) {
      showFor(armedButton);
    }
  };
  activeHoverRefresh = refresh;

  const onOver = (event) => {
    parkTitlesOnPath(event.target, host);
    const button = targetOf(event);
    if (!button || button === armedButton) {
      return;
    }
    clearTimer();
    owner.hide();
    armedButton = button;
    shownButton = null;
    lastShown = null;
    timer = clock.set(HOVER_DELAY_MS, () => {
      timer = null;
      showFor(button);
    });
  };
  const onOut = (event) => {
    const button = targetOf(event);
    if (!button || button !== armedButton) {
      return;
    }
    // A move to one of the button's own children re-enters through it, not
    // away from it, and must not restart the delay or drop the hint.
    if (button.contains(event.relatedTarget)) {
      return;
    }
    clearTimer();
    owner.hide();
    armedButton = null;
    shownButton = null;
    lastShown = null;
  };

  // The press answers the question the hint was asking, and a Clear takes
  // its button away with the rows, so nothing else would drop the hint.
  const onClick = (event) => {
    if (targetOf(event) === armedButton && armedButton) {
      disarm();
    }
  };

  host.addEventListener('pointerover', onOver);
  host.addEventListener('pointerout', onOut);
  host.addEventListener('click', onClick);

  return () => {
    clearTimer();
    unwatchTitles();
    armedButton = null;
    if (activeHoverRefresh === refresh) {
      activeHoverRefresh = null;
    }
    host.removeEventListener('pointerover', onOver);
    host.removeEventListener('pointerout', onOut);
    host.removeEventListener('click', onClick);
  };
}

// Installs all four triggers on `host` and returns the wrapped sender the six
// post-action messages must be routed through, plus one dispose for all four.
export function installHintTriggers(
    host, {send = () => {}, clock = REAL_TIMEOUT_CLOCK,
           gestureHintsLive: gestureHintsLiveOption,
           nativeHint: nativeHintOption} = {}) {
  const disposeHold =
      installHold(host, clock, gestureHintsLiveOption, nativeHintOption);
  const disposeModifier =
      installModifier(host, clock, gestureHintsLiveOption, nativeHintOption);
  const {send: wrappedSend} = installPostAction(
      host, clock, send, gestureHintsLiveOption, nativeHintOption);
  const disposeHover = installHover(host, clock, nativeHintOption);

  return {
    send: wrappedSend,
    dispose() {
      disposeHold();
      disposeModifier();
      disposeHover();
    },
  };
}
