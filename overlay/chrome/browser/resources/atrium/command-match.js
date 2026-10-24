// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// The palette's match rule for the rows the page filters itself. The browser
// runs the same rule over favourites, tabs and spaces in
// atrium_command_bar_match_text.cc, and both suites pin the same cases.

// Every token of the query must land, so "gh iss" needs both halves. A token
// lands on a word when it is that word's prefix, or when its characters run
// through the word in order from the word's first one.

// Dropping that shared first character is what turns a fuzzy matcher into one
// that matches everything, so it gates both kinds of hit.
const PREFIX_SCORE = 1000;
const SUBSEQUENCE_SCORE = 500;

// A floor under the position penalty: a hit late in a long title still has to
// outrank a miss, and a miss is not a score at all.
const MAX_POSITION_PENALTY = 100;

// ASCII only, the same limit the substring test before it had - a title in
// another script still matches, it just does not case-fold.
function lowerAscii(code) {
  return code >= 65 && code <= 90 ? code + 32 : code;
}

function isWordChar(code) {
  return code >= 0x80 || (code >= 97 && code <= 122) ||
      (code >= 65 && code <= 90) || (code >= 48 && code <= 57);
}

// The word runs from `start` up to `end` in `field`. `token` is already lower.
function scoreTokenInWord(token, field, start, end) {
  if (lowerAscii(field.charCodeAt(start)) !== token.charCodeAt(0)) {
    return null;
  }
  if (end - start >= token.length) {
    let prefix = true;
    for (let i = 1; i < token.length && prefix; i++) {
      prefix = lowerAscii(field.charCodeAt(start + i)) === token.charCodeAt(i);
    }
    if (prefix) {
      return PREFIX_SCORE;
    }
  }
  let need = 1;
  for (let at = start + 1; at < end && need < token.length; at++) {
    if (lowerAscii(field.charCodeAt(at)) === token.charCodeAt(need)) {
      need++;
    }
  }
  return need === token.length ? SUBSEQUENCE_SCORE : null;
}

// The query breaks on the same characters a field does, so a typed host
// arrives as the words it has to land on rather than as one token carrying
// dots that no word contains.
function tokenize(query) {
  const tokens = [];
  let at = 0;
  while (at < query.length) {
    if (!isWordChar(query.charCodeAt(at))) {
      at++;
      continue;
    }
    const start = at;
    while (at < query.length && isWordChar(query.charCodeAt(at))) {
      at++;
    }
    tokens.push(query.slice(start, at).replace(
        /[A-Z]/g, (c) => String.fromCharCode(c.charCodeAt(0) + 32)));
  }
  return tokens;
}

function scoreTokenInField(token, field) {
  let best = null;
  let at = 0;
  while (at < field.length) {
    if (!isWordChar(field.charCodeAt(at))) {
      at++;
      continue;
    }
    const start = at;
    while (at < field.length && isWordChar(field.charCodeAt(at))) {
      at++;
    }
    const found = scoreTokenInWord(token, field, start, at);
    if (found !== null) {
      const scored = found - Math.min(start, MAX_POSITION_PENALTY);
      if (best === null || scored > best) {
        best = scored;
      }
    }
  }
  return best;
}

/// How well `query` describes a candidate whose text is `fields` - a title, a
/// host, an alias. Higher is a better row, null is not a hit at all. A query
/// holding no token scores 0, so a caller decides what to show for one.
export function commandMatchScore(query, fields) {
  const surfaces = Array.isArray(fields) ? fields : [];
  let total = 0;
  for (const token of tokenize(String(query || ''))) {
    let best = null;
    for (const field of surfaces) {
      if (typeof field !== 'string' || field === '') {
        continue;
      }
      const scored = scoreTokenInField(token, field);
      if (scored !== null && (best === null || scored > best)) {
        best = scored;
      }
    }
    // One unmatched token disqualifies the row: that is what makes a second
    // word narrow the list rather than widen it.
    if (best === null) {
      return null;
    }
    total += best;
  }
  return total;
}
