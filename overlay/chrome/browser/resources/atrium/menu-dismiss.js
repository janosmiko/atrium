// Copyright 2026 The Atrium Authors
// Use of this source code is governed by the Apache License, Version 2.0
// found in the LICENSE file.

// A document only sees presses inside itself, so a click on the page, the
// devbar, or native chrome left a sidebar menu open. The window-level
// mouse-down monitor pushes atriumDismissMenus here instead.

const entries = new Set();

// `isOpen` is read back from the DOM, so a menu removed by any other path
// still reports itself closed.
export function registerMenu({close, isOpen}) {
  // AtriumBrowserWindow::PushDismissMenus is the caller. Installed here rather
  // than at import, so a module that only pulls in a menu helper needs no DOM.
  window.atriumDismissMenus = dismissMenus;
  const entry = {close, isOpen, open: false};
  entries.add(entry);
  return () => entries.delete(entry);
}

// chrome.send rather than the surface's own sender: this reports the document's
// state, not something a control did, and a caller's action log stays readable.
// Only a real transition is worth a message - closeMenu runs on every scroll.
export function refreshMenuState() {
  for (const entry of entries) {
    const open = !!entry.isOpen();
    if (open === entry.open) {
      continue;
    }
    entry.open = open;
    chrome.send(open ? 'sidebarMenuOpened' : 'sidebarMenuClosed', []);
  }
}

// A registration nothing releases keeps its whole root reachable, and no DOM
// anywhere shows one. Read the count directly instead.
export function menuRegistrationCount() {
  return entries.size;
}

export function dismissMenus() {
  for (const entry of [...entries]) {
    entry.close();
  }
  refreshMenuState();
}
