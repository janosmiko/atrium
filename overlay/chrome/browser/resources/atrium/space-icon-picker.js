// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The /spaceicon route: a native widget's own page, not an
// in-sidebar popover - the sidebar can be as narrow as 160px, too narrow for
// this grid at a usable cell size.

import {spaceGlyph} from './space-glyph.js';
import {icon} from './icons.js';
import {PHOSPHOR_NAMES} from './phosphor/fill.js';

// about 64 curated, single-codepoint emoji - no skin-tone or ZWJ
// sequences, which render as two grid cells' worth of glyph in some fonts.
const EMOJI = [
  '😀', '😎', '🤓', '🥳', '😴', '🤔', '👍', '👀',
  '🔥', '✨', '💡', '🎯', '🎨', '🎮', '🎵', '🎬',
  '📚', '📝', '📌', '📎', '🔒', '🔑', '⚙️', '🧩',
  '💻', '📱', '🖥️', '⌨️', '🖱️', '📷', '🎧', '🔋',
  '🌍', '🌙', '☀️', '⭐', '☁️', '🌈', '⚡', '🌊',
  '🌲', '🌵', '🌸', '🍀', '🍎', '🍕', '☕', '🍺',
  '🐶', '🐱', '🦊', '🐼', '🦁', '🐢', '🦋', '🐙',
  '🚀', '✈️', '🚗', '🚲', '🏠', '🏢', '🗺️', '🎉',
];

function buildTabSwitch(current, onChange) {
  const tabs = ['emoji', 'icon'];
  const labels = {emoji: 'Emoji', icon: 'Icon'};
  const wrap = document.createElement('div');
  wrap.className = 'settings-seg spicon-seg';
  wrap.setAttribute('role', 'radiogroup');
  wrap.setAttribute('aria-label', 'Icon type');
  wrap.style.setProperty(
      '--w', `calc((100% - ${3 * (tabs.length + 1)}px) / ${tabs.length})`);
  wrap.style.setProperty('--i', String(tabs.indexOf(current)));
  const thumb = document.createElement('span');
  thumb.className = 'settings-seg-thumb';
  wrap.appendChild(thumb);
  const options = tabs.map((tab) => {
    const option = document.createElement('button');
    option.type = 'button';
    option.className = 'settings-seg-option';
    option.dataset.tab = tab;
    option.setAttribute('role', 'radio');
    option.textContent = labels[tab];
    return option;
  });
  const select = (tab) => {
    wrap.style.setProperty('--i', String(tabs.indexOf(tab)));
    for (const option of options) {
      const on = option.dataset.tab === tab;
      option.classList.toggle('on', on);
      option.setAttribute('aria-checked', String(on));
      option.tabIndex = on ? 0 : -1;
    }
  };
  for (const [index, option] of options.entries()) {
    option.addEventListener('click', () => {
      select(option.dataset.tab);
      onChange(option.dataset.tab);
    });
    option.addEventListener('keydown', (event) => {
      const step = event.key === 'ArrowRight' ? 1
          : event.key === 'ArrowLeft' ? -1 : 0;
      if (!step) {
        return;
      }
      event.preventDefault();
      const next = options[(index + step + options.length) % options.length];
      select(next.dataset.tab);
      next.focus();
      onChange(next.dataset.tab);
    });
    wrap.appendChild(option);
  }
  select(current);
  return {el: wrap, select};
}

const GRID_COLS = 8;

function markSelectedCell(cells, currentValue) {
  let focusable = cells[0];
  for (const cell of cells) {
    const on = cell.dataset.value === currentValue;
    cell.setAttribute('aria-selected', String(on));
    cell.tabIndex = -1;
    if (on) {
      focusable = cell;
    }
  }
  if (focusable) {
    focusable.tabIndex = 0;
  }
  return focusable;
}

function moveGridFocus(grid, event) {
  const step = event.key === 'ArrowRight' ? 1
      : event.key === 'ArrowLeft' ? -1
      : event.key === 'ArrowDown' ? GRID_COLS
      : event.key === 'ArrowUp' ? -GRID_COLS : 0;
  if (!step) {
    return;
  }
  const cells = Array.from(grid.querySelectorAll('.spicon-cell'));
  const from = cells.indexOf(document.activeElement);
  const to = from + step;
  if (from === -1 || to < 0 || to >= cells.length) {
    return;
  }
  event.preventDefault();
  cells[from].tabIndex = -1;
  cells[to].tabIndex = 0;
  cells[to].focus();
}

export function installSpaceIconPicker(root) {
  const send = (name, args) => chrome.send(name, args);
  let spaceIndex = -1;

  const card = document.createElement('div');
  card.className = 'ov-card spiconpop';

  const top = document.createElement('div');
  top.className = 'spicon-top';

  const grid = document.createElement('div');
  grid.className = 'spicon-grid';
  grid.setAttribute('role', 'listbox');
  grid.addEventListener('keydown', (event) => moveGridFocus(grid, event));

  function pick(value) {
    send('sidebarSpaceIconSet', [spaceIndex, value]);
  }

  const renderGrid = (tab, currentValue) => {
    grid.replaceChildren();
    grid.setAttribute('aria-label', tab === 'icon' ? 'Icon' : 'Emoji');
    const values = tab === 'icon'
        ? PHOSPHOR_NAMES.map((name) => 'ph:' + name)
        : EMOJI;
    const cells = values.map((value) => {
      const cell = document.createElement('button');
      cell.type = 'button';
      cell.className = 'spicon-cell';
      cell.setAttribute('role', 'option');
      cell.dataset.value = value;
      if (tab === 'icon') {
        cell.setAttribute('aria-label', value.slice(3).replace(/-/g, ' '));
      }
      cell.appendChild(spaceGlyph(value, ''));
      cell.addEventListener('click', () => pick(value));
      grid.appendChild(cell);
      return cell;
    });
    return markSelectedCell(cells, currentValue);
  };

  const trash = document.createElement('button');
  trash.type = 'button';
  trash.className = 'spicon-trash';
  trash.setAttribute('aria-label', 'Remove icon');
  trash.appendChild(icon('trash'));
  trash.addEventListener('click', () => pick(''));

  let tabSwitch = null;

  card.append(top, grid);
  root.appendChild(card);

  window.atriumOpenSpaceIconPicker = (index, value) => {
    spaceIndex = index;
    const startTab = value.startsWith('ph:') ? 'icon' : 'emoji';
    tabSwitch?.el.remove();
    tabSwitch = buildTabSwitch(startTab, (tab) => renderGrid(tab, value));
    top.replaceChildren(tabSwitch.el, trash);
    const focusable = renderGrid(startTab, value);
    // preventScroll: the auto-resized widget has no scroll container of its
    // own to disturb, but a plain .focus() still fires a scrollIntoView on
    // whatever ancestor the page picks.
    focusable?.focus({preventScroll: true});
  };

  chrome.send('spaceIconPickerReady', []);
  return card;
}
