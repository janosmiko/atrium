// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// A plain script, not a module: the injected path evaluates this
// file as a string and cannot carry an import, so the surfaces load it as a
// classic script instead. Each consumer brings its own verb table.
globalThis.__atriumVimGrammar = globalThis.__atriumVimGrammar || (function() {
  function isEditable(el) {
    if (!el) return false;
    if (el.isContentEditable) return true;
    var name = (el.nodeName || '').toUpperCase();
    if (name === 'TEXTAREA') return !el.hasAttribute('readonly');
    if (name === 'INPUT') return !el.hasAttribute('readonly');
    if (el.getAttribute && el.getAttribute('contenteditable') === 'true') {
      return true;
    }
    return false;
  }

  // Second key -> what the pair means. A prefix with no entry for the key
  // that followed drops, so gj still scrolls.
  var PAIRS = {
    g: {g: 'top', t: 'next-tab', T: 'previous-tab'},
    y: {y: 'copy-url'},
    ']': {']': 'next-space'},
    '[': {'[': 'previous-space'},
  };

  // Single keys the browser owns. The consumer names them and does nothing.
  var BROWSER_VERBS = {
    x: 'close-tab',
    o: 'open-command-bar',
    t: 'open-command-bar',
  };

  // `pending` lives per machine rather than in this closure, so a g typed on
  // one surface cannot change what the next key means on another.
  function makeGrammar(options) {
    var verbs = (options && options.verbs) || {};
    var pending = null;

    function key(character) {
      if (pending) {
        var pair = PAIRS[pending][character];
        pending = null;
        if (pair) {
          var verb = verbs[pair];
          if (verb) {
            verb();
            return 'handled';
          }
          return pair;
        }
        // No entry for the key that followed, so it runs its own verb rather
        // than being swallowed by a prefix it does not belong to.
      }
      if (PAIRS[character]) {
        pending = character;
        return 'handled';
      }
      var named = BROWSER_VERBS[character];
      if (named) {
        return named;
      }
      var own = verbs[character];
      if (own) {
        own();
        return 'handled';
      }
      // Null, not 'handled'. A surface listens to every key and has to let a
      // key the grammar does not know reach whatever else claims it - the AI
      // panel's open hotkey is a bare printable character.
      return null;
    }

    return {
      key: key,
      clearPending: function() { pending = null; },
    };
  }

  return {makeGrammar: makeGrammar, isEditable: isEditable};
})();
