#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the space-dots bar is absolute over every pane, so a pane that
// reserves less room than the bar is tall hides its own last row.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const CSS = fs.readFileSync(
    path.join(SRC, 'chrome/browser/resources/atrium/sidebar.css'), 'utf8');

function rule(selector) {
  const escaped = selector.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  const found = new RegExp(`(^|\\n)${escaped}\\s*{([^}]*)}`).exec(CSS);
  assert.ok(found, `no rule for ${selector}`);
  return found[2];
}

// The shorthand on .ovrailpane and the longhand on .railpane .sidebar, read the
// same way so the two can be compared.
function bottomPadding(declarations) {
  const longhand = /padding-bottom:\s*(\d+)px/.exec(declarations);
  if (longhand) {
    return Number(longhand[1]);
  }
  const shorthand = /padding:\s*([^;]+);/.exec(declarations);
  assert.ok(shorthand, 'the rule reserves no bottom room at all');
  const parts = shorthand[1].trim().split(/\s+/);
  const bottom = parts.length >= 3 ? parts[2] : parts[parts.length - 1];
  const px = /^(\d+)px$/.exec(bottom);
  assert.ok(px, `bottom padding ${bottom} is not a plain px value`);
  return Number(px[1]);
}

test('the Overview pane reserves the bottom room a space pane does', () => {
  const spacePane = bottomPadding(rule('.railpane .sidebar'));
  const overviewPane = bottomPadding(rule('.ovrailpane'));
  assert.equal(overviewPane, spacePane,
               'the dots bar covers whatever the Overview pane does not reserve');
});

test('the reserved room clears the dots bar', () => {
  // The bar is absolute at bottom 0, so nothing below it scrolls into view.
  const bar = rule('.rail ~ .spacedots');
  assert.match(bar, /position:\s*absolute/);
  assert.match(bar, /bottom:\s*0/);
  assert.ok(bottomPadding(rule('.ovrailpane')) >= 40,
            'a pane reserving less than the bar is tall draws rows under it');
});
