// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
//
// the command bar's verb grammar. A command is a verb, a target, and
// an optional destination - `mv 3 work`, `close *.slack`, `split 2 5`.
//
// The PARSER and the REGISTRY are separate on purpose: a new verb is an entry
// in VERBS, not new parsing and not new UI. Everything here is pure, so the
// palette can preview a command without performing it.

/// What each verb takes. `dest` says whether a destination is required, and
/// `plural` whether the verb is meaningful against more than one target -
/// `close *.slack` is, `split 2 5` is not.
export const VERBS = {
  mv: {label: 'Move to space', dest: 'required', plural: true},
  close: {label: 'Close', dest: 'none', plural: true, send: 'sidebarTabClosed'},
  split: {label: 'Split with', dest: 'required', plural: false},
};

/// Splits `input` into a verb and its arguments, or null when the first word
/// names no verb.
///
/// Null rather than a "no verb" result, because the palette's ordinary URL and
/// search handling must be completely unaffected by anything typed here - a
/// command is a different mode, not a failed one.
export function parseCommand(input) {
  const words = String(input || '').trim().split(/\s+/).filter(Boolean);
  if (!words.length) {
    return null;
  }
  const verb = words[0].toLowerCase();
  const spec = VERBS[verb];
  if (!spec) {
    return null;
  }
  const rest = words.slice(1);
  // The destination is the LAST word when the verb takes one, so a target that
  // is itself several words still parses - `mv my long title work`.
  const dest = spec.dest === 'required' && rest.length > 1 ? rest[rest.length - 1] : null;
  const target = (dest ? rest.slice(0, -1) : rest).join(' ');
  return {verb, spec, target, dest};
}

function hostOf(url) {
  try {
    return new URL(url).host.toLowerCase();
  } catch {
    return '';
  }
}

/// Turns a host glob - `*.slack`, `slack.*` - into a matcher. Only `*` is
/// special, and it is the only wildcard the lab's own examples use.
function globMatcher(pattern) {
  const escaped = pattern.toLowerCase()
                      .replace(/[.+?^${}()|[\]\\]/g, '\\$&')
                      .replace(/\*/g, '.*');
  return new RegExp(`^${escaped}$`);
}

/// The tabs `target` names, in the order they were given.
///
/// Three forms, tried in this order: a 1-based index, a host glob when the
/// target contains a `*`, and otherwise a case-insensitive title fragment.
/// The index is 1-based because it is what the row list shows a user, not what
/// the array uses.
export function resolveTargets(target, tabs) {
  const rows = Array.isArray(tabs) ? tabs : [];
  const trimmed = String(target || '').trim();
  if (!trimmed) {
    return [];
  }
  if (/^\d+$/.test(trimmed)) {
    const at = Number(trimmed) - 1;
    return rows[at] ? [rows[at]] : [];
  }
  if (trimmed.includes('*')) {
    const matches = globMatcher(trimmed);
    return rows.filter((row) => matches.test(hostOf(row.url || '')));
  }
  const needle = trimmed.toLowerCase();
  return rows.filter((row) => (row.title || '').toLowerCase().includes(needle));
}

/// What the palette shows before the command runs: the verb, what it resolved
/// to, and whether it can run at all.
///
/// A command that matches nothing is NOT an error to swallow - it comes back
/// with `ok: false` and a reason, so the surface can say so and change nothing.
export function previewCommand(input, tabs) {
  const parsed = parseCommand(input);
  if (!parsed) {
    return null;
  }
  const {verb, spec, target, dest} = parsed;
  if (!target) {
    return {verb, spec, targets: [], dest, ok: false, reason: 'no target given'};
  }
  const targets = resolveTargets(target, tabs);
  if (!targets.length) {
    return {verb, spec, targets, dest, ok: false,
            reason: `nothing matches "${target}"`};
  }
  if (spec.dest === 'required' && !dest) {
    return {verb, spec, targets, dest, ok: false,
            reason: `${verb} needs a destination`};
  }
  if (!spec.plural && targets.length > 1) {
    return {verb, spec, targets, dest, ok: false,
            reason: `${verb} takes one tab, "${target}" matches ${targets.length}`};
  }
  if (!spec.send) {
    return {verb, spec, targets, dest, ok: false,
            reason: `${verb} cannot run from the palette yet`};
  }
  return {verb, spec, targets, dest, ok: true, reason: ''};
}
