// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Keycaps for a binding string. atrium_command_bar_actions.cc writes
// bindings as glyphs in the ⌃⌥⇧⌘ order, so a split on the modifier glyphs
// is a split on keys.

const MODIFIERS = new Set(['⌃', '⌥', '⇧', '⌘']);

// '⇧⌘Z' -> ['⇧', '⌘', 'Z']. Everything after the last modifier is one key,
// so 'click' and 'esc' stay whole.
export function splitBinding(binding) {
  const text = String(binding || '');
  const caps = [];
  let rest = text;
  while (rest.length && MODIFIERS.has(rest[0])) {
    caps.push(rest[0]);
    rest = rest.slice(1);
  }
  if (rest.length) {
    caps.push(rest);
  }
  return caps;
}

// DOM nodes only: the WebUI CSP registers no Trusted Types policy.
export function keycaps(binding) {
  const wrap = document.createElement('span');
  wrap.className = 'ov-keys';
  for (const cap of splitBinding(binding)) {
    const key = document.createElement('span');
    key.className = 'ov-key';
    key.textContent = cap;
    wrap.appendChild(key);
  }
  return wrap;
}
