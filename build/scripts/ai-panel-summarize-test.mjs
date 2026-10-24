#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The on-device summarize request/response round trip.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

const h = installDOM({pathname: '/ai-panel'});

const {installAiPanel, createNativeAiBackend} =
    await import(path.join(ATRIUM_DIR, 'ai-panel.js'));

test('aiPanelSummarizeRequested is registered in kSurfaceMessages', () => {
  const registered = fs.readFileSync(
      path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
      'utf8');
  assert.match(registered, /"aiPanelSummarizeRequested"/);
});

test('the native backend sends a request and renders the pushed summary',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {backend: createNativeAiBackend()});

       h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
       // The trailing list is the roaming pages, empty unless pinned.
       assert.deepEqual(h.sent.at(-1),
                        ['aiPanelSummarizeRequested', ['', 1, []]]);

       // No summary yet - native hasn't answered.
       assert.equal(root.querySelector('.aisum p'), null);

       window.atriumSetAiPanelSummary(1, 'a real on-device summary');
       assert.equal(root.querySelector('.aisum p').textContent,
                    'a real on-device summary');
       h.reset();
     });

test('an unavailable/empty-page explanation renders through the same path',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {backend: createNativeAiBackend()});

       h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
       window.atriumSetAiPanelSummaryError(
           1, 'On-device summarize isn\'t available on this Mac.');
       assert.equal(
           root.querySelector('.aisum p').textContent,
           'On-device summarize isn\'t available on this Mac.');
       h.reset();
     });

test('the summary renders through textContent, not innerHTML', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {backend: createNativeAiBackend()});

  h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
  window.atriumSetAiPanelSummary(1, '<img src=x onerror=alert(1)>');
  const p = root.querySelector('.aisum p');
  assert.equal(p.textContent, '<img src=x onerror=alert(1)>');
  assert.equal(p.querySelector('img'), null);
  h.reset();
});

test('the kill switch tears the panel down - no request can be sent while off',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {backend: createNativeAiBackend(), enabled: false});

       h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
       assert.equal(root.querySelector('.aipanel'), null,
                    'no panel DOM exists to fire the hotkey against');
       assert.equal(h.sent.find((s) => s[0] === 'aiPanelSummarizeRequested'),
                    undefined);
       h.reset();
     });

test('a reply is dropped when the panel re-attaches to another tab before it arrives',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       const panel = installAiPanel(root, {backend: createNativeAiBackend()});

       window.atriumSetAiPanelTarget({tabId: '1', spaceIndex: 0, pageTitle: 'A'});
       h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
       root.querySelector('[data-ai-panel-pin]').click();

       window.atriumSetAiPanelTarget({tabId: '2', spaceIndex: 1, pageTitle: 'B'});
       window.atriumSetAiPanelSummary(1, 'summary meant for tab A');

       assert.equal(
           root.querySelector('.aisum p'), null,
           'a reply for the tab the panel left must not render under the new one');
       assert.deepEqual(
           panel.getMemory(1), [],
           'a dropped reply must not be filed under the space it now shows');
       h.reset();
     });

test('a stale reply from a superseded request does not render under the reopened tab',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {backend: createNativeAiBackend()});

       window.atriumSetAiPanelTarget({tabId: '1', spaceIndex: 0, pageTitle: 'A'});
       h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));

       // Unpinned, so re-targeting to a different tab closes the panel.
       window.atriumSetAiPanelTarget({tabId: '3', spaceIndex: 2, pageTitle: 'C'});
       h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));

       window.atriumSetAiPanelSummary(1, 'summary for A (late)');
       assert.equal(
           root.querySelector('.aisum p'), null,
           'the superseded request\'s reply must not render under the reopened tab');

       window.atriumSetAiPanelSummary(2, 'summary for C');
       assert.equal(root.querySelector('.aisum p').textContent, 'summary for C');
       h.reset();
     });

// The on-device and network backends race - the newer request can finish
// first. Matching by id (not arrival order) must still land the right reply.
test('a reply is matched by id even when the older request answers second',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {backend: createNativeAiBackend()});

       window.atriumSetAiPanelTarget({tabId: '1', spaceIndex: 0, pageTitle: 'A'});
       h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));

       window.atriumSetAiPanelTarget({tabId: '3', spaceIndex: 2, pageTitle: 'C'});
       h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));

       // C's request (id 2) answers before A's older one (id 1).
       window.atriumSetAiPanelSummary(2, 'summary for C');
       assert.equal(root.querySelector('.aisum p').textContent, 'summary for C');

       window.atriumSetAiPanelSummary(1, 'summary for A (late)');
       assert.equal(
           root.querySelector('.aisum p').textContent, 'summary for C',
           'a late reply for the superseded request must not overwrite C\'s summary');
       h.reset();
     });

test('a reply for an unknown or already-resolved id is ignored', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {backend: createNativeAiBackend()});

  h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
  assert.doesNotThrow(() => window.atriumSetAiPanelSummary(999, 'nobody asked'));
  assert.equal(root.querySelector('.aisum p'), null);

  window.atriumSetAiPanelSummary(1, 'the real one');
  assert.doesNotThrow(() => window.atriumSetAiPanelSummary(1, 'a duplicate reply'));
  assert.equal(root.querySelector('.aisum p').textContent, 'the real one',
               'a second reply for an id already resolved must not overwrite it');
  h.reset();
});

test('the backend caps pending requests so an unanswered one cannot grow it forever',
     () => {
       const backend = createNativeAiBackend();
       const renders = [];
       for (let i = 1; i <= 9; i++) {
         backend.summarize({pageTitle: `p${i}`}, (text) => renders.push([i, text]));
       }

       window.atriumSetAiPanelSummary(1, 'evicted');
       window.atriumSetAiPanelSummary(9, 'kept');

       assert.deepEqual(renders, [[9, 'kept']],
                        'the oldest pending request past the cap must be dropped');
     });

test('aiPanelChatRequested is registered and routed', () => {
  const registered = fs.readFileSync(
      path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'), 'utf8');
  assert.match(registered, /"aiPanelChatRequested"/);
  const routes = fs.readFileSync(
      path.join(SRC,
                'chrome/browser/ui/atrium/atrium_browser_window_surface_routes.cc'),
      'utf8');
  assert.match(routes, /\{"aiPanelChatRequested",/);
});

function ask(root, question) {
  root.querySelector('.aichatinput').value = question;
  root.querySelector('[data-ai-panel-send]').click();
}

function aiBubbles(root) {
  return [...root.querySelectorAll('.aichatlog .aibubble-ai')].map(
      (b) => b.textContent);
}

test('a chat question goes native with its id and the pushed answer renders',
     async () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {backend: createNativeAiBackend()});
       window.atriumSetAiPanelTarget({tabId: '4', spaceIndex: 0, pageTitle: 'A'});

       ask(root, 'what is this about');
       assert.deepEqual(h.sent.at(-1),
                        ['aiPanelChatRequested',
                         ['4', 1, 'what is this about', []]]);

       // The stub answered after 450 ms. Nothing may arrive on its own.
       await new Promise((r) => setTimeout(r, 500));
       assert.deepEqual(aiBubbles(root), []);

       window.atriumSetAiPanelSummary(1, '<b>an on-device answer</b>');
       assert.deepEqual(aiBubbles(root), ['<b>an on-device answer</b>']);
       assert.equal(root.querySelector('.aichatlog b'), null);
       h.reset();
     });

test('a failed or unavailable chat call shows the error line', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {backend: createNativeAiBackend()});

  ask(root, 'hello');
  window.atriumSetAiPanelSummaryError(
      1, 'On-device chat isn\'t available on this Mac.');
  assert.deepEqual(aiBubbles(root),
                   ['On-device chat isn\'t available on this Mac.']);
  h.reset();
});

test('a second question before the first answer shows only the second answer',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {backend: createNativeAiBackend()});

       ask(root, 'first');
       ask(root, 'second');
       window.atriumSetAiPanelSummary(2, 'answer to second');
       window.atriumSetAiPanelSummary(1, 'answer to first (late)');
       assert.deepEqual(aiBubbles(root), ['answer to second']);
       h.reset();
     });

test('a chat answer owed to the tab being left is dropped', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {backend: createNativeAiBackend()});
  window.atriumSetAiPanelTarget({tabId: '1', spaceIndex: 0, pageTitle: 'A'});

  ask(root, 'about A');
  window.atriumSetAiPanelTarget({tabId: '2', spaceIndex: 0, pageTitle: 'B'});
  window.atriumSetAiPanelSummary(1, 'answer for A');
  assert.deepEqual(aiBubbles(root), []);
  h.reset();
});

test('a reply that arrives after the kill switch is off is ignored without throwing',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {backend: createNativeAiBackend()});

       h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
       window.atriumSetAiPanelEnabled(false);

       assert.doesNotThrow(
           () => window.atriumSetAiPanelSummary(1, 'late reply after teardown'));
       assert.equal(root.querySelector('.aipanel'), null);
       h.reset();
     });

// --- loader shown while the AI is answering ----------------------

function chatLoaders(root) {
  return root.querySelectorAll('.aistatus .ailoader');
}

function summaryLoaders(root) {
  return root.querySelectorAll('.aisum .ailoader');
}

test('sending a question shows the loader at once', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {backend: createNativeAiBackend()});

  ask(root, 'what is this about');

  assert.equal(chatLoaders(root).length, 1);
  assert.deepEqual(aiBubbles(root), [],
                    'a loader must not read back as an answer bubble');
  assert.equal(chatLoaders(root)[0].getAttribute('aria-live'), 'polite');
  assert.equal(chatLoaders(root)[0].textContent, 'Thinking');
  h.reset();
});

test('the answer replaces the loader bubble', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {backend: createNativeAiBackend()});

  ask(root, 'what is this about');
  window.atriumSetAiPanelSummary(1, 'an on-device answer');

  assert.equal(chatLoaders(root).length, 0,
               'the loader must be gone once the answer lands');
  assert.deepEqual(aiBubbles(root), ['an on-device answer']);
  h.reset();
});

test('a failed chat call also replaces the loader with the error line',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {backend: createNativeAiBackend()});

       ask(root, 'hello');
       window.atriumSetAiPanelSummaryError(1, 'nope');

       assert.equal(chatLoaders(root).length, 0);
       assert.deepEqual(aiBubbles(root), ['nope']);
       h.reset();
     });

test('opening the panel shows a summary loader until the summary lands',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {backend: createNativeAiBackend()});

       h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
       assert.ok(root.querySelector('.aisum .ailoader'),
                  'no loader shown while the summary is pending');
       assert.equal(root.querySelector('.aisum p'), null);

       window.atriumSetAiPanelSummary(1, 'a real on-device summary');
       assert.equal(root.querySelector('.aisum .ailoader'), null,
                    'the loader must be gone once the summary lands');
       assert.equal(root.querySelector('.aisum p').textContent,
                    'a real on-device summary');
       h.reset();
     });

test('a summary error also replaces the loader', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {backend: createNativeAiBackend()});

  h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
  window.atriumSetAiPanelSummaryError(1, 'not available');

  assert.equal(root.querySelector('.aisum .ailoader'), null);
  assert.equal(root.querySelector('.aisum p').textContent, 'not available');
  h.reset();
});

test('a second question before the first answer removes the first ' +
     'loader, leaving only the second',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {backend: createNativeAiBackend()});

       ask(root, 'first');
       assert.equal(chatLoaders(root).length, 1);
       ask(root, 'second');
       assert.equal(
           chatLoaders(root).length, 1,
           'the first loader must be gone, not stacked under a second');

       window.atriumSetAiPanelSummary(2, 'answer to second');
       window.atriumSetAiPanelSummary(1, 'answer to first (late)');
       assert.equal(chatLoaders(root).length, 0);
       assert.deepEqual(aiBubbles(root), ['answer to second']);
       h.reset();
     });

test('switching tabs mid-flight removes the loader, and it does not ' +
     'come back when the tab reopens',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {backend: createNativeAiBackend()});
       window.atriumSetAiPanelTarget({tabId: '1', spaceIndex: 0, pageTitle: 'A'});

       ask(root, 'about A');
       assert.equal(chatLoaders(root).length, 1);

       window.atriumSetAiPanelTarget({tabId: '2', spaceIndex: 0, pageTitle: 'B'});
       assert.equal(chatLoaders(root).length, 0,
                    'the loader must not survive the tab switch that cancelled it');

       window.atriumSetAiPanelTarget({tabId: '1', spaceIndex: 0, pageTitle: 'A'});
       assert.equal(
           chatLoaders(root).length, 0,
           'a stashed loader must not come back when the tab reopens');

       // The cancelled reply landing late must still do nothing.
       window.atriumSetAiPanelSummary(1, 'answer for A');
       assert.deepEqual(aiBubbles(root), []);
       h.reset();
     });

// A summary loader left behind is worse than a chat one: stashAttached()
// snapshots .aisum into the per-tab cache, so the tab keeps replaying a
// Thinking line for a request that was cancelled and will never answer.
test('switching tabs mid-summary removes the summary loader, and the ' +
     'tab does not come back still thinking',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {backend: createNativeAiBackend()});
       window.atriumSetAiPanelTarget({tabId: '1', spaceIndex: 0, pageTitle: 'A'});

       h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
       assert.equal(summaryLoaders(root).length, 1,
                    'the open did not start a summary, so this proves nothing');

       window.atriumSetAiPanelTarget({tabId: '2', spaceIndex: 0, pageTitle: 'B'});
       assert.equal(summaryLoaders(root).length, 0,
                    'the loader must not survive the tab switch that cancelled it');

       window.atriumSetAiPanelTarget({tabId: '1', spaceIndex: 0, pageTitle: 'A'});
       assert.equal(summaryLoaders(root).length, 0,
                    'the stashed loader came back and will think forever');

       window.atriumSetAiPanelSummary(1, 'summary for A');
       assert.equal(root.querySelector('.aisum p'), null,
                    'a cancelled summary must not land after the switch back');
       h.reset();
     });

test('a question sent while the summary is still loading leaves the ' +
     'summary loader alone',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {backend: createNativeAiBackend()});
       window.atriumSetAiPanelTarget({tabId: '9', spaceIndex: 0, pageTitle: 'A'});

       h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
       assert.equal(summaryLoaders(root).length, 1);

       ask(root, 'a question while the summary is still out');
       assert.equal(summaryLoaders(root).length, 1,
                    'the chat cancel reached the summary loader too');
       assert.equal(chatLoaders(root).length, 1);
       h.reset();
     });

test('turning AI off removes a pending chat loader', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {backend: createNativeAiBackend()});

  ask(root, 'hello');
  assert.equal(chatLoaders(root).length, 1);

  assert.doesNotThrow(() => window.atriumSetAiPanelEnabled(false));
  assert.equal(root.querySelector('.aipanel'), null);

  assert.doesNotThrow(() => window.atriumSetAiPanelSummary(1, 'too late'));
  h.reset();
});

// The helper's exit code says only that it failed. The reason travels as the
// first line of its stderr, so the two files have to spell the token the same
// way or every refusal reads as a generic breakage again.
test('the helper and the browser agree on the failure tokens', () => {
  const swift = fs.readFileSync(
      path.join(SRC, 'chrome/browser/ui/atrium/atrium_ai_foundation_models.swift'),
      'utf8');
  const backend = fs.readFileSync(
      path.join(SRC, 'chrome/browser/ui/atrium/atrium_ai_backend_mac.cc'), 'utf8');
  for (const token of ['unsupported-language', 'unavailable']) {
    assert.ok(swift.includes(`"${token}\\n"`),
              `the helper never writes ${token}`);
    assert.ok(backend.includes(`token == "${token}"`),
              `the browser never matches ${token}`);
  }
  // A page in a language the model refuses is a fact about the page, and
  // saying "call failed" for it sent the user looking for a broken install.
  assert.ok(
      backend.includes("does not handle this page's language"),
      'the unsupported-language branch must say what actually happened');
});

// A drift between the two copies of the chat instructions fails no
// build and no other test. It just makes the on-device and network backends
// answer to different rules, which nobody sees until they compare two replies.
function words(text) {
  return text.replace(/\s+/g, ' ').trim();
}

test('the on-device and network backends carry the same chat instructions', () => {
  const cc = fs.readFileSync(
      path.join(SRC, 'chrome/browser/ui/atrium/atrium_ai_chat_prompt.cc'),
      'utf8');
  const swift = fs.readFileSync(
      path.join(SRC, 'chrome/browser/ui/atrium/atrium_ai_foundation_models.swift'),
      'utf8');

  // Adjacent string literals in C++, a backslash-continued block in Swift.
  const literal = cc.split('kChatInstructions[] =')[1].split(';')[0];
  const fromCc = words([...literal.matchAll(/"([^"]*)"/g)].map((m) => m[1]).join(''));
  const fromSwift =
      words(swift.split('instructions: """')[1].split('"""')[0].replace(/\\\n/g, ' '));

  assert.ok(fromCc.length > 80, 'the C++ instructions were not extracted');
  assert.equal(fromSwift, fromCc,
               'the two copies of the chat instructions have drifted apart');
});

// The chat bubbles formatted from the day markdown landed and the
// summary pane never did, so one panel rendered a reply two different ways.
test('the summary pane formats markdown the way a chat bubble does', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {backend: createNativeAiBackend()});

  h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
  window.atriumSetAiPanelSummary(
      1, '**Career**\n\n* Born in *Bossier City*\n* Founded the band');

  const sum = root.querySelector('.aisum');
  assert.equal(sum.querySelectorAll('li').length, 2, 'the bullets must be a list');
  assert.equal(sum.querySelector('strong').textContent, 'Career');
  assert.equal(sum.querySelector('em').textContent, 'Bossier City');
  assert.ok(!sum.textContent.includes('*'),
            'the markers must be consumed, not shown');
  h.reset();
});

// The half that keeps any markdown-ish renderer from passing.
test('the summary pane and a chat bubble share one renderer', () => {
  const src = fs.readFileSync(path.join(ATRIUM_DIR, 'ai-panel.js'), 'utf8');
  const calls = [...src.matchAll(/renderMarkdown\(/g)].length;
  assert.ok(calls >= 2,
            'renderSummary must call renderMarkdown, not build its own nodes');
  assert.equal([...src.matchAll(/from '\.\/markdown-render\.js'/g)].length, 1,
               'one import, so there is no second copy of the renderer');
});

test('a summary is still text-only - no markup becomes nodes', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {backend: createNativeAiBackend()});

  h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
  window.atriumSetAiPanelSummary(1, '<img src=x onerror=alert(1)> and [x](javascript:alert(1))');

  const sum = root.querySelector('.aisum');
  assert.equal(sum.querySelector('img'), null);
  assert.equal(sum.querySelector('a'), null, 'an executable scheme must stay text');
  assert.ok(sum.textContent.includes('<img src=x onerror=alert(1)>'));
  h.reset();
});

// --- a streamed answer reaches the panel as it arrives ---

test('a multi-chunk stream repaints the summary before it completes', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {backend: createNativeAiBackend()});

  h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
  const seen = [];
  window.atriumSetAiPanelSummaryChunk(1, 'The page');
  seen.push(root.querySelector('.aisum').textContent);
  window.atriumSetAiPanelSummaryChunk(1, 'The page opens with');
  seen.push(root.querySelector('.aisum').textContent);
  window.atriumSetAiPanelSummary(1, 'The page opens with a filmography.');
  seen.push(root.querySelector('.aisum').textContent);

  // Three distinct readings, not one: a single final reading would also pass
  // a build that waits for the whole answer.
  assert.deepEqual(seen, [
    'The page',
    'The page opens with',
    'The page opens with a filmography.',
  ]);
  h.reset();
});

// The chunk push must not retire the request, or the final answer - and the
// error push that may replace it - finds nothing to render into.
test('a chunk leaves the request open for its own completion', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {backend: createNativeAiBackend()});

  h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
  window.atriumSetAiPanelSummaryChunk(1, 'half');
  window.atriumSetAiPanelSummary(1, 'half and half');
  assert.equal(root.querySelector('.aisum').textContent, 'half and half');

  // Retired now: a second push against a spent id renders nothing.
  window.atriumSetAiPanelSummary(1, 'a reply nobody asked for');
  assert.equal(root.querySelector('.aisum').textContent, 'half and half');
  h.reset();
});

test('a stream that fails midway keeps its text and adds the reason', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {backend: createNativeAiBackend()});

  h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
  window.atriumSetAiPanelSummaryChunk(1, 'The page opens with');
  window.atriumSetAiPanelSummaryError(1, 'Ollama stopped responding.');

  const sum = root.querySelector('.aisum');
  assert.ok(sum.textContent.includes('The page opens with'),
            'the text already on screen must survive the failure');
  assert.equal(sum.querySelector('.aierrnote').textContent,
               'Ollama stopped responding.');
  h.reset();
});

// --- the model's thinking, folded away ---

const think = (root) => root.querySelector('.aisum .aithink');

test('a reply with reasoning shows the answer and folds the rest',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {backend: createNativeAiBackend()});

       h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
       window.atriumSetAiPanelSummaryChunk(
           1, 'The page lists films.', 'The user wants a summary.');

       assert.equal(root.querySelector('.aisum .aisumtext').textContent,
                    'The page lists films.');
       const fold = think(root);
       assert.ok(fold, 'the thinking is there');
       assert.equal(fold.tagName, 'DETAILS');
       assert.equal(fold.open, false, 'it opens folded, showing the answer');
       assert.equal(fold.querySelector('.aithink-body').textContent,
                    'The user wants a summary.');
       h.reset();
     });

test('a reply with no reasoning shows no disclosure at all',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {backend: createNativeAiBackend()});

       h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
       window.atriumSetAiPanelSummary(1, 'a plain answer');
       assert.equal(think(root), null,
                    'an empty disclosure is worse than none');
       h.reset();
     });

// And the reason the fold is reused rather than rebuilt: the user can
// open it while the thinking is still arriving and watch the model work.
test('the thinking streams in and an opened fold stays open',
     () => {
       const root = document.createElement('div');
       document.body.appendChild(root);
       installAiPanel(root, {backend: createNativeAiBackend()});

       h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
       window.atriumSetAiPanelSummaryChunk(1, '', 'The user ');
       think(root).open = true;
       window.atriumSetAiPanelSummaryChunk(1, '', 'The user wants a list.');

       assert.equal(think(root).open, true,
                    'a rebuilt element would snap shut under the user');
       assert.equal(think(root).querySelector('.aithink-body').textContent,
                    'The user wants a list.');
       h.reset();
     });

// A new reply is a new element, so the disclosure it opens
// with is closed - the open state belongs to the reply, not to the panel.
test('the next reply opens folded again', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {backend: createNativeAiBackend()});

  h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
  window.atriumSetAiPanelSummaryChunk(1, 'first', 'first thinking');
  think(root).open = true;
  window.atriumSetAiPanelSummary(1, 'first');

  // A second summarize is a second request id and a fresh pane.
  h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
  h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
  window.atriumSetAiPanelSummaryChunk(2, 'second', 'second thinking');
  assert.equal(think(root).open, false);
  h.reset();
});

// And the pair: an error with nothing streamed before it is
// still the whole of what the pane shows.
test('an error with no stream behind it replaces the pane as before', () => {
  const root = document.createElement('div');
  document.body.appendChild(root);
  installAiPanel(root, {backend: createNativeAiBackend()});

  h.body.dispatchEvent(new Event('keydown', {bubbles: true, key: '`'}));
  window.atriumSetAiPanelSummaryError(1, 'AI is off.');

  const sum = root.querySelector('.aisum');
  assert.equal(sum.querySelector('.aierrnote'), null,
               'nothing was streamed, so there is nothing to append under');
  assert.equal(sum.textContent, 'AI is off.');
  h.reset();
});
