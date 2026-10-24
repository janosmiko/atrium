#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the /settings route's shell - what it renders, what a click
// means, and what it sends.
//
//   node --test build/scripts/settings-test.mjs
//
// The panes themselves are elsewhere. What is pinned here is the shape the
// panes will be hung on, and the two things that bite a new route:
//
//   payload  an unregistered chrome.send name is FATAL in the BROWSER process,
//            so the names are read from kSurfaceMessages rather than copied.
//   identity a selection carries the pane's own id, never its position in the
//            rail. A rail reordered later must not silently change what a
//            stored or in-flight selection means.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM} from './dom-shim.mjs';
import {waitFor} from './wait-for.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');

const h = installDOM({pathname: '/settings'});

const {installSettings, renderSettings, PANES, paneOrDefault, isKnownPane,
       GENERAL_ROWS, PANE_ROWS, rowValue, conflictingCombos, actionLabel,
       choiceLabel, spaceDetailRows, spaceNameRow, isToggleable,
       NOT_WIRED, ABOUT_NOTE, comboKeycaps,
       checkOllamaConnection, activeSwipePreset, APP_ICON_VARIANTS,
       profileCountLabel, profileDetailRows, PROFILE_PAGES,
       searchSettings, searchResultSelector} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/settings.js'));

// This repo's build/icons, which inject-icns.sh stages into the bundle and the
// browser serves the picker's tiles from.
const ICONS_DIR = path.join(
    path.dirname(new URL(import.meta.url).pathname), '../icons');

// A WHOLE settings object, in the shape atrium_core_query("settings", ...)
// returns - fixtures/stores/settings/current.json is the same set of keys.
//
// Whole rather than the one key under test, because that is the contract the
// write half has: the core REFUSES a partial object rather than completing it
// from defaults, so a surface that has not been pushed one has nothing it can
// legally send back.
const CORE_SETTINGS = {
  aiEnabled: true,
  aiModel: 'onDevice',
  autoSummarize: true,
  translateLanguage: '',
  animationsEnabled: false,
  chromePageTint: false,
  appIconVariant: 'crimson-ink',
  archiveThreshold: 'twelveHours',
  autoPipEnabled: true,
  autoMiniPlayer: 'audible',
  controllerVisibility: 'background',
  swipeHaptics: 'crisp',
  confirmToQuitEnabled: false,
  aiPageTextCap: 100000,
  customEndpointMaxTokens: 0,
  customEndpointModel: 'gpt-4o-mini',
  customEndpointTimeoutSeconds: 0,
  customEndpointUrl: '',
  externalLinksOpenInLittle: true,
  favoriteKeepAliveEnabled: true,
  favoriteKeepAliveThreshold: 'fiveMinutes',
  borderWidth: 10,
  glassOpacity: 0.55,
  horizontalScrollOverride: 'auto',
  importPromptShown: true,
  memorySaverEnabled: false,
  memorySaverThreshold: 'twoHours',
  notificationTimeout: 'fiveSeconds',
  ollamaHost: 'localhost',
  ollamaModel: 'llama3',
  ollamaPort: 11434,
  opaqueSurround: true,
  overlayAccent: 'brand',
  promptForDownload: true,
  schemaVersion: 1,
  sidebarPosition: 'left',
  startupBehavior: 'restoreLastActive',
  surroundColor: '#12282E',
  swipeBlendFactor: 6.0,
  swipeCommitThreshold: 0.15,
  swipeFollowTau: 0.15,
  swipeHoldSilenceGap: 0.5,
  swipeOvershootCap: 0.40,
  swipeSettleDur: 0.28,
  swipeSlowCommit: 0.30,
  swipeSpringDamp: 0.86,
  vimModeEnabled: false,
};

// The General pane with the core's values already pushed, which is the only
// state in which any row is editable.
function loadedRoot() {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings(CORE_SETTINGS);
  h.sent.length = 0;
  return root;
}

// installSettings lands on General. The Ollama rows are on the AI pane, and
// only the selected model's own rows are on screen - callers
// name the one they are testing.
function aiRoot(model) {
  const root = loadedRoot();
  root.querySelector('.settings-rail-item[data-pane="ai"]').click();
  selectAiModel(root, model);
  h.sent.length = 0;
  return root;
}

// Drives the Model row the same way a user does, through its own <select>.
function selectAiModel(root, model) {
  const select = root.querySelector('.settings-row-ai-model');
  select.value = model;
  select.dispatchEvent(new Event('change', {bubbles: true}));
}

// The id the most recent settingsChanged carried, for a caller replaying the
// C++ side's echo back into atriumSetSaveResult.
const lastSettingsSaveId = () =>
    h.sent.filter(([name]) => name === 'settingsChanged').at(-1)[1][1];

const warnToggle = (root) =>
    root.querySelector(
        '.settings-row[data-key="confirmToQuitEnabled"] .settings-row-toggle');

const warnValue = (root) =>
    root.querySelector(
            '.settings-row[data-key="confirmToQuitEnabled"] .settings-row-value')
        .textContent;

const littleToggle = (root) =>
    root.querySelector(
        '.settings-row[data-key="externalLinksOpenInLittle"] .settings-row-toggle');

const memorySaverToggle = (root) =>
    root.querySelector(
        '.settings-row[data-key="memorySaverEnabled"] .settings-row-toggle');

const memorySaverThresholdSelect = (root) =>
    root.querySelector(
        '.settings-row[data-key="memorySaverThreshold"] .settings-row-choice');

const keepAliveToggle = (root) =>
    root.querySelector(
        '.settings-row[data-key="favoriteKeepAliveEnabled"] .settings-row-toggle');

const keepAliveThresholdSelect = (root) =>
    root.querySelector(
        '.settings-row[data-key="favoriteKeepAliveThreshold"] .settings-row-choice');

const notificationTimeoutSelect = (root) =>
    root.querySelector(
        '.settings-row[data-key="notificationTimeout"] .settings-row-choice');

// sidebarPosition and startupBehavior are segmented (under three
// choices), not a <select> - a button per choice, one of them marked .on.
const segOption = (root, key, choice) =>
    root.querySelector(
        `.settings-row[data-key="${key}"] .settings-seg-option[data-choice="${choice}"]`);

const segSelected = (root, key) =>
    root.querySelector(`.settings-row[data-key="${key}"] .settings-seg-option.on`);

const pageTintToggle = (root) =>
    root.querySelector(
        '.settings-row[data-key="chromePageTint"] .settings-row-toggle');

const opaqueSurroundToggle = (root) =>
    root.querySelector(
        '.settings-row[data-key="opaqueSurround"] .settings-row-toggle');

const overlayAccentSelect = (root) =>
    root.querySelector(
        '.settings-row[data-key="overlayAccent"] .settings-row-choice');

const ollamaHostInput = (root) =>
    root.querySelector('.settings-row[data-key="ollamaHost"] .settings-row-text');

const ollamaPortInput = (root) =>
    root.querySelector('.settings-row[data-key="ollamaPort"] .settings-row-number');

const ollamaModelInput = (root) =>
    root.querySelector('.settings-row[data-key="ollamaModel"] .settings-row-text');

const customEndpointUrlInput = (root) =>
    root.querySelector('.settings-row[data-key="customEndpointUrl"] .settings-row-text');

const customEndpointModelInput = (root) =>
    root.querySelector('.settings-row[data-key="customEndpointModel"] .settings-row-text');
const maxTokensInput = (root) =>
    root.querySelector('.settings-row[data-key="customEndpointMaxTokens"] input');
const timeoutInput = (root) =>
    root.querySelector('.settings-row[data-key="customEndpointTimeoutSeconds"] input');
const advancedCard = (root) => root.querySelector('.settings-group-collapsible');
const pageTextCapInput = (root) =>
    root.querySelector('.settings-row[data-key="aiPageTextCap"] input');

const apiKeyInput = (root) => root.querySelector('.settings-row-api-key');

const apiKeyHint = (root) =>
    root.querySelector('.settings-row[data-key="customEndpointApiKey"] .settings-row-hint');

const clearApiKeyButton = (root) =>
    root.querySelector('.settings-row-clear-api-key');

const aiEnabledToggle = (root) =>
    root.querySelector('.settings-row[data-key="aiEnabled"] .settings-row-toggle');

const autoSummarizeToggle = (root) =>
    root.querySelector(
        '.settings-row[data-key="autoSummarize"] .settings-row-toggle');

const aiModelSelect = (root) => root.querySelector('.settings-row-ai-model');

const aiModelHint = (root) =>
    root.querySelector('.settings-row[data-key="aiModel"] .settings-row-hint');

const testConnectionButton = (root) =>
    root.querySelector('.settings-row-test-connection');

const testCustomEndpointConnectionButton = (root) =>
    root.querySelector('.settings-row-test-custom-endpoint-connection');

const customEndpointConnectionHint = (root) =>
    root.querySelector(
        '.settings-row[data-key="customEndpointConnection"] .settings-row-hint');

const REGISTERED = new Set(
    fs.readFileSync(
          path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
          'utf8')
        .split('kSurfaceMessages[] = {')[1]
        .split('};')[0]
        .match(/"[^"]+"/g)
        .map((quoted) => quoted.slice(1, -1)));

function freshRoot() {
  const root = document.createElement('div');
  document.body.appendChild(root);
  h.sent.length = 0;
  return root;
}

test('the rail renders one item per pane', () => {
  const root = freshRoot();
  renderSettings(root, PANES[0].id);
  assert.equal(root.querySelectorAll('.settings-rail-item').length, PANES.length);
});

test('a fresh open lands on the first pane and says so', () => {
  const root = freshRoot();
  installSettings(root);
  const ready = h.sent.find(([name]) => name === 'settingsReady');
  assert.ok(ready, 'settingsReady is sent');
  assert.deepEqual(ready[1], [PANES[0].id]);
  assert.equal(root.querySelector('.settings-body').dataset.pane, PANES[0].id);
});

test('a click selects that pane and reports its id, not its index', () => {
  const root = freshRoot();
  installSettings(root);
  const target = PANES[2];
  root.querySelector(`.settings-rail-item[data-pane="${target.id}"]`).click();

  const sent = h.sent.find(([name]) => name === 'settingsPaneSelected');
  assert.ok(sent, 'settingsPaneSelected is sent');
  assert.deepEqual(sent[1], [target.id], 'the id travels, never the position');
  assert.equal(root.querySelector('.settings-body').dataset.pane, target.id);
});

test('re-selecting the open pane sends nothing', () => {
  const root = freshRoot();
  installSettings(root);
  h.sent.length = 0;
  root.querySelector(`.settings-rail-item[data-pane="${PANES[0].id}"]`).click();
  assert.equal(
      h.sent.filter(([n]) => n === 'settingsPaneSelected').length, 0,
      'a no-op click is not a change to report');
});

// the rail's keyboard half. A rail item is a <button>, so without a
// roving tabindex every pane is its own Tab stop and Tab walks the rail instead
// of leaving it for the pane's controls.
const railItem = (root, id) =>
    root.querySelector(`.settings-rail-item[data-pane="${id}"]`);

const press = (el, key) =>
    el.dispatchEvent(new Event('keydown', {key, bubbles: true}));

test('the rail is one Tab stop, not one per pane', () => {
  const root = freshRoot();
  installSettings(root);
  const items = root.querySelectorAll('.settings-rail-item');
  const tabbable = items.filter((i) => i.tabIndex === 0);
  assert.equal(tabbable.length, 1, 'exactly one rail item is in the Tab order');
  assert.equal(tabbable[0].dataset.pane, PANES[0].id,
               'and it is the selected one, so Tab returns where the eye is');
});

test('an open window starts with the keyboard on the rail', () => {
  // Otherwise the first arrow key lands on the body and does nothing, and the
  // rail is only reachable by first pressing Tab an unguessable number of
  // times.
  const root = freshRoot();
  installSettings(root);
  assert.equal(document.activeElement, railItem(root, PANES[0].id));
});

test('an arrow key moves the selection to the next pane and reports it', () => {
  const root = freshRoot();
  installSettings(root);
  h.sent.length = 0;
  press(railItem(root, PANES[0].id), 'ArrowDown');

  assert.equal(root.querySelector('.settings-body').dataset.pane, PANES[1].id);
  const sent = h.sent.find(([name]) => name === 'settingsPaneSelected');
  assert.ok(sent, 'a keyboard selection reports the same way a click does');
  assert.deepEqual(sent[1], [PANES[1].id]);
});

test('the rail reads both axes, because the rail has been drawn both ways', () => {
  for (const [key, expected] of [['ArrowRight', PANES[1].id],
                                 ['ArrowLeft', PANES.at(-1).id]]) {
    const root = freshRoot();
    installSettings(root);
    press(railItem(root, PANES[0].id), key);
    assert.equal(root.querySelector('.settings-body').dataset.pane, expected,
                 `${key} moves the selection`);
  }
});

test('the selection wraps rather than dead-ending at either end', () => {
  const root = freshRoot();
  installSettings(root);
  press(railItem(root, PANES[0].id), 'ArrowUp');
  assert.equal(root.querySelector('.settings-body').dataset.pane,
               PANES.at(-1).id);
  press(railItem(root, PANES.at(-1).id), 'ArrowDown');
  assert.equal(root.querySelector('.settings-body').dataset.pane, PANES[0].id);
});

test('Home and End reach the ends in one press', () => {
  const root = freshRoot();
  installSettings(root);
  press(railItem(root, PANES[0].id), 'End');
  assert.equal(root.querySelector('.settings-body').dataset.pane,
               PANES.at(-1).id);
  press(railItem(root, PANES.at(-1).id), 'Home');
  assert.equal(root.querySelector('.settings-body').dataset.pane, PANES[0].id);
});

test('the keyboard stays on the rail after a move', () => {
  // renderSettings() rebuilds the whole tree, so the element the key was
  // pressed on is gone. Without an explicit focus the window drops focus to
  // the body and the next arrow key does nothing at all.
  const root = freshRoot();
  installSettings(root);
  press(railItem(root, PANES[0].id), 'ArrowDown');
  assert.equal(document.activeElement, railItem(root, PANES[1].id));
  press(document.activeElement, 'ArrowDown');
  assert.equal(root.querySelector('.settings-body').dataset.pane, PANES[2].id);
});

test('an arrow key inside the pane body is not a rail move', () => {
  // A <select> row uses arrows to change its own value. The rail must not
  // steal them.
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings(CORE_SETTINGS);
  h.sent.length = 0;
  press(root.querySelector('.settings-row-choice, .settings-row'), 'ArrowDown');
  assert.equal(root.querySelector('.settings-body').dataset.pane, PANES[0].id);
  assert.equal(h.sent.filter(([n]) => n === 'settingsPaneSelected').length, 0);
});

test('a retired pane id resolves to the pane that absorbed it', () => {
  // The lab merged shortcuts and swipe into inputs. A stored
  // selection naming either must not silently land on General.
  assert.equal(paneOrDefault('shortcuts'), 'inputs');
  assert.equal(paneOrDefault('swipe'), 'inputs');
});

test('the pane set and order match the approved design', () => {
  const core = PANES.filter((p) => p.id !== 'engines' && p.id !== 'profiles' &&
                                    p.id !== 'import')
      .map((p) => p.id);
  assert.deepEqual(core.slice(0, 6),
                   ['general', 'appearance', 'spaces', 'ai', 'inputs', 'routing'],
                   'first five and their order come from the lab; routing stays');
  // About, Search engines and Profiles are all pinned outside the lab's own
  // six rather than
  // folded into the list above, so the design set stays reviewable as itself.
  assert.deepEqual(core.slice(6), ['about']);
  assert.equal(PANES.findIndex((p) => p.id === 'profiles'),
               PANES.findIndex((p) => p.id === 'spaces') + 1,
               'Profiles sits right after Spaces');
  assert.equal(PANES.findIndex((p) => p.id === 'engines'),
               PANES.findIndex((p) => p.id === 'profiles') + 1,
               'Search engines sits right after Profiles');
  assert.equal(PANES.findIndex((p) => p.id === 'import'),
               PANES.findIndex((p) => p.id === 'routing') + 1,
               'Import sits right after Routing');
});

test('an unknown pane id falls back rather than rendering an empty frame', () => {
  assert.equal(paneOrDefault('a-pane-from-a-later-build'), PANES[0].id);
  assert.equal(isKnownPane('a-pane-from-a-later-build'), false);
  const root = freshRoot();
  renderSettings(root, 'a-pane-from-a-later-build');
  assert.equal(root.querySelector('.settings-body').dataset.pane, PANES[0].id);
});

test('every name this route sends is registered in kSurfaceMessages', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings(CORE_SETTINGS);
  root.querySelector(`.settings-rail-item[data-pane="${PANES[1].id}"]`).click();
  root.querySelector(`.settings-rail-item[data-pane="general"]`).click();
  warnToggle(root).click();
  // the licenses button's name crosses the same seam, and an
  // unregistered one would be fatal in the browser rather than a dead click.
  root.querySelector('.settings-rail-item[data-pane="about"]').click();
  root.querySelector('.settings-row-credits').click();
  const names = new Set(h.sent.map(([name]) => name));
  assert.ok(names.has('settingsOpenCredits'), 'the licenses button sent');
  assert.ok(names.size > 0, 'the route sends something');
  for (const name of names) {
    assert.ok(
        REGISTERED.has(name),
        `${name} must be in kSurfaceMessages - an unregistered name is FATAL ` +
            'in the browser process');
  }
});

test('the General pane shows one row per lab row', () => {
  const root = freshRoot();
  renderSettings(root, 'general');
  assert.equal(root.querySelectorAll('.settings-row').length, GENERAL_ROWS.length);
});

// A profile click, a toggle, or a push from the browser rebuilds the open
// pane in place. Only a rail change is an arrival.
test('a rebuild of the open pane does not replay the arrival animation', () => {
  const root = freshRoot();
  const swaps = () =>
      root.querySelector('.settings-body').className.includes('settings-pane-swap');
  renderSettings(root, 'general');
  assert.ok(swaps(), 'the first arrival animates');
  renderSettings(root, 'general');
  assert.ok(!swaps(), 'a rebuild in place must not slide or fade');
  renderSettings(root, 'appearance');
  assert.ok(swaps(), 'a rail change still animates');
});

test('a row backed by the core shows its value', () => {
  assert.equal(
      rowValue({label: 'x', key: 'sidebarPosition'}, {sidebarPosition: 'left'}),
      'left');
  assert.equal(
      rowValue({label: 'x', key: 'confirmToQuitEnabled'},
               {confirmToQuitEnabled: true}),
      'On');
});

test('a row with no store says so, and a row still waiting shows a dash', () => {
  // these were both a dash, and they are different states. "No store
  // in this build" is permanent until a task gives it one; "the browser has
  // not pushed a value" clears on the next push. A pane of dashes reads as
  // broken, which is what the honesty pass is for.
  assert.equal(rowValue({label: 'Default browser', key: null}, {}), NOT_WIRED);
  assert.equal(rowValue({label: 'x', key: 'notInTheStore'}, {}), '—');
});

test('an unwired row is marked on the row, not only in the value', () => {
  // The mark carries the dimming (settings.css) - a reader scanning labels has
  // to see which rows are not there yet without reaching the right column.
  const root = freshRoot();
  renderSettings(root, 'general');
  const unwired = [...root.querySelectorAll('.settings-row.unwired')];
  assert.equal(unwired.length,
               GENERAL_ROWS.filter((r) => !r.key && !r.special).length,
               'every keyless General row without its own renderer is marked');
  for (const row of unwired) {
    assert.equal(row.dataset.key, '');
    assert.equal(row.querySelector('.settings-row-value').textContent,
                 NOT_WIRED);
  }
});

// The Hide sidebar row is the window's state, not the store's: the browser
// pushes sidebarHidden beside the settings and the flip travels back on its
// own message, so no settingsChanged save carries it.
test('Hide sidebar is a switch that mirrors the window and sends its flip', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({...CORE_SETTINGS, sidebarHidden: false});
  const row = root.querySelector('.settings-row[data-row="hide-sidebar"]');
  assert.ok(row, 'the General pane renders the row by its mark');
  assert.equal(row.classList.contains('unwired'), false);
  const toggle = row.querySelector('.settings-row-toggle');
  assert.equal(toggle.getAttribute('aria-pressed'), 'false');

  h.sent.length = 0;
  toggle.click();
  const sent = h.sent.find(([n]) => n === 'settingsSidebarHiddenChanged');
  assert.deepEqual(sent[1], [true]);
  assert.equal(h.sent.filter(([n]) => n === 'settingsChanged').length, 0,
               'the flip is not a settings save');
  assert.equal(root.querySelector('.settings-row[data-row="hide-sidebar"] .settings-row-toggle')
                   .getAttribute('aria-pressed'), 'true', 'the switch flips at once');
});

test('Hide sidebar reads a pushed collapsed window as on', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({...CORE_SETTINGS, sidebarHidden: true});
  const toggle = root.querySelector('.settings-row[data-row="hide-sidebar"] .settings-row-toggle');
  assert.equal(toggle.getAttribute('aria-pressed'), 'true');
  h.sent.length = 0;
  toggle.click();
  assert.deepEqual(h.sent.find(([n]) => n === 'settingsSidebarHiddenChanged')[1], [false]);
});

test('Default browser waits for the check, then offers Make default only when it is not', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings(CORE_SETTINGS);
  const rowSel = '.settings-row[data-row="default-browser"]';
  assert.equal(root.querySelector(rowSel).classList.contains('unwired'), false);
  assert.equal(root.querySelector(`${rowSel} .settings-row-make-default`), null,
               'no button before the check answers');
  assert.match(root.querySelector(`${rowSel} .settings-row-hint`).textContent, /Checking/);

  window.atriumSetSettings({...CORE_SETTINGS, defaultBrowser: 'no'});
  const button = root.querySelector(`${rowSel} .settings-row-make-default`);
  assert.ok(button, 'not the default: the row offers the button');
  h.sent.length = 0;
  button.click();
  assert.deepEqual(h.sent.find(([n]) => n === 'settingsMakeDefaultBrowser')[1], []);
  assert.equal(root.querySelector(`${rowSel} .settings-row-make-default`).disabled, true,
               'one ask at a time while macOS shows its prompt');

  window.atriumSetSettings({...CORE_SETTINGS, defaultBrowser: 'no'});
  assert.equal(root.querySelector(`${rowSel} .settings-row-make-default`).disabled, false,
               'the re-check after the ask hands the button back');

  window.atriumSetSettings({...CORE_SETTINGS, defaultBrowser: 'unknown'});
  assert.ok(root.querySelector(`${rowSel} .settings-row-make-default`),
            'a check that could not tell still offers the ask');
  assert.match(root.querySelector(`${rowSel} .settings-row-hint`).textContent, /Could not tell/);

  window.atriumSetSettings({...CORE_SETTINGS, defaultBrowser: 'yes'});
  assert.equal(root.querySelector(`${rowSel} .settings-row-make-default`), null,
               'already the default: nothing to press');
  assert.match(root.querySelector(`${rowSel} .settings-row-value`).textContent, /Default/);
});

test('an unwired row is never a control', () => {
  // isToggleable already needs a bool from the store, but the surface must not
  // grow a keyless row that looks pressable either.
  const root = loadedRoot();
  for (const pane of ['general', 'appearance', 'ai', 'inputs']) {
    renderSettings(root, pane);
    assert.equal(
        root.querySelectorAll('.settings-row.unwired .settings-row-toggle')
            .length,
        0, `${pane} offers nothing to click on an unwired row`);
  }
});

test('the wired rows are left alone by the honesty pass', () => {
  const root = loadedRoot();
  for (const pane of ['general', 'appearance']) {
    renderSettings(root, pane);
    for (const row of root.querySelectorAll('.settings-row[data-key]')) {
      if (!row.dataset.key) {
        continue;
      }
      assert.equal(row.classList.contains('unwired'), false,
                   `${row.dataset.key} has a store and must not be marked`);
      assert.notEqual(
          row.querySelector('.settings-row-value').textContent, NOT_WIRED);
    }
  }
});

test('a push re-renders the open pane rather than resetting to General', () => {
  // The root is the element installSettings was GIVEN. It used to be looked up
  // by id, found nothing, and skipped every repaint - the values arrived and
  // the rows kept showing dashes. No id is set here on purpose.
  const root = freshRoot();
  installSettings(root);
  root.querySelector('.settings-rail-item[data-pane="spaces"]').click();
  window.atriumSetSettings({sidebarPosition: 'right'});
  assert.equal(root.querySelector('.settings-body').dataset.pane, 'spaces');
});

test('a push paints the values into the rows', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({sidebarPosition: 'right', confirmToQuitEnabled: true});
  const rows = [...root.querySelectorAll('.settings-row')];
  // A select/input's current value lives in .value, not .textContent - a
  // select's textContent is the concatenation of every <option>'s text, and
  // an input never reflects its value there at all. Only a plain span/button
  // row (the toggle and unwired shapes) uses .textContent.
  const valueFor = (key) => {
    const el = rows.find((r) => r.dataset.key === key)
                   .querySelector('.settings-row-value');
    return el.value !== undefined ? String(el.value) : el.textContent;
  };
  assert.equal(segSelected(root, 'sidebarPosition').dataset.choice, 'right');
  assert.equal(valueFor('confirmToQuitEnabled'), 'On');
});

test('Appearance and AI render their lab rows', () => {
  const root = freshRoot();
  // Eight. Light/dark/auto and Native tint strength are not rows, since they
  // have a field in no store. The rest each have one, including
  // overlayAccent and borderWidth.
  renderSettings(root, 'appearance');
  assert.equal(root.querySelectorAll('.settings-row').length, 8);
  // Five: the kill switch, the Model row, Auto summarize, the translation
  // language and the page text cap. Ollama and Endpoint rows are gated
  // on the Model selection (default On-device).
  renderSettings(root, 'ai');
  assert.equal(root.querySelectorAll('.settings-row').length, 5);
});

test('Appearance keeps only rows a store can answer', () => {
  // A row's key needs a settings.rs field that survives a load and a save
  // - the removed rows never had one anywhere.
  assert.deepEqual(PANE_ROWS.appearance.map((r) => r.key),
                   ['chromePageTint', 'opaqueSurround', 'borderWidth',
                    'overlayAccent', 'glassOpacity', 'skylightNightShift',
                    'skylightWeather', 'appIconVariant']);
  assert.equal(PANE_ROWS.appearance.some((r) => !r.key), false);
});

// both Skylight modules already composed every state and were
// handed pinned defaults, so these rows are the state reaching them.
test('the Skylight rows read the values the store pushes', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({skylightWeather: 'snow', skylightNightShift: false});
  root.querySelector('.settings-rail-item[data-pane="appearance"]').click();

  const rows = [...root.querySelectorAll('.settings-row')];
  const valueFor = (key) => {
    const row = rows.find((r) => r.dataset.key === key);
    assert.ok(row, `${key} is missing from Appearance`);
    const el = row.querySelector('.settings-row-value');
    return el.value !== undefined ? String(el.value) : el.textContent;
  };
  assert.equal(valueFor('skylightWeather'), 'snow');
  // Off, not the default On: a pushed false has to win, or the row would read
  // as enabled while the backdrop stayed flat.
  assert.equal(valueFor('skylightNightShift'), 'Off');
});

test('Appearance reads the two fields the store actually carries', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({opaqueSurround: true, glassOpacity: 0.82});
  root.querySelector('.settings-rail-item[data-pane="appearance"]').click();
  const rows = [...root.querySelectorAll('.settings-row')];
  const valueFor = (key) => {
    const el = rows.find((r) => r.dataset.key === key)
                   .querySelector('.settings-row-value');
    return el.value !== undefined ? String(el.value) : el.textContent;
  };
  assert.equal(valueFor('opaqueSurround'), 'On');
  assert.equal(valueFor('glassOpacity'), '0.82');
});

// The 'brand' spelling is retired in favour of the Crimson theme,
// whose accent is the same rgb(176 58 72). A file still saying 'brand' has
// to select Crimson - with no option matching, the select shows its first.
test('a stored brand accent selects Crimson', () => {
  const root = appearanceRoot();
  const select = overlayAccentSelect(root);
  assert.ok(select, 'overlayAccent renders a select');
  assert.equal(select.value, 'crimson');
});

test('changing overlayAccent sends the WHOLE object with the new choice', () => {
  const root = appearanceRoot();
  const select = overlayAccentSelect(root);
  select.value = 'dracula';
  select.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.overlayAccent, 'dracula');
  assert.deepEqual(
      {...payload, overlayAccent: 'brand'}, CORE_SETTINGS,
      'the whole object travels - the same contract every other choice row has');
});

test('a fresh push after a restart repaints overlayAccent with the saved choice', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({...CORE_SETTINGS, overlayAccent: 'dracula'});
  root.querySelector('.settings-rail-item[data-pane="appearance"]').click();
  assert.equal(overlayAccentSelect(root).value, 'dracula');
});

// --- the app icon picker ---

function appearanceRoot() {
  const root = loadedRoot();
  root.querySelector('.settings-rail-item[data-pane="appearance"]').click();
  h.sent.length = 0;
  return root;
}

const iconTile = (root, slug) =>
    root.querySelector(`.settings-app-icon[data-variant="${slug}"]`);

test('the App icon row shows one tile per committed variant', () => {
  const root = appearanceRoot();
  const tiles = [...root.querySelectorAll('.settings-app-icon')];
  assert.equal(tiles.length, 22);
  assert.deepEqual(tiles.map((t) => t.dataset.variant), APP_ICON_VARIANTS);
});

// Root-relative, so the tile loads from the host this page is served from.
// A cross-host chrome:// image fails before any data source runs.
test('each tile asks the browser for its own PNG', () => {
  const root = appearanceRoot();
  for (const slug of APP_ICON_VARIANTS) {
    const img = iconTile(root, slug).querySelector('.settings-app-icon-img');
    assert.equal(img.src, `/appicon/${slug}.png`);
    assert.equal(img.alt, slug);
  }
});

test('the pushed variant is the tile marked selected', () => {
  const root = appearanceRoot();
  assert.equal(iconTile(root, 'crimson-ink').classList.contains('on'), true);
  assert.equal(iconTile(root, 'crimson-ink').getAttribute('aria-checked'),
               'true');
  assert.equal(iconTile(root, 'crimson-paper').classList.contains('on'), false);
});

test('a retired variant on disk still lets a click pick a new one', () => {
  // A settings.json written before solarized-ink was retired. The core maps
  // it to the default on load, but the pushed object carries the raw value,
  // and a click that stopped on it left the picker dead.
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({...CORE_SETTINGS, appIconVariant: 'solarized-ink'});
  root.querySelector('.settings-rail-item[data-pane="appearance"]').click();
  h.sent.length = 0;
  assert.equal(root.querySelector('.settings-app-icon.on'), null);

  iconTile(root, 'crimson-ink').click();

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged was sent');
  assert.equal(JSON.parse(sent[1][0]).appIconVariant, 'crimson-ink');
});

test('picking a tile sends the whole object with the new variant', () => {
  const root = appearanceRoot();
  iconTile(root, 'nord-accent').click();

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged was sent');
  const written = JSON.parse(sent[1][0]);
  assert.equal(written.appIconVariant, 'nord-accent');
  // A field the pick does not touch travels unchanged - this is one edit to
  // the whole object, not a partial one.
  assert.equal(written.sidebarPosition, 'left');
});

test('picking a tile repaints the row so it reads as the one now chosen', () => {
  const root = appearanceRoot();
  iconTile(root, 'nord-accent').click();
  assert.equal(iconTile(root, 'nord-accent').classList.contains('on'), true);
  assert.equal(iconTile(root, 'crimson-ink').classList.contains('on'), false);
});

// The list is hardcoded because the browser has no list to push. It is only
// correct while it matches the files inject-icns.sh actually stages.
test('APP_ICON_VARIANTS names exactly the PNGs in build/icons', () => {
  const files = fs.readdirSync(ICONS_DIR)
      .filter((name) => name.endsWith('.png'))
      .map((name) => name.slice(0, -'.png'.length))
      .sort();
  assert.deepEqual([...APP_ICON_VARIANTS].sort(), files);
});

// Auto leads because it is the default, then Crimson, then the accents
// alphabetically. A tile order that drifts makes the three shipped variants
// hard to find in a grid of 22.
test('Auto leads, the two Crimson tiles follow, then the accents in order',
     () => {
       assert.deepEqual(APP_ICON_VARIANTS.slice(0, 3),
                        ['auto', 'crimson-paper', 'crimson-ink']);
       const rest = APP_ICON_VARIANTS.slice(3);
       assert.deepEqual(rest, [...rest].sort());
     });

// The tile the picker opens on when settings.json has never been written.
// It is the one slug the runtime answers by clearing the Dock icon rather
// than by naming a stack, so a rename here breaks that branch silently.
test('the Auto tile is the first one and asks for auto.png', () => {
  const root = appearanceRoot();
  const first = root.querySelector('.settings-app-icon');
  assert.equal(first.dataset.variant, 'auto');
  assert.equal(first.querySelector('.settings-app-icon-img').src,
               '/appicon/auto.png');
});

test('every row in the AI pane has a settings.rs key - none are unwired', () => {
  const WIRED = [
    'aiEnabled', 'aiModel', 'autoSummarize', 'translateLanguage', 'ollamaHost',
    'ollamaPort', 'ollamaModel', 'customEndpointUrl', 'customEndpointModel',
    'customEndpointMaxTokens', 'customEndpointTimeoutSeconds',
    'aiPageTextCap',
  ];
  const unwired = PANE_ROWS.ai.filter((r) => !WIRED.includes(r.key));
  assert.equal(unwired.length, 0);
});

// the Model selector. It has a settings.rs field.

test('before any push, or on an unrecognised value, the row reads as On-device', () => {
  const root = freshRoot();
  installSettings(root);
  root.querySelector('.settings-rail-item[data-pane="ai"]').click();
  assert.equal(aiModelSelect(root).value, 'onDevice');

  window.atriumSetSettings({...CORE_SETTINGS, aiModel: 'somethingNewer'});
  assert.equal(aiModelSelect(root).value, 'onDevice');
});

test('On-device is the default, and no per-model row is on screen for it', () => {
  const root = aiRoot('onDevice');
  assert.equal(aiModelSelect(root).value, 'onDevice');
  assert.equal(ollamaHostInput(root), null);
  assert.equal(ollamaPortInput(root), null);
  assert.equal(ollamaModelInput(root), null);
  assert.equal(customEndpointUrlInput(root), null);
  assert.equal(customEndpointModelInput(root), null);
  assert.equal(maxTokensInput(root), null);
  assert.equal(timeoutInput(root), null);
  assert.equal(advancedCard(root), null, 'no Advanced card either');
  assert.equal(root.querySelector('.settings-row[data-key="ollamaConnection"]'), null);
  assert.equal(apiKeyInput(root), null);
});

test('picking Ollama shows only Ollama\'s rows', () => {
  const root = aiRoot('ollama');
  assert.ok(ollamaHostInput(root), 'ollamaHost is on screen');
  assert.ok(ollamaPortInput(root), 'ollamaPort is on screen');
  assert.ok(ollamaModelInput(root), 'ollamaModel is on screen');
  assert.ok(root.querySelector('.settings-row[data-key="ollamaConnection"]'));
  assert.equal(customEndpointUrlInput(root), null, 'Endpoint is not');
  assert.equal(customEndpointModelInput(root), null, 'customEndpointModel is not');
  assert.equal(advancedCard(root), null, 'the Advanced card is not');
  assert.equal(apiKeyInput(root), null, 'the API key row is not');
});

test('picking Custom endpoint shows only its own rows', () => {
  const root = aiRoot('customEndpoint');
  assert.ok(customEndpointUrlInput(root), 'Endpoint is on screen');
  assert.ok(customEndpointModelInput(root), 'customEndpointModel is on screen');
  assert.ok(apiKeyInput(root), 'the API key row is on screen');
  assert.ok(testCustomEndpointConnectionButton(root));
  assert.equal(ollamaHostInput(root), null, 'ollamaHost is not');
  assert.equal(ollamaPortInput(root), null, 'ollamaPort is not');
  assert.equal(ollamaModelInput(root), null, 'ollamaModel is not');
  assert.equal(root.querySelector('.settings-row[data-key="ollamaConnection"]'), null);
});

test('switching the selector repaints which rows are on screen without a save', () => {
  const root = aiRoot('ollama');
  assert.ok(ollamaHostInput(root));
  assert.ok(ollamaModelInput(root));
  selectAiModel(root, 'customEndpoint');
  assert.equal(ollamaHostInput(root), null, 'Ollama rows are gone');
  assert.equal(ollamaModelInput(root), null, 'ollamaModel is gone too');
  assert.ok(customEndpointUrlInput(root), 'Endpoint rows are on screen instead');
  assert.ok(customEndpointModelInput(root), 'customEndpointModel is on screen instead');
});

test('an unavailable on-device model says so rather than being silently pickable', () => {
  const root = aiRoot('onDevice');
  window.atriumSetSettings({...CORE_SETTINGS, aiOnDeviceAvailable: false});
  assert.match(aiModelHint(root).textContent, /Unavailable on this Mac/);
  assert.ok(aiModelHint(root).classList.contains('settings-row-warn'));
  // Still selectable - the row states the problem, it does not remove the
  // option (Ollama and the custom endpoint report their own reachability the
  // same way, through a hint rather than a disabled control).
  assert.equal(aiModelSelect(root).value, 'onDevice');
});

test('with no availability signal pushed, on-device reads as not detected rather than a guess', () => {
  const root = aiRoot('onDevice');
  assert.equal(aiModelHint(root).textContent, 'Availability not detected yet');
  assert.equal(
      aiModelHint(root).classList.contains('settings-row-warn'), false,
      'unknown is not the same as unavailable');
});

test('Ollama and Custom endpoint never read as unavailable - they have their own connection check', () => {
  const ollamaRoot = aiRoot('ollama');
  assert.doesNotMatch(aiModelHint(ollamaRoot).textContent, /Unavailable|not detected/);
  const endpointRoot = aiRoot('customEndpoint');
  assert.doesNotMatch(aiModelHint(endpointRoot).textContent, /Unavailable|not detected/);
});

test('picking a model sends it through setRow/settingsChanged, like any other choice row', () => {
  const root = aiRoot('ollama');
  selectAiModel(root, 'customEndpoint');
  const [name, [json]] = h.sent.find(([n]) => n === 'settingsChanged');
  assert.equal(name, 'settingsChanged');
  assert.equal(JSON.parse(json).aiModel, 'customEndpoint');
});

test('a fresh push from the browser carries the model choice, and the row follows it', () => {
  const root = aiRoot('customEndpoint');
  window.atriumSetSettings({...CORE_SETTINGS, aiModel: 'ollama'});
  assert.equal(aiModelSelect(root).value, 'ollama');
  assert.ok(ollamaHostInput(root), 'the pushed model, not the pre-push selection, decides what is on screen');
  assert.equal(customEndpointUrlInput(root), null);
});

// An unrelated push must not wipe a field the user has not committed
// yet - setSettings used to rebuild every row from the store on every push.
test('an unrelated push keeps a mid-keystroke text field and its caret', () => {
  const root = aiRoot('ollama');
  const input = ollamaHostInput(root);
  input.focus();
  input.value = 'localh';
  input.selectionStart = 3;
  input.selectionEnd = 3;

  window.atriumSetSettings({...CORE_SETTINGS, aiModel: 'ollama'});

  const after = ollamaHostInput(root);
  assert.equal(after.value, 'localh');
  assert.equal(after.selectionStart, 3);
  assert.equal(after.selectionEnd, 3);
  assert.equal(document.activeElement, after);
});

test('a row carries the lab hint where the lab has one', () => {
  const root = freshRoot();
  renderSettings(root, 'general');
  const hint = root.querySelector('.settings-row .settings-row-hint');
  assert.ok(hint && hint.textContent.length > 0);
});

test('a combo bound twice is a conflict, reported on both actions', () => {
  const clashes = conflictingCombos({
    goToSpace1: ['cmd+1'],
    openShelf: ['cmd+1', 'cmd+e'],
    quit: ['cmd+q'],
  });
  assert.deepEqual([...clashes.keys()], ['cmd+1']);
  assert.deepEqual(clashes.get('cmd+1'), ['goToSpace1', 'openShelf']);
});

test('a combo bound once is not a conflict', () => {
  assert.equal(conflictingCombos({a: ['cmd+1'], b: ['cmd+2']}).size, 0);
});

test('an action name reads as words, and acronyms stay whole', () => {
  assert.equal(actionLabel('goToSpace1'), 'Go to space 1');
  // Splitting before every capital turned this into 'Edit current u r l'.
  assert.equal(actionLabel('editCurrentURL'), 'Edit current URL');
  assert.equal(actionLabel('openCommandBar'), 'Open command bar');
});

test('the swipe row comes before the shortcuts list, as the lab has it', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {goToSpace1: ['cmd+1']}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  const rows = [...root.querySelectorAll('.settings-row')];
  assert.equal(rows[0].querySelector('.settings-row-label').textContent
                   .startsWith('Swipe feel'),
               true, 'Swipe feel is first');
  assert.ok(rows[3].dataset.action,
            'the shortcut rows follow the Swipe feel, Swipe haptics and Vim mode rows');
});

test('the Inputs pane shows one row per action, plus the three setting rows', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {goToSpace1: ['cmd+1'], openShelf: []}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  assert.equal(root.querySelectorAll('.settings-row[data-action]').length, 2);
  assert.equal(root.querySelectorAll('.settings-row').length, 5);
});

test('an unassigned action says so rather than showing nothing', () => {
  // openCommandBar and the nine goToSidebarItem* ship unassigned. Blank would
  // read as a missing value; it is a real state.
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {openCommandBar: []}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  assert.equal(
      root.querySelector('.settings-row[data-action="openCommandBar"] .settings-row-value')
          .textContent,
      'Unassigned');
});

test('a conflicting row is marked', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {goToSpace1: ['cmd+1'], openShelf: ['cmd+1']}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  assert.equal(root.querySelectorAll('.settings-row.conflict').length, 2,
               'both rows are marked, not only the second');
});

test('a clashing combo paints the last keycap, where settings.css styles it', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {goToSpace1: ['cmd+1'], openShelf: ['cmd+1']}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  const row = hotkeyRow(root, 'goToSpace1');
  const combo = row.querySelector('.combo.conflict');
  assert.ok(combo, 'the clash marks the combo wrapper, not just the row');
  const caps = combo.querySelectorAll('kbd.key');
  assert.ok(caps.length > 0);
  assert.equal(combo.children[combo.children.length - 1], caps[caps.length - 1],
               'settings.css paints .combo.conflict kbd.key:last-child, so the last keycap must be last');
});

// --- the three swipe presets ---

function inputsRoot() {
  const root = loadedRoot();
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  h.sent.length = 0;
  return root;
}

const presetButton = (root, name) =>
    root.querySelector(`.settings-swipe-preset[data-preset="${name}"]`);

const vimModeToggle = (root) =>
    root.querySelector(
        '.settings-row[data-key="vimModeEnabled"] .settings-row-toggle');

test('the Swipe feel row offers the three lab presets', () => {
  const root = inputsRoot();
  const labels = [...root.querySelectorAll('.settings-swipe-preset')]
      .map((b) => b.textContent);
  assert.deepEqual(labels, ['Default', 'Quick', 'Cozy']);
});

test('the preset matching the pushed settings is marked on', () => {
  // CORE_SETTINGS carries the lab's Default preset values.
  const root = inputsRoot();
  assert.equal(presetButton(root, 'Default').classList.contains('on'), true);
  assert.equal(presetButton(root, 'Quick').classList.contains('on'), false);
  assert.equal(activeSwipePreset({swipeCommitThreshold: 0.99}), null);
});

test('choosing a preset applies its eight values and sends the whole object', () => {
  const root = inputsRoot();
  presetButton(root, 'Quick').click();

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged was sent');
  const written = JSON.parse(sent[1][0]);
  assert.equal(written.swipeCommitThreshold, 0.12);
  assert.equal(written.swipeSlowCommit, 0.25);
  assert.equal(written.swipeBlendFactor, 4.0);
  assert.equal(written.swipeHoldSilenceGap, 0.35);
  assert.equal(written.swipeOvershootCap, 0.30);
  assert.equal(written.swipeSettleDur, 0.18);
  assert.equal(written.swipeSpringDamp, 0.95);
  assert.equal(written.swipeFollowTau, 0.08);
  // A field CORE_SETTINGS carries but the preset does not touch travels
  // unchanged - this is one edit to the whole object, not a partial one.
  assert.equal(written.sidebarPosition, 'left');
});

test('choosing a preset repaints the row so it reads as the one now active', () => {
  const root = inputsRoot();
  presetButton(root, 'Cozy').click();
  assert.equal(presetButton(root, 'Cozy').classList.contains('on'), true);
  assert.equal(presetButton(root, 'Default').classList.contains('on'), false);
});

// --- the Vim mode switch ---

test('the Vim mode row is a real toggle, off by default', () => {
  const root = inputsRoot();
  const toggle = vimModeToggle(root);
  assert.ok(toggle, 'vimModeEnabled renders a toggle');
  assert.equal(toggle.getAttribute('aria-pressed'), 'false');
});

test('switching Vim mode on sends the whole object with ' +
     'vimModeEnabled true', () => {
  const root = inputsRoot();
  vimModeToggle(root).click();
  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.vimModeEnabled, true);
  assert.deepEqual({...payload, vimModeEnabled: false}, CORE_SETTINGS);
});

test('the Spaces pane lists every space and details the selected one', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSpaces({spaces: [
    {id: 'a', name: 'Work', emoji: 'W', tint: '#123456', profileID: 'Default',
     tabs: [{url: 'x'}, {url: 'y'}], pinnedTabs: [{url: 'z'}]},
    {id: 'b', name: 'Home', profileID: 'Second', tabs: [], pinnedTabs: []},
  ]});
  root.querySelector('.settings-rail-item[data-pane="spaces"]').click();
  // The New Space button shares .settings-space-item for its layout, so the
  // real spaces are one fewer than the full node count.
  const realSpaces = [...root.querySelectorAll('.settings-space-item')]
      .filter((n) => !n.classList.contains('settings-space-new'));
  assert.equal(realSpaces.length, 2);
  assert.equal(root.querySelector('.settings-space-name').value, 'Work');
  const values = [...root.querySelectorAll('.settings-space-detail .settings-row-value')]
      .map((n) => n.textContent);
  assert.ok(values.includes('2 open, 1 pinned'));
});

test('the Spaces list draws a ph: icon as a Phosphor svg, not the literal string', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSpaces({spaces: [
    {id: 'a', name: 'Work', emoji: 'ph:star', tint: '#123456',
     profileID: 'Default', tabs: [], pinnedTabs: []},
  ]});
  root.querySelector('.settings-rail-item[data-pane="spaces"]').click();
  const svg = root.querySelector('.settings-space-dot').firstChild;
  assert.equal(svg.tagName.toLowerCase(), 'svg');
  assert.equal(svg.dataset.icon, 'star');
});

test('clicking a space switches the detail panel', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSpaces({spaces: [
    {id: 'a', name: 'Work', profileID: 'Default', tabs: [], pinnedTabs: []},
    {id: 'b', name: 'Home', profileID: 'Second', tabs: [], pinnedTabs: []},
  ]});
  root.querySelector('.settings-rail-item[data-pane="spaces"]').click();
  root.querySelector('.settings-space-item[data-space="1"]').click();
  assert.equal(root.querySelector('.settings-space-name').value, 'Home');
  const values = [...root.querySelectorAll('.settings-space-detail .settings-row-value')]
      .map((n) => n.textContent);
  assert.ok(values.includes('Second'));
});

// The space name field carries no PANE_ROWS key, so it was still on the
// full-rebuild path that wipes an in-progress edit.
test('an unrelated push keeps a mid-keystroke space name and its caret', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSpaces({spaces: [
    {id: 'a', name: 'Work', profileID: 'Default', tabs: [], pinnedTabs: []},
  ]});
  root.querySelector('.settings-rail-item[data-pane="spaces"]').click();
  const input = root.querySelector('.settings-space-name');
  input.focus();
  input.value = 'New na';
  input.selectionStart = 6;
  input.selectionEnd = 6;

  window.atriumSetSpaces({spaces: [
    {id: 'a', name: 'Work', profileID: 'Default', tabs: [], pinnedTabs: []},
  ]});

  const after = root.querySelector('.settings-space-name');
  assert.equal(after.value, 'New na');
  assert.equal(after.selectionStart, 6);
  assert.equal(document.activeElement, after);
});

test('spaceDetailRows only carries the Tabs row', () => {
  // Name, Profile, Search engine and Auto-archive are all real controls,
  // rendered separately.
  const rows = spaceDetailRows({name: 'Work', profileID: 'Default'});
  assert.equal(rows.length, 1);
  assert.equal(rows[0].label, 'Tabs');
});

// dom-shim's selector engine has no :not() support, so this filters in JS
// rather than indexing the node list by position.
const archiveTtlSelect = (root) => [
  ...root.querySelectorAll('.settings-space-detail select'),
].find((s) => !s.classList.contains('settings-space-profile'));

test('the Auto-archive select defaults to Follow General when archiveTtl is absent', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSpaces({spaces: [
    {id: 'a', name: 'Work', profileID: 'Default', tabs: [], pinnedTabs: []},
  ]});
  root.querySelector('.settings-rail-item[data-pane="spaces"]').click();
  assert.equal(archiveTtlSelect(root).value, '');
});

test('the Auto-archive select shows the space\'s own rung', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSpaces({spaces: [
    {id: 'a', name: 'Work', profileID: 'Default', tabs: [], pinnedTabs: [],
     archiveTtl: 'thirtyDays'},
  ]});
  root.querySelector('.settings-rail-item[data-pane="spaces"]').click();
  assert.equal(archiveTtlSelect(root).value, 'thirtyDays');
});

test('settingsSpaceArchiveTtlChanged is registered - an unregistered name is FATAL in the browser', () => {
  assert.ok(REGISTERED.has('settingsSpaceArchiveTtlChanged'));
});

test('changing Auto-archive sends the space index and the chosen rung', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSpaces({spaces: [
    {id: 'a', name: 'Work', profileID: 'Default', tabs: [], pinnedTabs: []},
    {id: 'b', name: 'Home', profileID: 'Second', tabs: [], pinnedTabs: []},
  ]});
  root.querySelector('.settings-rail-item[data-pane="spaces"]').click();
  root.querySelector('.settings-space-item[data-space="1"]').click();
  const select = archiveTtlSelect(root);
  select.value = 'sevenDays';
  select.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsSpaceArchiveTtlChanged');
  assert.ok(sent, 'settingsSpaceArchiveTtlChanged is sent');
  assert.deepEqual(sent[1], [1, 'sevenDays']);
});

test('choosing Follow General sends an empty string', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSpaces({spaces: [
    {id: 'a', name: 'Work', profileID: 'Default', tabs: [], pinnedTabs: [],
     archiveTtl: 'twelveHours'},
  ]});
  root.querySelector('.settings-rail-item[data-pane="spaces"]').click();
  const select = archiveTtlSelect(root);
  select.value = '';
  select.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsSpaceArchiveTtlChanged');
  assert.deepEqual(sent[1], [0, '']);
});

// --- the Spaces pane's Theme Studio ------------------------------------------
// A preview that is never undone leaves the window painted with a spec no file
// holds, so the tests below pin when the browser is told to undo one.

const ONE_STOP = {
  name: '',
  stops: [{x: 0.5, y: 0.45, color: '#3366CC', alpha: 0.9}],
  opacity: 1,
  effects: {grain: 0, vignette: 0, frost: 0, warmth: 0},
  darkAdaptation: 0.45,
  nightShiftEnabled: false,
  ambientEnabled: false,
  version: 1,
};

const TWO_STOP = {
  ...ONE_STOP,
  name: 'Sunset',
  stops: [{x: 0, y: 0, color: '#FF8800', alpha: 1},
          {x: 1, y: 1, color: '#0044FF', alpha: 1}],
};

// The shape OnSpacesLoaded pushes: `theme` is what the store holds,
// `themeDraft` what the studio opens on.
const THEME_SPACES = [
  {id: 'a', name: 'Work', profileID: 'Default', tabs: [], pinnedTabs: [],
   theme: TWO_STOP, themeDraft: TWO_STOP},
  {id: 'b', name: 'Home', profileID: 'Second', tabs: [], pinnedTabs: [],
   theme: null, themeDraft: ONE_STOP},
];

function themeRoot(spaceList = THEME_SPACES) {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSpaces({spaces: spaceList});
  root.querySelector('.settings-rail-item[data-pane="spaces"]').click();
  // The selected space is module state that outlives one root, so an earlier
  // test's pick would otherwise decide which space this one edits.
  root.querySelector('.settings-space-item[data-space="0"]').click();
  h.sent.length = 0;
  return root;
}

const themeSent = (name) => h.sent.filter(([msg]) => msg === name);

test('both Theme Studio messages are registered - an unregistered name is FATAL in the browser', () => {
  assert.ok(REGISTERED.has('settingsThemePreview'));
  assert.ok(REGISTERED.has('settingsThemeChanged'));
});

test('the Spaces pane mounts the studio on the selected space', () => {
  const root = themeRoot();
  const studio = root.querySelector('.settings-space-detail .theme-studio');
  assert.notEqual(studio, null, 'the studio is inside the detail panel');
  assert.equal(root.querySelector('.theme-studio-space-name').textContent, 'Work');
  assert.equal(root.querySelectorAll('.theme-studio-swatch').length, 2);
});

test('an unthemed space opens on the draft the browser derived from its tint', () => {
  const root = themeRoot();
  root.querySelector('.settings-space-item[data-space="1"]').click();
  assert.equal(root.querySelector('.theme-studio-space-name').textContent, 'Home');
  assert.equal(root.querySelectorAll('.theme-studio-swatch').length, 1,
               'a tint is a one-stop theme');
});

test('an edit previews the draft on the window behind, under the space id', () => {
  const root = themeRoot();
  root.querySelector('.theme-studio-preset').click();
  h.tick();
  const sent = themeSent('settingsThemePreview');
  assert.equal(sent.length, 1);
  assert.equal(sent[0][1][0], 'a');
  assert.equal(Array.isArray(sent[0][1][1].stops), true);
});

test('Save sends the spec, not a preview', () => {
  const root = themeRoot();
  root.querySelector('.theme-studio-save').click();
  const sent = themeSent('settingsThemeChanged');
  assert.equal(sent.length, 1);
  assert.equal(sent[0][1][0], 'a');
  assert.deepEqual(sent[0][1][1].stops.map((s) => s.color), ['#FF8800', '#0044FF']);
});

test('Remove Theme sends a null spec, which is how the store drops it', () => {
  const root = themeRoot();
  root.querySelector('.theme-studio-remove').click();
  assert.deepEqual(themeSent('settingsThemeChanged'), [
    ['settingsThemeChanged', ['a', null]],
  ]);
});

test('switching space puts the previewed space\'s stored theme back', () => {
  const root = themeRoot();
  root.querySelector('.theme-studio-preset').click();
  h.tick();  // the studio coalesces its send onto the next frame
  h.sent.length = 0;
  root.querySelector('.settings-space-item[data-space="1"]').click();
  assert.deepEqual(themeSent('settingsThemePreview'), [
    ['settingsThemePreview', ['a', null]],
  ]);
});

test('leaving the Spaces pane puts the stored theme back', () => {
  const root = themeRoot();
  root.querySelector('.theme-studio-preset').click();
  h.tick();
  h.sent.length = 0;
  root.querySelector('.settings-rail-item[data-pane="general"]').click();
  assert.deepEqual(themeSent('settingsThemePreview'), [
    ['settingsThemePreview', ['a', null]],
  ]);
  assert.equal(root.querySelector('.theme-studio'), null);
});

test('a saved theme is not undone on the way out', () => {
  const root = themeRoot();
  root.querySelector('.theme-studio-preset').click();
  root.querySelector('.theme-studio-save').click();
  h.sent.length = 0;
  root.querySelector('.settings-rail-item[data-pane="general"]').click();
  assert.deepEqual(themeSent('settingsThemePreview'), []);
});

test('an unrelated spaces push does not undo an unsaved draft', () => {
  // Every push rebuilds the whole pane, so the studio is remounted by edits
  // that have nothing to do with it - a rename elsewhere, a new profile.
  const root = themeRoot();
  root.querySelector('.theme-studio-preset').click();
  h.sent.length = 0;
  window.atriumSetSpaces({spaces: THEME_SPACES});
  assert.deepEqual(themeSent('settingsThemePreview'), []);
  assert.notEqual(root.querySelector('.theme-studio'), null);
});

test('atriumSelectSettingsSpace opens the Spaces pane on that space', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSpaces({spaces: THEME_SPACES});
  window.atriumSelectSettingsSpace(1);
  assert.equal(root.querySelector('.settings-body').dataset.pane, 'spaces');
  assert.equal(root.querySelector('.settings-space-name').value, 'Home');
});

test('atriumSelectSettingsSpace ignores an index no space holds', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSpaces({spaces: THEME_SPACES});
  window.atriumSelectSettingsSpace(9);
  assert.equal(root.querySelector('.settings-body').dataset.pane, 'general');
});

// the space pane's own Profile picker.

test('spaceNameRow shows the space\'s own name', () => {
  const line = spaceNameRow({name: 'Work'}, 0);
  assert.equal(line.querySelector('.settings-space-name').value, 'Work');
});

test('the space pane\'s Profile row is a picker over the profile list', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings(CORE_SETTINGS);
  window.atriumSetSpaces(PROFILE_DOC);
  root.querySelector('.settings-rail-item[data-pane="spaces"]').click();
  // selectedSpace is module state that outlives this call - a prior test's
  // click on a different space otherwise leaks into this one's default.
  root.querySelector('.settings-space-item[data-space="0"]').click();

  const select = root.querySelector('.settings-space-profile');
  assert.ok(select, 'the Profile row is a select, not a static span');
  assert.equal(select.value, 'Default');
  assert.deepEqual(
      [...select.querySelectorAll('option')].map((o) => o.value),
      ['Default', 'Second', 'Spare']);
});

test('picking another profile sends the space index and the profile id', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings(CORE_SETTINGS);
  window.atriumSetSpaces(PROFILE_DOC);
  root.querySelector('.settings-rail-item[data-pane="spaces"]').click();
  root.querySelector('.settings-space-item[data-space="2"]').click();
  h.sent.length = 0;

  const select = root.querySelector('.settings-space-profile');
  select.value = 'Default';
  select.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([n]) => n === 'settingsSpaceProfileChanged');
  assert.ok(sent);
  assert.deepEqual(sent[1], [2, 'Default']);
});

test('a space whose profile has no record still gets a selected option for it', () => {
  // A deleted profile must not read as a silent jump to another one.
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings(CORE_SETTINGS);
  window.atriumSetSpaces({
    spaces: [{id: 'a', name: 'Work', profileID: 'Ghost', tabs: [], pinnedTabs: []}],
    profiles: [{id: 'Default', name: 'Work', spaceCount: 1, available: true}],
  });
  root.querySelector('.settings-rail-item[data-pane="spaces"]').click();

  const select = root.querySelector('.settings-space-profile');
  assert.equal(select.value, 'Ghost');
  assert.deepEqual(
      [...select.querySelectorAll('option')].map((o) => o.value),
      ['Default', 'Ghost']);
});

test('settingsSpaceProfileChanged is registered - an unregistered name is FATAL in the browser', () => {
  assert.ok(REGISTERED.has('settingsSpaceProfileChanged'));
});

test('the per-space search engine and hibernate rows are gone', () => {
  // They belong to the profile now. A row left behind would edit a field
  // nothing reads and the user would watch it do nothing.
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings(CORE_SETTINGS);
  window.atriumSetSpaces(PROFILE_DOC);
  root.querySelector('.settings-rail-item[data-pane="spaces"]').click();

  const labels = [...root.querySelectorAll('.settings-space-detail .settings-row-label')]
      .map((n) => n.textContent);
  assert.ok(!labels.includes('Search engine'));
  assert.ok(!labels.includes('Hibernate background tabs'));
  assert.ok(!labels.includes('Hibernation timeout'));
  // Auto-archive stays: it is still a per-space setting.
  assert.ok(labels.some((l) => l.startsWith('Auto-archive')));
});

test('the retired per-space names are unregistered', () => {
  // A name left in kSurfaceMessages with no sender and no handler is a route
  // nobody maintains and a grep result that lies.
  assert.equal(REGISTERED.has('settingsSpaceSearchEngineChanged'), false);
  assert.equal(REGISTERED.has('settingsSpaceHibernateChanged'), false);
});

test('settingsSpaceRenamed is registered - an unregistered name is FATAL in the browser', () => {
  assert.ok(REGISTERED.has('settingsSpaceRenamed'));
});

test('renaming a space sends the trimmed name and repaints the list', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSpaces({spaces: [
    {id: 'a', name: 'Work', profileID: 'Default', tabs: [], pinnedTabs: []},
  ]});
  root.querySelector('.settings-rail-item[data-pane="spaces"]').click();
  const nameInput = root.querySelector('.settings-space-name');
  nameInput.value = '  Personal  ';
  nameInput.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsSpaceRenamed');
  assert.deepEqual(sent[1], [0, 'Personal']);
  assert.ok(
      [...root.querySelectorAll('.settings-space-item span')]
          .some((n) => n.textContent === 'Personal'),
      'the list repaints with the new name rather than waiting for a push');
});

test('an empty rename is dropped rather than sent', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSpaces({spaces: [
    {id: 'a', name: 'Work', profileID: 'Default', tabs: [], pinnedTabs: []},
  ]});
  root.querySelector('.settings-rail-item[data-pane="spaces"]').click();
  const nameInput = root.querySelector('.settings-space-name');
  nameInput.value = '   ';
  nameInput.dispatchEvent(new Event('change', {bubbles: true}));

  assert.equal(
      h.sent.find(([name]) => name === 'settingsSpaceRenamed'), undefined);
});

test('settingsSpaceCreated is registered - an unregistered name is FATAL in the browser', () => {
  assert.ok(REGISTERED.has('settingsSpaceCreated'));
});

test('clicking New Space sends settingsSpaceCreated', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSpaces({spaces: [
    {id: 'a', name: 'Work', profileID: 'Default', tabs: [], pinnedTabs: []},
  ]});
  root.querySelector('.settings-rail-item[data-pane="spaces"]').click();
  root.querySelector('.settings-space-new').click();

  const sent = h.sent.find(([name]) => name === 'settingsSpaceCreated');
  assert.ok(sent, 'settingsSpaceCreated is sent');
});

test('New Space, then its repush, selects the space this window asked for', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSpaces({spaces: [
    {id: 'a', name: 'Work', profileID: 'Default', tabs: [], pinnedTabs: []},
  ]});
  root.querySelector('.settings-rail-item[data-pane="spaces"]').click();
  root.querySelector('.settings-space-new').click();
  window.atriumSetSpaces({spaces: [
    {id: 'a', name: 'Work', profileID: 'Default', tabs: [], pinnedTabs: []},
    {id: 'b', name: 'New Space', profileID: 'Default', tabs: [], pinnedTabs: []},
  ]});
  assert.equal(root.querySelector('.settings-space-name').value, 'New Space');
});

// A space created from another window must not jump this window's
// detail panel or discard whatever it has open.
test('a repush grown by another window does not move the selection', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSpaces({spaces: [
    {id: 'a', name: 'Work', profileID: 'Default', tabs: [], pinnedTabs: []},
  ]});
  root.querySelector('.settings-rail-item[data-pane="spaces"]').click();
  window.atriumSetSpaces({spaces: [
    {id: 'a', name: 'Work', profileID: 'Default', tabs: [], pinnedTabs: []},
    {id: 'b', name: 'New Space', profileID: 'Default', tabs: [], pinnedTabs: []},
  ]});
  assert.equal(root.querySelector('.settings-space-name').value, 'Work');
});

test('settingsSpaceOpened is registered - an unregistered name is FATAL in the browser', () => {
  assert.ok(REGISTERED.has('settingsSpaceOpened'));
});

test('clicking Open sends settingsSpaceOpened with the selected index', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSpaces({spaces: [
    {id: 'a', name: 'Work', profileID: 'Default', tabs: [], pinnedTabs: []},
    {id: 'b', name: 'Home', profileID: 'Second', tabs: [], pinnedTabs: []},
  ]});
  root.querySelector('.settings-rail-item[data-pane="spaces"]').click();
  root.querySelector('.settings-space-item[data-space="1"]').click();
  root.querySelector('.settings-space-open').click();

  const sent = h.sent.find(([name]) => name === 'settingsSpaceOpened');
  assert.deepEqual(sent[1], [1]);
});

// the Profiles pane.

const PROFILE_DOC = {
  spaces: [
    {id: 'a', name: 'Work', profileID: 'Default', tabs: [], pinnedTabs: []},
    {id: 'b', name: 'Side', profileID: 'Default', tabs: [], pinnedTabs: []},
    {id: 'c', name: 'Home', profileID: 'Second', tabs: [], pinnedTabs: []},
  ],
  profiles: [
    {id: 'Default', name: 'Work', spaceCount: 2, available: true,
     suggestEnabled: true, downloadDir: '/Users/me/Downloads',
     searchEngine: 'kagi'},
    {id: 'Second', name: 'Home', spaceCount: 1, available: true,
     suggestEnabled: false, downloadDir: '/Users/me/Second'},
    {id: 'Spare', name: 'Spare', spaceCount: 0, available: false},
  ],
};

function profilesRoot() {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings(CORE_SETTINGS);
  window.atriumSetSpaces(PROFILE_DOC);
  root.querySelector('.settings-rail-item[data-pane="profiles"]').click();
  // selectedProfile is module state that outlives this call - a prior test's
  // click on a different row otherwise leaks into this one's default.
  root.querySelector('.settings-profile-item[data-profile="0"]').click();
  h.sent.length = 0;
  return root;
}

// PROFILE_DOC's settings carry no searchEngines, so its engine select holds
// nothing a pick can land on - these two give it two real options.
function engineProfileDoc(defaultEngine) {
  return {
    ...PROFILE_DOC,
    profiles: PROFILE_DOC.profiles.map(
        (p) => p.id === 'Default' ? {...p, searchEngine: defaultEngine} : p),
  };
}

function engineProfilesRoot() {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({
    ...CORE_SETTINGS,
    searchEngines: [
      {id: 'kagi', name: 'Kagi', enabled: true},
      {id: 'ddg', name: 'DuckDuckGo', enabled: true},
    ],
  });
  window.atriumSetSpaces(engineProfileDoc('kagi'));
  root.querySelector('.settings-rail-item[data-pane="profiles"]').click();
  root.querySelector('.settings-profile-item[data-profile="0"]').click();
  h.sent.length = 0;
  return root;
}

test('the rail carries a Profiles pane', () => {
  assert.ok(PANES.some((p) => p.id === 'profiles'));
  assert.equal(isKnownPane('profiles'), true);
});

test('the Profiles pane lists every profile with its space count', () => {
  const root = profilesRoot();
  const items = [...root.querySelectorAll('.settings-profile-item')]
      .filter((n) => !n.classList.contains('settings-profile-new'));
  assert.equal(items.length, 3);
  const counts = items.map(
      (n) => n.querySelector('.settings-profile-count').textContent);
  assert.deepEqual(counts, ['2 spaces', '1 space', 'Not used']);
});

test('a profile with no spaces reads Not used, not zero', () => {
  // Zero reads as a number the user has to interpret - the words say it.
  assert.equal(profileCountLabel({spaceCount: 0}), 'Not used');
  assert.equal(profileCountLabel({spaceCount: 1}), '1 space');
  assert.equal(profileCountLabel({spaceCount: 4}), '4 spaces');
});

test('clicking a profile switches the detail panel', () => {
  const root = profilesRoot();
  root.querySelector('.settings-profile-item[data-profile="1"]').click();
  assert.equal(root.querySelector('.settings-profile-name').value, 'Home');
});

// Same gap as the space name field - no PANE_ROWS key, still on the
// full-rebuild path.
test('an unrelated push keeps a mid-keystroke profile name and its caret', () => {
  const root = profilesRoot();
  const input = root.querySelector('.settings-profile-name');
  input.focus();
  input.value = 'New na';
  input.selectionStart = 6;
  input.selectionEnd = 6;

  window.atriumSetSpaces(PROFILE_DOC);

  const after = root.querySelector('.settings-profile-name');
  assert.equal(after.value, 'New na');
  assert.equal(after.selectionStart, 6);
  assert.equal(document.activeElement, after);
});

test('every profile message is registered - an unregistered name is FATAL in the browser', () => {
  for (const name of ['settingsProfileAdded', 'settingsProfileRenamed',
                      'settingsProfileDeleted', 'settingsProfileSearchEngineChanged',
                      'settingsProfileHibernateChanged', 'settingsProfileSuggestChanged',
                      'settingsProfileDownloadDirPick', 'settingsProfileOpenPage']) {
    assert.ok(REGISTERED.has(name), name);
  }
});

test('Add sends settingsProfileAdded', () => {
  const root = profilesRoot();
  root.querySelector('.settings-profile-new').click();
  assert.ok(h.sent.find(([name]) => name === 'settingsProfileAdded'));
});

test('Add, then its repush, selects the profile this window asked for', () => {
  const root = profilesRoot();
  root.querySelector('.settings-profile-new').click();
  window.atriumSetSpaces({
    ...PROFILE_DOC,
    profiles: [...PROFILE_DOC.profiles,
               {id: 'Fourth', name: 'Fourth', spaceCount: 0, available: true}],
  });
  assert.equal(root.querySelector('.settings-profile-name').value, 'Fourth');
});

// A profile added from another window must not move this window's
// selection.
test('a profile list grown by another window does not move the selection', () => {
  const root = profilesRoot();
  window.atriumSetSpaces({
    ...PROFILE_DOC,
    profiles: [...PROFILE_DOC.profiles,
               {id: 'Fourth', name: 'Fourth', spaceCount: 0, available: true}],
  });
  assert.equal(root.querySelector('.settings-profile-name').value, 'Work');
});

test('renaming a profile sends its id, never its position', () => {
  const root = profilesRoot();
  root.querySelector('.settings-profile-item[data-profile="1"]').click();
  const input = root.querySelector('.settings-profile-name');
  input.value = 'Personal';
  input.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsProfileRenamed');
  assert.ok(sent);
  assert.deepEqual(sent[1], ['Second', 'Personal']);
});

test('an emptied rename sends nothing - a profile row always needs a name', () => {
  const root = profilesRoot();
  const input = root.querySelector('.settings-profile-name');
  input.value = '   ';
  input.dispatchEvent(new Event('change', {bubbles: true}));
  assert.equal(h.sent.filter(([n]) => n === 'settingsProfileRenamed').length, 0);
});

test('Delete is disabled while a space uses the profile, and says why', () => {
  const root = profilesRoot();
  const button = root.querySelector('.settings-profile-delete');
  assert.equal(button.disabled, true);
  assert.match(
      root.querySelector('.settings-profile-delete-hint').textContent,
      /space/i);
});

test('Delete is live for a profile with no spaces and sends its id', () => {
  const root = profilesRoot();
  root.querySelector('.settings-profile-item[data-profile="2"]').click();
  const button = root.querySelector('.settings-profile-delete');
  assert.equal(button.disabled, false);
  button.click();
  const sent = h.sent.find(([name]) => name === 'settingsProfileDeleted');
  assert.deepEqual(sent[1], ['Spare']);
});

test('the search engine row shows the profile\'s own engine and sends its id', () => {
  const root = profilesRoot();
  const select = root.querySelector('.settings-profile-engine');
  assert.equal(select.value, 'kagi');
  select.value = 'google';
  select.dispatchEvent(new Event('change', {bubbles: true}));
  const sent = h.sent.find(([n]) => n === 'settingsProfileSearchEngineChanged');
  assert.deepEqual(sent[1], ['Default', 'google']);
});

test('the hibernate rows send the profile id, the mode and the threshold together', () => {
  const root = profilesRoot();
  const mode = root.querySelector('.settings-profile-hibernate-mode');
  mode.value = 'always';
  mode.dispatchEvent(new Event('change', {bubbles: true}));
  const sent = h.sent.find(([n]) => n === 'settingsProfileHibernateChanged');
  assert.deepEqual(sent[1], ['Default', 'always', '']);
});

test('the suggestions toggle reflects the pushed pref and sends the flip', () => {
  const root = profilesRoot();
  const toggle = root.querySelector('.settings-profile-suggest');
  assert.equal(toggle.getAttribute('aria-pressed'), 'true');
  toggle.click();
  const sent = h.sent.find(([n]) => n === 'settingsProfileSuggestChanged');
  assert.deepEqual(sent[1], ['Default', false]);
});

test('a profile whose Chromium profile did not load reads Unavailable', () => {
  const root = profilesRoot();
  root.querySelector('.settings-profile-item[data-profile="2"]').click();
  const values = [...root.querySelectorAll('.settings-profile-detail .settings-row-value')]
      .map((n) => n.textContent);
  assert.ok(values.includes('Unavailable'));
});

test('the download row shows the directory and asks for the picker', () => {
  const root = profilesRoot();
  assert.equal(
      root.querySelector('.settings-profile-download').textContent,
      '/Users/me/Downloads');
  root.querySelector('.settings-profile-download').click();
  const sent = h.sent.find(([n]) => n === 'settingsProfileDownloadDirPick');
  assert.deepEqual(sent[1], ['Default']);
});

test('each data link sends its own page name with the selected id', () => {
  const root = profilesRoot();
  for (const page of PROFILE_PAGES) {
    h.sent.length = 0;
    root.querySelector(`.settings-profile-page[data-page="${page.key}"]`).click();
    const sent = h.sent.find(([n]) => n === 'settingsProfileOpenPage');
    assert.deepEqual(sent[1], ['Default', page.key],
                     `${page.key} sends its own name`);
  }
});

test('the page never sends a URL, only an enum name', () => {
  // The whole reason the browser holds the map: a renderer that could name
  // the destination could name any of them.
  for (const page of PROFILE_PAGES) {
    assert.doesNotMatch(page.key, /:/, `${page.key} is a name, not a URL`);
  }
});

function hintTexts(root, selector) {
  return [...root.querySelectorAll(selector)].map((n) => n.textContent);
}

test('a data link the browser could not open says so on the selected profile', () => {
  const root = profilesRoot();
  root.querySelector('.settings-profile-page[data-page="passwords"]').click();
  assert.deepEqual(hintTexts(root, '.settings-profile-pages-hint'), []);

  window.atriumSettingsProfilePageRefused('Default');
  assert.deepEqual(hintTexts(root, '.settings-profile-pages-hint'),
                   ['Atrium could not open a window for this profile']);

  // A refusal that outlived its click would accuse the next one.
  root.querySelector('.settings-profile-page[data-page="passwords"]').click();
  assert.deepEqual(hintTexts(root, '.settings-profile-pages-hint'), []);
});

test('an engine pick the browser could not apply says so on the selected profile', () => {
  const root = profilesRoot();
  const select = root.querySelector('.settings-profile-engine');
  select.value = 'google';
  select.dispatchEvent(new Event('change', {bubbles: true}));
  assert.deepEqual(hintTexts(root, '.settings-profile-engine-hint'), []);

  window.atriumSetProfileEngineApplyFailed('Default');
  assert.deepEqual(
      hintTexts(root, '.settings-profile-engine-hint'),
      ['Saved, but could not switch this profile\'s searches yet']);
});

test('the select shows the picked engine once the browser pushes the save', () => {
  const root = engineProfilesRoot();
  const pick = (id) => {
    const select = root.querySelector('.settings-profile-engine');
    select.value = id;
    select.dispatchEvent(new Event('change', {bubbles: true}));
  };

  pick('ddg');
  window.atriumSetProfileEngineApplyFailed('Default');
  assert.deepEqual(
      hintTexts(root, '.settings-profile-engine-hint'),
      ['Saved, but could not switch this profile\'s searches yet']);

  // The select is rebuilt from the last pushed document, so a pick the
  // browser never pushes back leaves the row reading the old engine.
  h.sent.length = 0;
  pick('ddg');
  assert.deepEqual(hintTexts(root, '.settings-profile-engine-hint'), []);
  assert.deepEqual(
      h.sent.find(([name]) => name === 'settingsProfileSearchEngineChanged')[1],
      ['Default', 'ddg']);

  window.atriumSetSpaces(engineProfileDoc('ddg'));
  assert.equal(root.querySelector('.settings-profile-engine').value, 'ddg');
  assert.deepEqual(hintTexts(root, '.settings-profile-engine-hint'), []);
});

test('an unloaded profile says why its data links are dead', () => {
  // Six disabled buttons and nothing else is the silent failure to avoid.
  const root = profilesRoot();
  root.querySelector('.settings-profile-item[data-profile="2"]').click();
  assert.deepEqual(hintTexts(root, '.settings-profile-pages-hint'),
                   ['This profile is not loaded, so its pages cannot open']);
});

// the write half. Warn before quitting is the smallest round
// trip there is - a plain bool - and it is deliberately the only editable row
// until it has been proved end to end.

test('toggling Warn before quitting sends the WHOLE object with that key flipped', () => {
  const root = loadedRoot();
  warnToggle(root).click();

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.confirmToQuitEnabled, true, 'the flip travels');
  assert.deepEqual(
      {...payload, confirmToQuitEnabled: false}, CORE_SETTINGS,
      'the whole object travels - the core refuses a partial one rather than ' +
          'completing it from defaults');
});

test('the row repaints on the click rather than waiting for the browser', () => {
  const root = loadedRoot();
  assert.equal(warnValue(root), 'Off');
  warnToggle(root).click();
  assert.equal(warnValue(root), 'On');
});

test('a row the browser has not pushed a value for is not togglable', () => {
  // Sending would mean sending a partial object, which the core refuses. A row
  // showing a dash has nothing to flip.
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({});  // the module keeps the last push; clear it
  assert.equal(warnToggle(root), null);
  assert.equal(
      isToggleable(GENERAL_ROWS.find((r) => r.key === 'confirmToQuitEnabled'),
                   {}),
      false);
});

// opaqueSurround's own pass. The row must be a live toggle.

test('opaqueSurround renders a toggle, not a dead span', () => {
  const root = loadedRoot();
  root.querySelector('.settings-rail-item[data-pane="appearance"]').click();
  assert.ok(opaqueSurroundToggle(root), 'opaqueSurround renders a toggle');
});

test('toggling Native app view sends the WHOLE object with that key flipped', () => {
  const root = loadedRoot();
  root.querySelector('.settings-rail-item[data-pane="appearance"]').click();
  opaqueSurroundToggle(root).click();

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.opaqueSurround, false, 'the flip travels');
  assert.deepEqual(
      {...payload, opaqueSurround: true}, CORE_SETTINGS,
      'the whole object travels, the same contract every other toggle has');
});

// the page tint's off switch.
test('Page tint renders a toggle in Appearance', () => {
  const root = loadedRoot();
  root.querySelector('.settings-rail-item[data-pane="appearance"]').click();
  assert.ok(pageTintToggle(root), 'chromePageTint renders a toggle');
});

test('the page tint is off until asked for', () => {
  // It repaints the window on every navigation. A user who has not chosen it
  // should not meet it first.
  const root = loadedRoot();
  root.querySelector('.settings-rail-item[data-pane="appearance"]').click();
  assert.equal(pageTintToggle(root).classList.contains('on'), false);
});

test('turning the page tint on sends the whole object with that key flipped', () => {
  const root = loadedRoot();
  root.querySelector('.settings-rail-item[data-pane="appearance"]').click();
  pageTintToggle(root).click();

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.chromePageTint, true, 'the flip travels');
  assert.deepEqual({...payload, chromePageTint: false}, CORE_SETTINGS,
                   'the whole object travels, as every other toggle does');
});

test('a declined save reaches the USER, not only the log', () => {
  // -2 means settings.json is at a schema this build cannot read. There is
  // nothing to retry, and a settings edit that silently does nothing is worse
  // than one that fails loudly.
  const root = loadedRoot();
  warnToggle(root).click();
  window.atriumSetSaveResult(-2, lastSettingsSaveId());

  const alert = root.querySelector('.settings-alert');
  assert.ok(alert, 'the surface says the edit did not save');
  assert.match(alert.textContent, /newer version/);
});

test('a refused save puts the row back rather than leaving it showing an edit', () => {
  const root = loadedRoot();
  warnToggle(root).click();
  assert.equal(warnValue(root), 'On');
  window.atriumSetSaveResult(-2, lastSettingsSaveId());
  assert.equal(warnValue(root), 'Off', 'the row agrees with the file again');
});

test('each failing code says its own thing', () => {
  const message = (code) => {
    const root = loadedRoot();
    warnToggle(root).click();
    window.atriumSetSaveResult(code, lastSettingsSaveId());
    return root.querySelector('.settings-alert').textContent;
  };
  assert.match(message(-1), /quit/, 'the edit is in the surface only');
  assert.notEqual(message(0), message(-1));
  assert.notEqual(message(-2), message(-1));
  // A code from a later core still says something rather than nothing.
  assert.ok(message(-99).length > 0);
});

test('a save that worked clears the warning and keeps the new value', () => {
  const root = loadedRoot();
  warnToggle(root).click();
  window.atriumSetSaveResult(-2, lastSettingsSaveId());
  warnToggle(root).click();
  window.atriumSetSaveResult(1, lastSettingsSaveId());

  assert.equal(root.querySelector('.settings-alert'), null);
  assert.equal(warnValue(root), 'On');
});

// a full rebuild shows the right value too, so only node identity
// (not a value check) tells it apart from the narrow update meant to replace it.

test('editing a row does not replace the rail or the heading', () => {
  const root = loadedRoot();
  const rail = root.querySelector('.settings-rail');
  const heading = root.querySelector('.settings-heading');
  warnToggle(root).click();
  assert.equal(root.querySelector('.settings-rail'), rail);
  assert.equal(root.querySelector('.settings-heading'), heading);
});

test('editing a row does not replace a sibling row', () => {
  const root = loadedRoot();
  const sibling =
      root.querySelector('.settings-row[data-key="externalLinksOpenInLittle"]');
  warnToggle(root).click();
  assert.equal(
      root.querySelector('.settings-row[data-key="externalLinksOpenInLittle"]'),
      sibling);
});

test('a toggle keeps focus on its replacement row', () => {
  const root = loadedRoot();
  const toggle = warnToggle(root);
  toggle.focus();
  toggle.click();
  assert.notEqual(document.activeElement, toggle,
                   'the old node was replaced');
  assert.equal(document.activeElement, warnToggle(root),
               'focus followed the edit to its replacement');
});

test('a successful save reply repaints nothing further', () => {
  const root = loadedRoot();
  const rail = root.querySelector('.settings-rail');
  const heading = root.querySelector('.settings-heading');
  const sibling =
      root.querySelector('.settings-row[data-key="externalLinksOpenInLittle"]');
  warnToggle(root).click();
  const editedRow =
      root.querySelector('.settings-row[data-key="confirmToQuitEnabled"]');
  window.atriumSetSaveResult(1, lastSettingsSaveId());
  assert.equal(root.querySelector('.settings-rail'), rail);
  assert.equal(root.querySelector('.settings-heading'), heading);
  assert.equal(
      root.querySelector('.settings-row[data-key="externalLinksOpenInLittle"]'),
      sibling);
  assert.equal(
      root.querySelector('.settings-row[data-key="confirmToQuitEnabled"]'),
      editedRow, 'the row the edit already painted is left alone');
});

test('a refused save reverts only the edited row', () => {
  const root = loadedRoot();
  const rail = root.querySelector('.settings-rail');
  const sibling =
      root.querySelector('.settings-row[data-key="externalLinksOpenInLittle"]');
  warnToggle(root).click();
  window.atriumSetSaveResult(-2, lastSettingsSaveId());
  assert.equal(root.querySelector('.settings-rail'), rail);
  assert.equal(
      root.querySelector('.settings-row[data-key="externalLinksOpenInLittle"]'),
      sibling);
  assert.equal(warnValue(root), 'Off', 'the row itself is back to its saved value');
});

test('a failed save reverts only its own edit, leaving a later one in flight in place', () => {
  const root = loadedRoot();
  warnToggle(root).click();
  const idA = lastSettingsSaveId();
  littleToggle(root).click();
  const idB = lastSettingsSaveId();
  assert.notEqual(idA, idB, 'each save gets its own id');

  // idA's reply arrives after idB was already sent - the failure this whole
  // path exists to make visible, without stomping idB's still-live edit.
  window.atriumSetSaveResult(-2, idA);

  assert.equal(warnValue(root), 'Off', 'the failed edit reverts');
  assert.equal(littleToggle(root).classList.contains('on'), false,
      'the edit sent after the failed one stays as the user left it');

  // The next save resends the whole object - it must carry the reverted
  // value and the kept edit, not the version that predates either.
  memorySaverToggle(root).click();
  const next = lastSettingsSent();
  assert.equal(next.confirmToQuitEnabled, false, 'stays reverted on disk too');
  assert.equal(next.externalLinksOpenInLittle, false,
      'the kept edit is not overwritten by the stale reverted state');
});

test('the failed-save banner appears without touching the rail or a row', () => {
  const root = loadedRoot();
  const rail = root.querySelector('.settings-rail');
  const sibling =
      root.querySelector('.settings-row[data-key="externalLinksOpenInLittle"]');
  warnToggle(root).click();
  window.atriumSetSaveResult(-2, lastSettingsSaveId());
  assert.ok(root.querySelector('.settings-alert'));
  assert.equal(root.querySelector('.settings-rail'), rail);
  assert.equal(
      root.querySelector('.settings-row[data-key="externalLinksOpenInLittle"]'),
      sibling);
});

test('a rejected endpoint URL updates an existing banner rather than replacing it', () => {
  // The credential rejection never clears saveError first, so it has to find
  // and reuse whatever banner is already showing.
  const root = aiRoot('customEndpoint');
  aiEnabledToggle(root).click();
  window.atriumSetSaveResult(-2, lastSettingsSaveId());
  const alert = root.querySelector('.settings-alert');
  assert.ok(alert, 'a failure is already showing');

  const input = customEndpointUrlInput(root);
  input.value = 'https://res.example.azure.com/v1?api-key=SECRET';
  input.dispatchEvent(new Event('change', {bubbles: true}));

  assert.equal(root.querySelector('.settings-alert'), alert);
  assert.match(root.querySelector('.settings-alert').textContent, /API key/);
});

test('a save that finally works removes the banner without touching the rail', () => {
  const root = loadedRoot();
  const rail = root.querySelector('.settings-rail');
  warnToggle(root).click();
  window.atriumSetSaveResult(-2, lastSettingsSaveId());
  warnToggle(root).click();
  window.atriumSetSaveResult(1, lastSettingsSaveId());
  assert.equal(root.querySelector('.settings-alert'), null);
  assert.equal(root.querySelector('.settings-rail'), rail);
});

test('a credential-bearing endpoint URL is rejected without touching the rail', () => {
  const root = aiRoot('customEndpoint');
  const rail = root.querySelector('.settings-rail');
  const input = customEndpointUrlInput(root);
  input.value = 'https://res.example.azure.com/v1?api-key=SECRET';
  input.dispatchEvent(new Event('change', {bubbles: true}));
  assert.equal(root.querySelector('.settings-rail'), rail);
  assert.equal(customEndpointUrlInput(root).value, '',
               'the rejected text is not left showing');
});

test('the always-on-top push updates just the pin', () => {
  const root = loadedRoot();
  const rail = root.querySelector('.settings-rail');
  const pin = root.querySelector('.settings-pin');
  window.atriumSetSettingsAlwaysOnTop(true);
  assert.equal(root.querySelector('.settings-rail'), rail);
  assert.equal(root.querySelector('.settings-pin'), pin,
               'the pin itself is not rebuilt either');
  assert.equal(pin.getAttribute('aria-pressed'), 'true');
  // alwaysOnTop is module state, not per-root - restore it for every test
  // below that assumes a fresh, unpinned window.
  window.atriumSetSettingsAlwaysOnTop(false);
});

test('switching AI model still rebuilds the pane, not just its own row', () => {
  // aiModel can add or remove sibling rows (Ollama vs Custom endpoint) -
  // buildRowNode's row.special check keeps it off the narrow path used
  // for everything else.
  const root = aiRoot('onDevice');
  const rail = root.querySelector('.settings-rail');
  selectAiModel(root, 'ollama');
  assert.notEqual(root.querySelector('.settings-rail'), rail);
  assert.ok(ollamaHostInput(root), 'and the ollama rows are there to prove it ran');
});

test('switching AI off drops the connection row, not just its own', () => {
  const root = aiRoot('ollama');
  assert.ok(root.querySelector('.settings-row[data-key="ollamaConnection"]'),
            'the connection row is up before the edit');
  aiEnabledToggle(root).click();
  assert.equal(
      root.querySelector('.settings-row[data-key="ollamaConnection"]'), null,
      'aiEnabled is structural too, the same as aiModel above');
});

// The External links row. The routing has to exist first - a toggle that
// persists and routes nothing is a shape that shipped twice already.

test('the External links row moves the key the openURLs hook reads', () => {
  // Read out of the C++ rather than typed here. The write path and the seeded
  // read are different code, so a row naming a key nothing reads would look
  // exactly like a working one on screen.
  const hook = fs.readFileSync(
      path.join(SRC, 'chrome/browser/ui/atrium/atrium_external_link.cc'),
      'utf8');
  const key = hook.match(/FindBool\("([^"]+)"\)/)[1];
  const row = GENERAL_ROWS.find((r) => r.key === key);
  assert.ok(row, `General carries a row keyed ${key}`);
  assert.equal(row.toggle, true, 'it is a bool, so it is a toggle');
});

test('toggling External links sends the WHOLE object with that key flipped', () => {
  const root = loadedRoot();
  littleToggle(root).click();

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.externalLinksOpenInLittle, false, 'the flip travels');
  assert.deepEqual(
      {...payload, externalLinksOpenInLittle: true}, CORE_SETTINGS,
      'the whole object travels, the same contract the other toggle has');
});

test('autoMiniPlayer is the last choice/toggle/number control a keyboard reaches in General', () => {
  // Pinned so a row added after it is a deliberate choice, caught on the
  // host instead of in a six-minute VM run.
  const root = loadedRoot();
  const controls = [...root.querySelectorAll(
      '.settings-row-toggle, .settings-row-choice, .settings-row-number')];
  assert.equal(controls.at(-1).closest('.settings-row').dataset.key,
               'autoMiniPlayer');
});

// Hibernate background tabs was bound to memorySaverThreshold (a
// string), so isToggleable never matched and the row was a dead span.

test('the memory saver toggle sends the WHOLE object with that key flipped', () => {
  const root = loadedRoot();
  memorySaverToggle(root).click();

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.memorySaverEnabled, true, 'the flip travels');
  assert.deepEqual(
      {...payload, memorySaverEnabled: false}, CORE_SETTINGS,
      'the whole object travels, the same contract the other toggles have');
});

test('the memory saver threshold is a choice row, not a dead span', () => {
  const root = loadedRoot();
  const select = memorySaverThresholdSelect(root);
  assert.ok(select, 'memorySaverThreshold renders a select');
  assert.equal(select.value, 'twoHours');
});

test('changing the threshold sends the WHOLE object with the new rung', () => {
  const root = loadedRoot();
  const select = memorySaverThresholdSelect(root);
  select.value = 'fourHours';
  select.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.memorySaverThreshold, 'fourHours');
  assert.deepEqual(
      {...payload, memorySaverThreshold: 'twoHours'}, CORE_SETTINGS);
});

test('the keep-alive rows sit in the Performance card after hibernation', () => {
  const root = loadedRoot();
  // The title renders as the card's previous sibling, not a descendant
  // (renderGroupedRows appends title then card to the same parent).
  const title = [...root.querySelectorAll('.settings-group-title')].find(
      (t) => t.textContent === 'Performance');
  const card = title && title.nextElementSibling;
  assert.ok(card, 'General has a Performance card');
  assert.deepEqual(
      [...card.querySelectorAll('.settings-row')].map((r) => r.dataset.key),
      ['memorySaverEnabled', 'memorySaverThreshold',
       'favoriteKeepAliveEnabled', 'favoriteKeepAliveThreshold']);
  const labels = [...card.querySelectorAll('.settings-row')].map(
      (r) => r.querySelector('.settings-row-label')?.textContent);
  assert.ok(labels.some((l) => l.startsWith('Keep closed favourites loaded')));
  assert.ok(labels.some((l) => l.startsWith('Keep-alive time')));
});

test('the keep-alive toggle sends the WHOLE object with that key flipped', () => {
  const root = loadedRoot();
  keepAliveToggle(root).click();

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.favoriteKeepAliveEnabled, false, 'the flip travels');
  assert.deepEqual({...payload, favoriteKeepAliveEnabled: true}, CORE_SETTINGS);
});

test('the keep-alive time offers the five rungs with readable labels', () => {
  const root = loadedRoot();
  const select = keepAliveThresholdSelect(root);
  assert.ok(select, 'favoriteKeepAliveThreshold renders a select');
  assert.equal(select.value, 'fiveMinutes');
  assert.deepEqual(
      [...select.querySelectorAll('option')].map((o) => o.textContent),
      ['1 minute', '5 minutes', '15 minutes', '30 minutes', '1 hour']);
});

test('changing the keep-alive time sends the WHOLE object with the new rung', () => {
  const root = loadedRoot();
  const select = keepAliveThresholdSelect(root);
  select.value = 'oneHour';
  select.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.favoriteKeepAliveThreshold, 'oneHour');
  assert.deepEqual(
      {...payload, favoriteKeepAliveThreshold: 'fiveMinutes'}, CORE_SETTINGS);
});

// the Notification timeout row, previously key: null.

test('the notification timeout row is a choice row, not a dead span', () => {
  const root = loadedRoot();
  const select = notificationTimeoutSelect(root);
  assert.ok(select, 'notificationTimeout renders a select');
  assert.equal(select.value, 'fiveSeconds');
});

test('changing the notification timeout sends the WHOLE object with the new step', () => {
  const root = loadedRoot();
  const select = notificationTimeoutSelect(root);
  select.value = 'tenSeconds';
  select.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.notificationTimeout, 'tenSeconds');
  assert.deepEqual(
      {...payload, notificationTimeout: 'fiveSeconds'}, CORE_SETTINGS,
      'the whole object travels - the same contract every other choice row has');
});

test('a fresh push after a restart repaints the row with the saved step', () => {
  // A restart is a fresh installSettings() plus the browser's first push of
  // what atrium_core_query read off disk - the round trip every other row uses.
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({...CORE_SETTINGS, notificationTimeout: 'tenSeconds'});
  assert.equal(notificationTimeoutSelect(root).value, 'tenSeconds');
});

test('a profile that stored off or thirtySeconds opens on the step it migrated to', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({...CORE_SETTINGS, notificationTimeout: 'off'});
  assert.equal(notificationTimeoutSelect(root).value, 'oneSecond');

  const root2 = freshRoot();
  installSettings(root2);
  window.atriumSetSettings({...CORE_SETTINGS, notificationTimeout: 'thirtySeconds'});
  assert.equal(notificationTimeoutSelect(root2).value, 'tenSeconds');
});

// startupBehavior is a segmented strip rather than a <select> - one of the
// two rows under three choices.

test('startupBehavior is a segmented choice, not a dead span', () => {
  const root = loadedRoot();
  const selected = segSelected(root, 'startupBehavior');
  assert.ok(selected, 'startupBehavior renders a segmented control');
  assert.equal(selected.dataset.choice, 'restoreLastActive');
});

test('changing startupBehavior sends the WHOLE object with the new choice', () => {
  const root = loadedRoot();
  segOption(root, 'startupBehavior', 'blankTab').click();

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.startupBehavior, 'blankTab');
  assert.deepEqual(
      {...payload, startupBehavior: 'restoreLastActive'}, CORE_SETTINGS,
      'the whole object travels - the same contract every other choice row has');
});

test('a fresh push after a restart repaints the row with the saved behavior', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({...CORE_SETTINGS, startupBehavior: 'blankTab'});
  assert.equal(segSelected(root, 'startupBehavior').dataset.choice, 'blankTab');
});

// autoMiniPlayer (a select, four choices) and
// controllerVisibility (segmented, two choices).

test('autoMiniPlayer renders as a select holding the pushed choice', () => {
  const root = loadedRoot();
  const select = root.querySelector(
      '.settings-row[data-key="autoMiniPlayer"] .settings-row-choice');
  assert.ok(select, 'autoMiniPlayer renders a select');
  assert.equal(select.value, 'audible');
});

test('changing autoMiniPlayer sends the WHOLE object with the new choice', () => {
  const root = loadedRoot();
  const select = root.querySelector(
      '.settings-row[data-key="autoMiniPlayer"] .settings-row-choice');
  select.value = 'anyVideo';
  select.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.autoMiniPlayer, 'anyVideo');
  assert.deepEqual(
      {...payload, autoMiniPlayer: 'audible'}, CORE_SETTINGS,
      'the whole object travels - the same contract every other choice row has');
});

test('swipeHaptics is a segmented Off, Crisp, Smooth choice on the inputs pane', () => {
  const root = inputsRoot();
  const selected = segSelected(root, 'swipeHaptics');
  assert.ok(selected, 'swipeHaptics renders a segmented control');
  assert.equal(selected.dataset.choice, 'crisp');
  assert.deepEqual(
      [...root.querySelectorAll(
          '.settings-row[data-key="swipeHaptics"] [data-choice]')]
          .map((b) => b.textContent),
      ['Off', 'Crisp', 'Smooth']);
});

test('changing swipeHaptics sends the WHOLE object with the new choice', () => {
  const root = inputsRoot();
  segOption(root, 'swipeHaptics', 'off').click();
  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.deepEqual(payload, {...CORE_SETTINGS, swipeHaptics: 'off'});
});

test('controllerVisibility is a segmented choice holding the pushed choice', () => {
  const root = loadedRoot();
  const selected = segSelected(root, 'controllerVisibility');
  assert.ok(selected, 'controllerVisibility renders a segmented control');
  assert.equal(selected.dataset.choice, 'background');
});

test('changing controllerVisibility sends the WHOLE object with the new choice', () => {
  const root = loadedRoot();
  segOption(root, 'controllerVisibility', 'always').click();

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.controllerVisibility, 'always');
  assert.deepEqual(
      {...payload, controllerVisibility: 'background'}, CORE_SETTINGS,
      'the whole object travels - the same contract every other choice row has');
});

test('a fresh push after a restart repaints both media rows with the saved choices', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings(
      {...CORE_SETTINGS, autoMiniPlayer: 'off', controllerVisibility: 'always'});
  assert.equal(
      root.querySelector('.settings-row[data-key="autoMiniPlayer"] .settings-row-choice')
          .value,
      'off');
  assert.equal(segSelected(root, 'controllerVisibility').dataset.choice, 'always');
});

// the regression shape - a control that reads the changed value
// right after the edit but resets on the NEXT rebuild, because something
// read a stale value instead of the module's own settings object.

test('startupBehavior keeps its edited value across a pane rebuild', () => {
  const root = loadedRoot();
  segOption(root, 'startupBehavior', 'blankTab').click();
  renderSettings(root, 'general');
  assert.equal(segSelected(root, 'startupBehavior').dataset.choice, 'blankTab');
});

test('opaqueSurround keeps its edited value across a pane rebuild', () => {
  const root = loadedRoot();
  root.querySelector('.settings-rail-item[data-pane="appearance"]').click();
  opaqueSurroundToggle(root).click();
  renderSettings(root, 'appearance');
  assert.equal(opaqueSurroundToggle(root).getAttribute('aria-pressed'), 'false');
});

// settings.js used to set option.textContent to the raw store
// key, so a choice row read as fifteenMinutes/threeSeconds/chromeRow instead
// of a human label.

test('every choice row keeps its raw identifier as the value, and no option reads as camelCase', () => {
  const root = loadedRoot();
  for (const paneId of ['general', 'appearance']) {
    renderSettings(root, paneId);
    for (const select of root.querySelectorAll('.settings-row-choice')) {
      const key = select.closest('.settings-row').dataset.key;
      const row = PANE_ROWS[paneId].find((r) => r.key === key);
      for (const option of select.querySelectorAll('option')) {
        assert.ok(row.choices.includes(option.value),
                   `${key} option value "${option.value}" is not one of its raw choices`);
        assert.doesNotMatch(
            option.textContent, /[a-z][A-Z]/,
            `${key} option "${option.value}" reads "${option.textContent}"`);
      }
    }
  }
});

test('the choice rows match the lab\'s own wording', () => {
  const root = loadedRoot();
  assert.equal(segOption(root, 'sidebarPosition', 'left').textContent, 'Left');
  assert.equal(segOption(root, 'sidebarPosition', 'right').textContent, 'Right');
  const optionText = (key, value) => {
    const select = root.querySelector(
        `.settings-row[data-key="${key}"] .settings-row-choice`);
    return [...select.querySelectorAll('option')]
        .find((o) => o.value === value)
        .textContent;
  };
  assert.equal(optionText('notificationTimeout', 'oneSecond'), '1 s');
  assert.equal(optionText('notificationTimeout', 'threeSeconds'), '3 s');
  assert.equal(optionText('notificationTimeout', 'keep'), 'Keep');
});

// the Ollama host and port rows, previously the single
// key: null 'Ollama server' row.

test('the Ollama host row is text, not a dead span', () => {
  const root = aiRoot('ollama');
  const input = ollamaHostInput(root);
  assert.ok(input, 'ollamaHost renders a text input');
  assert.equal(input.value, 'localhost');
});

test('the Ollama port row is a number, not a dead span', () => {
  const root = aiRoot('ollama');
  const input = ollamaPortInput(root);
  assert.ok(input, 'ollamaPort renders a number input');
  assert.equal(input.value, '11434');
});

test('changing the host sends the WHOLE object with the new value', () => {
  const root = aiRoot('ollama');
  const input = ollamaHostInput(root);
  input.value = '192.168.1.50';
  input.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.ollamaHost, '192.168.1.50');
  // aiRoot('ollama') picks the model first (a real setRow write,
  // same as this edit), so that field differs from CORE_SETTINGS too.
  assert.deepEqual(
      {...payload, ollamaHost: 'localhost', aiModel: 'onDevice'},
      CORE_SETTINGS);
});

test('changing the port sends the WHOLE object with the new value', () => {
  const root = aiRoot('ollama');
  const input = ollamaPortInput(root);
  input.value = '11500';
  input.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.ollamaPort, 11500);
  assert.deepEqual(
      {...payload, ollamaPort: 11434, aiModel: 'onDevice'}, CORE_SETTINGS);
});

test('a fresh push after a restart repaints the host and port rows', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings(
      {...CORE_SETTINGS, ollamaHost: '10.0.0.5', ollamaPort: 12000});
  root.querySelector('.settings-rail-item[data-pane="ai"]').click();
  selectAiModel(root, 'ollama');
  assert.equal(ollamaHostInput(root).value, '10.0.0.5');
  assert.equal(ollamaPortInput(root).value, '12000');
});

test('the connection row label names the configured host and port', () => {
  const root = aiRoot('ollama');
  const row = root.querySelector('.settings-row[data-key="ollamaConnection"]');
  assert.ok(row, 'the connection row renders');
  assert.match(row.querySelector('.settings-row-label').firstChild.textContent,
               /^Ollama \(localhost:11434\)$/);
});

test('the connection row follows an edited host and port without a save', () => {
  const root = aiRoot('ollama');
  ollamaHostInput(root).value = '10.0.0.9';
  ollamaHostInput(root).dispatchEvent(new Event('change', {bubbles: true}));
  const row = root.querySelector('.settings-row[data-key="ollamaConnection"]');
  assert.match(row.querySelector('.settings-row-label').firstChild.textContent,
               /^Ollama \(10\.0\.0\.9:11434\)$/);
});

// the per-backend model name rows.

test('the Ollama model row is text, not a dead span', () => {
  const root = aiRoot('ollama');
  const input = ollamaModelInput(root);
  assert.ok(input, 'ollamaModel renders a text input');
  assert.equal(input.value, 'llama3');
});

test('changing the Ollama model sends the WHOLE object with the new value', () => {
  const root = aiRoot('ollama');
  const input = ollamaModelInput(root);
  input.value = 'llama3:70b-instruct';
  input.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.ollamaModel, 'llama3:70b-instruct');
  assert.deepEqual(
      {...payload, ollamaModel: 'llama3', aiModel: 'onDevice'}, CORE_SETTINGS);
});

test('a fresh push after a restart repaints the Ollama model row', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({...CORE_SETTINGS, ollamaModel: 'mistral'});
  root.querySelector('.settings-rail-item[data-pane="ai"]').click();
  selectAiModel(root, 'ollama');
  assert.equal(ollamaModelInput(root).value, 'mistral');
});

// The advanced rows. Both are empty for anyone who has not met a slow or an
// expensive endpoint, which is why the card they sit in starts closed.

test('the Advanced card is on screen for Custom endpoint, and starts closed', () => {
  const root = aiRoot('customEndpoint');
  const card = advancedCard(root);
  assert.ok(card, 'the Advanced card renders');
  assert.equal(card.tagName, 'DETAILS');
  // The attribute, not .open: this harness is a DOM stub with no details
  // behaviour, and an absent `open` is what makes the real element start shut.
  assert.equal(card.hasAttribute('open'), false, 'closed until the user opens it');
  assert.equal(card.querySelector('summary').textContent, 'Advanced');
});

// Closed is not hidden: a details keeps its children in the DOM, so the rows
// are reachable by search and by the keyboard without opening the card first.
test('both advanced rows render inside the card, at their off value', () => {
  const root = aiRoot('customEndpoint');
  assert.ok(maxTokensInput(root), 'the response length cap renders');
  assert.ok(timeoutInput(root), 'the request timeout renders');
  assert.equal(maxTokensInput(root).value, '0');
  assert.equal(timeoutInput(root).value, '0');
  assert.ok(advancedCard(root).contains(maxTokensInput(root)));
  assert.ok(advancedCard(root).contains(timeoutInput(root)));
});

test('changing the cap sends the WHOLE object with the new number', () => {
  const root = aiRoot('customEndpoint');
  const input = maxTokensInput(root);
  input.value = '512';
  input.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.customEndpointMaxTokens, 512);
  assert.deepEqual(
      {...payload, customEndpointMaxTokens: 0, aiModel: 'onDevice'},
      CORE_SETTINGS);
});

test('changing the timeout sends the WHOLE object with the new number', () => {
  const root = aiRoot('customEndpoint');
  const input = timeoutInput(root);
  input.value = '180';
  input.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.customEndpointTimeoutSeconds, 180);
  assert.deepEqual(
      {...payload, customEndpointTimeoutSeconds: 0, aiModel: 'onDevice'},
      CORE_SETTINGS);
});

test('a fresh push after a restart repaints both advanced rows', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({
    ...CORE_SETTINGS,
    customEndpointMaxTokens: 1024,
    customEndpointTimeoutSeconds: 240,
  });
  root.querySelector('.settings-rail-item[data-pane="ai"]').click();
  selectAiModel(root, 'customEndpoint');
  assert.equal(maxTokensInput(root).value, '1024');
  assert.equal(timeoutInput(root).value, '240');
});

// --- the page text cap ---

// The Advanced card is gated to customEndpoint, so a cap placed there
// would vanish for the two backends that also read it.
test('the page text cap row shows for every backend, not just customEndpoint', () => {
  for (const model of ['onDevice', 'ollama', 'customEndpoint']) {
    const root = aiRoot(model);
    const input = pageTextCapInput(root);
    assert.ok(input, `aiPageTextCap renders for ${model}`);
    assert.equal(input.value, '100000', `default shows for ${model}`);
    assert.ok(!advancedCard(root)?.contains(input),
              `aiPageTextCap is outside the Advanced card for ${model}`);
  }
});

test('changing the page text cap sends the WHOLE object with the new number', () => {
  const root = aiRoot('onDevice');
  const input = pageTextCapInput(root);
  input.value = '250000';
  input.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.aiPageTextCap, 250000);
  assert.deepEqual({...payload, aiPageTextCap: 100000}, CORE_SETTINGS);
});

test('a fresh push after a restart repaints the page text cap', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({...CORE_SETTINGS, aiPageTextCap: 40000});
  root.querySelector('.settings-rail-item[data-pane="ai"]').click();
  assert.equal(pageTextCapInput(root).value, '40000');
});

test('the Custom endpoint model row is text, not a dead span', () => {
  const root = aiRoot('customEndpoint');
  const input = customEndpointModelInput(root);
  assert.ok(input, 'customEndpointModel renders a text input');
  assert.equal(input.value, 'gpt-4o-mini');
});

test('changing the endpoint model sends the WHOLE object with the new value', () => {
  const root = aiRoot('customEndpoint');
  const input = customEndpointModelInput(root);
  input.value = 'claude-3-5-sonnet';
  input.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.customEndpointModel, 'claude-3-5-sonnet');
  assert.deepEqual(
      {...payload, customEndpointModel: 'gpt-4o-mini', aiModel: 'onDevice'},
      CORE_SETTINGS);
});

test('a fresh push after a restart repaints the Custom endpoint model row', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({...CORE_SETTINGS, customEndpointModel: 'gpt-4o'});
  root.querySelector('.settings-rail-item[data-pane="ai"]').click();
  selectAiModel(root, 'customEndpoint');
  assert.equal(customEndpointModelInput(root).value, 'gpt-4o');
});

// the custom endpoint URL and its keychain-backed API key.

test('the Endpoint row is text, not a dead span', () => {
  const root = aiRoot('customEndpoint');
  const input = customEndpointUrlInput(root);
  assert.ok(input, 'customEndpointUrl renders a text input');
  assert.equal(input.value, '');
});

test('changing the endpoint sends the WHOLE object with the new value', () => {
  const root = aiRoot('customEndpoint');
  const input = customEndpointUrlInput(root);
  input.value = 'https://api.example.com/v1/messages';
  input.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.customEndpointUrl, 'https://api.example.com/v1/messages');
  assert.deepEqual(
      {...payload, customEndpointUrl: '', aiModel: 'onDevice'}, CORE_SETTINGS);
});

// an Azure/Google-style endpoint URL carries its key in the
// query string, and there is no round trip back to this window after a save
// to say the core dropped it - so a credential-shaped URL must never reach
// `settingsChanged` at all.
test('a credential-shaped query param is refused before it is ever sent', () => {
  const root = aiRoot('customEndpoint');
  const input = customEndpointUrlInput(root);
  input.value = 'https://res.example.azure.com/v1?api-key=SECRET';
  input.dispatchEvent(new Event('change', {bubbles: true}));

  assert.equal(
      h.sent.some(([name]) => name === 'settingsChanged'), false,
      'a URL that would leak a key is never sent to the core');
  const alert = root.querySelector('.settings-alert');
  assert.ok(alert, 'the user is told, not left to wonder why nothing changed');
  assert.match(alert.textContent, /API key/);
});

test('an ordinary query param on the endpoint URL still saves', () => {
  const root = aiRoot('customEndpoint');
  const input = customEndpointUrlInput(root);
  input.value = 'https://api.example.com/v1?api-version=2024-02-01';
  input.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(
      payload.customEndpointUrl,
      'https://api.example.com/v1?api-version=2024-02-01');
});

test('settingsCustomEndpointApiKeyChanged is registered - an unregistered name is FATAL in the browser', () => {
  assert.ok(REGISTERED.has('settingsCustomEndpointApiKeyChanged'));
});

test('the API key row is a password input, never pre-filled', () => {
  const root = aiRoot('customEndpoint');
  const input = apiKeyInput(root);
  assert.ok(input, 'the API key row renders');
  assert.equal(input.type, 'password');
  assert.equal(input.value, '', 'never carries a value from `settings`');
});

// CRITICAL fix: textEditAnchor used to match this field too, so a push
// mid-typing restored the typed plaintext key straight back into the DOM.
test('a push mid-typing leaves the API key field empty, never restores the plaintext', () => {
  const root = aiRoot('customEndpoint');
  const input = apiKeyInput(root);
  input.focus();
  input.value = 'sk-test-12345';

  window.atriumSetCustomEndpointApiKeyStatus(false);

  assert.equal(apiKeyInput(root).value, '');
});

function aiRootWithUrl() {
  const root = aiRoot('customEndpoint');
  const url = customEndpointUrlInput(root);
  url.value = 'https://custom-endpoint.test/v1';
  url.dispatchEvent(new Event('change', {bubbles: true}));
  return root;
}

test('the API key field is disabled with a hint until an endpoint URL is saved', () => {
  const root = aiRoot('customEndpoint');
  assert.equal(apiKeyInput(root).disabled, true);
  assert.equal(apiKeyHint(root).textContent, 'Save the endpoint URL first');
});

test('the API key field stays disabled for a URL that is not https', () => {
  for (const bad of ['foo', 'http://custom-endpoint.test/v1']) {
    const root = aiRoot('customEndpoint');
    const url = customEndpointUrlInput(root);
    url.value = bad;
    url.dispatchEvent(new Event('change', {bubbles: true}));
    assert.equal(apiKeyInput(root).disabled, true, bad);
    assert.equal(apiKeyHint(root).textContent, 'Save the endpoint URL first');
  }
});

test('the API key field turns on once the endpoint URL is set', () => {
  const root = aiRootWithUrl();
  assert.equal(apiKeyInput(root).disabled, false);
});

test('no key saved reads as such before the browser answers otherwise', () => {
  const root = aiRootWithUrl();
  assert.equal(apiKeyHint(root).textContent, 'No key saved');
});

test('typing a key sends it on its own message, never through settingsChanged', () => {
  const root = aiRoot('customEndpoint');
  const input = apiKeyInput(root);
  input.value = 'sk-test-12345';
  input.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsCustomEndpointApiKeyChanged');
  assert.ok(sent, 'settingsCustomEndpointApiKeyChanged is sent');
  assert.deepEqual(sent[1], ['sk-test-12345']);
  assert.equal(
      h.sent.some(([name]) => name === 'settingsChanged'), false,
      'the key must never ride in the settingsChanged blob');
});

test('the field clears itself immediately after sending - the plaintext does not linger', () => {
  const root = aiRoot('customEndpoint');
  const input = apiKeyInput(root);
  input.value = 'sk-test-12345';
  input.dispatchEvent(new Event('change', {bubbles: true}));
  assert.equal(input.value, '');
});

test('a blur with nothing typed sends nothing - it is not a request to clear the key', () => {
  const root = aiRoot('customEndpoint');
  apiKeyInput(root).dispatchEvent(new Event('change', {bubbles: true}));
  assert.equal(
      h.sent.some(([name]) => name === 'settingsCustomEndpointApiKeyChanged'),
      false);
});

test('the browser pushing hasKey=true updates the hint without a re-open', () => {
  const root = aiRootWithUrl();
  window.atriumSetCustomEndpointApiKeyStatus(true);
  assert.equal(apiKeyHint(root).textContent, 'A key is saved in the keychain');
});

// settings.js only sent a typed key, so the native SecItemDelete
// branch (an empty-string call) was unreachable from the app - a key could
// not be removed without Keychain Access or the security CLI.

test('no key saved renders no Remove button - nothing to remove yet', () => {
  const root = aiRoot('customEndpoint');
  window.atriumSetCustomEndpointApiKeyStatus(false);
  assert.equal(clearApiKeyButton(root), null);
});

test('a saved key renders a Remove button', () => {
  const root = aiRoot('customEndpoint');
  window.atriumSetCustomEndpointApiKeyStatus(true);
  assert.ok(clearApiKeyButton(root), 'the Remove key button renders');
});

test('clicking Remove key sends the empty string - the only path that reaches SecItemDelete', () => {
  const root = aiRoot('customEndpoint');
  window.atriumSetCustomEndpointApiKeyStatus(true);
  clearApiKeyButton(root).click();

  const sent = h.sent.find(([name]) => name === 'settingsCustomEndpointApiKeyChanged');
  assert.ok(sent, 'settingsCustomEndpointApiKeyChanged is sent');
  assert.deepEqual(sent[1], ['']);
});

test('clearing the endpoint field clears the stored key too - it would otherwise outlive its use', () => {
  const root = aiRoot('customEndpoint');
  window.atriumSetCustomEndpointApiKeyStatus(true);
  const input = customEndpointUrlInput(root);
  input.value = '';
  input.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsCustomEndpointApiKeyChanged');
  assert.ok(sent, 'clearing the endpoint also clears the key');
  assert.deepEqual(sent[1], ['']);
});

test('changing the endpoint to a new value leaves the stored key alone', () => {
  const root = aiRoot('customEndpoint');
  window.atriumSetCustomEndpointApiKeyStatus(true);
  const input = customEndpointUrlInput(root);
  input.value = 'https://api.example.com/v1';
  input.dispatchEvent(new Event('change', {bubbles: true}));

  assert.equal(
      h.sent.some(([name]) => name === 'settingsCustomEndpointApiKeyChanged'),
      false);
});

test('a rogue chrome.send while AI is off cannot reach the keychain', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({...CORE_SETTINGS, aiEnabled: false});
  root.querySelector('.settings-rail-item[data-pane="ai"]').click();
  selectAiModel(root, 'customEndpoint');
  // Same posture as ollamaHost/ollamaPort staying visible while off:
  // the plain Endpoint field is a stored value, not a live
  // network action, so only the key-submitting row is absent.
  assert.equal(apiKeyInput(root), null, 'the row is absent, not merely hidden');
});

// the request-time probe
// (settingsCheckCustomEndpointConnection) had no caller in settings.js.

test('settingsCheckCustomEndpointConnection is registered - an unregistered name is FATAL in the browser', () => {
  assert.ok(REGISTERED.has('settingsCheckCustomEndpointConnection'));
});

test('the custom endpoint connection row renders a Test connection control', () => {
  const root = aiRoot('customEndpoint');
  assert.ok(testCustomEndpointConnectionButton(root));
});

// The model rides along because the probe sends a real chat-completions
// request - a URL alone cannot tell the native side what to ask for.
test('clicking Test connection sends the endpoint URL and model for the native probe', () => {
  const root = aiRoot('customEndpoint');
  window.atriumSetSettings({
    ...CORE_SETTINGS,
    aiModel: 'customEndpoint',
    customEndpointUrl: 'https://api.example.com/v1',
    customEndpointModel: 'gpt-4o-mini',
  });
  h.sent.length = 0;
  testCustomEndpointConnectionButton(root).click();

  const sent = h.sent.find(
      ([name]) => name === 'settingsCheckCustomEndpointConnection');
  assert.ok(sent, 'settingsCheckCustomEndpointConnection is sent');
  // The timeout rides along too: the probe honours the same advanced row the
  // panel does, so a 10 s default cannot cut off an endpoint set to 120.
  assert.deepEqual(sent[1], ['https://api.example.com/v1', 'gpt-4o-mini', 0]);
});

test('a configured timeout reaches the probe alongside the URL and model', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({
    ...CORE_SETTINGS,
    aiModel: 'customEndpoint',
    customEndpointUrl: 'https://api.example.com/v1',
    customEndpointTimeoutSeconds: 120,
  });
  root.querySelector('.settings-rail-item[data-pane="ai"]').click();
  h.sent.length = 0;
  testCustomEndpointConnectionButton(root).click();

  const sent = h.sent.find(
      ([name]) => name === 'settingsCheckCustomEndpointConnection');
  assert.ok(sent, 'settingsCheckCustomEndpointConnection is sent');
  assert.equal(sent[1][2], 120);
});

test('a pushed reachable result shows Reachable', () => {
  const root = aiRoot('customEndpoint');
  testCustomEndpointConnectionButton(root).click();
  window.atriumSetCustomEndpointConnectionStatus({reachable: true});
  assert.equal(customEndpointConnectionHint(root).textContent, 'Reachable');
});

test('a pushed refusal shows the native reason, not a generic failure', () => {
  const root = aiRoot('customEndpoint');
  testCustomEndpointConnectionButton(root).click();
  window.atriumSetCustomEndpointConnectionStatus(
      {reachable: false, error: 'That address is not reachable from here'});
  assert.equal(
      customEndpointConnectionHint(root).textContent,
      'That address is not reachable from here');
  assert.ok(
      customEndpointConnectionHint(root).classList.contains(
          'settings-row-warn'),
      'a refusal is flagged, not shown as a plain hint');
});

test('AC: a rogue chrome.send while AI is off cannot reach the probe', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({...CORE_SETTINGS, aiEnabled: false});
  root.querySelector('.settings-rail-item[data-pane="ai"]').click();
  selectAiModel(root, 'customEndpoint');
  assert.equal(
      testCustomEndpointConnectionButton(root), null,
      'the row is absent, not merely hidden');
});

test('checkOllamaConnection resolves reachable, calling only /api/tags', async () => {
  const calls = [];
  const fetchImpl = async (url) => {
    calls.push(url);
    return {ok: true};
  };
  const result = await checkOllamaConnection('localhost', 11434, {fetchImpl});
  assert.deepEqual(result, {reachable: true});
  assert.deepEqual(calls, ['http://localhost:11434/api/tags'],
                    'no page content, nothing else is sent');
});

test('checkOllamaConnection reports an HTTP error rather than treating it as reachable', async () => {
  const fetchImpl = async () => ({ok: false, status: 500});
  const result =
      await checkOllamaConnection('localhost', 11434, {fetchImpl});
  assert.equal(result.reachable, false);
  assert.match(result.error, /500/);
});

test('checkOllamaConnection reports a network failure as unreachable, not a throw', async () => {
  const fetchImpl = async () => {
    throw new Error('ECONNREFUSED');
  };
  const result =
      await checkOllamaConnection('localhost', 19999, {fetchImpl});
  assert.equal(result.reachable, false);
  assert.ok(result.error, 'a clear message, not a silent failure');
});

test('an unreachable host does not hang - it resolves on its own timeout', async () => {
  // A fetchImpl that never settles and only answers the abort signal, so this
  // proves the timeout itself fires rather than racing a fast rejection.
  const fetchImpl = (url, {signal}) => new Promise((resolve, reject) => {
    signal.addEventListener('abort', () => reject(new DOMException('', 'AbortError')));
  });
  const result = await checkOllamaConnection(
      'localhost', 11434, {fetchImpl, timeoutMs: 10});
  assert.equal(result.reachable, false);
  assert.match(result.error, /Timed out/);
});

test('Test connection shows a clear error in the panel for a bad endpoint', async () => {
  const root = aiRoot('ollama');
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async () => {
    throw new Error('ECONNREFUSED');
  };
  try {
    root.querySelector('.settings-row-test-connection').click();
    await waitFor(() => {
      const hint = root.querySelector(
          '.settings-row[data-key="ollamaConnection"] .settings-row-hint');
      return hint.textContent !== 'Checking…' && hint.textContent || null;
    });
    const hint = root.querySelector(
        '.settings-row[data-key="ollamaConnection"] .settings-row-hint');
    assert.equal(hint.textContent, 'Could not reach that host and port');
    assert.ok(hint.classList.contains('settings-row-warn'));
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test('a renderSettings failure mid-check is logged, not left as an unhandled rejection', async () => {
  const root = aiRoot('ollama');
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async () => ({ok: true});

  // Let the "Checking…" paint succeed, and fail only the post-await repaint.
  const originalAppendChild = root.appendChild.bind(root);
  let appendCalls = 0;
  root.appendChild = (node) => {
    appendCalls++;
    if (appendCalls > 2) {
      throw new Error('root is detached');
    }
    return originalAppendChild(node);
  };

  const originalConsoleError = console.error;
  const loggedErrors = [];
  console.error = (...args) => loggedErrors.push(args);

  let unhandled = null;
  const onUnhandledRejection = (reason) => {
    unhandled = reason;
  };
  process.on('unhandledRejection', onUnhandledRejection);

  try {
    testConnectionButton(root).click();
    await waitFor(() => appendCalls > 2);
    await new Promise((resolve) => setImmediate(resolve));
    assert.equal(
        unhandled, null,
        'a render failure during the check must not become an unhandled rejection');
    assert.ok(
        loggedErrors.some(
            (args) => args.some(
                (arg) => arg instanceof Error && arg.message === 'root is detached')),
        'the render failure is logged to the console, not dropped silently');
  } finally {
    process.removeListener('unhandledRejection', onUnhandledRejection);
    console.error = originalConsoleError;
    root.appendChild = originalAppendChild;
    globalThis.fetch = originalFetch;
  }
});

test('the warning survives the next push from the browser', () => {
  // The push re-renders the open pane, and a warning that vanished with it
  // would leave the user believing an edit that is not on disk.
  const root = loadedRoot();
  warnToggle(root).click();
  window.atriumSetSaveResult(-2, lastSettingsSaveId());
  window.atriumSetSettings(CORE_SETTINGS);
  assert.ok(root.querySelector('.settings-alert'));
});

// the AI kill switch.

test('the Enable AI features row is a real toggle, not a dead span', () => {
  const root = aiRoot('ollama');
  const toggle = aiEnabledToggle(root);
  assert.ok(toggle, 'aiEnabled renders a toggle');
  assert.equal(toggle.getAttribute('aria-pressed'), 'true');
});

test('switching it off sends the whole object with aiEnabled false', () => {
  const root = aiRoot('ollama');
  aiEnabledToggle(root).click();
  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.aiEnabled, false);
  assert.deepEqual(
      {...payload, aiEnabled: true, aiModel: 'onDevice'}, CORE_SETTINGS);
});

test('the connection row and its network-triggering button do not exist while off - not hidden, absent', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({...CORE_SETTINGS, aiEnabled: false});
  root.querySelector('.settings-rail-item[data-pane="ai"]').click();
  selectAiModel(root, 'ollama');
  assert.equal(testConnectionButton(root), null);
  assert.equal(root.querySelector('.settings-row[data-key="ollamaConnection"]'), null);
});

test('a click that reaches the check handler anyway still cannot call fetch while off', () => {
  // A future regression could put the row back on the page without this
  // file's own render gate - the guard inside the handler is the second lock.
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({...CORE_SETTINGS, aiEnabled: false});
  root.querySelector('.settings-rail-item[data-pane="ai"]').click();
  selectAiModel(root, 'ollama');
  const rogue = document.createElement('button');
  rogue.className = 'settings-row-test-connection';
  root.appendChild(rogue);
  let called = false;
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async () => {
    called = true;
    return {ok: true};
  };
  try {
    rogue.dispatchEvent(new Event('click', {bubbles: true}));
  } finally {
    globalThis.fetch = originalFetch;
    rogue.remove();
  }
  assert.equal(called, false);
});

test('switching AI off mid-check aborts the real fetch, not just its result', () => {
  const root = aiRoot('ollama');
  let sawAbort = false;
  const originalFetch = globalThis.fetch;
  globalThis.fetch = (url, {signal}) => new Promise((resolve, reject) => {
    signal.addEventListener('abort', () => {
      sawAbort = true;
      reject(new DOMException('', 'AbortError'));
    });
  });
  try {
    testConnectionButton(root).click();
    assert.equal(sawAbort, false, 'not aborted before the toggle');
    aiEnabledToggle(root).click();
    assert.ok(sawAbort, 'the in-flight fetch must be aborted, not merely ignored');
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test('turning AI off and back on leaves the Ollama host and port unchanged', () => {
  const root = aiRoot('ollama');
  ollamaHostInput(root).value = '10.0.0.9';
  ollamaHostInput(root).dispatchEvent(new Event('change', {bubbles: true}));

  aiEnabledToggle(root).click();
  assert.equal(testConnectionButton(root), null, 'no surface while off');

  aiEnabledToggle(root).click();
  assert.equal(ollamaHostInput(root).value, '10.0.0.9');
  assert.equal(ollamaPortInput(root).value, '11434');
});

test('turning AI off and back on leaves the endpoint URL unchanged, and the API key row is absent while off', () => {
  const root = aiRoot('customEndpoint');
  customEndpointUrlInput(root).value = 'https://api.example.com/v1';
  customEndpointUrlInput(root).dispatchEvent(new Event('change', {bubbles: true}));

  aiEnabledToggle(root).click();
  assert.equal(apiKeyInput(root), null, 'no surface while off');

  aiEnabledToggle(root).click();
  assert.equal(customEndpointUrlInput(root).value, 'https://api.example.com/v1');
});

// the Auto summarize switch.

test('the Auto summarize row is a real toggle, on by default', () => {
  const root = aiRoot('onDevice');
  const toggle = autoSummarizeToggle(root);
  assert.ok(toggle, 'autoSummarize renders a toggle');
  assert.equal(toggle.getAttribute('aria-pressed'), 'true');
});

test('switching Auto summarize off sends the whole object with ' +
     'autoSummarize false', () => {
  const root = aiRoot('onDevice');
  autoSummarizeToggle(root).click();
  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.autoSummarize, false);
  assert.deepEqual({...payload, autoSummarize: true}, CORE_SETTINGS);
});

// the translation language the panel's Translate and summarize
// action answers in.

const translateLanguageSelect = (root) =>
    root.querySelector(
        '.settings-row[data-key="translateLanguage"] .settings-row-choice');

test('the Translation language row is a select, defaulting to the ' +
     'system language', () => {
  const root = aiRoot('onDevice');
  const select = translateLanguageSelect(root);
  assert.ok(select, 'translateLanguage renders a select');
  assert.equal(select.value, '');
  assert.match(select.querySelector('option').textContent, /^System \(/,
               'the default option must name the language it resolves to');
});

test('picking a language sends the whole object with its tag', () => {
  const root = aiRoot('onDevice');
  const select = translateLanguageSelect(root);
  select.value = 'hu';
  select.dispatchEvent(new Event('change', {bubbles: true}));
  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.translateLanguage, 'hu');
  assert.deepEqual({...payload, translateLanguage: ''}, CORE_SETTINGS);
});

test('the options read as language names, not raw tags', () => {
  const root = aiRoot('onDevice');
  const labels = [...translateLanguageSelect(root).querySelectorAll('option')]
                     .map((o) => o.textContent);
  assert.ok(labels.includes('Hungarian'), labels.join(', '));
  assert.ok(labels.includes('Japanese'), labels.join(', '));
});

// the licenses route.
//
// What is pinned here is REACHABILITY. A credits page that exists in the
// binary but that nothing in Atrium's own chrome links to satisfies neither
// the acceptance criterion nor the BSD-3 term behind it, and that was the
// state before this pane: chrome://credits was registered and unreferenced.

test('the About pane is in the rail, so the notices have a route', () => {
  const root = freshRoot();
  renderSettings(root, 'about');
  assert.ok(root.querySelector('.settings-rail-item[data-pane="about"]'),
            'About is reachable from the rail rather than by URL only');
  assert.equal(root.querySelector('.settings-body').dataset.pane, 'about');
});

test('the About pane offers the licenses as a control, not as prose', () => {
  const root = freshRoot();
  renderSettings(root, 'about');
  const button = root.querySelector('.settings-row-credits');
  assert.ok(button, 'the licenses row is present');
  assert.equal(button.tagName, 'BUTTON',
               'a control, so the keyboard reaches it - the settings profile ' +
                   'allows no browser windows, so a link would go nowhere');
});

test('the licenses click asks the browser rather than naming a URL', () => {
  const root = freshRoot();
  installSettings(root);
  root.querySelector('.settings-rail-item[data-pane="about"]').click();
  h.sent.length = 0;
  root.querySelector('.settings-row-credits').click();
  const [name, args] = h.sent.at(-1);
  assert.equal(name, 'settingsOpenCredits');
  // The destination is fixed in the browser. A renderer that could name the
  // URL could name any of them.
  assert.deepEqual(args, []);
});

test('the About text claims nothing about Atrium\'s own licence', () => {
  // The licence statement lives elsewhere. This pane attributes upstream only, and
  // must not imply that Atrium's own code carries Chromium's terms.
  assert.match(ABOUT_NOTE, /built on Chromium/);
  assert.doesNotMatch(ABOUT_NOTE, /BSD|BSL|copyright|©/i);
});

// the rail is a vertical left column per variant A
// (docs/lab/settings-lab.html), not a horizontal top bar.
const SETTINGS_CSS =
    fs.readFileSync(path.join(SRC, 'chrome/browser/resources/atrium/settings.css'), 'utf8');

test('the rail is laid out as a column, not a horizontal row', () => {
  const rule = SETTINGS_CSS.match(/\.settings-rail\s*{([^}]+)}/);
  assert.ok(rule, '.settings-rail rule is present');
  assert.match(rule[1], /flex-direction:\s*column/);
});

test('the shell puts the rail beside the body, not above it', () => {
  const rule = SETTINGS_CSS.match(/#atrium-root\.settingsroot\s*{([^}]+)}/);
  assert.ok(rule, '#atrium-root.settingsroot rule is present');
  assert.match(
      rule[1], /grid-template-columns/,
      'a column template is the left-rail layout variant A settled on');
});

test('each rail item shows a drawn icon and its label', () => {
  const root = freshRoot();
  renderSettings(root, PANES[0].id);
  for (const pane of PANES) {
    const item = root.querySelector(`.settings-rail-item[data-pane="${pane.id}"]`);
    const icon = item.querySelector('.settings-rail-item-icon');
    const label = item.querySelector('.settings-rail-item-label');
    assert.ok(icon.querySelector('svg'), `${pane.id} shows a drawn icon`);
    assert.equal(label.textContent, pane.title, `${pane.id} shows its label`);
  }
});

// the pin, and the paint a boolean row gets.
//
// The pin is the one control on this route the CORE does not own. Every other
// control edits settings.json; this one edits the window, so the browser is
// the only thing that knows whether the widget is actually floating. The page
// therefore sends and waits rather than marking the button itself - a renderer
// reload then comes back showing the truth instead of a default.

const pinButton = (root) => root.querySelector('.settings-pin');

test('the window carries a pin button beside the pane rail', () => {
  const root = freshRoot();
  renderSettings(root, PANES[0].id);
  const pin = pinButton(root);
  assert.ok(pin, 'the pin renders');
  assert.equal(pin.tagName, 'BUTTON', 'it is a button, not a styled span');
  assert.equal(
      pin.getAttribute('aria-pressed'), 'false',
      'a window that has not been pinned reads as unpressed');
  assert.ok(root.querySelector('.settings-rail'), 'the rail renders too');
});

test('the pin asks the browser to float the window, and to drop it again', () => {
  const root = loadedRoot();
  pinButton(root).click();
  assert.deepEqual(
      h.sent.filter(([name]) => name === 'settingsAlwaysOnTopChanged'),
      [['settingsAlwaysOnTopChanged', [true]]]);

  // The browser answers, and only then does the button read as pressed.
  window.atriumSetSettingsAlwaysOnTop(true);
  assert.equal(pinButton(root).getAttribute('aria-pressed'), 'true');

  h.sent.length = 0;
  pinButton(root).click();
  assert.deepEqual(
      h.sent.filter(([name]) => name === 'settingsAlwaysOnTopChanged'),
      [['settingsAlwaysOnTopChanged', [false]]]);
});

test('settingsAlwaysOnTopChanged is registered, so sending it cannot abort the browser', () => {
  assert.ok(
      REGISTERED.has('settingsAlwaysOnTopChanged'),
      'an unregistered name NOTREACHEDs in the browser process');
});

test('a click alone does not mark the pin - the browser has not answered yet', () => {
  const root = loadedRoot();
  // What the browser does on every open: it states the window's real z-order
  // rather than letting the page start from a guess.
  window.atriumSetSettingsAlwaysOnTop(false);
  pinButton(root).click();
  assert.equal(
      pinButton(root).getAttribute('aria-pressed'), 'false',
      'the page reports the window state it was told, never one it assumed');
});

test('the pushed pin state survives the rebuild a pane change causes', () => {
  const root = loadedRoot();
  window.atriumSetSettingsAlwaysOnTop(true);
  root.querySelector(`.settings-rail-item[data-pane="${PANES[1].id}"]`).click();
  assert.equal(pinButton(root).getAttribute('aria-pressed'), 'true');
});

test('the pin never travels in the settings object', () => {
  const root = loadedRoot();
  window.atriumSetSettingsAlwaysOnTop(true);
  h.sent.length = 0;
  warnToggle(root).click();
  const saved = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(saved, 'the toggle still saves');
  const object = JSON.parse(saved[1][0]);
  assert.deepEqual(
      Object.keys(object).filter((key) => /alwaysOnTop/i.test(key)), [],
      'a window-only control must not reach settings.json');
});

test('a boolean row paints as a filled switch, not as underlined text', () => {
  const rule = SETTINGS_CSS.match(/\.settings-row-toggle\s*{([^}]+)}/);
  assert.ok(rule, '.settings-row-toggle rule is present');
  assert.doesNotMatch(
      rule[1], /text-decoration:\s*underline/,
      'underlined text is what made an editable row read as a caption');
  assert.match(rule[1], /border-radius:\s*999px/, 'the pill radius');
});

test('an on row fills with the accent, and reads it off aria-pressed', () => {
  const rule = SETTINGS_CSS.match(
      /\.settings-row-toggle\[aria-pressed="true"\]\s*{([^}]+)}/);
  assert.ok(rule, 'the on state is keyed off aria-pressed');
  assert.match(rule[1], /background:\s*var\(--ov-accent/);
});

// dom-shim has no layout engine - these check the two rules a real reflow
// needs (see the CSS comments) rather than measuring pixel positions.
test('the row label can shrink below its hint text so the switch never overflows', () => {
  const rule = SETTINGS_CSS.match(/\.settings-row-label\s*{([^}]+)}/);
  assert.ok(rule, '.settings-row-label rule is present');
  assert.match(rule[1], /min-width:\s*0/);
});

test('the switch keeps its 38px track instead of being squeezed by a long hint', () => {
  const rule = SETTINGS_CSS.match(/\.settings-row-toggle\s*{([^}]+)}/);
  assert.ok(rule, '.settings-row-toggle rule is present');
  assert.match(rule[1], /flex-shrink:\s*0/);
});

// --- the Search engines pane -----------------------------------------------

const ENGINES = [
  {id: 'youtube', name: 'YouTube', keyword: 'youtube.com',
   url: 'https://www.youtube.com/results?search_query=%s', enabled: true, builtin: true},
  {id: 'u1', name: 'Docs', keyword: 'docs.rs',
   url: 'https://docs.rs/?q=%s', enabled: true, builtin: false},
];

function enginesPane() {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({searchEngines: ENGINES, searchEnginesSeed: 1});
  root.querySelector('.settings-rail-item[data-pane="engines"]').click();
  h.sent.length = 0;
  return root;
}

function lastSettingsSent() {
  const sent = [...h.sent].reverse().find(([name]) => name === 'settingsChanged');
  return sent ? JSON.parse(sent[1][0]) : null;
}

test('the engines pane lists every engine with its keyword and a builtin tag', () => {
  const root = enginesPane();
  const rows = root.querySelectorAll('.engine-row');
  assert.equal(rows.length, 2);
  assert.equal(rows[0].querySelector('.engine-name').textContent, 'YouTube');
  assert.equal(rows[0].querySelector('.engine-keyword').textContent, 'youtube.com');
  assert.equal(rows[0].querySelector('.engine-builtin') !== null, true);
  assert.equal(rows[1].querySelector('.engine-builtin'), null);
});

test('the enabled toggle sends the whole list with the flag flipped', () => {
  const root = enginesPane();
  const toggle = root.querySelectorAll('.engine-row .engine-enabled')[0];
  toggle.click();
  const next = lastSettingsSent();
  assert.equal(next.searchEngines[0].enabled, false);
  assert.equal(next.searchEngines[1].enabled, true);
});

test('a refused save puts the toggle back, same as a refused row edit', () => {
  const root = enginesPane();
  root.querySelectorAll('.engine-row .engine-enabled')[0].click();
  window.atriumSetSaveResult(-2, lastSettingsSaveId());

  const rows = root.querySelectorAll('.engine-row');
  assert.equal(rows[0].querySelector('.engine-enabled').checked, true,
      'the flag reverts to what was on disk');
  assert.equal(root.querySelector('.settings-body').dataset.pane, 'engines',
      'the rail still shows the pane the failed edit happened on');
});

test('add appends a blank enabled user engine and selects it', () => {
  const root = enginesPane();
  root.querySelector('.engine-add').click();
  const detail = root.querySelector('.engine-detail');
  assert.equal(detail.querySelector('input[name="name"]').value, '');
  const rows = root.querySelectorAll('.engine-row');
  assert.equal(rows.length, 3);
  assert.equal(rows[2].classList.contains('engine-selected'), true);
});

test('editing the url sends on blur, and a url without %s does not send', () => {
  const root = enginesPane();
  root.querySelectorAll('.engine-row')[1].click();
  const url = root.querySelector('.engine-detail input[name="url"]');
  url.value = 'https://docs.rs/?q=';
  url.dispatchEvent(new Event('blur', {bubbles: true}));
  assert.equal(lastSettingsSent(), null);
  assert.match(root.querySelector('.engine-error').textContent, /%s/);
  url.value = 'https://docs.rs/releases/search?query=%s';
  url.dispatchEvent(new Event('blur', {bubbles: true}));
  assert.equal(lastSettingsSent().searchEngines[1].url,
      'https://docs.rs/releases/search?query=%s');
});

test('delete sends the list without the engine', () => {
  const root = enginesPane();
  root.querySelectorAll('.engine-row')[1].click();
  root.querySelector('.engine-delete').click();
  assert.deepEqual(lastSettingsSent().searchEngines.map((e) => e.id), ['youtube']);
});

// The browser feeds the sent list straight to TemplateURLData, which DCHECKs
// on an empty keyword or URL - so the blank row an Add makes is the pane's
// business alone until its fields validate.
test('a blank added engine stays in the pane and never travels', () => {
  const root = enginesPane();
  root.querySelector('.engine-add').click();
  assert.equal(root.querySelectorAll('.engine-row').length, 3);

  root.querySelectorAll('.engine-row .engine-enabled')[0].click();

  assert.deepEqual(lastSettingsSent().searchEngines.map((e) => e.id),
      ['youtube', 'u1']);
  assert.equal(root.querySelectorAll('.engine-row').length, 3,
      'the half-typed row is still on screen');
});

// --- the Routing pane --------------------------------------------------

const RULES = [
  {id: 'r1', enabled: true, matchType: 'contains', pattern: 'corp.example',
   targetSpaceID: 'a', excludedSourceSpaceIDs: []},
  {id: 'r2', enabled: true, matchType: 'contains', pattern: 'github.com',
   targetSpaceID: 'current', excludedSourceSpaceIDs: []},
];
const ROUTING_SPACES = [{id: 'a', name: 'Work'}, {id: 'b', name: 'Home'}];

function routingPane(routing = {schemaVersion: 2, defaultTargetSpaceID: '', rules: RULES}) {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSpaces({spaces: ROUTING_SPACES.map((s) => (
      {...s, tabs: [], pinnedTabs: []}))});
  window.atriumSetRoutingRules(routing);
  root.querySelector('.settings-rail-item[data-pane="routing"]').click();
  h.sent.length = 0;
  return root;
}

function lastRoutingSent() {
  const sent = [...h.sent].reverse()
      .find(([name]) => name === 'settingsRoutingRulesChanged');
  return sent ? JSON.parse(sent[1][0]) : null;
}

function card(root, id) {
  return root.querySelector(`.routing-card[data-rule-id="${id}"]`);
}

test('the routing pane renders one sentence card per rule, then the fallback', () => {
  const root = routingPane();
  const cards = root.querySelectorAll('.routing-sentences > .routing-card');
  assert.equal(cards.length, 3, 'two rules and the fallback');
  assert.equal(card(root, 'r1').querySelector('input[name="pattern"]').value, 'corp.example');
  assert.equal(card(root, 'r1').querySelector('select[name="targetSpaceID"]').value, 'a');
  assert.equal(card(root, 'r2').querySelector('select[name="targetSpaceID"]').value, 'current');
  assert.ok(cards[2].classList.contains('routing-fallback'));
  assert.equal(cards[2].querySelector('select[name="defaultTargetSpaceID"]').value, '');
});

test('the target picker offers Current space first', () => {
  const root = routingPane();
  const options = [...card(root, 'r1').querySelectorAll('select[name="targetSpaceID"] option')];
  assert.deepEqual(options.map((o) => o.value), ['current', 'a', 'b']);
  assert.equal(options[0].textContent, 'Current space');
});

test('add appends a card targeting the current space and focuses its pattern', () => {
  const root = routingPane();
  root.querySelector('.routing-add').click();
  // dom-shim's selector engine has no :not() support, so this filters in JS.
  const cards = [...root.querySelectorAll('.routing-sentences > .routing-card')]
      .filter((c) => !c.classList.contains('routing-fallback'));
  assert.equal(cards.length, 3);
  const fresh = cards[2];
  assert.equal(fresh.querySelector('select[name="targetSpaceID"]').value, 'current');
  assert.equal(document.activeElement, fresh.querySelector('input[name="pattern"]'));
  assert.equal(lastRoutingSent(), null, 'an empty pattern does not ship');
});

test('a pattern commits on blur and ships the whole file', () => {
  const root = routingPane();
  const pattern = card(root, 'r1').querySelector('input[name="pattern"]');
  pattern.value = 'internal.example';
  pattern.dispatchEvent(new Event('blur', {bubbles: true}));
  const sent = lastRoutingSent();
  assert.equal(sent.schemaVersion, 2);
  assert.equal(sent.defaultTargetSpaceID, '');
  assert.equal(sent.rules[0].pattern, 'internal.example');
  assert.equal(sent.rules.length, 2);
});

test('an empty pattern shows the reason on its card and does not ship', () => {
  const root = routingPane();
  const pattern = card(root, 'r1').querySelector('input[name="pattern"]');
  pattern.value = '';
  pattern.dispatchEvent(new Event('blur', {bubbles: true}));
  assert.equal(lastRoutingSent(), null);
  assert.match(card(root, 'r1').querySelector('.routing-error').textContent, /pattern/i);
});

test('toggling an except chip sends excludedSourceSpaceIDs', () => {
  const root = routingPane();
  const chip = card(root, 'r1').querySelector('.routing-chip[data-space-id="b"]');
  assert.equal(chip.getAttribute('aria-pressed'), 'false');
  chip.click();
  assert.deepEqual(lastRoutingSent().rules[0].excludedSourceSpaceIDs, ['b']);
  assert.equal(card(root, 'r1').querySelector('.routing-chip[data-space-id="b"]')
      .getAttribute('aria-pressed'), 'true');
});

test('the fallback select sends defaultTargetSpaceID', () => {
  const root = routingPane();
  const select = root.querySelector('select[name="defaultTargetSpaceID"]');
  select.value = 'b';
  select.dispatchEvent(new Event('change', {bubbles: true}));
  assert.equal(lastRoutingSent().defaultTargetSpaceID, 'b');
});

test('a current-space target ships and a deleted excluded space is dropped', () => {
  const root = routingPane({schemaVersion: 2, defaultTargetSpaceID: '', rules: [
    {...RULES[1], excludedSourceSpaceIDs: ['gone', 'b']}]});
  const pattern = card(root, 'r2').querySelector('input[name="pattern"]');
  pattern.value = 'github.com/edited';
  pattern.dispatchEvent(new Event('blur', {bubbles: true}));
  const sent = lastRoutingSent();
  assert.equal(sent.rules[0].targetSpaceID, 'current');
  assert.deepEqual(sent.rules[0].excludedSourceSpaceIDs, ['b']);
});

test('a target space that no longer exists refuses to send', () => {
  const root = routingPane();
  window.atriumSetSpaces({spaces: [{id: 'b', name: 'Home', tabs: [], pinnedTabs: []}]});
  const pattern = card(root, 'r1').querySelector('input[name="pattern"]');
  pattern.value = 'corp.example/edited';
  pattern.dispatchEvent(new Event('blur', {bubbles: true}));
  assert.equal(lastRoutingSent(), null);
  assert.match(card(root, 'r1').querySelector('.routing-error').textContent, /space/i);
});

test('move down swaps the order and delete drops the rule', () => {
  const root = routingPane();
  card(root, 'r1').querySelector('.routing-down').click();
  assert.deepEqual(lastRoutingSent().rules.map((r) => r.id), ['r2', 'r1']);
  card(root, 'r2').querySelector('.routing-delete').click();
  assert.deepEqual(lastRoutingSent().rules.map((r) => r.id), ['r1']);
});

test('the enabled checkbox sends the flag', () => {
  const root = routingPane();
  card(root, 'r1').querySelector('.routing-enabled').click();
  assert.equal(lastRoutingSent().rules[0].enabled, false);
});

test('a refused save reverts to what was on disk', () => {
  const root = routingPane();
  card(root, 'r1').querySelector('.routing-delete').click();
  window.atriumSetRoutingSaveResult(-1);
  assert.ok(card(root, 'r1'), 'the deleted rule is back');
});

test('settingsRoutingRulesChanged is a registered surface message', () => {
  assert.ok(REGISTERED.has('settingsRoutingRulesChanged'));
});

// the Accent row offers the brand book's themes. The slugs are the
// contract with overlay.css's html.atrium-theme-* classes, so a rename here
// that misses the stylesheet paints nothing rather than failing loudly.
const accentRow = () =>
    PANE_ROWS.appearance.find((r) => r.key === 'overlayAccent');

test('the accent row is called Accent, not Overlay accent', () => {
  assert.equal(accentRow().label, 'Accent');
});

test('every brand book theme is offered', () => {
  const choices = accentRow().choices;
  assert.equal(choices.length, 20, 'nineteen brand book themes and System');
  assert.equal(new Set(choices).size, choices.length, 'a slug is offered twice');
});

// The System accent reads NSColor.controlAccentColor, so it has no class in
// overlay.css and no entry in the brand book. Only the picker knows it exists.
test('the accent row offers System', () => {
  assert.ok(accentRow().choices.includes('system'));
  assert.equal(choiceLabel('overlayAccent', 'system'), 'System');
});

// macOS has no Multicolor colour to read - AppKit answers blue for it - so the
// pane is the only place a user can be told before they pick it.
test('the accent hint says what Multicolor does', () => {
  assert.match(accentRow().hint, /Multicolor is blue/);
});

test('System sits last, so a stored brand accent still lands on Crimson', () => {
  const choices = accentRow().choices;
  assert.equal(choices[choices.length - 1], 'system');
  assert.equal(choices[0], 'crimson');
});

test('picking System sends the whole object with the system slug', () => {
  const root = appearanceRoot();
  const select = overlayAccentSelect(root);
  const offered = [...select.children].map((option) => option.value);
  assert.ok(offered.includes('system'),
      'the row renders no System option, so nothing could pick it');
  select.value = 'system';
  select.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.filter(([name]) => name === 'settingsChanged').at(-1);
  assert.ok(sent, 'settingsChanged is sent');
  assert.equal(JSON.parse(sent[1][0]).overlayAccent, 'system');
});

test('a saved System accent repaints the picker after a restart', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings({...CORE_SETTINGS, overlayAccent: 'system'});
  root.querySelector('.settings-rail-item[data-pane="appearance"]').click();
  assert.equal(overlayAccentSelect(root).value, 'system');
});

// actionLabel's slug split would render these as 'Rose Pine' and 'Tokyo night'.
test('theme names keep the brand book spelling', () => {
  assert.equal(choiceLabel('overlayAccent', 'rose-pine'), 'Rosé Pine');
  assert.equal(choiceLabel('overlayAccent', 'tokyo-night'), 'Tokyo Night');
  assert.equal(choiceLabel('overlayAccent', 'monokai-pro'), 'Monokai Pro');
});

test('no choice falls through to the camelCase fallback', () => {
  for (const choice of accentRow().choices) {
    const label = choiceLabel('overlayAccent', choice);
    assert.ok(label && label !== choice,
        `${choice} has no label of its own, so it renders as its slug`);
  }
});

// --- the overlay-language redesign --------------------------------

const OVERLAY_CSS = fs.readFileSync(
    path.join(SRC, 'chrome/browser/resources/atrium/overlay.css'), 'utf8');

// these four read a fallback nothing in the tree defined. --atrium-warn had
// the same problem and is covered alongside them.
test('no settings.css token falls through to an undefined fallback', () => {
  for (const token of ['--atrium-accent', '--atrium-hairline',
                        '--atrium-row-selected-bg', '--atrium-font-body',
                        '--atrium-warn']) {
    assert.doesNotMatch(
        SETTINGS_CSS, new RegExp(`var\\(${token}\\b`),
        `${token} must not appear in settings.css`);
  }
});

// every var(--ov-radius-*) name settings.css reads is one overlay.css
// actually defines - a renamed step would fall through silently otherwise.
test('every radius token settings.css reads is one overlay.css defines', () => {
  const used = new Set(
      [...SETTINGS_CSS.matchAll(/var\(--ov-radius-[a-z]+/g)]
          .map((m) => m[0].slice('var('.length)));
  assert.ok(used.size > 0, 'settings.css reads at least one radius token');
  for (const token of used) {
    assert.match(
        OVERLAY_CSS, new RegExp(`${token}:`),
        `${token} is not one overlay.css defines`);
  }
});

// a border-radius in settings.css is a ladder token, a pill (999px),
// or a circle (50%) - the two shapes the ladder does not cover.
test('every literal border-radius in settings.css is a pill or a circle', () => {
  const literals = [...SETTINGS_CSS.matchAll(/border-radius:\s*([^;]+);/g)]
      .map((m) => m[1].trim())
      .filter((value) => !value.startsWith('var('));
  assert.ok(literals.length > 0, 'at least one literal radius is expected');
  for (const value of literals) {
    assert.ok(value === '999px' || value === '50%',
        `${value} is neither a pill nor a circle - pills stay 999`);
  }
});

// no control reads as a caption rather than something to click.
test('no control in settings.css is an underlined text link', () => {
  assert.doesNotMatch(SETTINGS_CSS, /text-decoration:\s*underline/);
});

// General, Appearance and AI - the three panes the task named as one
// flat ladder - render their rows inside titled cards.
test('General, Appearance and AI group their rows into cards', () => {
  const root = loadedRoot();
  for (const paneId of ['general', 'appearance', 'ai']) {
    renderSettings(root, paneId);
    assert.ok(root.querySelector('.settings-group'),
        `${paneId} renders at least one group card`);
    assert.ok(root.querySelector('.settings-group-title'),
        `${paneId} renders at least one group title`);
  }
});

// the window's own palette and type come from the overlay contract.
test('the root reads its background, ink and font off overlay tokens', () => {
  const rule = SETTINGS_CSS.match(/#atrium-root\.settingsroot\s*{([^}]+)}/);
  assert.ok(rule, '#atrium-root.settingsroot rule is present');
  assert.match(ruleBody('.settings-body'), /background:\s*var\(--ov-ground\)/);
  assert.match(rule[1], /color:\s*var\(--ov-fg\)/);
  assert.match(rule[1], /font:.*var\(--ov-font\)/);
});

// Sidebar position and Restore tabs on launch are the two rows under three
// choices (Overlay accent's 20 choices are a field), so both
// take the segmented shape rather than a <select>.
test('Sidebar position and Restore tabs on launch are segmented', () => {
  const root = loadedRoot();
  assert.equal(segSelected(root, 'sidebarPosition').dataset.choice, 'left');
  assert.equal(segSelected(root, 'startupBehavior').dataset.choice,
               'restoreLastActive');
});

// The Glass opacity row is the lab's one demonstrated "Number" control - a
// native range a user drags to, not a typed field like ollamaPort's own id.
test('Glass opacity renders a range input plus its value field', () => {
  const root = appearanceRoot();
  const row = root.querySelector('.settings-row[data-key="glassOpacity"]');
  const range = row.querySelector('.settings-slider-range');
  assert.ok(range, 'the range renders');
  assert.equal(range.type, 'range');
  assert.equal(range.min, '0.02');
  assert.equal(range.max, '1');
  assert.equal(range.step, '0.01');
  assert.equal(range.value, '0.55');
  assert.equal(
      row.querySelector('.settings-row-number').value, '0.55');
});

test('changing the Glass opacity range commits its value', () => {
  const root = appearanceRoot();
  const range = root.querySelector(
      '.settings-row[data-key="glassOpacity"] .settings-slider-range');
  range.value = '0.41';
  range.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.glassOpacity, 0.41);
});

// The second slider in the pane, and the one that proves the
// control is no longer keyed to glassOpacity by name.
test('Border width renders a range over the store range', () => {
  const root = appearanceRoot();
  const row = root.querySelector('.settings-row[data-key="borderWidth"]');
  const range = row.querySelector('.settings-slider-range');
  assert.ok(range, 'the range renders');
  assert.equal(range.type, 'range');
  assert.equal(range.min, '4', 'MIN_BORDER_WIDTH');
  assert.equal(range.max, '10', 'MAX_BORDER_WIDTH');
  assert.equal(range.step, '1', 'whole points, the unit the inset is in');
  assert.equal(range.value, '10');
  assert.equal(row.querySelector('.settings-row-number').value, '10');
});

test('changing the Border width range commits its value', () => {
  const root = appearanceRoot();
  const range = root.querySelector(
      '.settings-row[data-key="borderWidth"] .settings-slider-range');
  range.value = '8';
  range.dispatchEvent(new Event('change', {bubbles: true}));

  const sent = h.sent.find(([name]) => name === 'settingsChanged');
  assert.ok(sent, 'settingsChanged is sent');
  const payload = JSON.parse(sent[1][0]);
  assert.equal(payload.borderWidth, 8);
  // The whole object, not a patch - the same contract every other row keeps.
  assert.equal(payload.glassOpacity, 0.55);
});

test('Border width sits in The window, beside the other surround rows', () => {
  const root = appearanceRoot();
  // The title is the card's previous sibling, not a child of it.
  const title = [...root.querySelectorAll('.settings-group-title')].find(
      (t) => t.textContent === 'The window');
  assert.ok(title, 'The window group renders');
  const group = title.nextElementSibling;
  assert.deepEqual(
      [...group.querySelectorAll('.settings-row')].map((r) => r.dataset.key),
      ['chromePageTint', 'opaqueSurround', 'borderWidth']);
});

// --- the rail's platform chrome and the missing focus
// ring - one defect showing in two states ------------------------------------

// the rail item drew as a native button because it was the one
// button family here that never reset the platform's own chrome.
test('the rail item resets platform chrome like every other button', () => {
  const rule = SETTINGS_CSS.match(/\.settings-rail-item\s*{([^}]+)}/);
  assert.ok(rule, '.settings-rail-item rule is present');
  assert.match(rule[1], /appearance:\s*none/);
  assert.match(rule[1], /border:\s*0/);
});

// Native mode sets --ov-sel to the accent, so the selected item's default
// accent ink is invisible there.
test('the selected rail item keeps readable ink in native mode', () => {
  const rule = SETTINGS_CSS.match(
      /html\.atrium-native-chrome\s+\.settings-rail-item\.selected\s*{([^}]+)}/);
  assert.ok(rule, 'a native-mode rule for the selected rail item is present');
  assert.match(rule[1], /color:\s*var\(--ov-accent-ink\)/);
});

// Native mode sets --ov-sel to the solid accent, the thumb's own colour, so
// the whole track drew as one accent block.
test('the segmented track stays a wash in native mode', () => {
  const rule = SETTINGS_CSS.match(
      /html\.atrium-native-chrome\s+\.settings-seg\s*,\s*html\.atrium-native-chrome\s+\.settings-swipe-presets\s*{([^}]+)}/);
  assert.ok(rule, 'a native-mode rule covers the segmented and Swipe feel tracks');
  assert.match(rule[1], /background:\s*color-mix\(in srgb, var\(--ov-accent\) 14%, transparent\)/);
});

// Native mode swaps --ov-font for the system font, whose metrics move every
// row. The settings window keeps Archivo so both looks line up.
test('the settings root keeps Archivo in native mode', () => {
  const rule = SETTINGS_CSS.match(
      /html\.atrium-native-chrome\s+#atrium-root\.settingsroot\s*{([^}]+)}/);
  assert.ok(rule, 'a native-mode rule for the settings root is present');
  assert.match(rule[1], /--ov-font:\s*'Archivo'/);
  assert.match(rule[1], /--ov-font-mono:\s*ui-monospace/);
});

// Settings is always native. Native mode sets --ov-paper to the
// ground, so a group card would show only its border, and a theme class's
// light ink would vanish on the dark ground.
test('the settings cards and ink read on the native ground', () => {
  const rule = SETTINGS_CSS.match(
      /html\.atrium-native-chrome:has\(#atrium-root\.settingsroot\)\s*{([^}]+)}/);
  assert.ok(rule, 'a native-mode html rule scoped to the settings root');
  assert.match(rule[1], /--ov-ink:\s*var\(--atrium-surround-ink/);
  assert.match(rule[1], /--ov-paper:\s*color-mix\([^;]*var\(--ov-ground\)/);
});

// .ov-icon sets its own colour (accent, or mute in native mode), which
// outranks the button's inherited ink, so the pressed pin drew a dark icon
// on the accent fill.
test('the pressed pin paints its icon in the accent ink', () => {
  const rule = SETTINGS_CSS.match(
      /\.settings-pin\[aria-pressed="true"\]\s+\.ov-icon\s*{([^}]+)}/);
  assert.ok(rule, 'a rule for the pressed pin icon is present');
  assert.match(rule[1], /color:\s*inherit/);
});

// A wheel that the pane cannot consume chains to the document, which
// rubber-bands. The html stays transparent so the window's sidebar vibrancy
// shows, never the white a page paints by default.
test('a wheel never leaves the pane or paints white', () => {
  // The other route sheets set `html, body` transparent on every route, so
  // the plain selector loses the cascade and the rule needs :has().
  const rule = SETTINGS_CSS.match(
      /html:has\(#atrium-root\.settingsroot\)\s*{([^}]+)}/);
  const html = rule ? rule[1] : null;
  assert.ok(html, 'an html rule scoped to the settings root is present');
  assert.match(html, /overscroll-behavior:\s*none/);
  assert.match(html, /background:\s*transparent/);
  const body = ruleBody('.settings-body');
  assert.match(body, /overscroll-behavior:\s*contain/);
});

// every custom control on this route takes its keyboard focus ring
// from the overlay convention (overlay.css's .ov-button:focus-visible),
// rather than falling back to the browser default.
const FOCUS_VISIBLE_CONTROLS = [
  '.settings-pin', '.settings-rail-item', '.settings-row-toggle',
  '.settings-row-choice', '.settings-row-number', '.settings-row-text',
  '.settings-seg-option', '.settings-swipe-preset', '.settings-app-icon',
  '.settings-space-item', '.settings-profile-item', '.settings-row-credits',
  '.settings-row-test-connection',
  '.settings-row-test-custom-endpoint-connection',
  '.settings-row-clear-api-key', '.settings-space-open',
  '.settings-profile-download', '.settings-profile-page',
  '.settings-profile-delete', '.engine-add', '.engine-delete',
];

// A :focus-visible selector may share its body with siblings in a
// comma-separated group (the row-choice family does), so the lookup finds
// where the selector's own name sits and reads whichever brace pair follows.
function focusVisibleBody(selector) {
  const escaped = selector.replace(/\./g, '\\.');
  const start = SETTINGS_CSS.search(new RegExp(`${escaped}:focus-visible\\s*[,{]`));
  if (start === -1) return null;
  const open = SETTINGS_CSS.indexOf('{', start);
  return SETTINGS_CSS.slice(open + 1, SETTINGS_CSS.indexOf('}', open));
}

test('every interactive control defines its own :focus-visible ring', () => {
  for (const selector of FOCUS_VISIBLE_CONTROLS) {
    const body = focusVisibleBody(selector);
    assert.ok(body, `${selector}:focus-visible rule is present`);
    assert.match(
        body, /outline:\s*2px solid var\(--ov-accent\)/,
        `${selector}:focus-visible takes its ring from the overlay convention`);
  }
});

// Trap: the segmented thumb slides directly under its option, and
// the 3px gap between options is too tight for an outward ring - it draws
// inside the button instead of matching the rest of this list.
test('the segmented option draws its ring inward, away from the sliding thumb', () => {
  const rule =
      SETTINGS_CSS.match(/\.settings-seg-option:focus-visible\s*{([^}]+)}/);
  assert.ok(rule, '.settings-seg-option:focus-visible rule is present');
  assert.match(rule[1], /outline-offset:\s*-2px/);
});

// a button is border-box by default, so a track narrower than its
// full box overflows the icon and drags the ring off centre.
test('the app-icon track fits the icon plus its padding and border', () => {
  const iconsRule = SETTINGS_CSS.match(/\.settings-app-icons\s*{([^}]+)}/);
  const iconRule = SETTINGS_CSS.match(/\.settings-app-icon\s*{([^}]+)}/);
  const imgRule = SETTINGS_CSS.match(/\.settings-app-icon-img\s*{([^}]+)}/);
  assert.ok(iconsRule, '.settings-app-icons rule is present');
  assert.ok(iconRule, '.settings-app-icon rule is present');
  assert.ok(imgRule, '.settings-app-icon-img rule is present');

  const track = iconsRule[1].match(/grid-template-columns:\s*repeat\(auto-fill,\s*(\d+)px\)/);
  const padding = iconRule[1].match(/padding:\s*(\d+)px/);
  const border = iconRule[1].match(/border:\s*(\d+)px solid/);
  const img = imgRule[1].match(/width:\s*(\d+)px/);
  assert.ok(track, 'grid-template-columns is a fixed auto-fill track');
  assert.ok(padding, '.settings-app-icon has a padding declaration');
  assert.ok(border, '.settings-app-icon has a border-width declaration');
  assert.ok(img, '.settings-app-icon-img has a fixed width');

  const trackPx = Number(track[1]);
  const wantPx = Number(img[1]) + 2 * Number(padding[1]) + 2 * Number(border[1]);
  assert.equal(trackPx, wantPx,
      `track is ${trackPx}px but the button's border box is ${wantPx}px`);
});

// --- the settings search field ---
//
// Results are a list under the field, not a filtered rail: hiding rail items
// would fight the roving tabindex, one Tab stop with arrows inside it.

const searchField = (root) => root.querySelector('.settings-search-field');

const searchResults = (root) =>
    [...root.querySelectorAll('.settings-search-result')];

// Types into the field the way a user does - the surface listens for 'input'.
function typeSearch(root, text) {
  const field = searchField(root);
  field.value = text;
  field.dispatchEvent(new Event('input', {bubbles: true}));
  return field;
}

test('the search field sits above the rail, in the rail\'s own column', () => {
  const root = freshRoot();
  installSettings(root);
  const column = root.querySelector('.settings-sidebar');
  assert.ok(column, 'the rail and the field share one column');
  const kids = [...column.children];
  assert.ok(kids[0].classList.contains('settings-drag-region'),
            'the window drag handle comes first');
  assert.ok(kids[1].classList.contains('settings-search'),
            'the field follows it');
  assert.ok(kids[2].classList.contains('settings-rail'),
            'and the rail follows the field');
});

test('the corner strip that clears the floating traffic lights is still there', () => {
  const root = freshRoot();
  installSettings(root);
  const dragRegion = root.querySelector('.settings-drag-region');
  assert.ok(dragRegion, 'the window needs a drag handle with no titlebar');
});

// dom-shim has no layout engine, so a real drag is the VM scenario's job -
// this checks the CSS source and the no-drag class list only.

function ruleBody(selector) {
  const escaped = selector.replace(/[.#]/g, '\\$&');
  const rule = SETTINGS_CSS.match(new RegExp(`${escaped}\\s*{([^}]+)}`));
  return rule ? rule[1] : null;
}

test('the pane heading and the sidebar background are drag regions', () => {
  for (const selector of ['.settings-heading', '.settings-sidebar']) {
    const body = ruleBody(selector);
    assert.ok(body, `${selector} rule is present`);
    assert.match(body, /-webkit-app-region:\s*drag/,
                 `${selector} moves the window`);
    assert.match(body, /user-select:\s*none/,
                 `${selector} does not turn a press into a text selection`);
  }
});

// On macOS a wheel over a drag region never reaches the page: AppKit picks
// the wheel target with the hitTest: that returns nil for HTCAPTION.
test('neither the scroll container nor the pane content is a drag region', () => {
  for (const selector of ['.settings-body', '.settings-pane-content']) {
    const body = ruleBody(selector);
    assert.ok(body, `${selector} rule is present`);
    assert.doesNotMatch(body, /-webkit-app-region:\s*drag/,
                         `${selector} does not drag - .settings-heading does`);
  }
});

// .engine-row and .theme-studio-canvas-wrap are plain divs the tag
// selectors here miss. .settings-rail and .settings-search-results scroll
// on their own, same reason .settings-body needs a hole rather than a drag.
const NO_DRAG_SELECTORS = [
  'button', 'input', 'select', 'textarea', '.settings-row',
  '.engine-row', '.theme-studio-canvas-wrap',
  '.settings-rail', '.settings-search-results',
];
const NO_DRAG_SELECTOR = NO_DRAG_SELECTORS.join(', ');

test('every interactive class and scroll container punches its own no-drag hole', () => {
  const rule = SETTINGS_CSS.match(
      /([^{}]+)\{\s*-webkit-app-region:\s*no-drag;\s*\}/);
  assert.ok(rule, 'a no-drag rule is present');
  for (const selector of NO_DRAG_SELECTORS) {
    const escaped = selector.replace(/[.]/g, '\\.');
    assert.match(rule[1], new RegExp(`(^|[\\s,])${escaped}([\\s,]|$)`),
                 `${selector} is in the no-drag rule`);
  }
});

test('a representative control from every pane matches the no-drag rule', () => {
  const cases = [
    [loadedRoot(), '.settings-search-field', 'the search field'],
    [loadedRoot(), '.settings-search-results', 'the search results list'],
    [loadedRoot(), '.settings-rail', 'the rail (a scroll container)'],
    [loadedRoot(), '.settings-rail-item', 'a rail item'],
    [loadedRoot(), '.settings-pin', 'the always-on-top pin'],
    [loadedRoot(), '.settings-row', 'a plain row'],
    [appearanceRoot(), '.settings-slider-range', 'the glass-opacity range'],
    [enginesPane(), '.engine-row', 'an engine row'],
    [themeRoot(), '.theme-studio-canvas-wrap', 'the theme studio canvas'],
  ];
  for (const [root, selector, label] of cases) {
    const el = root.querySelector(selector);
    assert.ok(el, `${label} (${selector}) renders`);
    assert.ok(el.matches(NO_DRAG_SELECTOR), `${label} is in the no-drag hole`);
  }
});

test('the About pane\'s attribution note stays selectable, unlike its chrome', () => {
  const root = loadedRoot();
  root.querySelector('.settings-rail-item[data-pane="about"]').click();
  const note = root.querySelector('.settings-about-note');
  assert.ok(note, 'the note renders');
  assert.equal(note.textContent, ABOUT_NOTE);

  const noteBody = ruleBody('.settings-about-note');
  assert.ok(noteBody, '.settings-about-note rule is present');
  assert.match(noteBody, /user-select:\s*text/);
});

test('a plain informational row value stays selectable, unlike its label', () => {
  const valueBody = ruleBody('span.settings-row-value');
  assert.ok(valueBody, 'span.settings-row-value rule is present');
  assert.match(valueBody, /user-select:\s*text/);
});

test('a query matches a row label from another pane', () => {
  // 'glass' alone also lands on Native app view's hint ("tinted glass") -
  // the full label picks out only the row this test is pinning.
  const hits = searchSettings('glass opacity');
  assert.equal(hits.length, 1);
  assert.equal(hits[0].pane, 'appearance');
  assert.equal(hits[0].key, 'glassOpacity');
  assert.equal(hits[0].label, 'Glass opacity');
});

test('a query matches a row hint, not only its label', () => {
  // 'Default 11434' is the Ollama port row's hint; the label says neither
  // word. A field that only saw labels would find nothing here.
  const hits = searchSettings('11434');
  assert.deepEqual(hits.map((h) => h.key), ['ollamaPort']);
});

test('a query reaches the Inputs rows the hotkey store builds', () => {
  // The Inputs pane has no PANE_ROWS list for its shortcuts - they come from
  // the pushed action names, so the index has to walk them through
  // actionLabel the same way the pane does.
  const hits = searchSettings('space 1', ['goToSpace1', 'openShelf']);
  assert.deepEqual(hits.map((h) => h.action), ['goToSpace1']);
  assert.equal(hits[0].pane, 'inputs');
  assert.equal(hits[0].label, 'Go to space 1');
});

test('a label match outranks a hint match', () => {
  const hits = searchSettings('sidebar');
  assert.equal(hits[0].label, 'Hide sidebar',
               'the row whose own name says it comes first');
  assert.ok(hits.length > 1, 'the hint matches still show, below it');
});

test('an empty query is no query, not a match on everything', () => {
  assert.deepEqual(searchSettings(''), []);
  assert.deepEqual(searchSettings('   '), []);
});

test('a row with no key is still addressable by the mark', () => {
  // 'Hide sidebar' is unwired, so data-key is empty on its row and cannot
  // name it.
  const hits = searchSettings('hide sidebar');
  assert.equal(searchResultSelector(hits[0]),
               '.settings-row[data-row="hide-sidebar"]');
  const root = freshRoot();
  installSettings(root);
  assert.ok(root.querySelector(searchResultSelector(hits[0])),
            'and that selector finds the row the General pane rendered');
});

test('typing shows the matches under the field', () => {
  const root = freshRoot();
  installSettings(root);
  assert.equal(searchResults(root).length, 0, 'nothing before a query');
  typeSearch(root, 'glass opacity');
  const shown = searchResults(root);
  assert.equal(shown.length, 1);
  assert.equal(
      shown[0].querySelector('.settings-search-result-label').textContent,
      'Glass opacity');
  assert.equal(
      shown[0].querySelector('.settings-search-result-pane').textContent,
      'Appearance', 'the result says which pane it will take you to');
});

test('choosing a result switches to that pane and marks the row', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings(CORE_SETTINGS);
  typeSearch(root, 'glass');
  searchResults(root)[0].click();

  assert.equal(root.querySelector('.settings-body').dataset.pane, 'appearance');
  const row = root.querySelector('.settings-row[data-key="glassOpacity"]');
  assert.ok(row, 'the row is on screen');
  assert.ok(row.classList.contains('found'), 'and it is marked');
});

test('choosing a second result on the same pane clears the first mark', () => {
  // selectPane no-ops when the pane is already open (re-selecting is not a
  // change to report), so the old mark survives unless chooseSearchResult
  // clears it itself.
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings(CORE_SETTINGS);
  typeSearch(root, 'glass opacity');
  searchResults(root)[0].click();
  assert.equal(root.querySelector('.settings-body').dataset.pane, 'appearance');

  typeSearch(root, 'night shift');
  searchResults(root)[0].click();

  const marked = root.querySelectorAll('.settings-row.found');
  assert.equal(marked.length, 1, 'only the newly chosen row stays marked');
  assert.equal(marked[0].dataset.key, 'skylightNightShift');
});

test('Enter chooses the highlighted result, arrows move the highlight', () => {
  const root = freshRoot();
  installSettings(root);
  // aiModel: 'ollama' - the Ollama rows only render for their own model
  // (AI_MODEL_ROW_OWNER), and the chosen result has to find one on screen.
  window.atriumSetSettings({...CORE_SETTINGS, aiModel: 'ollama'});
  const field = typeSearch(root, 'ollama');
  assert.ok(searchResults(root).length > 1, 'more than one row says Ollama');
  assert.ok(searchResults(root)[0].classList.contains('active'),
            'the first result starts highlighted');
  press(field, 'ArrowDown');
  assert.ok(searchResults(root)[1].classList.contains('active'));

  press(field, 'Enter');
  assert.equal(root.querySelector('.settings-body').dataset.pane, 'ai');
  assert.equal(root.querySelectorAll('.settings-row.found').length, 1);
});

test('a chosen Inputs result marks the shortcut row the store built', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {goToSpace1: ['cmd+1']}});
  typeSearch(root, 'go to space 1');
  searchResults(root)[0].click();
  assert.equal(root.querySelector('.settings-body').dataset.pane, 'inputs');
  assert.ok(
      root.querySelector('.settings-row[data-action="goToSpace1"]')
          .classList.contains('found'));
});

test('the rail stays one Tab stop while the field holds text', () => {
  // The roving tabindex is why results are a list rather than a
  // filtered rail: a hidden rail item would take the Tab stop with it.
  const root = freshRoot();
  installSettings(root);
  typeSearch(root, 'glass');
  const items = root.querySelectorAll('.settings-rail-item');
  assert.equal(items.length, PANES.length, 'no rail item is hidden');
  assert.equal(items.filter((i) => i.tabIndex === 0).length, 1);
});

test('the arrow keys still walk the rail while the field holds text', () => {
  const root = freshRoot();
  installSettings(root);
  typeSearch(root, 'glass');
  press(railItem(root, PANES[0].id), 'ArrowDown');
  assert.equal(root.querySelector('.settings-body').dataset.pane, PANES[1].id);
  assert.equal(searchField(root).value, 'glass', 'and the query survives it');
});

test('Escape clears the field before the window sees it', () => {
  const root = freshRoot();
  installSettings(root);
  const field = typeSearch(root, 'glass');
  const delivered = press(field, 'Escape');
  assert.equal(field.value, '', 'the field is cleared');
  assert.equal(searchResults(root).length, 0, 'and so is the result list');
  assert.equal(delivered, false,
               'the key is consumed, so it never reaches the window close');
});

test('Escape on an empty field is the window\'s to answer', () => {
  const root = freshRoot();
  installSettings(root);
  assert.equal(press(searchField(root), 'Escape'), true,
               'nothing to clear, so nothing is swallowed');
});

test('Escape clears the field from anywhere in the window', () => {
  // This is about the window's Escape, and the field does not have to hold
  // focus for the window to get one.
  const root = freshRoot();
  installSettings(root);
  typeSearch(root, 'glass');
  assert.equal(press(railItem(root, PANES[0].id), 'Escape'), false);
  assert.equal(searchField(root).value, '');
});

test('Escape clears the field from a result button too', () => {
  const root = freshRoot();
  installSettings(root);
  typeSearch(root, 'glass');
  assert.equal(press(searchResults(root)[0], 'Escape'), false);
  assert.equal(searchField(root).value, '');
});

test('the search field and its results take the overlay ring', () => {
  assert.match(SETTINGS_CSS,
               /\.settings-search-field:focus-visible\s*{[^}]*outline:\s*2px solid var\(--ov-accent\)/);
  // Scoped to the keyframe name, not just 'animation:' - the reduced-motion
  // override matches that bare a moment too, and sits earlier in the file.
  assert.match(SETTINGS_CSS,
               /\.settings-row\.found\s*{[^}]*animation:\s*settings-row-found\b/,
               'the marked row is highlighted briefly, not forever');
});

// --- the results replace the rail, not sit beside it ---

const searchWrap = (root) => root.querySelector('.settings-search');

test('idle: no query, the rail shows and the wrap carries no active mark', () => {
  const root = freshRoot();
  installSettings(root);
  assert.ok(!searchWrap(root).classList.contains('settings-search-active'));
});

test('results: a query hides the rail in favor of the result list', () => {
  const root = freshRoot();
  installSettings(root);
  typeSearch(root, 'glass opacity');
  assert.ok(searchWrap(root).classList.contains('settings-search-active'));
  assert.match(SETTINGS_CSS,
               /\.settings-search\.settings-search-active\s*~\s*\.settings-rail\s*{[^}]*display:\s*none/,
               'the class the field carries is the one the CSS hides the rail on');
});

test('no results: a query with no match shows one line, not an empty area', () => {
  const root = freshRoot();
  installSettings(root);
  typeSearch(root, 'zzzznotasetting');
  assert.equal(searchResults(root).length, 0);
  const empty = root.querySelector('.settings-search-empty');
  assert.ok(empty, 'a line stands in for the missing results');
  assert.equal(empty.textContent, 'No results');
  assert.ok(searchWrap(root).classList.contains('settings-search-active'),
            'the rail stays out of the way even with nothing to show');
});

test('cleared: emptying the field restores the rail and drops the no-match line', () => {
  const root = freshRoot();
  installSettings(root);
  typeSearch(root, 'zzzznotasetting');
  typeSearch(root, '');
  assert.ok(!searchWrap(root).classList.contains('settings-search-active'));
  assert.equal(root.querySelector('.settings-search-empty'), null);
});

test('choosing a result keeps the rail out of the way while the query stands', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetSettings(CORE_SETTINGS);
  typeSearch(root, 'glass opacity');
  searchResults(root)[0].click();
  assert.ok(searchWrap(root).classList.contains('settings-search-active'),
            'the query survives the pane switch, so the results still stand in for the rail');
});

test('the field takes the same radius as the other field controls, sized to its column', () => {
  const rule = SETTINGS_CSS.match(/\.settings-search-field\s*{([^}]+)}/);
  assert.ok(rule, '.settings-search-field rule is present');
  assert.match(rule[1], /border-radius:\s*var\(--ov-radius-anchor\)/,
               'the same token .settings-row-text and .settings-row-choice use');
  assert.match(rule[1], /box-sizing:\s*border-box/,
               'so its border and padding stay inside the 100% width, not past its column');
});

// --- rebinding a shortcut in the Inputs pane ---

function hotkeyRow(root, action) {
  return root.querySelector(`.settings-row[data-action="${action}"]`);
}

test('a combo renders as keycaps in the macOS modifier order', () => {
  const caps = [...comboKeycaps('cmd+shift+opt+ctrl+k').querySelectorAll('kbd.key')]
      .map((k) => k.textContent);
  assert.deepEqual(caps, ['⌃', '⌥', '⇧', '⌘', 'K']);
  const named = [...comboKeycaps('opt+tab').querySelectorAll('kbd.key')]
      .map((k) => k.textContent);
  assert.deepEqual(named, ['⌥', '⇥']);
});

test('shortcut rows sit in one ledger grid with three cells each', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {openCommandBar: ['cmd+l'], goToSpace1: []}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  const ledger = root.querySelector('.hotkey-ledger');
  assert.ok(ledger, 'the rows share one grid so their columns align');
  const row = hotkeyRow(root, 'openCommandBar');
  assert.equal(row.parentElement, ledger);
  assert.ok(row.querySelector('.settings-row-label'));
  assert.ok(row.querySelector('.settings-row-value .combo'));
  assert.ok(row.querySelector('.hotkey-actions .hotkey-record'));
  assert.equal(
      hotkeyRow(root, 'goToSpace1').querySelector('.settings-row-value').textContent,
      'Unassigned');
});

test('record, clear and reset are icon buttons with accessible names', () => {
  const root = freshRoot();
  installSettings(root);
  // atriumSetHotkeys takes the shipped defaults as its second argument -
  // there is no separate atriumSetHotkeyDefaults global.
  window.atriumSetHotkeys({actions: {openCommandBar: ['cmd+k']}},
                          {actions: {openCommandBar: ['cmd+l']}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  const row = hotkeyRow(root, 'openCommandBar');
  for (const [sel, name] of [['.hotkey-record', 'Record'], ['.hotkey-clear', 'Clear'],
                             ['.hotkey-reset', 'Reset']]) {
    const b = row.querySelector(sel);
    assert.ok(b.querySelector('svg'), `${sel} carries an icon`);
    assert.equal(b.getAttribute('aria-label'), name);
    assert.equal(b.textContent.trim(), '', `${sel} has no visible text`);
  }
  row.querySelector('.hotkey-record').click();
  assert.equal(row.querySelector('.hotkey-record').getAttribute('aria-label'),
               'Press a key, Escape cancels');
});

function keydown(init) {
  document.dispatchEvent(new Event('keydown', {bubbles: true, ...init}));
}

test('recording a chord shows it on the row and sends the whole map', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {openCommandBar: []}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  h.sent.length = 0;

  hotkeyRow(root, 'openCommandBar').querySelector('.hotkey-record').click();
  keydown({key: 'k', code: 'KeyK', metaKey: true, shiftKey: true});

  assert.equal(
      hotkeyRow(root, 'openCommandBar').querySelector('.settings-row-value')
          .textContent,
      '⇧⌘K');
  const sent = h.sent.find(([name]) => name === 'settingsHotkeysChanged');
  assert.ok(sent, 'settingsHotkeysChanged was sent');
  const written = JSON.parse(sent[1][0]);
  assert.deepEqual(written.actions.openCommandBar, ['cmd+shift+k']);
});

test('a bare modifier keydown keeps the capture open for the real key', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {openCommandBar: []}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  h.sent.length = 0;

  const record = hotkeyRow(root, 'openCommandBar').querySelector('.hotkey-record');
  record.click();
  keydown({key: 'Meta', code: 'MetaLeft', metaKey: true});
  assert.equal(record.classList.contains('capturing'), true,
               'still capturing after a modifier-only keydown');
  assert.equal(h.sent.length, 0);

  keydown({key: 'k', code: 'KeyK', metaKey: true});
  assert.equal(h.sent.length, 1);
});

test('a chord without cmd or ctrl is refused at the control, not sent', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {openCommandBar: []}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  h.sent.length = 0;

  hotkeyRow(root, 'openCommandBar').querySelector('.hotkey-record').click();
  keydown({key: 'k', code: 'KeyK', shiftKey: true});

  assert.equal(h.sent.length, 0, 'a chord with no cmd/ctrl is never sent');
  assert.match(hotkeyRow(root, 'openCommandBar')
      .querySelector('.hotkey-error').textContent, /needs ⌘ or Ctrl/);
});

test('Escape with no other modifier cancels a capture without sending', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {openCommandBar: []}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  h.sent.length = 0;

  const record = hotkeyRow(root, 'openCommandBar').querySelector('.hotkey-record');
  record.click();
  keydown({key: 'Escape', code: 'Escape'});

  assert.equal(record.classList.contains('capturing'), false);
  assert.equal(h.sent.length, 0);
});

test('Clear sends an empty list and the row reads Unassigned', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {goToSpace1: ['cmd+1']}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  h.sent.length = 0;

  hotkeyRow(root, 'goToSpace1').querySelector('.hotkey-clear').click();

  assert.equal(
      hotkeyRow(root, 'goToSpace1').querySelector('.settings-row-value')
          .textContent,
      'Unassigned');
  const sent = h.sent.find(([name]) => name === 'settingsHotkeysChanged');
  assert.deepEqual(JSON.parse(sent[1][0]).actions.goToSpace1, []);
});

test('Clear is disabled for an action that already has no combo', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {openCommandBar: []}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  assert.equal(
      hotkeyRow(root, 'openCommandBar').querySelector('.hotkey-clear').disabled,
      true);
});

test('a chord already bound to another action is refused, not sent', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {goToSpace1: ['cmd+1'], openShelf: []}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  h.sent.length = 0;

  hotkeyRow(root, 'openShelf').querySelector('.hotkey-record').click();
  keydown({key: '1', code: 'Digit1', metaKey: true});

  assert.equal(h.sent.find(([name]) => name === 'settingsHotkeysChanged'), undefined,
      'the clashing bind must not be sent');
  assert.match(hotkeyRow(root, 'openShelf').querySelector('.hotkey-error').textContent,
      /already used by/);
});

test('the clash warning keeps working after a rebind repaints the pane', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {goToSpace1: ['cmd+1'], openShelf: []}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();

  hotkeyRow(root, 'openShelf').querySelector('.hotkey-record').click();
  keydown({key: '1', code: 'Digit1', metaKey: true});
  // The browser's own push after a successful write, same as any other pane.
  window.atriumSetHotkeys({actions: {goToSpace1: ['cmd+1'], openShelf: ['cmd+1']}});

  assert.equal(root.querySelectorAll('.settings-row.conflict').length, 2,
               'both rows are still marked after the repaint');
});

test('a failed hotkeys save reverts the pane to the pre-edit bindings', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {openCommandBar: []}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();

  hotkeyRow(root, 'openCommandBar').querySelector('.hotkey-record').click();
  keydown({key: 'k', code: 'KeyK', metaKey: true});
  assert.equal(
      hotkeyRow(root, 'openCommandBar').querySelector('.settings-row-value')
          .textContent,
      '⌘K');

  window.atriumSetHotkeysSaveResult(-1);
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  assert.equal(
      hotkeyRow(root, 'openCommandBar').querySelector('.settings-row-value')
          .textContent,
      'Unassigned');
});

test('settingsHotkeysChanged is a registered surface message', () => {
  assert.ok(REGISTERED.has('settingsHotkeysChanged'));
});

// --- putting a shortcut back on the chord Atrium ships ---

// The two maps the browser pushes together: what is bound now, and what the
// build ships. openCommandBar is rebound, toggleSidebar is untouched.
const SHIPPED = {actions: {openCommandBar: ['cmd+t'], toggleSidebar: ['cmd+s']}};

function inputsPane(bound, shipped = SHIPPED) {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys(bound, shipped);
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  h.sent.length = 0;
  return root;
}

test('only a row that differs from its shipped chord offers a reset', () => {
  const root = inputsPane(
      {actions: {openCommandBar: ['cmd+shift+k'], toggleSidebar: ['cmd+s']}});

  assert.ok(hotkeyRow(root, 'openCommandBar').querySelector('.hotkey-reset'),
            'the rebound row offers one');
  assert.equal(hotkeyRow(root, 'toggleSidebar').querySelector('.hotkey-reset'),
               null, 'the row still on its default has nothing to put back');
});

// Record appends to the row, so clearing a multi-combo action and putting the
// same chords back is a normal way to reach the shipped set in another order.
test('a row holding the shipped chords in another order is still shipped', () => {
  const root = inputsPane({actions: {nextTab: ['cmd+j', 'cmd+opt+down']}},
                          {actions: {nextTab: ['cmd+opt+down', 'cmd+j']}});

  assert.equal(hotkeyRow(root, 'nextTab').querySelector('.hotkey-reset'), null);
});

test('an unbound row offers a reset, since the unbind is the user edit', () => {
  const root = inputsPane(
      {actions: {openCommandBar: [], toggleSidebar: ['cmd+s']}});

  assert.ok(hotkeyRow(root, 'openCommandBar').querySelector('.hotkey-reset'));
});

test('a row reset names the action to forget and shows the shipped chord', () => {
  const root = inputsPane(
      {actions: {openCommandBar: ['cmd+shift+k'], toggleSidebar: ['cmd+s']}});

  hotkeyRow(root, 'openCommandBar').querySelector('.hotkey-reset').click();

  const sent = h.sent.find(([name]) => name === 'settingsHotkeysChanged');
  assert.ok(sent, 'settingsHotkeysChanged was sent');
  const written = JSON.parse(sent[1][0]);
  assert.equal(written.forget, 'openCommandBar',
               'the core drops the entry, the pane does not write cmd+t back');
  assert.equal(written.forgetAll, undefined);
  assert.equal(
      hotkeyRow(root, 'openCommandBar').querySelector('.settings-row-value')
          .textContent,
      '⌘T');
  assert.equal(hotkeyRow(root, 'openCommandBar').querySelector('.hotkey-reset'),
               null, 'the row is back at its default, so the control goes');
});

test('resetting a row of an action that ships unassigned drops the row', () => {
  const root = inputsPane({actions: {createSpace: ['cmd+shift+n']}},
                          {actions: {toggleSidebar: ['cmd+s']}});

  hotkeyRow(root, 'createSpace').querySelector('.hotkey-reset').click();

  const written = JSON.parse(
      h.sent.find(([name]) => name === 'settingsHotkeysChanged')[1][0]);
  assert.equal(written.forget, 'createSpace');
  assert.equal(hotkeyRow(root, 'createSpace'), null);
});

test('the pane reset asks before it sends', () => {
  const root = inputsPane(
      {actions: {openCommandBar: ['cmd+shift+k'], toggleSidebar: ['cmd+s']}});

  root.querySelector('.hotkey-reset-all').click();
  assert.ok(root.querySelector('.hotkey-reset-question'), 'it asks');
  assert.equal(document.activeElement,
               root.querySelector('.hotkey-reset-all-cancel'),
               'cancel holds the focus - a held Enter repeats its click, and ' +
               'the repeat must not land on the confirm');
  assert.equal(root.querySelector('.hotkey-reset-all'), null,
               'the un-armed button is gone while the question stands');
  assert.equal(h.sent.filter(([n]) => n === 'settingsHotkeysChanged').length, 0,
               'nothing is written on the way to the question');

  root.querySelector('.hotkey-reset-all-cancel').click();
  assert.ok(root.querySelector('.hotkey-reset-all'), 'cancel puts it back');
  assert.equal(h.sent.filter(([n]) => n === 'settingsHotkeysChanged').length, 0);
});

test('Escape stands the pane reset down, the same as Cancel', () => {
  const root = inputsPane(
      {actions: {openCommandBar: ['cmd+shift+k'], toggleSidebar: ['cmd+s']}});
  root.querySelector('.hotkey-reset-all').click();

  root.querySelector('.hotkey-reset-all-cancel').dispatchEvent(
      new Event('keydown', {bubbles: true, key: 'Escape'}));

  assert.ok(root.querySelector('.hotkey-reset-all'), 'the ask button is back');
  assert.equal(root.querySelector('.hotkey-reset-question'), null);
  assert.equal(h.sent.filter(([n]) => n === 'settingsHotkeysChanged').length, 0,
               'standing down writes nothing');
});

// The window clears the search field on Escape, and it must not also do that
// while the question is the thing Escape is aimed at.
test('Escape on the confirm step does not reach the window behind it', () => {
  const root = inputsPane(
      {actions: {openCommandBar: ['cmd+shift+k'], toggleSidebar: ['cmd+s']}});
  const field = root.querySelector('.settings-search-field');
  field.value = 'side';
  field.dispatchEvent(new Event('input', {bubbles: true}));
  root.querySelector('.hotkey-reset-all').click();

  root.querySelector('.hotkey-reset-all-cancel').dispatchEvent(
      new Event('keydown', {bubbles: true, key: 'Escape'}));

  assert.equal(root.querySelector('.settings-search-field').value, 'side',
               'the query survived - the row answered Escape, not the window');
});

test('a confirmed pane reset forgets every action and shows the defaults', () => {
  const root = inputsPane(
      {actions: {openCommandBar: ['cmd+shift+k'], toggleSidebar: []}});

  root.querySelector('.hotkey-reset-all').click();
  root.querySelector('.hotkey-reset-all-confirm').click();

  const written = JSON.parse(
      h.sent.find(([name]) => name === 'settingsHotkeysChanged')[1][0]);
  assert.equal(written.forgetAll, true);
  assert.equal(written.forget, undefined);
  assert.equal(
      hotkeyRow(root, 'openCommandBar').querySelector('.settings-row-value')
          .textContent,
      '⌘T');
  assert.equal(
      hotkeyRow(root, 'toggleSidebar').querySelector('.settings-row-value')
          .textContent,
      '⌘S');
  assert.equal(root.querySelectorAll('.hotkey-reset').length, 0,
               'no row differs from its default any more');
});

test('an ordinary rebind still forgets nothing', () => {
  const root = inputsPane({actions: {openCommandBar: ['cmd+t']}});

  hotkeyRow(root, 'openCommandBar').querySelector('.hotkey-clear').click();

  const written = JSON.parse(
      h.sent.find(([name]) => name === 'settingsHotkeysChanged')[1][0]);
  assert.equal(written.forget, undefined);
  assert.equal(written.forgetAll, undefined);
});

// A build whose browser half predates the defaults push must not offer a
// control it cannot make good on.
test('no defaults pushed means no reset controls at all', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {openCommandBar: ['cmd+shift+k']}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();

  assert.equal(root.querySelectorAll('.hotkey-reset').length, 0);
  assert.equal(root.querySelector('.hotkey-reset-all'), null);
});

// --- the preset picker ---

// The two tables the browser pushes alongside the bound map. Arc leaves the
// command bar unassigned, which is the difference every case below drives.
const PRESETS = {
  arc: {openCommandBar: [], toggleSidebar: ['cmd+s'], nextTab: ['cmd+shift+]']},
  atrium: {
    openCommandBar: ['cmd+t'],
    toggleSidebar: ['cmd+s'],
    nextTab: ['cmd+opt+down'],
  },
};

function presetPane(bound) {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys(bound, {actions: PRESETS.atrium}, PRESETS);
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  h.sent.length = 0;
  return root;
}

function hotkeyPresetButton(root, name) {
  return root.querySelector(`.hotkey-preset[data-preset="${name}"]`);
}

function onPreset(root) {
  return root.querySelector('.hotkey-preset.on').dataset.preset;
}

test('the picker offers Arc, Atrium and Custom', () => {
  const root = presetPane({actions: PRESETS.atrium});

  assert.deepEqual(
      [...root.querySelectorAll('.hotkey-preset')].map((b) => b.dataset.preset),
      ['arc', 'atrium', 'custom']);
  assert.equal(onPreset(root), 'atrium');
});

test('a file that names a preset shows that preset, not the shipped one', () => {
  const root = presetPane({actions: PRESETS.arc, preset: 'arc'});

  assert.equal(onPreset(root), 'arc');
  assert.equal(
      hotkeyRow(root, 'openCommandBar').querySelector('.settings-row-value')
          .textContent,
      'Unassigned');
});

// Custom is a state the pane reads off the rows, never a table. Nothing to
// apply, so the option is there to be read and not to be picked.
test('Custom cannot be picked, it is what an edited row makes the pane', () => {
  const root = presetPane({actions: PRESETS.atrium});

  assert.equal(hotkeyPresetButton(root, 'custom').disabled, true);
});

test('a row away from the named preset reads the picker as Custom', () => {
  const root = presetPane(
      {actions: {...PRESETS.arc, toggleSidebar: ['cmd+shift+s']}, preset: 'arc'});

  assert.equal(onPreset(root), 'custom');
  assert.equal(hotkeyPresetButton(root, 'custom').disabled, false,
               'the option the pane is actually on is never disabled');
});

test('picking a preset applies its chords to every row and names it', () => {
  const root = presetPane({actions: PRESETS.atrium});

  hotkeyPresetButton(root, 'arc').click();

  const written = JSON.parse(
      h.sent.find(([name]) => name === 'settingsHotkeysChanged')[1][0]);
  assert.equal(written.preset, 'arc');
  assert.equal(written.forgetAll, true,
               'the user map goes, or its rows would keep winning over Arc');
  assert.equal(onPreset(root), 'arc');
  assert.equal(
      hotkeyRow(root, 'nextTab').querySelector('.settings-row-value').textContent,
      '⇧⌘]');
});

// Nothing of the user's is at stake between two presets with an empty map, so
// asking would be a question with one answer.
test('a preset to preset switch does not ask', () => {
  const root = presetPane({actions: PRESETS.atrium});

  hotkeyPresetButton(root, 'arc').click();

  assert.equal(root.querySelector('.hotkey-preset-question'), null);
  assert.equal(h.sent.filter(([n]) => n === 'settingsHotkeysChanged').length, 1);
});

test('leaving Custom asks before it discards the user rows', () => {
  const root = presetPane(
      {actions: {...PRESETS.atrium, toggleSidebar: ['cmd+shift+s']}});

  hotkeyPresetButton(root, 'arc').click();

  assert.ok(root.querySelector('.hotkey-preset-question'), 'it asks');
  assert.equal(h.sent.filter(([n]) => n === 'settingsHotkeysChanged').length, 0,
               'nothing is written on the way to the question');
  assert.equal(document.activeElement,
               root.querySelector('.hotkey-preset-cancel'),
               'cancel holds the focus, the same rule the pane reset follows');

  root.querySelector('.hotkey-preset-confirm').click();

  const written = JSON.parse(
      h.sent.find(([name]) => name === 'settingsHotkeysChanged')[1][0]);
  assert.equal(written.preset, 'arc');
  assert.equal(written.forgetAll, true);
  assert.equal(onPreset(root), 'arc');
});

test('cancelling the switch keeps the preset and the user rows', () => {
  const root = presetPane(
      {actions: {...PRESETS.atrium, toggleSidebar: ['cmd+shift+s']}});

  hotkeyPresetButton(root, 'arc').click();
  root.querySelector('.hotkey-preset-cancel').click();

  assert.equal(h.sent.filter(([n]) => n === 'settingsHotkeysChanged').length, 0);
  assert.equal(onPreset(root), 'custom');
  assert.equal(
      hotkeyRow(root, 'toggleSidebar').querySelector('.settings-row-value')
          .textContent,
      '⇧⌘S');
});

test('Escape stands the switch down, the same as Cancel', () => {
  const root = presetPane(
      {actions: {...PRESETS.atrium, toggleSidebar: ['cmd+shift+s']}});
  const field = root.querySelector('.settings-search-field');
  field.value = 'side';
  field.dispatchEvent(new Event('input', {bubbles: true}));
  hotkeyPresetButton(root, 'arc').click();

  root.querySelector('.hotkey-preset-cancel').dispatchEvent(
      new Event('keydown', {bubbles: true, key: 'Escape'}));

  assert.ok(hotkeyPresetButton(root, 'arc'), 'the picker is back');
  assert.equal(root.querySelector('.hotkey-preset-question'), null);
  assert.equal(h.sent.filter(([n]) => n === 'settingsHotkeysChanged').length, 0);
  assert.equal(root.querySelector('.settings-search-field').value, 'side',
               'the query survived - the row answered Escape, not the window');
});

test('editing a row under a preset switches the picker to Custom', () => {
  const root = presetPane({actions: PRESETS.arc, preset: 'arc'});

  hotkeyRow(root, 'toggleSidebar').querySelector('.hotkey-clear').click();

  assert.equal(onPreset(root), 'custom');
  const written = JSON.parse(
      h.sent.find(([name]) => name === 'settingsHotkeysChanged')[1][0]);
  assert.equal(written.preset, 'arc',
               'the file keeps naming the table the rows are merged over');
  assert.equal(written.forgetAll, undefined);
  assert.equal(
      hotkeyRow(root, 'nextTab').querySelector('.settings-row-value').textContent,
      '⇧⌘]', 'the rows the edit did not touch are untouched');
});

// A reset returns the row to the SELECTED preset, which is the whole reason
// the file records a name rather than a copy of the chords.
test('a row reset under a preset offers that preset chord back', () => {
  const root = presetPane(
      {actions: {...PRESETS.arc, nextTab: ['cmd+shift+y']}, preset: 'arc'});

  assert.equal(hotkeyRow(root, 'nextTab').querySelector('.hotkey-reset').title,
               'Back to cmd+shift+]');

  hotkeyRow(root, 'nextTab').querySelector('.hotkey-reset').click();

  assert.equal(
      hotkeyRow(root, 'nextTab').querySelector('.settings-row-value').textContent,
      '⇧⌘]');
  assert.equal(onPreset(root), 'arc', 'the last user row went, so did Custom');
});

test('a failed save puts the preset back with the bindings', () => {
  const root = presetPane({actions: PRESETS.atrium});

  hotkeyPresetButton(root, 'arc').click();
  window.atriumSetHotkeysSaveResult(-1);

  assert.equal(onPreset(root), 'atrium');
  assert.equal(
      hotkeyRow(root, 'nextTab').querySelector('.settings-row-value').textContent,
      '⌥⌘↓');
});

// A browser half that predates the preset push has nothing to draw a picker
// from, and the per-row reset must keep working without one.
test('no presets pushed means no picker', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {openCommandBar: ['cmd+shift+k']}},
                          {actions: PRESETS.atrium});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();

  assert.equal(root.querySelector('.hotkey-preset'), null);
  assert.ok(hotkeyRow(root, 'openCommandBar').querySelector('.hotkey-reset'));
});

test('a re-render while recording removes the capture listener', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {openCommandBar: []}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  hotkeyRow(root, 'openCommandBar').querySelector('.hotkey-record').click();

  root.querySelector('.settings-rail-item[data-pane="general"]').click();
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  h.sent.length = 0;
  keydown({key: 'k', code: 'KeyK', metaKey: true});

  assert.equal(h.sent.length, 0,
      'the listener from the pane before the rebuild must not still be live');
});

test('Record on a second row cancels the first row\'s capture', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {openCommandBar: [], goToSpace1: []}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();

  const firstRecord = hotkeyRow(root, 'openCommandBar').querySelector('.hotkey-record');
  const secondRecord = hotkeyRow(root, 'goToSpace1').querySelector('.hotkey-record');
  firstRecord.click();
  secondRecord.click();
  assert.equal(firstRecord.classList.contains('capturing'), false,
      'the first row gave up capture when the second one started');
  assert.equal(secondRecord.classList.contains('capturing'), true);

  h.sent.length = 0;
  keydown({key: 'k', code: 'KeyK', metaKey: true});
  const sent = h.sent.find(([name]) => name === 'settingsHotkeysChanged');
  const written = JSON.parse(sent[1][0]);
  assert.deepEqual(written.actions.goToSpace1, ['cmd+k']);
  assert.deepEqual(written.actions.openCommandBar, [],
      'only the row still capturing took the chord');
});

test('a recorded chord does not also move rail focus', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {openCommandBar: []}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  hotkeyRow(root, 'openCommandBar').querySelector('.hotkey-record').click();

  const before = [...root.querySelectorAll('.settings-rail-item')]
      .findIndex((n) => n.classList.contains('selected'));
  // railKey keys off event.target.closest('.settings-rail-item'), so the
  // chord has to land on a rail item - the same element real key-equivalent
  // delivery would use once it is focused - not merely on document.
  const selectedItem = root.querySelectorAll('.settings-rail-item')[before];
  selectedItem.dispatchEvent(new Event(
      'keydown',
      {bubbles: true, key: 'ArrowDown', code: 'ArrowDown', metaKey: true}));
  const after = [...root.querySelectorAll('.settings-rail-item')]
      .findIndex((n) => n.classList.contains('selected'));
  assert.equal(after, before,
      'the capture must stop the chord reaching the rail\'s own arrow handling');
});

test('a reserved combo is refused at the control, mirroring IsReservedHotkeyCombo', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {openCommandBar: []}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  h.sent.length = 0;

  hotkeyRow(root, 'openCommandBar').querySelector('.hotkey-record').click();
  keydown({key: 'w', code: 'KeyW', metaKey: true, shiftKey: true});

  assert.equal(h.sent.length, 0, 'cmd+shift+w is reserved and must not be sent');
  assert.match(hotkeyRow(root, 'openCommandBar')
      .querySelector('.hotkey-error').textContent, /reserved/);
});

test('a combo already bound to another action is refused, not silently swapped', () => {
  const root = freshRoot();
  installSettings(root);
  window.atriumSetHotkeys({actions: {openCommandBar: ['cmd+k'], goToSpace1: []}});
  root.querySelector('.settings-rail-item[data-pane="inputs"]').click();
  h.sent.length = 0;

  hotkeyRow(root, 'goToSpace1').querySelector('.hotkey-record').click();
  keydown({key: 'k', code: 'KeyK', metaKey: true});

  assert.equal(h.sent.length, 0,
      'cmd+k already belongs to openCommandBar and must not be sent for goToSpace1');
  assert.match(hotkeyRow(root, 'goToSpace1').querySelector('.hotkey-error').textContent,
      /already used by/);
});
