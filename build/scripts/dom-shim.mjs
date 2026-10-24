// Copyright 2026 The Atrium Authors
//
// A minimal DOM for the WebUI unit suites. The surfaces build their trees with
// document.createElement and delegate events from a root, so everything except
// layout is a pure function of the snapshot pushed from C++ - but none of it
// could be reached from `node --test` without a document.
//
// WHAT THIS IS FOR, and what it deliberately is not:
//
//   covered   data -> DOM structure, class/dataset/attribute mapping, event
//             ROUTING through delegated listeners, and the chrome.send payloads
//             a gesture produces. All deterministic.
//
//   NOT       layout, reflow, hit testing, real drag semantics, momentum. There
//             is no engine here, so getBoundingClientRect returns whatever the
//             test set. A reflow on dragstart cancelling the drag
//             could not be caught by any test written against this file, and
//             stays the VM probe's job (make drag-input-probe).
//
// A shim is a model of the browser, and a fix verified only against a model of
// the browser is not verified. Structure here; behaviour in the VM.
//
// Selector support is the subset the surfaces actually use: tag, .class, #id,
// [attr], [attr="value"], :hover (an element's `hovered` flag, there is no
// pointer), compounds of those, plus descendant and > combinators.

const SVG_NS = 'http://www.w3.org/2000/svg';

const camel = (name) => name.replace(/-([a-z])/g, (_, c) => c.toUpperCase());
const kebab = (name) => name.replace(/[A-Z]/g, (c) => '-' + c.toLowerCase());

// --- selectors --------------------------------------------------------------

// One compound: 'div.a.b[data-x="y"]' -> predicate. Split on the boundaries
// rather than a single mega-regex so an unsupported scrap raises instead of
// silently matching everything, which would make every test vacuously green.
function compound(source) {
  const tests = [];
  const rest = source.replace(
      /\[([a-zA-Z-]+)(?:([~|^$*]?=)"([^"]*)")?\]/g, (_, attr, op, value) => {
        tests.push((el) => {
          const actual = el.getAttribute(attr);
          if (actual === null) {
            return false;
          }
          return op ? actual === value : true;
        });
        return '';
      });

  const plain = rest.replace(/:hover/g, () => {
    tests.push((el) => el.hovered === true);
    return '';
  }).replace(/\*/g, () => {
    // The universal selector, which an element walk starts from.
    tests.push(() => true);
    return '';
  });

  for (const token of plain.match(/[.#]?[a-zA-Z][\w-]*/g) || []) {
    if (token.startsWith('.')) {
      const cls = token.slice(1);
      tests.push((el) => el.classList.contains(cls));
    } else if (token.startsWith('#')) {
      const id = token.slice(1);
      tests.push((el) => el.getAttribute('id') === id);
    } else {
      const tag = token.toLowerCase();
      tests.push((el) => el.tagName.toLowerCase() === tag);
    }
  }
  if (!tests.length) {
    throw new Error(`dom-shim: unsupported selector piece "${source}"`);
  }
  return (el) => tests.every((t) => t(el));
}

// Right-to-left, the way a browser matches: check the subject, then walk up.
function parseSelector(selector) {
  const parts = selector.trim().split(/\s+(>)\s+|\s+/).filter(Boolean);
  const steps = [];
  for (let i = 0; i < parts.length; i++) {
    if (parts[i] === '>') {
      steps[steps.length - 1].child = true;
      continue;
    }
    steps.push({match: compound(parts[i]), child: false});
  }
  return steps;
}

function matchesSelector(el, selector) {
  for (const one of selector.split(',')) {
    const steps = parseSelector(one);
    let node = el;
    let i = steps.length - 1;
    if (!steps[i].match(node)) {
      continue;
    }
    let ok = true;
    let directParent = steps[i].child;
    i--;
    for (; i >= 0; i--) {
      let found = false;
      for (let p = node.parentNode; p && p.nodeType === 1; p = p.parentNode) {
        if (steps[i].match(p)) {
          node = p;
          found = true;
          break;
        }
        if (directParent) {
          break;
        }
      }
      if (!found) {
        ok = false;
        break;
      }
      directParent = steps[i].child;
    }
    if (ok) {
      return true;
    }
  }
  return false;
}

// --- events -----------------------------------------------------------------

class ShimEvent {
  constructor(type, init = {}) {
    Object.assign(this, init);
    this.type = type;
    this.bubbles = init.bubbles !== false;
    this.defaultPrevented = false;
    this.target = null;
    this.currentTarget = null;
    this._stopped = false;
  }
  preventDefault() {
    this.defaultPrevented = true;
  }
  stopPropagation() {
    this._stopped = true;
  }
  stopImmediatePropagation() {
    this._stopped = true;
    this._stoppedImmediate = true;
  }
}

// --- nodes ------------------------------------------------------------------

class ShimText {
  constructor(text) {
    this.nodeType = 3;
    this.textContent = String(text);
    this.parentNode = null;
  }
}

class ShimElement {
  constructor(tagName, namespaceURI = null) {
    this.nodeType = 1;
    this.tagName = tagName.toUpperCase();
    // Injected page scripts read nodeName, which for an element is tagName.
    this.nodeName = this.tagName;
    this.namespaceURI = namespaceURI;
    this.parentNode = null;
    this.childNodes = [];
    this.hovered = false;
    this._attrs = new Map();
    this._listeners = new Map();
    this._rect = {x: 0, y: 0, top: 0, left: 0, right: 0, bottom: 0,
                  width: 0, height: 0};
    this._layout = null;
    // a real scroll container reports these from layout, which the
    // shim has none of. Tests set scrollHeight and clientHeight to stage a
    // container that overflows, then drive scrollTop as the reader would.
    this.scrollTop = 0;
    this.scrollHeight = 0;
    this.clientHeight = 0;

    // a real <details> reflects `open` to the attribute both ways,
    // which is how a disclosure survives being re-rendered under the reader.
    if (this.tagName === 'DETAILS') {
      Object.defineProperty(this, 'open', {
        get: () => this._attrs.has('open'),
        set: (next) => {
          if (next) {
            this._attrs.set('open', '');
          } else {
            this._attrs.delete('open');
          }
        },
        enumerable: true,
        configurable: true,
      });
    }

    // a media element answers play and pause rather than layout,
    // so the vim verbs can be asserted without a real decoder.
    if (this.tagName === 'VIDEO' || this.tagName === 'AUDIO') {
      this.paused = true;
      this.currentTime = 0;
      // NaN is what a real element reports until loadedmetadata, so a test
      // that wants a loaded video sets this rather than inheriting one.
      this.duration = NaN;
      this.playbackRate = 1;
      this.play = () => {
        this.paused = false;
      };
      this.pause = () => {
        this.paused = true;
      };
    }

    const el = this;
    this.style = new Proxy({
      setProperty(name, value) {
        this[name] = value;
      },
      removeProperty(name) {
        delete this[name];
      },
      getPropertyValue(name) {
        return this[name] ?? '';
      },
    }, {});

    this.classList = {
      contains: (c) => el._classes().includes(c),
      add: (...cs) => {
        const set = el._classes();
        for (const c of cs) {
          if (!set.includes(c)) {
            set.push(c);
          }
        }
        el._attrs.set('class', set.join(' '));
      },
      remove: (...cs) => {
        el._attrs.set(
            'class', el._classes().filter((c) => !cs.includes(c)).join(' '));
      },
      toggle: (c, force) => {
        const on = force === undefined ? !el.classList.contains(c) : !!force;
        on ? el.classList.add(c) : el.classList.remove(c);
        return on;
      },
      get length() {
        return el._classes().length;
      },
    };

    this.dataset = new Proxy({}, {
      get: (_, key) => el._attrs.get('data-' + kebab(String(key))),
      set: (_, key, value) => {
        el._attrs.set('data-' + kebab(String(key)), String(value));
        return true;
      },
      has: (_, key) => el._attrs.has('data-' + kebab(String(key))),
      deleteProperty: (_, key) => el._attrs.delete('data-' + kebab(String(key))),
      ownKeys: () => [...el._attrs.keys()]
          .filter((k) => k.startsWith('data-'))
          .map((k) => camel(k.slice(5))),
      getOwnPropertyDescriptor: () => (
          {enumerable: true, configurable: true}),
    });
  }

  _classes() {
    const raw = this._attrs.get('class');
    return raw ? raw.split(/\s+/).filter(Boolean) : [];
  }

  // HTMLInputElement.type (and button/script) reflects the content
  // attribute per spec, which an input[type="..."] selector depends on.
  get type() {
    return this._attrs.get('type') || '';
  }
  set type(value) {
    this._attrs.set('type', String(value));
  }

  get className() {
    return this._attrs.get('class') || '';
  }
  set className(value) {
    this._attrs.set('class', String(value));
  }

  get children() {
    return this.childNodes.filter((n) => n.nodeType === 1);
  }
  get firstChild() {
    return this.childNodes[0] || null;
  }
  get firstElementChild() {
    return this.children[0] || null;
  }
  get childElementCount() {
    return this.children.length;
  }
  get nextSibling() {
    const kids = this.parentNode?.childNodes || [];
    return kids[kids.indexOf(this) + 1] || null;
  }
  get previousElementSibling() {
    const kids = this.parentNode?.children || [];
    return kids[kids.indexOf(this) - 1] || null;
  }
  get nextElementSibling() {
    const kids = this.parentNode?.children || [];
    return kids[kids.indexOf(this) + 1] || null;
  }
  get parentElement() {
    return this.parentNode?.nodeType === 1 ? this.parentNode : null;
  }

  get textContent() {
    return this.childNodes
        .map((n) => (n.nodeType === 3 ? n.textContent : n.textContent))
        .join('');
  }
  set textContent(value) {
    for (const kid of this.childNodes) {
      kid.parentNode = null;
    }
    this.childNodes = [];
    if (value !== '' && value != null) {
      this.appendChild(new ShimText(value));
    }
  }

  // Reflected, because a plain property leaves the element invisible to the
  // matching selector - #atrium-root and [title] are both looked up that way.
  get id() {
    return this.getAttribute('id') || '';
  }
  set id(value) {
    this.setAttribute('id', value);
  }

  get title() {
    return this.getAttribute('title') || '';
  }
  set title(value) {
    this.setAttribute('title', value);
  }

  setAttribute(name, value) {
    this._attrs.set(name, String(value));
  }
  getAttribute(name) {
    return this._attrs.has(name) ? this._attrs.get(name) : null;
  }
  hasAttribute(name) {
    return this._attrs.has(name);
  }
  removeAttribute(name) {
    this._attrs.delete(name);
  }

  _adopt(node) {
    const child = typeof node === 'string' ? new ShimText(node) : node;
    child.parentNode?.removeChild?.(child);
    child.parentNode = this;
    return child;
  }
  appendChild(node) {
    if (node instanceof ShimFragment) {
      for (const kid of [...node.childNodes]) {
        this.appendChild(kid);
      }
      return node;
    }
    this.childNodes.push(this._adopt(node));
    return node;
  }
  append(...nodes) {
    for (const n of nodes) {
      this.appendChild(n);
    }
  }
  prepend(...nodes) {
    this.childNodes.unshift(...nodes.map((n) => this._adopt(n)));
  }
  insertBefore(node, ref) {
    // Detach first: a move within this parent shifts the index of `ref`.
    const child = this._adopt(node);
    const at = ref ? this.childNodes.indexOf(ref) : this.childNodes.length;
    this.childNodes.splice(at < 0 ? this.childNodes.length : at, 0, child);
    return node;
  }
  removeChild(node) {
    const at = this.childNodes.indexOf(node);
    if (at >= 0) {
      this.childNodes.splice(at, 1);
      node.parentNode = null;
    }
    return node;
  }
  remove() {
    this.parentNode?.removeChild(this);
  }
  replaceWith(...nodes) {
    const parent = this.parentNode;
    if (!parent) {
      return;
    }
    const at = parent.childNodes.indexOf(this);
    parent.removeChild(this);
    for (const [i, node] of nodes.entries()) {
      parent.childNodes.splice(at + i, 0, parent._adopt(node));
    }
  }
  replaceChildren(...nodes) {
    for (const kid of this.childNodes) {
      kid.parentNode = null;
    }
    this.childNodes = [];
    this.append(...nodes);
  }

  _descendants(out = []) {
    for (const kid of this.children) {
      out.push(kid);
      kid._descendants(out);
    }
    return out;
  }
  // A real shadow root is invisible to the host's own querySelectorAll, and a
  // closed one is invisible to script entirely. A shim that leaked either
  // would let a walk that never recurses pass its own test.
  attachShadow({mode}) {
    const root = new ShimElement('#shadow-root');
    root.host = this;
    this._shadowRoot = root;
    this._shadowOpen = mode === 'open';
    return root;
  }
  get shadowRoot() {
    return this._shadowOpen ? this._shadowRoot : null;
  }
  querySelectorAll(selector) {
    return this._descendants().filter((el) => matchesSelector(el, selector));
  }
  querySelector(selector) {
    return this.querySelectorAll(selector)[0] || null;
  }
  matches(selector) {
    return matchesSelector(this, selector);
  }
  closest(selector) {
    for (let n = this; n && n.nodeType === 1; n = n.parentNode) {
      if (matchesSelector(n, selector)) {
        return n;
      }
    }
    return null;
  }
  contains(node) {
    for (let n = node; n; n = n.parentNode) {
      if (n === this) {
        return true;
      }
    }
    return false;
  }

  get isConnected() {
    for (let n = this; n; n = n.parentNode) {
      if (n === globalThis.document?.documentElement) {
        return true;
      }
    }
    return false;
  }

  cloneNode(deep) {
    const clone = new ShimElement(this.tagName, this.namespaceURI);
    for (const [name, value] of this._attrs) {
      clone._attrs.set(name, value);
    }
    for (const key of Object.keys(this.style)) {
      if (key !== 'setProperty' && key !== 'removeProperty' &&
          key !== 'getPropertyValue') {
        clone.style[key] = this.style[key];
      }
    }
    if (deep) {
      for (const kid of this.childNodes) {
        clone.appendChild(
            kid.nodeType === 3 ? new ShimText(kid.textContent) : kid.cloneNode(true));
      }
    }
    return clone;
  }

  // Layout is the test's to declare - there is no engine here.
  setRect(rect) {
    Object.assign(this._rect, rect);
    if (rect.width != null && rect.left != null && rect.right == null) {
      this._rect.right = rect.left + rect.width;
    }
    if (rect.height != null && rect.top != null && rect.bottom == null) {
      this._rect.bottom = rect.top + rect.height;
    }
    return this;
  }
  // The layout box a transform does not move, so a test can declare a card
  // mid-animation: setRect is what it paints as, this is where it lives.
  // left/top are offsetParent-relative, the way the real offset* are.
  setLayoutBox(box) {
    this._layout = {...(this._layout || {left: 0, top: 0, width: 0, height: 0}),
                    ...box};
    return this;
  }
  getBoundingClientRect() {
    return {...this._rect};
  }
  get offsetLeft() {
    return this._layout ? this._layout.left : this._rect.left;
  }
  get offsetTop() {
    return this._layout ? this._layout.top : this._rect.top;
  }
  get offsetWidth() {
    return this._layout ? this._layout.width : this._rect.width;
  }
  get offsetHeight() {
    return this._layout ? this._layout.height : this._rect.height;
  }

  addEventListener(type, fn, options) {
    const capture = options === true || (options && options.capture);
    const list = this._listeners.get(type) || [];
    list.push({fn, capture: !!capture, once: !!(options && options.once)});
    this._listeners.set(type, list);
  }
  removeEventListener(type, fn) {
    this._listeners.set(
        type, (this._listeners.get(type) || []).filter((l) => l.fn !== fn));
  }
  dispatchEvent(event) {
    event.target = event.target || this;
    const path = [];
    for (let n = this; n; n = n.parentNode) {
      path.push(n);
    }

    const fire = (node, capture) => {
      for (const l of [...(node._listeners.get(event.type) || [])]) {
        if (l.capture !== capture) {
          continue;
        }
        event.currentTarget = node;
        l.fn.call(node, event);
        if (l.once) {
          node.removeEventListener(event.type, l.fn);
        }
        if (event._stoppedImmediate) {
          return;
        }
      }
    };

    for (let i = path.length - 1; i >= 1; i--) {
      fire(path[i], true);
      if (event._stopped) {
        return !event.defaultPrevented;
      }
    }
    fire(this, true);
    fire(this, false);
    if (event.bubbles) {
      for (let i = 1; i < path.length && !event._stopped; i++) {
        fire(path[i], false);
      }
    }
    return !event.defaultPrevented;
  }

  click() {
    this.dispatchEvent(new ShimEvent('click', {button: 0}));
  }
  // Tracked rather than ignored, so a test can tell where the keyboard is.
  // No focusin/focusout is dispatched - only the resting place is modelled.
  // Pointer capture, enough of it for a drag: the element remembers which
  // pointer ids it holds, which is all a handler can ask before deciding a
  // move belongs to its own drag.
  setPointerCapture(pointerId) {
    (this._capturedPointers ??= new Set()).add(pointerId);
  }

  releasePointerCapture(pointerId) {
    this._capturedPointers?.delete(pointerId);
  }

  hasPointerCapture(pointerId) {
    return this._capturedPointers?.has(pointerId) ?? false;
  }

  focus() {
    if (globalThis.document) {
      globalThis.document.activeElement = this;
    }
  }
  blur() {
    if (globalThis.document && globalThis.document.activeElement === this) {
      globalThis.document.activeElement = globalThis.document.body;
    }
  }
  scrollIntoView() {}
  // Lands at once. The behavior is kept so a test can tell a glide from a jump.
  scrollTo({left, top, behavior} = {}) {
    if (left !== undefined) {
      this.scrollLeft = left;
    }
    if (top !== undefined) {
      this.scrollTop = top;
    }
    this.lastScrollBehavior = behavior;
  }
  // HTMLInputElement.select(): selects the whole current value.
  select() {
    this.selectionStart = 0;
    this.selectionEnd = this.value != null ? String(this.value).length : 0;
  }
}

class ShimFragment extends ShimElement {
  constructor() {
    super('#document-fragment');
    this.nodeType = 11;
  }
}

// --- install ----------------------------------------------------------------

// A drag payload with the real setData/getData semantics, which the drag
// modules depend on for the source id.
export function makeDataTransfer(initial = {}) {
  const store = new Map(Object.entries(initial));
  return {
    dropEffect: 'none',
    effectAllowed: 'all',
    setData: (type, value) => store.set(type, String(value)),
    getData: (type) => store.get(type) ?? '',
    clearData: (type) => (type ? store.delete(type) : store.clear()),
    setDragImage: () => {},
    get types() {
      return [...store.keys()];
    },
  };
}

export {ShimEvent as Event};

// Installs a document on globalThis and returns the handle. Call in a
// beforeEach; the returned reset() drops listeners and the tree.
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

// The scripts atrium.html loads outside the module graph. Evaluated as text,
// the way the browser loads them, so a suite sees what a surface sees.
function loadClassicScripts() {
  const dir = path.join(
      process.env.ATRIUM_CHROMIUM_SRC ||
          path.join(os.homedir(), 'chromium-atrium/src'),
      'chrome/browser/resources/atrium');
  for (const name of ['vim-grammar.js']) {
    new Function(fs.readFileSync(path.join(dir, name), 'utf8'))();
  }
}

export function installDOM({pathname = '/'} = {}) {
  const document = {
    createElement: (tag) => new ShimElement(tag),
    createElementNS: (ns, tag) => new ShimElement(tag, ns),
    createDocumentFragment: () => new ShimFragment(),
    createTextNode: (t) => new ShimText(t),
    documentElement: new ShimElement('html'),
    body: new ShimElement('body'),
    addEventListener: (...a) => document.body.addEventListener(...a),
    removeEventListener: (...a) => document.body.removeEventListener(...a),
    dispatchEvent: (...a) => document.body.dispatchEvent(...a),
    querySelector: (s) => document.body.querySelector(s),
    querySelectorAll: (s) => document.body.querySelectorAll(s),
    getElementById: (id) => document.body.querySelector(`#${id}`),
  };
  document.documentElement.appendChild(document.body);
  document.activeElement = document.body;

  // The surfaces schedule paints through rAF; tests drive it by hand so a
  // frame is a step, not a race.
  const frames = [];
  let now = 0;

  globalThis.document = document;
  globalThis.SVG_NS = SVG_NS;
  // atrium.js routes off location.pathname at import time.
  globalThis.location = {pathname, href: 'chrome://atrium.top-chrome' + pathname};

  // atrium.html loads vim-grammar.js as a classic script ahead of the module
  // bundle, so a shim that left it out would make every route look broken.
  loadClassicScripts();

  // An event target that also holds arbitrary properties, because window is
  // both: the surfaces listen on it, and each install() hangs its push
  // entrypoint there (window.atriumSetSidebar and friends) for C++ to call.
  // Those four functions ARE the inbound half of the ABI, so a test that pushes
  // a snapshot through them is exercising the real path, not a stand-in.
  const win = new ShimElement('window');
  win.location = globalThis.location;
  // Where the surface sits on the desktop. Zero unless a test moves it, which
  // is what lets a screen-coordinate reading be told apart from a client one.
  win.screenX = 0;
  win.screenY = 0;
  globalThis.window = win;
  globalThis.requestAnimationFrame = (fn) => frames.push(fn) && frames.length;
  globalThis.cancelAnimationFrame = (id) => {
    frames[id - 1] = null;
  };
  globalThis.performance = {now: () => now};
  globalThis.Event = ShimEvent;
  globalThis.CustomEvent = ShimEvent;
  globalThis.getComputedStyle = (el) => el.style;

  // edge-glow.js and overscroll-install.js fall back to matchMedia only when
  // reducedMotion is omitted. Every test passes it, so that branch was dead
  // the same way ResizeObserver was.
  let mediaMatches = () => false;
  globalThis.matchMedia = (query) => ({
    media: query,
    get matches() {
      return mediaMatches(query);
    },
    addEventListener() {},
    removeEventListener() {},
    addListener() {},
    removeListener() {},
  });

  // sidebar.js escapes folder ids with CSS.escape before building a
  // selector. Three tests had patched in a spec-inaccurate stand-in because
  // this wasn't here. Follows the CSSOM serialize-an-identifier algorithm.
  globalThis.CSS = {
    escape(value) {
      const str = String(value);
      const len = str.length;
      let out = '';
      for (let i = 0; i < len; i++) {
        const ch = str[i];
        const code = str.charCodeAt(i);
        if (code === 0x0000) {
          out += '�';
          continue;
        }
        if ((code >= 0x0001 && code <= 0x001F) || code === 0x007F ||
            (i === 0 && code >= 0x0030 && code <= 0x0039) ||
            (i === 1 && code >= 0x0030 && code <= 0x0039 && str[0] === '-')) {
          out += '\\' + code.toString(16) + ' ';
          continue;
        }
        if (i === 0 && ch === '-' && len === 1) {
          out += '\\' + ch;
          continue;
        }
        if (code >= 0x0080 || ch === '-' || ch === '_' ||
            (code >= 0x0030 && code <= 0x0039) ||
            (code >= 0x0041 && code <= 0x005A) ||
            (code >= 0x0061 && code <= 0x007A)) {
          out += ch;
          continue;
        }
        out += '\\' + ch;
      }
      return out;
    },
  };

  const sent = [];
  globalThis.chrome = {send: (name, args = []) => sent.push([name, args])};

  // Node's own `navigator` global (fetch/undici) has only a getter, so
  // replacing it needs defineProperty rather than a plain assignment.
  const clipboard = [];
  Object.defineProperty(globalThis, 'navigator', {
    configurable: true,
    value: {
      // The app locale a WebUI document reads, which on a Mac follows the
      // system language. Pinned so a test that formats against it is stable.
      language: 'en-US',
      clipboard: {
        writeText: (text) => {
          clipboard.push(text);
          return Promise.resolve();
        },
      },
    },
  });

  // installChromeRowExtChips branches on `typeof
  // ResizeObserver`, so a leak test needs a real (if inert) constructor to
  // register against and a way to check who's still connected.
  const resizeObservers = [];
  globalThis.ResizeObserver = class {
    constructor(callback) {
      this._callback = callback;
      this._targets = new Set();
      this.disconnected = false;
      resizeObservers.push(this);
    }
    observe(target) {
      this._targets.add(target);
    }
    unobserve(target) {
      this._targets.delete(target);
    }
    disconnect() {
      this.disconnected = true;
      this._targets.clear();
    }
  };

  // toast-surface.js watches its root with one to know when a card arrives
  // or the stack empties. Inert, like ResizeObserver above: no test here
  // depends on the callback firing, only on `new MutationObserver` existing.
  globalThis.MutationObserver = class {
    constructor(callback) {
      this._callback = callback;
    }
    observe() {}
    disconnect() {}
  };

  return {
    document,
    window: win,
    body: document.body,
    sent,
    clipboard,
    resizeObservers,
    // Runs one frame's worth of callbacks. Callbacks queued DURING the frame
    // wait for the next tick, which is what the browser does.
    tick(advanceMs = 16) {
      now += advanceMs;
      const due = frames.splice(0, frames.length);
      for (const fn of due) {
        fn?.(now);
      }
      return due.filter(Boolean).length;
    },
    get pendingFrames() {
      return frames.filter(Boolean).length;
    },
    setNow(ms) {
      now = ms;
    },
    setMatchMedia(fn) {
      mediaMatches = fn;
    },
    reset() {
      frames.length = 0;
      sent.length = 0;
      clipboard.length = 0;
      resizeObservers.length = 0;
      now = 0;
      mediaMatches = () => false;
      document.body.replaceChildren();
      document.body._listeners.clear();
      document.activeElement = document.body;
    },
  };
}
