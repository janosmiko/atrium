#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The injected page script's grammar and mode machine. The C++
// half that calls into it is covered by atrium_vim_mode_browsertest.cc.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');
// Joined grammar-first, the way BuildVimInstallScript joins them. The order
// matters: vim-page.js reads the global the grammar publishes.
const SOURCE = [
  fs.readFileSync(path.join(ATRIUM_DIR, 'vim-grammar.js'), 'utf8'),
  fs.readFileSync(path.join(ATRIUM_DIR, 'vim-page.js'), 'utf8'),
].join('\n');

// The script is injected as a string, so it is evaluated the same way here.
function loadVim() {
  installDOM();
  delete globalThis.__atriumVimGrammar;
  const scrolled = [];
  // The motions ask for smooth scrolling, which only the options form can
  // carry. Recorded in the same [x, y] shape so both forms read alike.
  globalThis.window.scrollBy = (x, y) => scrolled.push(
      x && typeof x === 'object' ? [x.left || 0, x.top || 0, x.behavior]
                                 : [x, y]);
  globalThis.window.innerHeight = 800;
  globalThis.window.scrollTo = (x, y) => scrolled.push(['to', y]);
  const travelled = [];
  globalThis.window.history = {
    back: () => travelled.push('back'),
    forward: () => travelled.push('forward'),
  };
  new Function(SOURCE)();
  return {vim: globalThis.window.__atriumVim, scrolled, travelled};
}

// The shim models where focus rests but dispatches no focus event of its own,
// so the listener the script relies on in the browser has to be driven here.
function focusEditable(tag, attrs = {}) {
  const el = globalThis.document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    el.setAttribute(k, v);
  }
  globalThis.document.body.appendChild(el);
  el.focus();
  el.dispatchEvent(new globalThis.Event('focusin', {bubbles: true}));
  return el;
}

test('j and k scroll down and up', () => {
  const {vim, scrolled} = loadVim();
  assert.equal(vim.key('j'), 'handled');
  assert.equal(vim.key('k'), 'handled');
  assert.deepEqual(scrolled.map(([, y]) => Math.sign(y)), [1, -1]);
});

test('d and u scroll by half a viewport', () => {
  const {vim, scrolled} = loadVim();
  vim.key('d');
  vim.key('u');
  assert.deepEqual(scrolled, [[0, 400, 'smooth'], [0, -400, 'smooth']]);
});

// 60px a press meant dozens of presses down an article, and each
// one snapped.
test('j and k glide, and cover more than the 60px they used to', () => {
  const {vim, scrolled} = loadVim();
  vim.key('j');
  vim.key('k');
  assert.deepEqual(scrolled.map(([, , how]) => how), ['smooth', 'smooth']);
  assert.ok(scrolled[0][1] > 60, `a press moved only ${scrolled[0][1]}px`);
  assert.equal(scrolled[1][1], -scrolled[0][1]);
});

// the browser used to hand every control chord to the page, so the
// four page motions never reached vim at all.
test('the control motions scroll by half a viewport and by a page', () => {
  const {vim, scrolled} = loadVim();
  vim.key('ctrl-d');
  vim.key('ctrl-u');
  assert.deepEqual(scrolled, [[0, 400, 'smooth'], [0, -400, 'smooth']]);

  const page = loadVim();
  page.vim.key('ctrl-f');
  page.vim.key('ctrl-b');
  assert.deepEqual(page.scrolled.map(([, , how]) => how),
                   ['smooth', 'smooth']);
  const [down, up] = page.scrolled.map(([, y]) => y);
  assert.ok(down > 400 && down < 800,
            `a page move of ${down}px keeps no overlap`);
  assert.equal(up, -down);
});

// there was no history motion in the grammar at all.
test('H goes back and L goes forward', () => {
  const {vim, travelled} = loadVim();
  assert.equal(vim.key('H'), 'handled');
  assert.equal(vim.key('L'), 'handled');
  assert.deepEqual(travelled, ['back', 'forward']);
});

test('a focused field keeps H and L for typing', () => {
  const {vim, travelled} = loadVim();
  focusEditable('input');
  assert.equal(vim.key('H'), 'veto');
  assert.deepEqual(travelled, []);
});

test('gg needs both keystrokes and G needs one', () => {
  const {vim, scrolled} = loadVim();
  assert.equal(vim.key('g'), 'handled');
  assert.deepEqual(scrolled, [], 'a lone g scrolls nothing');
  vim.key('g');
  assert.deepEqual(scrolled, [['to', 0]]);

  vim.key('G');
  assert.equal(scrolled.length, 2);
  assert.equal(scrolled[1][0], 'to');
});

test('a g followed by another verb drops the prefix', () => {
  const {vim, scrolled} = loadVim();
  vim.key('g');
  vim.key('j');
  assert.equal(scrolled.length, 1, 'j ran, gg did not');
  vim.key('g');
  vim.key('g');
  assert.equal(scrolled.length, 2, 'the prefix was not left armed');
});

// The browser owns the clipboard: writeText from an isolated world needs
// a focused document and a gesture, and fails silently without either.
test('yy asks the browser to copy and a single y does not', () => {
  const {vim} = loadVim();
  assert.equal(vim.key('y'), 'handled');
  assert.equal(vim.key('y'), 'copy-url');
});

test('a y followed by another verb drops the prefix', () => {
  const {vim} = loadVim();
  vim.key('y');
  assert.equal(vim.key('j'), 'handled');
  assert.equal(vim.key('y'), 'handled', 'the prefix was not left armed');
});

// g and y must not share one pending slot, or gy would scroll to the top.
test('a g does not arm y, and a y does not arm g', () => {
  const {vim, scrolled} = loadVim();
  vim.key('g');
  assert.equal(vim.key('y'), 'handled');
  assert.deepEqual(scrolled, []);
  vim.key('y');
  assert.equal(vim.key('g'), 'handled');
  assert.deepEqual(scrolled, []);
});

// gt and gT join gg behind the same prefix, so the risk is a table
// that serves the new pairs and quietly drops the old one.
test('V3: gt and gT ask the browser to cycle tabs', () => {
  const {vim} = loadVim();
  assert.equal(vim.key('g'), 'handled');
  assert.equal(vim.key('t'), 'next-tab');
  assert.equal(vim.key('g'), 'handled');
  assert.equal(vim.key('T'), 'previous-tab');
});

test('V3: gg still reaches the top now that gt exists', () => {
  const {vim, scrolled} = loadVim();
  vim.key('g');
  vim.key('g');
  assert.deepEqual(scrolled, [['to', 0]]);
});

// A prefix with no entry for the key that followed drops and lets that key
// run its own verb, so gj scrolls rather than doing nothing.
test('V3: gj drops the prefix and scrolls', () => {
  const {vim, scrolled} = loadVim();
  vim.key('g');
  assert.equal(vim.key('j'), 'handled');
  assert.deepEqual(scrolled.map(([, y]) => Math.sign(y)), [1]);
});

test('V3: bracket pairs ask the browser to switch spaces', () => {
  const {vim} = loadVim();
  vim.key(']');
  assert.equal(vim.key(']'), 'next-space');
  vim.key('[');
  assert.equal(vim.key('['), 'previous-space');
});

// x and o are single keys the browser owns, so the page must not swallow them.
test('V3: x and o are passed to the browser, not run in the page', () => {
  const {vim, scrolled} = loadVim();
  assert.equal(vim.key('x'), 'close-tab');
  assert.equal(vim.key('o'), 'open-command-bar');
  assert.equal(vim.key('t'), 'open-command-bar');
  assert.deepEqual(scrolled, []);
});

test('focus in an input, a textarea, or a contenteditable is INSERT',
     () => {
  const {vim} = loadVim();
  assert.equal(vim.mode(), 'NORMAL');

  for (const [tag, attrs] of [['input', {}], ['textarea', {}],
                              ['div', {contenteditable: 'true'}]]) {
    const el = focusEditable(tag, attrs);
    assert.equal(vim.mode(), 'INSERT', `${tag} focus is INSERT`);
    el.blur();
    assert.equal(vim.mode(), 'NORMAL', `${tag} blur returns to NORMAL`);
  }
});

test('a readonly input is not INSERT', () => {
  const {vim} = loadVim();
  focusEditable('input', {readonly: 'readonly'});
  assert.equal(vim.mode(), 'NORMAL');
});

// The veto. The browser can steal a key while its cached mode is stale, and
// the page is the only half that knows the truth.
test('key() refuses the verb and reports veto while in INSERT', () => {
  const {vim, scrolled} = loadVim();
  focusEditable('input');
  assert.equal(vim.key('j'), 'veto');
  assert.deepEqual(scrolled, []);
});

// A hint over something nobody can see is a label that does nothing, so the
// shim's default zero rect means a target must be given a size to count.
function addLink(parent, href) {
  const el = globalThis.document.createElement('a');
  el.setAttribute('href', href);
  el.setRect({left: 0, top: 0, width: 40, height: 10});
  parent.appendChild(el);
  return el;
}

test('the walk finds a link inside an open shadow root', () => {
  const {vim} = loadVim();
  const host = globalThis.document.createElement('div');
  globalThis.document.body.appendChild(host);
  addLink(host.attachShadow({mode: 'open'}), '/deep');
  assert.equal(vim.clickableTargets().length, 1);
});

// Closed roots are unreachable from any world, ours included. The verb has to
// come back empty rather than throw.
test('a closed shadow root is not reachable and does not throw', () => {
  const {vim} = loadVim();
  const host = globalThis.document.createElement('div');
  globalThis.document.body.appendChild(host);
  addLink(host.attachShadow({mode: 'closed'}), '/deep');
  assert.deepEqual(vim.clickableTargets(), []);
});

// A div carrying role=button is clickable to a user and to this walk. The
// browsertest covers the tabindex clause, which this parser cannot express.
test('the walk finds a role-marked element, not only real links', () => {
  const {vim} = loadVim();
  const el = globalThis.document.createElement('div');
  el.setAttribute('role', 'button');
  el.setRect({left: 0, top: 0, width: 40, height: 10});
  globalThis.document.body.appendChild(el);
  assert.deepEqual(vim.clickableTargets(), [el]);
});

test('the walk skips a zero-size element', () => {
  const {vim} = loadVim();
  const shown = addLink(globalThis.document.body, '/shown');
  const hidden = globalThis.document.createElement('a');
  hidden.setAttribute('href', '/hidden');
  globalThis.document.body.appendChild(hidden);
  assert.deepEqual(vim.clickableTargets(), [shown]);
});

// Two frames must not both paint 'a', so the browser hands each one a slice
// of one global sequence: its offset, and the total across every frame.
test('labels are unique and single-character while the total fits', () => {
  const {vim} = loadVim();
  assert.deepEqual(vim.hintLabels(3, 0, 3), ['a', 's', 'd']);
  assert.equal(new Set(vim.hintLabels(9, 0, 9)).size, 9);
});

// 'a' cannot be both a label and the first half of 'aa', or typing it is
// ambiguous. Past the single-character capacity everything goes to pairs.
test('a total past the alphabet uses pairs for every label', () => {
  const {vim} = loadVim();
  const labels = vim.hintLabels(12, 0, 12);
  assert.ok(labels.every((l) => l.length === 2), labels.join(','));
  assert.equal(new Set(labels).size, 12);
});

test('an offset shifts the sequence without repeating', () => {
  const {vim} = loadVim();
  const first = vim.hintLabels(5, 0, 10);
  const second = vim.hintLabels(5, 5, 10);
  assert.deepEqual(first.filter((l) => second.includes(l)), []);
});

test('f paints a label per target and captures keys', () => {
  const {vim} = loadVim();
  addLink(globalThis.document.body, '/a');
  addLink(globalThis.document.body, '/b');
  assert.equal(vim.key('f'), 'capture');
  assert.equal(
      globalThis.document.body.querySelectorAll('.atrium-vim-hint').length, 2);
});

test('a full match clicks once and releases', () => {
  const {vim} = loadVim();
  const first = addLink(globalThis.document.body, '/a');
  addLink(globalThis.document.body, '/b');
  const clicks = [];
  first.addEventListener('click', () => clicks.push('/a'));
  vim.key('f');
  assert.equal(vim.key('a'), 'release');
  assert.deepEqual(clicks, ['/a']);
  assert.equal(
      globalThis.document.body.querySelectorAll('.atrium-vim-hint').length, 0);
});

// A label nobody typed must not fire, and hint mode must end rather than sit
// there eating every key.
test('a key matching no label releases without clicking', () => {
  const {vim} = loadVim();
  const only = addLink(globalThis.document.body, '/a');
  const clicks = [];
  only.addEventListener('click', () => clicks.push('/a'));
  vim.key('f');
  assert.equal(vim.key('z'), 'release');
  assert.deepEqual(clicks, []);
});

test('hide clears hints as well as the indicator', () => {
  const {vim} = loadVim();
  addLink(globalThis.document.body, '/a');
  vim.key('f');
  vim.hide();
  assert.equal(
      globalThis.document.body.querySelectorAll('.atrium-vim-hint').length, 0);
});

test('the indicator shows the current mode and follows focus', () => {
  const {vim} = loadVim();
  const indicator = () =>
      globalThis.document.body.querySelector('.atrium-vim-indicator');
  // The browser only calls in while the toggle is on, so the first drain is
  // what raises the indicator.
  vim.drainMode();
  assert.equal(indicator().textContent, 'NORMAL');
  focusEditable('input');
  assert.equal(indicator().textContent, 'INSERT');
});

// Off is the default, so an indicator the browser never asked for would
// land on every page of every user who leaves vim alone.
test('installing alone paints no indicator', () => {
  loadVim();
  assert.equal(
      globalThis.document.body.querySelectorAll('.atrium-vim-indicator').length,
      0);
});

test('hide removes the indicator and focus does not bring it back', () => {
  const {vim} = loadVim();
  vim.drainMode();
  const shown = () =>
      globalThis.document.body.querySelectorAll('.atrium-vim-indicator').length;
  assert.equal(shown(), 1);
  vim.hide();
  assert.equal(shown(), 0);
  focusEditable('input');
  assert.equal(shown(), 0);
});

test('drainMode returns the current mode', () => {
  const {vim} = loadVim();
  assert.equal(vim.drainMode(), 'NORMAL');
  focusEditable('input');
  assert.equal(vim.drainMode(), 'INSERT');
});

// The script is injected into a document that may not have committed yet.
test('installing twice keeps one instance and one indicator', () => {
  const {vim} = loadVim();
  vim.drainMode();
  new Function(SOURCE)();
  assert.equal(globalThis.window.__atriumVim, vim);
  assert.equal(
      globalThis.document.body.querySelectorAll('.atrium-vim-indicator').length,
      1);
});

// Media verbs. The shim's video answers play and pause, so these
// assert the verb reached an element rather than that a key was accepted.
function addVideo({width = 640, height = 360, duration = 300} = {}) {
  const el = globalThis.document.createElement('video');
  el.setRect({left: 0, top: 0, width, height});
  el.duration = duration;
  globalThis.document.body.appendChild(el);
  return el;
}

test('p toggles the primary video', () => {
  const {vim} = loadVim();
  const video = addVideo();
  assert.equal(video.paused, true);
  vim.key('p');
  assert.equal(video.paused, false);
  vim.key('p');
  assert.equal(video.paused, true);
});

test('comma and period seek back and forward', () => {
  const {vim} = loadVim();
  const video = addVideo();
  video.currentTime = 30;
  vim.key('.');
  assert.equal(video.currentTime, 35);
  vim.key(',');
  assert.equal(video.currentTime, 30);
});

// A seek past either end is a clamp, not a negative time or a seek beyond
// the duration, either of which a real element refuses.
test('a seek stops at both ends of the track', () => {
  const {vim} = loadVim();
  const video = addVideo();
  video.currentTime = 2;
  vim.key(',');
  assert.equal(video.currentTime, 0);
  video.currentTime = video.duration - 1;
  vim.key('.');
  assert.equal(video.currentTime, video.duration);
});

// The reason "primary" needs defining: an ad and a hero loop are both videos
// and neither is the one the reader means.
test('the largest visible video is the primary one', () => {
  const {vim} = loadVim();
  const small = addVideo({width: 100, height: 60});
  const big = addVideo({width: 800, height: 450});
  vim.key('p');
  assert.equal(big.paused, false);
  assert.equal(small.paused, true);
});

test('a video with no box is never the primary one', () => {
  const {vim} = loadVim();
  const hidden = addVideo({width: 0, height: 0});
  const shown = addVideo({width: 320, height: 180});
  vim.key('p');
  assert.equal(shown.paused, false);
  assert.equal(hidden.paused, true);
});

// Focused OR primary, and a reader who tabbed to a player means
// that one even when a bigger video is on the page.
test('a focused video beats the largest one', () => {
  const {vim} = loadVim();
  const big = addVideo({width: 800, height: 450});
  const small = addVideo({width: 100, height: 60});
  small.focus();
  vim.key('p');
  assert.equal(small.paused, false);
  assert.equal(big.paused, true);
});

// A page with no video must not swallow the key, or p would do nothing
// visible and feel broken on every ordinary page.
test('a media verb on a page with no video is refused', () => {
  const {vim} = loadVim();
  assert.equal(vim.key('p'), 'handled');
});

// duration is NaN until loadedmetadata, which is every video that has not
// started. A falsy-zero fallback clamped every forward seek back to the start.
test('a seek works before the metadata has loaded', () => {
  const {vim} = loadVim();
  const video = addVideo({duration: NaN});
  video.currentTime = 40;
  vim.key('.');
  assert.equal(video.currentTime, 45);
});

// A live stream reports Infinity and must seek like any other track.
test('a live stream seeks forward rather than clamping', () => {
  const {vim} = loadVim();
  const video = addVideo({duration: Infinity});
  video.currentTime = 100;
  vim.key('.');
  assert.equal(video.currentTime, 105);
});

// A site owns its own player and can throw from play() or a currentTime
// setter. A throw reaches the browser as a veto, so it would decide the page
// was in INSERT and type the raw character into the focused element.
test('a player that throws does not become a veto', () => {
  const {vim} = loadVim();
  const video = addVideo();
  video.play = () => {
    throw new Error('player not attached');
  };
  assert.equal(vim.key('p'), 'handled');

  const angry = addVideo({width: 900, height: 500});
  Object.defineProperty(angry, 'currentTime', {
    get: () => 0,
    set: () => {
      throw new Error('InvalidStateError');
    },
  });
  assert.equal(vim.key('.'), 'handled');
});
