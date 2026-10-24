// Copyright 2026 The Atrium Authors
//
// no real caller yet - the wake event is not built.

import {edgeGlow} from './edge-glow.js';
import {showToast} from './toast-stack.js';
import {shouldShowToast, dismissAfterMsForTimeout} from './notification-timeout.js';

// `tab` is {id, title, color}. focusTab defaults to the chrome.send a
// sidebar row click already makes.
export function handleSnoozedTabReturn(tab, opts = {}) {
  const {host, side, clock, notificationTimeoutStep = 'keep', focusTab} = opts;

  edgeGlow(tab.color, 'ring', {host, clock});

  if (!shouldShowToast(notificationTimeoutStep)) {
    return;
  }

  const focus = focusTab || ((id) => chrome.send('sidebarTabClicked', [id]));
  showToast(
      {color: tab.color, title: tab.title, onClick: () => focus(tab.id)},
      {host, side, clock,
       dismissAfterMs: dismissAfterMsForTimeout(notificationTimeoutStep)});
}
