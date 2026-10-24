#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

const h = installDOM();
const {installSpaceIconPicker} = await import(
    path.join(ATRIUM_DIR, 'space-icon-picker.js'));
const {PHOSPHOR_NAMES} = await import(
    path.join(ATRIUM_DIR, 'phosphor/fill.js'));

function fresh() {
  h.reset();
  const root = document.createElement('div');
  document.body.appendChild(root);
  const card = installSpaceIconPicker(root);
  return {root, card};
}

function open(index, value) {
  window.atriumOpenSpaceIconPicker(index, value);
}

const fire = (node, type, init = {}) => {
  const event = new Event(type, {bubbles: true, ...init});
  node.dispatchEvent(event);
  return event;
};

function sentSpaceIconSet() {
  return h.sent.filter(([name]) => name === 'sidebarSpaceIconSet');
}

test('sends spaceIconPickerReady on install', () => {
  fresh();
  assert.deepEqual(h.sent, [['spaceIconPickerReady', []]]);
});

test('opens on the Emoji tab by default, 64 cells', () => {
  const {card} = fresh();
  open(2, '');
  assert.equal(card.querySelector('.settings-seg-option.on').dataset.tab, 'emoji');
  assert.equal(card.querySelectorAll('.spicon-cell').length, 64);
});

test('opens on the Icon tab when the current value is a ph: pick', () => {
  const {card} = fresh();
  open(2, 'ph:star');
  assert.equal(card.querySelector('.settings-seg-option.on').dataset.tab, 'icon');
  assert.equal(card.querySelectorAll('.spicon-cell').length, PHOSPHOR_NAMES.length);
  assert.ok(card.querySelector('.spicon-cell svg'));
});

test('the grid is 8 columns wide', () => {
  const css = fs.readFileSync(
      path.join(ATRIUM_DIR, 'space-icon-picker.css'), 'utf8');
  assert.match(css, /grid-template-columns:\s*repeat\(8,\s*1fr\)/);
});

test('switching tabs re-renders the grid', () => {
  const {card} = fresh();
  open(2, '');
  const iconTab = [...card.querySelectorAll('.settings-seg-option')]
      .find((el) => el.dataset.tab === 'icon');
  iconTab.click();
  assert.equal(card.querySelectorAll('.spicon-cell').length, PHOSPHOR_NAMES.length);
  assert.ok(card.querySelector('.spicon-cell svg'));
});

test('a cell click sends the space index and value', () => {
  const {card} = fresh();
  open(2, '');
  card.querySelectorAll('.spicon-cell')[0].click();
  assert.equal(sentSpaceIconSet().length, 1);
  const [, args] = sentSpaceIconSet()[0];
  assert.equal(args[0], 2);
  assert.equal(typeof args[1], 'string');
  assert.ok(args[1].length > 0);
});

test('the trash button sends an empty value', () => {
  const {card} = fresh();
  open(2, 'ph:star');
  card.querySelector('.spicon-trash').click();
  assert.deepEqual(sentSpaceIconSet(), [['sidebarSpaceIconSet', [2, '']]]);
});

test('reopening for a different space resets the index and selection', () => {
  const {card} = fresh();
  open(2, 'ph:star');
  open(5, '');
  card.querySelectorAll('.spicon-cell')[0].click();
  assert.deepEqual(sentSpaceIconSet(), [['sidebarSpaceIconSet', [5, '😀']]]);
  assert.equal(card.querySelector('.settings-seg-option.on').dataset.tab, 'emoji');
});

test('opening the picker focuses the current cell, so arrows and a click drive it from the keyboard', () => {
  const {card} = fresh();
  open(2, 'ph:star');
  const cells = [...card.querySelectorAll('.spicon-cell')];
  const starIndex = cells.findIndex((c) => c.dataset.value === 'ph:star');
  assert.equal(document.activeElement, cells[starIndex]);
  const grid = card.querySelector('.spicon-grid');
  fire(grid, 'keydown', {key: 'ArrowRight'});
  assert.equal(document.activeElement, cells[starIndex + 1]);
  document.activeElement.click();  // what a native Enter does on a focused button
  assert.equal(sentSpaceIconSet().length, 1);
});

test('every Icon-tab cell has a readable aria-label, not the raw SVG', () => {
  const {card} = fresh();
  open(2, '');
  const iconTab = [...card.querySelectorAll('.settings-seg-option')]
      .find((el) => el.dataset.tab === 'icon');
  iconTab.click();
  for (const cell of card.querySelectorAll('.spicon-cell')) {
    const label = cell.getAttribute('aria-label');
    assert.ok(label && label.length > 0, `cell for ${cell.dataset.value} has no label`);
    assert.ok(!label.includes('-'), `label "${label}" still has the raw kebab-case name`);
  }
  const bookmarkCell = [...card.querySelectorAll('.spicon-cell')]
      .find((c) => c.dataset.value === 'ph:bookmark-simple');
  assert.equal(bookmarkCell.getAttribute('aria-label'), 'bookmark simple');
});

test('Emoji-tab cells still read their emoji', () => {
  const {card} = fresh();
  open(2, '');
  const cell = card.querySelectorAll('.spicon-cell')[0];
  assert.equal(cell.textContent, cell.dataset.value);
});

test('the grid is a labelled listbox of options', () => {
  const {card} = fresh();
  open(2, '');
  const grid = card.querySelector('.spicon-grid');
  assert.equal(grid.getAttribute('role'), 'listbox');
  assert.ok(grid.getAttribute('aria-label'));
  for (const cell of card.querySelectorAll('.spicon-cell')) {
    assert.equal(cell.getAttribute('role'), 'option');
  }
});

test('the current value is marked aria-selected and is the only tab stop', () => {
  const {card} = fresh();
  open(2, 'ph:star');
  const cells = [...card.querySelectorAll('.spicon-cell')];
  const selected = cells.filter((c) => c.getAttribute('aria-selected') === 'true');
  assert.equal(selected.length, 1);
  assert.equal(selected[0].dataset.value, 'ph:star');
  const tabbable = cells.filter((c) => c.tabIndex === 0);
  assert.deepEqual(tabbable, selected);
});

test('ArrowRight/ArrowLeft/ArrowDown/ArrowUp move the roving tab stop through the grid', () => {
  const {card} = fresh();
  open(2, '');
  const cells = [...card.querySelectorAll('.spicon-cell')];
  const grid = card.querySelector('.spicon-grid');
  cells[0].focus();
  fire(grid, 'keydown', {key: 'ArrowRight'});
  assert.equal(document.activeElement, cells[1]);
  assert.equal(cells[1].tabIndex, 0);
  assert.equal(cells[0].tabIndex, -1);
  fire(grid, 'keydown', {key: 'ArrowDown'});
  assert.equal(document.activeElement, cells[9]);
  fire(grid, 'keydown', {key: 'ArrowUp'});
  assert.equal(document.activeElement, cells[1]);
  fire(grid, 'keydown', {key: 'ArrowLeft'});
  assert.equal(document.activeElement, cells[0]);
});

test('arrow keys do not move focus past the first or last cell', () => {
  const {card} = fresh();
  open(2, '');
  const cells = [...card.querySelectorAll('.spicon-cell')];
  const grid = card.querySelector('.spicon-grid');
  cells[0].focus();
  fire(grid, 'keydown', {key: 'ArrowLeft'});
  assert.equal(document.activeElement, cells[0]);
  cells[cells.length - 1].focus();
  fire(grid, 'keydown', {key: 'ArrowRight'});
  assert.equal(document.activeElement, cells[cells.length - 1]);
});

test('the Emoji/Icon radiogroup moves focus and selection with ArrowLeft/ArrowRight', () => {
  const {card} = fresh();
  open(2, '');
  const emojiOption = card.querySelector('[data-tab="emoji"]');
  const iconOption = card.querySelector('[data-tab="icon"]');
  emojiOption.focus();
  fire(emojiOption, 'keydown', {key: 'ArrowRight'});
  assert.equal(document.activeElement, iconOption);
  assert.equal(iconOption.getAttribute('aria-checked'), 'true');
  assert.equal(card.querySelectorAll('.spicon-cell').length, PHOSPHOR_NAMES.length);
  fire(iconOption, 'keydown', {key: 'ArrowLeft'});
  assert.equal(document.activeElement, emojiOption);
  assert.equal(emojiOption.getAttribute('aria-checked'), 'true');
});
