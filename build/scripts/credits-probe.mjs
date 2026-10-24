#!/usr/bin/env node
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// KEPT MANUAL PROBE. Not in `make webui-probe`: it needs the settings
// window already open and, run against a bare probe profile, it stops at target
// discovery. Run it by hand with settings up.
// the licences route, end to end, against the running binary.
//
//   ./build/scripts/vm-cdp.sh && node build/scripts/credits-probe.mjs
//
// The settings window must already be open (Cmd+, - the driver sends it).
//
// What this proves that a unit test cannot: the click travels JS -> chrome.send
// -> the browser process -> a real tab, and the page that lands is the
// GENERATED notice set rather than components/resources/sample_credits.html.
// The placeholder is 401 bytes and contains ZERO third-party entries, so an
// entry count is the one measure the two cannot both satisfy.
//
// The click is a real Input.dispatchMouseEvent at the control's own
// coordinates, not an element.click(): a button that is covered, zero-sized or
// scrolled out of view passes the synthetic call and fails the hand.

import {connect} from './sidebar-probe-lib.mjs';

const CDP = 'http://127.0.0.1:9334';
const wait = (ms) => new Promise((r) => setTimeout(r, ms));

let failures = 0;
const check = (label, actual, expected) => {
  const ok = JSON.stringify(actual) === JSON.stringify(expected);
  if (!ok) { failures++; }
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${label}` +
      (ok ? '' : `\n       expected ${JSON.stringify(expected)}\n       actual   ${JSON.stringify(actual)}`));
};

async function attach(target) {
  return await connect(target);
}

const list = async () => (await (await fetch(`${CDP}/json/list`)).json());

// 1. The settings window.
const targets = await list();
const settingsTarget = targets.find((t) => t.url.includes('/settings'));
if (!settingsTarget) {
  console.log('FAIL no settings target - is the settings window open? (Cmd+,)');
  console.log(targets.map((t) => `     ${t.url}`).join('\n'));
  process.exit(1);
}
const settings = await attach(settingsTarget);

// 2. The About pane has to be REACHABLE, so it is clicked rather than rendered
//    directly.
const clickAt = async (page, selector) => {
  const box = await page.ev(`(() => {
    const el = document.querySelector('${selector}');
    if (!el) { return null; }
    const b = el.getBoundingClientRect();
    if (!b.width || !b.height) { return null; }
    return {x: b.left + b.width / 2, y: b.top + b.height / 2};
  })()`);
  if (!box) { return false; }
  for (const type of ['mousePressed', 'mouseReleased']) {
    await page.send('Input.dispatchMouseEvent',
                    {type, x: box.x, y: box.y, button: 'left', clickCount: 1});
  }
  await wait(150);
  return true;
};

check('the About pane is in the rail',
      await settings.ev(
          '!!document.querySelector(\'.settings-rail-item[data-pane="about"]\')'),
      true);

check('the About pane opens on a real click',
      await clickAt(settings, '.settings-rail-item[data-pane="about"]'), true);
check('the body is showing About',
      await settings.ev(
          'document.querySelector(".settings-body").dataset.pane'),
      'about');

check('the licences control is there and has a hit area',
      await clickAt(settings, '.settings-row-credits'), true);

// 3. The tab the browser opened. The page is ~19 MB, so it needs a moment.
let credits = null;
for (let i = 0; i < 40 && !credits; i++) {
  await wait(500);
  credits = (await list()).find((t) => t.url.startsWith('chrome://credits'));
}
check('the click opened chrome://credits in a tab', !!credits, true);
if (!credits) {
  console.log(`\n${failures} failure(s)`);
  process.exit(1);
}

const page = await attach(credits);
// Give the document time to parse; the generated page is large.
for (let i = 0; i < 60; i++) {
  if (await page.ev('document.readyState === "complete"')) { break; }
  await wait(500);
}

const entries = await page.ev('document.querySelectorAll(".product").length');
const title = await page.ev('document.title');
console.log(`     title=${JSON.stringify(title)} entries=${entries}`);

// The distinguishing measure. sample_credits.html has no .product elements at
// all and says so in its own body; anything above zero is the generated set.
check('the page carries the generated third-party entries, not the placeholder',
      entries > 100, true);
check('the placeholder text is absent',
      await page.ev(
          'document.body.textContent.includes("This is sample credits page")'),
      false);
check('Chromium\'s own BSD-3 disclaimer is on the page',
      await page.ev(`document.body.textContent.includes(
          'THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS')`),
      true);

console.log(failures ? `\n${failures} failure(s)` : '\nall checks passed');
process.exit(failures ? 1 : 0);
