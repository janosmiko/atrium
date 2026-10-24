#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the command bar's verb grammar.
//
//   node --test build/scripts/command-verbs-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');
const ATRIUM_DIR = path.join(SRC, 'chrome/browser/resources/atrium');

const {parseCommand, resolveTargets, previewCommand, VERBS} =
    await import(path.join(ATRIUM_DIR, 'command-verbs.js'));

const TABS = [
  {title: 'Inbox', url: 'https://mail.example/'},
  {title: 'Team chat', url: 'https://team.slack.com/'},
  {title: 'Design chat', url: 'https://design.slack.com/'},
];

// --- the parser --------------------------------------------------------------

test('a line that does not start with a verb is not a command at all', () => {
  // Null rather than a "no verb" result: the palette's URL and search handling
  // must be untouched by this, so a command is a different mode not a failed
  // one.
  assert.equal(parseCommand('example.com'), null);
  assert.equal(parseCommand('how do i close a tab'), null);
  assert.equal(parseCommand(''), null);
  assert.equal(parseCommand(undefined), null);
});

test('a verb with no destination takes the whole rest as its target', () => {
  const {verb, target, dest} = parseCommand('close Team chat');
  assert.equal(verb, 'close');
  assert.equal(target, 'Team chat');
  assert.equal(dest, null);
});

test('a destination is the LAST word, so a multi-word target still parses', () => {
  // 'mv my long title work' has to mean move "my long title" to "work".
  const {verb, target, dest} = parseCommand('mv my long title work');
  assert.equal(verb, 'mv');
  assert.equal(target, 'my long title');
  assert.equal(dest, 'work');
});

test('a verb is recognised whatever its case', () => {
  assert.equal(parseCommand('CLOSE Inbox').verb, 'close');
});

// --- the resolver ------------------------------------------------------------

test('a target can be a 1-based index', () => {
  // 1-based because that is what the row list shows a user, not what the array
  // uses - an off-by-one here closes the wrong tab.
  assert.deepEqual(resolveTargets('1', TABS).map((t) => t.title), ['Inbox']);
  assert.deepEqual(resolveTargets('3', TABS).map((t) => t.title), ['Design chat']);
  assert.deepEqual(resolveTargets('0', TABS), []);
  assert.deepEqual(resolveTargets('9', TABS), []);
});

test('a target can be a title fragment, case-insensitively', () => {
  assert.deepEqual(resolveTargets('chat', TABS).map((t) => t.title),
                   ['Team chat', 'Design chat']);
  assert.deepEqual(resolveTargets('INBOX', TABS).map((t) => t.title), ['Inbox']);
});

test('a target can be a host glob', () => {
  assert.deepEqual(resolveTargets('*.slack.com', TABS).map((t) => t.title),
                   ['Team chat', 'Design chat']);
  assert.deepEqual(resolveTargets('mail.*', TABS).map((t) => t.title), ['Inbox']);
});

test('a glob matches the HOST, never the title or the path', () => {
  // Otherwise 'close *chat*' would read as a glob and quietly take a different
  // set than the fragment it looks like.
  assert.deepEqual(resolveTargets('*chat*', TABS), []);
});

test('a tab with no url cannot be matched by a glob, and does not throw', () => {
  const rows = [{title: 'Cold'}, ...TABS];
  assert.deepEqual(resolveTargets('*.slack.com', rows).map((t) => t.title),
                   ['Team chat', 'Design chat']);
});

// --- the preview -------------------------------------------------------------

test('a runnable command previews what it will act on', () => {
  const p = previewCommand('close chat', TABS);
  assert.equal(p.ok, true);
  assert.equal(p.verb, 'close');
  assert.deepEqual(p.targets.map((t) => t.title), ['Team chat', 'Design chat']);
});

test('a verb the palette cannot perform says so rather than previewing as ok', () => {
  // mv resolves its targets perfectly well. Its handler wants a space index, a
  // section and a row index, and the palette can resolve none of them - so an
  // ok preview would promise a Return that does nothing, which is the whole
  // defect this grammar shipped with.
  const p = previewCommand('mv chat work', TABS);
  assert.equal(p.ok, false);
  assert.deepEqual(p.targets.map((t) => t.title), ['Team chat', 'Design chat']);
  assert.equal(p.dest, 'work');
  assert.match(p.reason, /cannot run from the palette/);
});

test('a verb matching no target reports the miss and is not runnable', () => {
  const p = previewCommand('close nothing-here', TABS);
  assert.equal(p.ok, false);
  assert.match(p.reason, /nothing matches/);
  assert.deepEqual(p.targets, []);
});

test('a verb with no target at all says so rather than matching everything', () => {
  // The dangerous failure: 'close' alone must not resolve to every tab.
  const p = previewCommand('close', TABS);
  assert.equal(p.ok, false);
  assert.deepEqual(p.targets, []);
  // The REASON matters, not just the emptiness: a bare verb is a different
  // miss from a target that matched nothing, and both end with no targets - so
  // asserting emptiness alone cannot tell the guard from the resolver.
  assert.equal(p.reason, 'no target given');
});

test('a verb needing a destination says so when none is given', () => {
  const p = previewCommand('mv chat', TABS);
  assert.equal(p.ok, false);
  assert.match(p.reason, /needs a destination/);
});

test('a singular verb refuses a target that matches several tabs', () => {
  // split takes one tab. Running it against two would have to pick one, and
  // guessing which is worse than saying no.
  const p = previewCommand('split chat 2', TABS);
  assert.equal(p.ok, false);
  assert.match(p.reason, /takes one tab/);
});

test('move-to-space, close and split are all registered', () => {
  assert.deepEqual(Object.keys(VERBS).sort(), ['close', 'mv', 'split']);
});

test('command-verbs.js is registered in atrium_resources.grd', () => {
  const grd = fs.readFileSync(path.join(ATRIUM_DIR, 'atrium_resources.grd'), 'utf8');
  assert.match(grd, /file="command-verbs\.js"/);
});
