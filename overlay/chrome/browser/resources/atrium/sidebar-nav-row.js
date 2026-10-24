// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// The pinned chrome row, ported from the lab's #sbChrome (swipe-lab.html:3655).
// Sidebar-level like the space dots, so a swipe moves the panes underneath it.

// The lab draws its own traffic dots and right-aligns back/forward/reload beside
// the chips that follow them. Here the lights are the OS window's and the chips
// are .crx-row on the opposite edge, so all four controls group on the left.

import {
  HOLD_THRESHOLD_MS,
  HOLD,
  PRESS,
  newHoldState,
  onHoldTimerTick,
  onPointerDown,
  onPointerMove,
  onPointerUp,
} from './hold-gesture.js';
import {keysForButton, refreshHoverHint} from './hint-triggers.js';
import {keycaps} from './keycaps.js';
import {refreshMenuState, registerMenu} from './menu-dismiss.js';

// No innerHTML anywhere in this tree: the WebUI CSP sets
// require-trusted-types-for 'script' with an empty allowlist.
const SVG_NS = 'http://www.w3.org/2000/svg';

function svgNode(tag, attrs) {
  const node = document.createElementNS(SVG_NS, tag);
  for (const [name, value] of Object.entries(attrs)) {
    node.setAttribute(name, value);
  }
  return node;
}

function buildButton(className, label) {
  const button = document.createElement('button');
  button.className = 'nb ' + className;
  button.type = 'button';
  button.title = label;
  button.setAttribute('aria-label', label);
  return button;
}

// The lab's own glyph wrapper (swipe-lab.html:56-63): 'display:block' on the
// svg inside kills its baseline gap, so the spin axis sits on the visual
// centre instead of a few px low.
function buildGlyph(content) {
  const g = document.createElement('span');
  g.className = 'g';
  if (typeof content === 'string') {
    g.textContent = content;
  } else {
    g.appendChild(content);
  }
  return g;
}

// Remove-reflow-add restarts a CSS animation still mid-run. The
// animationend listener below clears the class again after, so a later
// click always has something to add back.
function animateGlyph(glyph, className) {
  glyph.classList.remove(className);
  void glyph.offsetWidth;
  glyph.classList.add(className);
}

function installReplayableAnimation(glyph, className) {
  const clear = () => glyph.classList.remove(className);
  glyph.addEventListener('animationend', clear);
  // A style recalc mid-turn (e.g. prefers-reduced-motion flipping live) cancels
  // rather than ends the animation, and animationend never follows it - the
  // class would otherwise survive to freeze the glyph mid-turn.
  glyph.addEventListener('animationcancel', clear);
}

// The lab's sbToggleIcon (swipe-lab.html:2883): a panel outline whose arrow
// points where the sidebar will go. Only the hide direction is drawn - a hidden
// sidebar takes this button with it, and Cmd+S is what brings it back.
function buildToggleIcon() {
  const svg = svgNode(
      'svg',
      {class: 'sbticon', viewBox: '0 0 16 16', width: '16', height: '16'});
  svg.append(
      svgNode('rect', {
        x: '1.5', y: '2.5', width: '13', height: '11', rx: '2.2',
        fill: 'none', stroke: 'currentColor', 'stroke-width': '1.2',
      }),
      svgNode('line', {
        x1: '6.2', y1: '2.5', x2: '6.2', y2: '13.5',
        stroke: 'currentColor', 'stroke-width': '1.2',
      }),
      svgNode('path', {
        d: 'M11 5.6L8.6 8l2.4 2.4', fill: 'none', stroke: 'currentColor',
        'stroke-width': '1.4', 'stroke-linecap': 'round',
        'stroke-linejoin': 'round',
      }));
  return svg;
}

// The arrowhead reaches to y 0.68 while the arc's foot stops at 10, so the
// drawn ink sits 0.66 user units above the middle of a 0 0 12 12 box. The
// viewBox pays that back, or the arc rides high against the text glyphs.
const RELOAD_VIEW_BOX = '0 -0.66 12 12';

// The lab's reloadSVG (swipe-lab.html:2894-2907): one arc, and each modifier
// mode marks the inside of it. '' is the lab's own name for the plain mode.
function buildReloadIcon(mode = '') {
  const svg =
      svgNode('svg', {viewBox: RELOAD_VIEW_BOX, width: '14', height: '14'});
  const arc = svgNode('path', {
    d: 'M9.86 4.96A4 4 0 1 1 7.04 2.14', fill: 'none',
    stroke: 'currentColor', 'stroke-width': '1.3',
    'stroke-linecap': 'round',
  });
  // The lab's .rldash (swipe-lab.html:104): a broken arc reads as continuity
  // thrown away, which is what clearing the site's data does.
  if (mode === 'nuke') {
    arc.setAttribute('stroke-dasharray', '2.7 1.6');
  }
  svg.append(arc, svgNode('path', {
    d: 'M5.25 2.79L7.04 2.14L5.82 0.68', fill: 'none',
    stroke: 'currentColor', 'stroke-width': '1.3',
    'stroke-linecap': 'round', 'stroke-linejoin': 'round',
  }));
  if (mode === 'hard') {
    svg.append(svgNode('path', {
      class: 'rlbolt', d: 'M7.1 3.3L4.5 6.5H5.9L4.9 8.7L7.5 5.4H6.1Z',
      fill: 'currentColor',
    }));
  } else if (mode === 'nuke') {
    svg.append(
        svgNode('circle',
                {class: 'rlcrumb', cx: '4.95', cy: '5.3', r: '.85',
                 fill: 'currentColor'}),
        svgNode('circle',
                {class: 'rlcrumb', cx: '7.15', cy: '6.15', r: '.78',
                 fill: 'currentColor'}),
        svgNode('circle',
                {class: 'rlcrumb', cx: '5.7', cy: '7.6', r: '.7',
                 fill: 'currentColor'}));
  }
  return svg;
}

const TOGGLE_VARIANTS = {
  plain: {
    glyph: buildToggleIcon,
    label: 'Hide sidebar',
    message: 'sidebarToggleRequested',
  },
};

function navPath(d, width) {
  return svgNode('path', {
    d, fill: 'none', stroke: 'currentColor', 'stroke-width': width,
    'stroke-linecap': 'round', 'stroke-linejoin': 'round',
  });
}

// These six were the font's own ‹ › ⇤ ⇥ ↰ ↱, as the lab draws them
// (swipe-lab.html:3662). A font character sizes and sits by its face's metrics,
// not by the 16-unit box the toggle and reload share.

// 1.5 here renders the 1.7px of ink the reload arc's 1.3 does on its own box.
// The chevron spans a unit less than the toggle's rect: a pointed shape reads
// larger than an outlined one at equal height.
const NAV_PARTS = {
  plain: () => [navPath('M10.4 3L5.6 8l4.8 5', 1.5)],
  end: () => [
    navPath('M4.2 3v10', 1.5),
    navPath('M11.8 3.4L7.4 8l4.4 4.6', 1.5),
  ],
  newtab: () => [
    navPath('M8.6 5.4L5.2 8.8l3.4 3.4', 1.5),
    svgNode('rect', {
      x: '9.6', y: '2.2', width: '4.4', height: '4.4', rx: '1.2',
      fill: 'none', stroke: 'currentColor', 'stroke-width': '1.3',
    }),
  ],
};

// Forward is back reflected, so one drawing carries the pair and the two
// directions cannot drift apart.
function buildNavIcon(mode, forward) {
  const svg = svgNode('svg', {
    viewBox: '0 0 16 16', width: '16', height: '16', 'data-nav': mode,
  });
  const parts = NAV_PARTS[mode]();
  if (!forward) {
    svg.append(...parts);
    return svg;
  }
  const flip = svgNode('g', {transform: 'translate(16,0) scale(-1,1)'});
  flip.append(...parts);
  svg.append(flip);
  return svg;
}

// The lab's reload modes (swipe-lab.html:4748-4768), carried by all three
// controls. Shift alone claims nothing: it is Chromium's own new-window
// modifier, and taking it here would steal a click meant for a link.
const BACK_VARIANTS = {
  plain: {
    glyph: () => buildNavIcon('plain', false),
    label: 'Back',
    message: 'sidebarBackPressed',
  },
  option: {
    glyph: () => buildNavIcon('end', false),
    label: 'Back to the first page of this tab',
    message: 'sidebarBackToStart',
  },
  cmdshift: {
    glyph: () => buildNavIcon('newtab', false),
    label: 'Back in a new tab',
    message: 'sidebarBackInNewTab',
  },
};

const FORWARD_VARIANTS = {
  plain: {
    glyph: () => buildNavIcon('plain', true),
    label: 'Forward',
    message: 'sidebarForwardPressed',
  },
  option: {
    glyph: () => buildNavIcon('end', true),
    label: 'Forward to the last page of this tab',
    message: 'sidebarForwardToEnd',
  },
  cmdshift: {
    glyph: () => buildNavIcon('newtab', true),
    label: 'Forward in a new tab',
    message: 'sidebarForwardInNewTab',
  },
};

// All three are the lab's own drawing. The text glyphs that stood in for the
// modifier modes (↯, ⌦) came from a fallback face, which sized and sat them
// differently from every other glyph in the row.
const RELOAD_VARIANTS = {
  plain: {
    glyph: () => buildReloadIcon(),
    label: 'Reload',
    message: 'sidebarReloadPressed',
  },
  option: {
    glyph: () => buildReloadIcon('hard'),
    label: 'Hard reload, ignoring the cache',
    message: 'sidebarReloadHard',
  },
  cmdshift: {
    glyph: () => buildReloadIcon('nuke'),
    label: 'Hard reload, clearing this site\'s cookies and data',
    message: 'sidebarReloadClearingSite',
  },
};

function modifierFrom(event) {
  if (event.metaKey && event.shiftKey) {
    return 'cmdshift';
  }
  return event.altKey ? 'option' : 'plain';
}

// The keys arrive on window rather than on the row, so a tracker outlives the
// row that made it - installSidebarNavRow disposes the one it replaces.
export function createModifierTracker(row) {
  const painters = [];
  let state = 'plain';
  const apply = (next) => {
    if (next === state) {
      return;
    }
    state = next;
    if (next === 'plain') {
      delete row.dataset.mod;
    } else {
      row.dataset.mod = next;
    }
    for (const paint of painters) {
      paint(next);
    }
  };
  const sync = (event) => apply(modifierFrom(event));
  // A release lost to an app switch delivers no keyup, and the row would offer
  // the wider function for good. Any later event carries what is really down,
  // so an armed row resyncs from it - a bare click forces plain.
  const heal = (event) => {
    if (state !== 'plain') {
      sync(event);
    }
  };
  // Only the chord keys themselves ARM the row. Typing Option+E is an accent,
  // not a request to rearm Back - but it can still heal a state that is stale.
  const arm = (event) => {
    if (event.key === 'Alt' || event.key === 'Shift' || event.key === 'Meta') {
      sync(event);
      return;
    }
    heal(event);
  };
  // A key released while the surface is hidden or unfocused never delivers its
  // keyup, and the row would hold that state until the next chord.
  const clear = () => apply('plain');
  window.addEventListener('keydown', arm);
  window.addEventListener('keyup', sync);
  window.addEventListener('blur', clear);
  document.addEventListener('visibilitychange', clear);
  // Capture, so a stale state is gone before the button reads what to send.
  row.addEventListener('pointerdown', heal, true);
  row.addEventListener('click', heal, true);
  return {
    // Returns its own release. A group removed and rebuilt on every devbar
    // toggle would otherwise leave a painter per cycle here, each holding the
    // detached nodes it was built with.
    register: (paint) => {
      painters.push(paint);
      return () => {
        const at = painters.indexOf(paint);
        if (at >= 0) {
          painters.splice(at, 1);
        }
      };
    },
    painterCount: () => painters.length,
    current: () => state,
    // What the native flagsChanged monitor pushes. The listeners above only
    // fire while this surface holds the keyboard, which it does not while
    // someone is reading a page - they are the fallback, this is the path.
    set: (shift, cmd, option) => apply(
        modifierFrom({shiftKey: shift, metaKey: cmd, altKey: option})),
    dispose() {
      window.removeEventListener('keydown', arm);
      window.removeEventListener('keyup', sync);
      window.removeEventListener('blur', clear);
      document.removeEventListener('visibilitychange', clear);
      row.removeEventListener('pointerdown', heal, true);
      row.removeEventListener('click', heal, true);
    },
  };
}

// Greyed rather than dropped: a direction the tab cannot take keeps its slot,
// so the group does not shuffle under the pointer as history runs out.
const NAV_DIM = 'navdim';

function navDead(button) {
  return button.classList.contains(NAV_DIM);
}

// `container` is the row or the floating bar, whichever hosts the group. Both
// directions default to live: greying on a guess would be wrong for as long as
// the window's first push takes to arrive.
export function applyNavAbility(container, ability) {
  const {back = true, forward = true} = ability || {};
  container.querySelector('.sbback')?.classList.toggle(NAV_DIM, !back);
  container.querySelector('.sbfwd')?.classList.toggle(NAV_DIM, !forward);
}

// A tracker outlives every group built against it, so dropping a button has to
// drop its painter too - releaseNavButton is that half.
const painterOffByButton = new WeakMap();

function releaseNavButton(button) {
  painterOffByButton.get(button)?.();
  painterOffByButton.delete(button);
}

// Longer than the .12s the stylesheet gives the fade, so this only ever
// fires for a transition that never ran.
const GHOST_FALLBACK_MS = 200;

// Returns the reader a click uses to pick its function. The painter swaps the
// glyph NODE, leaving .g itself in place - that wrapper owns the animations.

// The outgoing glyph moves into a sibling ghost span, not a child of .g -
// a reader of .g's own content must see only the new glyph, never both.
function useVariants(button, glyph, variants, tracker, {reducedMotion = false} = {}) {
  const nodes = {};
  for (const [state, variant] of Object.entries(variants)) {
    nodes[state] = typeof variant.glyph === 'function'
        ? variant.glyph()
        : document.createTextNode(variant.glyph);
  }
  glyph.classList.add('gswap');
  let ghost = null;
  let ghostTimer = 0;
  let entryFrame = 0;
  const onGhostEnd = (event) => {
    if (event.target === ghost && event.propertyName === 'opacity') {
      dropGhost();
    }
  };
  function dropGhost() {
    if (entryFrame) {
      cancelAnimationFrame(entryFrame);
      entryFrame = 0;
    }
    if (ghostTimer) {
      clearTimeout(ghostTimer);
      ghostTimer = 0;
    }
    if (ghost) {
      ghost.removeEventListener('transitionend', onGhostEnd);
      ghost.removeEventListener('transitioncancel', onGhostEnd);
      ghost.remove();
      ghost = null;
    }
  }
  const paint = (state) => {
    const variant = variants[state] || variants.plain;
    const node = nodes[state] || nodes.plain;
    button.title = variant.label;
    button.setAttribute('aria-label', variant.label);
    refreshHoverHint();
    if (glyph.firstChild === node) {
      return;
    }
    const first = !glyph.firstChild;
    dropGhost();
    if (reducedMotion || first) {
      glyph.replaceChildren(node);
      return;
    }
    const outgoing = document.createElement('span');
    outgoing.className = 'gl ghost';
    outgoing.append(...glyph.childNodes);
    // After .g, not before: a selector reaching past .g for the drawn glyph
    // (e.g. '.sbreload svg') must still find the CURRENT one first.
    button.appendChild(outgoing);
    glyph.classList.add('entering');
    glyph.replaceChildren(node);
    void glyph.offsetWidth;
    // rAF, not the same task: without it both class changes land in one
    // style pass and the fade has no earlier state to run from (sidebar-flip.js).
    entryFrame = requestAnimationFrame(() => {
      entryFrame = 0;
      glyph.classList.remove('entering');
      outgoing.classList.add('out');
    });
    outgoing.addEventListener('transitionend', onGhostEnd);
    outgoing.addEventListener('transitioncancel', onGhostEnd);
    ghost = outgoing;
    ghostTimer = setTimeout(dropGhost, GHOST_FALLBACK_MS);
  };
  paint('plain');
  const off = tracker?.register(paint);
  if (off) {
    painterOffByButton.set(button, off);
  }
  return () => variants[tracker?.current() ?? 'plain'] || variants.plain;
}

// The Back control. A press navigates, a hold opens the history
// scrubber, and hold-gesture.js owns the rule that keeps the two apart.

// A BUTTON rather than a keyboard binding because a hold needs something to
// hold: Back reached the visible tab only through Chromium's IDC_BACK, a menu
// item and a hotkey with no press-and-hold surface at all.
export function buildBackButton(send = chrome.send.bind(chrome),
                                {reducedMotion = false, modifiers} = {}) {
  const button = buildButton('first sbback', BACK_VARIANTS.plain.label);
  const glyph = buildGlyph('');
  installReplayableAnimation(glyph, 'nudgeL');
  button.appendChild(glyph);
  const variant =
      useVariants(button, glyph, BACK_VARIANTS, modifiers, {reducedMotion});

  const state = newHoldState();
  let timer = 0;
  const stopTimer = () => {
    if (timer) {
      clearTimeout(timer);
      timer = 0;
    }
  };

  button.addEventListener('pointerdown', event => {
    // Nothing starts, so the release reads CANCELLED and the hold timer that
    // opens the scrubber never runs - one guard covers both gestures.
    if (navDead(button)) {
      return;
    }
    onPointerDown(state, event.timeStamp, event.clientX, event.clientY);
    stopTimer();
    // One shot, not an interval: the hold latches, so a second tick could only
    // ever report PENDING.
    timer = setTimeout(() => {
      timer = 0;
      if (onHoldTimerTick(state, event.timeStamp + HOLD_THRESHOLD_MS) === HOLD) {
        send('sidebarBackHeld', []);
      }
    }, HOLD_THRESHOLD_MS);
  });
  button.addEventListener('pointermove', event => {
    if (onPointerMove(state, event.clientX, event.clientY) !== HOLD) {
      // Cancelled or pending either way. The timer only matters while the
      // press is live, and onPointerMove has already ended it if it drifted.
      if (!state.down) {
        stopTimer();
      }
    }
  });
  const release = () => {
    stopTimer();
    if (onPointerUp(state) === PRESS) {
      if (!reducedMotion) {
        animateGlyph(glyph, 'nudgeL');
      }
      send(variant().message, []);
    }
  };
  button.addEventListener('pointerup', release);
  // A pointer that leaves the window never sends pointerup, and a press left
  // live would make the NEXT one inherit a stale timer.
  button.addEventListener('pointercancel', release);
  return button;
}

function buildClickButton(className, variants, send,
                          {animClass, reducedMotion = false, modifiers} = {}) {
  const button = buildButton(className, variants.plain.label);
  let wrapper = null;
  let current = () => variants.plain;
  if (animClass) {
    wrapper = buildGlyph('');
    installReplayableAnimation(wrapper, animClass);
    button.appendChild(wrapper);
    current =
        useVariants(button, wrapper, variants, modifiers, {reducedMotion});
  } else if (typeof variants.plain.glyph === 'function') {
    button.appendChild(variants.plain.glyph());
  } else {
    button.textContent = variants.plain.glyph;
  }
  button.addEventListener('click', event => {
    event.stopPropagation();
    if (navDead(button)) {
      return;
    }
    if (wrapper && !reducedMotion) {
      animateGlyph(wrapper, animClass);
    }
    send(current().message, []);
  });
  return button;
}

// Back, forward and reload, left to right. The floating address bar
// (devbar.js) builds its own copy through here, so the modifier variants, the
// glyphs and the animations stay one implementation across both surfaces.
export function buildNavGroup(send, {reducedMotion = false, modifiers,
                                     anchorFirst = true} = {}) {
  const back = buildBackButton(send, {reducedMotion, modifiers});
  // `first` is this row's right-align anchor. The bar lays its group out from
  // the left and has no use for it.
  if (!anchorFirst) {
    back.classList.remove('first');
  }
  return [
    back,
    buildClickButton('sbfwd', FORWARD_VARIANTS, send,
                     {animClass: 'nudgeR', reducedMotion, modifiers}),
    buildClickButton('sbreload', RELOAD_VARIANTS, send,
                     {animClass: 'spin', reducedMotion, modifiers}),
  ];
}

// the window buttons the page draws for itself. While the sidebar
// floats, the reveal panel is a child window over the parent's frame view, so
// the real buttons are behind it wherever they sit. The lab has the same three
// dots in its own chrome row (swipe-lab.html:55), and its titlebar copies are
// retired for the same reason.
const WINDOW_DOTS = [
  {cls: 'close', label: 'Close', message: 'sidebarWindowClose'},
  {cls: 'min', label: 'Minimise', message: 'sidebarWindowMinimize'},
  // The message keeps its name. The window answers it with full screen, the
  // way the green button does.
  {cls: 'zoom', label: 'Full Screen', message: 'sidebarWindowZoom'},
];

function buildWindowDots(send) {
  const group = document.createElement('div');
  group.className = 'sbtd';
  for (const dot of WINDOW_DOTS) {
    const button = document.createElement('button');
    button.className = 'td ' + dot.cls;
    button.title = dot.label;
    button.setAttribute('aria-label', dot.label);
    button.addEventListener('click', event => {
      event.stopPropagation();
      send(dot.message, []);
    });
    group.appendChild(button);
  }
  return group;
}

// Widths from sidebar-nav-row.css, mirrored by the same-named constants in
// atrium_nav_carve_decision.cc: the native carve must cover exactly the
// buttons this ladder leaves visible, or a click lands on a window drag.
// dotsGap is the row's own (.sbnav .sbtd margin-right), not a carve constant.
export const NAV_METRICS =
    {button: 22, gap: 2, toggle: 22, more: 16, count: 3, dotsGap: 10};

// The right group, left to right. Narrowing sheds them from the tail: reload
// first, then forward, and back survives longest as the control a user reaches
// for most.
const NAV_GROUP = [
  {cls: 'sbback', variants: BACK_VARIANTS},
  {cls: 'sbfwd', variants: FORWARD_VARIANTS},
  {cls: 'sbreload', variants: RELOAD_VARIANTS},
];

function navRowContentWidth(visible, withMore) {
  const items = 1 + visible + (withMore ? 1 : 0);
  return NAV_METRICS.toggle + visible * NAV_METRICS.button +
      (withMore ? NAV_METRICS.more : 0) + (items - 1) * NAV_METRICS.gap;
}

function navGroupOnlyWidth(visible) {
  return visible * NAV_METRICS.button + NAV_METRICS.more +
      visible * NAV_METRICS.gap;
}

// The overflow button is narrower than a nav button, so every rung buys room.
// Below the toggle rungs Hide sidebar yields to forward - it keeps Cmd+S and
// a menu row, and a narrow row exists to keep back and forward reachable.
export function visibleNavButtons(width) {
  // The toggle's lowest rung keeps back AND forward beside it - one rung
  // further down it is the toggle that goes, never forward.
  for (let count = NAV_METRICS.count; count >= 2; count--) {
    if (navRowContentWidth(count, count < NAV_METRICS.count) <= width) {
      return ['sbtoggle', ...NAV_GROUP.slice(0, count).map((e) => e.cls)];
    }
  }
  for (let count = 2; count > 0; count--) {
    if (navGroupOnlyWidth(count) <= width) {
      return NAV_GROUP.slice(0, count).map((e) => e.cls);
    }
  }
  return [];
}

// Not '…': the ellipsis reads as a truncation mark, and every emoji glyph in
// this row renders in colour against a monochrome strip.
const MORE_GLYPH = '⋯';

const MENU_CLASS = 'navmenu';

const navStateByRow = new WeakMap();

// The tracker outlives every menu built from it, so a painter left behind here
// repaints removed rows for the rest of the session.
const menuPaintersByRow = new WeakMap();

// `restoreFocus` for a dismissal the keyboard drove: Escape has to put the
// caret back on the button, or the next Tab starts from the document.
function closeNavMenu(row, {restoreFocus = false} = {}) {
  const menu = row.querySelector('.' + MENU_CLASS);
  const more = row.querySelector('.sbmore');
  more?.setAttribute('aria-expanded', 'false');
  if (menu && restoreFocus) {
    more?.focus();
  }
  for (const off of menuPaintersByRow.get(row) || []) {
    off();
  }
  menuPaintersByRow.delete(row);
  menu?.remove();
  refreshMenuState();
}

// A row stands in for a button that swaps function under Option and Cmd+Shift,
// so it paints and sends the same variant that button would.
function openNavMenu(row) {
  closeNavMenu(row);
  const send = navStateByRow.get(row)?.send;
  const tracker = trackersByRow.get(row);
  const releases = [];
  const menu = document.createElement('div');
  // ov-card carries the surface. .ctxmenu is layout only, so without it the
  // menu painted no background at all and read as text over the tab list.
  menu.className = 'ctxmenu ' + MENU_CLASS + ' ov-card';
  menu.setAttribute('role', 'menu');
  // The shed toggle after the nav actions - a hidden Hide sidebar still needs
  // a pointer path, Cmd+S alone is not discoverable.
  const entries = [...NAV_GROUP, {cls: 'sbtoggle', variants: TOGGLE_VARIANTS}];
  for (const entry of entries) {
    if (!row.querySelector('.' + entry.cls)?.classList.contains('navgone')) {
      continue;
    }
    const item = document.createElement('button');
    item.className = 'ctxitem ov-row';
    item.type = 'button';
    item.setAttribute('role', 'menuitem');
    const glyphBox = buildGlyph('');
    item.appendChild(glyphBox);
    // A span, not textContent: that would wipe the glyph appended before it.
    const label = document.createElement('span');
    label.className = 'ctxlabel';
    item.appendChild(label);
    // The same resolver the hover hint reads, so the two cannot name different
    // keys for one button. Asked by class, which is all it needs.
    const keys = keysForButton({className: entry.cls});
    const chip = keys ? keycaps(keys) : null;
    chip?.classList.add('ctxkey');
    const variantFor = (state) => entry.variants[state] || entry.variants.plain;
    const paint = (state) => {
      const variant = variantFor(state);
      const glyph = variant.glyph;
      glyphBox.replaceChildren(typeof glyph === 'function' ? glyph() : glyph);
      label.textContent = variant.label;
      // The key runs the plain function, so a wider variant that kept the chip
      // would name a key that does something else - hint-triggers.js drops it
      // from the hover hint for the same reason.
      if (variant === entry.variants.plain) {
        if (chip) {
          item.appendChild(chip);
        }
      } else {
        chip?.remove();
      }
    };
    paint(tracker?.current() ?? 'plain');
    const off = tracker?.register(paint);
    if (off) {
      releases.push(off);
    }
    item.addEventListener('click', () => {
      // Read before the close: the row's capture heal has already synced the
      // tracker to what this click carried.
      const {message} = variantFor(tracker?.current() ?? 'plain');
      closeNavMenu(row);
      send?.(message, []);
    });
    menu.appendChild(item);
  }
  menuPaintersByRow.set(row, releases);
  row.appendChild(menu);
  row.querySelector('.sbmore')?.setAttribute('aria-expanded', 'true');
  menu.querySelector('.ctxitem')?.focus();
  refreshMenuState();
  return menu;
}

function buildMoreButton(row) {
  const button = buildButton('sbmore', 'More navigation actions');
  button.setAttribute('aria-haspopup', 'menu');
  button.setAttribute('aria-expanded', 'false');
  button.appendChild(buildGlyph(MORE_GLYPH));
  button.addEventListener('click', (event) => {
    event.stopPropagation();
    if (row.querySelector('.' + MENU_CLASS)) {
      closeNavMenu(row);
    } else {
      openNavMenu(row);
    }
  });
  return button;
}

// A menu left open behind a hidden or unfocused surface is the same stale
// state a held modifier would be, and goes the same way.
function installNavMenuDismiss(row) {
  const onKey = (event) => {
    if (event.key === 'Escape') {
      closeNavMenu(row, {restoreFocus: true});
    }
  };
  const onPress = (event) => {
    const target = event.target;
    if (!target?.closest?.('.' + MENU_CLASS) && !target?.closest?.('.sbmore')) {
      closeNavMenu(row);
    }
  };
  const onAway = () => closeNavMenu(row);
  const unregister = registerMenu({
    close: () => closeNavMenu(row),
    isOpen: () => !!row.querySelector('.' + MENU_CLASS),
  });
  document.addEventListener('keydown', onKey);
  document.addEventListener('pointerdown', onPress, true);
  window.addEventListener('blur', onAway);
  document.addEventListener('visibilitychange', onAway);
  return () => {
    unregister();
    document.removeEventListener('keydown', onKey);
    document.removeEventListener('pointerdown', onPress, true);
    window.removeEventListener('blur', onAway);
    document.removeEventListener('visibilitychange', onAway);
  };
}

// The dots only take room while the sidebar floats, and then the row starts at
// the band inset instead of the traffic-light clearance.
function navControlsWidth(row) {
  const width = row.clientWidth || row.getBoundingClientRect?.().width || 0;
  const dots = row.querySelector('.sbtd');
  const lead = dots?.offsetWidth
      ? dots.offsetWidth + NAV_METRICS.dotsGap + NAV_METRICS.gap
      : 0;
  return width - lead;
}

// margin-left: auto lives on whichever control is leftmost in the group, so
// the group keeps hugging the row's right edge as members drop out.
function setGroupAnchor(row, target) {
  for (const button of row.querySelectorAll('.nb')) {
    if (button !== target) {
      button.classList.remove('first');
    }
  }
  target?.classList.add('first');
}

// A width of 0 means nothing has laid out yet - collapsing on that would hide
// the row before its first paint.
export function layoutNavRow(row, controlsWidth = navControlsWidth(row)) {
  if (navStateByRow.get(row)?.inRow === false) {
    return;
  }
  const visible = controlsWidth > 0
      ? visibleNavButtons(controlsWidth)
      : ['sbtoggle', ...NAV_GROUP.map((entry) => entry.cls)];
  const state = navStateByRow.get(row);
  if (state && state.visible?.join() === visible.join()) {
    return;
  }
  closeNavMenu(row);
  row.querySelector('.sbtoggle')
      ?.classList.toggle('navgone', !visible.includes('sbtoggle'));
  for (const entry of NAV_GROUP) {
    row.querySelector('.' + entry.cls)
        ?.classList.toggle('navgone', !visible.includes(entry.cls));
  }
  let more = row.querySelector('.sbmore');
  if (visible.length === NAV_METRICS.count + 1) {
    more?.remove();
    more = null;
  } else if (!more) {
    more = buildMoreButton(row);
    row.appendChild(more);
  }
  if (state) {
    state.visible = visible;
  }
  // The anchor pushes the group to the right edge, so it must skip the
  // toggle: an anchored toggle would carry the whole row off with it.
  const group = visible.filter((cls) => cls !== 'sbtoggle');
  const leftmost = group.length ? row.querySelector('.' + group[0]) : null;
  setGroupAnchor(row, leftmost || more);
}

// Keyed by row so the tracker a replaced row installed on window comes off
// with it - the row itself is unreachable by then, but its listeners are not.
const trackersByRow = new WeakMap();

export function buildNavRow({send = (name, args) => chrome.send(name, args),
                              reducedMotion} = {}) {
  const reduced = reducedMotion !== undefined
      ? reducedMotion
      : !!globalThis.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches;
  const row = document.createElement('div');
  row.className = 'sbnav';
  const modifiers = createModifierTracker(row);
  row.append(
      buildWindowDots(send),
      buildClickButton('sbtoggle', TOGGLE_VARIANTS, send),
      ...buildNavGroup(send, {reducedMotion: reduced, modifiers}));
  // The row is fixed to the sidebar's own edges, so its width IS the budget -
  // no resize message needed, and nothing lags a drag by a round trip.
  const dismiss = installNavMenuDismiss(row);
  const observer = globalThis.ResizeObserver
      ? new ResizeObserver(() => layoutNavRow(row))
      : null;
  observer?.observe(row);
  navStateByRow.set(row, {
    send,
    reducedMotion: reduced,
    inRow: true,
    visible: null,
    ability: null,
    dispose() {
      observer?.disconnect();
      dismiss();
      closeNavMenu(row);
    },
  });
  trackersByRow.set(row, modifiers);
  return row;
}

// the floating address bar hosts the group while it is up, and the
// row is left with the dots and the toggle. With nothing to shed, the overflow
// ladder has no work either - a menu button here would open an empty menu.
export function setNavGroupInRow(row, inRow) {
  const state = navStateByRow.get(row);
  if (!state || state.inRow === inRow) {
    return;
  }
  state.inRow = inRow;
  state.visible = null;
  closeNavMenu(row);
  row.querySelector('.sbmore')?.remove();
  for (const entry of NAV_GROUP) {
    const button = row.querySelector('.' + entry.cls);
    if (button) {
      releaseNavButton(button);
      button.remove();
    }
  }
  if (inRow) {
    row.append(...buildNavGroup(state.send, {
      reducedMotion: state.reducedMotion,
      modifiers: trackersByRow.get(row),
    }));
    // A fresh group is live in both directions, and the window pushes only
    // when the tab's history moves - which a handover is not.
    applyNavAbility(row, state.ability);
    layoutNavRow(row);
  } else {
    // layoutNavRow is the only code that clears navgone, and it early-returns
    // for as long as the group is away - a row narrow enough to have shed the
    // toggle before the handover would otherwise keep it shed forever.
    row.querySelector('.sbtoggle')?.classList.remove('navgone');
  }
}

// What the displayed tab's NavigationController allows, pushed by
// AtriumBrowserWindow::PushNavAbility. Held so the handover above can restore
// it onto a group this row rebuilds.
export function setNavAbility(row, ability) {
  const state = navStateByRow.get(row);
  if (!state) {
    return;
  }
  state.ability = ability;
  applyNavAbility(row, ability);
}

// Keyed by root so a second install on the same root tears down its own prior
// row instead of orphaning it.
const rowsByRoot = new WeakMap();

// Inserted as #atrium-root's previous sibling, not a child: every snapshot push
// calls root.replaceChildren(), and the row must outlive those.
export function installSidebarNavRow(root, options = {}) {
  const previous = rowsByRoot.get(root);
  if (previous) {
    trackersByRow.get(previous)?.dispose();
    navStateByRow.get(previous)?.dispose();
    previous.remove();
  }
  const row = buildNavRow(options);
  (root.parentNode || document.body).insertBefore(row, root);
  rowsByRoot.set(root, row);
  // AtriumBrowserWindow::SetSidebarNavModifiers pushes here on every
  // flagsChanged. Installed with the row so a replacement takes the receiver
  // with it rather than leaving the old row's tracker addressable.
  window.atriumSetNavModifiers = (shift, cmd, option) =>
      trackersByRow.get(row)?.set(!!shift, !!cmd, !!option);
  window.atriumSetNavAbility = (ability) => setNavAbility(row, ability);
  return row;
}

// sidebar.js owns window.atriumSetDevBarShown, which is the push that says
// whether the floating bar is up. It reaches its row through this rather than
// installing a second receiver for that same name.
export function navRowForRoot(root) {
  return rowsByRoot.get(root);
}

// The painter list is where a rebuilt group leaks, and it grows silently -
// nothing about the row's own DOM shows it. Read it directly instead.
export function navPainterCount(row) {
  return trackersByRow.get(row)?.painterCount() ?? 0;
}
