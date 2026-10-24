// Copyright 2026 The Atrium Authors
//
// The /devbar route, ported from the lab's .devbar
// (docs/lab/swipe-lab.html ~596-606, ~4040), including the insecure-origin
// treatment.

import {isInsecureOrigin, hostOf} from './insecure-origin.js';
import {createLoadFill} from './load-fill.js';
import {
  applyNavAbility,
  buildNavGroup,
  createModifierTracker,
} from './sidebar-nav-row.js';
import {installHover} from './hint-triggers.js';
import {REAL_TIMEOUT_CLOCK} from './clock.js';
import {parseShadowLayer, splitShadowLayers} from './overlay-shadow-spec.js';
import {renderExtStrip} from './sidebar-ext-strip.js';
import {buildQuickMenuButton} from './sidebar-ext-shared.js';
import {setIconScope, urlIconPath} from './icon-scope.js';

const SVG_NS = 'http://www.w3.org/2000/svg';

// Indexed by the pin state, so the label names what the next click does.
const PIN_LABEL = ['Pin the address bar', 'Unpin the address bar'];

function buildLockRect() {
  const rect = document.createElementNS(SVG_NS, 'rect');
  rect.setAttribute('x', '2.5');
  rect.setAttribute('y', '5');
  rect.setAttribute('width', '7');
  rect.setAttribute('height', '5');
  rect.setAttribute('rx', '1.2');
  rect.setAttribute('fill', 'none');
  rect.setAttribute('stroke', 'currentColor');
  rect.setAttribute('stroke-width', '1.2');
  return rect;
}

// Port of the lab's LOCK_SHUT (swipe-lab.html:1907).
function buildLockGlyph() {
  const svg = document.createElementNS(SVG_NS, 'svg');
  svg.setAttribute('viewBox', '0 0 12 12');
  svg.setAttribute('width', '11');
  svg.setAttribute('height', '11');
  const shackle = document.createElementNS(SVG_NS, 'path');
  shackle.setAttribute('d', 'M4 5V3.8a2 2 0 0 1 4 0V5');
  shackle.setAttribute('fill', 'none');
  shackle.setAttribute('stroke', 'currentColor');
  shackle.setAttribute('stroke-width', '1.2');
  svg.append(buildLockRect(), shackle);
  return svg;
}

// Port of the lab's LOCK_BROKEN (swipe-lab.html:1909) - same body, shackle
// snapped open on the right.
function buildBrokenLockGlyph() {
  const svg = document.createElementNS(SVG_NS, 'svg');
  svg.setAttribute('viewBox', '0 0 12 12');
  svg.setAttribute('width', '11');
  svg.setAttribute('height', '11');
  svg.setAttribute('aria-label', 'Not secure');
  const left = document.createElementNS(SVG_NS, 'path');
  left.setAttribute('d', 'M4 5V3.8a2 2 0 0 1 2-2');
  left.setAttribute('fill', 'none');
  left.setAttribute('stroke', 'currentColor');
  left.setAttribute('stroke-width', '1.2');
  left.setAttribute('stroke-linecap', 'round');
  const right = document.createElementNS(SVG_NS, 'path');
  right.setAttribute('d', 'M8 5V4.3');
  right.setAttribute('fill', 'none');
  right.setAttribute('stroke', 'currentColor');
  right.setAttribute('stroke-width', '1.2');
  right.setAttribute('stroke-linecap', 'round');
  svg.append(buildLockRect(), left, right);
  return svg;
}

// Port of the lab's PIN_SVG (swipe-lab.html:1491), sized to the lock beside it.
function buildPinGlyph() {
  const svg = document.createElementNS(SVG_NS, 'svg');
  svg.setAttribute('viewBox', '0 0 12 12');
  svg.setAttribute('width', '11');
  svg.setAttribute('height', '11');
  const head = document.createElementNS(SVG_NS, 'path');
  head.setAttribute('d', 'M4.3 1.5h3.4l-.4 3.1 2 2.1H2.7l2-2.1z');
  head.setAttribute('fill', 'none');
  head.setAttribute('stroke', 'currentColor');
  head.setAttribute('stroke-width', '1.1');
  head.setAttribute('stroke-linejoin', 'round');
  const needle = document.createElementNS(SVG_NS, 'path');
  needle.setAttribute('d', 'M6 6.9v3.6');
  needle.setAttribute('stroke', 'currentColor');
  needle.setAttribute('stroke-width', '1.2');
  needle.setAttribute('stroke-linecap', 'round');
  svg.append(head, needle);
  return svg;
}

const GRIP_LABEL = 'Drag to move this tab';
const TAB_DRAG_TYPE = 'application/x-atrium-tab';

// Two columns of three dots, the usual drag-handle mark.
function buildGripGlyph() {
  const svg = document.createElementNS(SVG_NS, 'svg');
  svg.setAttribute('viewBox', '0 0 6 12');
  svg.setAttribute('width', '6');
  svg.setAttribute('height', '12');
  for (const cx of [1, 5]) {
    for (const cy of [2, 6, 10]) {
      const dot = document.createElementNS(SVG_NS, 'circle');
      dot.setAttribute('cx', String(cx));
      dot.setAttribute('cy', String(cy));
      dot.setAttribute('r', '1');
      dot.setAttribute('fill', 'currentColor');
      svg.appendChild(dot);
    }
  }
  return svg;
}

// Keyed by bar so installDevBar can publish the native push's receiver against
// the tracker the bar it just built actually owns.
const trackersByBar = new WeakMap();
const fillsByBar = new WeakMap();

// while this bar is up it hosts back/forward/reload, and the
// sidebar's chrome row keeps only its dots and its toggle.
export function buildDevBar({send = (name, args) => chrome.send(name, args),
                              reducedMotion} = {}) {
  const bar = document.createElement('div');
  bar.className = 'devbar ov-card';

  const reduced = reducedMotion !== undefined
      ? reducedMotion
      : !!globalThis.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches;
  const nav = document.createElement('span');
  nav.className = 'dvnav';
  const modifiers = createModifierTracker(nav);
  nav.append(...buildNavGroup(
      send, {reducedMotion: reduced, modifiers, anchorFirst: false}));
  trackersByBar.set(bar, modifiers);

  const lock = document.createElement('span');
  lock.className = 'dvlock';
  lock.appendChild(buildLockGlyph());

  // Lab order (swipe-lab.html:4040): lock, warning, extensions, url, copy.
  const warn = document.createElement('span');
  warn.className = 'dvwarn';
  warn.textContent = 'Not secure';

  const exts = document.createElement('span');
  exts.className = 'dvexts';
  const extsStrip = document.createElement('span');
  extsStrip.className = 'dvexts-strip';
  exts.append(extsStrip,
              buildQuickMenuButton((name, args) => chrome.send(name, args)));

  const url = document.createElement('span');
  url.className = 'dvurl';

  const copy = document.createElement('button');
  copy.className = 'dvcopy ov-btn';
  copy.title = 'Copy URL';
  copy.textContent = '⧉';

  // After the lab's five, since the lab has no pin in this bar.
  const pin = document.createElement('button');
  pin.className = 'dvpin ov-btn';
  pin.type = 'button';
  pin.title = PIN_LABEL[0];
  pin.setAttribute('aria-label', PIN_LABEL[0]);
  pin.setAttribute('aria-pressed', 'false');
  pin.appendChild(buildPinGlyph());

  // the handle that drags this bar's tab like a sidebar row.
  const grip = document.createElement('span');
  grip.className = 'dvgrip';
  grip.draggable = true;
  grip.title = GRIP_LABEL;
  grip.setAttribute('aria-label', GRIP_LABEL);
  grip.appendChild(buildGripGlyph());

  // Lab order after the group, which leads the bar the way a toolbar's own
  // back/forward/reload do. The grip is the bar's left edge, before all of it.
  bar.append(grip, nav, lock, warn, exts, url, copy, pin);
  // First child, so the fill paints under every control the capsule holds.
  fillsByBar.set(bar, createLoadFill(bar));
  return bar;
}

// devbar.css's box-shadow never renders here, because there is no room past
// the bar's own viewport edge. AtriumOverlayShadowLayer paints it instead.
function pushDevBarShadowSpec(bar) {
  const cs = getComputedStyle(bar);
  const layers = splitShadowLayers(cs.boxShadow);
  const spec = layers.length ? parseShadowLayer(layers[0]) : null;
  if (!spec) {
    return;
  }
  const radius = parseFloat(cs.borderTopLeftRadius) || 0;
  // The away slide, so the native shadow leaves on the same curve as the bar
  // instead of sitting over a page the bar has left.
  const awayShift = parseFloat(cs.getPropertyValue('--dv-away-shift')) || 0;
  const awayMs = (parseFloat(cs.transitionDuration) || 0) * 1000;
  chrome.send('devBarShadowSpec', [
    spec.offsetX, spec.offsetY, spec.blur, radius,
    spec.r, spec.g, spec.b, spec.a,
    awayShift, awayMs,
  ]);
}

// A sidebar row's shape, since the drag means the same thing. The bar has no
// page title, so the host stands in for it.
function buildDragGhost(url) {
  const ghost = document.createElement('div');
  ghost.className = 'row tabrow sel dvghost';
  const favicon = document.createElement('img');
  favicon.className = 'fv';
  const src = urlIconPath(url);
  if (src) {
    favicon.src = src;
  }
  const title = document.createElement('span');
  title.className = 'tl';
  title.textContent = url ? hostOf(url) : '';
  ghost.append(favicon, title);
  return ghost;
}

// the window resolves the tab from the sending bar, so no message
// names one. The type is ours alone: no text/uri-list or text/plain, so a drop
// on a page can neither navigate it nor insert text.
function installGripDrag(grip, urlOf) {
  let lastMove = '';
  let ghost = null;
  grip.addEventListener('dragstart', (e) => {
    lastMove = '';
    e.dataTransfer?.setData(TAB_DRAG_TYPE, '');
    if (e.dataTransfer) {
      e.dataTransfer.effectAllowed = 'move';
    }
    ghost?.remove();
    // In the document, since the browser paints the image from a laid-out
    // element when dragstart returns. devbar.css parks it off screen.
    ghost = buildDragGhost(urlOf());
    document.body.appendChild(ghost);
    e.dataTransfer?.setDragImage?.(ghost, 16, 18);
    chrome.send('devBarTabDragStarted', []);
  });
  grip.addEventListener('drag', (e) => {
    // The last drag event of a gesture reads 0,0 on Mac.
    if (!e.screenX && !e.screenY) {
      return;
    }
    const key = `${e.screenX},${e.screenY}`;
    if (key === lastMove) {
      return;
    }
    lastMove = key;
    chrome.send('devBarTabDragMoved', [e.screenX, e.screenY]);
  });
  grip.addEventListener('dragend', (e) => {
    lastMove = '';
    ghost?.remove();
    ghost = null;
    chrome.send('devBarTabDragReleased', [e.screenX, e.screenY]);
  });
}

export function installDevBar(root, options = {}) {
  const bar = buildDevBar(options);
  root.appendChild(bar);
  // Same hint bubble every other hover target uses. `options.clock` lets a
  // test fake the 600ms delay.
  installHover(document.body, options.clock || REAL_TIMEOUT_CLOCK);
  // The sidebar row's receiver by the same name lives in the sidebar's own
  // document - AtriumBrowserWindow::PushNavModifiers pushes to both.
  window.atriumSetNavModifiers = (shift, cmd, option) =>
      trackersByBar.get(bar)?.set(!!shift, !!cmd, !!option);
  window.atriumSetNavAbility = (ability) => applyNavAbility(bar, ability);
  const lockSpan = bar.querySelector('.dvlock');
  const urlSpan = bar.querySelector('.dvurl');
  let currentUrl = '';

  // Pushed on every PushDevBarState() call (Developer Mode toggle, active-tab
  // switch, navigation) - see AtriumBrowserWindow::PushDevBarState.
  // slid away on a downward scroll, back on an upward one.
  window.atriumSetDevBarAway = (away) => {
    bar.classList.toggle('away', !!away);
  };

  // Pushed the moment the View becomes visible. The reflow read restarts the
  // animation on a second show, which a class already on the element skips.
  window.atriumDevBarEnter = () => {
    bar.classList.remove('descending');
    void bar.offsetWidth;
    bar.classList.add('descending');
  };

  // The same push AtriumBrowserWindow::PushLoadPhase sends to the sidebar.
  // The sidebar hides its own field while this bar is up, so exactly one of
  // the two ever shows a fill.
  // a split gives each pane its own bar, and every bar hears
  // every phase. An empty id is the single bar, which takes them all.
  let ownTabId = '';
  window.atriumSetDevBarTab = (tabId) => {
    ownTabId = tabId || '';
  };

  // ringColor is the same SidebarSeamLineColor ActivePaneRingView
  // paints (atrium_browser_window.cc), never a value of devbar.css's own.
  window.atriumSetDevBarFocused = (focused, ringColor) => {
    bar.classList.toggle('focused', !!focused);
    bar.style.setProperty('--dv-ring', ringColor || '');
  };

  window.atriumLoadPhase = (tabId, phase, fraction) => {
    if (ownTabId && String(tabId) !== ownTabId) {
      return;
    }
    fillsByBar.get(bar)?.setPhase(phase, fraction);
  };

  // Capture phase, so the window makes this bar's pane the active one before
  // any control's own click acts on the active pane. Only a split bar has a
  // pane to name.
  bar.addEventListener('pointerdown', (e) => {
    // A press on the grip does nothing until it moves.
    if (ownTabId && !e.target?.closest?.('.dvgrip')) {
      chrome.send('devBarPointerDown', []);
    }
  }, {capture: true});

  installGripDrag(bar.querySelector('.dvgrip'), () => currentUrl);

  window.atriumSetDevBarUrl = (url2) => {
    currentUrl = url2 || '';
    urlSpan.textContent = currentUrl;
    const bad = isInsecureOrigin(currentUrl);
    bar.classList.toggle('insecure', bad);
    lockSpan.replaceChildren(bad ? buildBrokenLockGlyph() : buildLockGlyph());
    lockSpan.title = bad ? 'Not secure: ' + hostOf(currentUrl) : '';
  };

  // The window decides what a pinned bar does about scroll. Here the pin is
  // only the switch and its lamp.
  const pinButton = bar.querySelector('.dvpin');
  let pinned = false;
  const showPinned = (next) => {
    pinned = Boolean(next);
    pinButton.setAttribute('aria-pressed', String(pinned));
    pinButton.title = PIN_LABEL[pinned ? 1 : 0];
    pinButton.setAttribute('aria-label', pinButton.title);
  };

  window.atriumSetDevBarPinned = showPinned;

  // The same push the sidebar strip takes, from the same snapshot. While this
  // bar is up the sidebar strip hides (devbar-shown), so only one shows.
  const extsSpan = bar.querySelector('.dvexts-strip');
  // Pushed just before the actions below, so the chips built from them can
  // already resolve an icon path.
  window.atriumSetIconScope = setIconScope;
  window.atriumSetSidebarExtensions = (actions) => {
    renderExtStrip(extsSpan, {
      actions: actions || [],
      onChipClick: (id, rect) => chrome.send('pillActionClicked', [id, rect]),
      onChipContextMenu: (id, rect) =>
          chrome.send('extensionChipContextMenu', [id, rect]),
    });
  };

  pinButton.addEventListener('click', () => {
    showPinned(!pinned);
    chrome.send('devBarPinToggled', [pinned]);
  });

  bar.querySelector('.dvcopy').addEventListener('click', () => {
    if (!currentUrl) {
      return;
    }
    // navigator.clipboard refuses a WebUI that never held focus,
    // which the floating bar never does. The window writes the clipboard.
    chrome.send('devBarCopyUrl', [currentUrl]);
  });

  // The same gesture the sidebar's row takes: hand the box to the window,
  // which anchors the palette on it.
  urlSpan.addEventListener('click', () => {
    const box = urlSpan.getBoundingClientRect();
    chrome.send('devBarUrlClicked',
                [Math.round(box.left), Math.round(box.top),
                 Math.round(box.width), Math.round(box.height)]);
  });

  // Re-runs whenever PushOverlayTheme fires - --ov-shadow-tight varies by
  // light, dark and native-chrome.
  window.__atriumPushOverlayShadowSpec = () => pushDevBarShadowSpec(bar);
  pushDevBarShadowSpec(bar);

  chrome.send('devBarReady', []);
}
