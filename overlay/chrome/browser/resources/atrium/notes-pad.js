// Copyright 2026 The Atrium Authors
//
// Split from notes.js's caret wiring - no Selection/Range test double here.
// No innerHTML: CSP's empty Trusted Types allowlist throws on any write.

// Block markers recognized at the start of an empty line, keyed by the exact
// text typed before the triggering space.
const BLOCK_MARKERS = {
  '#': 'h1',
  '##': 'h2',
  '###': 'h3',
  '-': 'ul',
  '*': 'ul',
  '1.': 'ol',
  '>': 'blockquote',
};

// The block type `marker` (typed then followed by a space) opens, or null if
// `marker` is not one of the recognized shortcuts.
export function blockTypeForMarker(marker) {
  return BLOCK_MARKERS[marker] ?? null;
}

// Inline markdown rules, checked in order against the text ending exactly at
// the caret. Negative lookbehinds replace the lab's leading-context capture
// group, so `inner`/`length` need no offset arithmetic.
const INLINE_RULES = [
  {tag: 'b', re: /\*\*([^*\s][^*]*)\*\*$/},
  {tag: 'i', re: /(?<!\*)\*([^*\s][^*]*)\*$/},
  {tag: 'i', re: /(?<![A-Za-z0-9_])_([^_\s][^_]*)_$/},
  {tag: 'code', re: /`([^`\s][^`]*)`$/},
];

// The first inline rule whose closing marker sits at the end of
// `textBeforeCaret`, as `{tag, inner, length}` (`length` is the whole
// matched span, marker included), or null.
export function inlineMatchBeforeCaret(textBeforeCaret) {
  for (const {tag, re} of INLINE_RULES) {
    const m = textBeforeCaret.match(re);
    if (m) {
      return {tag, inner: m[1], length: m[0].length};
    }
  }
  return null;
}

const LIST_TYPES = new Set(['ul', 'ol']);
const EXIT_ON_END_TYPES = new Set(['h1', 'h2', 'h3', 'blockquote']);

// What Enter does inside a block of `type` holding `text`, with the caret at
// `offset`: 'exit' to leave the block for a fresh paragraph, or null to leave
// the caller's ordinary newline/new-list-item behavior alone.
export function enterAction(type, text, offset) {
  if (LIST_TYPES.has(type)) {
    return text.trim() === '' ? 'exit' : null;
  }
  if (EXIT_ON_END_TYPES.has(type) && offset >= text.length) {
    return 'exit';
  }
  return null;
}

// One line of the pad's plain-text wire format: `{type, text}`. `type` is a
// block type ('p' default) or the marker prefix has already been consumed.
function parseMarkdownLine(line) {
  const m = line.match(/^(#{1,3}|>) (.*)$/);
  if (m) {
    const type = m[1] === '>' ? 'blockquote' : 'h' + m[1].length;
    return {type, text: m[2]};
  }
  const bullet = line.match(/^[-*] (.*)$/);
  if (bullet) {
    return {type: 'ul', text: bullet[1]};
  }
  const numbered = line.match(/^\d+\. (.*)$/);
  if (numbered) {
    return {type: 'ol', text: numbered[1]};
  }
  return {type: 'p', text: line};
}

// Builds one block element for `type`/`text` via DOM APIs (no innerHTML).
// List types return the `<li>` alone - the caller groups consecutive list
// blocks of the same type into one `<ul>`/`<ol>`.
export function buildBlockElement(doc, type, text) {
  const tag = type === 'ul' || type === 'ol' ? 'li' : type;
  const el = doc.createElement(tag === 'p' ? 'p' : tag);
  el.textContent = text;
  return el;
}

// Replaces `pad`'s children with the blocks `body` (the plain-text wire
// format: one block per line) describes.
export function renderMarkdownIntoPad(pad, body) {
  while (pad.firstChild) {
    pad.removeChild(pad.firstChild);
  }
  const lines = (body ?? '').split('\n').filter((l) => l.length > 0);
  if (lines.length === 0) {
    pad.appendChild(buildBlockElement(pad.ownerDocument ?? document, 'p', ''));
    return;
  }
  let listWrap = null;
  let listType = null;
  for (const line of lines) {
    const {type, text} = parseMarkdownLine(line);
    const doc = pad.ownerDocument ?? document;
    if (type === 'ul' || type === 'ol') {
      if (listType !== type) {
        listWrap = doc.createElement(type);
        pad.appendChild(listWrap);
        listType = type;
      }
      listWrap.appendChild(buildBlockElement(doc, type, text));
    } else {
      listWrap = null;
      listType = null;
      pad.appendChild(buildBlockElement(doc, type, text));
    }
  }
}

// The reverse of renderMarkdownIntoPad: `pad`'s current blocks, serialized
// back to the plain-text wire format persisted as SpaceNote.body.
export function serializeToMarkdown(pad) {
  const PREFIX = {h1: '# ', h2: '## ', h3: '### ', blockquote: '> '};
  const lines = [];
  for (const child of pad.children ?? []) {
    const tag = (child.tagName ?? '').toLowerCase();
    if (tag === 'ul' || tag === 'ol') {
      const marker = tag === 'ul' ? '- ' : '1. ';
      for (const li of child.children ?? []) {
        lines.push(marker + (li.textContent ?? ''));
      }
    } else {
      lines.push((PREFIX[tag] ?? '') + (child.textContent ?? ''));
    }
  }
  return lines.join('\n');
}
