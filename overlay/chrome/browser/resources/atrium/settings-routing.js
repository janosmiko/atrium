// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Every change sends the whole routing file. small_stores::load_routing_checked
// is the validator of record, so this pane only refuses what it can see.

import {buildIcon} from './settings.js';

export const CURRENT_SPACE = 'current';

const MATCH_TYPES = [['contains', 'host contains'], ['regex', 'URL matches']];

function validate(rule, spaces) {
  if (!rule.pattern.trim()) {
    return 'Pattern is required.';
  }
  if (!MATCH_TYPES.some(([value]) => value === rule.matchType)) {
    return 'Match type is required.';
  }
  if (!rule.targetSpaceID) {
    return 'Target space is required.';
  }
  if (rule.targetSpaceID !== CURRENT_SPACE &&
      !spaces.some((s) => s.id === rule.targetSpaceID)) {
    return 'Target space no longer exists.';
  }
  return '';
}

// A blank card stays in the pane until it validates but must not travel:
// route_for_url already skips an empty pattern, and a deleted target names
// nowhere. A deleted space in the except list is dropped, not refused.
export function shippableRules(list, spaces) {
  return (list || [])
      .filter((rule) => !validate(rule, spaces))
      .map((rule) => ({
        ...rule,
        excludedSourceSpaceIDs: (rule.excludedSourceSpaceIDs || [])
            .filter((id) => spaces.some((s) => s.id === id)),
      }));
}

function newId() {
  return 'r-' + crypto.randomUUID();
}

export function renderRoutingPane(body, routing, spaces, send) {
  const rules = routing.rules || [];
  const wrap = document.createElement('div');
  wrap.className = 'routing-sentences';

  rules.forEach((rule, index) => {
    wrap.appendChild(ruleCard(rule, index, routing, spaces, send));
  });

  const add = document.createElement('button');
  add.className = 'routing-add';
  add.type = 'button';
  add.textContent = '+ Add a rule';
  add.addEventListener('click', () => {
    const fresh = {id: newId(), enabled: true, matchType: 'contains',
                   pattern: '', targetSpaceID: CURRENT_SPACE,
                   excludedSourceSpaceIDs: []};
    // send() rebuilds the whole pane, and its own focus-restore (settings.js)
    // runs after that rebuild - querying and focusing only once
    // send() returns is what wins that race.
    const root = send({...routing, rules: [...rules, fresh]}, {rerenderOnly: true});
    const field = root && root.querySelector(
        `.routing-card[data-rule-id="${fresh.id}"] input[name="pattern"]`);
    if (field) {
      field.focus();
    }
  });
  wrap.appendChild(add);

  wrap.appendChild(fallbackCard(routing, spaces, send));
  body.appendChild(wrap);
}

function word(text) {
  const span = document.createElement('span');
  span.className = 'routing-word';
  span.textContent = text;
  return span;
}

function spaceOptions(select, spaces, selected, {current = false, none = false} = {}) {
  if (current) {
    const option = document.createElement('option');
    option.value = CURRENT_SPACE;
    option.textContent = 'Current space';
    option.selected = selected === CURRENT_SPACE;
    select.appendChild(option);
  }
  if (none) {
    const option = document.createElement('option');
    option.value = '';
    option.textContent = 'Current space';
    option.selected = !selected;
    select.appendChild(option);
  }
  for (const space of spaces) {
    const option = document.createElement('option');
    option.value = space.id;
    option.textContent = space.name;
    option.selected = space.id === selected;
    select.appendChild(option);
  }
  select.value = selected || '';
}

function ruleCard(rule, index, routing, spaces, send) {
  const rules = routing.rules;
  const card = document.createElement('div');
  card.className = 'routing-card';
  card.dataset.ruleId = rule.id;
  card.classList.toggle('routing-off', !rule.enabled);

  const error = document.createElement('div');
  error.className = 'routing-error';

  function replaceRules(nextRules, opts) {
    return send({...routing, rules: nextRules}, opts);
  }

  function commit(next) {
    const problem = validate(next, spaces);
    error.textContent = problem;
    if (problem) {
      const root = replaceRules(
          rules.map((x) => (x.id === rule.id ? next : x)), {rerenderOnly: true});
      // The re-render destroys the captured `error` node, so the message
      // goes into the rebuilt card. Scoped to root, not document: dom-shim
      // never removes a prior test's root, and data-rule-id is not unique.
      const rebuilt = root && root.querySelector(
          `.routing-card[data-rule-id="${rule.id}"] .routing-error`);
      if (rebuilt) {
        rebuilt.textContent = problem;
      }
      return;
    }
    replaceRules(rules.map((x) => (x.id === rule.id ? next : x)));
  }

  const line = document.createElement('div');
  line.className = 'routing-line';

  const enabled = document.createElement('input');
  enabled.type = 'checkbox';
  enabled.className = 'routing-enabled';
  enabled.checked = rule.enabled;
  enabled.addEventListener('click', () => {
    replaceRules(rules.map((x) => (x.id === rule.id ? {...x, enabled: !x.enabled} : x)));
  });

  const matchSelect = document.createElement('select');
  matchSelect.className = 'routing-slot';
  matchSelect.setAttribute('name', 'matchType');
  for (const [value, label] of MATCH_TYPES) {
    const option = document.createElement('option');
    option.value = value;
    option.textContent = label;
    option.selected = value === rule.matchType;
    matchSelect.appendChild(option);
  }
  matchSelect.addEventListener('change', () => {
    commit({...rule, matchType: matchSelect.value});
  });

  const patternInput = document.createElement('input');
  patternInput.type = 'text';
  patternInput.className = 'routing-slot routing-pattern';
  patternInput.setAttribute('name', 'pattern');
  patternInput.value = rule.pattern;
  patternInput.placeholder = rule.matchType === 'regex' ? '^https://.*\\.example\\.com/' : 'corp.example';
  patternInput.addEventListener('blur', () => {
    if (patternInput.value === rule.pattern) {
      return;
    }
    commit({...rule, pattern: patternInput.value});
  });
  patternInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') {
      patternInput.blur();
    }
  });

  const targetSelect = document.createElement('select');
  targetSelect.className = 'routing-slot';
  targetSelect.setAttribute('name', 'targetSpaceID');
  spaceOptions(targetSelect, spaces, rule.targetSpaceID, {current: true});
  targetSelect.addEventListener('change', () => {
    commit({...rule, targetSpaceID: targetSelect.value});
  });

  const verb = word(rule.targetSpaceID === CURRENT_SPACE ? 'stay in the' : 'open in');
  line.append(enabled, word('When the'), matchSelect, patternInput, verb, targetSelect);
  card.appendChild(line);

  const except = document.createElement('div');
  except.className = 'routing-except';
  except.appendChild(word('except when it starts in'));
  const excluded = rule.excludedSourceSpaceIDs || [];
  for (const space of spaces) {
    const chip = document.createElement('button');
    chip.type = 'button';
    chip.className = 'routing-chip';
    chip.dataset.spaceId = space.id;
    chip.setAttribute('aria-pressed', String(excluded.includes(space.id)));
    chip.textContent = space.name;
    chip.addEventListener('click', () => {
      const next = excluded.includes(space.id)
          ? excluded.filter((id) => id !== space.id)
          : [...excluded, space.id];
      commit({...rule, excludedSourceSpaceIDs: next});
    });
    except.appendChild(chip);
  }
  card.appendChild(except);

  const tools = document.createElement('div');
  tools.className = 'routing-tools';
  const order = document.createElement('span');
  order.className = 'settings-row-hint';
  order.textContent = `Rule ${index + 1} of ${rules.length} · first match wins`;
  tools.appendChild(order);

  const up = toolButton('routing-up', 'Move up', 'M8 12V4M4 8l4-4 4 4');
  up.disabled = index === 0;
  up.addEventListener('click', () => {
    const next = [...rules];
    [next[index - 1], next[index]] = [next[index], next[index - 1]];
    replaceRules(next);
  });
  const down = toolButton('routing-down', 'Move down', 'M8 4v8M4 8l4 4 4-4');
  down.disabled = index === rules.length - 1;
  down.addEventListener('click', () => {
    const next = [...rules];
    [next[index], next[index + 1]] = [next[index + 1], next[index]];
    replaceRules(next);
  });
  const del = toolButton('routing-delete', 'Delete', 'M4 4l8 8M12 4l-8 8');
  del.addEventListener('click', () => {
    replaceRules(rules.filter((x) => x.id !== rule.id));
  });
  tools.append(up, down, del);

  card.append(error, tools);
  return card;
}

function toolButton(className, label, d) {
  const button = document.createElement('button');
  button.type = 'button';
  button.className = `routing-tool ${className}`;
  button.setAttribute('aria-label', label);
  button.title = label;
  button.appendChild(buildIcon([{tag: 'path', d}]));
  return button;
}

function fallbackCard(routing, spaces, send) {
  const card = document.createElement('div');
  card.className = 'routing-card routing-fallback';
  const line = document.createElement('div');
  line.className = 'routing-line';
  const select = document.createElement('select');
  select.className = 'routing-slot';
  select.setAttribute('name', 'defaultTargetSpaceID');
  spaceOptions(select, spaces, routing.defaultTargetSpaceID || '', {none: true});
  select.addEventListener('change', () => {
    send({...routing, defaultTargetSpaceID: select.value});
  });
  const hint = document.createElement('span');
  hint.className = 'settings-row-hint';
  hint.textContent = 'Pick Current space to turn the fallback off';
  line.append(word('Everything else opens in'), select, hint);
  card.appendChild(line);
  return card;
}
