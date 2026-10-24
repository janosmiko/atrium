#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the Foyer strip.
//
//   node --test build/scripts/foyer-test.mjs
//
// The strip renders strings the browser does not control - a page title and
// URL supplied by an external app - into a document running at browser
// privilege. The class of bug this suite exists to catch is therefore
// two-sided:
//
//   routing   a delegated listener that stops firing after the tree it was
//             reasoning about is replaced. The strip rebuilds its whole
//             subtree on every push, and a listener bound to a replaced node
//             fails silently.
//
//   sinks     untrusted text reaching the DOM as anything but text. There is
//             no innerHTML here and there must never be one (the WebUI CSP
//             sets require-trusted-types-for 'script' with an empty allowlist,
//             so a sink write is blocked outright), and the URL must never
//             become navigable - the strip is read-only by design.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const h = installDOM();
// The surface publishes its C++-facing setter on window. The shim installs a
// document but no window object.
globalThis.window = globalThis;

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const {isFreshTabURL, displayText, renderFoyer, installFoyer} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/foyer.js'));

// A clean tree and an empty chrome.send log for every test.
function freshRoot() {
  h.reset();
  const root = document.createElement('div');
  h.body.appendChild(root);
  return root;
}

const sentNames = () => h.sent.map(([name]) => name);
const lastSent = (name) => h.sent.filter(([n]) => n === name).at(-1);
const click = (el) => el.dispatchEvent(new Event('click', {bubbles: true}));

// --- isFreshTabURL ----------------------------------------------------------

test('every URL a brand-new tab reports counts as fresh', () => {
  // The set is pinned by URLFieldModelTests.swift; the two arms must agree or
  // the same tab reads "New Tab" in one window and a raw address in another.
  for (const url of ['', 'about:blank', 'about:blank/', 'chrome://newtab',
                     'chrome://newtab/']) {
    assert.equal(isFreshTabURL(url), true, url);
  }
});

test('a page the user navigated to is not fresh', () => {
  for (const url of ['https://example.com/', 'chrome://extensions/',
                     'about:blank?x', 'file:///tmp/x.html', ' ']) {
    assert.equal(isFreshTabURL(url), false, url);
  }
});

test('a missing URL is not fresh, so callers must default it themselves', () => {
  // renderFoyer relies on this: it substitutes '' before asking.
  assert.equal(isFreshTabURL(undefined), false);
  assert.equal(isFreshTabURL(null), false);
});

// --- displayText ------------------------------------------------------------

test('a fresh tab reads as New Tab rather than its address', () => {
  for (const url of ['', 'about:blank', 'about:blank/', 'chrome://newtab',
                     'chrome://newtab/']) {
    assert.equal(displayText(url), 'New Tab', url);
  }
});

test('the strip reads like an address bar, not a raw URL', () => {
  const cases = [
    ['https://example.com/', 'example.com', 'https and the bare slash both go'],
    ['https://example.com', 'example.com', 'no slash to drop'],
    ['https://example.com/path/', 'example.com/path', 'a trailing slash anywhere goes'],
    ['https://a.com/b?c=1#d', 'a.com/b?c=1#d', 'query and fragment survive'],
    ['https://user@a.com/', 'user@a.com', 'credentials are not hidden'],
  ];
  for (const [url, want, why] of cases) {
    assert.equal(displayText(url), want, why);
  }
});

test('an insecure scheme stays visible', () => {
  // Only https is dropped. Hiding http:// too would make an insecure origin
  // read exactly like a secure one.
  assert.equal(displayText('http://example.com/'), 'http://example.com');
});

test('a non-http scheme is shown verbatim', () => {
  // Nothing is stripped and nothing is refused: the strip is a read-only
  // label, so an odd scheme is shown as-is rather than silently rewritten.
  assert.equal(displayText('javascript:alert(1)'), 'javascript:alert(1)');
  assert.equal(displayText('data:text/html,<b>x</b>'), 'data:text/html,<b>x</b>');
});

test('a URL with no host degrades to an empty label', () => {
  assert.equal(displayText('https://'), '');
  assert.equal(displayText('file:///x/'), 'file:///x');
});

test('a very long URL is returned whole', () => {
  // Truncation is the CSS's job (.furltext ellipsis). Cutting it here would
  // hide the tail from a strip that is already wider than the text.
  const long = 'https://example.com/' + 'a'.repeat(2000);
  assert.equal(displayText(long), 'example.com/' + 'a'.repeat(2000));
});

// --- renderFoyer ------------------------------------------------------------

test('the strip carries a title, a URL and the promote affordance', () => {
  const root = freshRoot();
  renderFoyer(root, {title: 'Docs', url: 'https://example.com/'});
  assert.equal(root.querySelector('.ftl').textContent, 'Docs');
  assert.equal(root.querySelector('.furltext').textContent, 'example.com');
  assert.equal(root.querySelectorAll('[data-foyer-promote]').length, 1);
});

test('an untrusted title lands as text, never as markup', () => {
  // An external app supplies it. The lab's openFoyer built this strip from a
  // template literal, which is what the port had to remove.
  const root = freshRoot();
  renderFoyer(root, {title: '<img src=x onerror=alert(1)>'});
  const title = root.querySelector('.ftl');
  assert.equal(title.textContent, '<img src=x onerror=alert(1)>');
  assert.equal(title.children.length, 0);
  assert.equal(root.querySelector('img'), null);
});

test('an untrusted URL lands as text and is never made navigable', () => {
  // The strip is read-only by design: no anchor, no href, so a javascript:
  // URL is a label and cannot be activated.
  const root = freshRoot();
  renderFoyer(root, {url: 'javascript:alert(document.cookie)'});
  assert.equal(root.querySelector('.furltext').textContent,
               'javascript:alert(document.cookie)');
  assert.equal(root.querySelector('.furltext').children.length, 0);
  assert.equal(root.querySelector('a'), null);
});

test('a snapshot with no fields renders the empty state', () => {
  const root = freshRoot();
  renderFoyer(root, {});
  assert.equal(root.querySelector('.ftl').textContent, '');
  assert.equal(root.querySelector('.furltext').textContent, 'New Tab',
               'not the string "undefined"');
});

test('the tint paints the dot, and an absent tint leaves it to the CSS', () => {
  const root = freshRoot();
  renderFoyer(root, {tint: 'rgb(91, 141, 239)'});
  assert.equal(root.querySelector('.fdot').style.background, 'rgb(91, 141, 239)');
  renderFoyer(root, {});
  assert.ok(!root.querySelector('.fdot').style.background,
            'no inline value, so the token default wins');
});

test('the lock glyph is built in the SVG namespace', () => {
  // An <svg> created in the HTML namespace parses but paints nothing.
  const root = freshRoot();
  renderFoyer(root, {url: 'https://example.com/'});
  assert.equal(root.querySelector('.flock').namespaceURI,
               'http://www.w3.org/2000/svg');
});

// A padlock beside a plaintext URL is a spoofed security indicator, and the URL
// here is supplied by an external app. The Swift arm carries the same rule -
// LittleChromeModel.isSecure - and the two must not drift while both exist.
test('only https shows a padlock', () => {
  const root = freshRoot();
  renderFoyer(root, {url: 'https://example.com/'});
  assert.ok(root.querySelector('.flock'), 'https must show the lock');
});

test('plaintext and local schemes show no padlock', () => {
  for (const url of ['http://example.com', 'file:///etc/passwd', 'about:blank',
                     'chrome://settings', 'javascript:alert(1)', '']) {
    const root = freshRoot();
    renderFoyer(root, {url});
    assert.equal(root.querySelector('.flock'), null,
                 `${url || '(empty)'} must not be shown as secure`);
  }
});

test('a lookalike scheme shows no padlock', () => {
  // 'httpsx://' starts with 'https' but is not https.
  const root = freshRoot();
  renderFoyer(root, {url: 'httpsx://example.com'});
  assert.equal(root.querySelector('.flock'), null);
});

test('the strip is built in reading order', () => {
  // .fhead is a flex row, so append order IS the on-screen order: dot, title,
  // URL pill, then the promote button hard against the end.
  const root = freshRoot();
  renderFoyer(root, {title: 'Docs', url: 'https://example.com/'});
  assert.deepEqual(
      root.querySelector('.fhead').children.map((el) => el.className),
      ['fdot', 'ftl', 'furl', 'fbtn']);
});

test('a re-render replaces the strip rather than stacking one', () => {
  const root = freshRoot();
  renderFoyer(root, {title: 'One'});
  renderFoyer(root, {title: 'Two'});
  assert.equal(root.querySelectorAll('.fhead').length, 1);
  assert.equal(root.querySelector('.ftl').textContent, 'Two');
});

// --- installFoyer -----------------------------------------------------------

test('installing the foyer announces readiness and installs the setter', () => {
  const root = freshRoot();
  installFoyer(root);
  assert.deepEqual(sentNames(), ['foyerReady']);
  assert.equal(typeof window.atriumSetFoyer, 'function');
});

test('the promote button asks the browser to open the space list', () => {
  // The list lives in the Swift model and has no producer here, so
  // the whole gesture is this one message.
  const root = freshRoot();
  installFoyer(root);
  window.atriumSetFoyer({title: 'Docs', url: 'https://example.com/'});
  click(root.querySelector('[data-foyer-promote]'));
  assert.deepEqual(lastSent('foyerMoveToSpace'), ['foyerMoveToSpace', []]);
});

test('promote routes from a node inside the button', () => {
  // The handler resolves the button by walking UP from the click's target.
  // Matching the target itself works only as long as the button stays a bare
  // text label - the day it gains a glyph, the gesture would stop routing
  // with nothing on screen to show it.
  const root = freshRoot();
  installFoyer(root);
  window.atriumSetFoyer({url: 'https://a.com/'});
  const glyph = document.createElement('span');
  root.querySelector('[data-foyer-promote]').appendChild(glyph);
  click(glyph);
  assert.deepEqual(lastSent('foyerMoveToSpace'), ['foyerMoveToSpace', []]);
});

test('a click elsewhere in the strip sends nothing', () => {
  const root = freshRoot();
  installFoyer(root);
  window.atriumSetFoyer({url: 'https://example.com/'});
  click(root.querySelector('.furl'));
  click(root.querySelector('.ftl'));
  assert.equal(lastSent('foyerMoveToSpace'), undefined);
});

test('promote still routes after the strip has been replaced', () => {
  // Every push rebuilds .fhead, so the listener has to live on the root that
  // survives.
  const root = freshRoot();
  installFoyer(root);
  window.atriumSetFoyer({url: 'https://a.com/'});
  window.atriumSetFoyer({url: 'https://b.com/'});
  click(root.querySelector('[data-foyer-promote]'));
  assert.deepEqual(lastSent('foyerMoveToSpace'), ['foyerMoveToSpace', []]);
});

test('a push with no snapshot renders the empty strip', () => {
  const root = freshRoot();
  installFoyer(root);
  window.atriumSetFoyer();
  assert.equal(root.querySelector('.furltext').textContent, 'New Tab');
});
