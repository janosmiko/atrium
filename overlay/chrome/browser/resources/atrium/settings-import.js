// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// One entry per browser the core can read.
export const IMPORT_SOURCES = [
  {id: 'arc', title: 'Arc'},
  {id: 'zen', title: 'Zen'},
  {id: 'chrome', title: 'Chrome'},
  {id: 'firefox', title: 'Firefox'},
];

// Report and summary keys, in the order the lines read them.
const KINDS = [
  ['spaces', 'space', 'spaces'],
  ['tabs', 'tab', 'tabs'],
  ['favorites', 'favourite', 'favourites'],
  ['routes', 'routing rule', 'routing rules'],
  ['engines', 'search engine', 'search engines'],
  ['profiles', 'profile', 'profiles'],
];

function counted(n, one, many) {
  return `${n} ${n === 1 ? one : many}`;
}

function joined(parts) {
  if (parts.length < 2) {
    return parts.join('');
  }
  return `${parts.slice(0, -1).join(', ')} and ${parts[parts.length - 1]}`;
}

export function summaryLine(summary) {
  const parts = KINDS.filter(([key]) => summary && summary[key] > 0)
      .map(([key, one, many]) => counted(summary[key], one, many));
  return parts.length ? joined(parts) : 'Nothing to import';
}

export function resultLine(result) {
  if (!result) {
    return '';
  }
  if (result.error) {
    return `Import failed: ${result.error}`;
  }
  // Routes and engines the browser could not save are not counted as added.
  const report = {...(result.report || {})};
  if (result.routingError) {
    report.routes = undefined;
  }
  if (result.enginesError) {
    report.engines = undefined;
  }
  const total = (field) => KINDS.reduce(
      (n, [key]) => n + ((report[key] && report[key][field]) || 0), 0);
  const added = KINDS.filter(([key]) => report[key] && report[key].added > 0)
      .map(([key, one, many]) => counted(report[key].added, one, many));
  const lines = [added.length ? `Added ${joined(added)}.` : 'Nothing new to import.'];
  if (total('duplicate') > 0) {
    lines.push(`${total('duplicate')} already in Atrium.`);
  }
  if (total('dropped') > 0) {
    lines.push(`${total('dropped')} skipped.`);
  }
  if (result.routingError) {
    lines.push(`Routing rules were not saved: ${result.routingError}.`);
  }
  if (result.enginesError) {
    lines.push(`Search engines were not saved: ${result.enginesError}.`);
  }
  if (result.themesError) {
    lines.push(`Themes were not saved: ${result.themesError}.`);
  }
  return lines.join(' ');
}

function element(tag, className, text) {
  const node = document.createElement(tag);
  node.className = className;
  if (text !== undefined) {
    node.textContent = text;
  }
  return node;
}

function sourceCard(source, preview, state, actions) {
  const card = element('div', 'import-card');
  card.setAttribute('data-source', source.id);
  card.appendChild(element('div', 'import-title', source.title));
  card.appendChild(element('div', 'import-counts', preview.error ?
      `Cannot read ${source.title}: ${preview.error}` : summaryLine(preview.summary)));
  if (!preview.error) {
    const button = element('button', 'import-run',
                           state.running === source.id ? 'Importing' : 'Import');
    button.type = 'button';
    button.disabled = state.running !== null;
    button.addEventListener('click', () => actions.run(source.id));
    card.appendChild(button);
  }
  if (state.result && state.result.source === source.id) {
    card.appendChild(element('div', 'import-result', resultLine(state.result)));
    const notes = (state.result.report && state.result.report.notes) || [];
    if (notes.length) {
      const list = element('ul', 'import-notes');
      for (const note of notes) {
        list.appendChild(element('li', 'import-note', note));
      }
      card.appendChild(list);
    }
  }
  return card;
}

// Asks the browser for each source's preview the first time the pane draws
// without one, and draws a card only for a source that has files on disk.
export function renderImportPane(body, state, actions) {
  const list = element('div', 'import-sources');
  let waiting = false;
  let shown = 0;
  for (const source of IMPORT_SOURCES) {
    const preview = state.previews[source.id];
    if (!preview) {
      waiting = true;
      if (!state.pending[source.id]) {
        actions.request(source.id);
      }
      continue;
    }
    if (!preview.detected) {
      continue;
    }
    list.appendChild(sourceCard(source, preview, state, actions));
    shown += 1;
  }
  if (shown === 0) {
    list.appendChild(element('p', 'import-empty', waiting ?
        'Looking for other browsers on this Mac.' :
        'No other browser found on this Mac.'));
  }
  body.appendChild(list);
}
