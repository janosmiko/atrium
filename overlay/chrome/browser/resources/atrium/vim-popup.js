// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Stock ExtensionViewHost delegates the popup, so the browser never
// sees a key pressed here and cannot forward one - this drives __atriumVim from
// inside the document instead.
window.__atriumVimPopup = window.__atriumVimPopup || (function() {
  var vim = window.__atriumVim;
  var shared = globalThis.__atriumVimGrammar;
  if (!vim || !shared) {
    return null;
  }

  // True while labels are on screen. vim-page owns the labels but exposes no
  // way to ask, and the answers to key() say enough: 'capture' means it wants
  // the next key too.
  var hinting = false;

  // f and the label keys only. The popup is an extension's own page with its
  // own shortcuts, so claiming every printable key here would break them.
  function onKeyDown(e) {
    if (e.metaKey || e.ctrlKey || e.altKey) {
      return;
    }
    if (shared.isEditable(document.activeElement)) {
      return;
    }
    // The ESC character, which is what the browser forwards for Escape and
    // what vim-page matches on. 'Escape' as a word reads as an unknown key.
    var character = e.key === 'Escape' ? '' : e.key;
    if (!hinting && character !== 'f' && character !== 'F') {
      return;
    }
    if (character.length !== 1) {
      return;
    }
    var answer = vim.key(character);
    hinting = answer === 'capture';
    // Both: the extension's own listener is on the same document, and a label
    // press that reaches it would run whatever that key means there too.
    e.preventDefault();
    e.stopPropagation();
  }

  document.addEventListener('keydown', onKeyDown, true);

  return {
    hinting: function() { return hinting; },
    dispose: function() {
      vim.hide();
      hinting = false;
      document.removeEventListener('keydown', onKeyDown, true);
    },
  };
})();
