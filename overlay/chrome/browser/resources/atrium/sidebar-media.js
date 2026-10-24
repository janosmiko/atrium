// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The media controller block under the tab list. One controller per record
// AtriumMediaHub pushes as mediaSessionsChanged, newest at the bottom.

import {faviconPath} from './icon-scope.js';
import {registerMenu} from './menu-dismiss.js';

const NS = 'http://www.w3.org/2000/svg';

// A fourth controller would take the tab list's own height, so the block holds
// three and scrolls the rest.
export const MAX_SHOWN_CONTROLLERS = 3;

// Built node by node rather than reached for in icons.js: the WebUI CSP
// registers no Trusted Types policy, and icons.js carries no transport marks.
// A node's own fill="currentColor" outranks the fill: none the sheet sets on
// the <svg>, so one glyph can carry a solid transport shape next to a stroked
// speaker wave.
const F = {fill: 'currentColor', stroke: 'none'};
const GLYPHS = {
  prev: [['path', {d: 'M18 6v12L8.5 12z', ...F}],
         ['rect', {x: '5', y: '6', width: '2.2', height: '12', rx: '1.1', ...F}]],
  play: [['path', {d: 'M7.5 5l11.5 7-11.5 7z', ...F}]],
  pause: [['rect', {x: '7', y: '5', width: '3.4', height: '14', rx: '1.2', ...F}],
          ['rect', {x: '13.6', y: '5', width: '3.4', height: '14', rx: '1.2', ...F}]],
  next: [['path', {d: 'M6 6v12l9.5-6z', ...F}],
         ['rect', {x: '16.8', y: '6', width: '2.2', height: '12', rx: '1.1', ...F}]],
  sound: [['path', {d: 'M4 9h4l5-4v14l-5-4H4z', ...F}],
          ['path', {d: 'M16.4 9.4a4 4 0 010 5.2'}],
          ['path', {d: 'M19.2 7.2a7.5 7.5 0 010 9.6'}]],
  mute: [['path', {d: 'M4 9h4l5-4v14l-5-4H4z', ...F}],
         ['path', {d: 'M16.5 9.5l5 5M21.5 9.5l-5 5'}]],
  pip: [['rect', {x: '3', y: '5', width: '18', height: '14', rx: '2.5'}],
        ['rect', {x: '12.5', y: '11.5', width: '7.5', height: '5.5', rx: '1.5', ...F}]],
  close: [['path', {d: 'M6 6l12 12M18 6L6 18'}]],
};

function glyph(name) {
  const svg = document.createElementNS(NS, 'svg');
  svg.setAttribute('viewBox', '0 0 24 24');
  svg.setAttribute('aria-hidden', 'true');
  svg.setAttribute('class', 'sbm-glyph');
  for (const [tag, attrs] of GLYPHS[name]) {
    const node = document.createElementNS(NS, tag);
    for (const [key, value] of Object.entries(attrs)) {
      node.setAttribute(key, value);
    }
    svg.appendChild(node);
  }
  return svg;
}

function buildButton(command, tabId, label, glyphName, onCommand) {
  const button = document.createElement('button');
  button.className = 'sbm-btn';
  button.dataset.command = command;
  button.title = label;
  button.setAttribute('aria-label', label);
  button.appendChild(glyph(glyphName));
  button.addEventListener('click', (e) => {
    e.stopPropagation();
    onCommand(command, tabId);
  });
  return button;
}

function buildFavicon(tabId) {
  const slot = document.createElement('span');
  slot.className = 'sbm-fv';
  // The scoped cache, keyed by tab id. A record's own faviconURL is whatever
  // the page declared, and never reaches an src.
  const src = faviconPath(tabId);
  if (src) {
    const img = document.createElement('img');
    img.src = src;
    img.alt = '';
    slot.appendChild(img);
  }
  return slot;
}

// Only one controller can be hovered - or have its volume band open - at a
// time, so a single measured height is enough to describe "the room the
// expanded row needs" for renderMediaBlock's caller.
function activeHeight(container) {
  let h = 36;
  for (const item of container.querySelectorAll('.sbm-item')) {
    if (item.classList.contains('sbm-vol-open')) {
      return 108;
    }
    if (item.hovered) {
      h = 72;
    }
  }
  return h;
}

// The one place a button's glyph and its label change together, so a patched
// controller cannot drift from a built one.
function setGlyph(button, glyphName, label) {
  button.replaceChildren(glyph(glyphName));
  button.title = label;
  button.setAttribute('aria-label', label);
}

function buildController(record, onCommand, refreshHeight) {
  const item = document.createElement('div');
  item.className = 'sbm-item';
  item.dataset.tabId = record.id;
  // The live closure, not the one this build captured: a later render replaces
  // it, and a patched controller outlives the render that made it.
  const refresh = () => (item.parentNode?.sbmRefreshHeight || refreshHeight)();
  item.addEventListener('mouseenter', () => {
    item.hovered = true;
    refresh();
  });
  item.addEventListener('mouseleave', () => {
    item.hovered = false;
    refresh();
  });

  const titleText = record.title || record.host || '';
  const title = document.createElement('button');
  title.type = 'button';
  title.className = 'sbm-title';
  title.textContent = titleText;
  title.title = 'Back to Tab';
  title.setAttribute('aria-label', 'Back to Tab: ' + titleText);
  title.addEventListener('click', (e) => {
    e.stopPropagation();
    onCommand('mediaBackToTab', record.id);
  });

  const playBtn = buildButton('mediaPlayPause', record.id,
      record.playing ? 'Pause' : 'Play', record.playing ? 'pause' : 'play',
      onCommand);
  playBtn.classList.add('sbm-play');

  // A track the page cannot skip still holds its place, dimmed: the row reads
  // as one transport whichever page fills it, and never reflows between pages.
  const prevBtn =
      buildButton('mediaPrev', record.id, 'Previous Track', 'prev', onCommand);
  prevBtn.classList.add('sbm-prev');
  prevBtn.disabled = !record.canPrev;

  const nextBtn =
      buildButton('mediaNext', record.id, 'Next Track', 'next', onCommand);
  nextBtn.classList.add('sbm-next');
  nextBtn.disabled = !record.canNext;

  const pipLabel = record.pictureInPicture ?
      'Exit Picture-in-Picture' : 'Picture-in-Picture';
  const pipBtn = buildButton(
      'mediaPictureInPicture', record.id, pipLabel, 'pip', onCommand);
  pipBtn.classList.add('sbm-pip-btn');
  pipBtn.setAttribute('aria-pressed', String(!!record.pictureInPicture));

  const closeBtn = buildButton('mediaClosed', record.id, 'Close', 'close', onCommand);
  closeBtn.classList.add('sbm-close-btn');

  const startVolume = typeof record.volume === 'number' ? record.volume : 1;
  const volBtn = document.createElement('button');
  volBtn.className = 'sbm-btn sbm-vol-btn';
  volBtn.title = 'Volume';
  volBtn.setAttribute('aria-label', 'Volume');
  volBtn.appendChild(glyph(startVolume > 0 ? 'sound' : 'mute'));
  volBtn.addEventListener('click', (e) => {
    e.stopPropagation();
    const opening = !item.classList.contains('sbm-vol-open');
    item.parentNode?.querySelectorAll('.sbm-item').forEach((other) => {
      if (other !== item) {
        other.classList.remove('sbm-vol-open');
      }
    });
    item.classList.toggle('sbm-vol-open', opening);
    refresh();
  });

  const volRange = document.createElement('input');
  volRange.type = 'range';
  volRange.className = 'sbm-vol-range';
  volRange.min = '0';
  volRange.max = '1';
  volRange.step = '0.01';
  volRange.value = String(startVolume);
  volRange.setAttribute('aria-label', 'Volume level');
  // Each drag step sends a command, and the hub answers with a push of its own.
  // That answer lags the thumb, so writing it back mid-drag drags the thumb
  // backwards under the finger and the slider reads as dead.
  let dragging = false;
  volRange.addEventListener('pointerdown', () => {
    dragging = true;
  });
  for (const done of ['pointerup', 'pointercancel', 'change']) {
    volRange.addEventListener(done, () => {
      dragging = false;
    });
  }
  volRange.sbmHeld = () => dragging || document.activeElement === volRange;
  volRange.addEventListener('input', (e) => {
    onCommand('mediaVolume', record.id, Number(e.target.value));
  });
  const volBand = document.createElement('div');
  volBand.className = 'sbm-volband';
  volBand.appendChild(volRange);

  item.append(...[
    title, buildFavicon(record.id), prevBtn, playBtn, nextBtn, pipBtn,
    closeBtn, volBtn, volBand,
  ].filter(Boolean));

  // Every command the row sends comes back as a fresh push, so a rebuild on
  // each one tore the hovered row - and an open volume band - out from under
  // the pointer. The same state drawn on the same nodes patches instead.
  item.sbmUpdate = (next) => {
    const text = next.title || next.host || '';
    if (title.textContent !== text) {
      title.textContent = text;
      title.setAttribute('aria-label', 'Back to Tab: ' + text);
    }
    setGlyph(playBtn, next.playing ? 'pause' : 'play',
             next.playing ? 'Pause' : 'Play');
    prevBtn.disabled = !next.canPrev;
    nextBtn.disabled = !next.canNext;
    setGlyph(pipBtn, 'pip', next.pictureInPicture ?
        'Exit Picture-in-Picture' : 'Picture-in-Picture');
    pipBtn.setAttribute('aria-pressed', String(!!next.pictureInPicture));
    const level = typeof next.volume === 'number' ? next.volume : 1;
    setGlyph(volBtn, level > 0 ? 'sound' : 'mute', 'Volume');
    if (!volRange.sbmHeld()) {
      volRange.value = String(level);
    }
  };
  return item;
}

export function renderMediaBlock(container, {
  records = [],
  onCommand = () => {},
  onHeightChange = () => {},
} = {}) {
  // A record with no tab id can be commanded by nothing, so a controller for
  // it would be a row of dead buttons.
  const list = (records || []).filter((r) => r && typeof r.id === 'string' && r.id);
  container.classList.toggle('sbm-hidden', list.length === 0);
  container.style.setProperty(
      '--sbm-rows', String(Math.min(list.length, MAX_SHOWN_CONTROLLERS)));
  const refreshHeight = () => {
    const h = activeHeight(container);
    container.style.setProperty('--sbm-h', h + 'px');
    onHeightChange(h);
  };
  // The dismiss entry outlives this render, and every render builds a fresh
  // closure over the new children.
  container.sbmRefreshHeight = refreshHeight;
  const drawn = [...container.querySelectorAll('.sbm-item')];
  const sameRows = drawn.length === list.length &&
      drawn.every((item, i) => item.dataset.tabId === list[i].id &&
                  typeof item.sbmUpdate === 'function');
  if (sameRows) {
    list.forEach((record, i) => drawn[i].sbmUpdate(record));
  } else {
    container.replaceChildren();
    for (const record of list) {
      container.appendChild(buildController(record, onCommand, refreshHeight));
    }
  }
  refreshHeight();
  return list.length;
}

// Keyed by root so a second sidebarReady handshake tears down its own prior
// container instead of orphaning it.
const installsByRoot = new WeakMap();

export function installSidebarMedia(root) {
  const prior = installsByRoot.get(root);
  if (prior) {
    prior.release();
    prior.container.remove();
  }

  const container = document.createElement('div');
  container.className = 'sbm-block sbm-hidden';
  (root.parentNode || document.body).insertBefore(container, root);

  let records = [];
  function update() {
    const shown = renderMediaBlock(container, {
      records,
      onCommand: (command, tabId, level) => chrome.send(
          command, level === undefined ? [tabId] : [tabId, level]),
      // The block is the root's sibling, so the tab list under the root
      // cannot read the drawn height off it directly.
      onHeightChange: (h) => root.style.setProperty('--sbm-h', h + 'px'),
    });
    root.classList.toggle('sbm-reflow', shown > 0);
    root.style.setProperty(
        '--sbm-rows', String(Math.min(shown, MAX_SHOWN_CONTROLLERS)));
  }

  // The hub pushes on the sidebarReady handshake and on every change. A hub
  // answers one profile only, so a private window's block starts empty.
  window.atriumMediaSessionsChanged = (list) => {
    records = Array.isArray(list) ? list : [];
    update();
  };

  // The volume band is a grown-in row, not a popover, but Esc and a click
  // outside still have to close it. The shared registry is the one close path.
  const release = registerMenu({
    close: () => {
      for (const item of container.querySelectorAll('.sbm-vol-open')) {
        item.classList.remove('sbm-vol-open');
      }
      container.sbmRefreshHeight?.();
    },
    isOpen: () => !!container.querySelector('.sbm-vol-open'),
  });

  update();
  installsByRoot.set(root, {container, release});
  return container;
}
