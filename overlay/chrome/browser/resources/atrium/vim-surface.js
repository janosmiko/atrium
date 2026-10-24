// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// vim-grammar.js is a classic script, so this reads its global
// rather than importing it: the injected page cannot carry an import, and
// both consumers have to load the same file.

const CURSOR_CLASS = 'atrium-vim-cursor';

// The surfaces bind Enter, Escape and the bare modifiers, and nothing else.
// So vim claims every printable key here, and yields whenever a control has
// focus - which is the only real conflict, and covers the rename field.
function surfaceOwnsKeys(root) {
  const el = document.activeElement;
  return !el || el === document.body || el === root ||
      el === document.documentElement;
}

// A surface whose own focus lands on a button - settings opens with one of its
// rail buttons focused - would be dead under the rule above, because a button
// eats no printable key. Such a surface passes this instead.
export function yieldsToEditableOnly() {
  return !globalThis.__atriumVimGrammar.isEditable(document.activeElement);
}

export function installVimSurface(options) {
  const root = options.root;
  const send = options.send || chrome.send.bind(chrome);
  const isEnabled = options.isEnabled;
  const selector = options.selector || '.row.tabrow';
  const ownsKeys = options.ownsKeys || surfaceOwnsKeys;
  const activate = options.activate ||
      ((row) => send('sidebarTabClicked', [row.dataset.tabId]));

  let index = 0;

  function rows() {
    return [...root.querySelectorAll(selector)];
  }

  function paint() {
    const all = rows();
    if (!all.length) return;
    index = Math.max(0, Math.min(index, all.length - 1));
    for (let i = 0; i < all.length; i++) {
      all[i].classList.toggle(CURSOR_CLASS, i === index);
    }
  }

  // Clamped, not wrapped: a cursor that jumps to the far end lands somewhere
  // the reader was not looking.
  function move(delta) {
    const all = rows();
    if (!all.length) return;
    index = Math.max(0, Math.min(index + delta, all.length - 1));
    paint();
  }

  // A surface that reads rather than lists scrolls instead of cursoring. The
  // AI panel is one long answer, so a cursor would have nothing to sit on.
  const scroller = options.scroller;
  const step = (rows_or_px) => {
    if (!scroller) return move(rows_or_px);
    scroller().scrollTop += rows_or_px * 60;
  };

  const grammar = globalThis.__atriumVimGrammar.makeGrammar({
    verbs: {
      j: () => step(1),
      k: () => step(-1),
      d: () => step(5),
      u: () => step(-5),
      G: () => {
        if (scroller) {
          scroller().scrollTop = scroller().scrollHeight;
          return;
        }
        index = rows().length - 1;
        paint();
      },
      top: () => {
        if (scroller) {
          scroller().scrollTop = 0;
          return;
        }
        index = 0;
        paint();
      },
    },
  });

  // A surface is one document with no frames, so it allocates its own labels
  // from zero. The page path asks the browser for a slice because each frame
  // runs its own copy and would otherwise start every label at 'a'.
  const ALPHABET = 'asdfghjkl';
  let hints = [];

  function clearHints() {
    for (const hint of hints) {
      hint.marker.remove();
    }
    hints = [];
  }

  function paintHints() {
    clearHints();
    const all = rows();
    const wide = all.length > ALPHABET.length;
    for (let i = 0; i < all.length; i++) {
      const label = wide
          ? ALPHABET[Math.floor(i / ALPHABET.length) % ALPHABET.length] +
                ALPHABET[i % ALPHABET.length]
          : ALPHABET[i];
      const marker = document.createElement('span');
      marker.className = 'atrium-vim-hint';
      marker.textContent = label;
      all[i].appendChild(marker);
      hints.push({label, row: all[i], marker});
    }
    return hints.length;
  }

  let typed = '';

  function hintKey(character) {
    typed += character;
    const matches = hints.filter((h) => h.label.startsWith(typed));
    if (matches.length === 1 && matches[0].label === typed) {
      const row = matches[0].row;
      clearHints();
      typed = '';
      activate(row);
      return;
    }
    if (!matches.length) {
      clearHints();
      typed = '';
    }
  }

  function onKeyDown(event) {
    if (!isEnabled() || event.metaKey || event.ctrlKey || event.altKey) {
      return;
    }
    if (!ownsKeys(root)) {
      grammar.clearPending();
      return;
    }
    if (event.key === 'Escape') {
      grammar.clearPending();
      clearHints();
      typed = '';
      return;
    }
    // Hints first: with them up every key is a label, not a verb.
    if (hints.length) {
      if (event.key.length === 1) {
        hintKey(event.key);
        event.preventDefault();
      }
      return;
    }
    if (event.key === 'Enter') {
      if (scroller) return;
      const row = rows()[index];
      if (row) activate(row);
      event.preventDefault();
      return;
    }
    if (event.key.length !== 1) {
      return;
    }
    if (!scroller && (event.key === 'f' || event.key === 'F')) {
      paintHints();
      event.preventDefault();
      return;
    }
    const answer = grammar.key(event.key);
    if (answer === null) {
      // Not ours. The AI panel's open hotkey is a bare printable character,
      // so swallowing every key vim has no verb for would kill it.
      return;
    }
    if (answer !== 'handled') {
      send('vimVerb', [answer]);
    }
    event.preventDefault();
  }

  document.addEventListener('keydown', onKeyDown);
  return {
    dispose: () => document.removeEventListener('keydown', onKeyDown),
  };
}
