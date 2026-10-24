// Copyright 2026 The Atrium Authors

const SVG_NS = 'http://www.w3.org/2000/svg';
const MEDIA_RE = /\.(png|jpe?g|gif|mp4|mov|webm|heic)$/i;
const DAY_MS = 24 * 60 * 60 * 1000;

export function isMediaFile(name) {
  return MEDIA_RE.test(name);
}

// Port of DownloadsGrouping's Today/Yesterday day buckets.
export function dayLabel(receivedAt, now = Date.now()) {
  const startOfDay = (ms) => {
    const d = new Date(ms);
    d.setHours(0, 0, 0, 0);
    return d.getTime();
  };
  const diffDays =
      Math.round((startOfDay(now) - startOfDay(receivedAt)) / DAY_MS);
  if (diffDays === 0) return 'Today';
  if (diffDays === 1) return 'Yesterday';
  return new Date(receivedAt).toLocaleDateString(
      undefined, {month: 'short', day: 'numeric'});
}

function extLabel(name) {
  const dot = name.lastIndexOf('.');
  return dot === -1 ? '' : name.slice(dot + 1).toUpperCase().slice(0, 3);
}

export function filterItems(items, query, spaceId) {
  const q = query.trim().toLowerCase();
  return items.filter((item) => {
    if (spaceId !== null && item.spaceId !== spaceId) {
      return false;
    }
    if (q && !item.name.toLowerCase().includes(q)) {
      return false;
    }
    return true;
  });
}

// Insertion order == first-seen order in `items`, so callers must pass
// items already sorted newest-first for the groups to come out that way.
export function groupByDay(items, now = Date.now()) {
  const groups = new Map();
  for (const item of items) {
    const label = dayLabel(item.receivedAt, now);
    if (!groups.has(label)) {
      groups.set(label, []);
    }
    groups.get(label).push(item);
  }
  return groups;
}

function spacesPresent(items) {
  const seen = new Map();
  for (const item of items) {
    if (item.spaceId != null && !seen.has(item.spaceId)) {
      seen.set(item.spaceId, item);
    }
  }
  return [...seen.values()];
}

function buildSearchIcon() {
  const svg = document.createElementNS(SVG_NS, 'svg');
  svg.setAttribute('viewBox', '0 0 12 12');
  svg.setAttribute('width', '12');
  svg.setAttribute('height', '12');
  const circle = document.createElementNS(SVG_NS, 'circle');
  circle.setAttribute('cx', '5');
  circle.setAttribute('cy', '5');
  circle.setAttribute('r', '3.4');
  circle.setAttribute('fill', 'none');
  circle.setAttribute('stroke', 'currentColor');
  circle.setAttribute('stroke-width', '1.4');
  const tail = document.createElementNS(SVG_NS, 'path');
  tail.setAttribute('d', 'M7.6 7.6l2.6 2.6');
  tail.setAttribute('stroke', 'currentColor');
  tail.setAttribute('stroke-width', '1.4');
  tail.setAttribute('stroke-linecap', 'round');
  svg.append(circle, tail);
  return svg;
}

export function renderDownloadsSection() {
  const wrap = document.createElement('div');
  wrap.className = 'ovlist';

  let items = [];
  let query = '';
  let spaceFilter = null;
  // the chip bar starts collapsed, as the lab's does
  // (swipe-lab.html:3018). A row of space chips above every download list is a
  // permanent cost for an occasional filter.
  let filterOpen = false;
  let searchDebounce = null;

  function removeFromList(item) {
    chrome.send('downloadsRemove', [item.id]);
  }

  function deleteFromDisk(item) {
    // Same 18x18 dlact.dlrmb button as the plain remove path - only the
    // branch taken on click differs, so geometry never shifts on Option.
    if (!window.confirm(`Delete "${item.name}" from disk?`)) {
      return;
    }
    chrome.send('downloadsDeleteFromDisk', [item.id]);
  }

  function buildRemoveButton(item) {
    const btn = document.createElement('button');
    btn.className = 'dlact dlrmb';
    btn.title = 'Remove from list - hold ⌥ to delete the file';
    btn.dataset.dlRemove = item.id;
    btn.textContent = '×';
    btn.addEventListener('click', (e) => {
      e.stopPropagation();
      if (e.altKey) {
        deleteFromDisk(item);
      } else {
        removeFromList(item);
      }
    });
    return btn;
  }

  function buildShowButton(item) {
    const btn = document.createElement('button');
    btn.className = 'dlact';
    btn.title = 'Show in Finder';
    btn.dataset.dlShow = item.id;
    btn.textContent = '⌕';
    btn.addEventListener('click', (e) => {
      e.stopPropagation();
      chrome.send('downloadsShowInFinder', [item.id]);
    });
    return btn;
  }

  function buildTile(item) {
    const tile = document.createElement('div');
    tile.className = 'dltile';
    tile.dataset.dlTile = item.id;

    const preview = document.createElement('div');
    preview.className = 'tprev';
    preview.textContent = extLabel(item.name);

    const name = document.createElement('div');
    name.className = 'tname';
    name.textContent = item.name;

    const actions = document.createElement('div');
    actions.className = 'tacts';
    actions.append(buildShowButton(item), buildRemoveButton(item));

    tile.append(preview, name, actions);
    return tile;
  }

  function buildRow(item) {
    const row = document.createElement('div');
    row.className = 'dlrow';

    const ico = document.createElement('span');
    ico.className = 'dlico';
    ico.textContent = extLabel(item.name);

    const info = document.createElement('div');
    info.className = 'dlinfo';
    const name = document.createElement('div');
    name.className = 'dln';
    name.textContent = item.name;
    info.append(name);

    if (item.status === 'active') {
      const bar = document.createElement('div');
      bar.className = 'dlbar' + (item.indeterminate ? ' indeterminate' : '');
      if (!item.indeterminate) {
        const fill = document.createElement('span');
        fill.style.width = `${item.progress ?? 0}%`;
        bar.append(fill);
      }
      const cap = document.createElement('div');
      cap.className = 'dlcap';
      cap.textContent = item.captionActive ?? '';
      info.append(bar, cap);
    } else {
      const cap = document.createElement('div');
      cap.className = 'dlcap';
      cap.textContent =
          item.spaceName ? `${item.sizeLabel} · ${item.spaceName}` :
                            item.sizeLabel ?? '';
      info.append(cap);
    }

    row.append(ico, info, buildShowButton(item), buildRemoveButton(item));
    return row;
  }

  function buildTools() {
    const tools = document.createElement('div');
    tools.className = 'dltools';

    const search = document.createElement('span');
    search.className = 'dlsearch';
    const input = document.createElement('input');
    input.type = 'text';
    input.placeholder = 'Search downloads…';
    input.value = query;
    input.dataset.dlSearch = '';
    input.addEventListener('mousedown', (e) => e.stopPropagation());
    input.addEventListener('input', () => {
      query = input.value;
      clearTimeout(searchDebounce);
      searchDebounce = setTimeout(renderResults, 140);
    });
    search.append(buildSearchIcon(), input);

    const filter = document.createElement('button');
    filter.type = 'button';
    filter.className = 'dltbtn' + (filterOpen ? ' on' : '');
    filter.title = 'Filter by space';
    filter.dataset.dlFilterToggle = '';
    filter.appendChild(buildFilterIcon());
    // The dot is the only sign a filter is on once the chips are hidden -
    // without it a collapsed bar and an unfiltered list look identical.
    if (!filterOpen && spaceFilter !== null) {
      const active = document.createElement('span');
      active.className = 'factive';
      filter.appendChild(active);
    }
    filter.addEventListener('click', () => {
      filterOpen = !filterOpen;
      renderToolsChipsAndResults();
    });

    tools.append(search, filter);
    return tools;
  }

  function buildFilterIcon() {
    const svg = document.createElementNS(SVG_NS, 'svg');
    svg.setAttribute('viewBox', '0 0 12 12');
    svg.setAttribute('width', '12');
    svg.setAttribute('height', '12');
    const path = document.createElementNS(SVG_NS, 'path');
    path.setAttribute('d', 'M1.5 2h9L7 6.4V10L5 8.8V6.4z');
    path.setAttribute('fill', 'none');
    path.setAttribute('stroke', 'currentColor');
    path.setAttribute('stroke-width', '1.3');
    path.setAttribute('stroke-linejoin', 'round');
    svg.appendChild(path);
    return svg;
  }

  function buildChips() {
    const spaces = spacesPresent(items);
    if (!spaces.length || !filterOpen) {
      return null;
    }
    const bar = document.createElement('div');
    bar.className = 'dlfbar';

    const all = document.createElement('button');
    all.className = 'dlfchip' + (spaceFilter === null ? ' on' : '');
    all.textContent = 'All';
    all.dataset.dlFilter = 'all';
    all.addEventListener('click', () => {
      spaceFilter = null;
      renderToolsChipsAndResults();
    });
    bar.append(all);

    for (const item of spaces) {
      const chip = document.createElement('button');
      chip.className =
          'dlfchip' + (spaceFilter === item.spaceId ? ' on' : '');
      chip.dataset.dlFilter = item.spaceId;
      const dot = document.createElement('span');
      dot.className = 'dlfdot';
      if (item.spaceAccent) {
        dot.style.background = item.spaceAccent;
      }
      chip.append(dot, document.createTextNode(item.spaceName ?? ''));
      chip.addEventListener('click', () => {
        spaceFilter = item.spaceId;
        renderToolsChipsAndResults();
      });
      bar.append(chip);
    }
    return bar;
  }

  function buildEmpty() {
    const empty = document.createElement('div');
    empty.className = 'ovempty';
    const q = query.trim();
    empty.textContent = items.length ?
        `No downloads${q ? ` matching "${q}"` : ''}.` :
        'No downloads yet.';
    return empty;
  }

  // A slot rather than the element itself: the filter button's own appearance
  // depends on filterOpen and on whether a filter is set, so the tools are
  // rebuilt rather than mutated in place.
  const toolsSlot = document.createElement('div');
  const chipsSlot = document.createElement('div');
  const resultsSlot = document.createElement('div');
  resultsSlot.className = 'dlresults';
  wrap.append(toolsSlot, chipsSlot, resultsSlot);

  function renderResults() {
    resultsSlot.replaceChildren();
    const visible = filterItems(items, query, spaceFilter)
                        .slice()
                        .sort((a, b) => b.receivedAt - a.receivedAt);

    if (!visible.length) {
      resultsSlot.append(buildEmpty());
      return;
    }

    for (const [label, groupItems] of groupByDay(visible)) {
      const heading = document.createElement('div');
      heading.className = 'ovgroup';
      heading.textContent = label;
      resultsSlot.append(heading);

      const media =
          groupItems.filter((i) => isMediaFile(i.name) && i.status !== 'active');
      const docs =
          groupItems.filter((i) => !isMediaFile(i.name) || i.status === 'active');

      if (media.length) {
        const tiles = document.createElement('div');
        tiles.className = 'dltiles';
        for (const item of media) {
          tiles.append(buildTile(item));
        }
        resultsSlot.append(tiles);
      }
      for (const item of docs) {
        resultsSlot.append(buildRow(item));
      }
    }
  }

  function renderToolsChipsAndResults() {
    // Focus and caret are preserved by hand: the search field is inside the
    // rebuilt tools, and a filter toggled mid-search would otherwise drop the
    // user out of the box they were typing in.
    const live = toolsSlot.querySelector('[data-dl-search]');
    const hadFocus = live && document.activeElement === live;
    const caret = live ? live.selectionStart : null;
    toolsSlot.replaceChildren(buildTools());
    if (hadFocus) {
      const next = toolsSlot.querySelector('[data-dl-search]');
      next?.focus?.();
      if (caret !== null && next?.setSelectionRange) {
        next.setSelectionRange(caret, caret);
      }
    }
    renderChipsAndResults();
  }

  function renderChipsAndResults() {
    chipsSlot.replaceChildren();
    const chips = buildChips();
    if (chips) {
      chipsSlot.append(chips);
    }
    renderResults();
  }

  const setDownloads = (nextItems) => {
    items = Array.isArray(nextItems) ? nextItems : [];
    renderToolsChipsAndResults();
  };
  window.atriumSetDownloads = setDownloads;

  renderToolsChipsAndResults();

  function dispose() {
    clearTimeout(searchDebounce);
    if (window.atriumSetDownloads === setDownloads) {
      delete window.atriumSetDownloads;
    }
  }

  return {root: wrap, dispose};
}
