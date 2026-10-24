// Copyright 2026 The Atrium Authors
//
// The Command Bar route: the Cmd+T palette (M4). Replaces CommandBarView.swift.
//
// The browser owns the matches. This module owns the field, the selection and
// the keyboard, and it sends one message per user intent. Nothing here decides
// what a match IS - AutocompleteController does that, and every row arrives
// carrying the index it holds in the current AutocompleteResult.
//
// That index is the whole reason accepts do not carry a row position. The list
// is rebuilt on every keystroke, and an index the browser did not issue points
// at whatever now sits in that slot - which, for a palette, means opening the
// wrong page.
//
// No innerHTML anywhere: match titles and URLs are attacker-controlled, and the
// WebUI CSP registers no Trusted Types policy.

import {commandMatchScore} from './command-match.js';
import {previewCommand} from './command-verbs.js';
import {faviconPath, setIconScope, urlIconPath} from './icon-scope.js';
import {icon} from './icons.js';
import {keycaps} from './keycaps.js';

// the row a verb command shows instead of matches. It is not
// selectable and carries no index - it describes what would happen, and the
// palette's accept path never reaches it.
function buildCommandPreview(preview) {
  const row = document.createElement('div');
  row.className = 'cbrow cbcmd' + (preview.ok ? '' : ' cbcmdbad');
  row.dataset.cbCommand = preview.verb;

  const title = document.createElement('span');
  title.className = 'cbtitle ov-title';
  title.textContent = preview.ok ?
      `${preview.spec.label}: ${preview.targets.map((t) => t.title || t.url || '')
                                    .join(', ')}` :
      preview.reason;

  const sub = document.createElement('span');
  sub.className = 'cbsub ov-sub';
  sub.textContent = preview.ok && preview.dest ? preview.dest : preview.verb;

  row.append(title, sub);
  return row;
}

// Rows the palette answers itself, built by atrium_command_bar_actions.cc from
// the native hotkey table and pushed on every open - so a verb and the key
// that runs it cannot drift apart, and each row can show its shortcut.
let actionCatalog = [];

// Below this, a typed query offers no verbs. Every action carries a word
// starting with s or c, so one letter would bury the omnibox rows under the
// whole catalog.
const MIN_ACTION_QUERY = 2;

// The same rule the browser filters favourites, tabs and spaces with, so one
// palette does not hold two ideas of what a query matches. Catalog order is
// kept: the score ranks rows, and the catalog is already ordered by hand.
function actionsFor(query) {
  const typed = String(query || '').trim().toLowerCase();
  if (typed.length < MIN_ACTION_QUERY) {
    return [];
  }
  return actionCatalog.filter(
      (action) => commandMatchScore(typed, action.aliases || []) !== null);
}

// The lab labels the section on the row rather than heading a group, so a
// single favourite does not cost a whole header line (lab 4961).
const SECTION_LABEL = {favorite: 'Favorites', tab: 'Open tab', space: 'Space'};

// bolds every case-insensitive occurrence of a typed query word
// inside `text`. DOM nodes only - the WebUI CSP has no Trusted Types policy,
// so title and subtitle text is attacker-controlled and innerHTML is out.
function appendHighlighted(parent, text, query) {
  const value = text || '';
  const tokens = String(query || '').trim().split(/\s+/)
      .filter((token) => token.length >= 1)
      .map((token) => token.toLowerCase());
  const unique = [...new Set(tokens)].sort((a, b) => b.length - a.length);
  // Longest first, and a token nested inside one already kept is dropped -
  // otherwise the shorter one wins its own span first in the alternation and
  // "at atr" against "atrium" bolds only "at".
  const kept = unique.filter(
      (token, i) => !unique.slice(0, i).some((longer) => longer.includes(token)));
  if (!kept.length) {
    parent.appendChild(document.createTextNode(value));
    return;
  }
  const escaped =
      kept.map((token) => token.replace(/[.*+?^${}()|[\]\\]/g, '\\$&'));
  const re = new RegExp(`(${escaped.join('|')})`, 'gi');
  value.split(re).forEach((part, i) => {
    if (!part) {
      return;
    }
    if (i % 2 === 1) {
      const b = document.createElement('b');
      b.className = 'cbhit';
      b.textContent = part;
      parent.appendChild(b);
    } else {
      parent.appendChild(document.createTextNode(part));
    }
  });
}

// A url/favorite row prefers the site's favicon, a tab row the live tab's.
// Falls back to the kind mark when the path is empty (no scope pushed yet)
// or the image fails to load.
function buildRowIcon(match) {
  let src = '';
  if (match.kind === 'tab') {
    src = faviconPath(match.id);
  } else if (match.kind === 'url' || match.kind === 'favorite') {
    src = urlIconPath(match.url);
  }
  if (!src) {
    const mark = icon(match.kind);
    mark.classList.add('cbicon');
    return mark;
  }
  const img = document.createElement('img');
  img.className = 'cbicon ov-icon';
  img.src = src;
  img.alt = '';
  img.addEventListener('error', () => {
    const mark = icon(match.kind);
    mark.classList.add('cbicon');
    img.replaceWith(mark);
  }, {once: true});
  return img;
}

function buildRow(match, position, query = '') {
  const row = document.createElement('div');
  row.className = 'cbrow ov-row';
  // A favourite carries a url and no index: it is not in the browser's
  // AutocompleteResult, so there is no position to send back.
  if (match.kind === 'favorite') {
    row.classList.add('cbfav');
    row.dataset.cbUrl = match.url || '';
  } else if (match.kind === 'action') {
    row.classList.add('cbaction');
  } else if (match.kind === 'tab') {
    // The live tab id the sidebar uses, for the same reason a favourite
    // carries a url: an open tab is not in the AutocompleteResult.
    row.classList.add('cbtab');
    row.dataset.cbTabId = match.id || '';
  } else if (match.kind === 'space') {
    // A rail position, not an AutocompleteResult one - sidebarSpaceSelected
    // counts the chips the sidebar draws.
    row.classList.add('cbspace');
    row.dataset.cbSpaceIndex = String(match.index);
  } else {
    row.dataset.cbIndex = String(match.index);
  }
  row.dataset.cbPos = String(position);
  if (match.keyword) {
    // The engine Tab would scope to. On the row because that is the
    // only place the offer is visible from outside the module - a profile with
    // no engine and a broken Tab key look identical without it.
    row.dataset.cbKeyword = match.keyword;
  }

  const title = document.createElement('span');
  title.className = 'cbtitle ov-title';
  appendHighlighted(title, match.title || '', query);

  const sub = document.createElement('span');
  sub.className = 'cbsub ov-sub';
  appendHighlighted(
      sub, SECTION_LABEL[match.kind] || match.subtitle || '', query);

  // The lab gives every row a 16px mark before the text (.cmditem .ci,
  // swipe-lab.html:877). Without one the list is a wall of words and a
  // navigation, a search and a favourite are indistinguishable at a glance.
  row.append(buildRowIcon(match), title, sub);

  // Only on a row that has a binding - an empty chip reads as glyphs that
  // failed to load.
  if (match.binding) {
    const chip = keycaps(match.binding);
    chip.classList.add('cbkey');
    row.appendChild(chip);
  }
  return row;
}

export function installCommandBar(root) {
  root.replaceChildren();

  const scrim = document.createElement('div');
  scrim.className = 'cbscrim';

  const card = document.createElement('div');
  card.className = 'cbcard ov-card';

  const field = document.createElement('input');
  field.className = 'cbinput';
  field.setAttribute('type', 'text');
  field.setAttribute('autocomplete', 'off');
  field.setAttribute('spellcheck', 'false');
  field.setAttribute('aria-label', 'Search or enter address');

  const list = document.createElement('div');
  list.className = 'cblist';
  list.setAttribute('role', 'listbox');

  // the engine Tab scoped the palette to. Empty and hidden until
  // then, so an unscoped bar looks exactly as it did.
  const chip = document.createElement('span');
  chip.className = 'cbscope';

  // the Tab-to-search offer beside the field, hidden until the
  // highlighted row carries a keyword and no scope is active.
  const tabHint = document.createElement('span');
  tabHint.className = 'cbtabhint ov-hint';
  const tabHintText = document.createElement('span');
  tabHintText.className = 'cbtabhint-text';
  tabHint.append(tabHintText, keycaps('Tab'));

  const fieldRow = document.createElement('div');
  fieldRow.className = 'cbfield';
  // tabHint after field, not the chip - it grows to fill the row, and only
  // trailing it puts the hint at the right edge.
  fieldRow.append(chip, field, tabHint);

  const foot = document.createElement('div');
  foot.className = 'cbfoot ov-foot';
  const open = document.createElement('span');
  open.textContent = 'Open';
  open.appendChild(keycaps('↩'));
  const close = document.createElement('span');
  close.textContent = 'Close';
  close.appendChild(keycaps('esc'));
  foot.append(open, close);

  card.append(fieldRow, list, foot);
  scrim.appendChild(card);
  root.appendChild(scrim);

  // Guards the deferred .cbexpand removal below and dismissCommandBar's
  // transitionend wait against a superseded open/close (deferred ledger).
  let anchorToken = 0;

  // A placement puts the card on the row that was clicked; without one it
  // stays where the scrim's flex box puts it. The expand class comes off on
  // the next frame so the transition has two heights to move between, and
  // Reduce Motion never adds it at all.
  const anchorCard = (placement) => {
    const token = ++anchorToken;
    if (!placement) {
      card.classList.remove('cbanchored', 'cbexpand');
      card.style.left = '';
      card.style.top = '';
      card.style.width = '';
      card.style.removeProperty('--cb-anchor-h');
      return;
    }
    card.style.left = placement.left + 'px';
    card.style.top = placement.top + 'px';
    card.style.width = placement.width + 'px';
    card.style.setProperty('--cb-anchor-h', placement.height + 'px');
    card.classList.add('cbanchored');
    if (globalThis.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches) {
      return;
    }
    card.classList.add('cbexpand');
    requestAnimationFrame(() => {
      if (token === anchorToken) {
        card.classList.remove('cbexpand');
      }
    });
  };

  // Close() (atrium_command_bar_widget.mm) only un-hides the row once this
  // message reaches it, so waiting for the shrink's transitionend is what
  // keeps the row from reappearing before the box lands back on it.
  const dismissCommandBar = () => {
    const reduceMotion =
        globalThis.matchMedia?.('(prefers-reduced-motion: reduce)')?.matches;
    if (!card.classList.contains('cbanchored') || reduceMotion) {
      chrome.send('commandBarDismiss', []);
      return;
    }
    const token = ++anchorToken;
    let settleTimer = 0;
    const settle = () => {
      card.removeEventListener('transitionend', onTransitionEnd);
      card.removeEventListener('transitioncancel', onTransitionEnd);
      clearTimeout(settleTimer);
      // A reopen since this shrink started owns the card now.
      if (token === anchorToken) {
        chrome.send('commandBarDismiss', []);
      }
    };
    const onTransitionEnd = (e) => {
      if (e.target !== card || e.propertyName !== 'max-height') {
        return;
      }
      settle();
    };
    card.addEventListener('transitionend', onTransitionEnd);
    // A cancelled or never-painted transition fires no transitionend, and a
    // lost dismiss leaves the row hidden for good.
    card.addEventListener('transitioncancel', onTransitionEnd);
    settleTimer = setTimeout(settle, 400);
    card.classList.add('cbexpand');
  };

  // The matches of the CURRENT pass, and which one is highlighted. Selection is
  // held as a position here and translated to a match index on the way out.
  let matches = [];
  let selected = 0;
  // The query the last pass answered. Null until one lands, which is what keeps
  // the empty-state row off the screen while a pass is still in flight.
  let answeredQuery = null;
  // The query the last push carried, answered or not. Several passes land for
  // one query now, and this is how a push for the SAME query is told
  // from the first push for a new one - the first resets the highlight, the
  // rest must not.
  let pushedQuery = null;
  // The rows a verb resolves against. Pushed with the snapshot, the same way
  // favourites are.
  let tabs = [];
  // the engine the palette is scoped to, or null. `restore` is the
  // text the field held before Tab, which Escape and an emptying Backspace put
  // back - without it, entering a scope by accident loses what was typed.
  let scope = null;
  // how much of the field the user typed, when the rest of it is a
  // completion this module appended and selected. -1 when the field holds no
  // completion, which is what keeps Cmd+L's prefill selection out of it.
  let inlineTyped = -1;
  // A completion is a guess about text being typed forward, so a deletion must
  // not be answered with one. Set by Backspace or Delete and cleared by the
  // next character (Chrome tracks the same thing).
  let suppressInline = false;

  // Whether the field is still showing the completion that was applied. The
  // selection is checked, not just the flag: a click or a Cmd+L prefill can
  // move it without telling this module.
  function inlineShowing() {
    return inlineTyped >= 0 && field.selectionStart === inlineTyped &&
        field.selectionEnd === field.value.length &&
        field.selectionStart < field.selectionEnd;
  }

  function paintScope() {
    chip.textContent = scope ? scope.engine : '';
    chip.classList.toggle('cbscopeon', Boolean(scope));
  }

  // Reads the highlighted row directly rather than the DOM, since
  // paintSelection only marks a row selected - it does not say what it holds.
  function paintTabHint() {
    const match = matches[selected];
    const show = Boolean(!scope && match && match.keyword);
    tabHint.classList.toggle('cbtabhinton', show);
    if (show) {
      tabHintText.textContent = `Search ${match.engine || match.keyword}`;
    }
  }

  // The omnibox has no scope flag: AutocompleteInput parses a leading keyword
  // out of the query itself, which is how keyword mode is entered.
  function sendQuery() {
    chrome.send(
        'commandBarQuery',
        [scope ? `${scope.keyword} ${field.value}` : field.value]);
  }

  // The omnibox fills inline_autocompletion on the match it would
  // navigate to: the rest of the text it believes is being typed. Appended and
  // left selected, so Return goes there and the next character replaces it.
  function applyInline(snapshot) {
    if (scope || suppressInline) {
      return;
    }
    const typed = field.value;
    // Only a pass for the text the field STILL holds. A pass answering an
    // older query would complete a prefix the user has already typed past, and
    // a pass answering the completed text would append its tail twice.
    if (!typed || !snapshot || snapshot.query !== typed) {
      return;
    }
    // The RENDERED first row. The hygiene pass reorders and drops omnibox
    // rows, so completing from any other row would put a destination in the
    // field that Return does not go to.
    const top = matches[0];
    if (!top || !top.inlineCompletion) {
      return;
    }
    field.value = typed + top.inlineCompletion;
    field.setSelectionRange(typed.length, field.value.length);
    inlineTyped = typed.length;
  }

  // Right and End take the completion into the query rather than moving the
  // caret, which is the omnibox's own behaviour.
  function acceptInline() {
    field.setSelectionRange(field.value.length, field.value.length);
    inlineTyped = -1;
    sendQuery();
  }

  function enterScope(match) {
    scope = {keyword: match.keyword, engine: match.engine || match.keyword,
             restore: field.value};
    field.value = '';
    paintScope();
    // The rows on screen answered the pre-Tab text, so an Enter arriving before
    // the scoped pass lands would accept one of them.
    matches = [];
    selected = 0;
    render();
    sendQuery();
  }

  function leaveScope() {
    field.value = scope.restore;
    scope = null;
    paintScope();
    paintTabHint();
    sendQuery();
  }

  // Enter pressed before the async AutocompleteController pass for
  // the current query has settled used to be a silent no-op - matches is
  // empty between a keystroke and the next atriumSetCommandBar call (see
  // atrium_autocomplete_bridge.cc, OnResultChanged: only the final pass is
  // pushed). commandBarAccept takes a match index, not raw text, and no
  // sibling message accepts one (atrium_command_bar_widget.mm), so there is
  // no ABI to act on the typed text directly. Instead the keystroke is
  // remembered and honored against the top match of the pass that lands
  // next, bounded so a query that never resolves does not wait forever.
  let pendingAccept = false;
  let pendingAcceptTimer = null;

  function clearPendingAccept() {
    pendingAccept = false;
    if (pendingAcceptTimer !== null) {
      clearTimeout(pendingAcceptTimer);
      pendingAcceptTimer = null;
    }
  }

  // What identifies a row across two passes for the same query. Not the match
  // index: the browser re-ranks the whole result on every pass, so a row keeps
  // its text and takes a new index. The id/url tie-breaks same-titled rows.
  function rowKey(match) {
    return `${match.kind}\u0000${match.title || ''}\u0000${match.subtitle || ''}\u0000${match.id || match.url || ''}`;
  }

  function paintSelection() {
    const rows = list.querySelectorAll('.cbrow');
    for (const row of rows) {
      const isSelected = Number(row.dataset.cbPos) === selected;
      row.classList.toggle('cbsel', isSelected);
      row.classList.toggle('ov-sel', isSelected);
    }
    paintTabHint();
  }

  // Returns whether the field holds a command, in which case the list shows its
  // preview and nothing else.
  function renderCommandPreview() {
    // Inside a scope the field holds search terms, so a word that happens to
    // read as a verb is not one.
    const preview = scope ? null : previewCommand(field.value, tabs);
    if (!preview) {
      return false;
    }
    matches = [];
    selected = 0;
    list.replaceChildren(buildCommandPreview(preview));
    paintTabHint();
    return true;
  }

  // A row saying nothing matched, so Return on an unknown word answers instead
  // of doing nothing at all - the silence that got this reported as the prompt
  // being broken. Only shown once a pass for the CURRENT text has landed:
  // between a keystroke and its pass the list is legitimately empty, and saying
  // "no match" there would flash on every word typed.
  function buildEmptyRow(typed) {
    const row = document.createElement('div');
    row.className = 'cbrow cbempty';
    const title = document.createElement('span');
    title.className = 'cbtitle ov-title';
    title.textContent = `No command or match for "${typed}"`;
    const sub = document.createElement('span');
    sub.className = 'cbsub ov-sub';
    sub.textContent = 'Try settings, courtyard, overview, new space';
    row.append(title, sub);
    return row;
  }

  function render() {
    list.replaceChildren();
    if (!matches.length) {
      const typed = field.value.trim();
      if (answeredQuery !== null && answeredQuery === field.value && typed) {
        list.appendChild(buildEmptyRow(typed));
      }
      paintSelection();
      return;
    }
    matches.forEach((match, position) => {
      list.appendChild(buildRow(match, position, pushedQuery || ''));
    });
    paintSelection();
  }

  function move(delta) {
    if (!matches.length) {
      return;
    }
    clearPendingAccept();
    selected = (selected + delta + matches.length) % matches.length;
    paintSelection();
  }

  function accept(position) {
    // A typed command owns the whole list, so `matches` is empty and there is
    // no row for `position` to name. Running it here is what makes Return do
    // something - before this the preview was the only thing a command ever
    // produced.
    // A completion is the browser's guess, not typed text, so the field can
    // spell a verb the user never wrote - "clo" completing to "close 1" would
    // close a tab on a keystroke meant for the highlighted row.
    const preview =
        scope || inlineShowing() ? null : previewCommand(field.value, tabs);
    if (preview) {
      clearPendingAccept();
      if (!preview.ok) {
        return;  // The row already says why, and nothing should change.
      }
      for (const target of preview.targets) {
        chrome.send(preview.spec.send, [target.id]);
      }
      chrome.send('commandBarDismiss', []);
      return;
    }
    const match = matches[position];
    if (!match) {
      return;
    }
    clearPendingAccept();
    if (match.kind === 'action') {
      chrome.send(match.send, match.args || []);
      // The palette dismisses itself rather than the browser doing it for us:
      // a command sends one message per tab, so a close driven from the native
      // side would end the palette after the first one.
      chrome.send('commandBarDismiss', []);
      return;
    }
    if (match.kind === 'favorite') {
      // By url, through the same handler a favourite tile click takes, so the
      // palette and the sidebar cannot open a favourite differently
      //.
      chrome.send('commandBarFavoriteAccept', [match.url || '']);
      return;
    }
    if (match.kind === 'tab') {
      // The message a sidebar row click sends, so the two surfaces cannot
      // activate a tab differently. The widget only forwards it, so the
      // dismiss is ours to send.
      chrome.send('sidebarTabClicked', [match.id || '']);
      chrome.send('commandBarDismiss', []);
      return;
    }
    if (match.kind === 'space') {
      // The message a rail chip click sends, so the two surfaces cannot switch
      // spaces differently. The widget only forwards it, so the dismiss is
      // ours to send.
      chrome.send('sidebarSpaceSelected', [match.index]);
      chrome.send('commandBarDismiss', []);
      return;
    }
    // The omnibox's own suggestions are kind 'search' too and DO have a
    // position. Only Atrium's engine rows carry a keyword with none, and they
    // fell through to commandBarAccept(-1), which resolves to nothing.
    // The omnibox hangs an associated keyword on its own default row too, so a
    // keyword that IS the whole typed text must beat the position, or
    // accepting that row by index searches the DEFAULT provider for the word.
    const typedTheKeyword = !scope && Boolean(match.keyword) &&
        field.value.trim().toLowerCase() === match.keyword.toLowerCase();
    if (match.kind === 'search' && match.keyword &&
        (typedTheKeyword ||
         !(typeof match.index === 'number' && match.index >= 0))) {
      chrome.send('commandBarEngineAccept', [match.keyword]);
      return;
    }
    chrome.send('commandBarAccept', [match.index]);
  }

  window.atriumSetCommandBar = (snapshot) => {
    // Favourites lead. They are the user's own list, and burying them under
    // whatever the omnibox ranked first would make the section decorative.
    tabs = (snapshot && snapshot.tabs) || tabs;
    // Only a FINAL pass can say nothing matched. The provisional
    // push is made before the omnibox is asked, and the passes between it and
    // the last one can still be followed by rows - treating either as an answer
    // flashed "No command or match" the instant a url was typed.
    answeredQuery = snapshot && snapshot.final &&
            typeof snapshot.query === 'string' ?
        snapshot.query :
        null;
    // Held BEFORE the list is replaced, so the row the user chose can be found
    // again in the new one.
    const pushQuery =
        snapshot && typeof snapshot.query === 'string' ? snapshot.query : null;
    const held =
        pushQuery !== null && pushQuery === pushedQuery ? matches[selected] : null;
    pushedQuery = pushQuery;
    // Open tabs sit between the two: the window's own contents beat a guess
    // about where the user might want to go, and a fresh profile has no
    // favourites, which is what made the palette open blank.
    matches = [...actionsFor(snapshot && snapshot.query),
               ...((snapshot && snapshot.favorites) || []),
               ...((snapshot && snapshot.tabMatches) || []),
               ...((snapshot && snapshot.spaceMatches) || []),
               ...((snapshot && snapshot.matches) || [])];
    // Back to the default match whenever the query changes: the browser
    // re-ranks the whole result, so carrying the old highlight forward would
    // point it at an unrelated row. Within ONE query the highlight stays on the
    // row the user chose - passes now stream, and resetting on each
    // of them would undo an arrow key a fraction of a second after it was
    // pressed.
    selected = 0;
    if (held) {
      const key = rowKey(held);
      const at = matches.findIndex((match) => rowKey(match) === key);
      if (at >= 0) {
        selected = at;
      }
    }
    render();
    // The pass this query was waiting on just landed - honor the Enter that
    // was pressed too early against its top match now instead of dropping it.
    // Only if this pass is actually FOR the text the field still holds: the
    // snapshot carries the query it was built from (BuildCommandBarMatchesJSON),
    // and checking it here is what stops a pass for an older query - one still
    // in flight when the user kept typing - from being accepted as if it
    // answered the current one.
    const query = snapshot && snapshot.query;
    if (pendingAccept && matches.length && query === field.value) {
      accept(0);
      return;
    }
    // After the pending accept, never before it: completing the field changes
    // the text the guard above compares against.
    applyInline(snapshot);
  };

  // Called from CommandBarWidget::Show, so a palette reopened after an accept
  // does not come back holding the last query. `prefill` is the current tab's
  // URL on an edit open and absent on every other one. `placement` is the
  // clicked row's box, absent when the palette opens centred.
  // Pushed once per open rather than per pass: the rows only change when a
  // binding does, so a rebind is in force by the next time the palette opens.
  window.atriumSetCommandBarActions = (payload) => {
    actionCatalog = (payload && payload.actions) || [];
  };

  // Pushed the same way actions are: once per open, from CommandBarWidget.
  window.atriumSetCommandBarIconScope = (token) => setIconScope(token);

  window.atriumResetCommandBar = (prefill, placement) => {
    clearPendingAccept();
    anchorCard(placement);
    field.value = typeof prefill === 'string' ? prefill : '';
    scope = null;
    paintScope();
    matches = [];
    selected = 0;
    answeredQuery = null;
    pushedQuery = null;
    inlineTyped = -1;
    suppressInline = false;
    render();
    field.focus();
    if (field.value) {
      // Selected, not just filled, so the next keystroke replaces the URL the
      // way it does in a location bar - no keydown handling needed for that.
      field.select();
    }
    // Show() resets the page and asks for nothing, so every open after the
    // first came up blank - only commandBarReady, which fires once per
    // document, ever ran the empty query.
    chrome.send('commandBarQuery', [field.value]);
  };

  field.addEventListener('input', () => {
    // A fresh keystroke invalidates any Enter that was still waiting on the
    // previous query's pass - the pass that lands next answers this query.
    clearPendingAccept();
    // Whatever tail the last pass selected was part of this edit, so it is
    // typed text now and no longer a completion to accept.
    inlineTyped = -1;
    // a verb command previews locally and asks the browser for
    // nothing. Sending the query anyway would race an autocomplete pass in
    // behind the preview and replace it.
    if (renderCommandPreview()) {
      return;
    }
    sendQuery();
  });

  field.addEventListener('keydown', (e) => {
    // Before any handler below: a deletion must not be answered with
    // a completion, and the character after it starts guessing again.
    if (e.key === 'Backspace' || e.key === 'Delete') {
      suppressInline = true;
      inlineTyped = -1;
    } else if (e.key.length === 1 && !e.metaKey && !e.ctrlKey) {
      suppressInline = false;
    }
    if (e.key === 'ArrowDown') {
      e.preventDefault();
      move(1);
      return;
    }
    if (e.key === 'ArrowUp') {
      e.preventDefault();
      move(-1);
      return;
    }
    if ((e.key === 'ArrowRight' || e.key === 'End') && inlineShowing()) {
      e.preventDefault();
      acceptInline();
      return;
    }
    if (e.ctrlKey && !e.metaKey && e.key === 'n') {
      e.preventDefault();
      move(1);
      return;
    }
    if (e.ctrlKey && !e.metaKey && e.key === 'p') {
      e.preventDefault();
      move(-1);
      return;
    }
    if (e.key === 'Enter') {
      e.preventDefault();
      if (matches.length) {
        accept(selected);
        return;
      }
      // No pass has landed for the current text yet. Do not drop the
      // keystroke - remember it and act on the top match of the
      // next pass, bounded so a query that never resolves does not hang.
      if (field.value.trim() !== '') {
        pendingAccept = true;
        if (pendingAcceptTimer !== null) {
          clearTimeout(pendingAcceptTimer);
        }
        pendingAcceptTimer = setTimeout(clearPendingAccept, 800);
      }
      return;
    }
    // Tab scopes the palette to the highlighted row's engine, and
    // the two ways out of a scope come before the dismiss below: the first
    // Escape leaves the scope, only the second closes the bar.
    if (e.key === 'Tab') {
      // Every Tab, not only a scoping one: nothing else in the card takes
      // focus, so a Tab that reached the browser took key off the field.
      e.preventDefault();
      const row = e.shiftKey || scope ? null : matches[selected];
      if (row && row.keyword) {
        clearPendingAccept();
        enterScope(row);
      }
      return;
    }
    if (scope && (e.key === 'Escape' ||
                  (e.key === 'Backspace' && field.value === ''))) {
      e.preventDefault();
      clearPendingAccept();
      leaveScope();
      return;
    }
    if (e.key === 'Escape') {
      e.preventDefault();
      clearPendingAccept();
      dismissCommandBar();
      return;
    }
    // Shift+Delete is Chrome's own gesture for forgetting a suggestion. Only
    // the matches the browser marked deletable can answer it.
    if (e.key === 'Delete' && e.shiftKey) {
      const match = matches[selected];
      if (match && match.deletable) {
        e.preventDefault();
        chrome.send('commandBarDeleteMatch', [match.index]);
      }
    }
    // A text selection means the native copy is the right one. An omnibox
    // url row keeps its URL in subtitle, or title with no description
    // (RowTextForMatch); a search row has none.
    if (e.metaKey && e.key === 'c' &&
        field.selectionStart === field.selectionEnd) {
      const match = matches[selected];
      const url = match &&
          (match.url ||
           (match.kind === 'url' ? (match.subtitle || match.title) : ''));
      if (url && navigator.clipboard) {
        e.preventDefault();
        navigator.clipboard.writeText(url).catch(() => {});
      }
    }
  });

  root.addEventListener('click', (e) => {
    const row = e.target.closest('.cbrow');
    if (row) {
      accept(Number(row.dataset.cbPos));
      return;
    }
    // Anything outside the card is the scrim, and a click there means dismiss.
    if (!e.target.closest('.cbcard')) {
      // A dismiss this way skipped clearPendingAccept - Escape's path cleared
      // it, this one did not - leaving a flag that could still fire once the
      // bar reopens and a pass lands before the reopen's own reset runs.
      clearPendingAccept();
      dismissCommandBar();
    }
  });

  // The first open cannot be served by atriumResetCommandBar: the widget calls
  // it before this document exists, and a push that lands before the page is
  // live is dropped. So the field claims focus as it installs, which is the
  // one open where that is the only thing that will.
  field.focus();

  chrome.send('commandBarReady', []);
}
