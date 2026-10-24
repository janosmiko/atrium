#!/usr/bin/env node --test
// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// the /commandbar route - what a keystroke means and what it sends.
//
//   node --test build/scripts/commandbar-test.mjs
//
// The command bar is the first surface where the KEYBOARD is the whole
// interaction, so the two failure modes the sidebar suite watches for both
// change shape here:
//
//   selection  Enter must accept the row the user can see highlighted. The
//              list is rebuilt on every keystroke, so a selection tracked by
//              row position silently follows whatever now sits in that slot.
//              Every accept therefore carries the match's OWN index, the one
//              the browser handed us, not its position in the list.
//   payload    an unregistered chrome.send name is FATAL in the BROWSER
//              process, so the names are read from kSurfaceMessages rather
//              than copied here.

import {test} from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {installDOM, Event} from './dom-shim.mjs';

const SRC = process.env.ATRIUM_CHROMIUM_SRC ||
    path.join(os.homedir(), 'chromium-atrium/src');

const h = installDOM({pathname: '/commandbar'});

const {installCommandBar} =
    await import(path.join(SRC, 'chrome/browser/resources/atrium/commandbar.js'));

const REGISTERED = new Set(
    fs.readFileSync(
          path.join(SRC, 'chrome/browser/ui/atrium/atrium_surface_host.h'),
          'utf8')
        .split('kSurfaceMessages[] = {')[1]
        .split('};')[0]
        .match(/"[^"]+"/g)
        .map((quoted) => quoted.slice(1, -1)));

// Two history entries and one search suggestion, which is the ordinary shape:
// the top match is deletable (it came from the user's own history), the search
// suggestion is not.
const MATCHES = [
  {index: 0, title: 'Atrium', subtitle: 'https://atrium.example/', kind: 'history', deletable: true},
  {index: 1, title: 'Atrium docs', subtitle: 'https://docs.example/', kind: 'history', deletable: true},
  {index: 2, title: 'atrium browser', subtitle: 'Search', kind: 'search', deletable: false},
];

const root = document.createElement('div');
document.body.appendChild(root);
installCommandBar(root);

const READY = h.sent.splice(0, h.sent.length);

function reset() {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar({query: 'atr', matches: MATCHES});
  h.sent.length = 0;
}

const rows = () => root.querySelectorAll('.cbrow');
const selectedRow = () => root.querySelector('.cbrow.cbsel');
const input = () => root.querySelector('.cbinput');
const key = (name, init = {}) => input().dispatchEvent(
    new Event('keydown', {bubbles: true, key: name, ...init}));
const click = (el) => el.dispatchEvent(new Event('click', {bubbles: true}));

test('install announces the renderer is live', () => {
  assert.deepEqual(READY, [['commandBarReady', []]]);
});

test('every name it sends is registered in kSurfaceMessages', () => {
  reset();
  input().value = 'at';
  input().dispatchEvent(new Event('input', {bubbles: true}));
  key('ArrowDown');
  key('Enter');
  key('Escape');
  window.atriumSetCommandBar({query: 'at', matches: MATCHES});
  key('Delete', {shiftKey: true});
  const names = new Set(h.sent.map((m) => m[0]));
  assert.notEqual(names.size, 0);
  for (const name of names) {
    assert.ok(REGISTERED.has(name), `${name} is not in kSurfaceMessages`);
  }
});

test('typing asks the browser for matches', () => {
  reset();
  input().value = 'atri';
  input().dispatchEvent(new Event('input', {bubbles: true}));
  assert.deepEqual(h.sent, [['commandBarQuery', ['atri']]]);
});

test('a push renders one row per match', () => {
  reset();
  assert.equal(rows().length, 3);
  assert.equal(rows()[1].querySelector('.cbtitle').textContent, 'Atrium docs');
  assert.equal(rows()[1].querySelector('.cbsub').textContent,
      'https://docs.example/');
});

test('the top match is selected before the user touches anything', () => {
  reset();
  assert.equal(selectedRow(), rows()[0]);
});

test('the arrows move the selection and wrap at both ends', () => {
  reset();
  key('ArrowDown');
  assert.equal(selectedRow(), rows()[1]);
  key('ArrowDown');
  key('ArrowDown');
  assert.equal(selectedRow(), rows()[0]);
  key('ArrowUp');
  assert.equal(selectedRow(), rows()[2]);
});

// Every pass now reaches the page, finished or not, because a pass
// the controller's stop timer ends notifies nobody and its rows were lost. That
// makes several pushes land for one query, so the highlight has to survive them
// - otherwise an arrow key is undone by the next pass a fraction of a second
// later.
test('a later pass for the same query keeps the row the user chose', () => {
  reset();
  key('ArrowDown');
  // The same query, answered again and re-ranked: the chosen row moves to the
  // BOTTOM of the list, so a highlight that followed the slot would land on a
  // different row and one that reset would land on the first.
  window.atriumSetCommandBar(
      {query: 'atr', matches: [MATCHES[0], MATCHES[2], MATCHES[1]]});
  assert.equal(
      selectedRow().querySelector('.cbtitle').textContent, 'Atrium docs');
});

test('a pass for a new query goes back to the top match', () => {
  reset();
  key('ArrowDown');
  window.atriumSetCommandBar({query: 'atri', matches: MATCHES});
  assert.equal(selectedRow(), rows()[0]);
});

test('Enter accepts the match index, not the row position', () => {
  reset();
  // A second push with the top match dropped: row 0 now holds index 1.
  window.atriumSetCommandBar({query: 'atr', matches: MATCHES.slice(1)});
  h.sent.length = 0;
  key('Enter');
  assert.deepEqual(h.sent, [['commandBarAccept', [1]]]);
});

test('Escape dismisses', () => {
  reset();
  key('Escape');
  assert.deepEqual(h.sent, [['commandBarDismiss', []]]);
});

test('a click on a row accepts that row', () => {
  reset();
  click(rows()[2]);
  assert.deepEqual(h.sent, [['commandBarAccept', [2]]]);
});

test('a click outside the card dismisses, a click on the card does not', () => {
  reset();
  click(root.querySelector('.cbscrim'));
  assert.deepEqual(h.sent, [['commandBarDismiss', []]]);
  h.sent.length = 0;
  click(root.querySelector('.cbcard'));
  assert.deepEqual(h.sent, []);
});

test('Shift+Delete removes a deletable match and only a deletable one', () => {
  reset();
  key('Delete', {shiftKey: true});
  assert.deepEqual(h.sent, [['commandBarDeleteMatch', [0]]]);

  h.sent.length = 0;
  key('ArrowDown');
  key('ArrowDown');
  key('Delete', {shiftKey: true});
  assert.deepEqual(h.sent, []);
});

test('Enter with no matches sends nothing', () => {
  reset();
  window.atriumSetCommandBar({query: '', matches: []});
  h.sent.length = 0;
  key('Enter');
  assert.deepEqual(h.sent, []);
});

test('reset clears the field and the list for the next open', () => {
  reset();
  input().value = 'stale';
  window.atriumResetCommandBar();
  assert.equal(input().value, '');
  assert.equal(rows().length, 0);
});

// --- favourites in the palette -------------------------------------

const FAVORITES = [
  {title: 'Atrium Docs', url: 'https://docs.example/', kind: 'favorite'},
];

test('favourites lead the list, ahead of what the omnibox ranked', () => {
  // They are the user's own list. Burying them under the top autocomplete guess
  // would make the section decorative.
  window.atriumResetCommandBar();
  window.atriumSetCommandBar(
      {query: 'atr', favorites: FAVORITES, matches: MATCHES});
  h.sent.length = 0;
  assert.equal(rows()[0].querySelector('.cbtitle').textContent, 'Atrium Docs');
  assert.equal(rows().length, FAVORITES.length + MATCHES.length);
});

test('a favourite row is marked and labelled, an autocomplete row is not', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar(
      {query: 'atr', favorites: FAVORITES, matches: MATCHES});
  h.sent.length = 0;
  const fav = rows()[0];
  assert.equal(fav.classList.contains('cbfav'), true);
  assert.equal(fav.querySelector('.cbsub').textContent, 'Favorites');
  assert.equal(rows()[1].classList.contains('cbfav'), false);
});

test('a favourite row carries a url and NO index', () => {
  // The index goes straight back to the browser's AutocompleteResult, and a
  // favourite is not in it - carrying one would accept an unrelated row.
  window.atriumResetCommandBar();
  window.atriumSetCommandBar({query: 'atr', favorites: FAVORITES, matches: []});
  h.sent.length = 0;
  assert.equal(rows()[0].dataset.cbUrl, 'https://docs.example/');
  assert.equal(rows()[0].dataset.cbIndex, undefined);
});

test('accepting a favourite sends its url, not an index', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar(
      {query: 'atr', favorites: FAVORITES, matches: MATCHES});
  h.sent.length = 0;
  key('Enter');
  assert.deepEqual(
      h.sent, [['commandBarFavoriteAccept', ['https://docs.example/']]]);
});

test('accepting an autocomplete row below a favourite still sends its index', () => {
  // The favourite shifts every autocomplete row down by one position. Position
  // and index are different numbers now, and confusing them would open the
  // wrong page.
  window.atriumResetCommandBar();
  window.atriumSetCommandBar(
      {query: 'atr', favorites: FAVORITES, matches: MATCHES});
  h.sent.length = 0;
  key('ArrowDown');
  key('Enter');
  assert.deepEqual(h.sent, [['commandBarAccept', [0]]]);
});

test('every name the favourite path sends is registered with the browser', () => {
  assert.ok(REGISTERED.has('commandBarFavoriteAccept'));
});

// --- verb commands preview in place -------------------------------

const TAB_ROWS = [
  {title: 'Inbox', url: 'https://mail.example/'},
  {title: 'Team chat', url: 'https://team.slack.com/'},
];

function typeInto(text) {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar({query: '', matches: MATCHES, tabs: TAB_ROWS});
  h.sent.length = 0;
  input().value = text;
  input().dispatchEvent(new Event('input', {bubbles: true}));
}

test('a verb previews what it would act on, in place of the matches', () => {
  typeInto('close chat');
  const preview = root.querySelector('.cbcmd');
  assert.ok(preview, 'no command preview rendered');
  assert.match(preview.textContent, /Team chat/);
  assert.equal(rows().length, 1, 'the autocomplete rows must not sit under it');
});

test('a verb command asks the browser for nothing', () => {
  // Sending the query anyway would race an autocomplete pass in behind the
  // preview and replace it.
  typeInto('close chat');
  assert.deepEqual(h.sent, []);
});

test('a miss previews the reason and is marked unrunnable', () => {
  typeInto('close nothing-here');
  const preview = root.querySelector('.cbcmd');
  assert.ok(preview.classList.contains('cbcmdbad'));
  assert.match(preview.textContent, /nothing matches/);
});

test('ordinary text still queries the browser and shows no preview', () => {
  // The palette's URL and search handling has to be untouched by any of this.
  typeInto('example.com');
  assert.equal(root.querySelector('.cbcmd'), null);
  assert.deepEqual(h.sent, [['commandBarQuery', ['example.com']]]);
});

// --- a Settings row the palette resolves itself ---------------

const titles = () => [...rows()].map((r) => r.querySelector('.cbtitle')?.textContent);

// The action rows ride the browser's pass, the way favourites do: the query
// comes back on the snapshot and the palette adds its own rows to it.
// One ANSWERED pass: the omnibox finished with this query. `final` is what
// lets the page draw its "nothing matched" row, so a helper standing for an
// answer has to carry it.
function pass(query, extra = {}) {
  window.atriumResetCommandBar();
  // The browser sends the verbs on every open now. ACTION_CATALOG at the foot
  // of this file is what it sends.
  window.atriumSetCommandBarActions({actions: ACTION_CATALOG});
  window.atriumSetCommandBar({query, matches: MATCHES, final: true, ...extra});
  h.sent.length = 0;
}

test('typing settings offers a Settings row', () => {
  pass('settings');
  assert.ok(titles().includes('Settings'), titles().join(' | '));
});

test('a prefix of settings offers it too', () => {
  pass('sett');
  assert.ok(titles().includes('Settings'));
});

test('the Settings row leads, ahead of whatever the omnibox ranked', () => {
  // It is the exact thing the user named. Burying it under a search suggestion
  // for the word "settings" would make it decorative.
  pass('settings');
  assert.equal(titles()[0], 'Settings');
});

test('accepting Settings opens the window, and asks for no pane', () => {
  pass('settings');
  const row = [...rows()].find(
      (r) => r.querySelector('.cbtitle')?.textContent === 'Settings');
  row.dispatchEvent(new Event('click', {bubbles: true}));
  assert.deepEqual(h.sent.filter(([name]) => name === 'openSettingsPane'),
                   [['openSettingsPane', ['']]]);
});

test('ordinary text offers no Settings row', () => {
  pass('example.com');
  assert.equal(titles().includes('Settings'), false);
});

test('an empty palette offers no action rows', () => {
  // A freshly opened palette shows the user's own list, not a menu.
  pass('');
  assert.equal(titles().includes('Settings'), false);
});

test('each verb it offers names a message the browser registers', () => {
  // The vocabulary rule as a test: a row that cannot reach a handler is a row
  // that reads live and does nothing, which is the defect this suite grew for.
  for (const query of ['settings', 'courtyard', 'overview', 'new space',
                       'capture viewport', 'capture region',
                       'capture full page']) {
    pass(query);
    const row = [...rows()].find((r) => r.classList.contains('cbaction'));
    assert.ok(row, `no action row for "${query}"`);
    row.dispatchEvent(new Event('click', {bubbles: true}));
    const sent = h.sent.map(([name]) => name).filter((n) => !n.startsWith('commandBar'));
    assert.equal(sent.length, 1, `"${query}" sent ${sent.length} messages`);
    assert.ok(REGISTERED.has(sent[0]), `"${sent[0]}" is not registered`);
  }
});

test('one screenshot word offers all three capture verbs', () => {
  // The core and the region overlay shipped with no caller at all: the command
  // bar that drove them was Swift, and the HTML palette never got the verbs. A
  // user reaching for a screenshot does not know which of the three they want,
  // so the shared word has to offer the choice rather than one guess.
  pass('screenshot');
  const offered = titles().filter((t) => t.startsWith('Capture '));
  assert.deepEqual(offered.sort(),
                   ['Capture Full Page', 'Capture Region', 'Capture Viewport']);
});

test('each capture verb is reachable by its own words too', () => {
  for (const [query, title] of [['crop', 'Capture Region'],
                                ['visible', 'Capture Viewport'],
                                ['whole', 'Capture Full Page']]) {
    pass(query);
    assert.ok(titles().includes(title),
              `"${query}" offered ${titles().join(' | ')}`);
  }
});

test('README names the same verbs the palette offers', () => {
  // The doc rots the moment a verb is renamed or dropped, and a palette verb is
  // the only place a user finds these - there is no menu entry for them. So the
  // table is read rather than trusted.
  const readme = fs.readFileSync(
      path.join(path.dirname(new URL(import.meta.url).pathname), '../../README.md'),
      'utf8');
  const table = readme.split('## Command palette')[1].split('##')[0];
  const documented = [...table.matchAll(/^\| ([A-Z][^|]*?) \|/gm)]
      .map((m) => m[1].trim())
      .filter((name) => name !== 'Verb');
  assert.notEqual(documented.length, 0, 'no verb rows found in the README table');

  // Every documented verb must be offered, by a word the README itself names.
  for (const title of documented) {
    pass(title.toLowerCase());
    assert.ok(titles().includes(title),
              `README lists "${title}" but the palette does not offer it`);
  }

  // And the other way: a verb the palette offers and the README omits is a verb
  // nobody can discover from the docs.
  pass('screenshot');
  for (const offered of titles().filter((t) => t.startsWith('Capture '))) {
    assert.ok(documented.includes(offered),
              `the palette offers "${offered}" and the README does not name it`);
  }
});

test('a verb is reachable by any of its words, not just its first', () => {
  // The old matcher took one whole-string prefix, so a two-word verb could not
  // be found by its second word and "new sp" found nothing at all.
  pass('space');
  assert.ok(titles().includes('New Space'));
  pass('new sp');
  assert.ok(titles().includes('New Space'));
  pass('pref');
  assert.ok(titles().includes('Settings'));
});

test('a word that matches nothing says so instead of staying silent', () => {
  // The field holds the text the pass answers, as it does in a real palette -
  // the row is deliberately withheld when the two disagree.
  pass('qqzz', {matches: []});
  input().value = 'qqzz';
  window.atriumSetCommandBar({query: 'qqzz', matches: [], final: true});
  const row = root.querySelector('.cbrow.cbempty');
  assert.ok(row, 'no empty-state row');
  assert.match(row.querySelector('.cbtitle').textContent, /qqzz/);
});

test('the empty-state row stays away until a pass has answered', () => {
  // Between a keystroke and its pass the list is legitimately empty. Saying
  // "no match" there would flash on every word the user types.
  window.atriumResetCommandBar();
  input().value = 'qq';
  assert.equal(root.querySelector('.cbrow.cbempty'), null);
});

test('every row carries a mark before its text', () => {
  // The lab gives each row a 16px .ci; the app built only a title
  // and a subtitle, so the list was a wall of words and a navigation, a search
  // and a favourite looked identical.
  // A query no local verb answers, so the rows are exactly the two matches.
  window.atriumSetCommandBar({query: 'zzq', matches: [
    {index: 0, title: 'google.com', kind: 'url'},
    {index: 1, title: 'zzq', kind: 'search'},
  ]});
  const rows = root.querySelectorAll('.cbrow');
  const icons = root.querySelectorAll('.cbrow .cbicon');
  assert.equal(rows.length, 2, 'the fixture is the two matches');
  assert.equal(icons.length, 2, 'one mark per row');
  assert.equal(icons[0].tagName.toLowerCase(), 'svg', 'and it is an SVG mark');
  assert.notEqual(icons[0].dataset.icon, icons[1].dataset.icon,
                  'a navigation and a search do not look the same');
});

test('a provisional pass does not claim the query was answered', () => {
  // Query() pushes Atrium's own rows the moment the text arrives,
  // BEFORE the omnibox is asked, so that a profile whose autocomplete never
  // answers still shows favourites and tabs. That push carries the query and
  // an empty matches list, and the page could not tell it from a real answer -
  // so typing a url flashed "No command or match" instantly, and whether the
  // real matches replaced it in time was a race the user experienced as
  // random. Measured against the running app: the omnibox does answer, with a
  // url row for google.com, one push later.
  input().value = 'google.com';
  window.atriumSetCommandBar({query: 'google.com', matches: []});
  assert.equal(root.querySelector('.cbrow.cbempty'), null,
               'nothing has been asked yet, so nothing can be missing');

  window.atriumSetCommandBar({
    query: 'google.com',
    matches: [{index: 0, title: 'google.com', kind: 'url'}],
    final: true,
  });
  assert.equal(root.querySelector('.cbrow.cbempty'), null);
  assert.ok(root.querySelector('.cbrow'), 'the real pass renders its rows');
});

test('a provisional pass followed by a real empty one still says so', () => {
  // The floor under the test above: withholding the row for a provisional push
  // must not withhold it for the answer that follows.
  input().value = 'qqzz';
  window.atriumSetCommandBar({query: 'qqzz', matches: []});
  window.atriumSetCommandBar({query: 'qqzz', matches: [], final: true});
  assert.ok(root.querySelector('.cbrow.cbempty'), 'no empty-state row');
});

test('the empty-state row cannot be accepted', () => {
  pass('qqzz', {matches: []});
  input().value = 'qqzz';
  window.atriumSetCommandBar({query: 'qqzz', matches: [], final: true});
  // Proved present first: without this the assertion below passes on a palette
  // that simply rendered nothing, which is the state it exists to rule out.
  assert.ok(root.querySelector('.cbrow.cbempty'));
  h.sent.length = 0;
  key('Enter');
  assert.deepEqual(h.sent.filter(([n]) => n === 'commandBarAccept'), []);
});

test('a close command closes the tabs it names', () => {
  const tabs = [
    {id: 't1', title: 'Team chat', url: 'https://slack.example/a'},
    {id: 't2', title: 'Design chat', url: 'https://slack.example/b'},
    {id: 't3', title: 'Docs', url: 'https://docs.example/'},
  ];
  pass('', {tabs});
  input().value = 'close chat';
  h.sent.length = 0;
  key('Enter');
  assert.deepEqual(h.sent.filter(([n]) => n === 'sidebarTabClosed'),
                   [['sidebarTabClosed', ['t1']], ['sidebarTabClosed', ['t2']]]);
});

test('a command the palette cannot perform sends nothing', () => {
  // It resolves its targets and still must not act: mv has no message the
  // palette can fill in, and a Return that half-ran one would be worse than
  // one that says so.
  pass('', {tabs: [{id: 't1', title: 'Team chat', url: 'https://s.example/'}]});
  input().value = 'mv chat work';
  h.sent.length = 0;
  key('Enter');
  assert.deepEqual(h.sent.filter(([n]) => n !== 'commandBarQuery'), []);
});

test('a close command names a live tab id, not a row position', () => {
  // The message takes the id the sidebar uses. Sending an index would close
  // whatever happened to be at that position in the browser's own strip.
  pass('', {tabs: [{id: 'live-9', title: 'Docs', url: 'https://docs.example/'}]});
  input().value = 'close 1';
  h.sent.length = 0;
  key('Enter');
  assert.deepEqual(h.sent.filter(([n]) => n === 'sidebarTabClosed'),
                   [['sidebarTabClosed', ['live-9']]]);
});

test('.cblist fits exactly five .cbrow rows', () => {
  // .cblist sizes itself as 5 * --cb-row-h, border-box. A .cbrow without its
  // own box-sizing adds its padding on top of that height, so each row
  // renders taller than the list assumed and only ~3.75 fit.
  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/commandbar.css'),
      'utf8');
  const cbrowRule = css.split(/\.cbrow\s*{/)[1].split('}')[0];
  assert.match(cbrowRule, /box-sizing:\s*border-box/,
      '.cbrow must be border-box so its padding fits inside min-height');
});

test('row padding comes from the shared .ov-row, not a per-surface override', () => {
  // The absence assertion below needs the presence one above it, or a deleted
  // .cbrow rule would pass.
  const overlayCss = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/overlay.css'), 'utf8');
  const ovRowRule = overlayCss.split(/\.ov-row\s*{/)[1].split('}')[0];
  assert.match(ovRowRule, /padding:\s*8px\s+10px/,
      '.ov-row is where every overlay row takes its padding from');

  const css = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/commandbar.css'),
      'utf8');
  const cbrowRule = css.split(/\.cbrow\s*{/)[1].split('}')[0];
  assert.doesNotMatch(cbrowRule, /^\s*padding:/m,
      '.cbrow must not restate padding, or the surfaces drift again');
  assert.match(cbrowRule, /min-height:\s*var\(--cb-row-h\)/,
      '.cbrow height must track --cb-row-h, not the sidebar\'s row token');
  const cbcardRule = css.split(/\.cbcard\s*{/)[1].split('}')[0];
  assert.match(cbcardRule, /--cb-row-h:\s*32px/,
      '--cb-row-h must stay 32px: the 16px icon slot plus 2 * 8px padding');
  const cblistRule = css.split(/\.cblist\s*{/)[1].split('}')[0];
  assert.match(cblistRule, /height:\s*calc\(5 \* var\(--cb-row-h\) \+ 10px\)/,
      '.cblist must size five rows from the same --cb-row-h');
});

// --- open tabs in the palette -----------------------------------------------
// A window with no favourites opened a blank list: the empty query could only
// produce favourite rows, and a fresh profile has none.

const TAB_MATCHES = [
  {id: 't1', title: 'Team chat', url: 'https://slack.example/a', kind: 'tab'},
  {id: 't2', title: 'Weather', url: 'https://weather.example/', kind: 'tab'},
];

test('an empty field lists the open tabs', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar({query: '', tabMatches: TAB_MATCHES, matches: []});
  h.sent.length = 0;
  assert.deepEqual(titles(), ['Team chat', 'Weather']);
});

test('tab rows sit after the favourites and before the omnibox matches', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar(
      {query: 'atr', favorites: FAVORITES, tabMatches: TAB_MATCHES,
       matches: MATCHES});
  h.sent.length = 0;
  assert.deepEqual(titles().slice(0, 3),
                   ['Atrium Docs', 'Team chat', 'Weather']);
  assert.equal(rows().length,
               FAVORITES.length + TAB_MATCHES.length + MATCHES.length);
});

test('a tab row is marked and labelled, an autocomplete row is not', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar(
      {query: '', tabMatches: TAB_MATCHES, matches: MATCHES});
  h.sent.length = 0;
  const tab = rows()[0];
  assert.equal(tab.classList.contains('cbtab'), true);
  assert.equal(tab.querySelector('.cbsub').textContent, 'Open tab');
  assert.equal(tab.dataset.cbTabId, 't1');
  assert.equal(tab.dataset.cbIndex, undefined);
  assert.equal(rows()[TAB_MATCHES.length].classList.contains('cbtab'), false);
});

test('a tab row does not look like a favourite or a match', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar(
      {query: 'atr', favorites: FAVORITES, tabMatches: TAB_MATCHES,
       matches: MATCHES});
  h.sent.length = 0;
  const glyph = (row) => row.querySelector('.cbicon').dataset.icon;
  assert.notEqual(glyph(rows()[1]), glyph(rows()[0]), 'tab vs favourite');
  assert.notEqual(glyph(rows()[1]), glyph(rows()[3]), 'tab vs match');
});

test('the browser filters the tab rows, the page renders what it sent', () => {
  // The narrowing is the browser's (TabMatchesForQuery). A second rule here
  // would drop a row the browser had already decided to keep.
  window.atriumResetCommandBar();
  window.atriumSetCommandBar(
      {query: 'chat', tabMatches: [TAB_MATCHES[0]], matches: MATCHES});
  h.sent.length = 0;
  assert.deepEqual(titles().slice(0, 1), ['Team chat']);
  assert.equal(rows().length, 1 + MATCHES.length);
});

test('accepting a tab row activates that tab and closes the bar', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar(
      {query: '', tabMatches: TAB_MATCHES, matches: MATCHES});
  h.sent.length = 0;
  key('ArrowDown');
  key('Enter');
  assert.deepEqual(
      h.sent, [['sidebarTabClicked', ['t2']], ['commandBarDismiss', []]]);
});

test('the tab path sends only names the browser has registered', () => {
  assert.ok(REGISTERED.has('sidebarTabClicked'));
});

test('a second pass for the same query keeps the highlight on the tab ' +
     'the user picked, even when another tab shares its title', () => {
  window.atriumResetCommandBar();
  const sameTitleTabs = [
    {id: 't1', title: 'Same', url: 'https://a.example/', kind: 'tab'},
    {id: 't2', title: 'Same', url: 'https://b.example/', kind: 'tab'},
  ];
  window.atriumSetCommandBar({query: 'x', tabMatches: sameTitleTabs, matches: []});
  h.sent.length = 0;
  key('ArrowDown');
  window.atriumSetCommandBar({query: 'x', tabMatches: sameTitleTabs, matches: []});
  key('Enter');
  assert.deepEqual(
      h.sent, [['sidebarTabClicked', ['t2']], ['commandBarDismiss', []]]);
});

test('the unfiltered tab list still reaches the verb grammar', () => {
  // `tabs` and `tabMatches` are separate keys for this reason: `close 2` counts
  // positions in the whole list, so a narrowed one would close another tab.
  window.atriumResetCommandBar();
  window.atriumSetCommandBar({
    query: 'chat',
    tabMatches: [TAB_MATCHES[0]],
    tabs: [{id: 't1', title: 'Team chat', url: 'https://slack.example/a'},
           {id: 't2', title: 'Weather', url: 'https://weather.example/'}],
    matches: [],
  });
  input().value = 'close 2';
  h.sent.length = 0;
  key('Enter');
  assert.deepEqual(h.sent.filter(([n]) => n === 'sidebarTabClosed'),
                   [['sidebarTabClosed', ['t2']]]);
});

test('a reopened palette asks for the empty query again', () => {
  // Show() only resets the page, so without this the second Cmd+T opened on a
  // blank list - nothing else re-runs the empty query.
  h.sent.length = 0;
  window.atriumResetCommandBar();
  assert.deepEqual(h.sent, [['commandBarQuery', ['']]]);
});

test('Ctrl+N moves the highlight down and wraps', () => {
  reset();
  key('n', {ctrlKey: true});
  assert.equal(selectedRow(), rows()[1]);
  key('n', {ctrlKey: true});
  key('n', {ctrlKey: true});
  assert.equal(selectedRow(), rows()[0]);
});

test('Ctrl+P moves the highlight up', () => {
  reset();
  key('p', {ctrlKey: true});
  assert.equal(selectedRow(), rows()[2]);
});

// Cmd+L opens the palette on the current URL rather than empty.
// CommandBarWidget::Show passes it to atriumResetCommandBar; Cmd+T calls the
// same function with nothing, which is what keeps the two keys apart.
const PAGE_URL = 'https://atrium.example/docs?q=1';

// The shim models focus but not a text caret, so select() is counted rather
// than measured - the browser's own selection is what the VM scenario reads.
function countSelects() {
  const field = input();
  const counter = {n: 0};
  field.select = () => {
    counter.n++;
  };
  return counter;
}

test('an open with a prefill fills the field and selects it', () => {
  const selects = countSelects();
  h.sent.length = 0;
  window.atriumResetCommandBar(PAGE_URL);
  assert.equal(input().value, PAGE_URL);
  assert.equal(selects.n, 1);
  assert.equal(document.activeElement, input());
});

test('a prefilled open asks the browser about the URL, not the empty query',
     () => {
       countSelects();
       h.sent.length = 0;
       window.atriumResetCommandBar(PAGE_URL);
       assert.deepEqual(h.sent, [['commandBarQuery', [PAGE_URL]]]);
     });

test('the open after it comes back empty and selects nothing', () => {
  const selects = countSelects();
  window.atriumResetCommandBar(PAGE_URL);
  selects.n = 0;
  h.sent.length = 0;
  window.atriumResetCommandBar();
  assert.equal(input().value, '');
  assert.equal(selects.n, 0);
  assert.deepEqual(h.sent, [['commandBarQuery', ['']]]);
});

test('Enter on a prefilled open takes the row the URL pass answered with',
     () => {
       countSelects();
       window.atriumResetCommandBar(PAGE_URL);
       window.atriumSetCommandBar(
           {query: PAGE_URL, matches: [{index: 7, title: 'Atrium docs',
                                        subtitle: PAGE_URL, kind: 'url'}]});
       assert.equal(input().value, PAGE_URL);
       h.sent.length = 0;
       key('Enter');
       assert.deepEqual(h.sent, [['commandBarAccept', [7]]]);
     });

test('an Enter pressed before the URL pass lands is honored against it', () => {
  // The pending-accept guard compares the pass's query with the field, so a
  // prefill that set one without the other would drop this Enter.
  countSelects();
  window.atriumResetCommandBar(PAGE_URL);
  key('Enter');
  h.sent.length = 0;
  window.atriumSetCommandBar(
      {query: PAGE_URL, matches: [{index: 3, title: 'Atrium docs',
                                   subtitle: PAGE_URL, kind: 'url'}]});
  assert.deepEqual(h.sent, [['commandBarAccept', [3]]]);
});

// --- spaces as rows ---------------------------------------------------------
// the palette offered a New Space verb and no way to reach a space
// that already exists.

const SPACE_MATCHES = [
  {index: 1, id: '1', title: 'Personal', kind: 'space'},
  {index: 3, id: '3', title: 'Personal Finance', kind: 'space'},
];

test('a space row renders with its own glyph and section label', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar({
    query: 'person',
    favorites: FAVORITES,
    tabMatches: TAB_MATCHES,
    spaceMatches: SPACE_MATCHES,
    matches: MATCHES,
  });
  h.sent.length = 0;
  const space = root.querySelector('.cbrow.cbspace');
  assert.equal(space.querySelector('.cbtitle').textContent, 'Personal');
  assert.equal(space.querySelector('.cbsub').textContent, 'Space');
  const glyph = (row) => row.querySelector('.cbicon').dataset.icon;
  assert.notEqual(glyph(space), glyph(root.querySelector('.cbrow.cbtab')));
  assert.notEqual(glyph(space), glyph(root.querySelector('.cbrow.cbfav')));
});

// goToSpaceN's chip rides the same match.binding field and keycaps
// helper the action rows already use - no second renderer.
test('a space row with a binding draws it as a chip', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar({
    query: 'person',
    spaceMatches: [{...SPACE_MATCHES[0], binding: '⇧⌘1'}],
    matches: [],
  });
  h.sent.length = 0;
  assert.equal(
      root.querySelector('.cbrow.cbspace .cbkey').textContent, '⇧⌘1');
});

test('a space row with no binding draws no chip', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar(
      {query: 'person', spaceMatches: SPACE_MATCHES, matches: []});
  h.sent.length = 0;
  assert.equal(root.querySelector('.cbrow.cbspace .cbkey'), null);
});

test('the next push carries a rebinding for a space, and the chip follows',
     () => {
       window.atriumResetCommandBar();
       window.atriumSetCommandBar({
         query: 'person',
         spaceMatches: [{...SPACE_MATCHES[0], binding: '⇧⌘1'}],
         matches: [],
       });
       assert.equal(
           root.querySelector('.cbrow.cbspace .cbkey').textContent, '⇧⌘1');

       window.atriumSetCommandBar({
         query: 'person',
         spaceMatches: [{...SPACE_MATCHES[0], binding: '⇧⌘2'}],
         matches: [],
       });
       assert.equal(
           root.querySelector('.cbrow.cbspace .cbkey').textContent, '⇧⌘2');
     });

test('space rows sit after the tab rows and before the omnibox matches', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar({
    query: 'person',
    favorites: FAVORITES,
    tabMatches: TAB_MATCHES,
    spaceMatches: SPACE_MATCHES,
    matches: MATCHES,
  });
  h.sent.length = 0;
  assert.deepEqual(titles().slice(0, 5),
                   ['Atrium Docs', 'Team chat', 'Weather', 'Personal',
                    'Personal Finance']);
  assert.equal(
      rows().length,
      FAVORITES.length + TAB_MATCHES.length + SPACE_MATCHES.length +
          MATCHES.length);
});

test('accepting a space row switches to it and closes the bar', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar(
      {query: 'person', spaceMatches: SPACE_MATCHES, matches: []});
  h.sent.length = 0;
  key('ArrowDown');
  key('Enter');
  assert.deepEqual(
      h.sent, [['sidebarSpaceSelected', [3]], ['commandBarDismiss', []]]);
});

test('the space it switches to is named by rail position, not row position',
     () => {
       // The row the user picks is the FIRST one, and the space it names is the
       // second chip on the rail. A send that passed the row's own position
       // would switch to somebody else's space.
       window.atriumResetCommandBar();
       window.atriumSetCommandBar(
           {query: 'person', spaceMatches: SPACE_MATCHES, matches: []});
       h.sent.length = 0;
       key('Enter');
       assert.deepEqual(
           h.sent, [['sidebarSpaceSelected', [1]], ['commandBarDismiss', []]]);
     });

test('the space path sends only names the browser has registered', () => {
  assert.ok(REGISTERED.has('sidebarSpaceSelected'));
});

test('a second pass for the same query keeps the highlight on the space ' +
     'the user picked, even when another space shares its name', () => {
  // Two spaces with one name is ordinary rather than an edge case: every space
  // sidebarSpaceCreated makes starts out called "New Space".
  window.atriumResetCommandBar();
  const sameName = [
    {index: 0, id: '0', title: 'Research', kind: 'space'},
    {index: 2, id: '2', title: 'Research', kind: 'space'},
  ];
  window.atriumSetCommandBar(
      {query: 'research', spaceMatches: sameName, matches: []});
  h.sent.length = 0;
  key('ArrowDown');
  window.atriumSetCommandBar(
      {query: 'research', spaceMatches: sameName, matches: []});
  key('Enter');
  assert.deepEqual(
      h.sent, [['sidebarSpaceSelected', [2]], ['commandBarDismiss', []]]);
});

test('an empty field offers no space rows', () => {
  // The browser decides this (SpaceMatchesForQuery), and the empty-query
  // snapshot carries no spaceMatches at all - the page must render what it was
  // sent rather than keeping the previous pass's rows.
  window.atriumResetCommandBar();
  window.atriumSetCommandBar(
      {query: 'person', spaceMatches: SPACE_MATCHES, matches: []});
  window.atriumSetCommandBar({query: '', tabMatches: TAB_MATCHES, matches: []});
  h.sent.length = 0;
  assert.equal(root.querySelectorAll('.cbrow.cbspace').length, 0);
  assert.deepEqual(titles(), ['Team chat', 'Weather']);
});

// Tab-to-search. The Swift bar scoped the palette to an engine and
// the HTML one handled no Tab key at all.

// The shape the bridge emits for a row the omnibox offered a keyword for: the
// engine's keyword goes into the next query, its name onto the chip.
const ENGINE_MATCHES = [
  {index: 0, title: 'bing', subtitle: 'Search', kind: 'search',
   deletable: false, keyword: 'bing.com', engine: 'Bing'},
  {index: 1, title: 'Bing', subtitle: 'https://www.bing.com/', kind: 'url',
   deletable: false},
];

const chip = () => root.querySelector('.cbscope');

function enterScope(typed = 'bing') {
  window.atriumResetCommandBar();
  input().value = typed;
  input().dispatchEvent(new Event('input', {bubbles: true}));
  window.atriumSetCommandBar({query: typed, matches: ENGINE_MATCHES});
  key('Tab');
}

test('Tab on a row carrying an engine enters its scope', () => {
  window.atriumResetCommandBar();
  input().value = 'bing';
  input().dispatchEvent(new Event('input', {bubbles: true}));
  window.atriumSetCommandBar({query: 'bing', matches: ENGINE_MATCHES});
  h.sent.length = 0;
  key('Tab');
  assert.equal(chip().textContent, 'Bing');
  assert.equal(input().value, '', 'the field clears, the way the omnibox does');
  assert.deepEqual(h.sent, [['commandBarQuery', ['bing.com ']]]);
});

test('typing in a scope asks for the keyword and the terms', () => {
  // This is how AutocompleteInput is put into keyword mode: it parses the
  // leading keyword itself, so the page prefixes rather than sending a flag.
  enterScope();
  h.sent.length = 0;
  input().value = 'hats';
  input().dispatchEvent(new Event('input', {bubbles: true}));
  assert.deepEqual(h.sent, [['commandBarQuery', ['bing.com hats']]]);
});

test('Escape in a scope leaves it and puts the pre-Tab text back', () => {
  enterScope();
  input().value = 'hats';
  input().dispatchEvent(new Event('input', {bubbles: true}));
  h.sent.length = 0;
  key('Escape');
  assert.equal(input().value, 'bing');
  assert.equal(chip().textContent, '');
  assert.deepEqual(h.sent, [['commandBarQuery', ['bing']]],
      'it re-asks for the restored text and does NOT dismiss');
});

test('Backspace on an empty field leaves the scope', () => {
  enterScope();
  h.sent.length = 0;
  key('Backspace');
  assert.equal(input().value, 'bing');
  assert.equal(chip().textContent, '');
  assert.deepEqual(h.sent, [['commandBarQuery', ['bing']]]);
});

test('Backspace on a field with terms in it stays in the scope', () => {
  // Otherwise deleting the last letter of a search term would drop the engine
  // as well, which is a scope nobody can hold while editing.
  enterScope();
  input().value = 'hats';
  input().dispatchEvent(new Event('input', {bubbles: true}));
  h.sent.length = 0;
  key('Backspace');
  assert.equal(chip().textContent, 'Bing');
  assert.deepEqual(h.sent, []);
});

test('Enter in a scope accepts the highlighted row', () => {
  enterScope();
  window.atriumSetCommandBar({
    query: 'bing.com hats',
    matches: [{index: 0, title: 'hats', subtitle: 'Search', kind: 'search',
               deletable: false}],
  });
  h.sent.length = 0;
  key('Enter');
  assert.deepEqual(h.sent, [['commandBarAccept', [0]]]);
});

// The reported bug: typing go offered Google, Tab scoped to it, and Enter did
// nothing at all. Atrium builds these rows itself, so index is -1 and the old
// fall-through sent commandBarAccept(-1), which resolves to no match.
test('Enter on an engine row opens that engine', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar({
    query: 'go',
    matches: [{index: -1, title: 'Google', subtitle: 'Search', kind: 'search',
               keyword: 'google.com', engine: 'Google', deletable: false}],
  });
  h.sent.length = 0;
  key('Enter');
  assert.deepEqual(h.sent, [['commandBarEngineAccept', ['google.com']]]);
});

// UpdateAssociatedKeywords hangs the keyword on the omnibox's OWN default row
// when the whole input is one, so that row carries a keyword AND a position.
// Accepting it by position searches the default provider for the word.
test('Enter on the default row whose keyword is the whole typed text opens the engine',
    () => {
      window.atriumResetCommandBar();
      input().value = 'maps';
      window.atriumSetCommandBar({
        query: 'maps',
        matches: [{index: 0, title: 'maps', subtitle: 'Search', kind: 'search',
                   keyword: 'maps', engine: 'Google Maps', deletable: false}],
      });
      h.sent.length = 0;
      key('Enter');
      assert.deepEqual(h.sent, [['commandBarEngineAccept', ['maps']]]);
    });

// Once terms follow the keyword the omnibox is in keyword mode and its row
// already searches that engine, so the position is the correct route.
test('Enter on a keyword row with terms typed after it still accepts by position',
    () => {
      window.atriumResetCommandBar();
      input().value = 'maps cats';
      window.atriumSetCommandBar({
        query: 'maps cats',
        matches: [{index: 0, title: 'cats', subtitle: 'Search', kind: 'search',
                   keyword: 'maps', engine: 'Google Maps', deletable: false}],
      });
      h.sent.length = 0;
      key('Enter');
      assert.deepEqual(h.sent, [['commandBarAccept', [0]]]);
    });

// In a scope the field holds the TERMS, never the keyword, so terms that
// happen to equal the keyword must not be read as a request for the home.
test('Enter in a scope whose terms equal the keyword still accepts by position',
    () => {
      enterScope();
      input().value = 'bing.com';
      input().dispatchEvent(new Event('input', {bubbles: true}));
      window.atriumSetCommandBar({
        query: 'bing.com bing.com',
        matches: [{index: 0, title: 'bing.com', subtitle: 'Search',
                   kind: 'search', keyword: 'bing.com', engine: 'Bing',
                   deletable: false}],
      });
      h.sent.length = 0;
      key('Enter');
      assert.deepEqual(h.sent, [['commandBarAccept', [0]]]);
    });

// A history row for the keyword's own site is a page the user visited, not the
// engine's front door.
test('Enter on a url row carrying the typed keyword still accepts by position',
    () => {
      window.atriumResetCommandBar();
      input().value = 'maps';
      window.atriumSetCommandBar({
        query: 'maps',
        matches: [{index: 0, title: 'Google Maps',
                   subtitle: 'https://www.google.com/maps/place/atrium',
                   kind: 'url', keyword: 'maps', engine: 'Google Maps',
                   deletable: false}],
      });
      h.sent.length = 0;
      key('Enter');
      assert.deepEqual(h.sent, [['commandBarAccept', [0]]]);
    });

// The omnibox's own suggestions share the kind and DO have a position, so the
// engine branch must not swallow them - that would break every ordinary search.
test('Enter on an omnibox search suggestion still accepts by position', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar({
    query: 'zzq',
    matches: [{index: 3, title: 'zzq', subtitle: 'Search', kind: 'search',
               deletable: false}],
  });
  h.sent.length = 0;
  key('Enter');
  assert.deepEqual(h.sent, [['commandBarAccept', [3]]]);
});

test('Tab on a row that carries no engine does nothing', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar({query: 'atr', matches: MATCHES});
  h.sent.length = 0;
  key('Tab');
  assert.equal(chip().textContent, '');
  assert.deepEqual(h.sent, []);
});

test('Escape outside a scope still dismisses the palette', () => {
  reset();
  key('Escape');
  assert.deepEqual(h.sent, [['commandBarDismiss', []]]);
});

test('reopening the palette drops any scope it was left in', () => {
  enterScope();
  h.sent.length = 0;
  window.atriumResetCommandBar();
  assert.equal(chip().textContent, '');
  assert.deepEqual(h.sent, [['commandBarQuery', ['']]]);
});

test('a row that offers a scope says so on the row', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar({query: 'bing', matches: ENGINE_MATCHES});
  h.sent.length = 0;
  assert.equal(rows()[0].dataset.cbKeyword, 'bing.com');
  assert.equal(rows()[1].dataset.cbKeyword, undefined);
});

// the Tab-to-search hint beside the field.

const tabHint = () => root.querySelector('.cbtabhint');

test('a push whose selected row carries a keyword shows the hint', () => {
  window.atriumResetCommandBar();
  input().value = 'bing';
  input().dispatchEvent(new Event('input', {bubbles: true}));
  window.atriumSetCommandBar({query: 'bing', matches: ENGINE_MATCHES});
  h.sent.length = 0;
  const hint = tabHint();
  assert.equal(hint.classList.contains('cbtabhinton'), true);
  assert.equal(hint.querySelector('.cbtabhint-text').textContent, 'Search Bing');
  assert.equal(hint.querySelector('.ov-key').textContent, 'Tab');
});

test('arrowing to a row without a keyword hides the hint', () => {
  window.atriumResetCommandBar();
  input().value = 'bing';
  input().dispatchEvent(new Event('input', {bubbles: true}));
  window.atriumSetCommandBar({query: 'bing', matches: ENGINE_MATCHES});
  h.sent.length = 0;
  key('ArrowDown');
  assert.equal(tabHint().classList.contains('cbtabhinton'), false);
});

test('Tab hides the hint and shows the chip, Escape brings the hint back', () => {
  enterScope();
  const hint = tabHint();
  assert.equal(hint.classList.contains('cbtabhinton'), false);
  assert.equal(chip().textContent, 'Bing');
  key('Escape');
  // The restored query answers with the same row, the way a real pass would.
  window.atriumSetCommandBar({query: 'bing', matches: ENGINE_MATCHES});
  assert.equal(hint.classList.contains('cbtabhinton'), true);
});

test('typing a verb hides the hint under its preview', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar({query: '', matches: MATCHES, tabs: TAB_ROWS});
  h.sent.length = 0;
  input().value = 'bing';
  input().dispatchEvent(new Event('input', {bubbles: true}));
  window.atriumSetCommandBar({query: 'bing', matches: ENGINE_MATCHES});
  assert.equal(tabHint().classList.contains('cbtabhinton'), true,
      'the hint should be showing before the verb is typed');
  input().value = 'close chat';
  input().dispatchEvent(new Event('input', {bubbles: true}));
  assert.ok(root.querySelector('.cbcmd'), 'no command preview rendered');
  assert.equal(tabHint().classList.contains('cbtabhinton'), false);
});

test('a keyword with no engine name falls back to the keyword itself', () => {
  window.atriumResetCommandBar();
  input().value = 'docs';
  input().dispatchEvent(new Event('input', {bubbles: true}));
  window.atriumSetCommandBar({query: 'docs', matches: [
    {index: 0, title: 'docs.rs', subtitle: 'Search', kind: 'search',
     deletable: false, keyword: 'docs.rs'},
  ]});
  h.sent.length = 0;
  assert.equal(
      tabHint().querySelector('.cbtabhint-text').textContent, 'Search docs.rs');
});

// Cmd+C copies the highlighted row's URL when the field holds no
// text selection, the way Arc does. A text selection means the native copy
// runs instead.

test('Cmd+C with a favourite highlighted and no selection copies its url', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar({query: 'atr', favorites: FAVORITES, matches: []});
  h.sent.length = 0;
  h.clipboard.length = 0;
  key('c', {metaKey: true});
  assert.deepEqual(h.clipboard, ['https://docs.example/']);
});

test('Cmd+C with a selection in the field does not copy the row', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar({query: 'atr', favorites: FAVORITES, matches: []});
  input().selectionStart = 0;
  input().selectionEnd = 3;
  h.sent.length = 0;
  h.clipboard.length = 0;
  key('c', {metaKey: true});
  assert.deepEqual(h.clipboard, []);
  input().selectionStart = undefined;
  input().selectionEnd = undefined;
});

test('Cmd+C on a search row does not copy', () => {
  reset();
  key('ArrowDown');
  key('ArrowDown');
  assert.equal(selectedRow().querySelector('.cbtitle').textContent,
      'atrium browser');
  h.clipboard.length = 0;
  key('c', {metaKey: true});
  assert.deepEqual(h.clipboard, []);
});

// the omnibox fills inline_autocompletion on its default match, and
// the palette appends it to what was typed and leaves it selected - so Return
// goes straight there and the next character replaces it.

// The shim models focus but no caret, so the field is given the two methods a
// real input has. The tests then read back the range production code set.
function withCaret() {
  const field = input();
  if (!field.caretModelled) {
    field.caretModelled = true;
    let text = field.value;
    Object.defineProperty(field, 'value', {
      configurable: true,
      get: () => text,
      // A real input collapses its selection to the end when the value is
      // assigned. Without it a range one test set outlives that test, and the
      // next Cmd+C reads it as the user having selected text.
      set: (next) => {
        text = String(next);
        field.selectionStart = text.length;
        field.selectionEnd = text.length;
      },
    });
  }
  field.setSelectionRange = (start, end) => {
    field.selectionStart = start;
    field.selectionEnd = end;
  };
  field.select = () => {
    field.selectionStart = 0;
    field.selectionEnd = field.value.length;
  };
  return field;
}

// A real field leaves the caret after the inserted character, and the accept
// path reads it, so the helper sets it rather than each test remembering to.
function typeChars(field, text) {
  field.value = text;
  field.selectionStart = text.length;
  field.selectionEnd = text.length;
  field.dispatchEvent(new Event('input', {bubbles: true}));
}

const COMPLETING = [
  {index: 0, title: 'Atrium', subtitle: 'https://atrium.example/', kind: 'url',
   deletable: true, inlineCompletion: 'ium.example'},
];

test('a pass for the typed text appends the completion and selects it', () => {
  const field = withCaret();
  window.atriumResetCommandBar();
  typeChars(field, 'atr');
  window.atriumSetCommandBar({query: 'atr', matches: COMPLETING});
  assert.equal(field.value, 'atrium.example');
  assert.equal(field.selectionStart, 3);
  assert.equal(field.selectionEnd, 14);
});

test('Backspace suppresses the completion the pass behind it carries', () => {
  const field = withCaret();
  window.atriumResetCommandBar();
  typeChars(field, 'atri');
  key('Backspace');
  typeChars(field, 'atr');
  window.atriumSetCommandBar({query: 'atr', matches: COMPLETING});
  assert.equal(field.value, 'atr');
  assert.equal(field.selectionEnd, 3);
});

test('the character typed after a Backspace completes again', () => {
  const field = withCaret();
  window.atriumResetCommandBar();
  typeChars(field, 'atri');
  key('Backspace');
  typeChars(field, 'atr');
  window.atriumSetCommandBar({query: 'atr', matches: COMPLETING});
  key('i');
  typeChars(field, 'atri');
  window.atriumSetCommandBar({
    query: 'atri',
    matches: [{index: 0, title: 'Atrium', subtitle: 'https://atrium.example/',
               kind: 'url', inlineCompletion: 'um.example'}],
  });
  assert.equal(field.value, 'atrium.example');
  assert.equal(field.selectionStart, 4);
});

test('Right takes the completion and asks about the whole text', () => {
  const field = withCaret();
  window.atriumResetCommandBar();
  typeChars(field, 'atr');
  window.atriumSetCommandBar({query: 'atr', matches: COMPLETING});
  h.sent.length = 0;
  key('ArrowRight');
  assert.equal(field.value, 'atrium.example');
  assert.equal(field.selectionStart, 14);
  assert.equal(field.selectionEnd, 14);
  assert.deepEqual(h.sent, [['commandBarQuery', ['atrium.example']]]);
});

test('End takes it too', () => {
  const field = withCaret();
  window.atriumResetCommandBar();
  typeChars(field, 'atr');
  window.atriumSetCommandBar({query: 'atr', matches: COMPLETING});
  h.sent.length = 0;
  key('End');
  assert.equal(field.selectionStart, 14);
  assert.deepEqual(h.sent, [['commandBarQuery', ['atrium.example']]]);
});

test('Right on a prefilled field is not an inline accept', () => {
  // Cmd+L selects the whole URL and the scoped Tab clears the field. Neither
  // is a completion, and treating either as one would send a query the user
  // never asked for on a plain caret key.
  const field = withCaret();
  window.atriumResetCommandBar(PAGE_URL);
  h.sent.length = 0;
  key('ArrowRight');
  assert.equal(field.value, PAGE_URL);
  assert.deepEqual(h.sent, []);
});

test('Return with a completion showing accepts the row it came from', () => {
  const field = withCaret();
  window.atriumResetCommandBar();
  typeChars(field, 'atr');
  window.atriumSetCommandBar({query: 'atr', matches: COMPLETING});
  h.sent.length = 0;
  key('Enter');
  assert.deepEqual(h.sent, [['commandBarAccept', [0]]]);
});

test('a pass for an older query does not complete the field', () => {
  const field = withCaret();
  window.atriumResetCommandBar();
  typeChars(field, 'atri');
  window.atriumSetCommandBar({query: 'atr', matches: COMPLETING});
  assert.equal(field.value, 'atri');
});

test('an empty field is never completed', () => {
  const field = withCaret();
  window.atriumResetCommandBar();
  window.atriumSetCommandBar({query: '', matches: COMPLETING});
  assert.equal(field.value, '');
});

test('a scoped palette never completes the field', () => {
  // Inside a scope the field holds search terms and the query on the wire
  // carries the keyword, so a completion built from a host would be nonsense.
  const field = withCaret();
  enterScope();
  typeChars(field, 'atr');
  window.atriumSetCommandBar({query: 'atr', matches: COMPLETING});
  assert.equal(field.value, 'atr');
});

test('a selection the user made themselves is not an inline accept', () => {
  // A drag over the tail's end reaches the field's end and spans two or more
  // characters, so only the typed-length half of the guard can tell it from
  // the completion this module applied.
  const field = withCaret();
  window.atriumResetCommandBar();
  typeChars(field, 'atr');
  window.atriumSetCommandBar({query: 'atr', matches: COMPLETING});
  field.selectionStart = 9;
  field.selectionEnd = 14;
  h.sent.length = 0;
  key('ArrowRight');
  key('End');
  assert.equal(field.value, 'atrium.example');
  assert.equal(field.selectionStart, 9);
  assert.equal(field.selectionEnd, 14);
  assert.deepEqual(h.sent, []);
});

test('a row above the completing one blocks the completion', () => {
  // The browser's hygiene pass drops the omnibox row for a page already open
  // and the page draws tab rows above matches, so the row carrying a
  // completion is not always the row Return takes.
  const field = withCaret();
  window.atriumResetCommandBar();
  typeChars(field, 'atr');
  window.atriumSetCommandBar({
    query: 'atr',
    tabMatches: [{id: 't1', title: 'Atrium', url: 'https://atrium.example/',
                  kind: 'tab'}],
    matches: COMPLETING,
  });
  assert.equal(field.value, 'atr');
  assert.equal(rows()[0].dataset.cbTabId, 't1');
});

test('a tab row that inherited the completion still completes', () => {
  // The browser hands the dropped omnibox row's completion to the tab row that
  // replaced it, so the field completes to a page already open and Return
  // switches to it rather than opening it a second time.
  const field = withCaret();
  window.atriumResetCommandBar();
  typeChars(field, 'atr');
  window.atriumSetCommandBar({
    query: 'atr',
    tabMatches: [{id: 't1', title: 'Atrium', url: 'https://atrium.example/',
                  kind: 'tab', inlineCompletion: 'ium.example'}],
    matches: [],
  });
  assert.equal(field.value, 'atrium.example');
  assert.equal(field.selectionStart, 3);
  h.sent.length = 0;
  key('Enter');
  assert.deepEqual(h.sent,
      [['sidebarTabClicked', ['t1']], ['commandBarDismiss', []]]);
});

test('Return does not run the verb the completed text spells', () => {
  // The completion is the browser's guess, not something the user typed, so
  // it must not reach the verb parser - "clo" completing to "close 1" would
  // otherwise close a tab on a keystroke meant for the top match.
  const field = withCaret();
  window.atriumResetCommandBar();
  typeChars(field, 'clo');
  window.atriumSetCommandBar({
    query: 'clo',
    tabs: TAB_ROWS,
    matches: [{index: 5, title: 'Clothing', subtitle: 'https://clo.example/',
               kind: 'url', inlineCompletion: 'se 1'}],
  });
  assert.equal(field.value, 'close 1');
  h.sent.length = 0;
  key('Enter');
  assert.deepEqual(h.sent, [['commandBarAccept', [5]]]);
});

test('Cmd+C on an untitled url row copies the title, which holds the URL',
     () => {
       // RowTextForMatch (atrium_command_bar_matches.cc:35-37) leaves subtitle
       // empty and puts the URL in title when the match has no description.
       window.atriumResetCommandBar();
       window.atriumSetCommandBar({
         query: 'https://example.com/untitled',
         matches: [{index: 0, title: 'https://example.com/untitled',
                    subtitle: '', kind: 'url', deletable: false}],
       });
       h.clipboard.length = 0;
       key('c', {metaKey: true});
       assert.deepEqual(h.clipboard, ['https://example.com/untitled']);
     });

test('a rejecting clipboard write does not raise an unhandled rejection',
     async () => {
       window.atriumResetCommandBar();
       window.atriumSetCommandBar(
           {query: 'atr', favorites: FAVORITES, matches: []});
       const original = navigator.clipboard.writeText;
       navigator.clipboard.writeText = () => Promise.reject(new Error('denied'));
       let unhandled = false;
       const onUnhandled = () => { unhandled = true; };
       process.on('unhandledRejection', onUnhandled);
       key('c', {metaKey: true});
       await new Promise((resolve) => setTimeout(resolve, 0));
       process.off('unhandledRejection', onUnhandled);
       navigator.clipboard.writeText = original;
       assert.equal(unhandled, false);
     });

// key() hands back the shim's dispatchEvent result, which cannot say whether
// the handler swallowed the key. This returns the event itself.
const keyEvent = (name, init = {}) => {
  const event = new Event('keydown', {bubbles: true, key: name, ...init});
  input().dispatchEvent(event);
  return event;
};

test('Tab never lets focus leave the field', () => {
  // There is nothing else in the card to focus, so a Tab the page did not
  // swallow takes key off the field and the bar stops accepting typing.
  window.atriumResetCommandBar();
  window.atriumSetCommandBar({query: 'atr', matches: MATCHES});
  h.sent.length = 0;
  assert.equal(keyEvent('Tab').defaultPrevented, true,
      'a row carrying no engine still swallows Tab');
  assert.equal(keyEvent('Tab', {shiftKey: true}).defaultPrevented, true,
      'and so does Shift+Tab, which scopes nothing');
  assert.deepEqual(h.sent, []);
  enterScope();
  h.sent.length = 0;
  assert.equal(keyEvent('Tab').defaultPrevented, true,
      'Tab inside a scope neither re-scopes nor escapes the field');
  assert.equal(chip().textContent, 'Bing');
  assert.deepEqual(h.sent, []);
});

// --- action rows come from the browser, and carry their binding ------------

// What atrium_command_bar_actions.cc builds, as the browser pushes it. The
// page owns no verb list any more, so a suite that did not push this would be
// testing a palette with no action rows at all.
const ACTION_CATALOG = [
  {kind: 'action', title: 'Settings', subtitle: 'Atrium',
   send: 'openSettingsPane', args: [''], binding: '⌘,',
   aliases: ['settings', 'preferences']},
  {kind: 'action', title: 'Import from Browser', subtitle: 'Atrium',
   send: 'openSettingsPane', args: ['import'], binding: '',
   aliases: ['import', 'import from browser', 'import bookmarks', 'arc']},
  {kind: 'action', title: 'Courtyard', subtitle: 'Atrium',
   send: 'sidebarCourtyardOpened', args: [], binding: '',
   aliases: ['courtyard', 'spaces']},
  {kind: 'action', title: 'Overview', subtitle: 'Atrium',
   send: 'sidebarOverviewToggled', args: [], binding: '⇧⌘O',
   aliases: ['overview']},
  {kind: 'action', title: 'New Space', subtitle: 'Atrium',
   send: 'sidebarSpaceCreated', args: [], binding: '',
   aliases: ['new space', 'space']},
  {kind: 'action', title: 'Toggle Sidebar', subtitle: 'Atrium',
   send: 'sidebarToggleRequested', args: [], binding: '⌘S',
   aliases: ['toggle sidebar', 'sidebar', 'hide sidebar', 'show sidebar']},
  {kind: 'action', title: 'Reload', subtitle: 'Atrium',
   send: 'sidebarReloadPressed', args: [], binding: '⌘R',
   aliases: ['reload', 'refresh']},
  {kind: 'action', title: 'Hard Reload', subtitle: 'Atrium',
   send: 'sidebarReloadHard', args: [], binding: '⇧⌘R',
   aliases: ['hard reload', 'force reload', 'bypass cache', 'ignore cache']},
  {kind: 'action', title: 'Empty Cache and Reload', subtitle: 'Atrium',
   send: 'sidebarReloadClearingCache', args: [], binding: '⌥⌘R',
   aliases: ['empty cache and reload', 'empty cache', 'clear cache']},
  {kind: 'action', title: 'Clear Site Data and Reload', subtitle: 'Atrium',
   send: 'sidebarReloadClearingSite', args: [], binding: '⌥⇧⌘R',
   aliases: ['clear site data and reload', 'clear site data', 'clear cookies']},
  {kind: 'action', title: 'Toggle AI panel', subtitle: 'Atrium',
   send: 'aiPanelToggleRequested', args: [], binding: '⇧⌘I',
   aliases: ['ai panel', 'toggle ai panel', 'ai', 'assistant']},
  {kind: 'action', title: 'Toggle side panel', subtitle: 'Atrium',
   send: 'extensionSidePanelToggleRequested', args: [], binding: '',
   aliases: ['side panel', 'toggle side panel', 'extension panel']},
  {kind: 'action', title: 'Capture Viewport', subtitle: 'Screenshot',
   send: 'captureViewport', args: [], binding: '',
   aliases: ['capture viewport', 'screenshot', 'shot', 'grab', 'snap',
             'visible']},
  {kind: 'action', title: 'Capture Region', subtitle: 'Screenshot',
   send: 'captureRegion', args: [], binding: '',
   aliases: ['capture region', 'screenshot', 'shot', 'grab', 'snap', 'crop',
             'area', 'select']},
  {kind: 'action', title: 'Capture Full Page', subtitle: 'Screenshot',
   send: 'captureFullPage', args: [], binding: '',
   aliases: ['capture full page', 'screenshot', 'shot', 'grab', 'snap',
             'whole', 'full']},
];

function actionPass(query, catalog = ACTION_CATALOG) {
  window.atriumResetCommandBar();
  window.atriumSetCommandBarActions({actions: catalog});
  window.atriumSetCommandBar({query, matches: MATCHES, final: true});
  h.sent.length = 0;
}

const actionRows = () => [...root.querySelectorAll('.cbrow.cbaction')];
const rowTitled = (title) => actionRows().find(
    (row) => row.querySelector('.cbtitle')?.textContent === title);

test('an alias reaches the row it belongs to', () => {
  actionPass('crop');
  assert.deepEqual(
      actionRows().map((row) => row.querySelector('.cbtitle').textContent),
      ['Capture Region']);
});

test('an action the hotkey registry named is reachable too', () => {
  actionPass('sidebar');
  assert.ok(rowTitled('Toggle Sidebar'), 'no Toggle Sidebar row');
});

test('the palette offers nothing the browser has not sent', () => {
  // The page holding its own copy is the drift this replaces: a verb removed
  // in C++ has to disappear here, not linger.
  actionPass('settings', []);
  assert.deepEqual(actionRows(), []);
});

test('a one-letter query offers no action rows', () => {
  // Every action carries a word starting with s or c, so a single letter would
  // bury the omnibox rows under the whole catalog.
  actionPass('s');
  assert.deepEqual(actionRows(), []);
});

test('two letters are enough', () => {
  actionPass('se');
  assert.ok(rowTitled('Settings'), 'no Settings row for "se"');
});

test('an action row draws its binding as a chip', () => {
  actionPass('sidebar');
  assert.equal(rowTitled('Toggle Sidebar').querySelector('.cbkey').textContent,
               '⌘S');
});

test('a row nothing is bound to draws no chip', () => {
  // Unbound is a real state - most verbs have no key at all - and an empty
  // chip would read as a shortcut whose glyphs failed to load.
  actionPass('courtyard');
  assert.equal(rowTitled('Courtyard').querySelector('.cbkey'), null);
});

test('only action rows carry a chip', () => {
  actionPass('courtyard');
  assert.equal(root.querySelectorAll('.cbrow:not(.cbaction) .cbkey').length, 0);
});

test('Settings shows its own shortcut, a bound row beside an unbound one', () => {
  actionPass('settings');
  assert.equal(rowTitled('Settings').querySelector('.cbkey').textContent,
               '⌘,');
  actionPass('courtyard');
  assert.equal(rowTitled('Courtyard').querySelector('.cbkey'), null);
});

test('the next push carries a rebinding, and the chip follows', () => {
  const rebound = ACTION_CATALOG.map(
      (action) => action.title === 'Toggle Sidebar' ?
          {...action, binding: '⇧⌘J'} : action);
  actionPass('sidebar');
  assert.equal(rowTitled('Toggle Sidebar').querySelector('.cbkey').textContent,
               '⌘S');
  actionPass('sidebar', rebound);
  assert.equal(rowTitled('Toggle Sidebar').querySelector('.cbkey').textContent,
               '⇧⌘J');
});

test('accepting an action row still sends its own message', () => {
  actionPass('sidebar');
  click(rowTitled('Toggle Sidebar'));
  assert.deepEqual(h.sent,
                   [['sidebarToggleRequested', []], ['commandBarDismiss', []]]);
});

test('an action row carrying arguments still sends them', () => {
  actionPass('settings');
  click(rowTitled('Settings'));
  assert.deepEqual(h.sent,
                   [['openSettingsPane', ['']], ['commandBarDismiss', []]]);
});

test('the fixture above is the catalog the browser really sends', () => {
  // Read rather than trusted: this suite proves what the page does with the
  // rows, and is worth nothing if the rows are not the ones C++ builds.
  const table =
      fs.readFileSync(
            path.join(SRC,
                      'chrome/browser/ui/atrium/atrium_command_bar_actions.cc'),
            'utf8')
          .split('std::to_array<CatalogRow>({')[1]
          .split('\n});')[0];
  const rows = table.split('},').filter((chunk) => chunk.includes('"'));
  const built = rows.map((chunk) => {
    const quoted = [...chunk.matchAll(/"([^"]*)"/g)].map((m) => m[1]);
    return {send: quoted[0], title: quoted.find((s) => /^[A-Z]/.test(s))};
  });
  assert.deepEqual(
      built,
      ACTION_CATALOG.map(({send, title}) => ({send, title})),
      'kCatalog and this fixture disagree - update ACTION_CATALOG');
});

// --- the shared overlay language --------------------------------------------

test('a row carries an SVG mark for its kind, in the shared icon slot', () => {
  reset();
  const mark = rows()[0].querySelector('.cbicon');
  assert.equal(mark.tagName.toLowerCase(), 'svg');
  assert.equal(mark.classList.contains('ov-icon'), true);
  assert.equal(mark.dataset.icon, 'url');
});

test('a bound action row shows one keycap per key', () => {
  actionPass('sidebar');
  const chip = document.querySelector('.cbrow.cbaction .cbkey');
  assert.equal(chip.classList.contains('ov-keys'), true);
  const caps = chip.querySelectorAll('.ov-key');
  assert.equal(caps.length, 2);
  assert.equal(caps[0].textContent, '⌘');
  assert.equal(caps[1].textContent, 'S');
  assert.equal(chip.textContent, '⌘S');
});

test('the card and its rows wear the shared classes', () => {
  reset();
  assert.equal(document.querySelector('.cbcard').classList.contains('ov-card'), true);
  assert.equal(rows()[0].classList.contains('ov-row'), true);
  assert.equal(selectedRow().classList.contains('ov-sel'), true);
});

test('a row builds its icon, title and subtitle in that order', () => {
  reset();
  const tags = [...rows()[0].children].map((c) => c.tagName.toLowerCase());
  assert.ok(tags[0] === 'svg' || tags[0] === 'img',
      'the icon slot holds a mark or a favicon');
  assert.deepEqual(tags.slice(1), ['span', 'span']);
});

test('a bound action row appends the chip after title and subtitle', () => {
  actionPass('sidebar');
  assert.deepEqual(
      [...actionRows()[0].children].map((c) => c.tagName.toLowerCase()),
      ['svg', 'span', 'span', 'span']);
});

test('the footer names Open and Close with their keys', () => {
  reset();
  const foot = document.querySelector('.cbfoot');
  assert.equal(foot.classList.contains('ov-foot'), true);
  const caps = [...foot.querySelectorAll('.ov-key')].map((c) => c.textContent);
  assert.deepEqual(caps, ['↩', 'esc']);
});

test('a title and a subtitle wear the shared classes, on a match row, a verb ' +
     'preview and the empty-state row', () => {
  reset();
  const row = rows()[0];
  assert.equal(row.querySelector('.cbtitle').classList.contains('ov-title'), true);
  assert.equal(row.querySelector('.cbsub').classList.contains('ov-sub'), true);

  typeInto('close chat');
  const preview = root.querySelector('.cbcmd');
  assert.equal(preview.querySelector('.cbtitle').classList.contains('ov-title'), true);
  assert.equal(preview.querySelector('.cbsub').classList.contains('ov-sub'), true);

  pass('qqzz', {matches: []});
  input().value = 'qqzz';
  window.atriumSetCommandBar({query: 'qqzz', matches: [], final: true});
  const empty = root.querySelector('.cbrow.cbempty');
  assert.equal(empty.querySelector('.cbtitle').classList.contains('ov-title'), true);
  assert.equal(empty.querySelector('.cbsub').classList.contains('ov-sub'), true);
});

// --- the typed query bolds inside a row's title and subtitle -----

const hits = (el) => [...el.querySelectorAll('b.cbhit')];

test('the typed substring renders bold in the title and the subtitle', () => {
  reset();
  const row = rows()[0];
  const titleHits = hits(row.querySelector('.cbtitle'));
  assert.equal(titleHits.length, 1);
  assert.equal(titleHits[0].textContent, 'Atr');
  const subHits = hits(row.querySelector('.cbsub'));
  assert.equal(subHits.length, 1);
  assert.equal(subHits[0].textContent, 'atr');
});

test('a query with no hit renders plain text nodes', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar({query: 'zzz', matches: MATCHES});
  const title = rows()[0].querySelector('.cbtitle');
  assert.equal(hits(title).length, 0);
  assert.equal(title.textContent, 'Atrium');
  assert.ok([...title.childNodes].every((node) => node.nodeType === 3),
      'a query with no hit should render text nodes only');
});

test('an empty query renders no bold node', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar({query: '', matches: MATCHES});
  const title = rows()[0].querySelector('.cbtitle');
  assert.equal(hits(title).length, 0);
  assert.equal(title.textContent, 'Atrium');
});

test('buildRow uses no innerHTML', () => {
  const source = fs.readFileSync(
      path.join(SRC, 'chrome/browser/resources/atrium/commandbar.js'), 'utf8');
  assert.doesNotMatch(source, /\.innerHTML/);
});

test('a longer token wins over a shorter one it contains', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBar({
    query: 'at atr',
    matches: [{index: 0, title: 'atrium docs', subtitle: '', kind: 'history',
               deletable: false}],
  });
  const titleHits = hits(rows()[0].querySelector('.cbtitle'));
  assert.equal(titleHits.length, 1);
  assert.equal(titleHits[0].textContent, 'atr');
});

// --- rows show the site favicon, the mark is the fallback -------------------

const ICON_MATCH = {index: 0, title: 'Example', subtitle: 'https://example.com/',
                     url: 'https://example.com/', kind: 'url',
                     deletable: false};

test('a scoped url row shows the site favicon', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBarIconScope('abc');
  window.atriumSetCommandBar({query: 'example', matches: [ICON_MATCH]});
  const mark = rows()[0].firstChild;
  assert.equal(mark.tagName.toLowerCase(), 'img');
  assert.equal(mark.classList.contains('cbicon'), true);
  assert.equal(
      mark.src,
      `/urlicon/abc/${encodeURIComponent('https://example.com/')}.png`);
  window.atriumSetCommandBarIconScope('');
});

test('a favicon that fails to load falls back to the kind mark', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBarIconScope('abc');
  window.atriumSetCommandBar({query: 'example', matches: [ICON_MATCH]});
  rows()[0].firstChild.dispatchEvent(new Event('error'));
  const mark = rows()[0].firstChild;
  assert.equal(mark.tagName.toLowerCase(), 'svg');
  assert.equal(mark.classList.contains('cbicon'), true);
  assert.equal(mark.dataset.icon, 'url');
  window.atriumSetCommandBarIconScope('');
});

test('a url row keeps the kind mark until a scope is pushed', () => {
  window.atriumResetCommandBar();
  window.atriumSetCommandBarIconScope('');
  window.atriumSetCommandBar({query: 'example', matches: [ICON_MATCH]});
  assert.equal(rows()[0].firstChild.tagName.toLowerCase(), 'svg');
});

test('an action row keeps the kind mark even with a scope pushed', () => {
  window.atriumSetCommandBarIconScope('abc');
  actionPass('sidebar');
  assert.equal(actionRows()[0].firstChild.tagName.toLowerCase(), 'svg');
  window.atriumSetCommandBarIconScope('');
});

// --- the anchored open -------------------------------------------------------

const card = () => root.querySelector('.cbcard');
const PLACEMENT = {left: 12, top: 96, width: 320, height: 34};

test('a reset with a placement lays the card on the anchor', () => {
  reset();
  window.atriumResetCommandBar('https://example.test/', PLACEMENT);
  assert.equal(card().classList.contains('cbanchored'), true);
  assert.equal(card().style.left, '12px');
  assert.equal(card().style.top, '96px');
  assert.equal(card().style.width, '320px');
  assert.equal(card().style.getPropertyValue('--cb-anchor-h'), '34px');
});

test('a reset with no placement centres the card as before', () => {
  reset();
  window.atriumResetCommandBar('https://example.test/', PLACEMENT);
  window.atriumResetCommandBar();
  assert.equal(card().classList.contains('cbanchored'), false);
  assert.equal(card().style.left, '');
  assert.equal(card().style.width, '');
});

test('the anchored card expands from the row height over one frame', () => {
  reset();
  window.atriumResetCommandBar('https://example.test/', PLACEMENT);
  assert.equal(card().classList.contains('cbexpand'), true,
      'the first frame must be the row height, or the card pops open');
  h.tick();
  assert.equal(card().classList.contains('cbexpand'), false,
      'the class never came off, so the transition had one value');
});

test('Reduce Motion skips the expand but still anchors', () => {
  reset();
  h.setMatchMedia((q) => q.includes('reduced-motion'));
  window.atriumResetCommandBar('https://example.test/', PLACEMENT);
  assert.equal(card().classList.contains('cbanchored'), true);
  assert.equal(card().classList.contains('cbexpand'), false);
  assert.equal(h.pendingFrames, 0, 'a frame was queued for a skipped animation');
  h.setMatchMedia(() => false);
});

test('an anchored reset still selects the prefill', () => {
  reset();
  window.atriumResetCommandBar('https://example.test/page', PLACEMENT);
  const field = input();
  assert.equal(field.value, 'https://example.test/page');
  assert.equal(field.selectionStart, 0);
  assert.equal(field.selectionEnd, field.value.length);
});

test('the anchored card reads its easing from the overlay tokens', () => {
  const dir = path.join(SRC, 'chrome/browser/resources/atrium');
  const css = fs.readFileSync(path.join(dir, 'commandbar.css'), 'utf8');
  const rule = css.match(/\.cbcard\.cbanchored\s*{[^}]*}/)[0];
  assert.match(rule, /position:\s*absolute/);
  assert.match(rule, /var\(--ov-expand-ms\)/);
  assert.match(rule, /var\(--ov-ease\)/);
  const tokens = fs.readFileSync(path.join(dir, 'overlay.css'), 'utf8');
  assert.match(tokens, /--ov-ease:/);
  assert.match(tokens, /--ov-expand-ms:/);
});

// the row hide/show itself lives in sidebar.js and C++
// (CommandBarWidget::Close), out of this page's reach - these tests pin what
// commandbar.js owns: the collapsed frame's look and the close reversal.

test('the collapsed frame borrows the row look, not the card look', () => {
  const dir = path.join(SRC, 'chrome/browser/resources/atrium');
  const css = fs.readFileSync(path.join(dir, 'commandbar.css'), 'utf8');
  const collapsed = css.match(/\.cbcard\.cbanchored\.cbexpand\s*{[^}]*}/)[0];
  assert.match(collapsed, /border-radius:\s*var\(--ov-radius-anchor\)/);
  assert.match(collapsed, /box-shadow:\s*none/);
  const tokens = fs.readFileSync(path.join(dir, 'overlay.css'), 'utf8');
  assert.match(tokens, /--ov-radius-anchor:\s*8px/);
  const grown = css.match(/\.cbcard\.cbanchored\s*{[^}]*}/)[0];
  assert.match(grown, /border-radius/);
  assert.match(grown, /box-shadow/);
});

test('Escape on an anchored card re-collapses before it dismisses', () => {
  reset();
  window.atriumResetCommandBar('https://example.test/', PLACEMENT);
  h.tick();
  h.sent.length = 0;
  key('Escape');
  assert.deepEqual(h.sent, [], 'dismissed before the shrink even started');
  assert.equal(card().classList.contains('cbexpand'), true,
      'Escape must re-collapse the card, not just close it');
  card().dispatchEvent(new Event('transitionend', {propertyName: 'max-height'}));
  assert.deepEqual(h.sent, [['commandBarDismiss', []]]);
});

test('a transitionend for another property does not dismiss early', () => {
  reset();
  window.atriumResetCommandBar('https://example.test/', PLACEMENT);
  h.tick();
  h.sent.length = 0;
  key('Escape');
  card().dispatchEvent(new Event('transitionend', {propertyName: 'box-shadow'}));
  assert.deepEqual(h.sent, [], 'box-shadow settles before max-height does');
  card().dispatchEvent(new Event('transitionend', {propertyName: 'max-height'}));
  assert.deepEqual(h.sent, [['commandBarDismiss', []]]);
});

test('a click outside an anchored card also waits for the shrink', () => {
  reset();
  window.atriumResetCommandBar('https://example.test/', PLACEMENT);
  h.tick();
  h.sent.length = 0;
  click(root.querySelector('.cbscrim'));
  assert.deepEqual(h.sent, [], 'the scrim click must not dismiss instantly');
  card().dispatchEvent(new Event('transitionend', {propertyName: 'max-height'}));
  assert.deepEqual(h.sent, [['commandBarDismiss', []]]);
});

test('Reduce Motion dismisses an anchored card at once', () => {
  reset();
  window.atriumResetCommandBar('https://example.test/', PLACEMENT);
  h.setMatchMedia((q) => q.includes('reduced-motion'));
  h.sent.length = 0;
  key('Escape');
  assert.deepEqual(h.sent, [['commandBarDismiss', []]]);
  h.setMatchMedia(() => false);
});

test('a reopen during the shrink supersedes it - the stale end does not dismiss', () => {
  reset();
  window.atriumResetCommandBar('https://example.test/', PLACEMENT);
  h.tick();
  key('Escape');
  const stale = card();
  window.atriumResetCommandBar('https://example.test/two', PLACEMENT);
  h.sent.length = 0;
  stale.dispatchEvent(new Event('transitionend', {propertyName: 'max-height'}));
  assert.deepEqual(h.sent, [],
      'the shrink that started before the reopen must not close the new one');
});

test('a cancelled shrink still dismisses - the row must not stay hidden', () => {
  reset();
  window.atriumResetCommandBar('https://example.test/', PLACEMENT);
  h.tick();
  h.sent.length = 0;
  key('Escape');
  assert.deepEqual(h.sent, [], 'the dismiss must wait for the shrink');
  card().dispatchEvent(
      new Event('transitioncancel', {propertyName: 'max-height'}));
  assert.deepEqual(h.sent, [['commandBarDismiss', []]]);
});

test('a shrink that never reports still dismisses on the timer', async () => {
  reset();
  window.atriumResetCommandBar('https://example.test/', PLACEMENT);
  h.tick();
  h.sent.length = 0;
  key('Escape');
  assert.deepEqual(h.sent, [], 'the dismiss must wait for the shrink');
  await new Promise((resolve) => setTimeout(resolve, 450));
  assert.deepEqual(h.sent, [['commandBarDismiss', []]]);
});
