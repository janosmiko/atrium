#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//
//   node --test build/scripts/settings-import-test.mjs

import {test} from 'node:test';
import assert from 'node:assert/strict';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');

installDOM({pathname: '/settings'});

const {renderImportPane, summaryLine, resultLine} = await import(
    path.join(SRC, 'chrome/browser/resources/atrium/settings-import.js'));

const SUMMARY = {profiles: 2, spaces: 3, tabs: 6, favorites: 2, routes: 4, notes: 4};

function mount(state) {
  const body = document.createElement('div');
  const calls = [];
  renderImportPane(body, state, {
    request: (id) => calls.push(['request', id]),
    run: (id) => calls.push(['run', id]),
  });
  return {body, calls};
}

function fresh(overrides = {}) {
  return {previews: {}, pending: {}, running: null, result: null, ...overrides};
}

test('an unanswered source is asked for once and shows the waiting line', () => {
  const {body, calls} = mount(fresh());
  assert.deepEqual(calls, [['request', 'arc'], ['request', 'zen'], ['request', 'chrome'], ['request', 'firefox']]);
  assert.equal(body.querySelector('.import-empty').textContent,
               'Looking for other browsers on this Mac.');

  const again = mount(fresh({pending: {arc: true, zen: true, chrome: true, firefox: true}}));
  assert.deepEqual(again.calls, [], 'a pending request is not sent twice');
});

test('a source with no files shows no card, only the empty state', () => {
  const {body} = mount(fresh({previews: {
    arc: {source: 'arc', detected: false},
    zen: {source: 'zen', detected: false},
    chrome: {source: 'chrome', detected: false},
    firefox: {source: 'firefox', detected: false},
  }}));
  assert.equal(body.querySelectorAll('.import-card').length, 0);
  assert.equal(body.querySelector('.import-empty').textContent,
               'No other browser found on this Mac.');
});

test('a detected Zen source draws its own card next to Arc', () => {
  const zen = {profiles: 3, spaces: 2, tabs: 4, favorites: 1, routes: 0, notes: 6};
  const {body, calls} = mount(fresh({
    previews: {
      arc: {source: 'arc', detected: true, summary: SUMMARY},
      zen: {source: 'zen', detected: true, summary: zen},
      chrome: {source: 'chrome', detected: false},
      firefox: {source: 'firefox', detected: false},
    },
  }));
  const card = body.querySelector('.import-card[data-source="zen"]');
  assert.ok(card);
  assert.equal(card.querySelector('.import-title').textContent, 'Zen');
  assert.equal(card.querySelector('.import-counts').textContent,
               '2 spaces, 4 tabs, 1 favourite and 3 profiles');
  card.querySelector('.import-run').dispatchEvent(new Event('click', {bubbles: true}));
  assert.deepEqual(calls, [['run', 'zen']]);
});

test('a detected source shows a card with its counts and an Import button', () => {
  const {body, calls} = mount(fresh({
    previews: {
      arc: {source: 'arc', detected: true, summary: SUMMARY},
      zen: {source: 'zen', detected: false},
      chrome: {source: 'chrome', detected: false},
      firefox: {source: 'firefox', detected: false},
    },
  }));
  const card = body.querySelector('.import-card[data-source="arc"]');
  assert.ok(card);
  assert.equal(card.querySelector('.import-title').textContent, 'Arc');
  assert.equal(card.querySelector('.import-counts').textContent,
               '3 spaces, 6 tabs, 2 favourites, 4 routing rules and 2 profiles');
  const button = card.querySelector('.import-run');
  assert.equal(button.textContent, 'Import');
  button.dispatchEvent(new Event('click', {bubbles: true}));
  assert.deepEqual(calls, [['run', 'arc']]);
});

test('a detected Chrome source counts its search engines', () => {
  const chrome = {profiles: 2, spaces: 2, tabs: 8, favorites: 0, routes: 0, engines: 3, notes: 5};
  const {body, calls} = mount(fresh({
    previews: {
      arc: {source: 'arc', detected: false},
      zen: {source: 'zen', detected: false},
      chrome: {source: 'chrome', detected: true, summary: chrome},
      firefox: {source: 'firefox', detected: false},
    },
  }));
  const card = body.querySelector('.import-card[data-source="chrome"]');
  assert.ok(card);
  assert.equal(card.querySelector('.import-title').textContent, 'Chrome');
  assert.equal(card.querySelector('.import-counts').textContent,
               '2 spaces, 8 tabs, 3 search engines and 2 profiles');
  card.querySelector('.import-run').dispatchEvent(new Event('click', {bubbles: true}));
  assert.deepEqual(calls, [['run', 'chrome']]);
});

test('a detected Firefox source counts its tabs and search engines', () => {
  const firefox = {profiles: 2, spaces: 2, tabs: 11, favorites: 0, routes: 0, engines: 2, notes: 7};
  const {body, calls} = mount(fresh({
    previews: {
      arc: {source: 'arc', detected: false},
      zen: {source: 'zen', detected: false},
      chrome: {source: 'chrome', detected: false},
      firefox: {source: 'firefox', detected: true, summary: firefox},
    },
  }));
  const card = body.querySelector('.import-card[data-source="firefox"]');
  assert.ok(card);
  assert.equal(card.querySelector('.import-title').textContent, 'Firefox');
  assert.equal(card.querySelector('.import-counts').textContent,
               '2 spaces, 11 tabs, 2 search engines and 2 profiles');
  card.querySelector('.import-run').dispatchEvent(new Event('click', {bubbles: true}));
  assert.deepEqual(calls, [['run', 'firefox']]);
});

test('a running import disables the button', () => {
  const {body} = mount(fresh({
    previews: {arc: {source: 'arc', detected: true, summary: SUMMARY}},
    running: 'arc',
  }));
  const button = body.querySelector('.import-run');
  assert.equal(button.textContent, 'Importing');
  assert.equal(button.disabled, true);
});

test('a preview error replaces the counts and the button', () => {
  const {body} = mount(fresh({
    previews: {arc: {source: 'arc', detected: true, error: 'bad file'}},
  }));
  assert.equal(body.querySelector('.import-counts').textContent,
               'Cannot read Arc: bad file');
  assert.equal(body.querySelector('.import-run'), null);
});

test('the result line and the notes follow the card', () => {
  const report = {
    spaces: {added: 3, duplicate: 0, dropped: 0},
    tabs: {added: 6, duplicate: 0, dropped: 0},
    favorites: {added: 2, duplicate: 0, dropped: 0},
    routes: {added: 3, duplicate: 0, dropped: 1},
    profiles: {added: 1, duplicate: 1, dropped: 0},
    groups: {added: 1, duplicate: 0, dropped: 0},
    notes: ['first note', 'second note'],
  };
  const {body} = mount(fresh({
    previews: {arc: {source: 'arc', detected: true, summary: SUMMARY}},
    result: {source: 'arc', report},
  }));
  assert.equal(body.querySelector('.import-result').textContent,
               'Added 3 spaces, 6 tabs, 2 favourites, 3 routing rules and 1 ' +
               'profile. 1 already in Atrium. 1 skipped.');
  const notes = body.querySelectorAll('.import-notes li');
  assert.deepEqual(notes.map((n) => n.textContent), ['first note', 'second note']);
});

// Mirrors settings.js's setImportResult state update. That function lives in
// settings.js, which this file does not import, so the transition is copied
// here rather than the setter itself.
function applyImportResult(state, result) {
  return {
    ...state,
    result: {source: state.running, ...(result || {})},
    running: null,
  };
}

test('a result applied after its preview keeps the card, not just the line', () => {
  const withPreview = fresh({
    previews: {arc: {source: 'arc', detected: true, summary: SUMMARY}},
    running: 'arc',
  });
  const withResult = applyImportResult(withPreview,
      {report: {spaces: {added: 3, duplicate: 0, dropped: 0}}});
  const {body} = mount(withResult);
  assert.ok(body.querySelector('.import-card[data-source="arc"]'),
            'the preview is still there for the result to sit next to');
  assert.equal(body.querySelector('.import-result').textContent,
               'Added 3 spaces.');
  assert.equal(body.querySelector('.import-empty'), null);
});

test('summaryLine and resultLine cover the empty and error cases', () => {
  assert.equal(summaryLine({spaces: 0, tabs: 0}), 'Nothing to import');
  assert.equal(summaryLine({spaces: 1, tabs: 1}), '1 space and 1 tab');
  assert.equal(resultLine({error: 'no document'}), 'Import failed: no document');
  assert.equal(resultLine({report: {spaces: {added: 0, duplicate: 2, dropped: 0}}}),
               'Nothing new to import. 2 already in Atrium.');
  assert.equal(resultLine(null), '');
});

test('resultLine never counts routes that did not reach disk', () => {
  const report = {spaces: {added: 1, duplicate: 0, dropped: 0},
                  routes: {added: 2, duplicate: 0, dropped: 0}};
  assert.equal(resultLine({report}), 'Added 1 space and 2 routing rules.');
  assert.equal(resultLine({report, routingError: 'the file could not be written'}),
               'Added 1 space. Routing rules were not saved: the file could not be written.');
});

test('resultLine never counts engines that did not reach disk', () => {
  const report = {spaces: {added: 1, duplicate: 0, dropped: 0},
                  engines: {added: 2, duplicate: 0, dropped: 0}};
  assert.equal(resultLine({report}), 'Added 1 space and 2 search engines.');
  assert.equal(
      resultLine({report, enginesError: 'settings.json is in a form this build cannot read'}),
      'Added 1 space. Search engines were not saved: settings.json is in a form this build cannot read.');
});

test('resultLine says when the themes did not reach disk', () => {
  const report = {spaces: {added: 1, duplicate: 0, dropped: 0}};
  assert.equal(resultLine({report, themesError: 'the file could not be written'}),
               'Added 1 space. Themes were not saved: the file could not be written.');
});
