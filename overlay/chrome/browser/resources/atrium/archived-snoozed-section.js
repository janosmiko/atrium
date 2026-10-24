// Copyright 2026 The Atrium Authors
// The Archived list is also the tab graveyard: a plain tab close
// lands here too now, alongside the TTL sweep and explicit Archive actions.

import {urlIconPath} from './icon-scope.js';

// Seconds since 2001-01-01 (Swift's reference date), the same convention
// spaces.rs's SwiftDate uses - not the Unix epoch new Date() expects.
const SWIFT_TO_UNIX_EPOCH_OFFSET_S = 978307200;

function formatSwiftInstant(instant) {
  const ms = (instant + SWIFT_TO_UNIX_EPOCH_OFFSET_S) * 1000;
  return new Date(ms).toLocaleString(
      [], {month: 'short', day: 'numeric', hour: 'numeric', minute: '2-digit'});
}

// mutate.rs's CloseReason, camelCased over the wire.
const CLOSE_REASON_LABEL = {
  userClosed: 'Closed',
  archived: 'Archived',
  windowClosed: 'Window closed',
};

function archivedCaption(entry) {
  const space = entry.spaceName ?? 'removed space';
  const reason = CLOSE_REASON_LABEL[entry.reason] ?? 'Archived';
  return `${space} - ${reason} · ${formatSwiftInstant(entry.archivedAt)}`;
}

// A graveyard row has no live tab, so its icon comes from the URL-keyed
// favicon cache (the /urlicon/ route) rather than the tab-keyed one.
function buildArchivedIcon(entry) {
  const ico = document.createElement('span');
  ico.className = 'dlico';
  const img = document.createElement('img');
  img.className = 'dlicoimg';
  img.setAttribute('loading', 'lazy');
  img.src = urlIconPath(entry.url);
  img.alt = '';
  img.addEventListener('error', () => img.remove());
  ico.appendChild(img);
  return ico;
}

// Shared shape for the Archived and Snoozed overview lists: a
// row list with one primary action (Restore/Wake) and one destructive action
// (delete forever), each naming its originating space.
function renderRowListSection({
  windowSetterName,
  readyMessage,
  emptyText,
  buildCaption,
  buildIcon = () => {
    const ico = document.createElement('span');
    ico.className = 'dlico';
    return ico;
  },
  primaryLabel,
  sendPrimary,
  sendDelete,
  deleteConfirmLabel,
}) {
  const wrap = document.createElement('div');
  wrap.className = 'ovlist';

  let entries = [];

  function deleteForever(entry) {
    if (!window.confirm(deleteConfirmLabel(entry))) {
      return;
    }
    sendDelete(entry);
  }

  function buildRow(entry) {
    const row = document.createElement('div');
    row.className = 'dlrow';

    const ico = buildIcon(entry);

    const info = document.createElement('div');
    info.className = 'dlinfo';

    const name = document.createElement('div');
    name.className = 'dln';
    name.textContent = entry.title;

    const cap = document.createElement('div');
    cap.className = 'dlcap';
    cap.textContent = buildCaption(entry);

    info.append(name, cap);

    const primaryBtn = document.createElement('button');
    primaryBtn.className = 'dlact';
    primaryBtn.textContent = primaryLabel;
    primaryBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      sendPrimary(entry);
    });

    const deleteBtn = document.createElement('button');
    deleteBtn.className = 'dlact';
    deleteBtn.title = 'Delete forever';
    deleteBtn.textContent = '×';
    deleteBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      deleteForever(entry);
    });

    row.append(ico, info, primaryBtn, deleteBtn);
    return row;
  }

  function buildEmpty() {
    const empty = document.createElement('div');
    empty.className = 'ovempty';
    empty.textContent = emptyText;
    return empty;
  }

  function render() {
    wrap.replaceChildren();
    if (!entries.length) {
      wrap.append(buildEmpty());
      return;
    }
    for (const entry of entries) {
      wrap.append(buildRow(entry));
    }
  }

  const setter = (nextEntries) => {
    entries = Array.isArray(nextEntries) ? nextEntries : [];
    render();
  };
  window[windowSetterName] = setter;

  render();
  // overviewReady's push already ran before this section (mounted lazily on
  // its rail button's first click) registered the setter above - ask for a
  // fresh one, same catch-up notesListReady does for Notes.
  chrome.send(readyMessage, []);

  function dispose() {
    if (window[windowSetterName] === setter) {
      delete window[windowSetterName];
    }
  }

  return {root: wrap, dispose};
}

export function renderArchivedSection() {
  return renderRowListSection({
    windowSetterName: 'atriumSetArchived',
    readyMessage: 'archivedListReady',
    emptyText:
        'Nothing archived yet - closed and archived tabs land here.',
    buildCaption: archivedCaption,
    buildIcon: buildArchivedIcon,
    primaryLabel: 'Restore',
    sendPrimary: (entry) => chrome.send(
        'archivedListRestore', [entry.id, entry.url, entry.spaceIndex ?? -1]),
    sendDelete: (entry) => chrome.send('archivedListDelete', [entry.id]),
    deleteConfirmLabel: (entry) => `Delete "${entry.title}" forever?`,
  });
}

export function renderSnoozedSection() {
  return renderRowListSection({
    windowSetterName: 'atriumSetSnoozed',
    readyMessage: 'snoozedListReady',
    emptyText:
        'Nothing snoozed - hold Option and click a tab\'s close button.',
    buildCaption: (entry) => {
      const space = entry.spaceName ?? 'removed space';
      const alert = entry.alert ? ' · alert set' : '';
      return `${space} - returns ${formatSwiftInstant(entry.wakeAt)}${alert}`;
    },
    primaryLabel: 'Wake',
    sendPrimary: (entry) => chrome.send('snoozedListWake', [
      entry.id, entry.url, entry.spaceIndex ?? -1, entry.title,
      !!entry.alert
    ]),
    sendDelete: (entry) => chrome.send('snoozedListDelete', [entry.id]),
    deleteConfirmLabel: (entry) => `Delete "${entry.title}" forever?`,
  });
}
