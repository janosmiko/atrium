// Copyright 2026 The Atrium Authors
// Backed by atrium_core_notes_overview via OverviewController::PushNotesList -
// a real ABI, unlike Downloads' still-unconnected atriumSetDownloads seam.

import {spaceGlyph} from './space-glyph.js';

function previewText(body) {
  return body.split(/\s+/).filter(Boolean).join(' ');
}

function wordCount(body) {
  return body.trim() ? body.trim().split(/\s+/).length : 0;
}

export function renderNotesSection() {
  const wrap = document.createElement('div');
  wrap.className = 'ovlist';

  let entries = [];

  function createNote() {
    chrome.send('notesListCreate', []);
  }

  function openEntry(entry) {
    chrome.send('notesListOpen', [entry.id, entry.spaceIndex ?? -1]);
  }

  function dismissEntry(entry) {
    chrome.send('notesListDismiss', [entry.id]);
  }

  function deleteEntry(entry) {
    const label = entry.spaceName ? `Clear ${entry.spaceName}'s notes pad?` :
                                     `Delete "${entry.title}"?`;
    if (!window.confirm(label)) {
      return;
    }
    chrome.send('notesListDelete', [entry.id]);
  }

  function buildRemoveButton(entry) {
    const btn = document.createElement('button');
    btn.className = 'dlact dlrmb';
    btn.title = 'Remove from list - hold ⌥ to delete';
    btn.dataset.notesRemove = entry.id;
    btn.textContent = '×';
    btn.addEventListener('click', (e) => {
      e.stopPropagation();
      if (e.altKey) {
        deleteEntry(entry);
      } else {
        dismissEntry(entry);
      }
    });
    return btn;
  }

  function buildRow(entry) {
    const row = document.createElement('div');
    row.className = 'dlrow';
    row.dataset.notesEntry = entry.id;

    const ico = document.createElement('span');
    ico.className = 'dlico';
    if (entry.spaceAccent) {
      ico.style.background = entry.spaceAccent;
    }
    if (entry.spaceID) {
      ico.appendChild(spaceGlyph(entry.spaceIcon || '', entry.spaceName || ''));
    } else {
      ico.textContent = '✎';
    }

    const info = document.createElement('div');
    info.className = 'dlinfo';

    const name = document.createElement('div');
    name.className = 'dln';
    name.textContent = entry.title;

    const cap = document.createElement('div');
    cap.className = 'dlcap';
    const words = wordCount(entry.body);
    cap.textContent = `${entry.spaceID ? 'space pad' : 'custom note'} · ${
        words} word${words === 1 ? '' : 's'}`;

    info.append(name, cap);

    const preview = previewText(entry.body);
    if (preview) {
      const quote = document.createElement('div');
      quote.className = 'shq';
      quote.textContent =
          preview.length > 90 ? `${preview.slice(0, 90)}…` : preview;
      info.append(quote);
    }

    const openBtn = document.createElement('button');
    openBtn.className = 'dlact';
    openBtn.dataset.notesOpen = entry.id;
    openBtn.textContent = 'Open';
    openBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      openEntry(entry);
    });

    row.append(ico, info, openBtn, buildRemoveButton(entry));
    return row;
  }

  function buildHeader() {
    const header = document.createElement('div');
    header.className = 'dlrow';

    const count = document.createElement('span');
    count.style.font = '11px var(--sans)';
    count.style.opacity = '.6';
    count.style.flex = '1';
    count.textContent = `${entries.length} note${entries.length === 1 ? '' : 's'}`;

    const newBtn = document.createElement('button');
    newBtn.className = 'dlact';
    newBtn.dataset.notesNew = '';
    newBtn.textContent = '+ New Note';
    newBtn.addEventListener('click', createNote);

    header.append(count, newBtn);
    return header;
  }

  function buildEmpty() {
    const empty = document.createElement('div');
    empty.className = 'ovempty';
    empty.textContent =
        'No notes yet - + New Note here, or /note in the command bar.';
    return empty;
  }

  function render() {
    wrap.replaceChildren();
    wrap.append(buildHeader());
    if (!entries.length) {
      wrap.append(buildEmpty());
      return;
    }
    for (const entry of entries) {
      wrap.append(buildRow(entry));
    }
  }

  const setNotesList = (nextEntries) => {
    entries = Array.isArray(nextEntries) ? nextEntries : [];
    render();
  };
  window.atriumSetNotesList = setNotesList;

  render();
  // overviewReady's push already ran before this section (mounted lazily on
  // its rail button's first click) registered atriumSetNotesList above - ask
  // for a fresh one, same catch-up notesReady does for the standalone pad.
  chrome.send('notesListReady', []);

  function dispose() {
    if (window.atriumSetNotesList === setNotesList) {
      delete window.atriumSetNotesList;
    }
  }

  return {root: wrap, dispose};
}
