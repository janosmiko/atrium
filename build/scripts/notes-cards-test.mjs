#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Marginalia comment cards in the Notes pad.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';
import {waitFor} from './wait-for.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const CARDS = path.join(SRC, 'chrome/browser/resources/atrium/notes-cards.js');
const NOTES = path.join(SRC, 'chrome/browser/resources/atrium/notes.js');

const harness = installDOM({pathname: '/notes'});
const {buildCommentCard, renderCommentCards, flashCard, FLASH_CLASS} =
    await import(CARDS);
const {installNotes} = await import(NOTES);

function mark(id, quote, note = '') {
  return {id, url: 'https://example.com/doc', quote, note};
}

test('a highlight renders one card holding the quote and an empty editable note', () => {
  const card = buildCommentCard(document, mark('m1', 'the passage'));
  assert.equal(card.dataset.markId, 'm1');
  const quote = card.querySelector('.mquote');
  const field = card.querySelector('.mnote');
  assert.equal(quote.textContent, 'the passage');
  assert.equal(field.textContent, '', 'a fresh highlight starts with no note');
  assert.equal(field.contentEditable, 'true');
  assert.equal(quote.contentEditable, undefined, 'the quote is not editable');
});

test('a quote is written as text, so markup on the page stays inert', () => {
  const card = buildCommentCard(
      document, mark('m1', '<img src=x onerror=alert(1)>'));
  const quote = card.querySelector('.mquote');
  assert.equal(quote.textContent, '<img src=x onerror=alert(1)>');
  assert.equal(quote.children.length, 0, 'no element was parsed out of it');
});

test('one card per highlight, in the order given, and a re-render replaces them', () => {
  const host = document.createElement('div');
  renderCommentCards(host, [mark('m1', 'first'), mark('m2', 'second', 'said')]);
  assert.equal(host.children.length, 2);
  assert.equal(host.children[1].querySelector('.mnote').textContent, 'said');

  renderCommentCards(host, [mark('m3', 'only one now')]);
  assert.equal(host.children.length, 1, 'the stale cards are gone');
  assert.equal(host.children[0].dataset.markId, 'm3');

  renderCommentCards(host, undefined);
  assert.equal(host.children.length, 0, 'a note with no highlights renders none');
});

test('pad half: flashCard finds the linked card, scrolls it and flashes it', () => {
  const host = document.createElement('div');
  renderCommentCards(host, [mark('m1', 'first'), mark('m2', 'second')]);
  let scrolled = null;
  for (const card of host.children) {
    card.scrollIntoView = () => {
      scrolled = card.dataset.markId;
    };
  }

  assert.equal(flashCard(host, 'm2'), true);
  assert.equal(scrolled, 'm2');
  assert.equal(host.children[1].classList.contains(FLASH_CLASS), true);
  assert.equal(host.children[0].classList.contains(FLASH_CLASS), false);

  assert.equal(flashCard(host, 'no-such-mark'), false,
      'a mark whose card is gone must not throw');
});

// The dom-shim's selector parser splits attribute values on a bare `"`, so
// it can't distinguish an escaped id from an unescaped one - source is the
// only place this fix shows.
test('flashCard escapes the mark id before building its selector', () => {
  const src = fs.readFileSync(CARDS, 'utf8');
  assert.match(src, /CSS\.escape\(id\)/);
});

test('typing in a card sends marginaliaNoteChanged with that card id', async () => {
  harness.reset();
  const root = document.createElement('div');
  document.body.appendChild(root);
  installNotes(root);

  window.atriumSetNote({
    title: 'Notes',
    body: '',
    marginalia: [mark('m1', 'first'), mark('m2', 'second')],
  });
  const cards = root.querySelector('.notes-cards');
  assert.equal(cards.children.length, 2);

  const field = cards.children[1].querySelector('.mnote');
  field.textContent = 'why this matters';
  field.dispatchEvent(new Event('input', {bubbles: true}));

  const before = harness.sent.filter((m) => m[0] === 'marginaliaNoteChanged');
  assert.equal(before.length, 0, 'the save is debounced, not sent per keystroke');
  // The pad's own save uses a real setTimeout, which the DOM shim leaves alone.
  await waitFor(() => harness.sent.some((m) => m[0] === 'marginaliaNoteChanged'));

  const sent = harness.sent.filter((m) => m[0] === 'marginaliaNoteChanged');
  assert.equal(sent.length, 1);
  assert.deepEqual(sent[0][1], ['m2', 'why this matters']);
});

test('the route exposes atriumFlashMarginal for a click on a page mark', () => {
  harness.reset();
  const root = document.createElement('div');
  document.body.appendChild(root);
  installNotes(root);
  window.atriumSetNote({title: 'Notes', body: '', marginalia: [mark('m1', 'q')]});

  assert.equal(typeof window.atriumFlashMarginal, 'function');
  assert.equal(window.atriumFlashMarginal('m1'), true);
  assert.equal(window.atriumFlashMarginal('m9'), false);
});

test("clicking a card's quote sends marginalMarkJumpRequested with its id", () => {
  harness.reset();
  const root = document.createElement('div');
  document.body.appendChild(root);
  installNotes(root);
  window.atriumSetNote({
    title: 'Notes',
    body: '',
    marginalia: [mark('m1', 'first'), mark('m2', 'second')],
  });

  const cards = root.querySelector('.notes-cards');
  cards.children[1].querySelector('.mquote')
      .dispatchEvent(new Event('click', {bubbles: true}));

  const sent = harness.sent.filter((m) => m[0] === 'marginalMarkJumpRequested');
  assert.equal(sent.length, 1);
  assert.deepEqual(sent[0][1], ['m2']);
});

test("clicking a card's note field does not jump - it places the caret", () => {
  harness.reset();
  const root = document.createElement('div');
  document.body.appendChild(root);
  installNotes(root);
  window.atriumSetNote({title: 'Notes', body: '', marginalia: [mark('m1', 'q')]});

  root.querySelector('.mnote').dispatchEvent(new Event('click', {bubbles: true}));

  assert.equal(
      harness.sent.filter((m) => m[0] === 'marginalMarkJumpRequested').length,
      0);
});
