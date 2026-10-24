// Copyright 2026 The Atrium Authors
//
// Which profile's icon cache this document may read. Every chrome surface
// renders in one shared BrowserContext, so the browser cannot tell an
// incognito window's sidebar from a regular one by the request alone.

let scope = '';

// Set from the snapshot on every push. The browser mints one token per
// profile and gives it only to that profile's surfaces.
// Answers whether the scope changed, so a surface that draws once per push
// knows its paths went stale.
export function setIconScope(token) {
  const next = typeof token === 'string' ? token : '';
  const changed = next !== scope;
  scope = next;
  return changed;
}

// Empty before the first scoped snapshot arrives, and for a row with no url.
// An empty src is never requested, so the row keeps its tint.
export function urlIconPath(url) {
  if (!scope || !url) {
    return '';
  }
  return '/urlicon/' + scope + '/' + encodeURIComponent(url) + '.png';
}

export function isUrlIconPath(path) {
  return typeof path === 'string' && path.startsWith('/urlicon/');
}

// Which carried row icon a new path may take over. A URL's icon may cross a
// profile switch, so its scope is dropped. A tab's never does: the tab id is
// one tab browser-wide. Empty for any other path.
export function iconKey(path) {
  if (isUrlIconPath(path)) {
    return path.replace(/^\/urlicon\/[^/]*\//, 'urlicon/');
  }
  return typeof path === 'string' && path.startsWith('/favicon/') ? path : '';
}

// Scoped for the same reason: tab ids are a small per-process counter, so an
// unscoped path could be walked to read the open tabs of every profile.
export function faviconPath(tabId) {
  if (!scope || !tabId) {
    return '';
  }
  return '/favicon/' + scope + '/' + encodeURIComponent(tabId) + '.png';
}

// Scoped for the same reason: an id a profile already knows would otherwise
// answer whether that extension is cached anywhere else in the browser.
// `rev` changes when the browser stores new bytes. Blink reuses an image it
// holds for the same URL, so without it a late icon load never reaches the page.
// `iconScope` comes with the push: the bytes live with the profile whose bridge
// sent the pins, and the page's own scope can name another profile. The page
// scope never opens extension icons (ExtIconScopeFor), so there is no fallback.
export function extIconPath(id, rev, iconScope) {
  if (!iconScope || !id) {
    return '';
  }
  const path = '/exticon/' + iconScope + '/' + encodeURIComponent(id) + '.png';
  return rev ? path + '?r=' + rev : path;
}
