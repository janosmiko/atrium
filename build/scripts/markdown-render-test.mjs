#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// ai-panel-test.mjs covers the reply reaching this renderer.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');

const h = installDOM({pathname: '/ai-panel'});
const {renderMarkdown} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/markdown-render.js'));

function render(text) {
  const box = document.createElement('div');
  renderMarkdown(box, text);
  return box;
}

const tags = (box) => [...box.childNodes].map((n) => n.tagName);

test('a bullet list renders as a list, not asterisks in a paragraph', () => {
  const box = render('Here:\n- first\n- second');
  assert.deepEqual(tags(box), ['P', 'UL']);
  assert.equal(box.childNodes[1].childNodes.length, 2);
  assert.equal(box.childNodes[1].childNodes[0].textContent, 'first');
  assert.ok(!box.textContent.includes('*'),
            'the markers must be consumed, not shown');
});

test('a dash list and a star list both render', () => {
  assert.deepEqual(tags(render('* one\n* two')), ['UL']);
  assert.deepEqual(tags(render('- one\n- two')), ['UL']);
});

test('headings render at their level', () => {
  assert.deepEqual(tags(render('# One\n## Two\n### Three')), ['H1', 'H2', 'H3']);
  assert.equal(render('## Two').childNodes[0].textContent, 'Two');
});

test('bold, italic and inline code render', () => {
  const bold = render('a **b** c').childNodes[0];
  assert.equal(bold.childNodes[1].tagName, 'STRONG');
  assert.equal(bold.childNodes[1].textContent, 'b');
  assert.equal(render('a *b* c').childNodes[0].childNodes[1].tagName, 'EM');
  assert.equal(render('a `b` c').childNodes[0].childNodes[1].tagName, 'CODE');
});

// Bold before italic, or the two stars of **x** read as an italic wrapping *x*.
test('bold wins over italic on the same run of stars', () => {
  const el = render('**bold**').childNodes[0].childNodes[0];
  assert.equal(el.tagName, 'STRONG');
  assert.equal(el.textContent, 'bold');
});

test('a link renders with its href', () => {
  const link = render('see [docs](https://example.com/x)').childNodes[0].childNodes[1];
  assert.equal(link.tagName, 'A');
  assert.equal(link.textContent, 'docs');
  assert.equal(link.getAttribute('href'), 'https://example.com/x');
});

// A reply is built from page text an attacker controls, so these are the
// cases a prompt injection would actually try.
test('HTML in a reply is shown as text, never built into nodes', () => {
  const box = render('<img src=x onerror="alert(1)"> and <b>bold</b>');
  assert.deepEqual(tags(box), ['P']);
  assert.equal(box.childNodes[0].childNodes.length, 1,
               'the markup must stay one text node');
  assert.ok(box.textContent.includes('<img src=x onerror="alert(1)">'),
            'the markup must be readable as text');
  assert.ok(!box.querySelector('img'), 'no element may be built from the reply');
  assert.ok(!box.querySelector('b'), 'no element may be built from the reply');
});

test('a javascript: link renders as text and builds no anchor', () => {
  // The presence half first: a null from querySelector proves nothing unless
  // the same call finds a link that IS supposed to be there.
  assert.ok(render('[ok](https://example.com)').querySelector('a'),
            'querySelector cannot see anchors, so the checks below are vacuous');

  const box = render('[click](javascript:alert(1))');
  assert.equal(box.querySelector('a'), null, 'an executable scheme must not become a link');
  assert.ok(box.textContent.includes('[click](javascript:alert(1))'));
});

test('a data: link renders as text and builds no anchor', () => {
  const box = render('[x](data:text/html,<script>alert(1)</script>)');
  assert.equal(box.querySelector('a'), null);
});

test('a script tag spelled across a bullet stays text', () => {
  const box = render('- <script>alert(1)</script>');
  assert.equal(box.querySelector('script'), null);
  assert.equal(box.childNodes[0].childNodes[0].textContent,
               '<script>alert(1)</script>');
});

test('an empty or absent reply renders nothing rather than throwing', () => {
  assert.equal(render('').childNodes.length, 0);
  assert.equal(render(null).childNodes.length, 0);
});

h.reset();
