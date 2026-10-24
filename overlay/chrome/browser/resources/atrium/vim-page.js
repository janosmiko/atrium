// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.
// Injected into the isolated world of every frame, so this is a
// plain script with no imports - the injector evaluates a string.
//
// This half owns the mode and the verbs. The browser half only enforces the
// steal, and its copy of the mode can be one keystroke stale, which is why
// key() can answer 'veto' rather than run a verb it should not.
window.__atriumVim = window.__atriumVim || (function() {
  var INDICATOR_CLASS = 'atrium-vim-indicator';
  var indicator = null;
  var hints = [];
  var typed = '';
  var newTab = false;

  // vim-grammar.js is concatenated ahead of this file, so a
  // missing global means the injector joined them the wrong way round.
  var shared = globalThis.__atriumVimGrammar;

  function mode() {
    return shared.isEditable(document.activeElement) ? 'INSERT' : 'NORMAL';
  }

  function ensureIndicator() {
    if (indicator && indicator.parentNode) return indicator;
    var host = document.body || document.documentElement;
    if (!host) return null;
    var existing = host.querySelector && host.querySelector('.' +
                                                            INDICATOR_CLASS);
    if (existing) {
      indicator = existing;
      return indicator;
    }
    indicator = document.createElement('div');
    indicator.className = INDICATOR_CLASS;
    indicator.style.cssText = 'position:fixed;bottom:8px;right:8px;' +
        'z-index:2147483647;font:11px ui-monospace,monospace;' +
        'padding:2px 6px;border-radius:4px;background:rgba(0,0,0,.7);' +
        'color:#fff;pointer-events:none;';
    host.appendChild(indicator);
    return indicator;
  }

  function paint() {
    var el = ensureIndicator();
    if (el) el.textContent = mode();
  }

  // A focus change only repaints an indicator that is already up. Creating one
  // here would put it back on a page the toggle was just turned off for.
  function repaintIfShown() {
    if (indicator && indicator.parentNode) paint();
  }

  function half() {
    return Math.round((window.innerHeight || 0) / 2);
  }

  // A page leaves two lines of the old view behind, the way vim's ctrl-f does,
  // so the reader keeps a foothold.
  var PAGE_OVERLAP = 60;

  function full() {
    return Math.max(0, (window.innerHeight || 0) - PAGE_OVERLAP);
  }

  // One press of j used to move 60px and snap there. Bigger and eased, so a
  // long article does not need dozens of presses and the eye can follow.
  var LINE_STEP = 120;

  function glide(dy) {
    window.scrollBy({top: dy, left: 0, behavior: 'smooth'});
  }

  // Roles are quoted so the node suite's selector parser sees them too. An
  // unquoted [role=button] reads as a tag name there and matches nothing.
  var CLICKABLE = 'a[href],button,input,select,textarea,summary,' +
      '[role="button"],[role="link"],[onclick],' +
      '[tabindex]:not([tabindex="-1"])';

  function visible(el) {
    if (!el.getBoundingClientRect) return false;
    var r = el.getBoundingClientRect();
    if (r.width <= 0 || r.height <= 0) return false;
    if (r.bottom < 0 || r.top > (window.innerHeight || 0)) return false;
    var style = window.getComputedStyle ? getComputedStyle(el) : null;
    if (!style) return true;
    return style.visibility !== 'hidden' && style.display !== 'none';
  }

  // Only an open root answers .shadowRoot. A closed one reads null here, which
  // is the same answer script in any other world gets.
  function walk(root, out, selector) {
    var all = root.querySelectorAll ? root.querySelectorAll('*') : [];
    for (var i = 0; i < all.length; i++) {
      var el = all[i];
      if (el.matches && el.matches(selector) && visible(el)) out.push(el);
      if (el.shadowRoot) walk(el.shadowRoot, out, selector);
    }
    return out;
  }

  function clickableTargets() {
    return walk(document, [], CLICKABLE);
  }

  // The largest visible player, because an ad and a hero loop are
  // both videos and neither is the one the reader means. A focused player
  // wins outright: tabbing to one says which.
  function primaryMedia() {
    var focused = document.activeElement;
    var name = focused && (focused.nodeName || '').toUpperCase();
    if (name === 'VIDEO' || name === 'AUDIO') {
      return focused;
    }
    var all = walk(document, [], 'video,audio');
    var best = null;
    var bestArea = 0;
    for (var i = 0; i < all.length; i++) {
      var rect = all[i].getBoundingClientRect();
      var area = rect.width * rect.height;
      if (area > bestArea) {
        best = all[i];
        bestArea = area;
      }
    }
    return best;
  }

  var SEEK_SECONDS = 5;

  function seekBy(delta) {
    var media = primaryMedia();
    if (!media) return;
    // duration is NaN until loadedmetadata, and NaN is falsy, so `|| 0` would
    // clamp every forward seek to the start on a video that has not loaded.
    var duration = isNaN(media.duration) ? Infinity : media.duration;
    media.currentTime =
        Math.max(0, Math.min(duration, (media.currentTime || 0) + delta));
  }

  function rateBy(factor) {
    var media = primaryMedia();
    if (!media) return;
    media.playbackRate =
        Math.max(0.25, Math.min(4, (media.playbackRate || 1) * factor));
  }

  var ALPHABET = 'asdfghjkl';
  var HINT_CLASS = 'atrium-vim-hint';

  // `total` is every frame's targets, not this frame's, because the width has
  // to be the same everywhere: one frame using 'a' while another uses 'aa'
  // would make 'a' both a label and a prefix.
  function hintLabels(count, offset, total) {
    var wide = total > ALPHABET.length;
    var out = [];
    for (var i = 0; i < count; i++) {
      var n = offset + i;
      if (!wide) {
        out.push(ALPHABET[n]);
      } else {
        out.push(ALPHABET[Math.floor(n / ALPHABET.length) % ALPHABET.length] +
                 ALPHABET[n % ALPHABET.length]);
      }
    }
    return out;
  }

  function clearHints() {
    for (var i = 0; i < hints.length; i++) {
      var marker = hints[i].marker;
      if (marker && marker.parentNode) marker.parentNode.removeChild(marker);
    }
    hints = [];
    typed = '';
  }

  function paintHints(offset, total) {
    clearHints();
    var targets = clickableTargets();
    var labels = hintLabels(targets.length, offset || 0,
                            total || targets.length);
    var host = document.body || document.documentElement;
    if (!host) return 0;
    for (var i = 0; i < targets.length; i++) {
      var rect = targets[i].getBoundingClientRect();
      var marker = document.createElement('div');
      marker.className = HINT_CLASS;
      marker.textContent = labels[i];
      marker.style.cssText = 'position:fixed;z-index:2147483647;' +
          'font:11px ui-monospace,monospace;padding:1px 3px;border-radius:3px;' +
          'background:#fd0;color:#000;pointer-events:none;left:' +
          rect.left + 'px;top:' + rect.top + 'px;';
      host.appendChild(marker);
      hints.push({label: labels[i], target: targets[i], marker: marker});
    }
    return targets.length;
  }

  // F opens in a new tab, which is a modified click rather than a second verb.
  function activate(target) {
    if (newTab && target.dispatchEvent && window.MouseEvent) {
      target.dispatchEvent(new MouseEvent('click', {
        bubbles: true, cancelable: true, metaKey: true,
      }));
      return;
    }
    if (target.click) target.click();
  }

  function hintKey(character) {
    typed += character;
    var matches = [];
    for (var i = 0; i < hints.length; i++) {
      if (hints[i].label.indexOf(typed) === 0) matches.push(hints[i]);
    }
    if (matches.length === 1 && matches[0].label === typed) {
      var target = matches[0].target;
      clearHints();
      activate(target);
      return 'release';
    }
    if (!matches.length) {
      clearHints();
      return 'release';
    }
    return 'capture';
  }

  // The page's half: what a motion means here. The keys and the pairs live in
  // vim-grammar.js, shared with Atrium's own surfaces.
  var grammar = shared.makeGrammar({
    verbs: {
      j: function() { glide(LINE_STEP); },
      k: function() { glide(-LINE_STEP); },
      d: function() { glide(half()); },
      u: function() { glide(-half()); },
      'ctrl-d': function() { glide(half()); },
      'ctrl-u': function() { glide(-half()); },
      'ctrl-f': function() { glide(full()); },
      'ctrl-b': function() { glide(-full()); },
      H: function() { window.history.back(); },
      L: function() { window.history.forward(); },
      G: function() {
        window.scrollTo(0, (document.body || document.documentElement)
            .scrollHeight || 0);
      },
      top: function() { window.scrollTo(0, 0); },
      p: function() {
        var media = primaryMedia();
        if (!media) return;
        if (media.paused) {
          media.play();
        } else {
          media.pause();
        }
      },
      ',': function() { seekBy(-SEEK_SECONDS); },
      '.': function() { seekBy(SEEK_SECONDS); },
      '<': function() { rateBy(0.5); },
      '>': function() { rateBy(2); },
    },
  });

  // Returns what the browser should do next, not merely whether this ran.
  // 'veto' hands the character back, 'copy-url' asks for a clipboard write the
  // page cannot do without a gesture, 'handled' means nothing further.
  function key(character) {
    if (hints.length) {
      // Escape leaves hint mode without picking anything.
      return character === '' ? (clearHints(), 'release')
                                    : hintKey(character);
    }
    if (mode() === 'INSERT') {
      grammar.clearPending();
      paint();
      return 'veto';
    }
    if (character === 'f' || character === 'F') {
      grammar.clearPending();
      newTab = character === 'F';
      return paintHints(0, 0) ? 'capture' : 'handled';
    }
    try {
      // A key the grammar does not know is still swallowed here: the browser
      // only forwards a character after RouteVimKey decided to steal it.
      return grammar.key(character) || 'handled';
    } catch (e) {
      // A site's own player can throw from play() or a currentTime setter.
      // The browser reads a throw as a veto, and would then type the raw
      // character into the focused element.
      return 'handled';
    }
  }

  // No paint here. The toggle is off for most users, and the browser only
  // calls in while it is on, so painting at install put an indicator on every
  // page a user who never enabled vim ever opened.
  document.addEventListener('focusin', repaintIfShown, true);
  document.addEventListener('focusout', function() {
    // focusout fires before the new element takes focus.
    setTimeout(repaintIfShown, 0);
  }, true);

  return {
    key: key,
    mode: mode,
    clickableTargets: clickableTargets,
    hintLabels: hintLabels,
    paintHints: paintHints,
    drainMode: function() {
      paint();
      return mode();
    },
    hide: function() {
      clearHints();
      if (indicator && indicator.parentNode) {
        indicator.parentNode.removeChild(indicator);
      }
      indicator = null;
    },
  };
})();
