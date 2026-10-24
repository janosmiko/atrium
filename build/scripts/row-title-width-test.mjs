#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the close button held 17px of every row, so a title that would
// have fitted was cut with an ellipsis on rows nobody was hovering.
//
//   node --test build/scripts/row-title-width-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const CSS = fs.readFileSync(
    path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');

// The declarations of one rule, by its exact selector.
function rule(selector) {
  const escaped = selector.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const found = new RegExp(`(^|\\n)${escaped}\\s*{([^}]*)}`).exec(CSS);
  assert.ok(found, `no rule for ${selector}`);
  return found[2];
}

test('the close button takes no width from the title', () => {
  // flex: none plus a fixed width is what reserved the space. Out of flow, the
  // title is the only thing left claiming the row, so it runs to the edge and
  // truncates there rather than 17px early.
  const x = rule('.row .x');
  assert.match(x, /position:\s*absolute/,
               'the close button still sits in the row flow');
  assert.doesNotMatch(x, /flex:\s*none/,
               'flex: none on an absolute box is the leftover of the old rule');

  const title = rule('.row .tl');
  assert.match(title, /flex:\s*1/);
  assert.match(title, /text-overflow:\s*ellipsis/,
               'a title that genuinely does not fit still has to be cut');
});

test('the title keeps its box whether the button shows or not', () => {
  // The clearbtn idiom grows max-width, which would shrink the flex:1 title
  // beside it and reflow the text on every hover. The close button is the last
  // child of the row, so it is laid over the title's tail instead.
  const hover = /\.row\.tabrow:is\(:hover, \.hoverhold\) \.x\s*{([^}]*)}/.exec(CSS);
  assert.ok(hover, 'the hover rule for the close button is gone');
  assert.doesNotMatch(hover[1], /max-width|width|margin/,
                      'a hover that changes the button box moves the title');
});

test('hovering ends the title before the button', () => {
  const hover = /\.row\.tabrow:is\(:hover, \.hoverhold\) \.tl\s*{([^}]*)}/.exec(CSS);
  assert.ok(hover, 'nothing makes room for the button on hover');
  // Wide enough to clear the 17px button, or the glyph lands on the text.
  const pad = /padding-right:\s*(\d+)px/.exec(hover[1]);
  assert.ok(pad, 'the hovered title has no right padding');
  assert.ok(Number(pad[1]) >= 17,
            `the title ends ${pad[1]}px in, inside the button`);
  assert.match(hover[1], /transition-delay:\s*0s/,
               'the room has to open at once when the button appears');
});

test('the title keeps the room until the button has faded out', () => {
  // The button fades for .22s after a .06s delay. A padding that drops at
  // once lets the title run under the glyph for that whole time.
  const rest = rule('.row.tabrow .tl');
  const delay = /padding-right\s+0s\s+\S+\s+([\d.]+)s/.exec(rest);
  assert.ok(delay, 'the padding drops with no delay on unhover');
  assert.ok(Number(delay[1]) >= 0.28,
            `the padding drops after ${delay[1]}s, before the button is gone`);
});

test('the button keeps its own hover treatment', () => {
  const own = rule('.row .x:hover');
  assert.match(own, /opacity:\s*1/);
  assert.match(own, /transform:\s*rotate/,
               'the spin is the existing affordance and has to survive');
});

test('the fade is a hover affordance, not a permanent crop', () => {
  // A mask on the base rule would cut the last characters of every title in
  // the list, which is the defect this task is about, in a new form.
  assert.doesNotMatch(rule('.row .tl'), /mask-image/);
});
