// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// Nodes and textContent, never innerHTML: a reply is built from
// page text an attacker controls, whichever model produced it.
// Not notes-pad.js's INLINE_RULES - those end in `$` to match at a caret
// mid-typing, so they cannot scan a finished line.

// Only schemes that cannot execute. A reply carrying javascript: or data: is
// the injection this guards, and such a link renders as its own text instead.
const SAFE_LINK = /^https?:\/\//i;

// Ordered: the bold marker has to be tried before the italic one, or `**x**`
// matches as an italic `*` wrapping `*x*`.
const INLINE = [
  {tag: 'code', re: /`([^`]+)`/},
  {tag: 'strong', re: /\*\*([^*]+)\*\*/},
  {tag: 'em', re: /(?<!\*)\*([^*]+)\*(?!\*)/},
  {tag: 'a', re: /\[([^\]]+)\]\(([^)\s]+)\)/},
];

function appendInline(parent, text) {
  let earliest = null;
  for (const rule of INLINE) {
    const m = text.match(rule.re);
    if (m && (!earliest || m.index < earliest.m.index)) {
      earliest = {rule, m};
    }
  }
  if (!earliest) {
    parent.appendChild(document.createTextNode(text));
    return;
  }
  const {rule, m} = earliest;
  if (m.index > 0) {
    parent.appendChild(document.createTextNode(text.slice(0, m.index)));
  }
  if (rule.tag === 'a' && !SAFE_LINK.test(m[2])) {
    parent.appendChild(document.createTextNode(m[0]));
  } else {
    const el = document.createElement(rule.tag);
    if (rule.tag === 'a') {
      el.setAttribute('href', m[2]);
      el.setAttribute('rel', 'noreferrer noopener');
    }
    // Nested, so `**bold with `code`**` keeps both.
    appendInline(el, m[1]);
    parent.appendChild(el);
  }
  appendInline(parent, text.slice(m.index + m[0].length));
}

// Renders `text` into `container`, replacing whatever was there.
export function renderMarkdown(container, text) {
  container.replaceChildren();
  let list = null;
  for (const raw of String(text ?? '').split('\n')) {
    const line = raw.trimEnd();
    const bullet = line.match(/^\s*[-*] +(.*)$/);
    if (bullet) {
      if (!list) {
        list = document.createElement('ul');
        container.appendChild(list);
      }
      const item = document.createElement('li');
      appendInline(item, bullet[1]);
      list.appendChild(item);
      continue;
    }
    list = null;
    if (!line.trim()) {
      continue;
    }
    const heading = line.match(/^(#{1,3}) +(.*)$/);
    const block = document.createElement(heading ? `h${heading[1].length}` : 'p');
    appendInline(block, heading ? heading[2] : line);
    container.appendChild(block);
  }
}
