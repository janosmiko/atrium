#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// atrium.js, the one-origin route table.
//
//   make webui-test
//
// This module runs main() at import, so each route needs its own module
// instance - hence the cache-busting query on every import. That is also why it
// had no test: it is the only module in the tree with an import side effect.
//
// A route that silently fails to install leaves a blank window, which reads as
// "the surface is broken" rather than "the path is wrong", so the fallback
// branch matters as much as the routes.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const MODULE = path.join(SRC, 'chrome/browser/resources/atrium/atrium.js');

// Fresh globals plus a fresh module instance, since main() already ran for any
// previously imported copy.
let instance = 0;
async function route(pathname) {
  const handle = installDOM({pathname});
  const root = document.createElement('div');
  root.setAttribute('id', 'atrium-root');
  handle.body.appendChild(root);
  await import(`${MODULE}?route=${instance++}`);
  // /sidebar starts a live 60s repaint clock (skylight.js) -
  // clear it so importing the next route doesn't leak a timer past this test.
  clearInterval(root.querySelector('.bglayer')?._skyClockHandle);
  return {handle, root};
}

// A route does not paint on install: it wires listeners and hangs a push
// entrypoint on window for C++ to call. So "did the route install" is asked by
// pushing a snapshot through that entrypoint, which is the real inbound ABI.
test('/sidebar installs the sidebar push entrypoint', async () => {
  const {handle, root} = await route('/sidebar');
  assert.equal(typeof handle.window.atriumSetSidebar, 'function',
               'C++ pushes snapshots through this name; without it the ' +
               'surface can never render');
  handle.window.atriumSetSidebar({
    spaces: [{name: 'One', tabs: [{id: '1', title: 'T', url: 'https://a.test'}]}],
    activeSpace: 0,
    tabs: [{id: '1', title: 'T', url: 'https://a.test'}],
  });
  assert.ok(root.children.length > 0, 'a pushed snapshot rendered nothing');
});

// toast-stack.js and edge-glow.js had no caller anywhere in
// atrium.js's import graph - only their CSS was linked. This drives the real
// entrypoint sidebar.js now installs, not snoozed-tab-return.js directly.
test('/sidebar installs the snoozed-tab-return entrypoint', async () => {
  const {handle, root} = await route('/sidebar');
  assert.equal(typeof handle.window.atriumTabSnoozeReturned, 'function',
               'the wake event calls through this name');
  handle.window.atriumTabSnoozeReturned(
      {id: '1', title: 'Quarterly Report', color: '#7FB3D5'},
      {notificationTimeoutStep: 'keep'});
  assert.ok(root.querySelector('.edgefx'), 'edge-glow.js never rendered');
  assert.ok(root.querySelector('.toast'), 'toast-stack.js never rendered');
});

test('/commandbar installs both palette entrypoints', async () => {
  const {handle, root} = await route('/commandbar');
  // Two, not one: Show() resets the field through the second before the user
  // types, so a route that installed only the push would come back holding the
  // last query.
  assert.equal(typeof handle.window.atriumSetCommandBar, 'function');
  assert.equal(typeof handle.window.atriumResetCommandBar, 'function');
  handle.window.atriumSetCommandBar({
    query: 'a',
    matches: [{index: 0, title: 'A', subtitle: 'https://a.test', deletable: false}],
  });
  assert.ok(root.querySelectorAll('.cbrow').length > 0);
});

test('/foyer installs the foyer push entrypoint', async () => {
  const {handle, root} = await route('/foyer');
  assert.equal(typeof handle.window.atriumSetFoyer, 'function');
  handle.window.atriumSetFoyer({url: 'https://example.test/page'});
  assert.ok(root.children.length > 0);
});

// The real trigger is native (/note in the command bar); this proves the
// route itself reaches ROUTES rather than sitting unimported like
// skylight.js, toast-stack.js and edge-glow.js once did.
test('/notes installs the note push entrypoint and renders the pushed body', async () => {
  const {handle, root} = await route('/notes');
  assert.equal(typeof handle.window.atriumSetNote, 'function',
               'C++ pushes the active space\'s saved note through this name');
  handle.window.atriumSetNote({title: 'Notes — Work', body: '# Heading\nsome text'});
  assert.ok(root.querySelector('.notes-pad'), 'the pad never rendered');
  assert.equal(root.querySelector('.notes-pad h1')?.textContent, 'Heading');
  assert.equal(handle.sent.filter(([name]) => name === 'notesReady').length, 1);
});

// drives the real atrium.js entry point and a real body
// keydown, the way skylight-wiring-test.mjs proves a
// keystroke reaches rendered DOM rather than a test-built div.
test('/ai-panel: backtick opens on the real #atrium-root, Esc closes it',
     async () => {
       const {handle, root} = await route('/ai-panel');
       const backtick = () => handle.body.dispatchEvent(
           new Event('keydown', {bubbles: true, key: '`'}));
       const esc = () => handle.body.dispatchEvent(
           new Event('keydown', {bubbles: true, key: 'Escape'}));

       assert.equal(root.classList.contains('aipopen'), false);
       backtick();
       assert.equal(root.classList.contains('aipopen'), true,
                    'backtick must open the real panel rendered by ai-panel.js');
       assert.ok(handle.sent.some(([name]) => name === 'aiPanelSummarizeRequested'),
                 'opening must ask the real on-device backend, not a stub');
       handle.window.atriumSetAiPanelSummary(1, 'a real on-device summary');
       assert.equal(root.querySelector('.aipanel .aisum p').textContent,
                    'a real on-device summary');
       esc();
       assert.equal(root.classList.contains('aipopen'), false,
                    'Esc must close the real panel');
     });

test('/ai-panel installs the edge-flip entrypoint', async () => {
  const {handle, root} = await route('/ai-panel');
  assert.equal(typeof handle.window.atriumSetAiPanelOnRight, 'function');
  handle.window.atriumSetAiPanelOnRight(true);
  assert.ok(root.classList.contains('aionright'));
  handle.window.atriumSetAiPanelOnRight(false);
  assert.ok(root.classList.contains('aionleft'));
});

// no native caller pushes tab/space identity yet, so the target push is simulated - everything else
// runs the real atrium.js route and a real body keydown.
test('/ai-panel: unpinned panel closes on a real atriumSetAiPanelTarget mismatch',
     async () => {
       const {handle, root} = await route('/ai-panel');
       const backtick = () => handle.body.dispatchEvent(
           new Event('keydown', {bubbles: true, key: '`'}));
       assert.equal(typeof handle.window.atriumSetAiPanelTarget, 'function');

       handle.window.atriumSetAiPanelTarget(
           {tabId: '1', spaceIndex: 0, pageTitle: 'First'});
       backtick();
       assert.equal(root.classList.contains('aipopen'), true);

       handle.window.atriumSetAiPanelTarget(
           {tabId: '2', spaceIndex: 0, pageTitle: 'Second'});
       assert.equal(root.classList.contains('aipopen'), false,
                    'unpinned must close on a real tab switch');
     });

test('/ai-panel: pinned panel re-attaches its whole body through the real atriumSetAiPanelTarget entrypoint',
     async () => {
       const {handle, root} = await route('/ai-panel');
       const backtick = () => handle.body.dispatchEvent(
           new Event('keydown', {bubbles: true, key: '`'}));

       handle.window.atriumSetAiPanelTarget(
           {tabId: '1', spaceIndex: 0, pageTitle: 'First'});
       backtick();
       assert.equal(root.querySelector('.aictx').textContent, 'About: First');

       root.querySelector('[data-ai-panel-pin]').click();
       handle.window.atriumSetAiPanelTarget(
           {tabId: '2', spaceIndex: 0, pageTitle: 'Second'});

       assert.equal(root.classList.contains('aipopen'), true,
                    'pinned panel must stay open across a real tab switch');
       assert.equal(root.querySelector('.aictx').textContent, 'About: Second',
                    'the context line must re-target, not just the close');
     });

// no native caller pushes the kill switch yet, but the off state itself must be proven against the
// real atrium.js and a real body keydown, not a test-built stand-in.
test('/ai-panel: kill switch off leaves no panel node and a real backtick no-ops',
     async () => {
       const {handle, root} = await route('/ai-panel');
       const backtick = () => handle.body.dispatchEvent(
           new Event('keydown', {bubbles: true, key: '`'}));
       assert.equal(typeof handle.window.atriumSetAiPanelEnabled, 'function');

       handle.window.atriumSetAiPanelEnabled(false);
       assert.equal(root.querySelector('.aipanel'), null,
                    'the real off switch must remove the real panel node');

       handle.sent.length = 0;
       backtick();
       assert.deepEqual(handle.sent, [],
                         'backtick must do nothing against the real route once AI is off');
       assert.equal(handle.body._listeners.get('keydown')?.length ?? 0, 0,
                    'no keydown listener may survive on the real document body');
     });

// Pushed before any snapshot arrives, which is the startup order: C++ can call
// swipeTo as soon as the surface loads. It must not throw on an empty rail.
test('/sidebar tolerates a swipe before the first snapshot', async () => {
  const {handle} = await route('/sidebar');
  assert.equal(typeof handle.window.atriumSwipeTo, 'function');
  assert.doesNotThrow(() => handle.window.atriumSwipeTo(0));
});

test('an unrouted path names itself as text rather than rendering blank',
     async () => {
       const {root} = await route('/nope');
       assert.match(root.textContent, /no route for \/nope/);
     });

test('the unrouted path reports itself to the browser', async () => {
  const {handle} = await route('/typo');
  assert.deepEqual(handle.sent.filter((m) => m[0] === 'atriumReady'),
                   [['atriumReady', ['/typo']]]);
});

// Documents the current split rather than endorsing it: atriumReady is the
// fallback's own signal, and the installed routes do not send it. If a route is
// ever made to wait on that message, this is the test that will say so.
test('an installed route does not send atriumReady', async () => {
  const {handle} = await route('/sidebar');
  assert.equal(handle.sent.some((m) => m[0] === 'atriumReady'), false);
});

// Every name reaching chrome.send must be in kSurfaceMessages in
// atrium_surface_host.h. An unregistered name is not a no-op: it terminates the
// BROWSER process. verify-webui-messages.py gates the whole tree, and this
// keeps the route module honest at the unit level too.
test('the route module sends only the message it declares', async () => {
  const {handle} = await route('/unknown-path');
  assert.deepEqual([...new Set(handle.sent.map((m) => m[0]))], ['atriumReady']);
});
