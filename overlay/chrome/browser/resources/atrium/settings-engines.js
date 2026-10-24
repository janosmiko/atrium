// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The Search engines pane: a list on the left, one engine's fields on the
// right. Every change sends the whole settings object, the Rust loader is
// the validator of record, and this pane only refuses what it can see.

import {VERBS} from './command-verbs.js';

let selectedId = null;

// The command bar reads a verb keyword and matches keywords case-insensitively
// before an engine row is ever built, so both make a keyword unreachable.
function validate(engine, engines = []) {
  if (!engine.name.trim()) {
    return 'Name is required.';
  }
  const keyword = engine.keyword.trim();
  if (!keyword) {
    return 'Keyword is required.';
  }
  const lower = keyword.toLowerCase();
  if (Object.keys(VERBS).includes(lower)) {
    return `"${keyword}" is a command, not a keyword.`;
  }
  if (engines.some((other) => other.id !== engine.id &&
                    (other.keyword || '').trim().toLowerCase() === lower)) {
    return `"${keyword}" is already used by another engine.`;
  }
  if (!engine.url.startsWith('https://')) {
    return 'URL must start with https://.';
  }
  if (engine.url.split('%s').length !== 2) {
    return 'URL must contain %s exactly once.';
  }
  return '';
}

// The blank row an Add makes stays in the pane until its fields validate, but
// it must not travel: the browser feeds this list to TemplateURLData, whose
// SetKeyword and SetURL DCHECK on empty input.
export function shippableEngines(list) {
  const engines = list || [];
  return engines.filter((engine) => !validate(engine, engines));
}

function newId() {
  return 'u-' + crypto.randomUUID();
}

export function renderEnginesPane(body, settings, send) {
  const engines = settings.searchEngines || [];
  if (selectedId && !engines.some((e) => e.id === selectedId)) {
    selectedId = null;
  }

  const wrap = document.createElement('div');
  wrap.className = 'engines';

  const list = document.createElement('div');
  list.className = 'engine-list';
  for (const engine of engines) {
    const row = document.createElement('div');
    row.className = 'engine-row';
    row.classList.toggle('engine-selected', engine.id === selectedId);
    row.classList.toggle('engine-off', !engine.enabled);

    const enabled = document.createElement('input');
    enabled.type = 'checkbox';
    enabled.className = 'engine-enabled';
    enabled.checked = engine.enabled;
    enabled.addEventListener('click', (e) => {
      e.stopPropagation();
      send({...settings, searchEngines: engines.map((x) =>
          x.id === engine.id ? {...x, enabled: !x.enabled} : x)});
    });

    const name = document.createElement('span');
    name.className = 'engine-name';
    name.textContent = engine.name;

    const keyword = document.createElement('span');
    keyword.className = 'engine-keyword';
    keyword.textContent = engine.keyword;

    row.append(enabled, name, keyword);
    if (engine.builtin) {
      const tag = document.createElement('span');
      tag.className = 'engine-builtin';
      tag.textContent = 'Builtin';
      row.appendChild(tag);
    }
    row.addEventListener('click', () => {
      selectedId = engine.id;
      send(settings, {rerenderOnly: true});
    });
    list.appendChild(row);
  }

  const add = document.createElement('button');
  add.className = 'engine-add';
  add.textContent = '+ Add engine';
  add.addEventListener('click', () => {
    const fresh = {id: newId(), name: '', keyword: '', url: '',
                   enabled: true, builtin: false};
    selectedId = fresh.id;
    send({...settings, searchEngines: [...engines, fresh]}, {rerenderOnly: true});
  });
  list.appendChild(add);

  wrap.appendChild(list);

  const selected = engines.find((e) => e.id === selectedId);
  if (selected) {
    wrap.appendChild(detailFor(selected, engines, settings, send));
  }
  body.appendChild(wrap);
}

function detailFor(engine, engines, settings, send) {
  const detail = document.createElement('div');
  detail.className = 'engine-detail';
  const error = document.createElement('div');
  error.className = 'engine-error';

  // Each field commits on its own blur, so read the other two straight off
  // their inputs rather than an incrementally-synced draft.
  const fields = {};
  for (const [key, label] of [['name', 'Name'], ['keyword', 'Keyword'], ['url', 'URL']]) {
    const line = document.createElement('label');
    line.className = 'settings-row';
    const text = document.createElement('span');
    text.textContent = label;
    const input = document.createElement('input');
    input.type = 'text';
    input.setAttribute('name', key);
    input.value = engine[key];
    fields[key] = input;
    input.addEventListener('blur', () => {
      const next = {...engine, name: fields.name.value,
                    keyword: fields.keyword.value, url: fields.url.value};
      const problem = validate(next, engines);
      error.textContent = problem;
      if (problem) {
        return;
      }
      send({...settings, searchEngines: engines.map((x) =>
          x.id === engine.id ? next : x)});
    });
    line.append(text, input);
    detail.appendChild(line);
  }

  const del = document.createElement('button');
  del.className = 'engine-delete';
  del.textContent = 'Delete';
  del.addEventListener('click', () => {
    selectedId = null;
    send({...settings, searchEngines: engines.filter((x) => x.id !== engine.id)});
  });
  detail.append(error, del);
  return detail;
}
