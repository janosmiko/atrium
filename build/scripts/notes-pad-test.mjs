#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the Notes pad's markdown shortcut and Enter-exit rules.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const MODULE = path.join(SRC, 'chrome/browser/resources/atrium/notes-pad.js');

const {
  blockTypeForMarker,
  inlineMatchBeforeCaret,
  enterAction,
  buildBlockElement,
  renderMarkdownIntoPad,
  serializeToMarkdown,
} = await import(MODULE);

test('#, ##, ### open h1/h2/h3; - and * open a list; 1. opens a numbered list; > opens a quote', () => {
  assert.equal(blockTypeForMarker('#'), 'h1');
  assert.equal(blockTypeForMarker('##'), 'h2');
  assert.equal(blockTypeForMarker('###'), 'h3');
  assert.equal(blockTypeForMarker('-'), 'ul');
  assert.equal(blockTypeForMarker('*'), 'ul');
  assert.equal(blockTypeForMarker('1.'), 'ol');
  assert.equal(blockTypeForMarker('>'), 'blockquote');
  assert.equal(blockTypeForMarker('##letters'), null);
  assert.equal(blockTypeForMarker(''), null);
});

test('**bold** closes on the second **', () => {
  const m = inlineMatchBeforeCaret('hello **world**');
  assert.deepEqual(m, {tag: 'b', inner: 'world', length: 9});
});

test('*italic* closes on the second star, without eating a bold pair', () => {
  const m = inlineMatchBeforeCaret('hello *world*');
  assert.deepEqual(m, {tag: 'i', inner: 'world', length: 7});
  // A completed bold span must not also read as two italics.
  assert.equal(inlineMatchBeforeCaret('**world**')?.tag, 'b');
});

test('_italic_ closes on the second underscore', () => {
  const m = inlineMatchBeforeCaret('hello _world_');
  assert.deepEqual(m, {tag: 'i', inner: 'world', length: 7});
});

test('`code` closes on the second backtick', () => {
  const m = inlineMatchBeforeCaret('hello `world`');
  assert.deepEqual(m, {tag: 'code', inner: 'world', length: 7});
});

test('no inline match when nothing has closed yet', () => {
  assert.equal(inlineMatchBeforeCaret('hello **world'), null);
  assert.equal(inlineMatchBeforeCaret('plain text'), null);
});

test('Enter on an empty list item exits the list', () => {
  assert.equal(enterAction('ul', '', 0), 'exit');
  assert.equal(enterAction('ol', '   ', 3), 'exit');
  assert.equal(enterAction('ul', 'still typing', 5), null);
});

test('Enter at the end of a heading or quote exits the block', () => {
  assert.equal(enterAction('h1', 'Title', 5), 'exit');
  assert.equal(enterAction('blockquote', 'A quote', 7), 'exit');
  // Mid-text Enter is an ordinary line break within the block.
  assert.equal(enterAction('h2', 'Title', 2), null);
  assert.equal(enterAction('p', 'Title', 5), null);
});

installDOM({pathname: '/notes'});

test('markdown round-trips through the pad: parse, serialize, parse again', () => {
  const pad = document.createElement('div');
  const body = '# Heading\nplain line\n- one\n- two\n> a quote';
  renderMarkdownIntoPad(pad, body);
  assert.equal(pad.children.length, 4, 'heading, paragraph, one <ul>, one quote');
  assert.equal(serializeToMarkdown(pad), body);
});

test('an empty note renders one empty paragraph rather than nothing', () => {
  const pad = document.createElement('div');
  renderMarkdownIntoPad(pad, '');
  assert.equal(pad.children.length, 1);
  assert.equal(pad.children[0].tagName.toLowerCase(), 'p');
});

test('buildBlockElement writes via textContent, so markup in a note is inert text', () => {
  const el = buildBlockElement(document, 'h1', '<script>x</script>');
  assert.equal(el.tagName.toLowerCase(), 'h1');
  assert.equal(el.textContent, '<script>x</script>');
});
