#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

const h = installDOM({pathname: '/ai-panel'});
const {installAiPanel} = await import(path.join(ATRIUM_DIR, 'ai-panel.js'));

function panelOn() {
  h.reset();
  const root = document.createElement('div');
  h.body.appendChild(root);
  installAiPanel(root, {getContext: () => ({pageTitle: 'Doc'})});
  h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
  return root;
}

const press = (key) =>
    document.dispatchEvent(new Event('keydown', {bubbles: true, key}));

test('vim off leaves the chat log where it was', () => {
  const root = panelOn();
  const log = root.querySelector('.aichatlog');
  log.scrollTop = 0;
  log.scrollHeight = 1000;

  press('j');
  assert.equal(log.scrollTop, 0, 'a key must do nothing while the mode is off');
});

test('vim on scrolls the answer rather than moving a cursor', () => {
  const root = panelOn();
  const log = root.querySelector('.aichatlog');
  log.scrollTop = 0;
  log.scrollHeight = 1000;
  window.atriumSetVimModeEnabled(true);
  // Opening the panel focuses the prompt, and vim yields to a focused field.
  // Motion is for a reader who has left it.
  document.activeElement = h.body;

  press('j');
  assert.ok(log.scrollTop > 0, 'j has to scroll the panel');
  const afterDown = log.scrollTop;
  press('k');
  assert.ok(log.scrollTop < afterDown);
  window.atriumSetVimModeEnabled(false);
});

test('the panel keeps its own backtick hotkey', () => {
  const root = panelOn();
  window.atriumSetVimModeEnabled(true);

  // The grammar has no verb for it, so it must reach the panel's own handler
  // rather than being swallowed.
  h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
  assert.equal(root.classList.contains('aipopen'), false,
               'the backtick still closes the panel with vim on');
  window.atriumSetVimModeEnabled(false);
});

test('typing in the prompt is typing, not motion', () => {
  const root = panelOn();
  const log = root.querySelector('.aichatlog');
  log.scrollTop = 0;
  log.scrollHeight = 1000;
  window.atriumSetVimModeEnabled(true);
  document.activeElement = root.querySelector('.aichatinput');

  press('j');
  assert.equal(log.scrollTop, 0, 'a letter typed into the prompt must not move the panel');
  window.atriumSetVimModeEnabled(false);
});

test('the kill switch takes the listener with it', () => {
  panelOn();
  window.atriumSetVimModeEnabled(true);
  // The presence half: without it the absence below passes on a build where
  // nothing was ever installed.
  const listeners = () => h.body._listeners.get('keydown')?.length ?? 0;
  assert.ok(listeners() > 0, 'setup: the panel installed its listeners');

  window.atriumSetAiPanelEnabled(false);
  assert.equal(listeners(), 0,
               'a vim listener outliving the panel is the leak route-test guards');
});
