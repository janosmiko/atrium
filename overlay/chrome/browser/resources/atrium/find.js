// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// The /find route: the field, the match count, and the three walk verbs.
// FindSession owns the search itself - this module only turns a keystroke or
// a click into one chrome.send, and a pushed result into the count text.

import {icon} from './icons.js';

function button(kind, label, iconName, verb) {
  const el = document.createElement('button');
  el.className = `fb-${kind} ov-btn`;
  el.setAttribute('aria-label', label);
  el.appendChild(icon(iconName));
  el.addEventListener('click', () => chrome.send(verb, []));
  return el;
}

// total < 0 is the no-result-yet sentinel: never draw a stale or guessed count.
function countText(active, total) {
  if (total < 0) {
    return '';
  }
  if (total === 0) {
    return '0 of 0';
  }
  return `${active} of ${total}`;
}

export function installFind(root) {
  root.replaceChildren();
  root.classList.add('findroot');

  const panel = document.createElement('div');
  panel.className = 'fb ov-card';

  const field = document.createElement('input');
  field.className = 'fb-input';
  field.type = 'text';
  field.setAttribute('aria-label', 'Find in page');
  field.addEventListener(
      'input', () => chrome.send('findQueryChanged', [field.value]));
  field.addEventListener('keydown', (e) => {
    // Cmd+G and Cmd+Shift+G are handled here, not by the main menu: a live
    // RenderWidgetHostViewCocoa claims the chord first while the bar holds key.
    if (e.key === 'Enter' || (e.metaKey && e.key === 'g')) {
      // An IME commit arrives as Enter, and walking on it would search the
      // text as it stood before the composition landed.
      if (e.isComposing) {
        return;
      }
      e.preventDefault();
      chrome.send(e.shiftKey ? 'findPrevious' : 'findNext', []);
    } else if (e.key === 'Escape') {
      e.preventDefault();
      chrome.send('findClose', []);
    }
  });

  const count = document.createElement('span');
  count.className = 'fb-count';

  panel.append(
      field,
      count,
      button('prev', 'Previous match', 'up', 'findPrevious'),
      button('next', 'Next match', 'down', 'findNext'),
      button('close', 'Close find', 'close', 'findClose'));
  root.appendChild(panel);

  window.atriumSetFindResult = ({active, total}) => {
    count.textContent = countText(active, total);
  };

  window.atriumResetFind = ({query}) => {
    field.value = query;
    field.focus();
    field.select();
  };

  // The first open cannot be served by atriumResetFind: the widget pushes it
  // before this document is live and the call is dropped.
  field.focus();
  field.select();

  chrome.send('findReady', []);
}
