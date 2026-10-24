// Copyright 2026 The Atrium Authors
// Backed by atrium_core_shelf_overview via OverviewController::PushShelfList.
// Put on Shelf is out of scope here.

// Titles/quotes/URLs come from web pages. Escapes mirror
// AtriumBrowserWindow::CopyCurrentPageLink's Markdown/Quote copy.
export function escapeMarkdownText(text) {
  let out = '';
  for (const ch of text) {
    if (ch === '\\') {
      out += '\\\\';
    } else if (ch === '[') {
      out += '\\[';
    } else if (ch === ']') {
      out += '\\]';
    } else if (ch.charCodeAt(0) < 0x20) {
      out += ' ';
    } else {
      out += ch;
    }
  }
  return out;
}

export function escapeMarkdownUrl(url) {
  return url.replace(/\(/g, '%28').replace(/\)/g, '%29');
}

export function shelfEntryToMarkdown(entry) {
  let line = `- [${escapeMarkdownText(entry.title)}](${
      escapeMarkdownUrl(entry.url)})`;
  for (const quote of (entry.highlights ?? [])) {
    line += `\n  - "${escapeMarkdownText(quote)}"`;
  }
  return line;
}

export function shelfToMarkdown(entries) {
  return entries.map(shelfEntryToMarkdown).join('\n');
}

export function renderShelfSection() {
  const wrap = document.createElement('div');
  wrap.className = 'ovlist';

  let entries = [];

  function openEntry(entry) {
    chrome.send('shelfListOpen', [entry.id, entry.url]);
  }

  function removeEntry(entry) {
    chrome.send('shelfListRemove', [entry.id]);
  }

  function buildRow(entry) {
    const row = document.createElement('div');
    row.className = 'dlrow';
    row.dataset.shelfEntry = entry.id;

    const ico = document.createElement('span');
    ico.className = 'dlico';
    ico.textContent = entry.kind === 'clip' ? '↗' : '';

    const info = document.createElement('div');
    info.className = 'dlinfo';

    const name = document.createElement('div');
    name.className = 'dln';
    name.textContent = entry.title;

    const cap = document.createElement('div');
    cap.className = 'dlcap';
    const kindLabel = entry.kind === 'clip' ? 'clipped link' : 'page';
    const spaceLabel = entry.spaceName ?? 'removed space';
    const highlightCount = (entry.highlights ?? []).length;
    cap.textContent = `${kindLabel} - ${spaceLabel}${
        highlightCount ?
            ` · ${highlightCount} highlight${highlightCount === 1 ? '' : 's'}` :
            ''}`;

    info.append(name, cap);

    for (const quote of (entry.highlights ?? []).slice(0, 2)) {
      const q = document.createElement('div');
      q.className = 'shq';
      q.textContent = `"${quote}"`;
      info.append(q);
    }

    const openBtn = document.createElement('button');
    openBtn.className = 'dlact';
    openBtn.dataset.shelfOpen = entry.id;
    openBtn.textContent = 'Open';
    openBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      openEntry(entry);
    });

    const removeBtn = document.createElement('button');
    removeBtn.className = 'dlact';
    removeBtn.title = 'Remove from Shelf';
    removeBtn.dataset.shelfRemove = entry.id;
    removeBtn.textContent = '×';
    removeBtn.addEventListener('click', (e) => {
      e.stopPropagation();
      removeEntry(entry);
    });

    row.append(ico, info, openBtn, removeBtn);
    return row;
  }

  function copyAsMarkdown() {
    navigator.clipboard.writeText(shelfToMarkdown(entries));
  }

  function buildHeader() {
    const header = document.createElement('div');
    header.className = 'dlrow';

    const count = document.createElement('span');
    count.style.font = '11px var(--sans)';
    count.style.opacity = '.6';
    count.style.flex = '1';
    count.textContent =
        `${entries.length} item${entries.length === 1 ? '' : 's'}`;

    const copyBtn = document.createElement('button');
    copyBtn.className = 'dlact';
    copyBtn.dataset.shelfCopyMarkdown = '';
    copyBtn.textContent = 'Copy as Markdown';
    copyBtn.addEventListener('click', copyAsMarkdown);

    header.append(count, copyBtn);
    return header;
  }

  function buildEmpty() {
    const empty = document.createElement('div');
    empty.className = 'ovempty';
    empty.textContent = 'Shelf is empty - drag a tab or a page link to the ' +
        'left edge, or right-click a tab → Put on Shelf.';
    return empty;
  }

  function render() {
    wrap.replaceChildren();
    if (!entries.length) {
      wrap.append(buildEmpty());
      return;
    }
    wrap.append(buildHeader());
    for (const entry of entries) {
      wrap.append(buildRow(entry));
    }
  }

  const setShelf = (nextEntries) => {
    entries = Array.isArray(nextEntries) ? nextEntries : [];
    render();
  };
  window.atriumSetShelf = setShelf;

  render();
  // overviewReady's push already ran before this section (mounted lazily on
  // its rail button's first click) registered atriumSetShelf above - ask for
  // a fresh one, same catch-up notesListReady does for Notes.
  chrome.send('shelfListReady', []);

  function dispose() {
    if (window.atriumSetShelf === setShelf) {
      delete window.atriumSetShelf;
    }
  }

  return {root: wrap, dispose};
}
