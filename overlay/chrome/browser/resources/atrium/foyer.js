// Copyright 2026 The Atrium Authors
//
// The Foyer route: Little Atrium's minimal top chrome strip (
// traffic lights, URL pill, Move-to-Space). The traffic lights stay native, so
// this renders the pill and the promote affordance only.
//
// Ported from the lab's openFoyer (.fhead in docs/lab/swipe-lab.html), which
// builds its markup with a template literal interpolating a link's title and
// URL. Both are attacker-controlled - an external app supplies them - so the
// port is against DOM APIs, which is what the WebUI CSP requires anyway
// (no innerHTML, no Trusted Types policy).
//
// This replaces LittleAtriumChromeView.swift. displayText below is a port of
// LittleChromeModel.displayText and must stay behaviourally identical to it
// while both exist - the Swift version has unit tests that pin the rules.

const FRESH_TAB_URLS =
    ['', 'about:blank', 'about:blank/', 'chrome://newtab', 'chrome://newtab/'];

// Port of AtriumUI.URLFieldModel.isFreshTabURL.
export function isFreshTabURL(url) {
  return FRESH_TAB_URLS.includes(url);
}

// Port of AtriumUI.LittleChromeModel.displayText: drop a leading https:// and
// a bare trailing slash so the strip reads like an address bar rather than a
// raw URL. Read-only - this never round-trips back into a navigable string.
export function displayText(url) {
  if (isFreshTabURL(url)) {
    return 'New Tab';
  }
  let text = url;
  if (text.startsWith('https://')) {
    text = text.slice('https://'.length);
  }
  if (text.endsWith('/')) {
    text = text.slice(0, -1);
  }
  return text;
}

// A padlock is a claim about transport security, so only https earns one.
// http, file, about, chrome and javascript get none.
//
// Decided from the RAW url: displayText has already dropped the https:// by the
// time the label is built, so the stripped text cannot answer this.
export function isSecureURL(url) {
  return typeof url === 'string' && url.startsWith('https://');
}

function buildLockGlyph() {
  const svg =
      document.createElementNS('http://www.w3.org/2000/svg', 'svg');
  svg.setAttribute('class', 'flock');
  svg.setAttribute('viewBox', '0 0 12 12');
  svg.setAttribute('width', '10');
  svg.setAttribute('height', '10');
  const path =
      document.createElementNS('http://www.w3.org/2000/svg', 'path');
  path.setAttribute(
      'd', 'M3.5 5V3.75a2.5 2.5 0 0 1 5 0V5M2.75 5h6.5v5h-6.5z');
  path.setAttribute('fill', 'none');
  path.setAttribute('stroke', 'currentColor');
  path.setAttribute('stroke-width', '1.1');
  path.setAttribute('stroke-linejoin', 'round');
  svg.appendChild(path);
  return svg;
}

export function renderFoyer(root, snapshot) {
  root.replaceChildren();

  const head = document.createElement('div');
  head.className = 'fhead';

  const dot = document.createElement('span');
  dot.className = 'fdot';
  if (snapshot.tint) {
    dot.style.background = snapshot.tint;
  }

  const title = document.createElement('span');
  title.className = 'ftl';
  // textContent: an external app supplies this title.
  title.textContent = snapshot.title || '';

  const pill = document.createElement('span');
  pill.className = 'furl';
  // The URL arrives from an external app, so a padlock next to a plaintext one
  // is a spoofed security indicator, not a cosmetic slip.
  if (isSecureURL(snapshot.url || '')) {
    pill.appendChild(buildLockGlyph());
  }
  const urlText = document.createElement('span');
  urlText.className = 'furltext';
  urlText.textContent = displayText(snapshot.url || '');
  pill.appendChild(urlText);

  const promote = document.createElement('button');
  promote.className = 'fbtn';
  promote.dataset.foyerPromote = '';
  promote.textContent = 'Move to space';

  head.append(dot, title, pill, promote);
  root.appendChild(head);
  return head;
}

export function installFoyer(root) {
  window.atriumSetFoyer = (snapshot) => {
    renderFoyer(root, snapshot || {});
  };

  root.addEventListener('click', (e) => {
    if (e.target.closest('[data-foyer-promote]')) {
      // The space list itself is not built here: it lives in the Swift model
      // and has no producer yet. The browser opens the menu.
      chrome.send('foyerMoveToSpace', []);
    }
  });

  chrome.send('foyerReady', []);
}
